/* SPDX-License-Identifier: Apache-2.0
 * Real endpoint selection, deterministic DNS and retained TLS hostname lifetime.
 */
#include "network_http.h"
#include "esp_tls_errors.h"
#include "network_http_test_dns.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FakeHttp {
    char url[1024], host[261];
    esp_http_client_config_t config;
};
static unsigned clients, initializations;
static bool fail_init, fail_header;

static esp_err_t certificate_bundle(void *config) { (void)config; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    ++initializations;
    if (fail_init) return NULL;
    struct FakeHttp *client = calloc(1, sizeof(*client));
    assert(client);
    client->config = *config;
    assert(strlen(config->url) < sizeof(client->url));
    strcpy(client->url, config->url);
    client->config.url = client->url;
    ++clients;
    return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value) {
    assert(!strcmp(key, "Host") && strlen(value) < sizeof(client->host));
    if (fail_header) return ESP_ERR_NO_MEM;
    strcpy(client->host, value);
    return ESP_OK;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    assert(client && clients);
    --clients;
    free(client);
    return ESP_OK;
}
static esp_http_client_config_t config(void) {
    return (esp_http_client_config_t){.url = "https://mogo.mantake.shop/api/v2/components",
        .crt_bundle_attach = certificate_bundle, .disable_auto_redirect = true,
        .timeout_ms = 20000, .is_async = true, .buffer_size = 1024, .buffer_size_tx = 2048,
        .keep_alive_enable = true};
}
static esp_http_client_handle_t open_at(network_http_session_t *session, const char *address) {
    esp_http_client_config_t original = config();
    esp_err_t error = ESP_FAIL;
    esp_http_client_handle_t client = network_http_client_init(&original, NETWORK_BEARER_CELLULAR, session, &error);
    assert(client && error == ESP_OK && !network_http_test_dns_live);
    char url[128]; snprintf(url, sizeof(url), "https://%s:443/api/v2/components", address);
    assert(!strcmp(client->url, url) && !strcmp(client->host, "mogo.mantake.shop"));
    assert(client->config.common_name == session->hostname && !strcmp(session->hostname, "mogo.mantake.shop"));
    assert(!client->config.skip_cert_common_name_check && client->config.disable_auto_redirect);
    assert(client->config.crt_bundle_attach == original.crt_bundle_attach);
    assert(client->config.timeout_ms == original.timeout_ms && client->config.is_async == original.is_async);
    assert(client->config.buffer_size == original.buffer_size && client->config.buffer_size_tx == original.buffer_size_tx);
    assert(client->config.keep_alive_enable == original.keep_alive_enable);
    return client;
}
static void outcome(network_http_session_t *session, const char *address, bool success) {
    esp_http_client_handle_t client = open_at(session, address);
    network_http_note_result(session, success);
    assert(!session->addressed);
    esp_http_client_cleanup(client);
}

static void endpoint_selection(void) {
    network_http_session_t session = {0};
    network_http_test_dns_reset();
    outcome(&session, NETWORK_HTTP_TEST_FIRST, false);
    uint32_t failed = session.failed_address;
    network_http_note_result(&session, true); // A duplicate report must not overwrite the failure.
    assert(session.failed_address == failed && !session.preferred_address);
    outcome(&session, NETWORK_HTTP_TEST_SECOND, true);
    outcome(&session, NETWORK_HTTP_TEST_SECOND, true); // A new request prefers the working DNS answer.
    network_http_test_addresses[0] = NETWORK_HTTP_TEST_SECOND;
    network_http_test_addresses[1] = NETWORK_HTTP_TEST_FIRST;
    outcome(&session, NETWORK_HTTP_TEST_SECOND, false);
    outcome(&session, NETWORK_HTTP_TEST_FIRST, true);

    network_http_test_addresses[0] = "203.0.113.7";
    network_http_test_addresses[1] = "203.0.113.8";
    outcome(&session, "203.0.113.7", false); // Old successful IP has disappeared from DNS.
    outcome(&session, "203.0.113.8", true);
    network_http_test_address_count = 1;
    outcome(&session, "203.0.113.7", false);
    outcome(&session, "203.0.113.7", true); // A single address still permits the existing retry.

    network_http_test_dns_reset();
    session = (network_http_session_t){0};
    network_http_test_address_count = 3;
    network_http_test_addresses[1] = NETWORK_HTTP_TEST_FIRST;
    network_http_test_addresses[2] = NETWORK_HTTP_TEST_SECOND;
    outcome(&session, NETWORK_HTTP_TEST_FIRST, false);
    outcome(&session, NETWORK_HTTP_TEST_SECOND, true); // Deduplicate before applying the bound of two.
    assert(!clients && !network_http_test_dns_live);
}

static void hostname_and_request(void) {
    network_http_session_t session = {0}, other = {0};
    network_http_test_dns_reset();
    outcome(&session, NETWORK_HTTP_TEST_FIRST, false);
    outcome(&other, NETWORK_HTTP_TEST_FIRST, true); // Separate workers do not share endpoint state.
    outcome(&session, NETWORK_HTTP_TEST_SECOND, true);
    esp_http_client_config_t cfg = config();
    cfg.url = "https://mogo.mantake.shop:8443/api/v2/locations?q=%E5%8C%97%E4%BA%AC&page=2#discard";
    int callback_state = 123;
    cfg.user_data = &callback_state;
    esp_err_t error;
    esp_http_client_handle_t client = network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error);
    assert(client && error == ESP_OK && client->config.user_data == cfg.user_data);
    assert(!strcmp(client->url, "https://172.67.138.158:8443/api/v2/locations?q=%E5%8C%97%E4%BA%AC&page=2"));
    assert(!strcmp(client->host, "mogo.mantake.shop:8443"));
    assert(client->config.common_name == session.hostname && !strcmp(client->config.common_name, "mogo.mantake.shop"));
    esp_http_client_cleanup(client);
    const char *urls[] = {"https://mogo.mantake.shop", "https://mogo.mantake.shop?q=1", "https://mogo.mantake.shop/#", "https://mogo.mantake.shop/#unused"};
    const char *expected[] = {"/", "/?q=1", "/", "/"};
    for (unsigned i = 0; i < sizeof(urls) / sizeof(urls[0]); ++i) {
        cfg.url = urls[i];
        client = network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error);
        if (!client) fprintf(stderr, "Rejected test URL %s, error=0x%x\n", urls[i], error);
        assert(client && error == ESP_OK);
        char url[128]; snprintf(url, sizeof(url), "https://172.67.138.158:443%s", expected[i]);
        assert(!strcmp(client->url, url));
        esp_http_client_cleanup(client);
    }
    cfg.url = "https://different.example/api";
    client = network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error);
    assert(client && !strcmp(client->config.common_name, "different.example"));
    assert(!strcmp(client->url, "https://104.21.8.63:443/api") && !session.preferred_address);
    esp_http_client_cleanup(client);

    cfg = config();
    cfg.common_name = "mogo.mantake.shop";
    cfg.crt_bundle_attach = NULL;
    cfg.cert_pem = "custom trusted root";
    client = network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error);
    assert(client && client->config.cert_pem == cfg.cert_pem && !client->config.skip_cert_common_name_check);
    esp_http_client_cleanup(client);
}

static void failures_and_wifi(void) {
    network_http_session_t session = {0};
    network_http_test_dns_reset();
    esp_http_client_config_t cfg = config();
    esp_err_t error;
    for (unsigned i = 0; i < 3; ++i) {
        network_http_test_dns_error = i == 0 ? EAI_FAIL : i == 1 ? EAI_MEMORY : 0;
        network_http_test_address_count = 0;
        assert(!network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error));
        assert(error == (i == 1 ? ESP_ERR_NO_MEM : ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME));
        assert(!session.addressed && !network_http_test_dns_live && !clients);
    }
    network_http_test_dns_reset();
    network_http_test_addresses[0] = "0.0.0.0";
    network_http_test_address_count = 1;
    assert(!network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error));
    assert(error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME);
    network_http_test_dns_reset();
    fail_init = true;
    assert(!network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error));
    assert(error == ESP_ERR_NO_MEM && !session.addressed && !network_http_test_dns_live && !clients);
    fail_init = false; fail_header = true;
    assert(!network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error));
    assert(error == ESP_ERR_NO_MEM && !session.addressed && !network_http_test_dns_live && !clients);
    fail_header = false;
    unsigned dns_before = network_http_test_dns_calls;
    esp_http_client_handle_t client = network_http_client_init(&cfg, NETWORK_BEARER_WIFI, &session, &error);
    assert(client && error == ESP_OK && !strcmp(client->url, cfg.url) && !client->host[0] && !client->config.common_name);
    assert(network_http_test_dns_calls == dns_before && !session.addressed);
    esp_http_client_cleanup(client);
    fail_init = true;
    assert(!network_http_client_init(&cfg, NETWORK_BEARER_WIFI, &session, &error));
    assert(error == ESP_ERR_NO_MEM && network_http_test_dns_calls == dns_before);
    fail_init = false;
}

static void invalid_configuration(void) {
    network_http_test_dns_reset();
    network_http_session_t session = {0};
    esp_err_t error;
    const char *urls[] = {"http://mogo.mantake.shop/api", "https://user:password@mogo.mantake.shop/api",
        "https:///api", "https://mogo.mantake.shop:0/api", "https://mogo.mantake.shop:65536/api",
        "https://mogo.mantake.shop:abc/api", "https://mogo.mantake.shop/a\r\nX-Header:value",
        "https://mogo.mantake.shop/a b", "https://mogo.mantake.shop\\attacker.example/api",
        "https://-invalid.example/api", "https://invalid-.example/api", "https://invalid..example/api",
        "https://[::1]/api", "https://invalid%20host/api", NULL};
    unsigned initializations_before = initializations;
    for (unsigned i = 0; i < sizeof(urls) / sizeof(urls[0]); ++i) {
        esp_http_client_config_t cfg = config(); cfg.url = urls[i];
        assert(!network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error));
        assert(error == ESP_ERR_INVALID_ARG && !session.addressed && !clients);
    }
    char long_url[1100]; memset(long_url, 'a', sizeof(long_url));
    memcpy(long_url, "https://mogo.mantake.shop/", 25); long_url[sizeof(long_url) - 1] = 0;
    esp_http_client_config_t cfg = config(); cfg.url = long_url;
    assert(!network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error));
    for (unsigned i = 0; i < 4; ++i) {
        cfg = config();
        if (i == 0) cfg.skip_cert_common_name_check = true;
        if (i == 1) cfg.disable_auto_redirect = false;
        if (i == 2) cfg.crt_bundle_attach = NULL;
        if (i == 3) cfg.common_name = "attacker.example";
        assert(!network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, &session, &error));
        assert(error == ESP_ERR_INVALID_ARG);
    }
    assert(!network_http_client_init(NULL, NETWORK_BEARER_CELLULAR, &session, &error));
    assert(!network_http_client_init(&cfg, NETWORK_BEARER_CELLULAR, NULL, &error));
    assert(!network_http_test_dns_calls && initializations == initializations_before && !network_http_test_dns_live);
}

int main(void) {
    endpoint_selection();
    hostname_and_request();
    failures_and_wifi();
    invalid_configuration();
    assert(!clients && !network_http_test_dns_live);
    puts("PASS: cellular DNS failover, preferred-address membership, DNS reordering/removal, duplicates, single answer and session isolation");
    puts("PASS: original HTTPS Host/port/path/query, retained SNI/verification hostname, trust configuration, Wi-Fi and failure cleanup");
}
