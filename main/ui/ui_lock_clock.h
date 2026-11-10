/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
typedef struct {
    bool valid;
    int hour, minute, month, day, weekday;
} ui_lock_clock_face_t;
EpdRect ui_lock_clock_area(void);
/// 纯绘制；两列数字固定，日期不含年份，电量在星期下方。/ Paint fixed digit columns, month/day, weekday, then battery.
void ui_lock_clock_draw(uint8_t *fb, const ui_lock_clock_face_t *face);
