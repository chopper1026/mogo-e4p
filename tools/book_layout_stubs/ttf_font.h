/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主布局测试的字体契约。/ English: Font contract for host layout tests.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include "epdiy.h"
#include <stdbool.h>
#include <stddef.h>
#define TTF_FONT_SLOTS 4
#define TTF_FONT_SLOT_SYSTEM 0
/// 绘制期的字体切换点：从 offset 起改用 slot，直到下一个切换点。
/// A face switch while drawing: bytes from `offset` use `slot` until the next switch.
typedef struct {
    uint32_t offset;
    uint8_t slot;
} ttf_run_t;
void ttf_draw_set_runs(const ttf_run_t* runs, size_t count);
int ttf_font_select(int slot);
int ttf_font_selected(void);
bool ttf_font_slot_ready(int slot);
int ttf_text_width_px(int px, const char* text);
int ttf_text_left_bearing_px(int px, const char* text);
int ttf_ascender_px(int px);
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text, enum EpdFontFlags align, uint8_t fg, uint8_t bg);
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, uint8_t fg, uint8_t bg);
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, int target_width, uint8_t fg, uint8_t bg);
int ttf_draw_text_px_rotated(uint8_t* fb, int x, int y, int px, const char* text,
                             uint8_t fg, uint8_t bg);
int ttf_draw_text_px_vertical(uint8_t* fb, int x, int y, int px, const char* text,
                              int tracking_px, uint8_t fg, uint8_t bg);
int ttf_vertical_text_height_px(int px, const char* text, int tracking_px);

typedef struct {
    size_t start, end; ///< 输入 UTF-8 字节区间 / Input UTF-8 byte span
    uint32_t codepoint; ///< 原始码点 / Original codepoint
    int x, y, width, height; ///< 推进字格 / Pen advance cell
} ttf_glyph_position_t;
typedef void (*ttf_glyph_observer_fn)(void *context, const ttf_glyph_position_t *position);
typedef struct { ttf_glyph_observer_fn record; void *context; } ttf_glyph_observer_t;

/// 可选逐字观察；空观察器保持原像素与推进。/ Optional observation with unchanged pixels and advances.
void ttf_draw_text_px_observed(uint8_t *fb, int x, int y, int px, const char *text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg, const ttf_glyph_observer_t *observer);
void ttf_draw_text_px_spaced_observed(uint8_t *fb, int x, int y, int px, const char *text,
    int tracking, uint8_t fg, uint8_t bg, const ttf_glyph_observer_t *observer);
void ttf_draw_text_px_fitted_observed(uint8_t *fb, int x, int y, int px, const char *text,
    int tracking, int width, uint8_t fg, uint8_t bg, const ttf_glyph_observer_t *observer);
int ttf_draw_text_px_vertical_observed(uint8_t *fb, int x, int y, int px, const char *text,
    int tracking, uint8_t fg, uint8_t bg, const ttf_glyph_observer_t *observer);
