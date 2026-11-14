/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_reader_tap_grid.h"
#include "ui_kit.h"
#include <stdio.h>

EpdRect ui_reader_tap_cancel_rect(void) { return (EpdRect){20, 18, 108, 64}; }
EpdRect ui_reader_tap_save_rect(void) { return (EpdRect){556, 18, 108, 64}; }
EpdRect ui_reader_tap_picker_back_rect(void) { return (EpdRect){36, 522, 112, 68}; }
EpdRect ui_reader_tap_option_rect(unsigned option) {
    return option < UI_READER_TAP_ACTION_COUNT ? (EpdRect){36+(int)(option%2)*312, 646+(int)(option/2)*126, 300, 108} : (EpdRect){0};
}

void ui_reader_tap_grid_draw(uint8_t *fb, const EpdRect cells[9], const char *const labels[9], int selected, const char *error) {
    // 白色半透明罩保留真实正文；只改绘图缓存，不推屏或持久化。/ A translucent white veil retains real text, touching only the drawing buffer.
    int width=epd_rotated_display_width(), height=epd_rotated_display_height();
    uint8_t veil[16];
    for (unsigned gray = 0; gray < 16; ++gray) veil[gray] = (uint8_t)((15*80+gray*20+50)/100);
    size_t bytes = ((size_t)width * height + 1) / 2;
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t pixel = fb[i];
        fb[i] = (uint8_t)((veil[pixel >> 4] << 4) | veil[pixel & 15]);
    }
    for(unsigned i=0;i<9;++i){
        EpdRect r=cells[i];
        if(r.width<12||r.height<12)continue;
        bool active=(int)i==selected;
        ui_draw_control_frame(fb,r,0,active?UI_GRAY_BLACK:0x60);
        if(active)ui_draw_control_frame(fb,(EpdRect){r.x+2,r.y+2,r.width-4,r.height-4},0,UI_GRAY_BLACK);
        char number[4];snprintf(number,sizeof(number),"%u",i+1);
        ui_text_box(fb,(EpdRect){r.x+12,r.y+12<112?112:r.y+12,34,36},20,number,EPD_DRAW_ALIGN_CENTER,false);
        EpdRect label={r.x+12,r.y+r.height/2-34,r.width-24,68};
        ui_text_box(fb,label,28,labels[i],EPD_DRAW_ALIGN_CENTER,false);
    }
    ui_fill_rect_fast((EpdRect){0,0,width,106},UI_GRAY_WHITE,fb);
    ui_text_box(fb,(EpdRect){140,14,404,48},30,"九宫格翻页",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){140,64,404,32},18,error&&error[0]?error:"点选区域，设置它的功能",EPD_DRAW_ALIGN_CENTER,false);
    EpdRect cancel=ui_reader_tap_cancel_rect(),save=ui_reader_tap_save_rect();
    ui_text_box(fb,cancel,25,"取消",EPD_DRAW_ALIGN_CENTER,false);
    ui_fill_round_rect(fb,save,16,UI_GRAY_BLACK);
    ui_text_box(fb,save,25,"保存",EPD_DRAW_ALIGN_CENTER,true);
}

void ui_reader_tap_picker_draw(uint8_t *fb, unsigned cell, const char *const options[UI_READER_TAP_ACTION_COUNT], int selected) {
    ui_fill_round_rect(fb,(EpdRect){12,494,660,722},26,UI_GRAY_WHITE);
    ui_draw_control_frame(fb,(EpdRect){12,494,660,722},26,UI_GRAY_BLACK);
    EpdRect back=ui_reader_tap_picker_back_rect();
    ui_draw_icon(fb,back.x+30,back.y+34,32,UI_ICON_CHEVRON_LEFT,UI_GRAY_BLACK);
    char title[48];snprintf(title,sizeof(title),"区域 %u 的功能",cell+1);
    ui_text_box(fb,(EpdRect){162,522,462,64},30,title,EPD_DRAW_ALIGN_LEFT,false);
    for(unsigned i=0;i<UI_READER_TAP_ACTION_COUNT;++i){
        EpdRect r=ui_reader_tap_option_rect(i);bool active=(int)i==selected;
        ui_fill_round_rect(fb,r,18,active?UI_GRAY_BLACK:0xf0);
        ui_draw_control_frame(fb,r,18,active?UI_GRAY_BLACK:0x80);
        ui_text_box(fb,(EpdRect){r.x+16,r.y+12,r.width-32,r.height-24},28,options[i],EPD_DRAW_ALIGN_CENTER,active);
    }
}
