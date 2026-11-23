/*
 * 中文：以虚拟 TLS 和时钟执行生产刷新传输、令牌提交及 OTA 重启门控。
 * English: Run production refresh transport, token commit and the OTA restart gate with fake TLS and time.
 */
#include "cloudzao_partner.h"
#include "cloudzao_wire.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define CONFIG_MOGO_CLOUDZAO_TRANSFER 1
#define ESP_TLS_ERR_SSL_WANT_READ (-1)
#define ESP_TLS_ERR_SSL_WANT_WRITE (-2)
#define ESP_OK 0
#define PICO_UPDATE_READY 1
#define APP_REDRAW_NONE 0

typedef int nvs_handle_t;
typedef struct { bool non_block; int timeout_ms; void (*crt_bundle_attach)(void); const uint8_t *cacert_buf;
    size_t cacert_bytes; } esp_tls_cfg_t;
typedef struct { bool open; } esp_tls_t;
typedef struct { bool network_only; int mode; bool network_ready; } wifi_transfer_status_t;
enum { WIFI_TRANSFER_MODE_STA = 1 };
static const uint8_t oss_root_r3_start[1], oss_root_r3_end[1];
static atomic_bool s_busy, s_cancel, s_media_gone;
static void esp_crt_bundle_attach(void) {}
static bool media_ready(void) { return true; }
static void mbedtls_platform_zeroize(void *p, size_t n) { memset(p, 0, n); }
static void wifi_transfer_get_status(wifi_transfer_status_t *out) {
    *out = (wifi_transfer_status_t){true, WIFI_TRANSFER_MODE_STA, true};
}

enum scenario { CONNECT_CANCEL, MARK_CANCEL, MARK_FAIL, PARTIAL_SEND_CANCEL, REPLY_CANCEL, OTA_WAIT,
    FORCED_REFRESH_CANCEL, REPLY_TIMEOUT, JOB_TIMEOUT };
static enum scenario scenario;
static esp_tls_t tls;
static int64_t clock_us;
static unsigned connects, refresh_writes, reads, marks, saves, commits, restarts, list_writes;
static bool refresh_connection;
static cz_session_record_t durable;
static const cz_session_record_t first = {.state = CZ_SESSION_AUTHORIZED, .issued_at = 1794786600,
    .expires_at = 1794787500, .refresh_expires_at = 1794788500,
    .credential = "pat_fixture.first", .refresh = "prf_fixture.first"};
static const char token_body[] = "{\"accessToken\":\"pat_fixture.new\",\"refreshToken\":\"prf_fixture.new\","
    "\"expiresIn\":900,\"refreshExpiresIn\":2592000}";
static char message[256];
static size_t message_length, message_read;

static int64_t esp_timer_get_time(void) { return clock_us; }
static void vTaskDelay(unsigned ticks) { clock_us += ticks * 1000; }
static esp_tls_t *esp_tls_init(void) { assert(!tls.open); tls.open = true; refresh_connection = false; return &tls; }
static int esp_tls_conn_new_async(const char *host, int length, int port, const esp_tls_cfg_t *cfg, esp_tls_t *conn) {
    assert(length == (int)strlen(host) && port == 443 && cfg->non_block && conn->open);
    ++connects;
    if (scenario == CONNECT_CANCEL) atomic_store(&s_cancel, true);
    return 1;
}
static void esp_tls_conn_destroy(esp_tls_t *conn) { assert(conn->open); conn->open = false; }
static ssize_t esp_tls_conn_write(esp_tls_t *conn, const void *data, size_t len) {
    assert(conn->open && len);
    if (scenario == FORCED_REFRESH_CANCEL && connects == 1) {
        assert(durable.state == CZ_SESSION_AUTHORIZED && !strncmp(data, "GET ", 4));
        ++list_writes;
        return (ssize_t)len;
    }
    assert(durable.state == CZ_SESSION_ROTATING && !durable.refresh[0] && marks == 1);
    assert(refresh_connection || !strncmp(data, "POST ", 5));
    refresh_connection = true;
    if (++refresh_writes == 1 && scenario == PARTIAL_SEND_CANCEL) {
        atomic_store(&s_cancel, true);
        return (ssize_t)(len / 2);
    }
    return (ssize_t)len;
}

static int ota_tick(void);
static ssize_t esp_tls_conn_read(esp_tls_t *conn, void *out, size_t max) {
    assert(conn->open);
    ++reads;
    if (!refresh_connection) {
        const char denied[] = "HTTP/1.1 401 Rejected\r\nContent-Length: 0\r\n\r\n";
        assert(scenario == FORCED_REFRESH_CANCEL && max >= sizeof(denied) - 1);
        memcpy(out, denied, sizeof(denied) - 1);
        return sizeof(denied) - 1;
    }
    if (!message_read && scenario == OTA_WAIT) {
        assert(ota_tick() == APP_REDRAW_NONE && !commits && !restarts && atomic_load(&s_cancel));
    }
    size_t n = message_length - message_read;
    if (scenario == REPLY_TIMEOUT || scenario == JOB_TIMEOUT) {
        atomic_store(&s_cancel, true);
        n = 1;
        clock_us += 4000000;
    } else if (!message_read) n /= 2;
    else if (scenario == REPLY_CANCEL || scenario == FORCED_REFRESH_CANCEL) atomic_store(&s_cancel, true);
    assert(n && n <= max);
    memcpy(out, message + message_read, n);
    message_read += n;
    return (ssize_t)n;
}

#include "cloudzao_refresh_service.inc"

static struct { int state; bool busy; } s_update;
static int pico_online_commit(void) { assert(!atomic_load(&s_busy)); ++commits; return ESP_OK; }
static void esp_restart(void) { assert(!atomic_load(&s_busy)); ++restarts; }
static void pico_online_get_status(void *out) { (void)out; assert(false); }
static int ota_tick(void) {
#include "cloudzao_refresh_ota.inc"
    return -1;
}

static int64_t current_time(void *context) {
    (void)context;
    return first.expires_at - (scenario == FORCED_REFRESH_CANCEL ? 180 : 0);
}
static bool persist(void *context, const cz_session_record_t *record) {
    (void)context;
    if (record->state == CZ_SESSION_ROTATING) {
        assert(connects && !refresh_writes && !saves);
        ++marks;
        if (scenario == MARK_FAIL) return false;
        if (scenario == MARK_CANCEL) atomic_store(&s_cancel, true);
    } else {
        assert(record->state == CZ_SESSION_AUTHORIZED);
        ++saves;
    }
    durable = *record;
    return true;
}
static bool count_list(void *context, const void *bytes, size_t count) {
    (void)context; (void)bytes; (void)count; assert(false); return false;
}

static void run(enum scenario mode) {
    scenario = mode;
    s_update.state = PICO_UPDATE_READY;
    clock_us = 1000;
    connects = refresh_writes = reads = marks = saves = commits = restarts = list_writes = 0;
    message_read = 0;
    durable = first;
    atomic_store(&s_cancel, false);
    atomic_store(&s_busy, true);
    const int size = snprintf(message, sizeof(message), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",
                              strlen(token_body), token_body);
    assert(size > 0 && (size_t)size < sizeof(message));
    message_length = (size_t)size;
    unsigned char chunk[CLOUDZAO_CHUNK];
    char reply[CLOUDZAO_PARTNER_RESPONSE_CAP];
    worker_t worker = {.job = CZ_JOB_PREVIEW, .chunk = chunk,
        .deadline = clock_us + (mode == JOB_TIMEOUT ? 8000000 : 60000000)};
    const cz_ops_t raw = {.context = &worker, .stop = stop, .request = request, .now = current_time};
    cz_partner_t p = {.raw = &raw, .persist = persist, .record = durable, .response = reply};
    const cz_http_request_t req = {.limit = 128};
    const cz_error_t result = cloudzao_partner_request(&p, CZ_LIST, &req, count_list, NULL);
    assert(!tls.open && list_writes == (mode == FORCED_REFRESH_CANCEL ? 1u : 0u) && !commits && !restarts);
    switch (mode) {
        case CONNECT_CANCEL:
            assert(result == CZ_CANCELLED && !marks && !saves && !refresh_writes && !reads);
            assert(durable.state == CZ_SESSION_AUTHORIZED && !strcmp(durable.refresh, first.refresh));
            break;
        case MARK_CANCEL: case MARK_FAIL:
            assert(result == (mode == MARK_CANCEL ? CZ_CANCELLED : CZ_CREDENTIAL_STORAGE));
            assert(marks == 1 && saves == (mode == MARK_CANCEL ? 1u : 0u) && !refresh_writes && !reads);
            assert(durable.state == CZ_SESSION_AUTHORIZED && !strcmp(durable.refresh, first.refresh));
            break;
        case PARTIAL_SEND_CANCEL: case REPLY_CANCEL: case OTA_WAIT: case FORCED_REFRESH_CANCEL:
            assert(result == CZ_CANCELLED && marks == 1 && saves == 1 && message_read == message_length);
            assert(refresh_writes == (mode == PARTIAL_SEND_CANCEL ? 2u : 1u));
            assert(durable.state == CZ_SESSION_AUTHORIZED && !strcmp(durable.refresh, "prf_fixture.new"));
            assert(durable.refresh_expires_at == first.refresh_expires_at);
            break;
        case REPLY_TIMEOUT: case JOB_TIMEOUT:
            assert(result == CZ_REFRESH_INTERRUPTED && marks == 1 && !saves && refresh_writes == 1);
            assert(durable.state == CZ_SESSION_ROTATING && !durable.refresh[0]);
            assert(reads == (mode == REPLY_TIMEOUT ? 5u : 2u));
            break;
    }
    atomic_store(&s_busy, false);
    if (mode == OTA_WAIT) assert(ota_tick() == APP_REDRAW_NONE && commits == 1 && restarts == 1);
}

void cloudzao_refresh_transport_regression(void) {
    for (enum scenario mode = CONNECT_CANCEL; mode <= JOB_TIMEOUT; ++mode) run(mode);
    puts("CloudZao refresh transport: pre-send cancel, bounded completion, late cancel, no follow-up and OTA handoff passed");
}
