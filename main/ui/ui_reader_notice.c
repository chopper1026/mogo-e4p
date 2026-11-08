/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：白底阅读提示卡，标题、说明和边框共用固定布局。
 * English: White reader notices with shared geometry for headings, details and frames.
 */
#include "ui_reader_notice.h"
#include "ui_kit.h"
#include <string.h>

static unsigned notice_scale(unsigned scale) {
    return scale < 100 ? 100 : scale > 200 ? 200 : scale;
}

static int notice_extra(unsigned scale, int maximum) {
    return (int)(notice_scale(scale) - 100) * maximum / 100;
}

static int notice_lines(const char *message) {
    if (!message || !*message) return 0;
    int count = 1;
    for (; *message && count < 3; ++message)
        if (*message == '\n' && message[1]) ++count;
    return count;
}

EpdRect ui_reader_notice_area(const char *message, unsigned system_font_size) {
    int lines = notice_lines(message);
    if (!lines) return (EpdRect){0, 0, 0, 0};
    int height = 164 + notice_extra(system_font_size, 50);
    if (lines > 1) height += 30 + (lines - 1) * (40 + notice_extra(system_font_size, 20)) + (lines - 2) * 6;
    return (EpdRect){44, 136, 596, height + 8};
}

void ui_reader_notice_draw(uint8_t *fb, const char *message, unsigned system_font_size) {
    int lines = notice_lines(message);
    if (!fb || !lines) return;
    unsigned scale = notice_scale(system_font_size);
    EpdRect area = ui_reader_notice_area(message, scale);
    EpdRect card = {area.x + 4, area.y + 4, area.width - 8, area.height - 8};
    ui_text_set_system_scale(false);
    ui_fill_round_rect(fb, card, 16, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, card, 16, UI_GRAY_BLACK);
    ui_draw_control_frame(fb, (EpdRect){card.x + 6, card.y + 6, card.width - 12, card.height - 12}, 10, UI_GRAY_BLACK);

    int badge_height = 36 + notice_extra(scale, 20);
    EpdRect badge = {card.x + 28, card.y + 24, 140 + notice_extra(scale, 80), badge_height};
    ui_fill_round_rect(fb, badge, 6, UI_GRAY_BLACK);
    ui_text_box(fb, (EpdRect){badge.x + 10, badge.y + 2, badge.width - 20, badge.height - 4},
                22 * (int)scale / 100, "阅读提示", EPD_DRAW_ALIGN_CENTER, true);
    ui_draw_icon(fb, card.x + card.width - 52, badge.y + badge.height / 2, 36, UI_ICON_BOOK_OPEN, UI_GRAY_BLACK);

    int title_y = badge.y + badge.height + 16;
    int title_height = 64 + notice_extra(scale, 30);
    int detail_height = 40 + notice_extra(scale, 20);
    const char *at = message;
    for (int i = 0; i < lines; ++i) {
        const char *end = strchr(at, '\n');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        char line[128];
        if (length >= sizeof(line)) length = sizeof(line) - 1;
        memcpy(line, at, length); line[length] = 0;
        EpdRect box = {card.x + 32, title_y, card.width - 64, title_height};
        if (i) box = (EpdRect){card.x + 32, title_y + title_height + 30 + (i - 1) * (detail_height + 6), card.width - 64, detail_height};
        ui_text_box(fb, box, (i ? 28 : 42) * (int)scale / 100, line, EPD_DRAW_ALIGN_CENTER, false);
        if (!end) break;
        at = end + 1;
    }
    if (lines > 1) ui_draw_separator(fb, title_y + title_height + 14, card.x + 32, card.width - 64, UI_GRAY_BLACK);
}
