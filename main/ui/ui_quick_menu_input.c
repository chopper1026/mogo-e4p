/* SPDX-License-Identifier: Apache-2.0
 * 中文：顶部边缘识别器只产出快捷动作与前光事件，不访问页面、硬件或刷新。
 * English: The top-edge recognizer emits quick actions and frontlight events
 * without accessing pages, hardware or presentation.
 */
#include "ui_quick_menu.h"
#include <stdlib.h>
#include <string.h>
#include "ui_kit.h"
#include "ui_menu.h"

// 圆键之后的卡片命中编号；ui_quick_menu_pressed 只把 0..3 当作按住的圆键。
// Card hit ids follow the circles; ui_quick_menu_pressed only treats 0..3 as a held key.
enum { HIT_BRIGHTNESS = 4, HIT_CCT = 5 };

static int center_x(int button) { return (button * 2 + 1) * UI_LOCK_WIDTH / 8; }

static int circle_hit(int x, int y) {
    for (int i = 0; i < 4; ++i) {
        const int dx = x - center_x(i), dy = y - UI_QUICK_CIRCLE_Y;
        if (dx * dx + dy * dy <= UI_QUICK_HIT_RADIUS * UI_QUICK_HIT_RADIUS) return i;
    }
    return -1;
}

static int hit_test(const ui_quick_menu_t *menu, int x, int y) {
    int key = circle_hit(x, y);
    if (key >= 0) return key;
    if (!menu->frontlight) return -1;
    if (x >= 40 && x < UI_LOCK_WIDTH - 40 &&
        y >= UI_QUICK_FL_BRI_HIT_Y0 && y <= UI_QUICK_FL_BRI_HIT_Y1) return HIT_BRIGHTNESS;
    if (x >= 40 && x < UI_LOCK_WIDTH - 40 &&
        y >= UI_QUICK_FL_CCT_HIT_Y0 && y <= UI_QUICK_FL_CCT_HIT_Y1) return HIT_CCT;
    return -1;
}

static uint8_t slider_value(int hit, int x) {
    if (x < UI_QUICK_FL_TRACK_X) x = UI_QUICK_FL_TRACK_X;
    const int right = UI_QUICK_FL_TRACK_X + UI_QUICK_FL_TRACK_W;
    if (x > right) x = right;
    int value = (x - UI_QUICK_FL_TRACK_X) * 100 / UI_QUICK_FL_TRACK_W;
    if (hit == HIT_BRIGHTNESS && value < 1) value = 1;
    return (uint8_t)value;
}

void ui_quick_menu_reset(ui_quick_menu_t *menu) {
    const bool frontlight = menu->frontlight;
    const bool on = menu->fl_on;
    const uint8_t bri = menu->fl_bri ? menu->fl_bri : 40;
    const uint8_t cct = menu->fl_cct;
    memset(menu, 0, sizeof(*menu));
    menu->frontlight = frontlight;
    menu->fl_on = on;
    menu->fl_bri = bri;
    menu->fl_cct = cct;
    menu->hit = -1;
    menu->drag = UI_QUICK_CTL_NONE;
}

void ui_quick_menu_set_frontlight(ui_quick_menu_t *menu, bool available, bool on,
                                  uint8_t brightness, uint8_t cct) {
    menu->frontlight = available;
    menu->fl_on = on;
    menu->fl_bri = brightness < 1 ? 1 : brightness > 100 ? 100 : brightness;
    menu->fl_cct = cct > 100 ? 100 : cct;
}

void ui_quick_menu_cancel_input(ui_quick_menu_t *menu) {
    menu->tracking = false;
    menu->swallow = true;
    menu->drag = UI_QUICK_CTL_NONE;
}

int ui_quick_menu_pressed(const ui_quick_menu_t *menu) {
    return menu->open && menu->tracking && !menu->moved && menu->hit >= 0 && menu->hit < 4
        ? menu->hit : -1;
}

static void dismiss(ui_quick_menu_t *menu, ui_quick_event_t *event) {
    menu->open = menu->tracking = false;
    menu->swallow = true;
    menu->drag = UI_QUICK_CTL_NONE;
    event->action = UI_QUICK_CLOSE;
}

bool ui_quick_menu_feed(ui_quick_menu_t *menu, const app_ctx_t *ctx, bool allowed,
                        ui_quick_event_t *event) {
    *event = (ui_quick_event_t){0};
    if (!allowed) {
        bool owned = menu->tracking || menu->swallow || menu->open;
        if (menu->open) dismiss(menu, event);
        else ui_quick_menu_reset(menu);
        return owned;
    }
    if (!ctx->touch || (ctx->touch->touched && ctx->touch->count != 1)) {
        bool owned = menu->open || menu->tracking || menu->swallow;
        if (owned) ui_quick_menu_cancel_input(menu);
        return owned;
    }
    if (menu->swallow) {
        if (!ctx->touch->touched) menu->swallow = false;
        return true;
    }
    int x = ctx->touch->x, y = ctx->touch->y;
    if (ctx->pressed && ctx->touch->touched) {
        if (!menu->open && y > UI_QUICK_EDGE_PX) return false;
        if (menu->open && ui_key_hit_test(x, y) >= 0) {
            dismiss(menu, event);
            return true;
        }
        menu->tracking = true;
        menu->moved = false;
        menu->x0 = x; menu->y0 = y;
        menu->started_ms = ctx->now_ms;
        menu->hit = menu->open ? hit_test(menu, x, y) : -1;
        menu->drag = UI_QUICK_CTL_NONE;
    }
    if (!menu->tracking) return menu->open;
    int dx = x - (int)menu->x0, dy = y - (int)menu->y0;
    if (ctx->now_ms < menu->started_ms) {
        ui_quick_menu_cancel_input(menu);
        return true;
    }
    if (abs(dx) > UI_TOUCH_SLOP_PX || abs(dy) > UI_TOUCH_SLOP_PX) menu->moved = true;
    // 滑杆按下即定位：不等拖动阈值，手指落在哪一档就应用到哪一档。
    // A slider positions itself on touch: apply the tapped step before the drag threshold.
    if (menu->open && (menu->hit == HIT_BRIGHTNESS || menu->hit == HIT_CCT)) {
        const uint8_t value = slider_value(menu->hit, x);
        if (menu->hit == HIT_BRIGHTNESS) menu->fl_bri = value;
        else menu->fl_cct = value;
        // 滑杆只记录数值，不替开关开灯；灯灭时拖动只更新下次点亮要用的档位。
        // A slider only records the value and never acts as the switch; with the light off it queues the level for the next on.
        menu->drag = menu->hit == HIT_BRIGHTNESS
            ? UI_QUICK_CTL_BRIGHTNESS : UI_QUICK_CTL_CCT;
        event->control = menu->drag;
        event->value = value;
    }
    if (!menu->open && dy >= UI_QUICK_SWIPE_PX && dy >= abs(dx) * 2 &&
        ctx->now_ms - menu->started_ms <= 1500) {
        menu->open = true;
        menu->tracking = false;
        menu->swallow = true;
        event->action = UI_QUICK_OPEN;
    } else if (menu->open && menu->drag == UI_QUICK_CTL_NONE &&
               dy <= -UI_QUICK_SWIPE_PX && -dy >= abs(dx) * 2) {
        dismiss(menu, event);
    } else if (ctx->released) {
        menu->tracking = false;
        if (menu->open && !menu->moved) {
            if (menu->hit == 3 && menu->frontlight) {
                // 第四枚圆钮就是前光开关：点按翻转，并作为一次立即提交。
                // The fourth circle is the frontlight switch: a tap flips it and commits immediately.
                menu->fl_on = !menu->fl_on;
                event->control = UI_QUICK_CTL_ON;
                event->value = menu->fl_on ? 1 : 0;
                event->commit = true;
            } else if (menu->hit >= 0 && menu->hit < 4 && circle_hit(x, y) == menu->hit) {
                static const ui_quick_action_t actions[] = {
                    UI_QUICK_WIFI, UI_QUICK_BLUETOOTH, UI_QUICK_FULL, UI_QUICK_LOCK,
                };
                event->action = actions[menu->hit];
            } else if (menu->drag != UI_QUICK_CTL_NONE) {
                event->control = menu->drag;
                event->value = menu->drag == UI_QUICK_CTL_BRIGHTNESS
                    ? menu->fl_bri : menu->fl_cct;
                event->commit = true;
            } else if (menu->y0 >= (menu->frontlight ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT) &&
                       y >= (menu->frontlight ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT)) {
                dismiss(menu, event);
                menu->swallow = false;
            }
        } else if (menu->open && menu->drag != UI_QUICK_CTL_NONE) {
            // 拖动后松手：提交最后一档，避免只改硬件不落盘。
            // Release after a drag: commit the last step so hardware and NVS never diverge.
            event->control = menu->drag;
            event->value = menu->drag == UI_QUICK_CTL_BRIGHTNESS
                ? menu->fl_bri : menu->fl_cct;
            event->commit = true;
        }
        menu->drag = UI_QUICK_CTL_NONE;
    }
    return true;
}
