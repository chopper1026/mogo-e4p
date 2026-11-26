#pragma once
#include "weather_model.h"
#include "restore_nvs.h"

/// 恢复城市和自动刷新，不导入缓存或旧组件开关。/ Restore city and automatic refresh without importing cache or the legacy widget switch.
esp_err_t app_weather_restore_preferences(bool auto_refresh, bool has_city, const weather_location_t *city, struct restore_nvs *image);
