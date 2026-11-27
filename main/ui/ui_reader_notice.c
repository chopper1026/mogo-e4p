/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：白底阅读提示卡，标题、说明和边框共用固定布局。
 * English: White reader notices with shared geometry for headings, details and frames.
 */
#include "ui_locale.h"
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
    message=ui_tr(message);
    int lines = notice_lines(message);
    if (!lines) return (EpdRect){0, 0, 0, 0};
    int height = 164 + notice_extra(system_font_size, 50);
    if (lines > 1) height += 30 + (lines - 1) * (40 + notice_extra(system_font_size, 20)) + (lines - 2) * 6;
    return (EpdRect){(epd_rotated_display_width() - 596) / 2, 136, 596, height + 8};
}

void ui_reader_notice_draw(uint8_t *fb, const char *message, unsigned system_font_size) {
    message=ui_tr(message);
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

ui_reader_notice_confirm_layout_t ui_reader_notice_confirm_layout(const char *message, unsigned system_font_size) {
    ui_reader_notice_confirm_layout_t layout = {0};
    message = ui_tr(message);
    int lines = notice_lines(message);
    if (!lines) return layout;
    unsigned scale = notice_scale(system_font_size);
    int badge_height = 36 + notice_extra(scale, 16);
    int title_height = 56 + notice_extra(scale, 24);
    int detail_height = 38 + notice_extra(scale, 18);
    int check_height = 56 + notice_extra(scale, 12);
    int button_height = 72 + notice_extra(scale, 12);
    int text_height = title_height;
    if (lines > 1) text_height += 20 + (lines - 1) * detail_height + (lines - 2) * 6;
    int card_height = 24 + badge_height + 14 + text_height + 16 + check_height + 14 + button_height + 24;
    int area_height = card_height + 8;
    int area_y = (epd_rotated_display_height() - area_height) / 2;
    if (area_y < 8) area_y = 8;
    layout.area = (EpdRect){(epd_rotated_display_width() - 596) / 2, area_y, 596, area_height};
    EpdRect card = {layout.area.x + 4, layout.area.y + 4, layout.area.width - 8, layout.area.height - 8};
    int check_y = card.y + 24 + badge_height + 14 + text_height + 16;
    layout.checkbox = (EpdRect){card.x + 32, check_y, card.width - 64, check_height};
    layout.confirm = (EpdRect){card.x + 32, check_y + check_height + 14, card.width - 64, button_height};
    return layout;
}

void ui_reader_notice_confirm_draw(uint8_t *fb, const char *message, const char *option_label,
                                   const char *confirm_label, bool checked, unsigned system_font_size) {
    message = ui_tr(message);
    int lines = notice_lines(message);
    if (!fb || !lines) return;
    unsigned scale = notice_scale(system_font_size);
    ui_reader_notice_confirm_layout_t layout = ui_reader_notice_confirm_layout(message, scale);
    EpdRect card = {layout.area.x + 4, layout.area.y + 4, layout.area.width - 8, layout.area.height - 8};
    ui_text_set_system_scale(false);
    ui_fill_round_rect(fb, card, 16, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, card, 16, UI_GRAY_BLACK);
    ui_draw_control_frame(fb, (EpdRect){card.x + 6, card.y + 6, card.width - 12, card.height - 12}, 10, UI_GRAY_BLACK);

    int badge_height = 36 + notice_extra(scale, 16);
    EpdRect badge = {card.x + 28, card.y + 24, 140 + notice_extra(scale, 80), badge_height};
    ui_fill_round_rect(fb, badge, 6, UI_GRAY_BLACK);
    ui_text_box(fb, (EpdRect){badge.x + 10, badge.y + 2, badge.width - 20, badge.height - 4},
                22 * (int)scale / 100, "阅读提示", EPD_DRAW_ALIGN_CENTER, true);
    ui_draw_icon(fb, card.x + card.width - 52, badge.y + badge.height / 2, 36, UI_ICON_BOOK_OPEN, UI_GRAY_BLACK);

    int title_y = badge.y + badge.height + 14;
    int title_height = 56 + notice_extra(scale, 24);
    int detail_height = 38 + notice_extra(scale, 18);
    const char *at = message;
    for (int i = 0; i < lines; ++i) {
        const char *end = strchr(at, '\n');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        char line[128];
        if (length >= sizeof(line)) length = sizeof(line) - 1;
        memcpy(line, at, length); line[length] = 0;
        EpdRect box = {card.x + 32, title_y, card.width - 64, title_height};
        if (i) box = (EpdRect){card.x + 32, title_y + title_height + 20 + (i - 1) * (detail_height + 6), card.width - 64, detail_height};
        ui_text_box(fb, box, (i ? 28 : 40) * (int)scale / 100, line, EPD_DRAW_ALIGN_CENTER, false);
        if (!end) break;
        at = end + 1;
    }
    if (lines > 1) ui_draw_separator(fb, title_y + title_height + 10, card.x + 32, card.width - 64, UI_GRAY_BLACK);

    // 勾选框与标签居中排布，整行作为点击区域便于触控。
    // Center the checkbox and label together while keeping the whole row tappable.
    const char *opt = option_label ? option_label : "今后不再提示";
    int box_size = 32 + notice_extra(scale, 8);
    int max_text_w = layout.checkbox.width - box_size - 28;
    int opt_px = ui_text_fit_px(26 * (int)scale / 100, max_text_w, layout.checkbox.height - 8, opt);
    int text_w = ui_text_fixed_width_px(opt_px, opt);
    if (text_w > max_text_w) text_w = max_text_w;
    int group_w = box_size + 14 + text_w;
    int group_x = layout.checkbox.x + (layout.checkbox.width - group_w) / 2;
    EpdRect check_box = {group_x, layout.checkbox.y + (layout.checkbox.height - box_size) / 2, box_size, box_size};
    if (checked) {
        ui_fill_round_rect(fb, check_box, 6, UI_GRAY_BLACK);
        ui_draw_icon(fb, check_box.x + check_box.width / 2, check_box.y + check_box.height / 2,
                     box_size - 8, UI_ICON_CHECK, UI_GRAY_WHITE);
    } else {
        ui_fill_round_rect(fb, check_box, 6, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, check_box, 6, UI_GRAY_BLACK);
    }
    ui_text_fixed_vc(fb, check_box.x + box_size + 14, layout.checkbox.y + layout.checkbox.height / 2,
                     opt_px, opt, EPD_DRAW_ALIGN_LEFT, false);

    ui_fill_round_rect(fb, layout.confirm, 14, UI_GRAY_BLACK);
    ui_text_box(fb, (EpdRect){layout.confirm.x + 16, layout.confirm.y + 4, layout.confirm.width - 32, layout.confirm.height - 8},
                30 * (int)scale / 100, confirm_label ? confirm_label : "确认", EPD_DRAW_ALIGN_CENTER, true);
}
