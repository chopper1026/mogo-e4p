/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epd_highlevel.h"
/// 绘制当前时钟，入口/密码取消/冷启动共用。/ Paint current time at lock entry, PIN cancel and cold boot.
bool lock_clock_draw(uint8_t *fb);
/// 只在成功推屏后登记所画分钟。/ Confirm the drawn minute only after a successful presentation.
void lock_clock_presented(bool success);
/// 在同一锁屏等待会话内刷新；true表示尝试过推屏，调用方应先重新检查唤醒输入。
/// Refresh in the same locked wait; true asks the caller to recheck wake inputs before sleeping.
bool lock_clock_tick(EpdiyHighlevelState *hl);
