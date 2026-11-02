#!/usr/bin/env python3
"""中文：首页留白、旧入口移除与分钟刷新回归。
English: Regress Home blank space, retired routes and minute updates.
"""
from pathlib import Path
import os
import subprocess
import tempfile

from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
HOME = Path(os.environ.get("HOME_TEST_SOURCE", ROOT / "main/apps/app_dashboard.c"))
source = HOME.read_text()
for retired in ("HOME_COMPONENT", "HOME_GOAL", "HOME_TIME", "ui_home_clock",
                "ui_home_folder", "book_ticket", "home_goal_minutes", "home_component"):
    assert retired not in source, f"retired Home implementation remains: {retired}"

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define BOOK_EXCERPT_CAP 128
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define ESP_ERR_NOT_FINISHED 7
#define DISPLAY_REFRESH_DEFAULT 0
enum EpdDrawError {EPD_DRAW_SUCCESS, EPD_DRAW_FAILED};
enum {DISPLAY_REFRESH_DETAIL, DISPLAY_REFRESH_FEEDBACK};
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT};
typedef enum {APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL} app_redraw_t;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool touched;} touch_t;
typedef struct {bool consumed;touch_t *touch;uint8_t *fb;void *hl;} app_ctx_t;
enum {UI_GESTURE_PRESS, UI_GESTURE_MOVE, UI_GESTURE_TAP, UI_GESTURE_LONG_PRESS,
      UI_GESTURE_SWIPE_L, UI_GESTURE_SWIPE_R, UI_GESTURE_SWIPE_U};
typedef struct {int type,x0,y0,x,y;} ui_gesture_event_t;
typedef struct {char path[128],title[128],author[128];uint8_t *cover;
    bool has_progress;struct {unsigned pct;} progress;} home_book_t;
typedef struct {bool mounted;} board_sd_info_t;
typedef enum {UI_AVATAR_PENDING, UI_AVATAR_READY, UI_AVATAR_FAILED} ui_avatar_state_t;
static home_book_t s_current,s_recent[6];
static bool s_clock_pending,s_press_pending,s_home_force_full_once,s_scan_pending;
static bool s_avatar_pending,s_avatar_ready,s_cover_waiting;
static char s_avatar_path[288],s_excerpt[128];
static uint8_t s_avatar[3600];
static EpdRect s_pressed,s_area,last_area;
static int64_t s_clock_minute=-2,minute;
static unsigned s_cover_generation;
static int s_cover_next,cover_calls,scan_calls,nav=-1,open_calls;
static int draws,pages,fulls,last_intent,clock_paints;
static bool wait_cover,fail_open;
static enum EpdDrawError draw_result;
static uint8_t framebuffer[UI_LOCK_WIDTH*UI_LOCK_HEIGHT],cover_pixel;
static void pixel(uint8_t *fb,int x,int y,uint8_t gray){
    assert(x>=0&&x<UI_LOCK_WIDTH&&y>=0&&y<UI_LOCK_HEIGHT);
    fb[y*UI_LOCK_WIDTH+x]=gray;
}
static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){
    for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x)pixel(fb,x,y,gray);
}
static void ui_clear_page(uint8_t *fb){memset(fb,255,sizeof(framebuffer));}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){
    (void)radius;ui_fill_rect_fast(r,gray,fb);
}
static void ui_nav_status(uint8_t *fb){pixel(fb,24,18,0);}
static void ui_nav_draw(uint8_t *fb,int tab){assert(tab==0);pixel(fb,24,1112,0);}
static void ui_nav_clock_draw(uint8_t *fb){++clock_paints;pixel(fb,24,18,0);}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool white){
    (void)px;(void)text;(void)align;pixel(fb,x,y,white?255:0);
}
static void label(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray){
    (void)px;(void)text;(void)align;pixel(fb,x,y,gray);
}
static void label_on(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray,uint8_t bg){
    (void)bg;label(fb,x,y,px,text,align,gray);
}
static int ui_text_fixed_width_px(int px,const char *text){(void)px;(void)text;return 100;}
static void fit(char *text,int px,int width){(void)text;(void)px;(void)width;}
static void home_frame(uint8_t *fb,EpdRect r,int radius){(void)radius;pixel(fb,r.x,r.y,0);}
static void circle_frame(uint8_t *fb,int x,int y,int radius){(void)radius;pixel(fb,x,y,0);}
static void epd_fill_circle(int x,int y,int radius,uint8_t gray,uint8_t *fb){(void)radius;pixel(fb,x,y,gray);}
static void ui_wallpaper_draw_avatar(uint8_t *fb,const uint8_t *data,EpdRect r,int radius){
    (void)data;(void)radius;pixel(fb,r.x,r.y,0);
}
static void ui_draw_separator(uint8_t *fb,int y,int x,int width,uint8_t gray){
    ui_fill_rect_fast((EpdRect){x,y,width,1},gray,fb);
}
static int title_lines(uint8_t *fb,const char *text,uint8_t bg){
    (void)text;(void)bg;pixel(fb,264,320,0);return 1;
}
static void draw_cover(uint8_t *fb,const home_book_t *book,EpdRect r){
    (void)book;ui_fill_rect_fast(r,0,fb);
}
static home_book_t *featured(void){return s_current.path[0]?&s_current:&s_recent[0];}
static int64_t ui_nav_clock_minute(void){return minute;}
static enum EpdDrawError display_refresh_area(void *hl,int intent,EpdRect area,int flags){
    (void)hl;assert(flags==DISPLAY_REFRESH_DEFAULT);++draws;last_area=area;last_intent=intent;return draw_result;
}
static void guard_draw_result(void *hl,enum EpdDrawError result){(void)hl;assert(result==draw_result);}
static enum EpdDrawError update_display_full(void *hl){(void)hl;++fulls;return draw_result;}
static enum EpdDrawError update_display_fast_page(void *hl){(void)hl;++pages;return draw_result;}
static int board_sd_get_info(board_sd_info_t *info){info->mounted=true;return 0;}
static unsigned book_cover_jobs_begin(void){return 1;}
static void scan_books(void){++scan_calls;}
static void restore_cover_cache(void){}
static void remember_scan_state(const board_sd_info_t *info){(void)info;}
static void refresh_excerpt(void){}
static void avatar_prepare_state(void){}
static ui_avatar_state_t ui_avatar_poll(const char *path,int size,uint8_t *out){
    (void)path;(void)size;(void)out;return UI_AVATAR_READY;
}
static bool load_cover(home_book_t *book,int slot){
    assert(slot==0||slot==1);++cover_calls;s_cover_waiting=wait_cover;
    if(wait_cover)return false;book->cover=&cover_pixel;return true;
}
static int ui_nav_hit(int x,int y){return y>=1112?x/171:-1;}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;nav=tab;}
static bool app_book_request_open_from_home(const char *path){assert(path[0]);++open_calls;return !fail_open;}
'''
for name in ("continue_card_rect", "continue_card_hit_rect", "continue_card_pressed",
             "control_feedback_area", "render_root", "render", "present", "on_tick",
             "hit", "control", "on_gesture", "main_page_visible"):
    unit += "\n" + function(name, HOME)
unit += r'''
static void blank_lower(void){
    for(int y=574;y<1112;++y)for(int x=0;x<684;++x)assert(framebuffer[y*684+x]==255);
}
int main(void){
    app_ctx_t ctx={.fb=framebuffer};touch_t touch={0};ctx.touch=&touch;
    for(int has_book=0;has_book<2;++has_book)for(int avatar=0;avatar<2;++avatar)for(int pressed=0;pressed<2;++pressed){
        snprintf(s_current.path,sizeof(s_current.path),"%s",has_book?"/book.epub":"");
        strcpy(s_current.title,"Title");strcpy(s_current.author,"Author");
        strcpy(s_excerpt,"Excerpt");s_current.has_progress=true;s_current.progress.pct=42;
        s_avatar_ready=avatar;s_press_pending=pressed;s_pressed=continue_card_rect();
        render(&ctx,ctx.fb);blank_lower();assert(main_page_visible(&ctx));
    }
    s_press_pending=false;
    for(int y=574;y<1112;y+=11)for(int x=0;x<684;x+=13)for(int type=UI_GESTURE_PRESS;type<=UI_GESTURE_SWIPE_U;++type){
        ui_gesture_event_t ev={type,x,y,x,y};nav=-1;int opened=open_calls;
        assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE);assert(!s_press_pending&&nav==-1&&open_calls==opened);
    }
    ui_gesture_event_t ev={UI_GESTURE_TAP,540,118,540,118};nav=-1;
    assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&nav==-1);
    ev.x0=618;assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&nav==3);
    ev.x0=400;ev.y0=1150;on_gesture(&ctx,&ev);assert(nav==2);
    minute=s_clock_minute=100;s_cover_next=1;
    assert(on_tick(&ctx)==APP_REDRAW_NONE&&!draws&&!pages&&!fulls&&!scan_calls&&!cover_calls);
    minute=101;assert(on_tick(&ctx)==APP_REDRAW_AREA&&s_clock_pending);
    assert(present(&ctx,APP_REDRAW_AREA));blank_lower();
    assert(draws==1&&!pages&&!fulls&&clock_paints==1&&last_intent==DISPLAY_REFRESH_DETAIL);
    assert(last_area.x==24&&last_area.y==18&&last_area.width==156&&last_area.height==44&&s_clock_minute==101);
    minute=102;draw_result=EPD_DRAW_FAILED;assert(on_tick(&ctx)==APP_REDRAW_AREA);
    present(&ctx,APP_REDRAW_AREA);assert(s_clock_minute==101&&on_tick(&ctx)==APP_REDRAW_AREA);
    draw_result=EPD_DRAW_SUCCESS;present(&ctx,APP_REDRAW_AREA);assert(s_clock_minute==102);
    touch.touched=true;minute=103;assert(on_tick(&ctx)==APP_REDRAW_NONE);touch.touched=false;
    ctx.consumed=true;assert(on_tick(&ctx)==APP_REDRAW_NONE);ctx.consumed=false;minute=102;
    s_cover_next=0;s_current.cover=NULL;wait_cover=true;assert(on_tick(&ctx)==APP_REDRAW_NONE&&s_cover_next==0);
    wait_cover=false;assert(on_tick(&ctx)==APP_REDRAW_AREA&&cover_calls==2&&s_area.y==278);
    for(int i=0;i<10;++i)assert(on_tick(&ctx)==APP_REDRAW_NONE);
    assert(cover_calls==2&&!scan_calls);
    s_home_force_full_once=true;present(&ctx,APP_REDRAW_PAGE);assert(fulls==1&&!s_home_force_full_once);
    present(&ctx,APP_REDRAW_PAGE);assert(pages==1);present(&ctx,APP_REDRAW_FULL);assert(fulls==2);
    blank_lower();
    puts("PASS: blank lower Home, no retired gesture routes, avatar/nav, minute-only bounded updates/retry, one resume cover, full-refresh policy");
}
'''
with tempfile.TemporaryDirectory(prefix="dashboard-home-") as directory:
    work = Path(directory)
    c_file, binary = work / "test.c", work / "test"
    c_file.write_text(unit)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", str(c_file), "-o", str(binary)],
                   cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
