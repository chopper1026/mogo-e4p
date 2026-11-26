/*
 * 中文：当前页面持有的天气服务；UI任务交接结果，后台处理有界HTTP与已选城市的自动缓存。
 * English: Page-owned weather service; UI hands off results; the worker handles bounded HTTP and automatic cache writes for a selected city.
 */
#pragma once
#include "weather_model.h"
typedef enum {
    APP_WEATHER_IDLE, APP_WEATHER_LOADING, APP_WEATHER_READY, APP_WEATHER_CITY_REQUIRED,
    APP_WEATHER_DISABLED, APP_WEATHER_UNAVAILABLE, APP_WEATHER_FAILED
} app_weather_state_t;
typedef enum { APP_WEATHER_FETCH, APP_WEATHER_LOCATE, APP_WEATHER_SEARCH } app_weather_request_t;
typedef struct {
    bool enabled, has_city, has_forecast; ///< enabled仅迁移旧单天气开关；城市与缓存 / enabled migrates the old single-widget preference; city and cache
    bool auto_refresh; ///< 六小时自动更新开关 / Six-hour automatic refresh preference
    weather_location_t city; ///< 当前城市 / Selected city
    weather_forecast_t forecast; ///< 两日原始预报 / Original two-day forecast
    weather_location_t results[WEATHER_CITY_LIMIT]; ///< 仅当前搜索 / Current search only
    unsigned result_count, result_total, result_page, revision; ///< 搜索分页与UI版本 / Search pagination and UI revision
    app_weather_state_t state; ///< 当前请求状态 / Current request state
    char notice[97]; ///< 本地提示，不展示上游原始错误 / Local message, never raw upstream errors
} app_weather_view_t;

/// 以下生命周期和选择接口仅由UI任务调用。/ Lifecycle and selection APIs below are UI-task only.
void app_weather_init(void);
const app_weather_view_t *app_weather_view(void);
/// 天气专用UTC秒；使用服务器参照，不修改系统时钟。/ Weather-only UTC seconds using a server reference, without changing the system clock.
int64_t app_weather_now(void);
bool app_weather_set_enabled(bool enabled);
/// 仅控制自动请求；手动更新和选城不受影响。/ Gate automatic requests only, preserving manual updates and city selection.
bool app_weather_set_auto_refresh(bool enabled);
bool app_weather_select(const weather_location_t *city);
bool app_weather_request(app_weather_request_t request, const char *query);
/// 页码从零开始；优先本地缓存，仅补取缺失批次。/ Zero-based pages use local results first, fetching only missing batches.
bool app_weather_search(const char *query, unsigned page);
/// automatic由前台页面按首页槽位决定，旧enabled字段不再决定新布局。/ The foreground page gates automatic work by Home slots, not the legacy enabled field.
bool app_weather_poll(bool automatic);
/// 空闲非天气页静默更新已选城市；调用者确认组件仍在首页。/ Quiet refresh for an idle non-weather page; caller checks widget presence.
void app_weather_background_poll(void);
/// 仅回收或取消静默任务，不等待、不影响页面发起的请求。/ Collect or cancel only quiet work, without waiting or affecting page requests.
void app_weather_background_pause(void);
/// 通知取消，不等待；旧结果不会写回设置或缓存。/ Signal cancellation without waiting or publishing stale results.
void app_weather_cancel(void);
/// 非天气页也回收已取消结果；不消费有效回复。/ Reap cancelled results off-page without consuming valid replies.
void app_weather_reap_cancelled(void);
/// 取消后等待真正交还无线与HTTP，不删除仍运行的任务。/ Cancel and await actual radio/HTTP handoff; never delete a running worker.
void app_weather_cancel_join(void);
bool app_weather_busy(void);
/// NVS 空间紧张时删除可重建的预报缓存，保留城市偏好与当前内存视图。/ Evict rebuildable forecast cache from NVS under space pressure while keeping preferences and in-RAM view.
bool app_weather_evict_cache(void);
void app_weather_clear_search(void);
