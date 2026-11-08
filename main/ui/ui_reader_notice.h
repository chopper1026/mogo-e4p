/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：阅读提示卡的纯绘制与清除区域，换行依次分隔标题和两行说明。
 * English: Pure reader-notice painting and erase bounds; newlines separate a title and two detail lines.
 */
#pragma once
#include "epdiy.h"

/// 返回包含完整边框的刷新区域。/ Return presentation bounds covering the complete frame.
EpdRect ui_reader_notice_area(const char *message, unsigned system_font_size);
/// 白底黑字提示；不推屏、不更改设置或处理输入。/ White notice with black ink; no presentation, settings changes or input handling.
void ui_reader_notice_draw(uint8_t *fb, const char *message, unsigned system_font_size);
