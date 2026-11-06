/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：有界的页内 UTF-8 选区，不持有章节或绘图资源。
 * English: Bounded page-local UTF-8 selection without chapter or drawing ownership. */
#pragma once
#include "book_layout.h"
#define READER_SELECTION_BYTES 128u
#define READER_SELECTION_SCALARS 32u

typedef struct {
    bool active, dragging;
    size_t anchor, focus;
    uint32_t generation;
    size_t page;
    char query[READER_SELECTION_BYTES + 1];
} reader_selection_t;
/// 单字起选、拖动扩选；所有指针仅本次调用借用。/ Start or extend selection; pointers are borrowed only for the call.
bool reader_selection_begin(reader_selection_t *selection, const char *text, size_t length,
    const book_layout_glyph_t *glyphs, size_t count, size_t page, uint32_t generation, int x, int y);
bool reader_selection_move(reader_selection_t *selection, const char *text, size_t length,
    const book_layout_glyph_t *glyphs, size_t count, int x, int y);
bool reader_selection_range(const reader_selection_t *selection, const book_layout_glyph_t *glyphs,
    size_t count, uint32_t *start, uint32_t *end);
void reader_selection_cancel(reader_selection_t *selection);
/// 严格 UTF-8 检查并计数；错误返回 0。/ Strict UTF-8 validation and scalar count; zero on failure.
size_t reader_selection_utf8(const char *text, size_t bytes);
/// 逐字查询保留组合符，返回下一个字节位置；末尾返回 0。/ Per-character lookup keeps combining marks and returns the next byte offset, or zero at the end.
size_t reader_selection_character(const char *text, size_t offset, char output[READER_SELECTION_BYTES + 1]);
