/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：电源层的点按确认及纯绘图，硬件动作由主循环执行。
 * English: Power-layer tap confirmation and pure drawing; the loop owns hardware actions.
 */
#pragma once

#include "app.h"
#include "epdiy.h"

typedef enum {
    UI_POWER_ACTION_NONE = 0,
    UI_POWER_ACTION_CANCEL,
    UI_POWER_ACTION_SHUTDOWN,
    UI_POWER_ACTION_RESTART,
} ui_power_action_t;

typedef struct {
    ui_power_action_t target; ///< 按下时捕获的动作。/ Action captured on press.
    int x0, y0; ///< 接触起点。/ Contact origin.
    int64_t started_ms; ///< 接触起始时间，时钟倒退取消。/ Contact start; cancel on clock regression.
    bool swallow; ///< 忽略已中断或打开时已有的接触，直到抬手。/ Ignore an interrupted or pre-existing contact until lift.
} ui_power_dialog_t;

/// 重置点按；接触存在或不确定时，先等待抬手。/ Reset the tap; wait for lift if contact is present or uncertain.
void ui_power_dialog_reset(ui_power_dialog_t *dialog, bool touching);
/// 仅提交在同一按钮内完成的有效单指点按。/ Commit only a valid single-finger tap completed inside the same button.
ui_power_action_t ui_power_dialog_feed(ui_power_dialog_t *dialog, const app_ctx_t *ctx);
/// 按钮的绘制与点击使用同一逻辑矩形。/ Drawing and hit testing share each logical button rectangle.
EpdRect ui_power_dialog_button_rect(ui_power_action_t action);
void ui_power_dialog_draw(uint8_t *fb);
EpdRect ui_power_dialog_rect(void);
void ui_power_final_draw(uint8_t *fb, bool restarting);
/// 事件路径生成关机画面；自定义图片仅访问TF缓存，票根/拼贴可读取卡上阅读素材。
/// Event-only shutdown art; custom images use SD caches, while ticket/collage may read card sources.
void ui_power_shutdown_prepare(uint8_t *fb);
