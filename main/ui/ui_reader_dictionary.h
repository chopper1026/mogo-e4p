/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：临时选区与白底释义卡片，只绘制和命中测试。
 * English: Temporary selection and white definition sheet, drawing and hit testing only. */
#pragma once
#include "../book/reader_selection.h"
#include "../book/book_dictionary.h"
typedef struct {
    bool open, loading, can_character;
    int top;
    book_dictionary_kind_t kind;
    size_t offset;
    size_t next_offset; ///< 没有下一页时为 0。/ Zero when no next page is available.
} ui_reader_dictionary_t;
/// XOR 选区可重复调用擦除，允许动态背景不重排。/ Repeated XOR erases the selection without reflowing dynamic backgrounds.
EpdRect ui_reader_dictionary_selection(uint8_t *fb, const reader_selection_t *selection,
    const book_layout_glyph_t *glyphs, size_t count);
void ui_reader_dictionary_draw(uint8_t *fb, const ui_reader_dictionary_t *view,
    const book_dictionary_result_t *result, const char *query);
typedef enum {
    UI_DICTIONARY_NONE = -1, UI_DICTIONARY_CLOSE, UI_DICTIONARY_PREV, UI_DICTIONARY_NEXT,
    UI_DICTIONARY_CHARACTER, UI_DICTIONARY_EN_ZH, UI_DICTIONARY_EN_EN
} ui_dictionary_action_t;
/// 英文切换不受查询状态限制。/ Mode selection remains available regardless of lookup status.
ui_dictionary_action_t ui_reader_dictionary_action(const ui_reader_dictionary_t *view, int x, int y);
/// 返回下一页起点；没有剩余释义时为 0。/ Return the next page start, or zero if nothing remains.
size_t ui_reader_dictionary_next_page(const book_dictionary_result_t *result, size_t offset);
size_t ui_reader_dictionary_content_size(const book_dictionary_result_t *result);
/// 页内选字失败提示也使用阅读字体。/ Selection failures also use the reader face.
void ui_reader_dictionary_notice(uint8_t *fb);
