#pragma once

#include "ui_kit.h"

enum { UI_NOTEPAD_ROWS = 10, UI_NOTEPAD_INK_STEPS = 3 };

/// 正常及多选复用十行，表头与内容使用同一组列坐标。 / Both modes use ten rows and share column anchors between the header and entries.
EpdRect ui_notepad_row(int row, bool selecting);
/// 绘制纸面与顶部卷边；多选时不占用操作栏下方的空间。 / Paint the paper and curl; omit the curl when space is reserved for selection controls.
void ui_notepad_paper(uint8_t *fb, bool selecting, bool fast);
/// 绘制书籍行及局部按压反馈；row=-1 绘制表头。 / Paint a book row and its press feedback; row=-1 paints the heading.
void ui_notepad_line(uint8_t *fb, int row, bool selecting, bool selected, bool pressed, bool fast,
                     const char *title, const char *author, unsigned progress);
/// 从已绘制的普通行读取实际书名字迹，含省略号，不含相邻列。 / Measure the title ink in the painted normal row, including its ellipsis but not neighbouring columns.
EpdRect ui_notepad_title_ink(const uint8_t *fb, int row);
/// 追加一段笔迹，返回该段的局部脏区；按顺序从零调用。 / Add one stroke segment and return its dirty rectangle; call in order from zero.
EpdRect ui_notepad_ink_step(uint8_t *fb, int row, EpdRect title_ink, unsigned step);
/// 设置页预览使用同一幅纸卷。 / The settings thumbnail uses the same curl artwork.
void ui_notepad_thumbnail(uint8_t *fb, EpdRect bounds);
