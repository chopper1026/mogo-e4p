/* 离线完整引导及配网返回；只替换平台 I/O。/ Exercise the actual offline wizard and provisioning return using platform fakes. */
#include <assert.h>
#include "../main/assets/setup_font_samples.h"
#include "app_setup.h"
#include "font_download.h"
#include "home_components.h"
#include "transfer_ui_test_env.h"
void (*test_observe_pixel)(int x, int y, uint8_t gray);
static ui_language_t saved;
static setup_step_t saved_step;
static app_home_widget_t actual[2];
static int writes, downloads;
static font_job_state_t job;
static bool wifi_success, save_failed;
static const app_desc_t home, transfer, settings_page;
static const app_desc_t* setup_settings_page(int index) { assert(index == 3); return &settings_page; }
#define app_at setup_settings_page
extern const app_desc_t app_setup;
const app_desc_t* app_setup_page(void) {
    return &app_setup;
}
static const app_desc_t* app_home_page(void) {
    return &home;
}
ui_language_t ui_language(void) {
    return saved;
}
setup_step_t app_setup_step(void) {
    return saved_step;
}
bool app_setup_save(ui_language_t l, setup_step_t s) {
    if (save_failed) return false;
    saved = l;
    saved_step = s;
    return true;
}
const char* ui_language_name(ui_language_t l) {
    return ((const char*[]){ "简体中文", "繁體中文", "English" })[l];
}
#include "catalog.inc"
void app_home_components_init(bool b) {
    (void)b;
}
app_home_widget_t app_home_component_at(unsigned i) {
    return actual[i];
}
bool app_home_components_save_pair(app_home_widget_t a, app_home_widget_t b) {
    actual[0] = a;
    actual[1] = b;
    ++writes;
    return true;
}
static void app_weather_init(void) {}
static const struct {
    bool enabled;
} weather = { false };
#define app_weather_view() (&weather)
bool font_download_start(void) {
    ++downloads;
    return true;
}
bool font_download_quiesce(unsigned t) {
    (void)t;
    return true;
}
font_job_state_t font_download_state(void) {
    return job;
}
static unsigned received;
unsigned font_download_percent(void) {
    return received;
}
const char* font_download_path(void) {
    return "font";
}
uint32_t font_download_bytes(void) {
    return 0;
}
void app_transfer_open_setup_wifi(app_ctx_t* c) {
    wifi_success = false;
    c->request_app = &transfer;
}
bool app_transfer_take_setup_wifi_success(void) {
    bool result = wifi_success;
    wifi_success = false;
    return result;
}
static int ttf_font_open(const char* p) {
    (void)p;
    return ESP_OK;
}
static const char* ttf_font_path(void) {
    return "font";
}
static void app_settings_set_system_font_path(const char* p) {
    (void)p;
}
static const char* app_settings_system_font_path(void) {
    return "font";
}
static void app_font_activate_system(void) {}
static char heading[128], counter[24];
typedef EpdRect display_rect_t;
#define display_rect_from_epd(r) (r)
enum EpdFontFlags { test_align_left = EPD_DRAW_ALIGN_LEFT };
static int meter_paints;
static void ui_setup_text_box(
    uint8_t* fb, EpdRect r, int px, const char* text, enum EpdFontFlags align, bool inverted) {
    (void)align;
    (void)inverted;
    if (strchr(text, '%')) ++meter_paints;
    (void)fb;
    (void)px;
    if (r.x == 456 && r.y == 40) snprintf(counter, sizeof(counter), "%s", text);
    if (r.y == 182) snprintf(heading, sizeof(heading), "%s", text);
}
#define ui_draw_control_frame(...) ((void)0)
#define epd_fill_circle(...) ((void)0)
#include "setup_page.inc"
static app_redraw_t hit(app_ctx_t* c, int x, int y) {
    return app_setup.on_gesture(
        c, &(ui_gesture_event_t){ .type = UI_GESTURE_TAP, .x0 = x, .y0 = y });
}
static void boot(app_ctx_t* c) {
    assert(app_setup_start(APP_SETUP_RESUME) == &app_setup);
    app_setup.on_enter(c);
}
uintptr_t _rodata_start = UINTPTR_MAX, _rodata_end;
int main(void) {
    for (size_t i = 0; i < sizeof(catalog) / sizeof(catalog[0]); ++i) {
        const char* items[] = { catalog[i].key, catalog[i].tw, catalog[i].en };
        for (unsigned j = 0; j < 3; ++j) {
            uintptr_t a = (uintptr_t)items[j], b = a + strlen(items[j]) + 1;
            if (a < _rodata_start) _rodata_start = a;
            if (b > _rodata_end) _rodata_end = b;
        }
    }
    char signature[] = "系统语言";
    assert(ui_tr_for(UI_LANG_EN, signature) == signature);
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        app_ctx_t c = { 0 };
        saved = UI_LANG_CN;
        saved_step = SETUP_LANGUAGE;
        actual[0] = APP_HOME_WIDGET_READING;
        actual[1] = APP_HOME_WIDGET_WEATHER;
        test_status = (wifi_transfer_status_t){0};
        boot(&c);
        hit(&c, 55, language_row(l).y + 25);
        assert(saved == UI_LANG_CN);
        app_setup.render(&c, NULL);
        assert(!strcmp(heading, ((const char*[]){ "系统语言", "系統語言", "System language" })[l]));
        hit(&c, 55, 1040);
        assert((int)saved == l && saved_step == SETUP_WIFI);
        boot(&c);
        app_setup.render(&c, NULL);
        assert(!strcmp(heading, "Wi-Fi") && !strcmp(counter, "02 / 04"));
        int start = downloads;
        save_failed = true;
        app_back_result_t back = app_setup.edge_back(&c);
        assert(!back.accepted && back.redraw == APP_REDRAW_PAGE && step == SETUP_WIFI && notice && !c.request_app);
        save_failed = false; back = app_setup.edge_back(&c);
        assert(back.accepted && back.redraw == APP_REDRAW_PAGE && !notice);
        assert(saved_step == SETUP_LANGUAGE);
        hit(&c, 55, 1040);
        assert(saved_step == SETUP_WIFI);
        hit(&c, action_rect(true).x + 32, 1064);
        assert(c.request_app == &transfer && saved_step == SETUP_WIFI);
        test_status.network_ready = true;
        test_status.mode = WIFI_TRANSFER_MODE_AP;
        app_setup.on_exit(&c);
        c.request_app = NULL;
        app_setup.on_enter(&c);
        assert(!wifi_ready && step == SETUP_WIFI && downloads == start);
        if (l == UI_LANG_EN) {
            hit(&c, action_rect(true).x + 32, 1064);
            assert(c.request_app == &transfer);
            app_setup.on_exit(&c);
            c.request_app = NULL;
            wifi_success = true;
            test_status.mode = WIFI_TRANSFER_MODE_STA;
            app_setup.on_enter(&c);
            assert(!wifi_success);
        } else {
            if (l == UI_LANG_TW) {
                test_status.mode = WIFI_TRANSFER_MODE_STA;
                c.now_ms += 1000;
                assert(app_setup.on_tick(&c) == APP_REDRAW_PAGE && wifi_ready);
                c.now_ms += 1000;
                assert(app_setup.on_tick(&c) == APP_REDRAW_NONE);
            }
            hit(&c, action_rect(wifi_ready).x + 32, 1064);
        }
        assert(saved_step == SETUP_FONT && downloads == start);
        hit(&c, 64, 64);
        assert(saved_step == SETUP_WIFI);
        hit(&c, action_rect(wifi_ready).x + 32, 1064);
        boot(&c);
        app_setup.render(&c, NULL);
        assert(step == SETUP_FONT && !strcmp(counter, "03 / 04") && downloads == start);
        hit(&c, action_rect(download_is_primary()).x + 32, 1064);
        job = FONT_BUSY;
        received = 9;
        c.now_ms += 499;
        assert(app_setup.on_tick(&c) == APP_REDRAW_NONE);
        c.now_ms++;
        int painted = meter_paints;
        assert(
            app_setup.on_tick(&c) == APP_REDRAW_AREA && progress == 5 &&
            meter_paints == painted + 1);
        EpdRect a = app_setup.area_hint(&c);
        assert(
            a.x == meter.x && a.y == meter.y && a.width == meter.width && a.height == meter.height);
        c.now_ms += 500;
        assert(app_setup.on_tick(&c) == APP_REDRAW_NONE);
        received = 99;
        assert(app_setup.on_tick(&c) == APP_REDRAW_AREA && progress == 95);
        job = FONT_NO_CARD;
        app_setup.on_tick(&c);
        assert(!c.request_app && downloads == start + 1);
        hit(&c, action_rect(download_is_primary()).x + 32, 1064);
        job = FONT_NEEDS_WIFI;
        app_setup.on_tick(&c);
        assert(c.request_app == &transfer);
        app_setup.on_exit(&c);
        c.request_app = NULL;
        app_setup.on_enter(&c);
        assert(downloads == start + 2);
        boot(&c);
        assert(downloads == start + 2);
        hit(&c, action_rect(!download_is_primary()).x + 32, 1064);
        assert(saved_step == SETUP_HOME);
        boot(&c);
        int old = writes;
        assert(hit(&c, widget_cell(2, 0).x + 40, widget_cell(2, 0).y + 40) == APP_REDRAW_AREA);
        assert(
            slots[0] == APP_HOME_WIDGET_WEATHER && slots[1] == APP_HOME_WIDGET_READING &&
            writes == old);
        hit(&c, widget_cell(4, 0).x + 40, widget_cell(4, 0).y + 40);
        hit(&c, widget_cell(4, 1).x + 40, widget_cell(4, 1).y + 40);
        hit(&c, 64, 64);
        assert(step == SETUP_FONT && saved_step == SETUP_FONT);
        hit(&c, action_rect(!download_is_primary()).x + 32, 1064);
        assert(step == SETUP_HOME);
        hit(&c, 55, 1040);
        assert(
            actual[0] == APP_HOME_WIDGET_EMPTY && actual[1] == APP_HOME_WIDGET_EMPTY &&
            writes == old + 1 && c.request_app == &home && saved_step == SETUP_DONE);
        app_setup_start(APP_SETUP_LANGUAGE_ONLY);
        app_setup.on_enter(&c);
        c.request_app = NULL;
        hit(&c, 55, 1040);
        assert(saved_step == SETUP_DONE && c.request_app == &settings_page && !returning_from_wifi);
        app_setup_start(APP_SETUP_REPLAY);
        assert((int)preview == l);
        app_setup.on_enter(&c);
        assert(slots[0] == actual[0] && slots[1] == actual[1]);
        c.request_app = NULL; saved_step = SETUP_WIFI; save_failed = true;
        back = app_setup.edge_back(&c);
        assert(!back.accepted && back.redraw == APP_REDRAW_PAGE && notice && !c.request_app);
        save_failed = false; back = app_setup.edge_back(&c);
        assert(back.accepted && back.redraw == APP_REDRAW_NONE && c.request_app == &settings_page);
        app_setup_start(APP_SETUP_FONT_ONLY);
        app_setup.on_enter(&c);
        hit(&c, 342, 1064);
        job = FONT_READY;
        assert(app_setup.on_tick(&c) == APP_REDRAW_PAGE && installed && !pending);
        int done = downloads;
        hit(&c, 342, 1064);
        assert(downloads == done);
        app_setup.on_media_lost(&c);
        assert(!installed);
        c.request_app = NULL;
        hit(&c, 342, 1064);
        job = FONT_NEEDS_WIFI;
        assert(app_setup.on_tick(&c) == APP_REDRAW_NONE && c.request_app == &transfer);
        app_setup.on_exit(&c);
        c.request_app = NULL;
        wifi_success = true;
        test_status = (wifi_transfer_status_t){ .mode=WIFI_TRANSFER_MODE_STA, .network_ready=true };
        app_setup.on_enter(&c);
        assert(step == SETUP_FONT && pending && !wifi_success && downloads == done + 2);
        app_setup.on_before_lock(&c);
        assert(!pending);

    }
    puts(
        "PASS: three offline locales, preview/confirm, reboot after each step, no download on "
        "resume, Wi-Fi skip/cancel/success, widget swap, dual blanks and replay");
}
