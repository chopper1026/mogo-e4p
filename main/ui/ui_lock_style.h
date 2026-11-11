/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0 */
#pragma once
#include "epdiy.h"
#include "ui_gesture.h"
/// 设置页内的选择器；render纯绘制，tick接收文件任务及准备内置样式。
/// In-page selector: render only paints; tick receives file results and prepares built-in faces.
void ui_lock_style_open(const uint8_t *background);
/// 离页停止任务并保留有界小图；介质失效才丢弃。/ Leave while retaining bounded thumbnails until media invalidation.
void ui_lock_style_close(void);
void ui_lock_style_media_lost(void);
void ui_lock_style_media_ready(void);
void ui_lock_style_render(uint8_t *fb);
/// 仅当前可见批次全部就绪/失败时返回true，调用方执行一次整屏GC16。
/// True once a changed visible batch settles (ready or failed); present one whole-screen GC16.
bool ui_lock_style_tick(int64_t now_ms);
/// 已完成批次的区域；未完成结果及不可见预读不触发刷新。/ Completed batch bounds; intermediate/invisible results never repaint.
EpdRect ui_lock_style_dirty(void);
bool ui_lock_style_gesture(const ui_gesture_event_t *event);
/// 返回true表示从长按预览返回选择器，false由设置页退回上级。
bool ui_lock_style_back(void);
