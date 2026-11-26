#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：执行真实排版滑杆、预览合并与重排，覆盖中断和失败回退。
# English: Exercise actual typography sliders, coalesced previews and reflow, including interruption and rollback.
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
reader = root / 'main/apps/app_book.c'
settings = root / 'main/settings/settings.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app/app_touch.h"
#include "book/book_font_size.h"
#define BOOK_PX_MIN BOOK_READER_PX_MIN
#define BOOK_PX_MAX BOOK_READER_PX_MAX
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 96
enum { MARGIN_LEFT, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOTTOM, MARGIN_COUNT };
static int s_line_spacing;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint8_t *fb;void *hl;const app_touch_t *touch;int64_t now_ms;
 bool pressed,released,consumed,request_app,request_menu,request_return;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE} app_redraw_t;
enum {READING,SHELF,READER_PANEL_FONT_SETTINGS,READER_PANEL_LAYOUT_SETTINGS};
enum {DISPLAY_ORIENTATION_PORTRAIT,DISPLAY_ORIENTATION_LEFT};
#define UI_TOUCH_SLOP_PX 24
#define UI_SWIPE_MIN_PX 120
#define UI_LONG_PRESS_MS 500
#define UI_GRAY_BLACK 0
#define UI_GRAY_WHITE 255
#define EPD_DRAW_ALIGN_CENTER 0
enum { DISPLAY_REFRESH_CAPSULE, DISPLAY_REFRESH_DEFAULT };
static int s_view=READING,s_reader_panel=READER_PANEL_LAYOUT_SETTINGS,orientation;
static struct { bool active; } s_dictionary_selection;
static char s_open_error[1],s_reader_notice[2];
static bool s_clear_confirm,s_reader_slider_dragged,s_reader_slider_feedback_pending;
static int s_reader_slider_origin_x,s_reader_slider_shown;
static int64_t s_reader_slider_next_preview_ms;
static EpdRect s_area,crop;
static int paints, pushes, knob_x, layout_calls;
static bool scaled;
static uint8_t line_spacing=130;
static int s_px=36,s_margin[4]={24,24,0,0},s_reader_slider=-1;
static int s_reader_preview_px,s_reader_preview_margin[4],s_reader_preview_line;
static int s_reader_preview_para,s_reader_preview_tracking;
static bool fail_layout;
static uint8_t s_book_tracking=15,s_book_para=25;
static unsigned writes,saves,builds,invalidations;
static char s_text_storage[1],*s_text=s_text_storage;
static size_t s_text_len=1,s_block_count;
static const void *s_blocks;
static int typography;
static uint8_t stored_tracking=207,stored_para=25;
#define NVS_KEY_BOOK_TRACK "bk_track"
#define NVS_KEY_BOOK_PARA "bk_para"
static void nvs_put_u8(const char *key,uint8_t value){
 ++writes;
 if(!strcmp(key,NVS_KEY_BOOK_TRACK))stored_tracking=value;
 else {assert(!strcmp(key,NVS_KEY_BOOK_PARA));stored_para=value;}
}
static uint8_t app_settings_book_line_spacing(void){return line_spacing;}
static int reader_ui_extra(int pixels){(void)pixels;return 0;}
static int reader_margin_input(EpdRect r,int x,int current){
 (void)r;(void)x;return current;
}
#define BOOK_MARGIN_CHOICES 17
static int reader_margin_value(int level){return level*6;}
static EpdRect body_rect_for_tracking(int tracking){
 (void)tracking;return (EpdRect){24,188,636,940};
}
static EpdRect body_rect(void){return body_rect_for_tracking(s_book_tracking);}
static size_t book_layout_page_start_offset(size_t page){return page*100;}
static size_t book_layout_page_for_offset(size_t off){return off/100;}
static void lock_draw(void){}
static void unlock_draw(void){}
static void invalidate_prep(void){++invalidations;}
static void refresh_chapter_lead(void){}
static void book_layout_set_typography(int pixels){typography=pixels;}
static bool book_layout_build_blocks(const char *text,size_t length,const void *blocks,size_t count,EpdRect rect,int px){
 assert(text==s_text&&length==s_text_len&&blocks==s_blocks&&count==s_block_count);
 assert(rect.width==636&&px==s_px);++builds;return !fail_layout;
}
static void save_progress(void){++saves;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static int display_orientation_get(void){return orientation;}
static unsigned app_settings_system_font_size(void){return 100;}
static EpdRect ui_reader_notice_area(const char *notice,unsigned size){(void)notice;(void)size;return crop;}
static void middle_pending_clear(void){}
static void ui_text_set_system_scale(bool on){scaled=on;}
static void shape(EpdRect r,int gray){
 assert(gray==UI_GRAY_WHITE||gray==UI_GRAY_BLACK);
 assert(ui_rect_hit(crop,r.x,r.y)&&ui_rect_hit(crop,r.x+r.width-1,r.y+r.height-1));
}
static void ui_fill_rect_fast(EpdRect r,int gray,uint8_t *fb){(void)fb;assert(scaled&&gray==UI_GRAY_WHITE);shape(r,gray);++paints;}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int gray){(void)fb;(void)radius;shape(r,gray);}
static void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,int gray){ui_fill_round_rect(fb,r,radius,gray);}
static void ui_draw_control_frame(uint8_t *fb,EpdRect r,int radius,int gray){ui_fill_round_rect(fb,r,radius,gray);}
static void epd_fill_circle(int x,int y,int radius,int gray,uint8_t *fb){
 (void)fb;shape((EpdRect){x-radius,y-radius,2*radius+1,2*radius+1},gray);knob_x=x;
}
static void epd_draw_circle(int x,int y,int radius,int gray,uint8_t *fb){epd_fill_circle(x,y,radius,gray,fb);}
static void ui_text_box_bw(uint8_t *fb,EpdRect r,int px,const char *text,int align,bool inverted){
 (void)fb;(void)px;(void)text;(void)align;assert(scaled&&!inverted);shape(r,UI_GRAY_BLACK);
}
static int display_refresh_area(void *hl,int intent,EpdRect r,int flags){
 (void)hl;assert(intent==DISPLAY_REFRESH_CAPSULE&&flags==DISPLAY_REFRESH_DEFAULT);
 assert(!memcmp(&r,&crop,sizeof(r)));++pushes;return 0;
}
static void guard_draw_result(void *hl,int result){(void)hl;assert(result==0);}
static int reader_margin_level(int pixels){return pixels/6;}
'''
header = (root / 'main/settings/settings.h').read_text()
for name in ('APP_BOOK_TRACK_MIN', 'APP_BOOK_TRACK_MAX', 'APP_BOOK_TRACK_COUNT', 'APP_BOOK_PARAGRAPH_COUNT'):
    unit += re.search(r'^#define ' + name + r' .*', header, re.M)[0] + '\n'
for name in ('app_settings_book_paragraph_value', 'app_settings_book_paragraph_level',
             'app_settings_book_tracking', 'app_settings_set_book_tracking',
             'app_settings_book_paragraph_spacing', 'app_settings_set_book_paragraph_spacing'):
    unit += function(name, settings) + '\n'
unit += r'''
static app_redraw_t apply_reader_layout(app_ctx_t *ctx,int px,const int margin[4],int line,uint8_t para){
 (void)ctx;++layout_calls;
 if(fail_layout)return APP_REDRAW_NONE;
 s_px=px;line_spacing=line;memcpy(s_margin,margin,sizeof(s_margin));
 app_settings_set_book_paragraph_spacing(para);save_progress();return APP_REDRAW_PAGE;
}
'''
from reader_reflow_test_code import reflow_code
unit += reflow_code("typography")
gesture_header = root / 'main/ui/ui_gesture.h'
unit += gesture_header.read_text().replace('#pragma once', '').replace('#include "app.h"', '')
for name in ('ui_gesture_reset', 'emit', 'ui_gesture_feed_drag'):
    unit += function(name, root / 'main/ui/ui_gesture.c') + '\n'
for name in ('apply_reader_typography', 'slider_index', 'reader_slider_rect', 'reader_slider_at', 'reader_slider_capture',
             'reader_slider_map', 'reader_slider_begin', 'reader_slider_move', 'reader_slider_value',
             'reader_slider_refresh_rect', 'draw_pill_slider', 'draw_reader_size_slider', 'draw_reader_slider',
             'reader_slider_commit', 'reader_slider_present'):
    unit += function(name, reader) + '\n'
unit += re.search(r'^enum \{ READER_SLIDER_SLOP_PX .*', reader.read_text(), re.M)[0] + '\n'
for name in ('reader_slider_visible', 'reader_slider_feedback', 'reader_slider_tick', 'reader_slider_gesture'):
    unit += function(name, reader) + '\n'
unit += r'''
static app_touch_t touch;
static ui_gesture_t gesture;
static app_ctx_t drag_ctx;
static void reset_drag(int slider){
 ui_gesture_reset(&gesture);touch=(app_touch_t){0};drag_ctx=(app_ctx_t){.touch=&touch};
 s_reader_panel=slider==0?READER_PANEL_FONT_SETTINGS:READER_PANEL_LAYOUT_SETTINGS;
 s_reader_slider=-1;s_reader_slider_feedback_pending=false;crop=reader_slider_refresh_rect(slider);
 s_book_tracking=15;stored_tracking=207;typography=0;s_px=36;line_spacing=130;s_book_para=25;
 for(int i=0;i<4;++i)s_margin[i]=24;
 paints=pushes=layout_calls=0;builds=writes=saves=0;
}
static app_redraw_t feed(int64_t ms,int contacts,int x,int y){
 const bool was_touched=touch.touched;
 if(contacts){touch.x=x;touch.y=y;}
 touch.touched=contacts>0;touch.count=contacts;
 drag_ctx.now_ms=ms;drag_ctx.pressed=!was_touched&&touch.touched;drag_ctx.released=was_touched&&!touch.touched;
 ui_gesture_event_t event;
 if(!ui_gesture_feed_drag(&gesture,&drag_ctx,&event,false,
    drag_ctx.pressed&&reader_slider_capture(&drag_ctx,touch.x,touch.y)))return APP_REDRAW_NONE;
 bool handled;
 app_redraw_t redraw=reader_slider_gesture(&drag_ctx,&event,&handled);
 if(s_reader_slider>=0||event.type==UI_GESTURE_RELEASE)assert(handled);
 if(redraw==APP_REDRAW_AREA)assert(reader_slider_present(&drag_ctx,redraw));
 return redraw;
}
static void tick(int64_t ms,bool expected){
 drag_ctx.now_ms=ms;
 app_redraw_t redraw=reader_slider_tick(&drag_ctx);
 assert(redraw==(expected?APP_REDRAW_AREA:APP_REDRAW_NONE));
 assert(reader_slider_present(&drag_ctx,redraw)==expected);
 assert(!reader_slider_present(&drag_ctx,redraw)&&!scaled);
}
static void drag_cases(void){
 const int cy=reader_slider_rect(7).y+38;
 reset_drag(7);feed(0,1,342,cy);feed(500,1,342,cy);tick(500,false);
 feed(505,1,346,cy);assert(s_reader_slider_dragged&&!writes&&!paints);
 feed(506,1,360,cy);feed(507,1,390,cy);feed(508,1,400,cy);
 assert(!writes&&!saves&&!paints);tick(512,true);
 assert(knob_x>=392&&knob_x<=406&&paints==1&&pushes==1);
 feed(520,1,500,cy);tick(571,false);tick(572,true);
 feed(580,1,501,cy);tick(632,false);
 assert(pushes==2&&!writes&&!builds&&!saves);
 feed(635,1,670,cy-160);assert(feed(637,0,0,0)==APP_REDRAW_PAGE);
 assert(pushes==2&&writes==1&&builds==1&&saves==1&&s_book_tracking==30);
 assert(s_reader_slider==-1&&s_page==2);

 reset_drag(7);feed(0,1,342,cy);feed(5,1,359,cy);tick(6,true);
 feed(10,1,342,cy);assert(feed(14,0,0,0)==APP_REDRAW_AREA);
 assert(!writes&&!builds&&!saves&&pushes==2&&knob_x==342);

 reset_drag(7);feed(0,1,40,cy);feed(5,1,43,cy);tick(5,false);
 assert(feed(10,0,0,0)==APP_REDRAW_PAGE&&s_book_tracking==14&&writes==1&&!pushes);

 reset_drag(7);feed(0,1,342,cy);feed(5,1,475,cy);tick(7,true);
 assert(feed(10,2,300,cy)==APP_REDRAW_AREA);
 assert(feed(15,1,500,cy)==APP_REDRAW_NONE&&feed(20,0,0,0)==APP_REDRAW_NONE);
 assert(!writes&&!saves&&!builds&&pushes==2&&s_book_tracking==15);

 reset_drag(7);feed(0,1,342,cy);feed(5,1,470,cy);tick(7,true);
 fail_layout=true;assert(feed(10,0,0,0)==APP_REDRAW_AREA);fail_layout=false;
 assert(builds==2&&!writes&&!saves&&pushes==2&&s_book_tracking==15);

 reset_drag(7);feed(0,1,342,cy);feed(5,1,470,cy);tick(7,true);
 drag_ctx.request_app=true;assert(feed(10,0,0,0)==APP_REDRAW_NONE);
 assert(s_reader_slider==-1&&!writes&&!saves&&!builds&&pushes==1);

 for(int slider=0;slider<8;++slider){
  reset_drag(slider);EpdRect r=reader_slider_rect(slider);
  assert(!reader_slider_capture(&drag_ctx,r.x-1,r.y));
  assert(reader_slider_capture(&drag_ctx,r.x,r.y));
  feed(0,1,r.x,r.y+20);feed(5,1,r.x+8,r.y+20);
  assert(s_reader_slider==slider&&s_reader_slider_dragged);
  feed(10,1,683,r.y+20);assert(feed(11,0,0,0)==APP_REDRAW_PAGE);
  assert(saves==1&&layout_calls==(slider==7?0:1)&&!pushes);
  const int maximum[]={BOOK_PX_MAX,96,96,96,96,150,app_settings_book_paragraph_value(APP_BOOK_PARAGRAPH_COUNT-1),30};
  assert(reader_slider_value(slider,false)==maximum[slider]);
 }
 puts("PASS: short/reversed/held drags, all rails, latest-only preview, local rollback and one anchored commit");
}

int main(void){
 app_ctx_t ctx={0};
 EpdRect track=reader_slider_rect(7),para=reader_slider_rect(6);
 for(int start=0;start<APP_BOOK_TRACK_COUNT;++start)for(int direction=-1;direction<=1;direction+=2){
  s_book_tracking=start;stored_tracking=192+start;typography=start+APP_BOOK_TRACK_MIN;
  int expected=start+direction;
  if(expected<0)expected=0;
  if(expected>=APP_BOOK_TRACK_COUNT)expected=APP_BOOK_TRACK_COUNT-1;
  reader_slider_begin(7,direction<0?track.x:track.x+track.width-1);
  assert(s_reader_preview_tracking==expected);
  reader_slider_commit(&ctx);
  assert(s_reader_slider==-1&&app_settings_book_tracking()==expected);
  assert(stored_tracking==192+expected&&typography==expected+APP_BOOK_TRACK_MIN);
 }
 for(int start=0;start<APP_BOOK_TRACK_COUNT;++start)for(int target=0;target<APP_BOOK_TRACK_COUNT;++target){
  s_book_tracking=start;stored_tracking=192+start;typography=start+APP_BOOK_TRACK_MIN;
  int x=track.x+64+(target*(track.width-128)+15)/30;
  if(x>=track.x+track.width-64)x=track.x+track.width-65;
  reader_slider_begin(7,x);
  assert(s_reader_preview_tracking==target);
  reader_slider_move(x);
  assert(s_reader_preview_tracking==target);
  reader_slider_commit(&ctx);
  assert(app_settings_book_tracking()==target&&stored_tracking==192+target);
  assert(typography==target+APP_BOOK_TRACK_MIN&&s_page==2);
 }
 for(int start=0;start<APP_BOOK_PARAGRAPH_COUNT;++start)for(int direction=-1;direction<=1;direction+=2){
  s_book_para=app_settings_book_paragraph_value(start);stored_para=s_book_para;
  int expected=start+direction;
  if(expected<0)expected=0;
  if(expected>=APP_BOOK_PARAGRAPH_COUNT)expected=APP_BOOK_PARAGRAPH_COUNT-1;
  reader_slider_begin(6,direction<0?para.x:para.x+para.width-1);
  assert(s_reader_preview_para==app_settings_book_paragraph_value(expected));
  reader_slider_commit(&ctx);
  assert(app_settings_book_paragraph_spacing()==app_settings_book_paragraph_value(expected));
  assert(stored_para==app_settings_book_paragraph_spacing());
 }
 for(int target=0;target<APP_BOOK_PARAGRAPH_COUNT;++target){
  int x=para.x+64+(target*(para.width-128)+5)/10;
  if(x>=para.x+para.width-64)x=para.x+para.width-65;
  reader_slider_begin(6,x);reader_slider_commit(&ctx);
  assert(s_book_para==app_settings_book_paragraph_value(target)&&stored_para==s_book_para);
 }
 for(int start=0;start<APP_BOOK_TRACK_COUNT;++start){
  s_book_tracking=start;stored_tracking=192+start;typography=start+APP_BOOK_TRACK_MIN;
  unsigned old_writes=writes,old_saves=saves,old_builds=builds;
  fail_layout=true;
  assert(apply_reader_typography(&ctx,(start+1)%APP_BOOK_TRACK_COUNT)==APP_REDRAW_NONE);
  fail_layout=false;
  assert(s_book_tracking==start&&stored_tracking==192+start);
  assert(typography==start+APP_BOOK_TRACK_MIN&&s_page==2);
  assert(writes==old_writes&&saves==old_saves&&builds==old_builds+2);
 }
 s_px=90;reader_slider_begin(0,648);assert(s_reader_preview_px==90);
 s_px=18;reader_slider_begin(0,36);assert(s_reader_preview_px==18);
 EpdRect line=reader_slider_rect(5);
 reader_slider_begin(5,line.x+64+(21*(line.width-128)+20)/40);assert(s_reader_preview_line==131);
 reader_slider_begin(5,line.x+64+(19*(line.width-128)+20)/40);assert(s_reader_preview_line==129);
 assert(invalidations&&saves&&writes);
 puts("PASS: actual press/drag/commit for all 31 tracking and 11 paragraph stops, endpoint bounds, NVS encoding, anchored reflow and unchanged saved spacing on reflow failure");
 drag_cases();
}
'''
from reader_session_test_state import position_state
unit = position_state(page=2) + unit
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'test.c'
    source.write_text(unit)
    binary = source.with_suffix('')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-I' + str(root / 'main'), "-I" + str(root / "main/boot"), "-I" + str(root / "main/diagnostics"), "-I" + str(root / "main/display"), "-I" + str(root / "main/services"), "-I" + str(root / "main/settings"), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
