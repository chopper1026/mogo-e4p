/*
 * 中文：平台天气代理、独占短连和本地两日缓存；搜索批次仅暂存RAM，不保存密钥或完整城市字典。
 * English: Platform weather proxy, exclusive short connections and two-day cache; search batches stay in RAM, without keys or a full city dictionary.
 * 冻结：不抢占传书/升级；页面离开、锁屏、关机和快捷WiFi前必须取消等待。
 * Frozen: Never steal transfer/update sessions; cancel and join before exit, lock, shutdown or quick WiFi.
 */
#include "weather_service.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_tls_errors.h"
#include "esp_timer.h"
#include "epdiy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ota_online.h"
#include "device_power.h"
#include "ble_page_turner.h"
#include "time_service.h"
#include "wifi_transfer.h"
#include "network_service.h"
#include "network_http.h"
#include "../diagnostics/network_diagnostics.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifndef CONFIG_PICO_COMPONENT_API_ORIGIN
#define CONFIG_PICO_COMPONENT_API_ORIGIN "https://mogo.mantake.shop"
#endif
#define WEATHER_RETRY_US (5LL * 60 * 1000000)
#define WEATHER_SESSION_US (45LL * 1000000)
#define WEATHER_CONNECT_US (6LL * 1000000)
#define WEATHER_RESPONSE_US (5LL * 1000000)
#define WEATHER_CELLULAR_CONNECT_US (20LL * 1000000)
#define WEATHER_CELLULAR_RESPONSE_US (10LL * 1000000)
#define WEATHER_IO_POLL_MS 200
#define WEATHER_REFRESH_SECONDS (6LL * 60 * 60)
typedef struct weather_search_batch {
    struct weather_search_batch *next;
    unsigned page, count;
    weather_location_t locations[WEATHER_SEARCH_BATCH_SIZE];
} weather_search_batch_t;
typedef struct {
    uint32_t schema;
    uint8_t enabled, has_city, auto_refresh;
    weather_location_t city;
} weather_preferences_t;
typedef struct {
    uint32_t schema;
    weather_forecast_t forecast;
} weather_cache_t;
typedef struct {
    int64_t utc, at_us;
} weather_clock_t;
typedef struct {
    weather_clock_t clock;
    bool seen;
} weather_date_header_t;
typedef struct {
    app_weather_state_t state;
    char notice[97];
    bool has_city, has_forecast, clear_cache;
    weather_location_t city;
    weather_forecast_t forecast;
    weather_clock_t clock;
    unsigned total;
    weather_search_batch_t *batches;
} weather_result_t;
static app_weather_view_t s_view;
static weather_result_t s_result;
static app_weather_request_t s_request;
static char s_query[193];
static char s_cached_query[193];
static weather_search_batch_t *s_search_batches;
static bool s_search_total_known, s_requested_total_known;
static unsigned s_search_total, s_requested_total, s_requested_missing[2], s_requested_missing_count;
static weather_location_t s_requested_city;
static bool s_requested_has_city, s_loaded, s_pending;
static bool s_auto_attempted;
static unsigned s_search_generation, s_requested_search_generation, s_requested_page;
static atomic_bool s_busy, s_cancel;
static SemaphoreHandle_t s_done;
static int64_t s_next_attempt, s_deadline;
static int32_t s_display_day;
static bool s_display_valid;
static weather_clock_t s_clock, s_requested_clock;
static network_http_session_t s_http_session;

static int64_t clock_now(const weather_clock_t *clock) {
    if (!clock->utc) return time(NULL);
    int64_t elapsed = esp_timer_get_time() - clock->at_us;
    return elapsed >= 0 ? clock->utc + elapsed / 1000000 : 0;
}
int64_t app_weather_now(void) { return clock_now(&s_clock); }

static bool write_blob(const char *key, const void *value, size_t size) {
    if (!epd_flash_begin()) return false;
    nvs_handle_t h;
    esp_err_t err = nvs_open("kiiko_weather", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = value ? nvs_set_blob(h, key, value, size) : nvs_erase_key(h, key);
        if (err == ESP_ERR_NVS_NOT_FOUND && !value) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    epd_flash_end();
    return err == ESP_OK;
}
static bool save_preferences(bool enabled, bool auto_refresh, bool has_city, const weather_location_t *city) {
    weather_preferences_t p = {.schema = 3, .enabled = enabled, .has_city = has_city, .auto_refresh = auto_refresh};
    if (has_city) p.city = *city;
    return write_blob("prefs", &p, sizeof(p));
}
void app_weather_init(void) {
    if (s_loaded) return;
    s_loaded = true;
    s_clock = (weather_clock_t){0};
    s_view.state = APP_WEATHER_IDLE;
    s_view.auto_refresh = true;
    nvs_handle_t h;
    if (nvs_open("kiiko_weather", NVS_READONLY, &h) == ESP_OK) {
        weather_preferences_t p = {0}; size_t size = sizeof(p);
        if (nvs_get_blob(h, "prefs", &p, &size) == ESP_OK && size == sizeof(p) && p.schema == 3 &&
            p.enabled <= 1 && p.has_city <= 1 && p.auto_refresh <= 1 &&
            (!p.has_city || weather_location_valid(&p.city))) {
            s_view.enabled = p.enabled; s_view.has_city = p.has_city; s_view.city = p.city;
            s_view.auto_refresh = p.auto_refresh;
        }
        weather_cache_t cache = {0};size = sizeof(cache);
        if (s_view.has_city && nvs_get_blob(h, "cache", &cache, &size) == ESP_OK &&
            size == sizeof(cache) && cache.schema == 2 && weather_forecast_valid(&cache.forecast, time(NULL)) &&
            !strcmp(s_view.city.id, cache.forecast.location.id)) {
            s_view.forecast = cache.forecast;s_view.has_forecast = true;
        }
        nvs_close(h);
    }
    s_display_day = weather_local_day(time(NULL));
    s_display_valid = s_view.has_forecast;
}
const app_weather_view_t *app_weather_view(void) { return &s_view; }
bool app_weather_busy(void) { return atomic_load(&s_busy); }
static bool cancelled(void) {
    return atomic_load(&s_cancel) || esp_timer_get_time() >= s_deadline;
}
static void result_notice(app_weather_state_t state, const char *message) {
    s_result.state = state; snprintf(s_result.notice, sizeof(s_result.notice), "%s", message);
}
static void api_error(int status, const char *body) {
    char code[65];weather_error_code(body,strlen(body),code,sizeof(code));
    if (!strcmp(code, "COMPONENT_DISABLED")) {
        s_result.clear_cache = true; result_notice(APP_WEATHER_DISABLED, "天气服务暂未开放");
    } else if (!strcmp(code, "CITY_SELECTION_REQUIRED")) {
        result_notice(APP_WEATHER_CITY_REQUIRED, "未能确定城市，请手动选择");
    } else if (status == 429 || !strcmp(code, "WEATHER_COOLDOWN") ||
               !strcmp(code, "WEATHER_REFRESH_IN_PROGRESS")) {
        result_notice(APP_WEATHER_UNAVAILABLE, "请求较多，请稍后重试");
    } else result_notice(APP_WEATHER_UNAVAILABLE, "天气服务暂不可用，请稍后重试");
}
static int http_wait_ms(int64_t until) {
    if (until > s_deadline) until = s_deadline;
    int64_t remaining = (until - esp_timer_get_time() + 999) / 1000;
    return remaining > WEATHER_IO_POLL_MS ? WEATHER_IO_POLL_MS : (int)remaining;
}
static esp_err_t weather_date_header(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_HEADER || !event->user_data || !event->header_key ||
        strcasecmp(event->header_key, "Date")) return ESP_OK;
    weather_date_header_t *header = event->user_data;
    header->clock.utc = header->seen ? 0 : weather_http_date(event->header_value);
    header->clock.at_us = esp_timer_get_time();
    header->seen = true;
    return ESP_OK;
}
static bool http_get_once(const char *path, char *body, weather_clock_t *response_clock,
                          network_bearer_t bearer, bool *retryable) {
    *retryable = false;
    body[0] = 0;
    weather_date_header_t header = {0};
    if (response_clock) *response_clock = (weather_clock_t){0};
    char url[1024];
    const char *origin = CONFIG_PICO_COMPONENT_API_ORIGIN;
    size_t origin_length = strlen(origin);
    if (strncmp(origin, "https://", 8) || origin_length < 9 || origin_length > 200 ||
        strchr(origin + 8, '/') || strchr(origin + 8, '?') || strchr(origin + 8, '#') ||
        strchr(origin + 8, '@') || cancelled()) return false;
    if (snprintf(url, sizeof(url), "%s/api/v2%s", origin, path) >= (int)sizeof(url)) return false;
    const int64_t connect_us = bearer == NETWORK_BEARER_CELLULAR
        ? WEATHER_CELLULAR_CONNECT_US : WEATHER_CONNECT_US;
    const int64_t response_us = bearer == NETWORK_BEARER_CELLULAR
        ? WEATHER_CELLULAR_RESPONSE_US : WEATHER_RESPONSE_US;
    int64_t started = esp_timer_get_time();
    esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = (int)(connect_us / 1000), .is_async = true,
        .disable_auto_redirect = true, .buffer_size = 1024, .buffer_size_tx = 1024,
        .event_handler = response_clock ? weather_date_header : NULL, .user_data = &header,
    };
    esp_err_t init_error;
    esp_http_client_handle_t client = network_http_client_init(&config, bearer, &s_http_session, &init_error);
    if (!client) {
        *retryable = init_error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME && !cancelled();
        network_diagnostics_http_failure("weather", NULL, "init", init_error, bearer);
        return false;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    bool ok = false;
    bool transport_error;
    const char *phase = "open";
    long result = ESP_FAIL;
    esp_err_t opened = ESP_FAIL;
    while (!cancelled() && esp_timer_get_time() - started < connect_us) {
        opened = esp_http_client_open(client, 0);
        if (opened != ESP_ERR_HTTP_EAGAIN) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    transport_error = opened != ESP_OK;
    result = opened;
    if (!cancelled() && opened == ESP_OK) {
        phase = "headers";
        int64_t until = esp_timer_get_time() + response_us;
        int64_t length = -1;
        // 短等待读取可取消，EAGAIN仅在当前承载的响应窗口内重试。
        // Short I/O waits stay cancellable within the selected bearer's response window.
        while (!cancelled()) {
            int wait = http_wait_ms(until);
            if (wait <= 0) { result = ESP_ERR_TIMEOUT; break; }
            esp_err_t timeout_result = esp_http_client_set_timeout_ms(client, wait);
            if (timeout_result != ESP_OK) { result = timeout_result; break; }
            length = esp_http_client_fetch_headers(client);
            result = (long)length;
            if (length != -ESP_ERR_HTTP_EAGAIN) break;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        transport_error = length < 0 || esp_timer_get_time() >= until;
        if (!cancelled() && esp_timer_get_time() < until && length >= 0 && length <= WEATHER_BODY_LIMIT) {
            phase = "body";
            transport_error = true;
            size_t used = 0;
            until = esp_timer_get_time() + response_us;
            while (!cancelled() && used < WEATHER_BODY_LIMIT) {
                int wait = http_wait_ms(until);
                if (wait <= 0) { result = ESP_ERR_TIMEOUT; break; }
                esp_err_t timeout_result = esp_http_client_set_timeout_ms(client, wait);
                if (timeout_result != ESP_OK) { result = timeout_result; break; }
                size_t chunk = WEATHER_BODY_LIMIT - used;
                if (chunk > 512) chunk = 512;
                int n = esp_http_client_read(client, body + used, (int)chunk);
                result = n;
                if (n == -ESP_ERR_HTTP_EAGAIN) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
                if (n < 0) break;
                if (!n) { ok = esp_http_client_is_complete_data_received(client); break; }
                used += (size_t)n;
                until = esp_timer_get_time() + response_us;
            }
            body[used] = 0;
            if (used == WEATHER_BODY_LIMIT) ok = esp_http_client_is_complete_data_received(client);
            if (ok) transport_error = false;
            if (ok && !cancelled() && !memchr(body, 0, used)) {
                int status = esp_http_client_get_status_code(client);
                if (status != 200) {
                    phase = "status"; result = status;
                    api_error(status, body); ok = false;
                }
            } else ok = false;
        }
    }
    ok = ok && !cancelled();
    *retryable = !ok && !cancelled() && transport_error;
    if (!cancelled()) network_http_note_result(&s_http_session, !transport_error);
    if (!ok && !atomic_load(&s_cancel))
        network_diagnostics_http_failure("weather", client, phase, result, bearer);
    if (ok) network_diagnostics_http_response("weather", client, strlen(body));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ok && response_clock) *response_clock = header.clock;
    return ok;
}
static bool http_get(const char *path, char *body, weather_clock_t *response_clock,
                     network_bearer_t bearer) {
    const unsigned attempts = bearer == NETWORK_BEARER_CELLULAR ? 2 : 1;
    for (unsigned attempt = 0; attempt < attempts && !cancelled(); ++attempt) {
        bool retryable;
        if (http_get_once(path, body, response_clock, bearer, &retryable)) return true;
        if (!retryable || attempt + 1 == attempts || cancelled()) break;
        // 同一租约内仅重新建立一次连接；不重试业务错误，也不延长整个会话。
        // Retry one fresh connection on the same lease, within the original session deadline.
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}
static void search_free_batches(weather_search_batch_t **list) {
    while (*list) {
        weather_search_batch_t *next = (*list)->next;
        heap_caps_free(*list);
        *list = next;
    }
}
static weather_search_batch_t *search_batch(unsigned page) {
    for (weather_search_batch_t *batch = s_search_batches; batch; batch = batch->next)
        if (batch->page == page) return batch;
    return NULL;
}
static bool search_show_page(unsigned page) {
    unsigned offset = page * WEATHER_CITY_LIMIT;
    if (!s_search_total_known || (s_search_total ? offset >= s_search_total : page != 0)) return false;
    unsigned count = s_search_total ? s_search_total - offset : 0;
    if (count > WEATHER_CITY_LIMIT) count = WEATHER_CITY_LIMIT;
    for (unsigned i = 0; i < count; ++i) {
        weather_search_batch_t *batch = search_batch((offset + i) / WEATHER_SEARCH_BATCH_SIZE);
        if (!batch || (offset + i) % WEATHER_SEARCH_BATCH_SIZE >= batch->count) return false;
    }
    memset(s_view.results, 0, sizeof(s_view.results));
    for (unsigned i = 0; i < count; ++i) {
        weather_search_batch_t *batch = search_batch((offset + i) / WEATHER_SEARCH_BATCH_SIZE);
        s_view.results[i] = batch->locations[(offset + i) % WEATHER_SEARCH_BATCH_SIZE];
    }
    s_view.result_count = count; s_view.result_total = s_search_total; s_view.result_page = page;
    s_view.state = APP_WEATHER_READY;
    snprintf(s_view.notice, sizeof(s_view.notice), "%s", count ? "请选择城市（自动识别仅作参考）" : "未找到城市，请换个名称");
    ++s_view.revision;
    return true;
}
static void weather_worker(void *arg) {
    (void)arg;
    network_lease_t lease = {0};
    char *body = heap_caps_malloc(WEATHER_BODY_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    result_notice(APP_WEATHER_FAILED, "天气更新失败，可稍后重试");
    if (!body) {

        result_notice(APP_WEATHER_FAILED, "内存不足，请稍后重试"); goto done;
    }
    if (cancelled()) goto done;
    if (network_service_acquire(NETWORK_USE_WEATHER, &lease) != ESP_OK ||
        !network_service_wait_ready(&lease, 30000)) {
        result_notice(APP_WEATHER_UNAVAILABLE, "当前联网方式不可用，请检查网络设置");
        goto done;
    }
    network_diagnostics_snapshot("weather", lease.bearer);
    if (cancelled() || !http_get("/components", body, NULL, lease.bearer)) goto done;
    bool enabled, available;
    if (!weather_catalog_parse(body, strlen(body), &enabled, &available)) {
        network_diagnostics_data_failure("weather", "catalog-parse", "invalid-catalog",
                                        strlen(body), 0, lease.bearer);
        goto done;
    }
    if (!enabled) {
        s_result.clear_cache = true; result_notice(APP_WEATHER_DISABLED, "天气服务暂未开放"); goto done;
    }
    if (!available) { result_notice(APP_WEATHER_UNAVAILABLE, "天气服务尚未就绪"); goto done; }
    if (s_request == APP_WEATHER_SEARCH) {
        char escaped[577], path[640];
        if (!weather_url_encode(s_query, escaped, sizeof(escaped))) goto done;
        for (unsigned i = 0; i < s_requested_missing_count; ++i) {
            unsigned page = s_requested_missing[i];
            if (i && page * WEATHER_SEARCH_BATCH_SIZE >= s_result.total) break;
            weather_search_batch_t *batch = heap_caps_malloc(sizeof(*batch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!batch) goto done;
            memset(batch, 0, sizeof(*batch));
            unsigned total;
            snprintf(path, sizeof(path), "/components/weather/locations?q=%s&page=%u&pageSize=%u", escaped, page, WEATHER_SEARCH_BATCH_SIZE);
            bool ok = http_get(path, body, NULL, lease.bearer) && weather_locations_parse(body, strlen(body), batch->locations,
                WEATHER_SEARCH_BATCH_SIZE, &batch->count, &total, &batch->page) && batch->page == page &&
                (!s_requested_total_known || total == s_requested_total) && (!i || total == s_result.total);
            if (!ok) { heap_caps_free(batch); goto done; }
            s_result.total = total;
            batch->next = s_result.batches; s_result.batches = batch;
        }
        result_notice(APP_WEATHER_READY, ""); goto done;
    }
    weather_location_t city = s_requested_city;
    if (s_request == APP_WEATHER_LOCATE || !s_requested_has_city) {
        if (!http_get("/components/weather/auto-location", body, NULL, lease.bearer)) {
            if (s_result.state == APP_WEATHER_FAILED)
                result_notice(APP_WEATHER_CITY_REQUIRED, "未能确定城市，请手动选择");
            goto done;
        }
        if (!weather_location_parse(body, strlen(body), &city)) {
            network_diagnostics_data_failure("weather", "location-parse", "invalid-location",
                                            strlen(body), 0, lease.bearer);
            result_notice(APP_WEATHER_CITY_REQUIRED, "识别结果不支持，请手动选择"); goto done;
        }
        s_result.has_city = true; s_result.city = city;
    }
    char path[256];
    snprintf(path, sizeof(path), "/components/weather/forecast?locationId=%s", city.id);
    weather_clock_t clock = s_requested_clock, response_clock;
    if (!http_get(path, body, &response_clock, lease.bearer)) goto done;
    // 仅接受同一可信HTTPS预报的时间；旧响应不能倒退参照或延长原始有效期。
    // Trust time only from the same HTTPS forecast; older responses cannot roll back time or extend expiry.
    if (response_clock.utc && (!clock.utc || clock_now(&response_clock) > clock_now(&clock))) clock = response_clock;
    weather_parse_result_t parsed = weather_forecast_parse_result(body, strlen(body), clock_now(&clock),
                                                                &city, &s_result.forecast);
    if (parsed != WEATHER_PARSE_OK) {
        network_diagnostics_data_failure("weather", "forecast-parse", weather_parse_result_name(parsed),
                                        strlen(body), clock_now(&clock), lease.bearer);
        goto done;
    }
    s_result.clock = clock;
    s_result.has_forecast = true; result_notice(APP_WEATHER_READY, "");
done:
    network_service_release(&lease);
    heap_caps_free(body);
    if (cancelled() && s_result.state == APP_WEATHER_READY) {
        s_result.has_forecast = false;
        result_notice(APP_WEATHER_FAILED, "天气更新失败，可稍后重试");
    }
    if (s_result.state != APP_WEATHER_READY) search_free_batches(&s_result.batches);
    atomic_store(&s_busy, false);
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}
static bool collect_result(void) {
    if (!s_pending || app_weather_busy() || xSemaphoreTake(s_done, 0) != pdTRUE) return false;
    s_pending = false;
    if (s_request == APP_WEATHER_SEARCH && s_requested_search_generation != s_search_generation) {
        search_free_batches(&s_result.batches);
        s_view.state = APP_WEATHER_IDLE; s_view.notice[0] = 0; ++s_view.revision; return true;
    }
    s_view.state = s_result.state; snprintf(s_view.notice, sizeof(s_view.notice), "%s", s_result.notice);
    if (s_result.has_city) {
        if (save_preferences(s_view.enabled, s_view.auto_refresh, true, &s_result.city)) {
            s_view.city = s_result.city; s_view.has_city = true; s_view.has_forecast = false;
        } else {
            s_result.has_forecast = false;
            snprintf(s_view.notice, sizeof(s_view.notice), "城市保存失败，请检查设置存储");
        }
    }
    if (s_result.clear_cache) { s_view.has_forecast = false; (void)write_blob("cache", NULL, 0); }
    else if (s_result.has_forecast) {
        s_clock = s_result.clock;
        s_view.forecast = s_result.forecast; s_view.has_forecast = true;
        weather_cache_t cache = {.schema=2,.forecast=s_view.forecast};
        (void)write_blob("cache", &cache, sizeof(cache));
    }
    if (s_request == APP_WEATHER_SEARCH) {
        if (s_result.state == APP_WEATHER_READY) {
            while (s_result.batches) {
                weather_search_batch_t *batch = s_result.batches;
                s_result.batches = batch->next; batch->next = s_search_batches; s_search_batches = batch;
            }
            s_search_total = s_result.total; s_search_total_known = true;
            (void)search_show_page(s_requested_page);
        }
    } else {
        s_view.result_count = s_view.result_total = s_view.result_page = 0;
        memset(s_view.results, 0, sizeof(s_view.results));
    }
    ++s_view.revision; return true;
}
void app_weather_cancel_join(void) {
    if (!s_pending) return;
    atomic_store(&s_cancel, true);
    xSemaphoreTake(s_done, portMAX_DELAY);
    s_pending = false;
    search_free_batches(&s_result.batches);
    s_view.state = APP_WEATHER_IDLE;
    s_view.notice[0] = 0;
    ++s_view.revision;
}
bool app_weather_set_enabled(bool enabled) {
    app_weather_init(); app_weather_cancel_join();
    if (!save_preferences(enabled, s_view.auto_refresh, s_view.has_city, &s_view.city)) return false;
    s_view.enabled = enabled; s_next_attempt = 0; ++s_view.revision; return true;
}
bool app_weather_set_auto_refresh(bool enabled) {
    app_weather_init(); app_weather_cancel_join();
    if (!save_preferences(s_view.enabled, enabled, s_view.has_city, &s_view.city)) return false;
    s_view.auto_refresh = enabled; s_next_attempt = 0; ++s_view.revision; return true;
}
bool app_weather_select(const weather_location_t *city) {
    if (!weather_location_valid(city)) return false;
    app_weather_init(); app_weather_cancel_join();
    if (!save_preferences(s_view.enabled, s_view.auto_refresh, true, city)) return false;
    s_view.city = *city; s_view.has_city = true; s_view.has_forecast = false;
    app_weather_clear_search();
    s_view.state = APP_WEATHER_IDLE; s_view.notice[0] = 0;
    (void)write_blob("cache", NULL, 0);
    s_next_attempt = 0; ++s_view.revision; return true;
}
void app_weather_clear_search(void) {
    ++s_search_generation;
    search_free_batches(&s_search_batches);
    s_cached_query[0] = 0; s_search_total_known = false; s_search_total = 0;
    if(s_view.state!=APP_WEATHER_LOADING)s_view.notice[0]=0;
    s_view.result_count = s_view.result_total = s_view.result_page = 0;
    memset(s_view.results, 0, sizeof(s_view.results)); ++s_view.revision;
}
static bool request_page(app_weather_request_t request, const char *query, unsigned page) {
    app_weather_init(); (void)collect_result();
    if (s_pending || pico_online_busy()) return false;
    if (!weather_clock_valid(time(NULL))) {
        s_view.state = APP_WEATHER_UNAVAILABLE;
        snprintf(s_view.notice, sizeof(s_view.notice), "请先校准系统日期与时间"); ++s_view.revision; return false;
    }
    if (ble_pt_connected()) {
        s_view.state = APP_WEATHER_UNAVAILABLE;
        snprintf(s_view.notice, sizeof(s_view.notice), "蓝牙翻页器正在使用，请稍后更新"); ++s_view.revision; return false;
    }
    const pmu_snapshot_t *pmu = device_power_get();
    int percent = pmu_battery_percent(pmu);
    if (percent >= 0 && percent < 10 && !pmu_battery_charging(pmu)) {
        s_view.state = APP_WEATHER_UNAVAILABLE;
        snprintf(s_view.notice, sizeof(s_view.notice), "电量较低，请充电后更新天气"); ++s_view.revision; return false;
    }
    if (request == APP_WEATHER_SEARCH) {
        if (!weather_query_valid(query) || page >= (WEATHER_SEARCH_TOTAL_LIMIT + WEATHER_CITY_LIMIT - 1) / WEATHER_CITY_LIMIT) return false;
        if (!page || strcmp(s_cached_query, query)) {
            app_weather_clear_search(); snprintf(s_cached_query, sizeof(s_cached_query), "%s", query);
        }
        if (s_search_total_known && page * WEATHER_CITY_LIMIT >= s_search_total) return false;
        snprintf(s_query, sizeof(s_query), "%s", query);
    }
    s_requested_page = page;
    if (!s_done) s_done = xSemaphoreCreateBinary();
    if (!s_done) return false;
    s_request = request; s_requested_city = s_view.city; s_requested_has_city = s_view.has_city;
    s_requested_clock = s_clock;
    if(request==APP_WEATHER_LOCATE||(request==APP_WEATHER_FETCH&&!s_view.has_city))s_auto_attempted=true;
    memset(&s_result, 0, sizeof(s_result));
    s_requested_total_known = s_search_total_known; s_requested_total = s_search_total;
    s_requested_missing_count = 0;
    if (request == APP_WEATHER_SEARCH) {
        unsigned first = page * WEATHER_CITY_LIMIT / WEATHER_SEARCH_BATCH_SIZE;
        unsigned last = (page * WEATHER_CITY_LIMIT + WEATHER_CITY_LIMIT - 1) / WEATHER_SEARCH_BATCH_SIZE;
        for (unsigned batch = first; batch <= last; ++batch)
            if (!search_batch(batch)) s_requested_missing[s_requested_missing_count++] = batch;
    }
    atomic_store(&s_cancel, false); atomic_store(&s_busy, true);
    // 先公布忙状态，主循环不再调度自动校时/OTA；等待旧校时释放后才启动网络。
    // Publish busy before suppressing automatic time/OTA scheduling; let the old sync release its session first.
    app_time_service_cancel();
    s_next_attempt = esp_timer_get_time() + WEATHER_RETRY_US;
    s_deadline = esp_timer_get_time() + WEATHER_SESSION_US;
    if (xTaskCreate(weather_worker, "weather", 8192, NULL, 1, NULL) != pdPASS) {

        atomic_store(&s_busy, false);
        s_view.state = APP_WEATHER_FAILED;
        snprintf(s_view.notice, sizeof(s_view.notice), "内存不足，请稍后重试"); ++s_view.revision; return false;
    }
    s_pending = true; s_view.state = APP_WEATHER_LOADING; s_view.notice[0] = 0;
    s_requested_search_generation = s_search_generation;
    ++s_view.revision; return true;
}
bool app_weather_request(app_weather_request_t request, const char *query) {
    return request_page(request, query, 0);
}
bool app_weather_search(const char *query, unsigned page) {
    app_weather_init(); (void)collect_result();
    if (s_pending || !weather_query_valid(query) || page >= (WEATHER_SEARCH_TOTAL_LIMIT + WEATHER_CITY_LIMIT - 1) / WEATHER_CITY_LIMIT) return false;
    if (!strcmp(s_cached_query, query) && search_show_page(page)) return true;
    return request_page(APP_WEATHER_SEARCH, query, page);
}
bool app_weather_poll(bool automatic) {
    app_weather_init(); unsigned revision = s_view.revision;
    (void)collect_result(); int64_t now = app_weather_now();
    bool valid = s_view.has_forecast && weather_forecast_valid(&s_view.forecast, now);
    int32_t day = weather_local_day(now);
    if (day != s_display_day || valid != s_display_valid) {
        s_display_day = day; s_display_valid = valid; ++s_view.revision;
    }
    if (automatic && s_view.auto_refresh && (s_view.has_city||!s_auto_attempted) && !s_pending && esp_timer_get_time() >= s_next_attempt &&
        s_view.state != APP_WEATHER_CITY_REQUIRED &&
        (!valid || now >= s_view.forecast.fetched + WEATHER_REFRESH_SECONDS || !weather_day_for(&s_view.forecast, now, true))) {
        s_next_attempt = esp_timer_get_time() + WEATHER_RETRY_US;
        (void)app_weather_request(APP_WEATHER_FETCH, NULL);
    }
    return revision != s_view.revision;
}
