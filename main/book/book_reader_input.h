/* SPDX-License-Identifier: Apache-2.0
 * 中文：正文点按分区与九宫格几何。用户2026-11-10新增九格自定义动作；点按在抬起时直接提交。
 * English: Body tap regions and grid geometry. The 2026-11-10 user revision adds nine custom actions, committed on release.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"

static inline EpdRect book_reader_grid_cell(EpdRect body, unsigned cell) {
    if (cell >= 9 || body.width <= 0 || body.height <= 0) return (EpdRect){0};
    int col = (int)(cell % 3), row = (int)(cell / 3);
    int x = body.width * col / 3, y = body.height * row / 3;
    return (EpdRect){body.x + x, body.y + y,
        body.width * (col + 1) / 3 - x, body.height * (row + 1) / 3 - y};
}

static inline int book_reader_grid_hit(EpdRect body, int x, int y) {
    for (unsigned i = 0; i < 9; ++i) {
        EpdRect r = book_reader_grid_cell(body, i);
        if (r.width > 0 && r.height > 0 && x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
            return (int)i;
    }
    return -1;
}

/// rtl 表示竖排从右到左：横向分区的「上一页」在右半边，左右翻转。
/// rtl marks right-to-left vertical text: the previous-page half is the right one.
static inline int book_reader_tap_direction(EpdRect body, int x, int y, bool vertical, bool rtl) {
    if (x < body.x || x >= body.x + body.width || y < body.y || y >= body.y + body.height) return 0;
    if (vertical) return y < body.y + body.height / 3 ? -1 : 1;
    bool previous_half = rtl ? x >= body.x + body.width / 2 : x < body.x + body.width / 2;
    return previous_half ? -1 : 1;
}
