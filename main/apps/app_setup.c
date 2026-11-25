/* 四步引导；配网和字体下载都可跳过。/ Four-step setup with optional Wi-Fi and font download. */
#include "app_setup.h"
#include <stdio.h>
#include <string.h>
#include "app_font_context.h"
#include "app_registry.h"
#include "app_transfer_mode.h"
#include "assets/setup_font_samples.h"
#include "display_geometry.h"
#include "font_download.h"
#include "home_components.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_locale.h"
#include "weather_service.h"
#include "wifi_transfer.h"

static app_setup_mode_t mode;
static ui_language_t preview;
static setup_step_t step;
static app_home_widget_t slots[2];
static const char* notice;
static bool pending, returning_from_wifi;
static bool wifi_ready;
static char wifi_ssid[33];
static int64_t wifi_polled_ms;
static const setup_step_t pages[] = { SETUP_LANGUAGE, SETUP_WIFI, SETUP_FONT, SETUP_HOME };
enum { PAGE_COUNT = sizeof(pages) / sizeof(pages[0]) };
static unsigned progress;
static bool installed, load_slots;
static int64_t last_meter_ms;
static const bool full_modes[] = { true, true, false, false };
static const char* const widgets[] = { "最近阅读", "天气", "留白", "阅读手账", "迷你书库" };
static const app_home_widget_t choices[] = { APP_HOME_WIDGET_READING, APP_HOME_WIDGET_MINI_LIBRARY,
                                             APP_HOME_WIDGET_WEATHER, APP_HOME_WIDGET_JOURNAL,
                                             APP_HOME_WIDGET_EMPTY };
const app_desc_t* app_setup_start(app_setup_mode_t requested) {
    mode = requested;
    preview = ui_language();
    notice = NULL;
    pending = returning_from_wifi = installed = false;
    load_slots = true;
    step = mode == APP_SETUP_RESUME      ? app_setup_step()
           : mode == APP_SETUP_FONT_ONLY ? SETUP_FONT
                                         : SETUP_LANGUAGE;
    return app_setup_page();
}
static void start_download(app_ctx_t* ctx) {
    pending = font_download_start();
    progress = 0;
    last_meter_ms = ctx->now_ms;
    notice = pending ? NULL : "下载失败，请重试";
}
static bool advance(setup_step_t next);
static bool read_wifi(void) {
    wifi_transfer_status_t status;
    wifi_transfer_get_status(&status);
    bool ready = status.mode == WIFI_TRANSFER_MODE_STA && status.network_ready;
    const char* ssid = ready ? status.wifi_ssid : "";
    bool changed = wifi_ready != ready || strcmp(wifi_ssid, ssid) != 0;
    wifi_ready = ready;
    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
    return changed;
}
static void enter(app_ctx_t* ctx) {
    if (load_slots) {
        load_slots = false;
        app_weather_init();
        app_home_components_init(app_weather_view()->enabled);
        for (unsigned i = 0; i < 2; i++) slots[i] = app_home_component_at(i);
    }
    if (step == SETUP_WIFI || returning_from_wifi) {
        read_wifi();
        wifi_polled_ms = ctx->now_ms;
        if (returning_from_wifi && app_transfer_take_setup_wifi_success() && wifi_ready) {
            if (step == SETUP_WIFI) advance(SETUP_FONT);
            else if (step == SETUP_FONT) start_download(ctx);
        }
        returning_from_wifi = false;
    }
}
static bool modal(app_ctx_t* ctx) {
    (void)ctx;
    return true;
}
static bool no_menu(app_ctx_t* ctx) {
    (void)ctx;
    return false;
}
static void stop(app_ctx_t* ctx) {
    (void)ctx;
    if (pending) {
        font_download_quiesce(3500);
        pending = false;
        notice = "下载已取消";
    }
}
/* ---- 纸页与控件 / Page and controls ---- */
enum { LEFT = 40, WIDTH = 604, SAMPLE_TOP = 526, FOOT_Y = 1036, ROW_Y = 448, ROW_H = 94 };
static const EpdRect back_rect = { LEFT, 40, 160, 70 };
static const EpdRect meter = { LEFT, 790, WIDTH, 64 };
static EpdRect dirty;

static bool has_back(void) {
    return step != SETUP_LANGUAGE || mode != APP_SETUP_RESUME;
}
static EpdRect language_row(int i) {
    return (EpdRect){ LEFT, 356 + i * 143, WIDTH, 143 };
}
static EpdRect widget_cell(int i, int slot) {
    return (EpdRect){ 356 + slot * 144, ROW_Y + i * ROW_H, 144, ROW_H };
}
static EpdRect action_rect(bool primary) {
    bool split = (step == SETUP_WIFI && !wifi_ready) ||
                 (step == SETUP_FONT && !installed && full_modes[mode]);
    return (EpdRect){ split && primary ? 312 : LEFT, FOOT_Y, split ? (primary ? 332 : 252) : WIDTH,
                      92 };
}
static void text_box(
    uint8_t* fb, EpdRect r, int px, const char* s, enum EpdFontFlags align, bool inv) {
    // 预览不修改系统语言；栈上副本不会被普通绘制路径二次翻译。/ Preview independently; stack text bypasses a second translation in the shared renderer.
    char shown[160];
    snprintf(shown, sizeof(shown), "%s", ui_tr_for(preview, s));
    ui_setup_text_box(fb, r, px, shown, align, inv);
}
static void left(uint8_t* fb, EpdRect r, int px, const char* s) {
    text_box(fb, r, px, s, EPD_DRAW_ALIGN_LEFT, false);
}
static void rule(uint8_t* fb, int y) {
    ui_fill_rect_fast((EpdRect){ LEFT, y, WIDTH, 2 }, 0x90, fb);
}
static void radio_mark(uint8_t* fb, int x, int y, bool on) {
    epd_fill_circle(x, y, 13, 0, fb);
    epd_fill_circle(x, y, 11, 255, fb);
    if (on) epd_fill_circle(x, y, 7, 0, fb);
}
static int page_index(void) {
    for (int i = 0; i < PAGE_COUNT; ++i) if (pages[i] == step) return i;
    return 0;
}
static void draw_header(uint8_t* fb) {
    if (has_back()) {
        ui_draw_icon(fb, 54, 75, 26, UI_ICON_CHEVRON_LEFT, 0);
        left(fb, (EpdRect){ 78, 40, 120, 70 }, 26, "返回");
    } else {
        left(fb, back_rect, 34, "mogo");
    }
    if (full_modes[mode]) {
        int index = page_index();
        char count[16];
        snprintf(count, sizeof(count), "%02d / %02d", index + 1, PAGE_COUNT);
        text_box(fb, (EpdRect){ 456, 40, 188, 70 }, 26, count, EPD_DRAW_ALIGN_RIGHT, false);
        enum { GAP = 8, SEGMENT = (WIDTH - (PAGE_COUNT - 1) * GAP) / PAGE_COUNT };
        for (int i = 0; i < PAGE_COUNT; ++i)
            ui_fill_rect_fast(
                (EpdRect){ LEFT + (SEGMENT + GAP) * i, 132, SEGMENT, i <= index ? 6 : 2 },
                i <= index ? 0 : 0x90, fb);
    } else
        rule(fb, 132);
    left(
        fb, (EpdRect){ LEFT, 182, WIDTH, 84 }, 60,
        step == SETUP_LANGUAGE ? "系统语言"
        : step == SETUP_WIFI   ? "Wi-Fi"
        : step == SETUP_FONT   ? "系统字体"
                               : "首页组件");
    left(
        fb, (EpdRect){ LEFT, 275, WIDTH, 52 }, 27,
        step == SETUP_LANGUAGE  ? "选择你想使用的语言。"
        : step == SETUP_WIFI    ? "连接网络，或稍后在设置中连接。"
        : step == SETUP_HOME    ? "为首页的上下两区各选一项。"
        : preview == UI_LANG_EN ? "英文界面无需下载字体"
                                : "只更改系统字体，不更改阅读字体");
}
static void draw_languages(uint8_t* fb) {
    for (int i = 0; i < UI_LANG_COUNT; ++i) {
        EpdRect r = language_row(i);
        rule(fb, r.y);
        left(
            fb, (EpdRect){ r.x + 4, r.y + 10, WIDTH - 84, r.height - 20 }, 43, ui_language_name(i));
        radio_mark(fb, 616, r.y + r.height / 2, i == (int)preview);
    }
    rule(fb, language_row(UI_LANG_COUNT).y);
}
static void draw_wifi(uint8_t* fb) {
    rule(fb, 370);
    ui_draw_icon(fb, LEFT + 23, 445, 44, UI_ICON_WIFI, 0);
    left(fb, (EpdRect){ LEFT + 68, 413, WIDTH - 68, 64 }, 28, "2.4 GHz Wi-Fi");
    left(fb, (EpdRect){ LEFT, 505, WIDTH, 90 }, 52, wifi_ready ? "已连接" : "尚未连接");
    EpdRect detail = { LEFT, 606, WIDTH, 64 };
    if (wifi_ready && wifi_ssid[0])
        ui_setup_text_box(fb, detail, 30, wifi_ssid, EPD_DRAW_ALIGN_LEFT, false);
    else
        left(fb, detail, 28, wifi_ready ? "可以继续设置系统字体。" : "无需网络也能完成引导。");
    rule(fb, 708);
}
static void draw_font(uint8_t* fb) {
    char meta[64];
    rule(fb, 346);
    left(fb, (EpdRect){ LEFT, 358, WIDTH, 45 }, 25, "推荐字体");
    left(fb, (EpdRect){ LEFT, 403, WIDTH, 66 }, 52, "霞鹜新晰黑");
    snprintf(
        meta, sizeof(meta), "Screen   /   %.1f MiB",
        font_download_bytes() / 1048576.0);
    left(fb, (EpdRect){ LEFT, 469, WIDTH, 45 }, 28, meta);
    enum { PAIRS = SETUP_SAMPLE_WIDTH / 2 };
    _Static_assert(SETUP_SAMPLE_WIDTH <= WIDTH && SETUP_SAMPLE_WIDTH % 2 == 0, "Specimen width");
    const uint8_t* sample = setup_samples[preview];
    for (int pos = 0; pos < PAIRS * SETUP_SAMPLE_HEIGHT;) {
        uint8_t token = *sample++;
        int count = (token & 127) + 1;
        if (!(token & 128)) {
            pos += count;
            continue;
        }
        while (count--) {
            uint8_t pair = *sample++;
            int x = LEFT + 2 * (pos % PAIRS), y = SAMPLE_TOP + pos / PAIRS;
            if (pair >> 4) epd_draw_pixel(x, y, 255 - 17 * (pair >> 4), fb);
            if (pair & 15) epd_draw_pixel(x + 1, y, 255 - 17 * (pair & 15), fb);
            ++pos;
        }
    }
    rule(fb, 708);
    ui_draw_icon(fb, 56, 752, 30, UI_ICON_WIFI, 0);
    left(fb, (EpdRect){ 88, 718, WIDTH - 48, 68 }, 25, "通过 Wi-Fi 下载到 TF 卡");
}
static void draw_meter(uint8_t* fb) {
    char pct[12];
    snprintf(pct, sizeof(pct), "%u%%", progress);
    ui_clear_rect_fast(fb, meter);
    // 黑白轨道避免 DU 无法还原浅灰填充。/ A binary track avoids gray fills that DU cannot reproduce.
    ui_fill_rect_fast((EpdRect){ LEFT, meter.y + 31, 488, 2 }, 0, fb);
    int filled = (int)(488 * progress / 100);
    if (filled) ui_fill_rect_fast((EpdRect){ LEFT, meter.y + 26, filled, 12 }, 0, fb);
    text_box(
        fb, (EpdRect){ 540, meter.y, 104, meter.height }, 30, pct, EPD_DRAW_ALIGN_RIGHT, false);
}
static EpdRect mark_rect(int row, int slot) {
    EpdRect r = widget_cell(row, slot);
    return (EpdRect){ r.x + r.width / 2 - 14, r.y + r.height / 2 - 14, 28, 28 };
}
static void draw_choice(uint8_t* fb, int row, int slot) {
    EpdRect r = mark_rect(row, slot);
    radio_mark(fb, r.x + 14, r.y + 14, slots[slot] == choices[row]);
}
static void draw_layout(uint8_t* fb) {
    for (int slot = 0; slot < 2; ++slot) {
        EpdRect r = widget_cell(0, slot);
        text_box(
            fb, (EpdRect){ r.x + 4, ROW_Y - 84, r.width - 8, 80 }, 25,
            slot ? "下方区域" : "上方区域", EPD_DRAW_ALIGN_CENTER, false);
    }
    for (int i = 0; i < 5; ++i) {
        EpdRect r = widget_cell(i, 0);
        rule(fb, r.y);
        left(fb, (EpdRect){ LEFT + 4, r.y + 4, 300, r.height - 8 }, 33, widgets[choices[i]]);
        for (int slot = 0; slot < 2; ++slot) draw_choice(fb, i, slot);
    }
    rule(fb, widget_cell(5, 0).y);
}
static bool download_is_primary(void) {
    return mode == APP_SETUP_FONT_ONLY || preview != UI_LANG_EN;
}
static void action(uint8_t* fb, bool primary, const char* s) {
    EpdRect r = action_rect(primary);
    if (primary)
        ui_fill_round_rect(fb, r, 8, 0);
    else
        ui_draw_control_frame(fb, r, 8, 0);
    text_box(
        fb, (EpdRect){ r.x + 14, r.y + 6, r.width - 28, r.height - 12 }, 30, s,
        EPD_DRAW_ALIGN_CENTER, primary);
}
static void draw_footer(uint8_t* fb) {
    if (notice) left(fb, (EpdRect){ LEFT, 936, WIDTH, 76 }, 27, notice);
    if (step == SETUP_WIFI && !wifi_ready) {
        action(fb, false, "暂时跳过");
        action(fb, true, "选择网络");
    } else if (step != SETUP_FONT || installed) {
        action(
            fb, true,
            step == SETUP_HOME || mode == APP_SETUP_FONT_ONLY ? "完成"
            : mode == APP_SETUP_LANGUAGE_ONLY                 ? "保存"
                                                              : "下一步");
    } else {
        bool dl = download_is_primary();
        action(fb, dl, pending ? "取消下载" : "下载并使用");
        if (full_modes[mode]) action(fb, !dl, "暂时跳过");
    }
}
static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    ui_clear_page(fb);
    draw_header(fb);
    if (step == SETUP_LANGUAGE)
        draw_languages(fb);
    else if (step == SETUP_WIFI)
        draw_wifi(fb);
    else if (step == SETUP_HOME)
        draw_layout(fb);
    else {
        draw_font(fb);
        if (pending) draw_meter(fb);
    }
    draw_footer(fb);
}
static display_rect_t area_hint(app_ctx_t* ctx) {
    (void)ctx;
    return display_rect_from_epd(dirty);
}
static bool advance(setup_step_t next) {
    if (!app_setup_save(preview, full_modes[mode] ? next : app_setup_step())) {
        notice = "保存失败，请重试";
        return false;
    }
    step = next;
    if (step == SETUP_WIFI) read_wifi();
    notice = NULL;
    return true;
}
static app_redraw_t setup_back(app_ctx_t *ctx) {
    if (!has_back()) return APP_REDRAW_NONE;
    stop(ctx);
    if (!full_modes[mode] || step == SETUP_LANGUAGE) {
        if (mode == APP_SETUP_REPLAY && app_setup_step() != SETUP_DONE &&
            !app_setup_save(ui_language(), SETUP_DONE)) {
            notice = "保存失败，请重试";
            return APP_REDRAW_PAGE;
        }
        ctx->request_app = app_at(3);
        return APP_REDRAW_NONE;
    }
    int index = page_index();
    if (index > 0) advance(pages[index - 1]);
    return APP_REDRAW_PAGE;
}
static app_redraw_t tap(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    int x = ev->x0, y = ev->y0;
    if (ui_rect_hit(back_rect, x, y)) return setup_back(ctx);
    if (step == SETUP_LANGUAGE) {
        for (int i = 0; i < UI_LANG_COUNT; ++i) {
            if (!ui_rect_hit(language_row(i), x, y) || (int)preview == i) continue;
            preview = i;
            installed = false;
            notice = NULL;
            return APP_REDRAW_PAGE;
        }
    }
    if (step == SETUP_WIFI) {
        if (!wifi_ready && ui_rect_hit(action_rect(true), x, y)) {
            notice = NULL;
            returning_from_wifi = true;
            app_transfer_open_setup_wifi(ctx);
            return APP_REDRAW_NONE;
        }
        if (!ui_rect_hit(action_rect(wifi_ready), x, y)) return APP_REDRAW_NONE;
        advance(SETUP_FONT);
        return APP_REDRAW_PAGE;
    }
    if (step == SETUP_FONT && !installed && ui_rect_hit(action_rect(download_is_primary()), x, y)) {
        if (pending)
            stop(ctx);
        else
            start_download(ctx);
        return APP_REDRAW_PAGE;
    }
    if (step == SETUP_HOME) {
        for (int s = 0; s < 2; ++s) {
            for (int i = 0; i < 5; ++i) {
                if (!ui_rect_hit(widget_cell(i, s), x, y) || choices[i] == slots[s]) continue;
                app_home_widget_t before[] = { slots[0], slots[1] };
                if (choices[i] != APP_HOME_WIDGET_EMPTY && choices[i] == slots[1 - s])
                    slots[1 - s] = slots[s];
                slots[s] = choices[i];
                dirty = mark_rect(i, s);
                for (int col = 0; col < 2; ++col) {
                    for (int row = 0; row < 5; ++row) {
                        if ((before[col] == choices[row]) == (slots[col] == choices[row])) continue;
                        draw_choice(ctx->fb, row, col);
                        dirty = ui_rect_union(dirty, mark_rect(row, col));
                    }
                }
                return APP_REDRAW_AREA;
            }
        }
    }
    bool skip = step == SETUP_FONT && !installed;
    if ((skip && !full_modes[mode]) ||
        !ui_rect_hit(action_rect(skip ? !download_is_primary() : true), x, y))
        return APP_REDRAW_NONE;
    if (step == SETUP_LANGUAGE) {
        bool language_only = mode == APP_SETUP_LANGUAGE_ONLY;
        if (advance(language_only ? SETUP_LANGUAGE : SETUP_WIFI) && language_only)
            ctx->request_app = app_at(3);
    } else if (step == SETUP_FONT) {
        stop(ctx);
        if (mode == APP_SETUP_FONT_ONLY)
            ctx->request_app = app_at(3);
        else
            advance(SETUP_HOME);
    } else if (!app_home_components_save_pair(slots[0], slots[1]))
        notice = "保存失败，请重试";
    else if (advance(SETUP_DONE))
        ctx->request_app = app_home_page();
    return ctx->request_app ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
}
static app_redraw_t tick(app_ctx_t* ctx) {
    if (step == SETUP_WIFI && ctx->now_ms - wifi_polled_ms >= 1000) {
        wifi_polled_ms = ctx->now_ms;
        return read_wifi() ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
    }
    if (!pending) return APP_REDRAW_NONE;
    font_job_state_t s = font_download_state();
    if (s == FONT_BUSY) {
        unsigned p = font_download_percent() / 5 * 5;
        if (p > 100) p = 100;
        if (p <= progress || ctx->now_ms - last_meter_ms < 500) return APP_REDRAW_NONE;
        last_meter_ms = ctx->now_ms;
        progress = p;
        draw_meter(ctx->fb);
        dirty = meter;
        return APP_REDRAW_AREA;
    }
    pending = false;
    if (s == FONT_NEEDS_WIFI) {
        returning_from_wifi = true;
        app_transfer_open_setup_wifi(ctx);
        return APP_REDRAW_NONE;
    }
    if (s == FONT_READY) {
        const char* path = font_download_path();
        bool ok = ttf_font_open(path) == ESP_OK && !strcmp(ttf_font_path(), path);
        if (ok) app_settings_set_system_font_path(path);
        ok = ok && !strcmp(app_settings_system_font_path(), path);
        app_font_activate_system();
        installed = ok;
        notice = ok ? "字体已安装" : "字体无法加载";
    } else
        notice = s == FONT_CONFLICT    ? "同名文件已保留"
                 : s == FONT_NO_CARD   ? "请插入 TF 卡"
                 : s == FONT_NO_SPACE  ? "TF 卡空间不足"
                 : s == FONT_CANCELLED ? "下载已取消"
                                       : "下载失败，请重试";
    return APP_REDRAW_PAGE;
}
static void media_lost(app_ctx_t* ctx) {
    installed = false;
    stop(ctx);
    if (step == SETUP_FONT) notice = "请插入 TF 卡";
}
const app_desc_t app_setup = { .title = "开机引导",
                               .owns_keys = true,
                               .enter_full = true,
                               .menu_handle_enabled = no_menu,
                               .modal_input_owned = modal,
                               .on_enter = enter,
                               .on_exit = stop,
                               .on_before_lock = stop,
                               .on_media_lost = media_lost,
                               .area_hint = area_hint,
                               .render = render,
                               .edge_back = setup_back, .on_gesture = tap,
                               .on_tick = tick };
