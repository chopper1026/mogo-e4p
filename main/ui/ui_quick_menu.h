/* SPDX-License-Identifier: Apache-2.0
 * 中文：全宽浅顶部层，下圆角与四个无文字圆钮；有前光的板把第四枚圆钮改成
 * 前光开关，圆钮下方常驻亮度与色温两条大滑杆。三个主页模式统一灰阶和清晰边框。
 * English: A shallow full-width top layer with rounded bottom corners and four
 * unlabeled circles; boards with a frontlight use the fourth circle as its
 * switch and keep two large brightness/colour-temperature sliders below. All
 * main modes share grayscale and clear borders.
 * 用户修订：锁屏图标移出快捷栏；第四枚圆钮用黑底白太阳表示开灯、浅底黑太阳
 * 表示熄灭，卡片只留两条加粗滑杆且不显示百分比。滑杆不自动开灯，灯关时调档
 * 只记录数值。
 * User revision: drop the lock icon from the quick layer; the fourth circle is
 * the frontlight switch, black with a white sun when on and light with a black
 * sun when off, and the card keeps only the two heavier sliders without
 * percentage labels. Sliders never turn the light on by themselves, so an off
 * light only records the new values.
 * 冻结：不访问硬件、设置或刷新，不申请帧缓存；只在展开/状态变化后由主循环调用绘图。
 * Frozen: No hardware, settings, presentation or frame allocations; the loop draws only on opening or state changes.
 */
#pragma once
#include "app.h"

#define UI_QUICK_EDGE_PX 32
#define UI_QUICK_SWIPE_PX 72
#define UI_QUICK_HEIGHT 264
#define UI_QUICK_FRONTLIGHT_HEIGHT 480
#define UI_QUICK_CIRCLE_Y 156
#define UI_QUICK_RADIUS 54
#define UI_QUICK_HIT_RADIUS 66

// 前光卡片：圆钮下方 224..444，给收放把手留到 456。
// Frontlight card: 224..444 below the circles, leaving the handle at 456.
#define UI_QUICK_FL_CARD_X 36
#define UI_QUICK_FL_CARD_Y 224
#define UI_QUICK_FL_CARD_W 612
#define UI_QUICK_FL_CARD_H 220
#define UI_QUICK_FL_CARD_RADIUS 18
#define UI_QUICK_FL_TRACK_X 60
#define UI_QUICK_FL_TRACK_W 564
#define UI_QUICK_FL_TRACK_H 14
#define UI_QUICK_FL_THUMB_R 22
#define UI_QUICK_FL_BRI_TRACK_Y 293
#define UI_QUICK_FL_CCT_TRACK_Y 383
// 命中区覆盖整行：细轨两侧留足手指，不要求压到轨道本身。
// Hit zones cover the whole row, so a finger never has to land on the thin track.
#define UI_QUICK_FL_BRI_HIT_Y0 228
#define UI_QUICK_FL_BRI_HIT_Y1 331
#define UI_QUICK_FL_CCT_HIT_Y0 332
#define UI_QUICK_FL_CCT_HIT_Y1 440
#define UI_QUICK_FL_BRI_ROW_Y 228
#define UI_QUICK_FL_BRI_ROW_H 104
#define UI_QUICK_FL_CCT_ROW_Y 332
#define UI_QUICK_FL_CCT_ROW_H 108

typedef enum {
    UI_QUICK_NONE, UI_QUICK_OPEN, UI_QUICK_CLOSE,
    UI_QUICK_WIFI, UI_QUICK_BLUETOOTH, UI_QUICK_FULL, UI_QUICK_LOCK,
} ui_quick_action_t;

/// 前光控件；快捷层只产出事件，硬件和设置由主循环执行。
/// Frontlight controls; the layer only emits events and the loop owns hardware and settings.
typedef enum {
    UI_QUICK_CTL_NONE = 0,
    UI_QUICK_CTL_ON,
    UI_QUICK_CTL_BRIGHTNESS,
    UI_QUICK_CTL_CCT,
} ui_quick_control_t;

typedef struct {
    bool open, tracking, swallow, moved;
    uint16_t x0, y0;
    int hit;
    int64_t started_ms;
    /// 本板是否有前光；没有时第四枚圆钮画锁屏，不画卡片且层高仍是 UI_QUICK_HEIGHT。
    /// Whether this board has a frontlight; without one the fourth circle keeps the lock, the card is omitted and the layer stays UI_QUICK_HEIGHT.
    bool frontlight;
    bool fl_on;
    uint8_t fl_bri;
    uint8_t fl_cct;
    ui_quick_control_t drag;
} ui_quick_menu_t;

typedef struct {
    ui_quick_action_t action;
    ui_quick_control_t control;
    uint8_t value;
    /// 松手或点选时置位，拖动过程中保持 false；主循环只在这里写 NVS。
    /// Set on release or tap and left false while dragging; only then does the loop write NVS.
    bool commit;
} ui_quick_event_t;

/// 清除层和手势；保留本板前光能力与预览值。/ Clear layer and contact state while keeping frontlight capability and preview values.
void ui_quick_menu_reset(ui_quick_menu_t *menu);
/// 设置本板前光能力并同步预览值。/ Set frontlight capability and sync the preview values.
void ui_quick_menu_set_frontlight(ui_quick_menu_t *menu, bool available, bool on,
                                  uint8_t brightness, uint8_t cct);
/// 返回true表示整条触摸序列被接管；多指/读错不执行按钮与滑杆，收起之后仍吞到抬手。
/// True owns the whole contact stream; multitouch/read errors never activate controls, and dismissal still swallows until release.
bool ui_quick_menu_feed(ui_quick_menu_t *menu, const app_ctx_t *ctx, bool allowed,
                        ui_quick_event_t *event);
/// 取消接触但保留已打开的层；正在拖动的滑杆保留当前值，由主循环决定是否落盘。
/// Cancel a contact while retaining an already open layer; a dragged slider keeps its value and the loop decides persistence.
void ui_quick_menu_cancel_input(ui_quick_menu_t *menu);
/// 只覆盖顶部，保留下层其他像素；有前光时连同卡片一起绘制。
/// Cover the top only, leaving other pixels intact; draw the frontlight card when present.
void ui_quick_menu_draw(uint8_t *fb, const ui_quick_menu_t *menu, bool wifi, bool bluetooth);
/// 当前按住的圆键，取消/滑出/释放或按住卡片时为-1。/ The held key, or -1 after cancellation, movement, release or a card press.
int ui_quick_menu_pressed(const ui_quick_menu_t *menu);
/// 重画实心圆键并叠加状态点/圆内按压，不分配、不模糊、不排字。第四枚圆钮按
/// `menu->frontlight` 画太阳或锁；前光板开灯为黑底白太阳，熄灭为浅底黑太阳。
/// Repaint a solid key with its state badge and interior press, without allocating,
/// blurring or shaping text. The fourth circle shows a sun on frontlight boards
/// (or the lock elsewhere); the light-on face is black with a white sun.
bool ui_quick_menu_draw_button(uint8_t *fb, const ui_quick_menu_t *menu, int button,
                               bool active, bool pressed, EpdRect *dirty);
/// 只重画卡片的一行并返回受影响的矩形，供滑杆拖动局刷。
/// Repaint one card row and report the affected rectangle for local slider refreshes.
bool ui_quick_menu_draw_control(uint8_t *fb, const ui_quick_menu_t *menu,
                                ui_quick_control_t control, EpdRect *dirty);
/// 只重画清晰状态栏，供网络状态变化时局刷。/ Repaint only the sharp status strip for network changes.
void ui_quick_menu_draw_status(uint8_t *fb);
/// 当前层的整块区域；收起前用旧高度并集擦除卡片。/ The whole layer; close uses the union with the previous height to erase the card.
EpdRect ui_quick_menu_rect(const ui_quick_menu_t *menu);
/// 兼容主循环的关闭出口；实心控件没有缓存需要释放。/ Close hook for the loop; solid controls hold no cache.
void ui_quick_menu_end(void);
