/* SPDX-License-Identifier: Apache-2.0 */
#include "network_http.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "http_parser.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { MAX_ADDRESSES = 2, MAX_URL = 1024 };

static bool dns_hostname(const char *host, size_t size) {
    size_t label = 0;
    for (size_t i = 0; i < size; ++i) {
        const unsigned char c = host[i];
        if (c == '.') {
            if (!label || host[i - 1] == '-') return false;
            label = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || (c == '-' && label))) return false;
            if (++label > 63) return false;
        }
    }
    return label && host[size - 1] != '-';
}

esp_http_client_handle_t network_http_client_init(const esp_http_client_config_t *config,
        network_bearer_t bearer, network_http_session_t *session, esp_err_t *error) {
    if (error) *error = ESP_ERR_INVALID_ARG;
    if (!config || !session) return NULL;
    session->addressed = false;
    if (bearer != NETWORK_BEARER_CELLULAR) {
        esp_http_client_handle_t client = esp_http_client_init(config);
        if (error) *error = client ? ESP_OK : ESP_ERR_NO_MEM;
        return client;
    }

    if (!config->url || !config->disable_auto_redirect || config->skip_cert_common_name_check ||
        !(config->crt_bundle_attach || config->cert_pem || config->use_global_ca_store)) return NULL;
    size_t size = strnlen(config->url, MAX_URL);
    struct http_parser_url parsed;
    http_parser_url_init(&parsed);
    if (size == MAX_URL || http_parser_parse_url(config->url, size, 0, &parsed) ||
        !(parsed.field_set & (1 << UF_SCHEMA)) || parsed.field_data[UF_SCHEMA].len != 5 ||
        strncasecmp(config->url + parsed.field_data[UF_SCHEMA].off, "https", 5) ||
        !(parsed.field_set & (1 << UF_HOST)) || (parsed.field_set & (1 << UF_USERINFO))) return NULL;
    size_t host_size = parsed.field_data[UF_HOST].len;
    const char *host = config->url + parsed.field_data[UF_HOST].off;
    if (!host_size || host_size >= sizeof(session->hostname) || !dns_hostname(host, host_size) ||
        ((parsed.field_set & (1 << UF_PORT)) && !parsed.port)) return NULL;
    for (size_t i = 0; i < size; ++i)
        if ((unsigned char)config->url[i] <= ' ' || config->url[i] == '\\' ||
            (unsigned char)config->url[i] == 127) return NULL;
    if (config->common_name && (strlen(config->common_name) != host_size ||
        strncasecmp(config->common_name, host, host_size))) return NULL;

    // OTA clients outlive their open helper; keep SNI and certificate hostname storage in the session.
    if (strlen(session->hostname) != host_size || strncasecmp(session->hostname, host, host_size)) {
        memset(session, 0, sizeof(*session));
        memcpy(session->hostname, host, host_size);
    }
    uint32_t addresses[MAX_ADDRESSES];
    unsigned count = 0;
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *answer = NULL;
    int resolved = getaddrinfo(session->hostname, NULL, &hints, &answer);
    if (resolved) {
        if (answer) freeaddrinfo(answer);
        if (error) *error = resolved == EAI_MEMORY ? ESP_ERR_NO_MEM : ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME;
        return NULL;
    }
    for (struct addrinfo *item = answer; item && count < MAX_ADDRESSES; item = item->ai_next) {
        if (item->ai_family != AF_INET || !item->ai_addr || item->ai_addrlen < sizeof(struct sockaddr_in)) continue;
        uint32_t address = ((const struct sockaddr_in *)item->ai_addr)->sin_addr.s_addr;
        if (!address) continue;
        unsigned i = 0;
        while (i < count && addresses[i] != address) ++i;
        if (i == count) addresses[count++] = address;
    }
    if (answer) freeaddrinfo(answer);
    if (!count) {
        if (error) *error = ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME;
        return NULL;
    }
    unsigned selected = 0;
    for (unsigned i = 0; i < count; ++i)
        if (addresses[i] == session->preferred_address) selected = i;
    if (count > 1 && addresses[selected] == session->failed_address) selected = (selected + 1) % count;
    char address[INET_ADDRSTRLEN], authority[261], connection_url[MAX_URL];
    struct in_addr ipv4 = {.s_addr = addresses[selected]};
    if (!inet_ntop(AF_INET, &ipv4, address, sizeof(address))) return NULL;
    size_t authority_size = host_size;
    if (parsed.field_set & (1 << UF_PORT))
        authority_size = parsed.field_data[UF_PORT].off + parsed.field_data[UF_PORT].len - parsed.field_data[UF_HOST].off;
    if (authority_size >= sizeof(authority)) return NULL;
    memcpy(authority, host, authority_size);
    authority[authority_size] = 0;
    const char *suffix = host + authority_size;
    size_t suffix_size = size - (size_t)(suffix - config->url);
    const char *fragment = memchr(suffix, '#', suffix_size);
    if (fragment) suffix_size = (size_t)(fragment - suffix);
    unsigned port = parsed.field_set & (1 << UF_PORT) ? parsed.port : 443;
    int length = snprintf(connection_url, sizeof(connection_url), "https://%s:%u%s%.*s", address, port,
        suffix_size && suffix[0] == '/' ? "" : "/", (int)suffix_size, suffix);
    if (length < 0 || length >= (int)sizeof(connection_url)) return NULL;
    esp_http_client_config_t selected_config = *config;
    selected_config.url = connection_url;
    selected_config.common_name = session->hostname;
    esp_http_client_handle_t client = esp_http_client_init(&selected_config);
    if (!client) {
        if (error) *error = ESP_ERR_NO_MEM;
        return NULL;
    }
    esp_err_t result = esp_http_client_set_header(client, "Host", authority);
    if (result != ESP_OK) {
        esp_http_client_cleanup(client);
        if (error) *error = result;
        return NULL;
    }
    session->address = addresses[selected];
    session->addressed = true;
    ESP_LOGI("network_http", "HTTPS host=%s address=%s choices=%u", session->hostname, address, count);
    if (error) *error = ESP_OK;
    return client;
}

void network_http_note_result(network_http_session_t *session, bool connected) {
    if (!session || !session->addressed) return;
    if (connected) {
        session->preferred_address = session->address;
        session->failed_address = 0;
    } else {
        session->failed_address = session->address;
        if (session->preferred_address == session->address) session->preferred_address = 0;
    }
    session->addressed = false;
}
