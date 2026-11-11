// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
#include "ui_gesture.h"

/// 初始化蓝牙子页及可见快照，不自行启动协议栈。/ Open the sub-page without starting the stack.
void settings_bluetooth_open(void);
/// 离页或锁屏时停止扫描、取消学习。/ Stop discovery and cancel learning on exit or lock.
void settings_bluetooth_close(void);
/// 只绘制已采集状态，不消费报告或写设置。/ Paint the captured state without consuming reports or writing settings.
void settings_bluetooth_render(uint8_t *fb);
/// 采集异步状态；变化时请求页面刷新。/ Capture asynchronous state and report a needed repaint.
bool settings_bluetooth_tick(int64_t now_ms);
/// 处理内容区手势；返回是否需要重绘。/ Handle content gestures and report a needed repaint.
bool settings_bluetooth_gesture(const ui_gesture_event_t *event);
/// 消费内部返回；false 时由设置页返回来源。/ Consume internal Back; otherwise let Settings return to its origin.
bool settings_bluetooth_back(void);
