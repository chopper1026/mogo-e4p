/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从 app_book 提取的阅读设置绘制与共用几何；调用方持有设置和页面状态。
 * English: Reader-setting painting and shared geometry extracted from app_book; callers own settings and page state.
 */
#include "ui_reader_settings.h"
#include "ui_kit.h"
#include <stdio.h>

static int settings_extra(unsigned scale, int maximum) {
    if (scale < 100) scale = 100;
    if (scale > 200) scale = 200;
    return (int)(scale - 100) * maximum / 100;
}

int ui_reader_settings_top(unsigned system_font_size) {
    return 412 - settings_extra(system_font_size, 140);
}

ui_reader_settings_group_t ui_reader_settings_group(int index) {
    static const ui_reader_settings_group_t groups[] = {
        UI_READER_SETTINGS_REFRESH, UI_READER_SETTINGS_REFRESH,
        UI_READER_SETTINGS_INPUT, UI_READER_SETTINGS_INPUT,
        UI_READER_SETTINGS_PAGE, UI_READER_SETTINGS_PAGE,
        UI_READER_SETTINGS_PAGE, UI_READER_SETTINGS_PAGE,
        UI_READER_SETTINGS_PAGE, UI_READER_SETTINGS_INPUT,
    };
    return index >= 0 && index < 10 ? groups[index] : UI_READER_SETTINGS_GROUP_COUNT;
}

EpdRect ui_reader_settings_tab_rect(unsigned system_font_size, int index) {
    if (index < 0 || index >= UI_READER_SETTINGS_GROUP_COUNT) return (EpdRect){0, 0, 0, 0};
    return (EpdRect){36 + index * 208, ui_reader_settings_top(system_font_size) + 108, 196, 64};
}

EpdRect ui_reader_settings_rect(unsigned system_font_size, int index) {
    static const int rows[] = {0, 1, 0, 1, 0, 3, 2, 1, 4, 2};
    if (index < 0 || index >= 10) return (EpdRect){0, 0, 0, 0};
    int extra = settings_extra(system_font_size, 28);
    return (EpdRect){36, ui_reader_settings_top(system_font_size) + 196 + rows[index] * (120 + extra),
                     612, 112 + extra};
}

void ui_reader_settings_switch(uint8_t *fb, EpdRect track, bool on) {
    ui_fill_round_rect(fb, track, 19, on ? 0x38 : 0xc4);
    int cx = track.x + (on ? 49 : 20);
    epd_fill_circle(cx, track.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, track.y + 19, 16, UI_GRAY_BLACK, fb);
}

static void draw_reading_card(uint8_t *fb, unsigned scale, int index, const char *title,
                              const char *value, bool cycle) {
    EpdRect card = ui_reader_settings_rect(scale, index);
    ui_text_box(fb, (EpdRect){card.x+20, card.y+8, 490, card.height/2-8}, 27, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){card.x+20, card.y+card.height/2, 490, card.height/2-8}, 23, value, EPD_DRAW_ALIGN_LEFT, false);
    ui_draw_icon(fb, card.x+card.width-38, card.y+card.height/2, 30,
                 cycle ? UI_ICON_ROTATE_CW : UI_ICON_CHEVRON_RIGHT, 0x38);
    ui_hairline(fb, card.y+card.height-1, card.x+20, card.width-40, 0xa0);
}

static void draw_reading_toggle(uint8_t *fb, unsigned scale, int index, const char *title,
                                const char *detail, bool on) {
    EpdRect row = ui_reader_settings_rect(scale, index);
    ui_text_box(fb, (EpdRect){row.x+20, row.y+8, row.width-134, row.height/2-8}, 27, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){row.x+20, row.y+row.height/2, row.width-134, row.height/2-8}, 21,
                detail ? detail : on ? "开启" : "关闭", EPD_DRAW_ALIGN_LEFT, false);
    ui_reader_settings_switch(fb, (EpdRect){row.x+row.width-90, row.y+row.height/2-19, 69, 39}, on);
    ui_hairline(fb, row.y+row.height-1, row.x+20, row.width-40, 0xa0);
}

// 这两个面板的居中文字沿用页面的原有留白与行高。/ Keep the page's original margins and line height for centered labels in these sheets.
static void draw_centered_label(uint8_t *fb, int x, int cy, int px, const char *text, bool inverted) {
    int width = 2 * (x < 342 ? x - 42 : 642 - x);
    ui_text_box(fb, (EpdRect){x - width / 2, cy - 22, width, 44}, px, text, EPD_DRAW_ALIGN_CENTER, inverted);
}

void ui_reader_settings_draw(uint8_t *fb, const ui_reader_settings_values_t *values) {
    static const char *const tabs[] = {"操作", "页面", "刷新"};
    unsigned scale = values->system_font_size;
    for (int i = 0; i < UI_READER_SETTINGS_GROUP_COUNT; ++i) {
        EpdRect tab = ui_reader_settings_tab_rect(scale, i);
        bool selected = values->group == (ui_reader_settings_group_t)i;
        ui_fill_round_rect(fb, tab, 16, selected ? UI_GRAY_BLACK : 0xec);
        ui_text_box(fb, (EpdRect){tab.x+12, tab.y+6, tab.width-24, tab.height-12}, 28, tabs[i], EPD_DRAW_ALIGN_CENTER, selected);
    }
    if (values->group == UI_READER_SETTINGS_INPUT) {
        draw_reading_card(fb, scale, 2, "翻页方式", values->vertical_turn ? "上下点按 · 左右滑动" : "左右点按 · 左右滑动", false);
        draw_reading_card(fb, scale, 3, "按键控制", "短按、长按与双击动作", false);
        draw_reading_toggle(fb, scale, 9, "关闭触摸", "先在按键控制绑定触摸开关", values->touch_disabled);
    } else if (values->group == UI_READER_SETTINGS_PAGE) {
        draw_reading_toggle(fb, scale, 4, "状态栏", "阅读时显示时间与电量", values->statusbar);
        draw_reading_card(fb, scale, 7, "页眉页脚", "显示内容、栏高与进度条", false);
        draw_reading_toggle(fb, scale, 6, "竖排文字", "从右到左排列正文", values->vertical_text);
        draw_reading_toggle(fb, scale, 5, "隐藏插图", "跳过正文图片，保留原书文件", values->hide_images);
        draw_reading_toggle(fb, scale, 8, "书内样式", "关闭后使用统一排版", values->book_styles);
    } else if (values->group == UI_READER_SETTINGS_REFRESH) {
        char interval[24];
        if (values->full_pages) snprintf(interval, sizeof(interval), "每 %u 页", values->full_pages);
        else snprintf(interval, sizeof(interval), "%s", "不强制");
        draw_reading_card(fb, scale, 0, "自动全刷", interval, true);
        draw_reading_card(fb, scale, 1, "翻页效果", values->turn_effect ? "水波纹" : "普通", true);
    }
}

void ui_reader_turn_settings_draw(uint8_t *fb, bool vertical, bool shake) {
    for (int i = 0; i < 2; ++i) {
        int y = 370 + i * 322;
        bool selected = vertical == (i != 0);
        EpdRect card = {36, y, 612, 292};
        ui_fill_round_rect(fb, card, 22, selected ? 0xe0 : 0xf0);
        ui_draw_control_frame(fb, card, 22, 0x50);
        ui_draw_control_frame(fb, (EpdRect){37, y + 1, 610, 290}, 21, 0x50);
        EpdRect diagram = {64, y + 37, 182, 220};
        ui_fill_round_rect(fb, diagram, 12, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, diagram, 12, 0x58);
        if (i) {
            ui_hairline(fb, diagram.y + 73, 65, 180, 0x58);
            draw_centered_label(fb, 155, diagram.y + 36, 21, "上一页", false);
            draw_centered_label(fb, 155, diagram.y + 147, 21, "下一页", false);
        } else {
            ui_fill_rect_fast((EpdRect){155, diagram.y + 1, 1, 218}, UI_GRAY_BLACK, fb);
            ui_text_box(fb,(EpdRect){110-39,diagram.y+94-15,78,30},21,"上一",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){110-39,diagram.y+126-15,78,30},21,"页",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){201-39,diagram.y+94-15,78,30},21,"下一",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){201-39,diagram.y+126-15,78,30},21,"页",EPD_DRAW_ALIGN_CENTER,false);
        }
        ui_text_box(fb,(EpdRect){274,y+43,350,44},32,i ? "上下翻页" : "左右翻页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+112,350,44},23,i ? "上 1/3：上一页" : "左 1/2：上一页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+158,350,44},23,i ? "下 2/3：下一页" : "右 1/2：下一页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+213,350,44},21,"左右滑动均可翻页",EPD_DRAW_ALIGN_LEFT,false);
        epd_draw_circle(610, y + 61, 12, UI_GRAY_BLACK, fb);
        if (selected) epd_fill_circle(610, y + 61, 7, UI_GRAY_BLACK, fb);
    }
    // 晃动翻页整行开关：标题、方向说明与右端开关；全屏提示移到行下方。
    // A full-width shake-turn toggle row: title, direction hint and a right-hand switch; the fullscreen hint moves below it.
    EpdRect row = {36, 1000, 612, 88};
    ui_fill_round_rect(fb, row, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, row, 20, 0x68);
    ui_text_box(fb,(EpdRect){row.x+20,row.y+10,row.width-40,row.height/2-14},21,"晃动翻页",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){row.x+20,row.y+row.height/2+2,row.width-40,row.height/2-12},17,"左晃上一页，右晃下一页",EPD_DRAW_ALIGN_LEFT,false);
    ui_reader_settings_switch(fb,(EpdRect){row.x+row.width-90,row.y+row.height/2-19,69,39},shake);
    draw_centered_label(fb, 342, 1110, 21, "工具栏「更多」中可切换全屏", false);
}
