/* SPDX-License-Identifier: Apache-2.0
 * 中文：顶部快捷栏，最多两排、每排四个无文字圆钮；项目显隐与顺序由装配层提供。
 * 用户2026-11-16修订：取代按页面能力追加与固定键位，旋转键默认在各页常驻；
 * 不支持旋转的页面显示提示，不改变阅读会话。用户可在设置中选择项目和位置。
 * English: A top quick bar with at most two rows of four unlabeled circles, whose
 * visibility and order come from the layer owner. The user's 2026-11-16 revision
 * replaces page-specific appended keys and fixed positions: rotation is resident
 * by default, unavailable pages show a notice without changing the reader session,
 * and Settings manages the visible items and their positions.
 * 前光开关展开亮度和色温胶囊；熄灯收起。灯灭时停顿后继续下拉，或再次下拉，
 * 可仅在本次开层临时展开；连续快滑不连触发。横屏圆键在左、滑杆在右。
 * The light switch expands brightness/temperature capsules and switching off
 * collapses them. While off, a paused continuation or fresh pull expands them
 * for this opening only; a continuous flick never chains both actions. Landscape
 * puts circles on the left and sliders on the right.
 * WiFi、蓝牙、移动网络、震动和内存长按500ms进入对应设置，抬手不再触发短按。
 * Holding WiFi, Bluetooth, cellular, haptics or memory for 500ms opens its settings
 * without also activating the release tap.
 * 冻结：识别器只产出动作及前光事件，不访问页面、broker、硬件、设置或刷新；
 * 不申请帧缓存。绘图与命中使用同一项目顺序和几何，主循环负责调用绘图。
 * Frozen: the recognizer only emits actions and frontlight events, never accesses
 * pages, broker, hardware, settings or presentation, and allocates no framebuffer.
 * Painting and hits share item order and geometry; the loop owns drawing calls.
 */
#pragma once
#include "app.h"
#include "app_quick_items.h"
#include "epdiy.h"

#define UI_QUICK_EDGE_PX 32
#define UI_QUICK_SWIPE_PX 72
// 同一次下拉的「继续」判定：开层后停顿至少这么久，再往下拉过开层行程才算继续
// 下拉；用户实测单次自然下滑总行程常超 150px，按总行程判定会让开层与展开在一
// 次滑动里连触发（2026-10-25 用户反馈）。
// The "continue" test for the same pull: after opening, the finger must pause at
// least this long and then pull the opening distance again; a user's single
// natural swipe routinely spans 150px+, so a total-travel test chains opening and
// expanding inside one flick (user report 2026-10-25).
#define UI_QUICK_PULL_SETTLE_MS 300
// WiFi / 蓝牙 / 震动 / 内存圆键按住这么久直接进设置页；阈值到了就关层，抬手不再算点按。
// Holding the WiFi / Bluetooth / haptics / memory circle this long opens its settings page; the
// layer closes at the threshold and the release no longer counts as a tap.
#define UI_QUICK_HOLD_SETTINGS_MS 500
#define UI_QUICK_HEIGHT 264
#define UI_QUICK_FRONTLIGHT_HEIGHT 480
#define UI_QUICK_CIRCLE_Y 156
#define UI_QUICK_ROTATION_ROW_STEP 140
#define UI_QUICK_LANDSCAPE_COLUMN_STEP 140
#define UI_QUICK_LANDSCAPE_TRACK_X 660
#define UI_QUICK_RADIUS 54
#define UI_QUICK_HIT_RADIUS 66

// 前光胶囊：开灯时圆钮下方展开两条横向胶囊滑杆，直接浮在遮罩上，无卡片、无
// 文字、无旋钮；胶囊内活动灰为空档，白色填充从左缘生长，图标放在胶囊内左端；
// 再次熄灯即收起。
// Frontlight capsules: switching the light on expands two horizontal capsule
// sliders below the circles, floating on the shade with no card, text or knob;
// the capsule interior is the active gray when empty, a white fill grows from
// the left edge, and the icon sits inside the left end; switching off collapses
// them again.
#define UI_QUICK_FL_ICON_CX 96
#define UI_QUICK_FL_ICON_SIZE 44
#define UI_QUICK_FL_BRI_CY 284
#define UI_QUICK_FL_CCT_CY 384
#define UI_QUICK_FL_TRACK_X 48
#define UI_QUICK_FL_TRACK_W 588
#define UI_QUICK_FL_TRACK_H 64
// 行局刷矩形：覆盖整条胶囊并留出手指余量。/ Local row repaint: the whole capsule plus finger margin.
#define UI_QUICK_FL_ROW_X 40
#define UI_QUICK_FL_ROW_W 604
#define UI_QUICK_FL_ROW_H 80
// 命中区覆盖整行：不要求压到胶囊本身。
// Hit zones cover the whole row, so a finger never has to land on the capsule.
#define UI_QUICK_FL_BRI_HIT_Y0 228
#define UI_QUICK_FL_BRI_HIT_Y1 333
#define UI_QUICK_FL_CCT_HIT_Y0 334
#define UI_QUICK_FL_CCT_HIT_Y1 440

typedef enum {
    UI_QUICK_NONE, UI_QUICK_OPEN, UI_QUICK_CLOSE, UI_QUICK_EXPAND,
    UI_QUICK_WIFI, UI_QUICK_BLUETOOTH, UI_QUICK_CELLULAR, UI_QUICK_FULL, UI_QUICK_LOCK,
    UI_QUICK_WIFI_SETTINGS, UI_QUICK_BLUETOOTH_SETTINGS, UI_QUICK_CELLULAR_SETTINGS,
    UI_QUICK_ROTATION,
    UI_QUICK_HAPTICS, UI_QUICK_HAPTICS_SETTINGS,
    UI_QUICK_MEMORY_CLEAN, UI_QUICK_MEMORY_SETTINGS,
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
    bool sharing_tap; ///< 本次闭层接触仍是页面轻点候选。/ This closed-layer contact is still a candidate page tap.
    app_quick_layout_t layout; ///< 装配层提供的显示与顺序。/ Visibility/order supplied by the layer owner.
    const char *network_notice; ///< 静态网络错误提示。/ Static network error notice.
    bool rotation_notice; ///< 当前页面不能旋转的提示。/ Rotation-unavailable notice for the current page.
    bool rotation_available, rotation_enabled; ///< 只由当前页面提供，不落盘 / Supplied by the current page, never persisted
    bool haptics_available, haptics_enabled; ///< 马达能力与已保存的开关状态 / Motor capability and saved switch state
    bool cellular_available, cellular_enabled, cellular_ready; ///< Runtime capability and state; absent hardware hides the entry
    bool memory_busy; ///< 清理状态来自 broker；识别器不读取 broker / Cleanup state supplied by the broker owner; recognizer never reads it
    uint16_t x0, y0;
    int hit;
    int64_t started_ms;
    /// 本板是否有前光；仅控制前光项目与滑杆能力。
    /// Frontlight capability controls the light item and sliders only.
    bool frontlight;
    bool fl_on;
    /// 灯灭时继续下拉产生的本次开层临时展开；开层、收层与熄灯点按都会清零，
    /// 不跨层记忆，也不落盘。
    /// Session-only expansion from pulling the open layer further down while the
    /// light is off; opening, closing and the light-off tap all clear it. It is
    /// never remembered across openings or persisted.
    bool fl_expanded;
    /// 接触态：本次下拉刚把层拉开且灯灭，手指未抬；停顿一小会儿后再继续往下拉
    /// 过一程才临时展开——一次连续快滑的总行程不算「继续下拉」，开层与展开不会
    /// 在一次滑动里连触发。
    /// Contact state: this very pull just opened the layer with the light off;
    /// only a pause and then a further pull expands the capsules. A continuous
    /// fast swipe's total travel never counts, so opening and expanding cannot
    /// chain inside one flick.
    bool fl_pull;
    uint16_t fl_pull_x;    ///< 停顿锚点 x。/ x of the pause anchor.
    uint16_t fl_pull_y;    ///< 停顿判定的移动锚点 y。/ y anchor for the stillness test.
    int64_t fl_pull_ms;    ///< 最近一次明显移动的时间。/ time of the last material movement.
    bool fl_pull_settled;  ///< 已停顿，继续下拉可展开。/ paused; a further pull may expand.
    uint8_t fl_bri;
    uint8_t fl_cct;
    /// 每条滑杆自上次定稿以来达到的最大档位：定稿第一段据此把弄脏范围洗白。
    /// 由输入层在拖动中只增不减，松手两段定稿后收回当前档位。
    /// Per slider, the highest level reached since the last settle: stage one
    /// whitens this extent. The input layer only grows it during drags and the
    /// two-stage settle shrinks it back to the current level on release.
    uint8_t fl_bri_dirty;
    uint8_t fl_cct_dirty;
    ui_quick_control_t drag;
} ui_quick_menu_t;

/// 已保存的顺序同时控制绘图和命中；缺失硬件的项目不占位置。
/// Saved order drives painting and hits; unavailable hardware consumes no slot.
static inline bool ui_quick_menu_item_available(const ui_quick_menu_t *menu, app_quick_item_t item) {
    if (item == APP_QUICK_CELLULAR) return menu && menu->cellular_available;
    if (item == APP_QUICK_HAPTICS) return menu && menu->haptics_available;
    if (item == APP_QUICK_FRONTLIGHT) return menu && menu->frontlight;
    return item < APP_QUICK_ITEM_COUNT;
}
static inline const app_quick_layout_t *ui_quick_menu_layout(const ui_quick_menu_t *menu) {
    static const app_quick_layout_t defaults = APP_QUICK_LAYOUT_DEFAULT;
    return menu && app_quick_layout_valid(&menu->layout) ? &menu->layout : &defaults;
}
static inline app_quick_item_t ui_quick_menu_item(const ui_quick_menu_t *menu, int button) {
    const app_quick_layout_t *layout = ui_quick_menu_layout(menu);
    for (unsigned i = 0; i < layout->count; ++i) {
        app_quick_item_t item = (app_quick_item_t)layout->order[i];
        if (ui_quick_menu_item_available(menu, item) && button-- == 0) return item;
    }
    return APP_QUICK_ITEM_COUNT;
}
static inline int ui_quick_menu_button_count(const ui_quick_menu_t *menu) {
    const app_quick_layout_t *layout = ui_quick_menu_layout(menu);
    int count = 0;
    for (unsigned i = 0; i < layout->count; ++i)
        if (ui_quick_menu_item_available(menu, (app_quick_item_t)layout->order[i])) ++count;
    return count;
}
static inline int ui_quick_menu_find_button(const ui_quick_menu_t *menu, app_quick_item_t item) {
    for (int i = 0; i < ui_quick_menu_button_count(menu); ++i)
        if (ui_quick_menu_item(menu, i) == item) return i;
    return -1;
}
static inline int ui_quick_menu_base_button_count(const ui_quick_menu_t *menu) {
    int count = ui_quick_menu_button_count(menu);
    return count < 4 ? count : 4;
}
static inline int ui_quick_menu_lock_button(const ui_quick_menu_t *menu) { return ui_quick_menu_find_button(menu, APP_QUICK_LOCK); }
static inline int ui_quick_menu_cellular_button(const ui_quick_menu_t *menu) { return ui_quick_menu_find_button(menu, APP_QUICK_CELLULAR); }
static inline int ui_quick_menu_rotation_button(const ui_quick_menu_t *menu) { return ui_quick_menu_find_button(menu, APP_QUICK_ROTATION); }
static inline int ui_quick_menu_haptics_button(const ui_quick_menu_t *menu) { return ui_quick_menu_find_button(menu, APP_QUICK_HAPTICS); }
static inline int ui_quick_menu_memory_button(const ui_quick_menu_t *menu) { return ui_quick_menu_find_button(menu, APP_QUICK_MEMORY); }
static inline bool ui_quick_menu_landscape(void) {
    return epd_rotated_display_width() > epd_rotated_display_height();
}
/// 行列与命中半径由同一布局描述提供，横屏为滑杆保留独立触摸余量。
/// One layout descriptor supplies rows, columns and hit radii, leaving a separate landscape slider margin.
typedef struct { int row, column, columns, left, width, hit_radius; } ui_quick_button_layout_t;
static inline ui_quick_button_layout_t ui_quick_menu_button_layout(const ui_quick_menu_t *menu, int button, int width) {
    ui_quick_button_layout_t layout = {.row = button / 4, .column = button % 4, .columns = ui_quick_menu_base_button_count(menu),
        .left = 0, .width = width, .hit_radius = UI_QUICK_HIT_RADIUS};
    if (button >= 4) {
        layout.row = 1;
        layout.column = button - 4;
        layout.columns = ui_quick_menu_button_count(menu) - 4;
        if (ui_quick_menu_landscape()) {
            layout.left = 48;
            layout.width = UI_QUICK_LANDSCAPE_TRACK_X - 16 - layout.left;
        }
        const int radius = layout.width / (2 * layout.columns) - 2;
        if (radius < layout.hit_radius) layout.hit_radius = radius;
    }
    return layout;
}
static inline int ui_quick_menu_button_x(const ui_quick_menu_t *menu, int button, int width) {
    const ui_quick_button_layout_t layout = ui_quick_menu_button_layout(menu, button, width);
    if (ui_quick_menu_landscape())
        return 48 + UI_QUICK_RADIUS + layout.column * UI_QUICK_LANDSCAPE_COLUMN_STEP;
    return layout.left + (2 * layout.column + 1) * layout.width / (2 * layout.columns);
}
static inline int ui_quick_menu_row_offset(const ui_quick_menu_t *menu) {
    return menu && ui_quick_menu_button_count(menu) > 4 ? UI_QUICK_ROTATION_ROW_STEP : 0;
}
static inline int ui_quick_menu_button_y(const ui_quick_menu_t *menu, int button) {
    return UI_QUICK_CIRCLE_Y + ui_quick_menu_button_layout(menu, button, epd_rotated_display_width()).row * UI_QUICK_ROTATION_ROW_STEP;
}
static inline int ui_quick_menu_height(const ui_quick_menu_t *menu) {
    if (ui_quick_menu_landscape())
        return UI_QUICK_HEIGHT + UI_QUICK_ROTATION_ROW_STEP;
    return (menu && menu->frontlight && (menu->fl_on || menu->fl_expanded)
        ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT) + ui_quick_menu_row_offset(menu);
}
/// 滑杆绘图、数值换算与命中共用几何；横屏在右侧与两排圆键对齐。
/// Slider painting, value mapping and hit tests share geometry; landscape aligns right-side tracks with the two key rows.
static inline EpdRect ui_quick_menu_frontlight_track(const ui_quick_menu_t *menu, ui_quick_control_t control) {
    if (control != UI_QUICK_CTL_BRIGHTNESS && control != UI_QUICK_CTL_CCT) return (EpdRect){0};
    if (ui_quick_menu_landscape())
        return (EpdRect){UI_QUICK_LANDSCAPE_TRACK_X,
            UI_QUICK_CIRCLE_Y + (control == UI_QUICK_CTL_CCT ? UI_QUICK_ROTATION_ROW_STEP : 0) - UI_QUICK_FL_TRACK_H / 2,
            epd_rotated_display_width() - UI_QUICK_LANDSCAPE_TRACK_X - 48, UI_QUICK_FL_TRACK_H};
    return (EpdRect){UI_QUICK_FL_TRACK_X,
        (control == UI_QUICK_CTL_CCT ? UI_QUICK_FL_CCT_CY : UI_QUICK_FL_BRI_CY)
            + ui_quick_menu_row_offset(menu) - UI_QUICK_FL_TRACK_H / 2,
        UI_QUICK_FL_TRACK_W, UI_QUICK_FL_TRACK_H};
}
/// 命中覆盖整行并留手指余量，左右分区间隙不响应滑杆。
/// Hit zones cover each row with finger margins, leaving the inter-column gap inactive.
static inline EpdRect ui_quick_menu_frontlight_hit(const ui_quick_menu_t *menu, ui_quick_control_t control) {
    const EpdRect track = ui_quick_menu_frontlight_track(menu, control);
    if (track.width <= 0) return (EpdRect){0};
    if (ui_quick_menu_landscape())
        return (EpdRect){track.x - 8, track.y - 22, track.width + 16, track.height + 44};
    const int y0 = control == UI_QUICK_CTL_CCT ? UI_QUICK_FL_CCT_HIT_Y0 : UI_QUICK_FL_BRI_HIT_Y0;
    const int y1 = control == UI_QUICK_CTL_CCT ? UI_QUICK_FL_CCT_HIT_Y1 : UI_QUICK_FL_BRI_HIT_Y1;
    return (EpdRect){UI_QUICK_FL_ROW_X, y0 + ui_quick_menu_row_offset(menu), UI_QUICK_FL_ROW_W, y1 - y0 + 1};
}

typedef struct {
    ui_quick_action_t action;
    ui_quick_control_t control;
    uint8_t value;
    /// 松手或点选时置位，拖动过程中保持 false；主循环只在这里写 NVS。
    /// Set on release or tap and left false while dragging; only then does the loop write NVS.
    bool commit;
} ui_quick_event_t;

/// 清除层和手势；保留能力、震动状态与前光预览值。/ Clear layer/contact state while retaining capabilities, haptics state and light previews.
void ui_quick_menu_reset(ui_quick_menu_t *menu);
/// 绘制或清除本次旋转提示。/ Paint or clear the current rotation notice.
bool ui_quick_menu_draw_notice(uint8_t *fb, const ui_quick_menu_t *menu, EpdRect *dirty);
/// 设置本板前光能力并同步预览值；层高随开灯状态伸缩。
/// Set frontlight capability and sync the preview values; the layer height follows the on state.
void ui_quick_menu_set_frontlight(ui_quick_menu_t *menu, bool available, bool on,
                                  uint8_t brightness, uint8_t cct);
/// true 接管本帧；页面可在闭层时共享轻点候选，拖动或长按后不再放回。已展开或正在关闭的层独占输入。
/// True owns this frame. A closed layer may share a tap candidate until movement or a hold; an open or dismissing layer is exclusive.
bool ui_quick_menu_feed(ui_quick_menu_t *menu, const app_ctx_t *ctx, bool allowed,
                        bool share_tap, ui_quick_event_t *event);
/// 取消接触但保留已打开的层；正在拖动的滑杆保留当前值，由主循环决定是否落盘。
/// Cancel a contact while retaining an already open layer; a dragged slider keeps its value and the loop decides persistence.
void ui_quick_menu_cancel_input(ui_quick_menu_t *menu);
/// 只覆盖顶部，按已选按钮数量安排两排，前光滑杆按横竖屏排版。
/// Cover only the top, arranging selected buttons in at most two rows and orienting the sliders for the screen.
void ui_quick_menu_draw(uint8_t *fb, const ui_quick_menu_t *menu, bool wifi, bool bluetooth);
/// 当前按住的圆键，取消/滑出/释放或按住卡片时为-1。/ The held key, or -1 after cancellation, movement, release or a card press.
int ui_quick_menu_pressed(const ui_quick_menu_t *menu);
/// 按项目重画圆键和状态点/按压；前光活动态跟随开关，不依赖槽位。
/// Repaint an item and its state/press marks; frontlight activity follows its switch, independent of slot.
bool ui_quick_menu_draw_button(uint8_t *fb, const ui_quick_menu_t *menu, int button,
                               bool active, bool pressed, EpdRect *dirty);
/// 只重画胶囊行并返回受影响的矩形，供滑杆拖动局刷；熄灯时行不存在。
/// Repaint one capsule row and report the affected rectangle for local slider refreshes; rows do not exist while the light is off.
bool ui_quick_menu_draw_control(uint8_t *fb, const ui_quick_menu_t *menu,
                                ui_quick_control_t control, EpdRect *dirty);
/// 定稿第一段：把该行画成「白填充到弄脏上沿」的中间态并返回受影响的矩形，
/// 供主循环先洗白再落灰；熄灯时行不存在。
/// Settle stage one: paint the row with the white fill grown to the dirty
/// extent and report the rectangle, so the loop whitens before landing gray;
/// rows do not exist while the light is off.
bool ui_quick_menu_draw_control_white(uint8_t *fb, const ui_quick_menu_t *menu,
                                      ui_quick_control_t control, EpdRect *dirty);
/// 只重画清晰状态栏，供网络状态变化时局刷。/ Repaint only the sharp status strip for network changes.
void ui_quick_menu_draw_status(uint8_t *fb);
/// 当前层的整块区域；高度随开灯伸缩，收起前用旧高度并集擦除卡片。
/// The whole layer; the height follows the light state, and close uses the union with the previous height to erase the card.
EpdRect ui_quick_menu_rect(const ui_quick_menu_t *menu);
/// 兼容主循环的关闭出口；实心控件没有缓存需要释放。/ Close hook for the loop; solid controls hold no cache.
void ui_quick_menu_end(void);
