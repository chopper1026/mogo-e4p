/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：首页连续阅读题头；纯绘制七日书脊，不读取日期或写入统计。
 * English: Home streak header; paint seven daily spines without clock or statistics IO.
 */
#include "ui_reading_streak.h"
#include "ui_kit.h"
#include <stdio.h>

static void line2(uint8_t *f,int ax,int ay,int bx,int by,uint8_t ink){
    epd_draw_line(ax,ay,bx,by,ink,f);epd_draw_line(ax+1,ay,bx+1,by,ink,f);
}
static void dashed_spine(uint8_t *f,EpdRect r){
    const int dash=4,gap=3,weight=2;
    for(int x=r.x+2;x<r.x+r.width-2;x+=dash+gap){
        int n=r.x+r.width-2-x;if(n>dash)n=dash;
        epd_fill_rect((EpdRect){x,r.y,n,weight},144,f);
        epd_fill_rect((EpdRect){x,r.y+r.height-weight,n,weight},144,f);
    }
    for(int y=r.y+3;y<r.y+r.height;y+=dash+gap){
        int n=r.y+r.height-y;if(n>dash)n=dash;
        epd_fill_rect((EpdRect){r.x,y,weight,n},144,f);
        epd_fill_rect((EpdRect){r.x+r.width-weight,y,weight,n},144,f);
    }
    epd_draw_pixel(r.x+1,r.y+1,144,f);
    epd_draw_pixel(r.x+r.width-2,r.y+1,144,f);
}
static void lozenge(uint8_t *f,int cx,int cy,uint8_t ink){
    epd_draw_line(cx,cy-3,cx+3,cy,ink,f);
    epd_draw_line(cx+3,cy,cx,cy+3,ink,f);
    epd_draw_line(cx,cy+3,cx-3,cy,ink,f);
    epd_draw_line(cx-3,cy,cx,cy-3,ink,f);
}
static void spine(uint8_t *f,int x,int bottom,int w,int h,int index,unsigned minutes){
    EpdRect r={x,bottom-h,w,h};
    if(minutes<READING_STREAK_GOAL_MS){dashed_spine(f,r);return;}
    bool today=index==6;
    uint8_t ink=today?0:144,edge=today?64:112,accent=today?255:240;
    ui_fill_round_rect(f,r,3,ink);
    epd_fill_rect((EpdRect){x,bottom-3,w,3},ink,f);
    // 书背相接，装帧细节仅画在书脊内。/ Joined spine edges contain the binding details.
    epd_draw_vline(x,r.y+3,h-3,edge,f);
    epd_draw_vline(x+w-1,r.y+3,h-3,edge,f);
    epd_draw_hline(x+3,r.y+2,w-6,today?112:192,f);
    int cx=x+w/2,cy=r.y+h/2;
    epd_draw_hline(x+2,bottom-7,w-4,accent,f);
    epd_draw_hline(x+2,bottom-10,w-4,accent,f);
    if(today){
        // 白书签与勾标识今日达标。/ A white ribbon and check mark today as complete.
        epd_fill_rect((EpdRect){x+w-7,r.y+1,3,12},255,f);
        epd_draw_line(x+w-7,r.y+12,x+w-6,r.y+10,0,f);
        line2(f,cx-5,cy,cx-2,cy+3,255);
        line2(f,cx-2,cy+3,cx+4,cy-4,255);
    }else{
        epd_draw_hline(x+2,r.y+8,w-4,accent,f);
        epd_draw_hline(x+2,r.y+11,w-4,accent,f);
        if(index%3==0){
            lozenge(f,cx,cy,accent);
            epd_draw_pixel(cx,cy,accent,f);
        }else if(index%3==1){
            epd_draw_vline(cx-3,cy-6,13,accent,f);
            epd_draw_vline(cx+3,cy-6,13,accent,f);
            epd_draw_hline(cx-1,cy-8,3,accent,f);
            epd_draw_hline(cx-1,cy+8,3,accent,f);
            epd_draw_pixel(cx,cy,accent,f);
        }else{
            epd_draw_hline(cx-3,cy-6,7,accent,f);
            lozenge(f,cx,cy+1,accent);
            epd_draw_hline(cx-3,cy+8,7,accent,f);
        }
    }
}

EpdRect ui_reading_streak_area(void) { return (EpdRect){24,62,552,106}; }
static void fitted(uint8_t *fb,int x,int cy,int px,const char *value) {
    while (px>14 && ui_text_fixed_width_px(px,value)>576-x) --px;
    ui_text_fixed_vc(fb,x,cy,px,value,EPD_DRAW_ALIGN_LEFT,false);
}
void ui_reading_streak_draw(uint8_t *fb,const reading_streak_view_t *view) {
    unsigned days=view->days;
    bool read=view->valid && view->milliseconds[6]>=READING_STREAK_GOAL_MS;
    char number[12];snprintf(number,sizeof(number),"%u",days);
    int px=days>=100?62:days>=10?74:88;
    while (px>32 && ui_text_fixed_width_px(px,number)>144) --px;
    ui_text_fixed_vc(fb,36,117,px,number,EPD_DRAW_ALIGN_LEFT,false);
    int number_w=ui_text_fixed_width_px(px,number);
    int book_x=36+number_w+44,label_x=book_x+131+22;
    ui_text_fixed_vc(fb,36+number_w+6,135,21,"天",EPD_DRAW_ALIGN_LEFT,false);
    fitted(fb,label_x,104,25,"连续阅读");
    if (!view->valid) fitted(fb,label_x,136,20,"请先校准时间");
    else if (read) {
        line2(fb,label_x+1,137,label_x+5,141,0);line2(fb,label_x+5,141,label_x+12,131,0);
        fitted(fb,label_x+22,136,20,"今日已达标");
    } else {
        char progress[40];snprintf(progress,sizeof(progress),"今日 %u / 30 分钟",(unsigned)(view->milliseconds[6]/60000));
        fitted(fb,label_x,136,20,progress);
    }
    const int heights[]={44,49,54,59,64,69,74};
    const int widths[]={18,19,17,20,18,19,20};
    int x=book_x;
    for (int i=0;i<READING_STREAK_DAYS;++i) {
        spine(fb,x,144,widths[i],heights[i],i,view->valid?view->milliseconds[i]:0);
        char date[8];snprintf(date,sizeof(date),"%u",view->dates[i]);
        const char *shown=i==6?"今":view->valid?date:"-";
        ui_text_fixed_vc(fb,x+widths[i]/2,158,15,shown,EPD_DRAW_ALIGN_CENTER,false);
        x+=widths[i];
    }
    epd_draw_hline(book_x-4,144,139,0,fb);
}
