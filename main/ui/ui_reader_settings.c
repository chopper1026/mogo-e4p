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
        draw_reading_card(fb, scale, 2, "翻页方式", values->grid_tap ? "九宫格 · 自定义点按" : values->vertical_turn ? "上下点按 · 左右滑动" : "左右点按 · 左右滑动", false);
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

EpdRect ui_reader_turn_mode_rect(unsigned mode) {
    return mode < 3 ? (EpdRect){36, 370 + (int)mode * 218, 612, 200} : (EpdRect){0};
}
EpdRect ui_reader_turn_shake_rect(void) { return (EpdRect){36, 1040, 612, 100}; }

void ui_reader_turn_settings_draw(uint8_t *fb, unsigned mode, bool shake) {
    static const char *const titles[] = {"左右翻页", "上下翻页", "九宫格翻页"};
    static const char *const details[] = {"左右各半，轻点翻页", "上 1/3，下 2/3", "九个区域，自定义动作"};
    for (unsigned i = 0; i < 3; ++i) {
        EpdRect card = ui_reader_turn_mode_rect(i);
        bool selected = mode == i;
        ui_fill_round_rect(fb, card, 20, selected ? 0xe8 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, card, 20, selected ? UI_GRAY_BLACK : 0x80);
        if (selected) ui_draw_control_frame(fb, (EpdRect){card.x+2,card.y+2,card.width-4,card.height-4},18,UI_GRAY_BLACK);
        EpdRect diagram = {60, card.y + 22, 128, 156};
        ui_fill_round_rect(fb, diagram, 10, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, diagram, 10, 0x60);
        if (i == 0) {
            ui_fill_rect_fast((EpdRect){diagram.x+64,diagram.y+1,1,154},0x60,fb);
            ui_draw_icon(fb,diagram.x+32,diagram.y+78,26,UI_ICON_CHEVRON_LEFT,UI_GRAY_BLACK);
            ui_draw_icon(fb,diagram.x+96,diagram.y+78,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
        } else if (i == 1) {
            ui_hairline(fb,diagram.y+52,diagram.x+1,126,0x60);
            ui_draw_icon(fb,diagram.x+64,diagram.y+27,24,UI_ICON_CHEVRON_UP,UI_GRAY_BLACK);
            ui_draw_icon(fb,diagram.x+64,diagram.y+104,26,UI_ICON_CHEVRON_DOWN,UI_GRAY_BLACK);
        } else {
            for(int k=1;k<3;++k){
                ui_fill_rect_fast((EpdRect){diagram.x+k*128/3,diagram.y+1,1,154},0x60,fb);
                ui_hairline(fb,diagram.y+k*52,diagram.x+1,126,0x60);
            }
            ui_fill_round_rect(fb,(EpdRect){diagram.x+45,diagram.y+55,38,46},6,UI_GRAY_BLACK);
            ui_text_box(fb,(EpdRect){diagram.x+46,diagram.y+56,36,44},24,"＋",EPD_DRAW_ALIGN_CENTER,true);
        }
        ui_text_box(fb,(EpdRect){216,card.y+36,340,54},30,titles[i],EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){216,card.y+105,370,48},22,details[i],EPD_DRAW_ALIGN_LEFT,false);
        epd_draw_circle(610,card.y+61,12,UI_GRAY_BLACK,fb);
        if(selected)epd_fill_circle(610,card.y+61,7,UI_GRAY_BLACK,fb);
        if(i==2)ui_draw_icon(fb,610,card.y+133,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
    }
    EpdRect row=ui_reader_turn_shake_rect();
    ui_text_box(fb,(EpdRect){row.x+20,row.y+4,440,44},26,"晃动翻页",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){row.x+20,row.y+50,440,38},20,"左晃上一页，右晃下一页",EPD_DRAW_ALIGN_LEFT,false);
    ui_reader_settings_switch(fb,(EpdRect){row.x+row.width-90,row.y+30,69,39},shake);
}
