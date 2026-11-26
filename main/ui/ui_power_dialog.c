/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：长按电源的静态点按菜单与最终画面；控件纯绘制，独立准备入口可分配并读取TF关机图。
 * English: Static tap-to-act power menu and final art; controls paint only, while the separate prepare entry may allocate and read SD-cached shutdown art.
 * 用户修订：用静态黑白按钮取代滑轨，不生成按压或拖动反馈帧。
 * User revision: replace the slider with static black-and-white buttons, with no press or drag feedback frames.
 */
#include "ui_power_dialog.h"
#include "shutdown_wallpaper.h"
#include "settings.h"
#include "memory_manager.h"
#include "book_ticket.h"
#include "book_lock_collage.h"
#include "book_lock_cover.h"
#include "lock_calendar.h"
#include <stdlib.h>
#include "ui_kit.h"

enum {
    POWER_MARGIN = 64,
    POWER_WIDTH = UI_LOCK_WIDTH - 2 * POWER_MARGIN,
    POWER_GAP = 24,
    POWER_PRIMARY_Y = 542,
    POWER_PRIMARY_H = 116,
    POWER_SECONDARY_Y = POWER_PRIMARY_Y + POWER_PRIMARY_H + POWER_GAP,
    POWER_SECONDARY_W = (POWER_WIDTH - POWER_GAP) / 2,
    POWER_SECONDARY_H = 100,
};

EpdRect ui_power_dialog_button_rect(ui_power_action_t action) {
    switch (action) {
        case UI_POWER_ACTION_SHUTDOWN:
            return (EpdRect){POWER_MARGIN, POWER_PRIMARY_Y, POWER_WIDTH, POWER_PRIMARY_H};
        case UI_POWER_ACTION_RESTART:
            return (EpdRect){POWER_MARGIN, POWER_SECONDARY_Y, POWER_SECONDARY_W, POWER_SECONDARY_H};
        case UI_POWER_ACTION_CANCEL:
            return (EpdRect){POWER_MARGIN + POWER_SECONDARY_W + POWER_GAP, POWER_SECONDARY_Y,
                POWER_SECONDARY_W, POWER_SECONDARY_H};
        default:
            return (EpdRect){0};
    }
}

static bool hit_button(ui_power_action_t action, int x, int y) {
    const EpdRect rect = ui_power_dialog_button_rect(action);
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

void ui_power_dialog_reset(ui_power_dialog_t *dialog, bool touching) {
    if (dialog) *dialog = (ui_power_dialog_t){.swallow = touching};
}

ui_power_action_t ui_power_dialog_feed(ui_power_dialog_t *dialog, const app_ctx_t *ctx) {
    if (!dialog) return UI_POWER_ACTION_NONE;
    if (!ctx || ctx->consumed || !ctx->touch) {
        ui_power_dialog_reset(dialog, true);
        return UI_POWER_ACTION_NONE;
    }
    const app_touch_t *touch = ctx->touch;
    const int x = touch->x, y = touch->y;
    if (touch->touched && (touch->count != 1 || x >= UI_LOCK_WIDTH || y >= UI_LOCK_HEIGHT)) {
        ui_power_dialog_reset(dialog, true);
        return UI_POWER_ACTION_NONE;
    }
    if (dialog->swallow) {
        if (!touch->touched) dialog->swallow = false;
        return UI_POWER_ACTION_NONE;
    }
    if (dialog->target == UI_POWER_ACTION_NONE && ctx->pressed && touch->touched) {
        for (ui_power_action_t action = UI_POWER_ACTION_CANCEL; action <= UI_POWER_ACTION_RESTART; ++action) {
            if (hit_button(action, x, y)) {
                dialog->target = action;
                dialog->x0 = x;
                dialog->y0 = y;
                dialog->started_ms = ctx->now_ms;
                break;
            }
        }
    }
    if (dialog->target == UI_POWER_ACTION_NONE) return UI_POWER_ACTION_NONE;
    if (ctx->now_ms < dialog->started_ms || !hit_button(dialog->target, x, y) ||
        abs(x - dialog->x0) > UI_TOUCH_SLOP_PX || abs(y - dialog->y0) > UI_TOUCH_SLOP_PX) {
        ui_power_dialog_reset(dialog, touch->touched);
    } else if (!touch->touched) {
        const ui_power_action_t action = ctx->released ? dialog->target : UI_POWER_ACTION_NONE;
        ui_power_dialog_reset(dialog, false);
        return action;
    }
    return UI_POWER_ACTION_NONE;
}

EpdRect ui_power_dialog_rect(void) { return (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}; }

static void draw_button(uint8_t *fb, ui_power_action_t action, const char *label, int px) {
    const EpdRect rect = ui_power_dialog_button_rect(action);
    const bool primary = action == UI_POWER_ACTION_SHUTDOWN;
    if (primary) ui_fill_round_rect(fb, rect, UI_BTN_RADIUS, UI_GRAY_BLACK);
    else ui_draw_control_frame(fb, rect, UI_BTN_RADIUS, UI_GRAY_BLACK);
    ui_text_box(fb, ui_inset_rect(rect, UI_PAD), px, label, EPD_DRAW_ALIGN_CENTER, primary);
}

void ui_power_dialog_draw(uint8_t *fb) {
    if (!fb) return;
    ui_clear_page(fb);
    ui_draw_icon(fb, UI_LOCK_WIDTH / 2, 356, 64, UI_ICON_POWER, UI_GRAY_BLACK);
    ui_text_box(fb, (EpdRect){POWER_MARGIN, 409, POWER_WIDTH, 80}, 48,
        "电源选项", EPD_DRAW_ALIGN_CENTER, false);
    draw_button(fb, UI_POWER_ACTION_SHUTDOWN, "关机", 42);
    draw_button(fb, UI_POWER_ACTION_RESTART, "重启", UI_PX_BTN);
    draw_button(fb, UI_POWER_ACTION_CANCEL, "取消", UI_PX_BTN);
}

void ui_power_final_draw(uint8_t *fb, bool restarting) {
    ui_clear_page(fb);
    const int cx = UI_LOCK_WIDTH / 2;
    ui_draw_icon(fb, cx, 486, 72, restarting ? UI_ICON_ROTATE_CW : UI_ICON_POWER, UI_GRAY_BLACK);
    ui_text(fb, cx, 548, 48, restarting ? "正在重启" : "mogo 已关机", EPD_DRAW_ALIGN_CENTER, false);
    if (!restarting) ui_text(fb, cx, 624, 26, "长按电源键开机", EPD_DRAW_ALIGN_CENTER, false);
}

static void draw_shutdown_hint(uint8_t *fb) {
    // 底部居中的小字签低于书籍数量的白描边。
    // The compact bottom-center label sits below the book count outline.
    const char *hint = "已关机 · 长按电源键开机";
    const int px = 36, max_width = UI_LOCK_WIDTH - 32;
    int width = ui_text_fixed_width_px(px, hint);
    if (width > max_width) {
        hint = "已关机";
        width = ui_text_fixed_width_px(px, hint);
    }
    const int tag_width = width + 16;
    EpdRect tag = {(UI_LOCK_WIDTH - tag_width) / 2, UI_LOCK_HEIGHT - 36, tag_width, 36};
    epd_fill_rect(tag, 0xff, fb);
    ui_text_fixed_vc(fb, tag.x+8, tag.y+tag.height/2, px, hint, EPD_DRAW_ALIGN_LEFT, false);
}

void ui_power_shutdown_prepare(uint8_t *fb) {
    const app_shutdown_wallpaper_t *value = app_settings_shutdown_wallpaper();
    if (value->style == APP_OFF_SYSTEM) { ui_power_final_draw(fb, false); return; }
    ui_clear_page(fb);
    bool ok = false;
    if (value->style == APP_OFF_TICKET) ok = book_ticket_preview(fb, value->ticket_background == 1);
    else if (value->style == APP_OFF_COLLAGE) ok = book_lock_collage_prepare(fb);
    else if (value->style == APP_OFF_BOOK_COVER) ok = book_lock_cover_draw(fb, value->cover_crop);
    else if (value->style == APP_OFF_CALENDAR) ok = lock_calendar_draw_variant(fb, value->calendar_style == 1);
    else if (value->style == APP_OFF_CUSTOM) {
        uint8_t *pixels = mm_alloc(MM_OWNER_FILE_JOB, MM_POOL_PSRAM, SHUTDOWN_ART_BYTES, 0, 0);
        ok = pixels && shutdown_wallpaper_read(value, pixels);
        if (ok) for (unsigned y = 0; y < SHUTDOWN_ART_HEIGHT; ++y) for (unsigned x = 0; x < SHUTDOWN_ART_WIDTH; ++x) {
            unsigned at = y * SHUTDOWN_ART_WIDTH + x;
            epd_draw_pixel((int)x, (int)y, ((pixels[at / 2] >> ((at & 1u) * 4u)) & 15u) * 17u, fb);
        }
        mm_free(pixels);
    }
    if (!ok) { ui_power_final_draw(fb, false); return; }
    if (value->show_hint) draw_shutdown_hint(fb);
}
