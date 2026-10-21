/* SPDX-License-Identifier: Apache-2.0
 * 中文：全宽浅顶部层，下圆角与四个无文字圆钮；有前光的板在圆钮下方常驻一块
 * 实心前光卡片，开关、亮度、色温三件套下拉即见。
 * English: A shallow full-width top layer with rounded bottom corners and four
 * unlabeled circles; boards with a frontlight keep a solid card below them, so
 * its switch, brightness and colour temperature are visible as soon as it opens.
 * 用户修订：恢复实心灰阶控件，不再生成液态玻璃或缓存底图；前光三件套常驻，
 * 不再需要第二次点按展开。
 * User revision: restore solid grayscale controls without liquid glass or cached
 * backdrops; the three frontlight controls stay visible without a second tap.
 * 冻结：不访问硬件、设置或刷新，不申请帧缓存；只在展开/状态变化后由主循环调用绘图。
 * Frozen: No hardware, settings, presentation or frame allocations; the loop draws only on opening or state changes.
 */
#include "ui_quick_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include "ui_kit.h"
#include "ui_nav.h"

static int center_x(int button) { return (button * 2 + 1) * UI_LOCK_WIDTH / 8; }
static EpdRect key_rect(int button) {
    return (EpdRect){center_x(button)-UI_QUICK_RADIUS,UI_QUICK_CIRCLE_Y-UI_QUICK_RADIUS,
                     2*UI_QUICK_RADIUS,2*UI_QUICK_RADIUS};
}
static const ui_icon_t s_icons[]={UI_ICON_WIFI,UI_ICON_BLUETOOTH,UI_ICON_REFRESH_CW,UI_ICON_LOCK};

// 根据当前旋转读取逻辑像素；和图形接口相同，画点保持半字节邻居。
// Read logical pixels using the current rotation; drawing preserves the neighboring nibble like the graphics API.
static uint8_t pixel(uint8_t *fb, int x, int y) {
    int px, py;
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT: px = epd_width() - y - 1; py = x; break;
        case EPD_ROT_INVERTED_LANDSCAPE: px = epd_width() - x - 1; py = epd_height() - y - 1; break;
        case EPD_ROT_INVERTED_PORTRAIT: px = y; py = epd_height() - x - 1; break;
        default: px = x; py = y; break;
    }
    return (epd_get_pixel(px, py, epd_width(), epd_height(), fb) >> 4) * 17u;
}

static bool layer_pixel(int x, int y, int width, int height) {
    const int r = 56;
    if (y < height - r || (x >= r && x < width - r)) return true;
    int dx = x < r ? x - r : x - (width - r - 1);
    int dy = y - (height - r - 1);
    return dx * dx + dy * dy < r * r;
}

// 44px锁形沿用细线几何，圆角锁体与上半圆锁梁；不引入生成图标资产。
// A 44px lock follows the thin-line geometry, with a rounded body and arched shackle; no new generated icon asset is needed.
static void lock_icon(uint8_t *fb, int cx, int cy) {
    for (int stroke = 0; stroke < 3; ++stroke) {
        ui_draw_round_rect(fb, (EpdRect){cx - 14 + stroke, cy - 1 + stroke,
                                      28 - stroke * 2, 23 - stroke * 2}, 4 - stroke, UI_GRAY_BLACK);
        int radius = 9 - stroke;
        for (int x = -radius; x <= radius; ++x) {
            int y = 0;
            while ((y + 1) * (y + 1) + x * x <= radius * radius) ++y;
            epd_draw_pixel(cx + x, cy - 10 - y, UI_GRAY_BLACK, fb);
            if (abs(x) == radius) epd_draw_line(cx + x, cy - 10, cx + x, cy - 1, UI_GRAY_BLACK, fb);
        }
    }
    ui_fill_rect_fast((EpdRect){cx - 1, cy + 8, 3, 7}, UI_GRAY_BLACK, fb);
}

/* ---- 圆键 / Circle keys ---- */

static void draw_badge(uint8_t *fb,int button,bool active) {
    if(button>1||!active)return;
    int cx=center_x(button);
    epd_fill_circle(cx+30,UI_QUICK_CIRCLE_Y-30,5,UI_GRAY_WHITE,fb);
    epd_draw_circle(cx+30,UI_QUICK_CIRCLE_Y-30,5,0x50,fb);
}

// 实心圆键：活动态沿用原176灰，其余为244灰；按压只加内圈。
// Solid key: active uses the original 176 gray, others 244; a press only adds the inner ring.
static void draw_key(uint8_t *fb,int button,bool active,bool pressed) {
    int cx=center_x(button);
    epd_fill_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS,active?0xb0:0xf4,fb);
    epd_draw_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS,0x38,fb);
    epd_draw_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS-1,0x38,fb);
    if(button<3) ui_draw_icon(fb,cx,UI_QUICK_CIRCLE_Y,44,s_icons[button],UI_GRAY_BLACK);
    else lock_icon(fb,cx,UI_QUICK_CIRCLE_Y);
    if(pressed){
        epd_draw_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS-7,UI_GRAY_BLACK,fb);
        epd_draw_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS-8,UI_GRAY_BLACK,fb);
    }
}

/* ---- 前光卡片 / Frontlight card ---- */

static EpdRect fl_card_rect(void) {
    return (EpdRect){UI_QUICK_FL_CARD_X, UI_QUICK_FL_CARD_Y,
                     UI_QUICK_FL_CARD_W, UI_QUICK_FL_CARD_H};
}
static EpdRect fl_row_rect(ui_quick_control_t control) {
    // 行矩形内缩到卡片边框以内，局部重画不会擦掉圆角描边。
    // Row rectangles stay inside the card border so local repaints never erase its rounded outline.
    if (control == UI_QUICK_CTL_ON) return (EpdRect){40, 226, 604, 52};
    if (control == UI_QUICK_CTL_BRIGHTNESS) return (EpdRect){40, 278, 604, 64};
    if (control == UI_QUICK_CTL_CCT) return (EpdRect){40, 342, 604, 80};
    return (EpdRect){0};
}
static EpdRect fl_switch_rect(void) {
    return (EpdRect){UI_QUICK_FL_SWITCH_X, UI_QUICK_FL_SWITCH_Y,
                     UI_QUICK_FL_SWITCH_W, UI_QUICK_FL_SWITCH_H};
}
static int fl_thumb_x(uint8_t value) {
    return UI_QUICK_FL_TRACK_X + (UI_QUICK_FL_TRACK_W * value) / 100;
}

// 胶囊开关：关为浅灰轨、滑块在左；开为黑轨、白滑块在右。
// Pill switch: light track with the knob left when off, black track with the knob right when on.
static void draw_switch(uint8_t *fb, bool on) {
    EpdRect r = fl_switch_rect();
    ui_fill_round_rect(fb, r, r.height / 2, on ? 0x00 : 0xe6);
    ui_draw_round_rect(fb, r, r.height / 2, 0x38);
    const int cx = on ? r.x + r.width - r.height / 2 : r.x + r.height / 2;
    const int cy = r.y + r.height / 2;
    epd_fill_circle(cx, cy, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, cy, 16, 0x38, fb);
    epd_draw_circle(cx, cy, 15, 0x38, fb);
}

static void draw_slider(uint8_t *fb, int track_y, uint8_t value) {
    const EpdRect track = {UI_QUICK_FL_TRACK_X, track_y, UI_QUICK_FL_TRACK_W, UI_QUICK_FL_TRACK_H};
    ui_fill_round_rect(fb, track, UI_QUICK_FL_TRACK_H / 2, 0xcc);
    const int tx = fl_thumb_x(value);
    if (tx > track.x) {
        ui_fill_round_rect(fb, (EpdRect){track.x, track_y, tx - track.x, UI_QUICK_FL_TRACK_H},
                           UI_QUICK_FL_TRACK_H / 2, UI_GRAY_BLACK);
    }
    const int cy = track_y + UI_QUICK_FL_TRACK_H / 2;
    epd_fill_circle(tx, cy, UI_QUICK_FL_THUMB_R, UI_GRAY_WHITE, fb);
    epd_draw_circle(tx, cy, UI_QUICK_FL_THUMB_R, 0x38, fb);
    epd_draw_circle(tx, cy, UI_QUICK_FL_THUMB_R - 1, 0x38, fb);
}

static void draw_percent(uint8_t *fb, int cy, uint8_t value) {
    char text[8];
    snprintf(text, sizeof(text), "%u%%", (unsigned)value);
    ui_text_fixed_vc(fb, 624, cy, 28, text, EPD_DRAW_ALIGN_RIGHT, false);
}

static void draw_fl_row(uint8_t *fb, const ui_quick_menu_t *menu, ui_quick_control_t control) {
    const EpdRect row = fl_row_rect(control);
    if (row.width <= 0) return;
    ui_fill_rect_fast(row, UI_GRAY_WHITE, fb);
    if (control == UI_QUICK_CTL_ON) {
        ui_draw_icon(fb, 66, 252, 36, UI_ICON_SUN, UI_GRAY_BLACK);
        ui_text_fixed_vc(fb, 92, 252, 30, "前光", EPD_DRAW_ALIGN_LEFT, false);
        draw_switch(fb, menu->fl_on);
    } else if (control == UI_QUICK_CTL_BRIGHTNESS) {
        ui_text_fixed_vc(fb, 60, 294, 28, "亮度", EPD_DRAW_ALIGN_LEFT, false);
        draw_percent(fb, 294, menu->fl_bri);
        draw_slider(fb, UI_QUICK_FL_BRI_TRACK_Y, menu->fl_bri);
    } else if (control == UI_QUICK_CTL_CCT) {
        ui_text_fixed_vc(fb, 60, 358, 28, "色温", EPD_DRAW_ALIGN_LEFT, false);
        draw_percent(fb, 358, menu->fl_cct);
        draw_slider(fb, UI_QUICK_FL_CCT_TRACK_Y, menu->fl_cct);
        ui_text_fixed_vc(fb, 60, 404, 22, "暖", EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed_vc(fb, 624, 404, 22, "冷", EPD_DRAW_ALIGN_RIGHT, false);
    }
}

static void draw_fl_card(uint8_t *fb, const ui_quick_menu_t *menu) {
    const EpdRect card = fl_card_rect();
    ui_fill_round_rect(fb, card, UI_QUICK_FL_CARD_RADIUS, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, card, UI_QUICK_FL_CARD_RADIUS, 0x50);
    ui_draw_round_rect(fb, (EpdRect){card.x + 1, card.y + 1, card.width - 2, card.height - 2},
                       UI_QUICK_FL_CARD_RADIUS - 1, 0x50);
    draw_fl_row(fb, menu, UI_QUICK_CTL_ON);
    draw_fl_row(fb, menu, UI_QUICK_CTL_BRIGHTNESS);
    draw_fl_row(fb, menu, UI_QUICK_CTL_CCT);
}

void ui_quick_menu_end(void) {}

bool ui_quick_menu_draw_button(uint8_t *fb,int button,bool active,bool pressed,EpdRect *dirty) {
    if(!fb||!dirty||button<0||button>=4||epd_rotated_display_width()<UI_LOCK_WIDTH||epd_rotated_display_height()<UI_QUICK_HEIGHT)return false;
    draw_key(fb,button,active,pressed);
    draw_badge(fb,button,active);
    *dirty=key_rect(button);
    return true;
}

bool ui_quick_menu_draw_control(uint8_t *fb, const ui_quick_menu_t *menu,
                                ui_quick_control_t control, EpdRect *dirty) {
    if (!fb || !menu || !menu->frontlight || !dirty) return false;
    const EpdRect row = fl_row_rect(control);
    if (row.width <= 0) return false;
    draw_fl_row(fb, menu, control);
    *dirty = row;
    return true;
}

void ui_quick_menu_draw_status(uint8_t *fb) {
    ui_fill_rect_fast((EpdRect){0,0,epd_rotated_display_width(),76},UI_GRAY_WHITE,fb);
    ui_nav_status(fb);
}

EpdRect ui_quick_menu_rect(const ui_quick_menu_t *menu) {
    const int height = menu != NULL && menu->frontlight
        ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT;
    return (EpdRect){0, 0, UI_LOCK_WIDTH, height};
}

void ui_quick_menu_draw(uint8_t *fb,const ui_quick_menu_t *menu,bool wifi,bool bluetooth) {
    if(!fb)return;
    const int width = epd_rotated_display_width();
    const int height = epd_rotated_display_height();
    const int layer_height = menu != NULL && menu->frontlight
        ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT;
    // 每个像素只采一次原图，直接合成灰阶，不让主页快刷模式把菜单变成点阵。
    // Sample each underlay pixel once and compose grays directly, independently of fast main-page rendering.
    for (int y = 0; y < layer_height && y < height; ++y) for (int x = 0; x < width; ++x) {
        if (!layer_pixel(x, y, width, layer_height)) continue;
        unsigned tint = 238;
        for (int i = 0; i < 4; ++i) {
            int dx = x - center_x(i), dy = y - UI_QUICK_CIRCLE_Y;
            if (dx * dx + dy * dy <= UI_QUICK_RADIUS * UI_QUICK_RADIUS) {
                tint = (i == 0 && wifi) || (i == 1 && bluetooth) ? 176 : 244;
                break;
            }
        }
        uint8_t tone = (uint8_t)((pixel(fb, x, y) * 40u + tint * 216u + 128u) / 256u);
        epd_draw_pixel(x, y, tone, fb);
    }
    // 状态栏留清晰白底，沿用系统时间/签名/电池；四图标与底栏共用Lucide细线44px。
    // Keep the system time/signature/battery legible on white; share navigation's 44px Lucide line icons.
    ui_quick_menu_draw_status(fb);
    for (int i = 0; i < 4; ++i) {
        int cx = center_x(i);
        epd_draw_circle(cx, UI_QUICK_CIRCLE_Y, UI_QUICK_RADIUS, 0x38, fb);
        epd_draw_circle(cx, UI_QUICK_CIRCLE_Y, UI_QUICK_RADIUS - 1, 0x38, fb);
        if (i < 3) ui_draw_icon(fb, cx, UI_QUICK_CIRCLE_Y, 44, s_icons[i], UI_GRAY_BLACK);
        else lock_icon(fb, cx, UI_QUICK_CIRCLE_Y);
        if ((i == 0 && wifi) || (i == 1 && bluetooth)) {
            epd_fill_circle(cx + 30, UI_QUICK_CIRCLE_Y - 30, 5, UI_GRAY_WHITE, fb);
            epd_draw_circle(cx + 30, UI_QUICK_CIRCLE_Y - 30, 5, 0x50, fb);
        }
    }
    // 前光卡片在圆钮下方常驻；没有前光的板保持原来的 264 高度。
    // The frontlight card stays below the circles; boards without one keep the original 264 height.
    if (menu != NULL && menu->frontlight) draw_fl_card(fb, menu);
    ui_fill_round_rect(fb, (EpdRect){width / 2 - 26, layer_height - 24, 52, 6}, 3, 0x70);
    // 只画遮罩内的双像素轮廓，顶部直角、底部圆角，不越过下层边界。
    // Draw a two-pixel outline within the mask: square top, rounded bottom, no underlay leakage.
    for (int y = 0; y < layer_height && y < height; ++y) for (int x = 0; x < width; ++x) {
        if (layer_pixel(x, y, width, layer_height) && (x < 2 || x >= width - 2 || y < 2 ||
            y >= layer_height - 2 || !layer_pixel(x - 2, y, width, layer_height) ||
            !layer_pixel(x + 2, y, width, layer_height) || !layer_pixel(x, y + 2, width, layer_height)))
            epd_draw_pixel(x, y, 0x38, fb);
    }
}
