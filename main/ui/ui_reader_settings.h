/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从 app_book 提取的阅读设置绘制与共用几何；调用方持有设置和页面状态。
 * English: Reader-setting painting and shared geometry extracted from app_book; callers own settings and page state.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"

typedef enum {
    UI_READER_SETTINGS_INPUT,
    UI_READER_SETTINGS_PAGE,
    UI_READER_SETTINGS_REFRESH,
    UI_READER_SETTINGS_GROUP_COUNT,
} ui_reader_settings_group_t;

typedef struct {
    unsigned system_font_size; ///< 界面字号百分比 / UI font percentage
    unsigned full_pages; ///< 自动全刷间隔，零为不强制 / Full-refresh interval, zero disables forcing
    bool turn_effect; ///< 水波纹效果 / Ripple effect
    bool vertical_turn; ///< 上下点击翻页 / Vertical tap zones
    bool statusbar; ///< 显示状态栏 / Visible status bar
    bool hide_images; ///< 跳过书内插图 / Skip inline images
    bool vertical_text; ///< 竖排正文 / Vertical body text
    bool book_styles; ///< 保留书内样式 / Keep in-book styles
    bool touch_disabled; ///< 关闭阅读屏幕触摸 / Reader screen touch disabled
    ui_reader_settings_group_t group; ///< 当前设置分组 / Visible settings group
    bool grid_tap; ///< 九宫格点按 / Custom grid taps
} ui_reader_settings_values_t;

/// 阅读设置顶部；与页面命中共用，字号限制为100至200。/ Shared sheet top for drawing and hit testing, clamped to 100–200 percent.
int ui_reader_settings_top(unsigned system_font_size);
/// 十项设置所属分组；无效索引返回 GROUP_COUNT。/ Group for each setting; invalid indices return GROUP_COUNT.
ui_reader_settings_group_t ui_reader_settings_group(int index);
/// 分组页签共用绘制和命中矩形。/ Shared tab painting and hit bounds.
EpdRect ui_reader_settings_tab_rect(unsigned system_font_size, int index);
/// 分组内整行矩形；调用方只命中当前分组，无效索引返回空矩形。/ Full-width rows; callers hit only the visible group. Invalid indices return empty.
EpdRect ui_reader_settings_rect(unsigned system_font_size, int index);
/// 只画开关，不修改状态，供字体和按键面板复用。/ Paint a switch without state changes, shared by font and key sheets.
void ui_reader_settings_switch(uint8_t *fb, EpdRect track, bool on);
/// 绘制整行控件；调用方先画面板壳与左上返回，不推屏、不读取或修改设置。/ Paint full-width controls after the shell and top-left Back; no presentation or settings access.
void ui_reader_settings_draw(uint8_t *fb, const ui_reader_settings_values_t *values);
/// 绘制翻页示意与晃动开关；调用方先画面板壳和左上返回。/ Paint turn diagrams and the shake toggle after the shell and top-left Back.
EpdRect ui_reader_turn_mode_rect(unsigned mode);
EpdRect ui_reader_turn_shake_rect(void);
void ui_reader_turn_settings_draw(uint8_t *fb, unsigned mode, bool shake);
