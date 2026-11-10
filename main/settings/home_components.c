/*
 * 中文：首页双区域组件的NVS偏好，与设置恢复共用事务；不修改天气城市或缓存。
 * English: NVS preferences for two Home regions, sharing settings restore transactions without changing weather data.
 */
#include "home_components.h"
#include "epdiy.h"
#include "nvs.h"
static bool s_loaded;
static bool s_library_recent;
static app_home_widget_t s_slots[APP_HOME_SLOT_COUNT] = {APP_HOME_WIDGET_READING, APP_HOME_WIDGET_EMPTY};

bool app_home_components_value_valid(uint8_t value) {
    unsigned top, bottom;
    if (value & 0x80u) {
        top = value & 7u; bottom = (value >> 3) & 7u;
        if (top > APP_HOME_WIDGET_MINI_LIBRARY || bottom > APP_HOME_WIDGET_MINI_LIBRARY ||
            (!(value & 0x40u) && top != APP_HOME_WIDGET_MINI_LIBRARY && bottom != APP_HOME_WIDGET_MINI_LIBRARY)) return false;
    } else {
        if ((value & 0xf0u) != 0x10u) return false;
        top = value & 3u; bottom = (value >> 2) & 3u;
    }
    return top != bottom || top == APP_HOME_WIDGET_EMPTY;
}

// 旧布局原样编码；v2以最高位置位区分，每槽三位，第六位保存选书模式。/ Preserve legacy bytes; v2 uses the high bit, three bits per slot and bit six for the mode.
static uint8_t encode(app_home_widget_t top, app_home_widget_t bottom, bool recent) {
    if (!recent && top <= APP_HOME_WIDGET_JOURNAL && bottom <= APP_HOME_WIDGET_JOURNAL)
        return (uint8_t)(0x10u | (unsigned)top | ((unsigned)bottom << 2));
    return (uint8_t)(0x80u | (recent ? 0x40u : 0u) | (unsigned)top | ((unsigned)bottom << 3));
}

uint8_t app_home_components_value(void) {
    return encode(s_slots[0], s_slots[1], s_library_recent);
}

void app_home_components_apply(uint8_t value) {
    if (!app_home_components_value_valid(value)) return;
    s_slots[0] = (app_home_widget_t)(value & ((value & 0x80u) ? 7u : 3u));
    s_slots[1] = (app_home_widget_t)((value >> ((value & 0x80u) ? 3 : 2)) & ((value & 0x80u) ? 7u : 3u));
    s_library_recent = (value & 0xc0u) == 0xc0u;
    s_loaded = true;
}

void app_home_components_init(bool legacy_weather_enabled) {
    if (s_loaded) return;
    s_loaded = true;
    s_library_recent = false;
    s_slots[APP_HOME_SLOT_TOP] = APP_HOME_WIDGET_READING;
    s_slots[APP_HOME_SLOT_BOTTOM] = legacy_weather_enabled ? APP_HOME_WIDGET_WEATHER : APP_HOME_WIDGET_EMPTY;
    nvs_handle_t handle;
    if (nvs_open("read_pico", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t value = 0;
    if (nvs_get_u8(handle, "home_regions", &value) == ESP_OK) app_home_components_apply(value);
    nvs_close(handle);
}

app_home_widget_t app_home_component_at(unsigned slot) {
    return slot < APP_HOME_SLOT_COUNT ? s_slots[slot] : APP_HOME_WIDGET_EMPTY;
}

static bool save(uint8_t value) {
    if (!epd_flash_begin()) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("read_pico", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "home_regions", value);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    epd_flash_end();
    if (err != ESP_OK) return false;
    app_home_components_apply(value);
    return true;
}

bool app_home_component_set(unsigned slot, app_home_widget_t widget) {
    if (slot >= APP_HOME_SLOT_COUNT || (unsigned)widget > APP_HOME_WIDGET_MINI_LIBRARY) return false;
    app_home_components_init(false);
    if (s_slots[slot] == widget) return true;
    app_home_widget_t next[APP_HOME_SLOT_COUNT] = {s_slots[0], s_slots[1]};
    unsigned other = slot == APP_HOME_SLOT_TOP ? APP_HOME_SLOT_BOTTOM : APP_HOME_SLOT_TOP;
    if (widget != APP_HOME_WIDGET_EMPTY && next[other] == widget) next[other] = next[slot];
    next[slot] = widget;
    return save(encode(next[0], next[1], s_library_recent));
}

bool app_home_library_recent(void) { return s_library_recent; }
bool app_home_library_set_recent(bool recent) {
    app_home_components_init(false);
    return s_library_recent == recent || save(encode(s_slots[0], s_slots[1], recent));
}

bool app_home_has_widget(app_home_widget_t widget) {
    for (unsigned i = 0; i < APP_HOME_SLOT_COUNT; ++i) if (s_slots[i] == widget) return true;
    return false;
}
