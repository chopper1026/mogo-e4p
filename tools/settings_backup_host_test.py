#!/usr/bin/env python3
"""中文：以地址和未定义行为检查运行真实设置备份回归。
English: Run actual settings-backup regressions with address and undefined-behavior checks.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
(ROOT / "build/book-tests").mkdir(parents=True, exist_ok=True)
# 复用真实设置页的确认、忙态绘制和 tick 分发，不在夹具里重写流程。
# Reuse the real page's confirmation, busy paint and tick dispatch instead of reimplementing them.
PAGE = ROOT / "main/apps/app_device_settings.c"
source = PAGE.read_text()
controls = source[source.index("static bool s_config_confirm;"):
                  source.index("static bool s_batt_calibration_confirm;")]
touch = function("on_gesture", PAGE)
input_gate = touch.splitlines()[1]
assert "config_inputs_blocked(ctx->now_ms - ev->hold_ms)" in input_gate
config_tap = touch[touch.index("    if (s_page == SETTINGS_CONFIG) {"):
                   touch.index("    if (y<160 || y>=UI_NAV_TOP")]
tick_gate = function("on_tick", PAGE).splitlines()[1]
assert "return config_restore_tick(ctx)" in tick_gate

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl; uint8_t *fb; int64_t now_ms;} app_ctx_t;
typedef struct {int x0,y0; int64_t hold_ms;} ui_gesture_event_t;
typedef struct {int unused;} app_desc_t;
typedef enum {APP_REDRAW_NONE, APP_REDRAW_PAGE, APP_REDRAW_DONE} app_redraw_t;
typedef int esp_err_t;
enum {SETTINGS_MAIN, SETTINGS_CONFIG};
enum {ESP_OK, ESP_ERR_INVALID_STATE, APP_RESTORE_NO_CARD, APP_RESTORE_INTERNAL_VOLUME,
      ESP_ERR_NVS_NOT_ENOUGH_SPACE, ESP_ERR_NOT_FOUND, ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_SIZE};
enum {DISPLAY_REFRESH_FEEDBACK, DISPLAY_REFRESH_DEFAULT, UI_GRAY_WHITE};
enum {EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_LEFT};
enum EpdDrawError {EPD_DRAW_SUCCESS, EPD_DRAW_FAILED};
const app_desc_t app_device_settings = {0};
static int s_page = SETTINGS_CONFIG;
static char s_notice[96];
static int64_t clock_ms;
static int restores, saves, results, partials, busy_paints, buttons, activated, lights, books_changed;
static bool wifi;
static esp_err_t outcome;
static enum EpdDrawError push_error;
static int64_t esp_timer_get_time(void) {return clock_ms * 1000;}
static bool ui_rect_hit(EpdRect r, int x, int y) {
    return x >= r.x && x < r.x+r.width && y >= r.y && y < r.y+r.height;
}
static void ui_fill_rect_fast(EpdRect r, int gray, uint8_t *fb) {
    assert(fb && gray == UI_GRAY_WHITE && r.y == 561 && r.y+r.height == 859);
}
static void ui_draw_button(uint8_t *fb, EpdRect r, const char *text, bool on) {
    assert(fb && r.width == 612 && text && !on); ++buttons;
}
static void ui_text_box(uint8_t *fb, EpdRect r, int px, const char *text, int align, bool inverted) {
    assert(fb && px > 0 && !inverted);
    if (r.y == 681) {
        assert(align == EPD_DRAW_ALIGN_CENTER && !strcmp(text, "正在恢复，请稍候…"));
        ++busy_paints;
    } else assert(r.y == 819 && align == EPD_DRAW_ALIGN_LEFT && *text);
}
static enum EpdDrawError display_refresh_area(void *hl, int mode, EpdRect r, int flags) {
    assert(hl && mode == DISPLAY_REFRESH_FEEDBACK && flags == DISPLAY_REFRESH_DEFAULT);
    assert(r.y == 561 && r.y+r.height == 859 && busy_paints > partials);
    assert(restores == results); ++partials; return push_error;
}
static void guard_draw_result(void *hl, enum EpdDrawError err) {assert(hl && err == push_error);}
static esp_err_t app_settings_backup_save(void) {++saves; return ESP_OK;}
static const char *app_settings_backup_bad_bookmark(void) {return "";}
static esp_err_t app_settings_backup_restore(void) {
    assert(partials > restores && restores == results); ++restores; clock_ms += 2350; return outcome;
}
static void ttf_font_scan(void) {}
static void app_font_activate_system(void) {++activated;}
static void ui_text_set_system_scale(bool sys) {assert(sys);}
static bool frontlight_ready(void) {return true;}
static bool app_settings_frontlight_on(void) {return true;}
static int app_settings_frontlight_brightness(void) {return 12;}
static int app_settings_frontlight_cct(void) {return 42;}
static int frontlight_apply(bool on, int brightness, int cct) {
    assert(on && brightness == 12 && cct == 42); ++lights; return 0;
}
static void book_store_notify_changed(void) {++books_changed;}
static int wifi_transfer_get_saved_wifi(char *ssid, bool *saved) {*ssid = 0; *saved = wifi; return 0;}
''' + controls + function("config_render_controls", PAGE) + r'''
static void app_present(app_ctx_t *ctx, const app_desc_t *app, app_redraw_t redraw) {
    assert(app == &app_device_settings && redraw == APP_REDRAW_PAGE && !settings_modal_input_owned(ctx));
    assert(!s_config_confirm && *s_notice && restores == results+1);
    int before = buttons;
    config_render_controls(ctx->fb);
    assert(buttons == before+2);
    ++results; clock_ms += 190;
}
''' + function("config_restore_tick", PAGE) + "\nstatic app_redraw_t tick(app_ctx_t *ctx) {\n" + tick_gate + r'''
    return APP_REDRAW_NONE;
}
static app_redraw_t tap(app_ctx_t *ctx, int x, int y, int64_t started_ms, int64_t ended_ms) {
    const ui_gesture_event_t event = {.x0=x,.y0=y,.hold_ms=ended_ms-started_ms}, *ev=&event;
    ctx->now_ms = ended_ms;
''' + input_gate + '\n' + config_tap + r'''
    return APP_REDRAW_NONE;
}
int main(void) {
    uint8_t fb = 0;
    app_ctx_t ctx = {.fb=&fb,.hl=&fb};
    assert(tap(&ctx, 2, 721, 25, 50) == APP_REDRAW_NONE && !s_config_confirm);
    assert(tap(&ctx, 300, 721, 70, 90) == APP_REDRAW_PAGE && s_config_confirm && !partials);
    assert(tick(&ctx) == APP_REDRAW_NONE && !restores);
    assert(tap(&ctx, 300, 721, 130, 150) == APP_REDRAW_DONE && !s_config_confirm);
    assert(partials == 1 && !restores && !buttons && settings_modal_input_owned(&ctx));
    assert(tap(&ctx, 300, 721, 170, 180) == APP_REDRAW_NONE);
    assert(tap(&ctx, 300, 600, 200, 250) == APP_REDRAW_NONE && !saves);
    wifi = true; clock_ms = 280;
    assert(tick(&ctx) == APP_REDRAW_DONE && restores == 1 && results == 1);
    assert(strstr(s_notice, "WiFi 请重新连接") && activated == 1 && lights == 1 && books_changed == 1);
    assert(!settings_modal_input_owned(&ctx) && s_config_ignore_through_ms == clock_ms);
    assert(tap(&ctx, 300, 721, clock_ms-205, clock_ms-180) == APP_REDRAW_NONE);
    assert(tap(&ctx, 300, 721, clock_ms-10, clock_ms+10) == APP_REDRAW_NONE);
    assert(tap(&ctx, 300, 600, clock_ms-3, clock_ms+12) == APP_REDRAW_NONE);
    assert(!s_config_confirm && !saves && tick(&ctx) == APP_REDRAW_NONE && restores == 1);
    assert(tap(&ctx, 300, 721, clock_ms+1, clock_ms+25) == APP_REDRAW_PAGE && s_config_confirm);
    push_error = EPD_DRAW_FAILED;
    assert(tap(&ctx, 300, 721, clock_ms+50, clock_ms+60) == APP_REDRAW_PAGE && partials == 2);
    assert(settings_modal_input_owned(&ctx) && restores == 1);
    int before = busy_paints;
    config_render_controls(ctx.fb);
    assert(busy_paints == before+1);
    outcome = APP_RESTORE_NO_CARD; clock_ms += 180;
    assert(tick(&ctx) == APP_REDRAW_DONE && restores == 2 && results == 2);
    assert(strstr(s_notice, "未识别到 TF 卡") && activated == 1 && lights == 1 && books_changed == 1);
    assert(tap(&ctx, 300, 600, clock_ms+30, clock_ms+44) == APP_REDRAW_PAGE && saves == 1);
    puts("PASS: restore feedback precedes I/O; replayed taps cannot rearm restore; success and failure unlock controls");
}
'''

with tempfile.TemporaryDirectory(prefix="settings-backup-") as directory:
    folder = Path(directory)
    env, _ = toolchain(folder)
    binary = folder / "test"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-Itools/settings_backup_stubs", "-Icomponents/restore_nvs/include", "-Icomponents/wifi_transfer/include",
                    "-Imain", "-Imain/services", "tools/settings_backup_host_test.c", "-o", str(binary)],
                   cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
    fixture = folder / "restore.c"
    fixture.write_text(unit, encoding="utf-8")
    binary = folder / "restore"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    str(fixture), "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
