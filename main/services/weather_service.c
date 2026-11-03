/*
 * 中文：平台天气代理、独占短连和本地两日缓存；不保存密钥或完整城市列表。
 * English: Platform weather proxy, exclusive short connections and two-day cache; no keys or persistent city lists.
 * 冻结：不抢占传书/升级；页面离开、锁屏、关机和快捷WiFi前必须取消等待。
 * Frozen: Never steal transfer/update sessions; cancel and join before exit, lock, shutdown or quick WiFi.
 */
#include "weather_service.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
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
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef CONFIG_PICO_COMPONENT_API_ORIGIN
#define CONFIG_PICO_COMPONENT_API_ORIGIN "https://kiikoread.com"
#endif
#define WEATHER_RETRY_US (5LL * 60 * 1000000)
#define WEATHER_SESSION_US (45LL * 1000000)
#define WEATHER_CONNECT_US (6LL * 1000000)
#define WEATHER_REFRESH_SECONDS (6LL * 60 * 60)
typedef struct {
    uint32_t schema;
    uint8_t enabled, has_city, auto_refresh;
    weather_location_t city;
} weather_preferences_t;
// 新开关使用旧布局的填充字节，schema 1仍按原城市偏移读取。
// Store the new flag in legacy padding, preserving schema-1 city offsets.
_Static_assert(offsetof(weather_preferences_t, city) == 8, "weather preferences layout");
typedef struct {
    uint32_t schema;
    weather_forecast_t forecast;
} weather_cache_t;
typedef struct {
    app_weather_state_t state;
    char notice[97];
    bool has_city, has_forecast, clear_cache;
    weather_location_t city, results[WEATHER_CITY_LIMIT];
    weather_forecast_t forecast;
    unsigned count;
} weather_result_t;
static app_weather_view_t s_view;
static weather_result_t s_result;
static app_weather_request_t s_request;
static char s_query[193];
static weather_location_t s_requested_city;
static bool s_requested_has_city, s_loaded, s_pending;
static bool s_auto_attempted;
static bool s_label_attempted;
static unsigned s_search_generation, s_requested_search_generation;
static atomic_bool s_busy, s_cancel;
static SemaphoreHandle_t s_done;
static int64_t s_next_attempt, s_deadline;
static int32_t s_display_day;
static bool s_display_valid;

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
    weather_preferences_t p = {.schema = 2, .enabled = enabled, .has_city = has_city, .auto_refresh = auto_refresh};
    if (has_city) p.city = *city;
    return write_blob("prefs", &p, sizeof(p));
}
void app_weather_init(void) {
    if (s_loaded) return;
    s_loaded = true;
    s_view.state = APP_WEATHER_IDLE;
    s_view.auto_refresh = true;
    nvs_handle_t h;
    if (nvs_open("kiiko_weather", NVS_READONLY, &h) == ESP_OK) {
        weather_preferences_t p = {0}; size_t size = sizeof(p);
        if (nvs_get_blob(h, "prefs", &p, &size) == ESP_OK && size == sizeof(p) && (p.schema == 1 || p.schema == 2) &&
            p.enabled <= 1 && p.has_city <= 1 && (p.schema == 1 || p.auto_refresh <= 1) &&
            (!p.has_city || weather_location_valid(&p.city))) {
            s_view.enabled = p.enabled; s_view.has_city = p.has_city; s_view.city = p.city;
            s_view.auto_refresh = p.schema == 1 || p.auto_refresh;
        }
        weather_cache_t cache = {0};size = sizeof(cache);
        if (s_view.has_city && nvs_get_blob(h, "cache", &cache, &size) == ESP_OK &&
            size == sizeof(cache) && cache.schema == 1 && weather_forecast_valid(&cache.forecast, time(NULL)) &&
            !strcmp(s_view.city.name, cache.forecast.location.name) &&
            s_view.city.lat == cache.forecast.location.lat && s_view.city.lon == cache.forecast.location.lon) {
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
static bool http_get(const char *path, char *body) {
    char url[1024];
    const char *origin = CONFIG_PICO_COMPONENT_API_ORIGIN;
    size_t origin_length = strlen(origin);
    if (strncmp(origin, "https://", 8) || origin_length < 9 || origin_length > 200 ||
        strchr(origin + 8, '/') || strchr(origin + 8, '?') || strchr(origin + 8, '#') ||
        strchr(origin + 8, '@') || cancelled()) return false;
    if (snprintf(url, sizeof(url), "%s/api/v2%s", origin, path) >= (int)sizeof(url)) return false;
    int64_t started = esp_timer_get_time();
    ESP_LOGI("weather", "request start origin=%s path=%.*s", origin, (int)strcspn(path, "?"), path);
    esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 6000, .is_async = true,
        .disable_auto_redirect = true, .buffer_size = 1024, .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    bool ok = false;
    const char *phase = "connect";
    esp_err_t opened = ESP_FAIL;
    while (!cancelled() && esp_timer_get_time() - started < WEATHER_CONNECT_US) {
        opened = esp_http_client_open(client, 0);
        if (opened != ESP_ERR_HTTP_EAGAIN) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!cancelled() && opened == ESP_OK && esp_http_client_set_timeout_ms(client, 1200) == ESP_OK) {
        phase = "headers";
        int64_t length = esp_http_client_fetch_headers(client);
        if (length >= 0 && length <= WEATHER_BODY_LIMIT) {
            phase = "body";
            size_t used = 0;
            while (!cancelled() && used < WEATHER_BODY_LIMIT) {
                int n = esp_http_client_read(client, body + used, WEATHER_BODY_LIMIT - used);
                if (n < 0) break;
                if (!n) { ok = esp_http_client_is_complete_data_received(client); break; }
                used += (size_t)n;
            }
            body[used] = 0;
            if (used == WEATHER_BODY_LIMIT) ok = esp_http_client_is_complete_data_received(client);
            if (ok && !cancelled() && !memchr(body, 0, used)) {
                int status = esp_http_client_get_status_code(client);
                if (status != 200) { api_error(status, body); ok = false; }
            } else ok = false;
        }
    }
    ESP_LOGI("weather", "request finish phase=%s status=%d ok=%d cancelled=%d elapsed_ms=%lld",
             phase, esp_http_client_get_status_code(client), ok, cancelled(),
             (long long)((esp_timer_get_time() - started) / 1000));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok && !cancelled();
}
static bool city_needs_label(const weather_location_t *city) {
    for(const unsigned char *p=(const unsigned char *)city->name;*p;++p)
        if(*p>=128)return false;
    return city->name[0]!=0;
}
static bool localize_city(weather_location_t *city,char *body) {
    if(!city_needs_label(city))return true;
    char escaped[577],path[640];
    if(!weather_url_encode(city->name,escaped,sizeof(escaped)))return false;
    snprintf(path,sizeof(path),"/components/weather/locations?q=%s",escaped);
    if(!http_get(path,body))return false;
    weather_location_t found[WEATHER_CITY_LIMIT];unsigned count=0;
    if(!weather_locations_parse(body,strlen(body),found,&count))return true;
    int best=-1;double distance=1.0;
    for(unsigned i=0;i<count;++i) {
        double lat=found[i].lat-city->lat,lon=found[i].lon-city->lon;
        double current=lat*lat+lon*lon;
        if(!city_needs_label(&found[i])&&current<distance) {best=(int)i;distance=current;}
    }
    if(best>=0) {
        // 只修正展示名称，匹配附近候选；不改变已选坐标或重新按IP定位。
        // Relabel from a nearby match without changing selected coordinates or relocating by IP.
        memcpy(city->name,found[best].name,sizeof(city->name));
        memcpy(city->admin1,found[best].admin1,sizeof(city->admin1));
        memcpy(city->country,found[best].country,sizeof(city->country));
        s_result.has_city=true;s_result.city=*city;
    }
    return true;
}
static void weather_worker(void *arg) {
    (void)arg; bool owned = false;
    char *body = heap_caps_malloc(WEATHER_BODY_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    result_notice(APP_WEATHER_FAILED, "天气更新失败，可稍后重试");
    if (!body || cancelled()) goto done;
    wifi_transfer_status_t status; wifi_transfer_get_status(&status);
    bool borrowed = status.state == WIFI_TRANSFER_READY && status.mode == WIFI_TRANSFER_MODE_STA && status.network_ready;
    if (!borrowed && status.state != WIFI_TRANSFER_STOPPED) {
        result_notice(APP_WEATHER_UNAVAILABLE, "网络正在使用，请稍后重试"); goto done;
    }
    if (!borrowed) {
        char ssid[33]; bool saved = false;
        if (wifi_transfer_get_saved_wifi(ssid, &saved) != ESP_OK || !saved) {
            result_notice(APP_WEATHER_UNAVAILABLE, "请先在设置中保存 WiFi"); goto done;
        }
        const wifi_transfer_cfg_t config = {.mode = WIFI_TRANSFER_MODE_STA, .network_only = true};
        if (wifi_transfer_start(&config) != ESP_OK) goto done;
        owned = true;
    }
    while (!cancelled()) {
        wifi_transfer_get_status(&status);
        if (status.state == WIFI_TRANSFER_READY && status.network_ready) break;
        if (status.state == WIFI_TRANSFER_ERROR || status.state == WIFI_TRANSFER_STOPPED) goto done;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (cancelled() || !http_get("/components", body)) goto done;
    bool enabled, available;
    if (!weather_catalog_parse(body, strlen(body), &enabled, &available)) goto done;
    if (!enabled) {
        s_result.clear_cache = true; result_notice(APP_WEATHER_DISABLED, "天气服务暂未开放"); goto done;
    }
    if (!available) { result_notice(APP_WEATHER_UNAVAILABLE, "天气服务尚未就绪"); goto done; }
    if (s_request == APP_WEATHER_SEARCH) {
        char escaped[577], path[640];
        if (!weather_url_encode(s_query, escaped, sizeof(escaped))) goto done;
        snprintf(path, sizeof(path), "/components/weather/locations?q=%s", escaped);
        if (!http_get(path, body) || !weather_locations_parse(body, strlen(body), s_result.results, &s_result.count)) goto done;
        result_notice(APP_WEATHER_READY, s_result.count ? "请选择城市（自动识别仅作参考）" : "未找到城市，请换个名称"); goto done;
    }
    weather_location_t city = s_requested_city;
    if (s_request == APP_WEATHER_LOCATE || !s_requested_has_city) {
        if (!http_get("/components/weather/auto-location", body)) {
            if (s_result.state == APP_WEATHER_FAILED)
                result_notice(APP_WEATHER_CITY_REQUIRED, "未能确定城市，请手动选择");
            goto done;
        }
        if (!weather_location_parse(body, strlen(body), &city)) {
            result_notice(APP_WEATHER_CITY_REQUIRED, "识别结果不支持，请手动选择"); goto done;
        }
        s_result.has_city = true; s_result.city = city;
    }
    if(!localize_city(&city,body)) {
        if(cancelled()||s_result.clear_cache)goto done;
        s_result.notice[0]=0;
    }
    char path[256];
    snprintf(path, sizeof(path), "/components/weather/forecast?lat=%.2f&lon=%.2f&timezone=Asia%%2FShanghai", city.lat, city.lon);
    if (!http_get(path, body) || !weather_forecast_parse(body, strlen(body), time(NULL), &city, &s_result.forecast)) goto done;
    s_result.has_forecast = true; result_notice(APP_WEATHER_READY, "");
done:
    if (owned) wifi_transfer_stop();
    heap_caps_free(body);
    atomic_store(&s_busy, false);
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}
static bool collect_result(void) {
    if (!s_pending || app_weather_busy() || xSemaphoreTake(s_done, 0) != pdTRUE) return false;
    s_pending = false;
    if (s_request == APP_WEATHER_SEARCH && s_requested_search_generation != s_search_generation) {
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
        s_view.forecast = s_result.forecast; s_view.has_forecast = true;
        weather_cache_t cache = {.schema=1,.forecast=s_view.forecast};
        (void)write_blob("cache", &cache, sizeof(cache));
    }
    s_view.result_count = s_result.count;
    memcpy(s_view.results, s_result.results, sizeof(s_view.results));
    ++s_view.revision; return true;
}
void app_weather_cancel_join(void) {
    if (!s_pending) return;
    atomic_store(&s_cancel, true);
    xSemaphoreTake(s_done, portMAX_DELAY);
    s_pending = false;
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
    s_view.result_count = 0; s_view.state = APP_WEATHER_IDLE; s_view.notice[0] = 0;
    (void)write_blob("cache", NULL, 0);
    s_next_attempt = 0; ++s_view.revision; return true;
}
void app_weather_clear_search(void) {
    ++s_search_generation;
    if(s_view.state!=APP_WEATHER_LOADING)s_view.notice[0]=0;
    s_view.result_count = 0; memset(s_view.results, 0, sizeof(s_view.results)); ++s_view.revision;
}
bool app_weather_request(app_weather_request_t request, const char *query) {
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
        if (!weather_query_valid(query)) return false;
        snprintf(s_query, sizeof(s_query), "%s", query);
    }
    if (!s_done) s_done = xSemaphoreCreateBinary();
    if (!s_done) return false;
    s_request = request; s_requested_city = s_view.city; s_requested_has_city = s_view.has_city;
    if(request==APP_WEATHER_LOCATE||(request==APP_WEATHER_FETCH&&!s_view.has_city))s_auto_attempted=true;
    memset(&s_result, 0, sizeof(s_result));
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
    if(request!=APP_WEATHER_SEARCH)s_label_attempted=true;
    if (request == APP_WEATHER_SEARCH) app_weather_clear_search();
    s_requested_search_generation = s_search_generation;
    ++s_view.revision; return true;
}
bool app_weather_poll(bool automatic) {
    app_weather_init(); unsigned revision = s_view.revision;
    (void)collect_result(); int64_t now = time(NULL);
    bool valid = s_view.has_forecast && weather_forecast_valid(&s_view.forecast, now);
    int32_t day = weather_local_day(now);
    if (day != s_display_day || valid != s_display_valid) {
        s_display_day = day; s_display_valid = valid; ++s_view.revision;
    }
    if (automatic && s_view.auto_refresh && (s_view.has_city||!s_auto_attempted) && !s_pending && esp_timer_get_time() >= s_next_attempt &&
        s_view.state != APP_WEATHER_CITY_REQUIRED &&
        (!valid || now >= s_view.forecast.fetched + WEATHER_REFRESH_SECONDS || !weather_day_for(&s_view.forecast, now, true) ||
         (!s_label_attempted&&s_view.has_city&&city_needs_label(&s_view.city)))) {
        s_next_attempt = esp_timer_get_time() + WEATHER_RETRY_US;
        (void)app_weather_request(APP_WEATHER_FETCH, NULL);
    }
    return revision != s_view.revision;
}
