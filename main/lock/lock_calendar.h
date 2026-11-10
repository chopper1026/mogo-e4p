/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/// 使用当前北京时间绘制所选日历；时间无效时绘制校准提示。/ Draw the chosen face for Beijing time, or a clock-calibration prompt.
bool lock_calendar_draw(uint8_t *framebuffer);
/// 缓存身份包含日期和子样式，跨日/换样式后拒绝旧图。/ Include date and variant in cache identity, rejecting stale art after either changes.
uint64_t lock_calendar_key(void);

/// 已绘制画面的身份，保存时避免跨午夜误标日期。/ Identity of the drawn frame, avoiding a midnight race during cache writes.
uint64_t lock_calendar_frame_key(void);
