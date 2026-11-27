/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：阅读提示卡与确认提示卡的纯绘制和清除区域，换行依次分隔标题和两行说明。
 * English: Pure reader-notice and confirmation-notice painting and bounds; newlines separate a title and two detail lines.
 */
#pragma once
#include <stdbool.h>
#include "epdiy.h"

typedef struct {
    EpdRect area;     ///< 包含完整边框的清除与刷新区域 / Erase and presentation bounds covering the complete frame
    EpdRect checkbox; ///< 「今后不再提示」选项点击区域 / Tap target for the "don't show again" option
    EpdRect confirm;  ///< 确认按钮点击区域 / Tap target for the Confirm button
} ui_reader_notice_confirm_layout_t;

/// 返回包含完整边框的刷新区域。/ Return presentation bounds covering the complete frame.
EpdRect ui_reader_notice_area(const char *message, unsigned system_font_size);
/// 白底黑字提示；不推屏、不更改设置或处理输入。/ White notice with black ink; no presentation, settings changes or input handling.
void ui_reader_notice_draw(uint8_t *fb, const char *message, unsigned system_font_size);
/// 返回带勾选项和确认按钮的提示卡布局，绘制与命中共用。/ Shared geometry for a confirmation notice with checkbox and Confirm button.
ui_reader_notice_confirm_layout_t ui_reader_notice_confirm_layout(const char *message, unsigned system_font_size);
/// 绘制带勾选项和确认按钮的白底提示卡；纯绘制，不推屏或修改设置。/ Paint a white confirmation notice; paint-only without presentation or settings access.
void ui_reader_notice_confirm_draw(uint8_t *fb, const char *message, const char *option_label,
                                   const char *confirm_label, bool checked, unsigned system_font_size);
