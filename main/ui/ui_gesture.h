/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：由主循环喂入触摸快照的纯状态识别器，不解释页面动作。
 * English: Pure state recognizer fed by loop touch snapshots; no page actions.
 *
 * 冻结：翻页无按下反馈，阈值来自 ui_kit.h；短拖取消仅用于普通手势，显式捕获的连续控件需要区分抬手与取消。
 * Frozen: Turns have no press feedback and use ui_kit.h thresholds; short-drag cancellation is for ordinary gestures, not explicitly captured continuous controls.
 */
#pragma once
#include "app.h"

typedef enum {
    UI_GESTURE_PRESS, ///< 按下 / Press
    UI_GESTURE_MOVE, ///< 按住移动，用于连续滑块 / Move while held for continuous sliders
    UI_GESTURE_LONG_PRESS, ///< 长按一次 / Single long press
    UI_GESTURE_TAP, ///< 原点轻点 / Tap at origin
    UI_GESTURE_SWIPE_L, ///< 左滑 / Swipe left
    UI_GESTURE_SWIPE_R, ///< 右滑 / Swipe right
    UI_GESTURE_SWIPE_U, ///< 上滑 / Swipe up
    UI_GESTURE_SWIPE_D, ///< 下滑 / Swipe down
    UI_GESTURE_CANCEL, ///< 取消 / Cancel
    UI_GESTURE_RELEASE, ///< 显式启用的长拖或连续控件正常释放 / Normal release of an opt-in long drag or continuous control
} ui_gesture_type_t;

typedef struct ui_gesture_event_s {
    ui_gesture_type_t type; ///< 事件类型 / Event type
    uint16_t x0, y0; ///< 按下原点，也是 TAP 的逻辑坐标 / Press origin, also the logical TAP position
    uint16_t x, y; ///< 当前或释放位置，供控件落外取消 / Current or release position for outside-control cancellation
    int64_t hold_ms; ///< 非负持续时间 / Nonnegative duration
} ui_gesture_event_t;

typedef struct {
    bool active; ///< 正在跟踪 / Tracking a contact
    uint16_t x0, y0; ///< 起点 / Origin
    uint16_t x, y; ///< 最近有效坐标 / Last valid coordinates
    int64_t t0_ms; ///< 起始时刻 / Start time
    int64_t last_ms; ///< 最近时刻，倒退时取消 / Last time, cancel on clock regression
    bool moved; ///< 曾超出容差 / Has exceeded movement slop
    bool long_drag; ///< 接触起点固定的长拖能力 / Long-drag option captured at press
    bool captured_drag; ///< 本次接触归连续横向控件 / Contact belongs to a continuous horizontal control
    uint8_t horizontal_owner; ///< 输入层在按下时锁定；识别器本身不解释 / Origin owner, opaque to the recognizer
    uint8_t orientation; ///< 按下时的画面方向 / Orientation at press
    bool deferred_press; ///< 侧边延迟转交按下，避免滑杆等提前生效 / Defer edge presses until local intent is clear
    bool vertical_claim; ///< 纵向操作已领取本次接触 / A vertical interaction owns this contact
    bool long_fired; ///< 已发出长按 / Long press emitted
} ui_gesture_t;

/// 清除状态；进出页面时调用。/ Clear state when entering or leaving a page.
void ui_gesture_reset(ui_gesture_t *gesture);
/// 每次最多一个事件；consumed、多点或缺失快照取消活动序列。
/// Emit at most one event; consumed, multitouch or a missing snapshot cancels an active sequence.
bool ui_gesture_feed(ui_gesture_t *gesture, const app_ctx_t *ctx, ui_gesture_event_t *out);

/// 长拖能力只在按下时采样，默认接口保持原行为。/ Capture long-drag capability at press; the default API stays unchanged.
bool ui_gesture_feed_options(ui_gesture_t *gesture, const app_ctx_t *ctx,
                             ui_gesture_event_t *out, bool long_drag);

/// 在按下时锁定连续控件归属；横坐标变化发 MOVE，正常抬手总发 RELEASE。
/// Latch a continuous control at press; emit MOVE for changed x and RELEASE for any normal lift.
bool ui_gesture_feed_drag(ui_gesture_t *gesture, const app_ctx_t *ctx,
                          ui_gesture_event_t *out, bool long_drag, bool captured_drag);
