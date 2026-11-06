/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：临时选区与白底释义卡片，只绘制和命中测试。
 * English: Temporary selection and white definition sheet, drawing and hit testing only. */
#pragma once
#include "../book/reader_selection.h"
#include "../book/book_dictionary.h"
typedef struct {
    bool open, loading;
    int top;
    size_t offset;
} ui_reader_dictionary_t;
/// XOR 选区可重复调用擦除，允许动态背景不重排。/ Repeated XOR erases the selection without reflowing dynamic backgrounds.
EpdRect ui_reader_dictionary_selection(uint8_t *fb, const reader_selection_t *selection,
    const book_layout_glyph_t *glyphs, size_t count);
void ui_reader_dictionary_draw(uint8_t *fb, const ui_reader_dictionary_t *view,
    const book_dictionary_result_t *result, const char *query);
/// 动作：0关闭、1上一屏、2下一屏、3逐字查询、-1空白。/ Actions: close, previous, next, per-character lookup, or blank.
int ui_reader_dictionary_action(const ui_reader_dictionary_t *view, int x, int y);
size_t ui_reader_dictionary_page_end(const book_dictionary_result_t *result, size_t offset);
size_t ui_reader_dictionary_content_size(const book_dictionary_result_t *result);
/// 页内选字失败提示也使用阅读字体。/ Selection failures also use the reader face.
void ui_reader_dictionary_notice(uint8_t *fb);
