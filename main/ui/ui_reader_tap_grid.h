/* SPDX-License-Identifier: Apache-2.0
 * 中文：九宫格编辑器的纯绘制与控件几何，调用方持有正文分区、动作及草稿。
 * English: Pure grid-editor painting and control geometry; callers own body cells, actions and drafts.
 */
#pragma once
#include "epdiy.h"

#define UI_READER_TAP_ACTION_COUNT 7

EpdRect ui_reader_tap_cancel_rect(void);
EpdRect ui_reader_tap_save_rect(void);
EpdRect ui_reader_tap_option_rect(unsigned option);
EpdRect ui_reader_tap_picker_back_rect(void);
void ui_reader_tap_grid_draw(uint8_t *fb, const EpdRect cells[9], const char *const labels[9], int selected, const char *error);
void ui_reader_tap_picker_draw(uint8_t *fb, unsigned cell, const char *const options[UI_READER_TAP_ACTION_COUNT], int selected);
