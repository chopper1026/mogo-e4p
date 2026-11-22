/*
 * 中文：独占 CloudZao 的 HTTPS 和 TF 句柄；不改变无线开关或路由偏好。
 * English: Own CloudZao HTTPS and TF handles without changing radio switches or route preferences.
 */
#include "cloudzao_service.h"
#include "cloudzao_integration.h"
#include "cloudzao_history.h"
#include "esp_attr.h"
#include "board_sd.h"
#include "cloudzao_wire.h"
#include "cloudzao_partner.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "memory_peak.h"
#include "network_service.h"
#include "nvs.h"
#include "time_sync_policy.h"
#include "usb_storage.h"
#include "wifi_transfer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CZ_JOB_US (300LL * 1000000)
#define CZ_IDLE_US (8LL * 1000000)
#define CZ_TOKEN_REPLY_IDLE_US (20LL * 1000000)
#define CZ_STACK 8192u

extern const uint8_t oss_root_r3_start[] asm("_binary_cloudzao_oss_root_r3_pem_start");
extern const uint8_t oss_root_r3_end[] asm("_binary_cloudzao_oss_root_r3_pem_end");

static const char s_integration_id[] = MOGO_CLOUDZAO_BUILD_ID;
_Static_assert(sizeof(s_integration_id) <= CLOUDZAO_TOKEN_MAX + 1, "CloudZao integration ID is too long");

bool cloudzao_available(void) { return sizeof(s_integration_id) > 1; }

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static cz_status_t s_status;
static cz_transfer_t s_transfer;
static EXT_RAM_BSS_ATTR cz_row_t s_rows[CLOUDZAO_LIST_ROWS];
static EXT_RAM_BSS_ATTR cz_queue_t s_queue;
static bool s_queue_ready;
static EXT_RAM_BSS_ATTR cz_history_item_t s_history_items[CLOUDZAO_HISTORY_ROWS];
static cz_history_status_t s_history;
static atomic_bool s_busy, s_cancel, s_media_gone, s_books_changed;
static cz_job_t s_job;

typedef struct {
    int64_t deadline, next_check;
    cz_error_t link;
    cz_job_t job;
    uint16_t preview_http_error, refresh_http_status;
    unsigned char *chunk;
    int64_t rate_from_us;
    uint32_t rate_from_bytes, history_next;
    cz_partner_t partner;
    nvs_handle_t nvs;
} worker_t;

static void progress(void *context, const cz_status_t *status) {
    (void)context;
    portENTER_CRITICAL(&s_lock);
    s_status = *status;
    portEXIT_CRITICAL(&s_lock);
}

cz_status_t cloudzao_status(void) {
    portENTER_CRITICAL(&s_lock);
    cz_status_t result = s_status;
    portEXIT_CRITICAL(&s_lock);
    return result;
}

cz_transfer_t cloudzao_transfer_status(void) {
    portENTER_CRITICAL(&s_lock);
    cz_transfer_t result = s_transfer;
    portEXIT_CRITICAL(&s_lock);
    if (result.last_data_us && esp_timer_get_time() - result.last_data_us >= 2000000) result.rate = 0;
    return result;
}

bool cloudzao_transfer_row(unsigned index, cz_row_t *out) {
    portENTER_CRITICAL(&s_lock);
    const bool valid = index < s_transfer.rows;
    if (valid) *out = s_rows[index];
    portEXIT_CRITICAL(&s_lock);
    return valid;
}

bool cloudzao_offer(unsigned index, cz_row_t *out, cz_kind_t *kind) {
    portENTER_CRITICAL(&s_lock);
    const bool valid = s_queue_ready && index < s_queue.count;
    if (valid) {
        if (out) *out = s_queue.offers[index].file;
        if (kind) *kind = s_queue.offers[index].kind;
    }
    portEXIT_CRITICAL(&s_lock);
    return valid;
}

void cloudzao_discard_queue(void) {
    portENTER_CRITICAL(&s_lock);
    s_queue_ready = false;
    portEXIT_CRITICAL(&s_lock);
}

cz_history_status_t cloudzao_history_status(void) {
    portENTER_CRITICAL(&s_lock);
    cz_history_status_t result = s_history;
    portEXIT_CRITICAL(&s_lock);
    return result;
}

bool cloudzao_history_item(unsigned index, cz_history_item_t *out) {
    portENTER_CRITICAL(&s_lock);
    const bool valid = index < s_history.count;
    if (valid) *out = s_history_items[index];
    portEXIT_CRITICAL(&s_lock);
    return valid;
}

void cloudzao_cancel(void) { atomic_store(&s_cancel, true); }
void cloudzao_media_lost(void) { atomic_store(&s_media_gone, true); cloudzao_cancel(); }
bool cloudzao_take_books_changed(void) { return atomic_exchange(&s_books_changed, false); }

bool cloudzao_quiesce(unsigned timeout_ms) {
    cloudzao_cancel();
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (atomic_load(&s_busy)) {
        if (esp_timer_get_time() >= deadline) return false;
        vTaskDelay(1);
    }
    return true;
}

static bool media_ready(void) {
    return !usb_storage_active() && board_sd_is_mounted();
}

static cz_error_t stop(void *context) {
    worker_t *worker = context;
    int64_t now = esp_timer_get_time();
    if (now >= worker->next_check) {
        worker->next_check = now + 200000;
        wifi_transfer_status_t wifi = {0};
        wifi_transfer_get_status(&wifi);
        if (worker->job == CZ_JOB_DOWNLOAD && !media_ready()) atomic_store(&s_media_gone, true);
        if (!wifi.network_only || wifi.mode != WIFI_TRANSFER_MODE_STA || !wifi.network_ready)
            worker->link = CZ_WIFI;
    }
    if (atomic_load(&s_media_gone)) return CZ_MEDIA;
    if (atomic_load(&s_cancel)) return CZ_CANCELLED;
    if (now >= worker->deadline) return CZ_TIMEOUT;
    return worker->link;
}

static void row_changed(void *context, unsigned index, const cz_row_t *file, bool active) {
    (void)context;
    portENTER_CRITICAL(&s_lock);
    if (index < CLOUDZAO_LIST_ROWS) {
        s_rows[index] = *file;
        if (s_transfer.rows <= index) s_transfer.rows = index + 1;
    }
    if (file->state != CZ_ROW_WAIT) {
        s_transfer.current = index;
        s_transfer.active = active;
        s_transfer.file = *file;
        s_transfer.received = s_transfer.rate = 0;
        s_transfer.last_data_us = 0;
    }
    ++s_transfer.revision;
    portEXIT_CRITICAL(&s_lock);
}

static void stage_changed(void *context, cz_row_state_t state, size_t received) {
    worker_t *worker = context;
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    const bool same = state == s_transfer.file.state;
    if (!same) {
        worker->rate_from_us = now;
        worker->rate_from_bytes = (uint32_t)received;
        s_transfer.rate = 0;
        s_transfer.file.state = state;
        if (s_transfer.current < s_transfer.rows) s_rows[s_transfer.current].state = state;
    }
    if (state == CZ_ROW_DOWNLOAD && received > s_transfer.received) {
        s_transfer.last_data_us = now;
        const int64_t elapsed = now - worker->rate_from_us;
        if (elapsed >= 1000000) {
            s_transfer.rate = (uint32_t)(((uint64_t)(received - worker->rate_from_bytes) * 1000000) / elapsed);
            worker->rate_from_us = now;
            worker->rate_from_bytes = (uint32_t)received;
        }
    }
    s_transfer.received = (uint32_t)received;
    if (!same) ++s_transfer.revision;
    portEXIT_CRITICAL(&s_lock);
}

static void record_result(void *context, const cz_row_t *row) {
    worker_t *worker = context;
    const time_t now = time(NULL);
    const uint32_t utc = now >= 1577836800 && (uint64_t)now <= UINT32_MAX ? (uint32_t)now : 0;
    const cz_ops_t ops = {.context = worker, .root = "/sdcard", .stop = stop};
    (void)cloudzao_history_record(&ops, &worker->history_next, row, utc);
}

static cz_error_t stop_history(void *context) {
    (void)context;
    if (atomic_load(&s_media_gone) || !media_ready()) return CZ_MEDIA;
    return atomic_load(&s_cancel) ? CZ_CANCELLED : CZ_OK;
}

static void run_history_job(void *argument, bool clear) {
    memory_peak_token_t peak = (memory_peak_token_t)(uintptr_t)argument;
    unsigned count = 0;
    const cz_ops_t ops = {.root = "/sdcard", .stop = stop_history};
    // 读取时不发布未完成的行；清除失败且卡仍在时保留原列表。
    // Do not publish partial reads; keep the previous list if a clear fails with the card still present.
    const cz_error_t error = clear ? cloudzao_history_clear(&ops) : cloudzao_history_read(&ops, s_history_items, &count);
    memory_peak_finish(peak);
    portENTER_CRITICAL(&s_lock);
    if (clear && error != CZ_OK && error != CZ_MEDIA) count = s_history.count;
    s_history = (cz_history_status_t){.count = count, .error = error};
    portEXIT_CRITICAL(&s_lock);
    atomic_store(&s_busy, false);
    vTaskDelete(NULL);
}

static void run_history(void *argument) { run_history_job(argument, false); }
static void run_history_clear(void *argument) { run_history_job(argument, true); }

static cz_error_t start_history(bool clear) {
    bool idle = false;
    if (!atomic_compare_exchange_strong(&s_busy, &idle, true)) return CZ_RESOURCES;
    memory_peak_token_t peak = 0;
    if (memory_peak_try_begin(MEMORY_PEAK_CLOUDZAO, &peak) == ESP_OK &&
        memory_peak_check((memory_peak_heap_t){.internal_free = CZ_STACK + 4096, .internal_block = CZ_STACK}) == ESP_OK) {
        atomic_store(&s_cancel, false);
        atomic_store(&s_media_gone, false);
        portENTER_CRITICAL(&s_lock);
        const cz_history_status_t previous = s_history;
        s_history = (cz_history_status_t){.busy = true, .count = clear ? previous.count : 0};
        portEXIT_CRITICAL(&s_lock);
        if (xTaskCreate(clear ? run_history_clear : run_history, "cloudzao_history", CZ_STACK,
                        (void *)(uintptr_t)peak, 4, NULL) == pdPASS) return CZ_OK;
        portENTER_CRITICAL(&s_lock);
        s_history = previous;
        portEXIT_CRITICAL(&s_lock);
    }
    memory_peak_finish(peak);
    atomic_store(&s_busy, false);
    return CZ_RESOURCES;
}

cz_error_t cloudzao_start_history(void) { return start_history(false); }
cz_error_t cloudzao_start_history_clear(void) { return start_history(true); }

static uint64_t free_bytes(void *context) {
    (void)context;
    uint64_t total, available;
    return esp_vfs_fat_info("/sdcard", &total, &available) == ESP_OK ? available : 0;
}

static void book_added(void *context) { (void)context; atomic_store(&s_books_changed, true); }

static bool active(worker_t *worker, int64_t idle) {
    return esp_timer_get_time() < idle && stop(worker) == CZ_OK;
}

static bool would_block(ssize_t result) {
    return result == ESP_TLS_ERR_SSL_WANT_READ || result == ESP_TLS_ERR_SSL_WANT_WRITE;
}

static cz_error_t request(void *context, cz_request_t operation, const cz_http_request_t *req,
                          cz_sink_t sink, void *sink_context) {
    worker_t *worker = context;
    char host[254];
    size_t length = cloudzao_wire_request(operation, req, host, sizeof(host), (char *)worker->chunk, CLOUDZAO_CHUNK);
    esp_tls_t *tls = length ? esp_tls_init() : NULL;
    cz_error_t error = !length ? CZ_INVALID : !tls ? CZ_RESOURCES : CZ_TIMEOUT;
    unsigned http_status = 0;
    if (tls) {
        // OSS 携带交叉签名链；直接信任官方 R3 根，由常规路径验证在 R3 截止。
        // OSS serves a cross-signed chain; the normal verifier can stop at the explicit official R3 trust anchor.
        const bool oss = operation == CZ_DOWNLOAD && cloudzao_oss_host(host, strlen(host));
        esp_tls_cfg_t cfg = {
            .non_block = true, .timeout_ms = 200,
            .crt_bundle_attach = oss ? NULL : esp_crt_bundle_attach,
            .cacert_buf = oss ? oss_root_r3_start : NULL,
            .cacert_bytes = oss ? (size_t)(oss_root_r3_end - oss_root_r3_start) : 0,
        };
        int64_t idle = esp_timer_get_time() + CZ_IDLE_US;
        bool connected = false;
        while (active(worker, idle)) {
            int result = esp_tls_conn_new_async(host, (int)strlen(host), 443, &cfg, tls);
            if (result == 1) { connected = true; break; }
            if (result < 0) { error = CZ_SERVER; break; }
            vTaskDelay(1);
        }
        if (connected) {
            size_t sent = 0;
            idle = esp_timer_get_time() + CZ_IDLE_US;
            while (sent < length && active(worker, idle)) {
                if (req->maybe_sent) *req->maybe_sent = true;
                ssize_t written = esp_tls_conn_write(tls, worker->chunk + sent, length - sent);
                if (written > 0) {
                    sent += (size_t)written;
                    idle = esp_timer_get_time() + CZ_IDLE_US;
                } else if (!written || would_block(written)) vTaskDelay(1);
                else { error = CZ_SERVER; break; }
            }
            mbedtls_platform_zeroize(worker->chunk, CLOUDZAO_CHUNK);
            if (sent == length) {
                // 单次兑换与轮换刷新均给令牌回包更长的无进展时限。
                // Allow more reply time for both single-use exchange and refresh rotation.
                const bool token_reply = operation == CZ_EXCHANGE || operation == CZ_REFRESH;
                const int64_t reply_idle_us = token_reply ? CZ_TOKEN_REPLY_IDLE_US : CZ_IDLE_US;
                cz_response_t response;
                cloudzao_wire_begin(&response, operation, req->limit, sink, sink_context);
                idle = esp_timer_get_time() + reply_idle_us;
                while (active(worker, idle)) {
                    ssize_t n = esp_tls_conn_read(tls, worker->chunk, CLOUDZAO_CHUNK);
                    if (would_block(n)) { vTaskDelay(1); continue; }
                    if (n < 0) { error = CZ_SERVER; break; }
                    bool valid = cloudzao_wire_feed(&response, worker->chunk, (size_t)n);
                    http_status = response.parser.status_code;
                    if (!valid) { error = response.error; break; }
                    if (response.done) { error = response.error; break; }
                    idle = esp_timer_get_time() + reply_idle_us;
                }
            }
        }
        esp_tls_conn_destroy(tls);
    }
    // 请求缓冲区可能仍含未发送的请求头；每条请求统一擦除。/ Wipe any unsent headers on every request exit.
    mbedtls_platform_zeroize(worker->chunk, CLOUDZAO_CHUNK);
    cz_error_t interrupted = stop(worker);
    if (operation == CZ_REFRESH) worker->refresh_http_status = http_status;
    if (worker->job == CZ_JOB_PREVIEW)
        worker->preview_http_error = interrupted == CZ_OK && error == CZ_SERVER &&
            http_status >= 300 && http_status < 600 ? (uint16_t)http_status : 0;
    return interrupted == CZ_OK ? error : interrupted;
}

#define CZ_NVS_NAMESPACE "cloudzao"
#define CZ_NVS_KEY "partner"
#define CZ_NVS_VERSION 1u
#define CZ_NVS_NOTICE_KEY "notice_rev"
#define CZ_NOTICE_REVISION 1u

bool cloudzao_notice_acknowledged(void) {
    nvs_handle_t nvs;
    if (nvs_open(CZ_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    uint8_t revision = 0;
    const esp_err_t err = nvs_get_u8(nvs, CZ_NVS_NOTICE_KEY, &revision);
    nvs_close(nvs);
    return err == ESP_OK && revision == CZ_NOTICE_REVISION;
}

bool cloudzao_acknowledge_notice(void) {
    nvs_handle_t nvs;
    if (nvs_open(CZ_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(nvs, CZ_NVS_NOTICE_KEY, CZ_NOTICE_REVISION);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err == ESP_OK;
}

typedef struct {
    uint32_t version;
    char integration[CLOUDZAO_TOKEN_MAX + 1];
    cz_session_record_t record;
} stored_session_t;

static bool persist_session(void *context, const cz_session_record_t *record) {
    worker_t *worker = context;
    stored_session_t stored = {.version = CZ_NVS_VERSION, .record = *record};
    memcpy(stored.integration, s_integration_id, sizeof(s_integration_id));
    const esp_err_t err = nvs_set_blob(worker->nvs, CZ_NVS_KEY, &stored, sizeof(stored));
    cloudzao_partner_wipe(&stored, sizeof(stored));
    return err == ESP_OK && nvs_commit(worker->nvs) == ESP_OK;
}

static bool load_session(worker_t *worker) {
    stored_session_t stored = {0};
    size_t size = sizeof(stored);
    const esp_err_t err = nvs_get_blob(worker->nvs, CZ_NVS_KEY, &stored, &size);
    bool valid = err == ESP_ERR_NVS_NOT_FOUND || (err == ESP_OK && size == sizeof(stored) &&
        stored.version == CZ_NVS_VERSION && stored.integration[CLOUDZAO_TOKEN_MAX] == 0 &&
        cloudzao_partner_record_valid(&stored.record));
    if (valid && err == ESP_OK && !strcmp(stored.integration, s_integration_id))
        worker->partner.record = stored.record;
    cloudzao_partner_wipe(&stored, sizeof(stored));
    return valid;
}

static int64_t current_utc(void *context) {
    (void)context;
    const time_t now = time(NULL);
    return now > 0 && (uint64_t)now <= UINT32_MAX && app_time_sync_clock_valid((uint32_t)now) ? now : 0;
}

static bool pair_job(cz_job_t job) { return job == CZ_JOB_BIND || job == CZ_JOB_POLL || job == CZ_JOB_REBIND; }

static void log_refresh_failure(void *context, cz_error_t cause, bool maybe_sent, bool rebind_required) {
    worker_t *worker = context;
    const char *reason;
    switch (cause) {
        case CZ_TIMEOUT: reason = "timeout"; break;
        case CZ_INVALID: reason = "invalid_response"; break;
        case CZ_CREDENTIAL_STORAGE: reason = "storage"; break;
        case CZ_AUTH: case CZ_REFRESH_REUSED: reason = "rejected"; break;
        case CZ_SLOW_DOWN: reason = "rate_limit"; break;
        case CZ_CANCELLED: case CZ_MEDIA: reason = "interrupted"; break;
        default: reason = "request_failed"; break;
    }
    ESP_LOGW("cloudzao", "refresh: reason=%s cause=%d http=%u maybe_sent=%u rebind_required=%u",
             reason, (int)cause, maybe_sent ? (unsigned)worker->refresh_http_status : 0u,
             (unsigned)maybe_sent, (unsigned)rebind_required);
}

static cz_error_t partner_request(void *context, cz_request_t operation, const cz_http_request_t *req,
                                  cz_sink_t sink, void *sink_context) {
    worker_t *worker = context;
    return cloudzao_partner_request(&worker->partner, operation, req, sink, sink_context);
}

static void run(void *argument) {
    memory_peak_token_t peak = (memory_peak_token_t)(uintptr_t)argument;
    worker_t worker = {.deadline = esp_timer_get_time() + CZ_JOB_US, .job = s_job};
    char mac[18];
    uint8_t bytes[6];
    network_lease_t lease = {0};
    cz_status_t result = {.error = stop(&worker), .rebind_retry = worker.job == CZ_JOB_REBIND};
    char *json = NULL;
    if (result.error == CZ_OK && !cloudzao_token_valid(s_integration_id)) result.error = CZ_INTEGRATION;
    if (result.error == CZ_OK && !current_utc(NULL)) result.error = CZ_CLOCK;
    if (result.error == CZ_OK && network_service_borrow_ready_wifi(&lease) != ESP_OK) result.error = CZ_WIFI;
    if (result.error == CZ_OK && pair_job(worker.job) && esp_read_mac(bytes, ESP_MAC_WIFI_STA) != ESP_OK) result.error = CZ_WIFI;
    if (result.error == CZ_OK) {
        worker.chunk = heap_caps_malloc(CLOUDZAO_CHUNK + CLOUDZAO_PARTNER_RESPONSE_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!pair_job(worker.job)) json = heap_caps_malloc(CLOUDZAO_JSON_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!worker.chunk || (!pair_job(worker.job) && !json)) result.error = CZ_RESOURCES;
    }
    if (result.error == CZ_OK && (nvs_open(CZ_NVS_NAMESPACE, NVS_READWRITE, &worker.nvs) != ESP_OK || !load_session(&worker)))
        result.error = CZ_CREDENTIAL_STORAGE;
    if (result.error == CZ_OK) {
        const cz_ops_t raw = {.context = &worker, .stop = stop, .request = request, .now = current_utc};
        worker.partner.raw = &raw;
        worker.partner.response = (char *)worker.chunk + CLOUDZAO_CHUNK;
        worker.partner.persist = persist_session;
        worker.partner.refresh_failed = log_refresh_failure;
        if (pair_job(worker.job)) {
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
            result = cloudzao_partner_pair(&worker.partner, worker.job, s_integration_id,
                                           mac, esp_app_get_description()->version);
        } else {
            const cz_ops_t ops = {.context = &worker, .root = "/sdcard", .stop = stop, .request = partner_request,
                .now = current_utc, .free_bytes = free_bytes, .book_added = book_added, .progress = progress,
                .row = row_changed, .stage = stage_changed, .record = record_result};
            result = cloudzao_run(&ops, worker.job, &s_queue, json, worker.chunk);
        }
    }
    if (worker.nvs) nvs_close(worker.nvs);
    cloudzao_partner_wipe(&worker.partner.record, sizeof(worker.partner.record));
    free(json);
    free(worker.chunk);
    network_service_release(&lease);
    memory_peak_finish(peak);
    result.busy = false;
    portENTER_CRITICAL(&s_lock);
    if (worker.job == CZ_JOB_PREVIEW) {
        if (result.error == CZ_SERVER) result.preview_http_error = worker.preview_http_error;
        if (result.error == CZ_OK && atomic_load(&s_cancel)) result.error = CZ_CANCELLED;
        s_queue_ready = result.error == CZ_OK;
    }
    s_status = result;
    atomic_store(&s_busy, false);
    portEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

cz_error_t cloudzao_start(cz_job_t job, const cz_selection_t *selection) {
    if (!pair_job(job) && job != CZ_JOB_PREVIEW && job != CZ_JOB_DOWNLOAD) return CZ_INVALID;
    if (job == CZ_JOB_DOWNLOAD && !media_ready()) return CZ_MEDIA;
    bool idle = false;
    if (!atomic_compare_exchange_strong(&s_busy, &idle, true)) return CZ_RESOURCES;
    if (job == CZ_JOB_DOWNLOAD) {
        bool checked = false;
        portENTER_CRITICAL(&s_lock);
        if (selection && s_queue_ready) {
            for (unsigned i = 0; i < s_queue.count && !checked; ++i)
                checked = selection->checked[i] && s_queue.offers[i].kind != CZ_KIND_NONE;
        }
        portEXIT_CRITICAL(&s_lock);
        if (!checked) { atomic_store(&s_busy, false); return CZ_INVALID; }
    }
    memory_peak_token_t peak = 0;
    bool restore_queue = false;
    const size_t json_size = pair_job(job) ? 0 : CLOUDZAO_JSON_MAX + 1;
    const size_t chunk_size = CLOUDZAO_CHUNK + CLOUDZAO_PARTNER_RESPONSE_CAP;
    if (memory_peak_try_begin(MEMORY_PEAK_CLOUDZAO, &peak) != ESP_OK ||
        memory_peak_check((memory_peak_heap_t){.internal_free = CZ_STACK + 8 * 1024, .internal_block = CZ_STACK,
            .psram_free = json_size + chunk_size + 64 * 1024,
            .psram_block = json_size ? json_size : chunk_size}) != ESP_OK)
        goto failed;
    atomic_store(&s_cancel, false);
    atomic_store(&s_media_gone, false);
    s_job = job;
    if (!pair_job(job)) {
        portENTER_CRITICAL(&s_lock);
        restore_queue = s_queue_ready;
        s_queue_ready = false;
        if (job == CZ_JOB_DOWNLOAD) {
            s_queue.selection = *selection;
            s_transfer = (cz_transfer_t){0};
        }
        portEXIT_CRITICAL(&s_lock);
    }
    progress(NULL, &(cz_status_t){.busy = true});
    if (xTaskCreate(run, "cloudzao", CZ_STACK, (void *)(uintptr_t)peak, 4, NULL) == pdPASS) return CZ_OK;
failed:
    memory_peak_finish(peak);
    if (restore_queue && job == CZ_JOB_DOWNLOAD) {
        portENTER_CRITICAL(&s_lock);
        s_queue_ready = true;
        portEXIT_CRITICAL(&s_lock);
    }
    progress(NULL, &(cz_status_t){.error = CZ_RESOURCES});
    atomic_store(&s_busy, false);
    return CZ_RESOURCES;
}
