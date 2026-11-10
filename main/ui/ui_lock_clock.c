/* SPDX-License-Identifier: Apache-2.0
 * 中文：确认稿的数字位图与固件电量，不读时间、不切字体。/ Approved digit masks and firmware battery; no clock IO or font switching.
 */
#include "ui_lock_clock.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include "ttf_font.h"
#include "assets/clock_digits.h"
#include <stdio.h>

EpdRect ui_lock_clock_area(void) { return (EpdRect){160,288,384,672}; }
static void digit_draw(uint8_t *fb, unsigned digit, int column, int baseline) {
    const clock_digit_t *glyph = &clock_digits[digit];
    const int left = (684 - (CLOCK_DIGIT_CELL * 2 + CLOCK_DIGIT_TRACKING)) / 2;
    const int x = left + column * (CLOCK_DIGIT_CELL + CLOCK_DIGIT_TRACKING) +
                  (CLOCK_DIGIT_CELL - glyph->width) / 2;
    const int top = baseline + glyph->top;
    unsigned pixel = 0;
    for (unsigned i = 0; i < glyph->length; ++i) {
        const uint8_t run = clock_digit_runs[glyph->offset + i];
        for (unsigned j = 0; j < (unsigned)(run >> 4) + 1; ++j, ++pixel)
            epd_draw_pixel(x + (int)(pixel % glyph->width), top + (int)(pixel / glyph->width),
                           (uint8_t)((run & 15) << 4), fb);
    }
}
static void centered(uint8_t *fb, int baseline, int em, const char *text, int tracking) {
    unsigned count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if ((*p & 0xc0) != 0x80) ++count;
    int px = ttf_em_height_px(em), width;
    do {
        width = ttf_text_width_px(px,text) + ((int)count - 1) * tracking;
        if (width <= 360) break;
    } while (--px > 0);
    if (px <= 0) return;
    ttf_draw_text_px_spaced(fb,(684-width)/2,baseline,px,text,tracking,0,15);
}
void ui_lock_clock_draw(uint8_t *fb, const ui_lock_clock_face_t *face) {
    if (!fb || !face) return;
    ui_fill_rect_fast(ui_lock_clock_area(),UI_GRAY_WHITE,fb);
    if (face->valid) {
        digit_draw(fb,(unsigned)face->hour/10,0,480);
        digit_draw(fb,(unsigned)face->hour%10,1,480);
        digit_draw(fb,(unsigned)face->minute/10,0,688);
        digit_draw(fb,(unsigned)face->minute%10,1,688);
        char date[32];snprintf(date,sizeof(date),"%02d 月 %02d 日",face->month,face->day);
        static const char *const weekdays[] = {"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
        centered(fb,800,30,date,3);
        centered(fb,857,26,weekdays[face->weekday%7],4);
    } else {
        for (int row = 0; row < 2; ++row) for (int column = 0; column < 2; ++column)
            ui_fill_rect_fast((EpdRect){222+column*151,392+row*208,88,20},UI_GRAY_BLACK,fb);
        centered(fb,800,26,"请先校准时间",2);
    }
    ui_nav_battery_draw(fb,322,914);
}
