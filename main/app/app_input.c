/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从主循环提取同步输入管线，保持去抖、诊断、回调及取消边界。
 * English: Synchronous input pipeline extracted from the loop, retaining debounce, diagnostics, callbacks and cancellation boundaries.
 */
#include "app_input.h"
#include "app_edge_feedback.h"
#include "touch_diag.h"
#include "display_orientation.h"
#include "ui_menu.h"
#include "app_navigation.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include "app_registry.h"
#include <stdlib.h>
#include <string.h>

#if CONFIG_TOUCH_DIAG
static unsigned td_press, td_tap, td_swipe, td_longp, td_cancel, td_multi;
static bool td_multi_active;
static const char* td_gest_name(ui_gesture_type_t t) {
    switch (t) {
        case UI_GESTURE_PRESS: return "PRESS";
        case UI_GESTURE_MOVE: return "MOVE";
        case UI_GESTURE_LONG_PRESS: return "LONG";
        case UI_GESTURE_TAP: return "TAP";
        case UI_GESTURE_SWIPE_L: return "SWIPE_L";
        case UI_GESTURE_SWIPE_R: return "SWIPE_R";
        case UI_GESTURE_SWIPE_U: return "SWIPE_U";
        case UI_GESTURE_SWIPE_D: return "SWIPE_D";
        case UI_GESTURE_CANCEL: return "CANCEL";
        case UI_GESTURE_RELEASE: return "RELEASE";
        default: return "?";
    }
}
app_input_diag_counts_t app_input_diag_counts(void) {
    return (app_input_diag_counts_t){ .press = td_press, .tap = td_tap,
        .swipe = td_swipe, .longp = td_longp, .cancel = td_cancel, .multi = td_multi };
}
#endif

// 空槽常带着抬起事件或残留坐标，不能进页面看到的快照；抬手后保留最后一次位置。
// Empty slots often carry lift events or stale coords; keep last position on lift.
static void debounce_touch(
    app_touch_t* latest, const app_touch_t* raw, bool released
) {
    if (raw->touched) {
        *latest = *raw;
        for (int i = 0; i < APP_TOUCH_MAX_POINTS; i++) {
            if (!latest->points[i].active) {
                memset(&latest->points[i], 0, sizeof(latest->points[i]));
            }
        }
    } else if (released) {
        latest->touched = false;
        latest->count = 0;
        for (int i = 0; i < APP_TOUCH_MAX_POINTS; i++) {
            latest->points[i].active = false;
        }
    }
    memcpy(latest->raw, raw->raw, sizeof(raw->raw));
}

static void app_input_rotate_touch(app_touch_t *touch) {
    display_orientation_touch(&touch->x, &touch->y);
    for (unsigned i = 0; i < APP_TOUCH_MAX_POINTS; ++i)
        if (touch->points[i].active)
            display_orientation_touch(&touch->points[i].x, &touch->points[i].y);
}

bool app_input_filter_screen_touch(app_input_touch_filter_t *filter, app_touch_t *touch,
                                  esp_err_t err, bool disabled) {
    if (err != ESP_OK) {
        filter->blocked = filter->contact;
        if (filter->blocked) memset(touch, 0, sizeof(*touch));
        return filter->blocked;
    }
    if (!touch->touched) {
        *filter = (app_input_touch_filter_t){.disabled = disabled};
        return false;
    }
    const bool panel_key = touch->count == 1 && ui_key_hit_test(touch->x, touch->y) >= 0;
    if ((filter->contact && filter->disabled != disabled) || (disabled && !panel_key))
        filter->blocked = true;
    filter->contact = true;
    filter->disabled = disabled;
    if (!filter->blocked) {
        app_input_rotate_touch(touch);
        return false;
    }
    memset(touch, 0, sizeof(*touch));
    return true;
}

void app_input_drop_contact(app_touch_t *latest, bool *was_touched) {
    *latest = (app_touch_t){0};
    *was_touched = false;
}

void app_input_frame_to_touch(const touch_frame_t* f, app_touch_t* t) {
    memset(t, 0, sizeof(*t));
    t->touched = f->touched != 0;
    t->count = f->count;
    t->x = f->x;
    t->y = f->y;
}

void app_input_ingest_touch_frame(app_ctx_t* ctx, app_touch_t* latest,
                               const app_touch_t* touch, esp_err_t err,
                               bool* was_touched, int64_t frame_ms,
                               int64_t* last_input_ms,
                               bool* out_pressed, bool* out_released) {
    const bool pressed = touch->touched && !*was_touched;
    const bool released = !touch->touched && *was_touched;
#if CONFIG_TOUCH_DIAG
    if (pressed) {
        td_press++;
        TD_LOGI("PRESS x=%u y=%u n=%u", touch->x, touch->y, touch->count);
    }
    if (err == ESP_OK && touch->touched && touch->count > 1) {
        if (!td_multi_active) {
            td_multi_active = true;
            td_multi++;
            TD_LOGI("MULTI n=%u x=%u y=%u", touch->count, touch->x, touch->y);
        }
    } else if (td_multi_active) td_multi_active = false;
#endif
    debounce_touch(latest, touch, released);
    ctx->now_ms = frame_ms;
    if (err == ESP_OK && (touch->touched || released)) *last_input_ms = frame_ms;
    ctx->pressed = pressed;
    ctx->released = released;
    ctx->consumed = false;
    *out_pressed = pressed;
    *out_released = released;
}

/* ---- 全面屏路由 / Full-screen routing ---- */
enum { ROUTE_PAGE, ROUTE_EDGE_LEFT, ROUTE_EDGE_RIGHT, ROUTE_TABS, ROUTE_ROOT, ROUTE_BLOCK };

static unsigned horizontal_owner(app_ctx_t *ctx, const app_desc_t *app, int x, int y) {
    if (!app_settings_gestures() || (!app->edge_back && !app->main_page_visible && !app->horizontal_zone)) return ROUTE_PAGE;
    int width = epd_rotated_display_width(), height = epd_rotated_display_height();
    if (x >= width || y >= height) return ROUTE_PAGE;
    bool root = app_navigation_main_page_ready(ctx, app);
    if (root && ui_nav_hit(x, y) >= 0) return ROUTE_PAGE;
    int edge = app_settings_gesture_edge_px();
    if (x < edge) return ROUTE_EDGE_LEFT;
    if (x >= width - edge) return ROUTE_EDGE_RIGHT;
    app_horizontal_zone_t zone = app->horizontal_zone ? app->horizontal_zone(ctx, x, y) : APP_HORIZONTAL_DEFAULT;
    if (zone == APP_HORIZONTAL_LOCAL) return ROUTE_PAGE;
    if (zone == APP_HORIZONTAL_BLOCK) return ROUTE_BLOCK;
    if (zone == APP_HORIZONTAL_TABS) return ROUTE_TABS;
    return root ? ROUTE_ROOT : ROUTE_PAGE;
}

// 延迟的按下只交给点按、长按或纵向控件。按原点重放，不使用滑动终点命中。
// Replay a deferred press only for taps, holds or vertical controls, always at its origin.
static void replay_press(app_ctx_t *ctx, const app_desc_t *app, const ui_gesture_event_t *ev) {
    ui_gesture_event_t press = *ev;
    press.type = UI_GESTURE_PRESS;
    press.x = press.x0; press.y = press.y0; press.hold_ms = 0;
    const app_touch_t *touch = ctx->touch;
    const app_touch_t origin = {.touched = true, .count = 1, .x = ev->x0, .y = ev->y0,
        .points = {{.active = true, .x = ev->x0, .y = ev->y0}}};
    bool pressed = ctx->pressed, released = ctx->released;
    int64_t now = ctx->now_ms;
    ctx->touch = &origin; ctx->pressed = true; ctx->released = false;
    ctx->now_ms -= ev->hold_ms;
    app_redraw_t redraw = app->on_gesture(ctx, &press);
    ctx->touch = touch; ctx->pressed = pressed; ctx->released = released; ctx->now_ms = now;
    if (redraw != APP_REDRAW_NONE) app_present(ctx, app, redraw);
}

// 取消只清理按压反馈，不能提交抬手动作或覆盖已有导航请求。
// Cancellation clears pressed feedback without committing a release or replacing pending navigation.
static app_redraw_t cancel_page_press(app_ctx_t *ctx, const app_desc_t *app, ui_gesture_event_t event) {
    const app_desc_t *requested = ctx->request_app;
    bool menu = ctx->request_menu, back = ctx->request_return, released = ctx->released;
    event.type = UI_GESTURE_CANCEL;
    ctx->released = false;
    app_redraw_t redraw = app->on_gesture(ctx, &event);
    ctx->released = released;
    ctx->request_app = requested; ctx->request_menu = menu; ctx->request_return = back;
    return redraw;
}

void app_input_dispatch_gesture_frame(app_ctx_t* ctx, const app_desc_t* current,
                                   ui_gesture_t* gesture, esp_err_t err,
                                   bool owned_by_other, app_input_present_t present) {
    if (owned_by_other || !current->on_gesture || err != ESP_OK) return;
    // feed 在终态清空识别器，须先保留起手归属。/ Feed resets terminal state; retain origin ownership first.
    unsigned owner = gesture->horizontal_owner;
    bool deferred = gesture->deferred_press, vertical = gesture->vertical_claim;
    if (gesture->active && gesture->orientation != (uint8_t)display_orientation_get()) {
        app_input_cancel_gesture(ctx, current, gesture);
        ctx->consumed = true;
        return;
    }
    ui_gesture_event_t event;
    const bool new_press = !gesture->active && ctx->pressed && !ctx->consumed &&
        !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
    const bool capture = new_press && current->gesture_capture_drag &&
        current->gesture_capture_drag(ctx, ctx->touch->x, ctx->touch->y);
    const bool captured = capture || gesture->captured_drag;
    if (ui_gesture_feed_drag(gesture, ctx, &event,
            new_press && !capture && current->gesture_long_drag && current->gesture_long_drag(ctx), capture)) {
#if CONFIG_TOUCH_DIAG
        // MOVE 逐帧太多不记；按下与终态全记。MOVE frames are too noisy; log press + terminal.
        if (event.type != UI_GESTURE_MOVE) {
            switch (event.type) {
                case UI_GESTURE_TAP: td_tap++; break;
                case UI_GESTURE_LONG_PRESS: td_longp++; break;
                case UI_GESTURE_CANCEL:
                    td_cancel++;
                    touch_diag_beep();
                    break;
                case UI_GESTURE_SWIPE_L: case UI_GESTURE_SWIPE_R:
                case UI_GESTURE_SWIPE_U: case UI_GESTURE_SWIPE_D:
                    td_swipe++;
                    break;
                default: break;
            }
            TD_LOGI("GEND t=%s x0=%u y0=%u x=%u y=%u ms=%d",
                    td_gest_name(event.type), event.x0, event.y0, event.x, event.y,
                    (int)event.hold_ms);
        }
#endif
        if (event.type == UI_GESTURE_PRESS) {
            owner = gesture->captured_drag ? ROUTE_PAGE : horizontal_owner(ctx, current, event.x0, event.y0);
            gesture->orientation = (uint8_t)display_orientation_get();
            gesture->horizontal_owner = owner;
            gesture->deferred_press = owner == ROUTE_EDGE_LEFT || owner == ROUTE_EDGE_RIGHT;
            if (gesture->deferred_press) {
                if (current->on_deferred_press) current->on_deferred_press();
                return;
            }
        }
        if (event.type == UI_GESTURE_LONG_PRESS) gesture->horizontal_owner = ROUTE_PAGE;
        bool horizontal = event.type == UI_GESTURE_SWIPE_L || event.type == UI_GESTURE_SWIPE_R;
        if (owner != ROUTE_PAGE && event.type == UI_GESTURE_MOVE && !vertical) {
            int dy = abs((int)event.y - event.y0), dx = abs((int)event.x - event.x0);
            if (dy <= dx || dy <= UI_TOUCH_SLOP_PX) return;
            gesture->vertical_claim = true;
        }
        if (horizontal && owner != ROUTE_PAGE) {
            // 清除局部按压态；已处理的纵拖不能在抬手时变为横滑。
            // Clear local decoration; a committed vertical drag cannot become navigation on release.
            app_redraw_t cleanup = deferred ? APP_REDRAW_NONE : cancel_page_press(ctx, current, event);
            int delta = event.type == UI_GESTURE_SWIPE_L ? 1 : -1;
            app_redraw_t action = APP_REDRAW_NONE;
            if (!vertical) {
                if ((owner == ROUTE_EDGE_LEFT && delta < 0) || (owner == ROUTE_EDGE_RIGHT && delta > 0)) {
                    if (current->edge_back) {
                        bool pending = ctx->request_app || ctx->request_return || ctx->request_menu;
                        display_orientation_t origin = display_orientation_get();
                        app_back_result_t back = current->edge_back(ctx);
                        action = back.redraw;
                        // 只有实际退层才提示；回调已换向时不能沿用旧触点。/ Acknowledge only accepted backs in the original orientation.
                        if (back.accepted && !pending && origin == display_orientation_get())
                            app_edge_feedback_show(ctx, owner == ROUTE_EDGE_RIGHT, event.y0);
                    }
                } else if (owner == ROUTE_TABS && current->step_tab) action = current->step_tab(ctx, delta);
                else if (owner == ROUTE_ROOT && app_navigation_main_page_ready(ctx, current))
                    ui_nav_request(ctx, (app_index_of(current) + delta + 4) % 4);
            }
            ctx->consumed = true;
            if (action != APP_REDRAW_NONE) present(ctx, current, gesture, action);
            else if (cleanup != APP_REDRAW_NONE && !ctx->request_app && !ctx->request_return && !ctx->request_menu)
                present(ctx, current, gesture, cleanup);
            return;
        }
        if (deferred) {
            if (event.type == UI_GESTURE_CANCEL) return;
            gesture->deferred_press = false;
            replay_press(ctx, current, &event);
            if (ctx->request_app || ctx->request_menu || ctx->request_return) { ctx->consumed = true; return; }
        }
        app_redraw_t redraw = current->on_gesture(ctx, &event);
        present(ctx, current, gesture, redraw);
        if (redraw != APP_REDRAW_NONE || captured) ctx->consumed = true;
    }
}

void app_input_cancel_gesture(app_ctx_t* ctx, const app_desc_t* app, ui_gesture_t* gesture) {
    if (gesture->active && !gesture->deferred_press && app->on_gesture) {
        ui_gesture_event_t event = { .type = UI_GESTURE_CANCEL,
            .x0 = gesture->x0, .y0 = gesture->y0, .x = gesture->x, .y = gesture->y };
        app_redraw_t redraw = cancel_page_press(ctx, app, event);
        ui_gesture_reset(gesture);
        app_present(ctx, app, redraw);
    }
    ui_gesture_reset(gesture);
}
