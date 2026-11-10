/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：扇页书匣的纯绘制与共用命中几何；不分配内存、不访问书源。
 * English: Pure fan-deck drawing and shared hit geometry, without allocation or source IO. */
#pragma once
#include "epdiy.h"
#include "home_library.h"

typedef struct {
    unsigned from, progress, spread; ///< 进度及展开量0..256。/ Progress and spread in 0..256.
    bool motion; ///< 动态帧使用黑白，静止帧保留封面灰阶。/ Motion uses monochrome; settled art retains gray.
} ui_mini_library_frame_t;
typedef enum { UI_MINI_NONE, UI_MINI_SELECT, UI_MINI_OPEN, UI_MINI_RANDOM, UI_MINI_RECENT, UI_MINI_SHUFFLE } ui_mini_action_t;
typedef struct { ui_mini_action_t action; unsigned index; } ui_mini_hit_t;
void ui_mini_library_draw(uint8_t *fb, EpdRect region, const home_library_view_t *view, ui_mini_library_frame_t frame);
ui_mini_hit_t ui_mini_library_hit(EpdRect region, const home_library_view_t *view, int x, int y);
