#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：真实九格几何、编辑事务与正文八动作回归。/ Real grid geometry, editing transactions and eight body actions."""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function
from reader_session_test_state import position_state
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT/'main/apps/app_book.c'
UI = ROOT/'main/ui/ui_reader_tap_grid.c'
SETTINGS = ROOT/'main/settings/settings.h'
source = BOOK.read_text()
unit = position_state(page=0) + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "book_reader_input.h"
#include "settings.h"
enum {READING,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_TURN_SETTINGS,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_TAP_GRID};
typedef struct {int64_t now_ms;bool user_activity;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
typedef enum {UI_GESTURE_TAP,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D} ui_gesture_type_t;
typedef struct {ui_gesture_type_t type;uint16_t x0,y0;} ui_gesture_event_t;
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,s_reader_slider;
static int64_t s_stats_activity_ms,s_reader_notice_until;
static char s_reader_notice[128];
static bool s_reader_tools_toggle;
static struct {bool active;} s_dictionary_selection;
static EpdRect current_body={36,96,612,1104};
static int screen_width=684;
#define READER_WIDTH screen_width
#define READER_BAR_TOP 1096
static EpdRect body_rect(void){return current_body;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static EpdRect reader_sheet_back_rect(int top){return (EpdRect){36,top+20,64,64};}
static unsigned invalidates,saves,turns,homes,fullscreens,refreshes,lights,panel_calls,links;
static bool fail_save,shake,light_on,light_ready=true,image_slots,rtl;
static int last_direction,link_at=-1;
static app_reader_tap_mode_t configured_mode;
static uint8_t configured[9];
bool app_settings_reader_shake_turn(void){return shake;}
void app_settings_set_reader_shake_turn(bool on){shake=on;}
app_reader_tap_mode_t app_settings_reader_tap_mode(void){return configured_mode;}
app_reader_key_action_t app_settings_reader_tap_action(unsigned cell){assert(cell<9);return configured[cell];}
bool app_settings_reader_vertical_turn(void){return configured_mode==APP_READER_TAP_VERTICAL;}
bool app_settings_book_vertical(void){return rtl;}
bool app_settings_set_reader_tap_config(app_reader_tap_mode_t mode,const uint8_t *actions){
 ++saves;if(fail_save)return false;configured_mode=mode;memcpy(configured,actions,9);return true;
}
static void invalidate_prep(void){++invalidates;}
static void copy_text(char *out,size_t cap,const char *in){snprintf(out,cap,"%s",in);}
static app_redraw_t reader_toggle_touch(app_ctx_t *ctx){(void)ctx;assert(0);return APP_REDRAW_NONE;}
static app_redraw_t dictionary_dismiss(void){s_dictionary_selection.active=false;return APP_REDRAW_PAGE;}
static app_redraw_t turn_page(app_ctx_t *ctx,int direction){(void)ctx;++turns;last_direction=direction;return APP_REDRAW_PAGE;}
static app_redraw_t reader_return(app_ctx_t *ctx){(void)ctx;++homes;return APP_REDRAW_PAGE;}
static app_redraw_t toggle_reader_fullscreen(app_ctx_t *ctx){(void)ctx;++fullscreens;return APP_REDRAW_PAGE;}
static app_redraw_t reader_manual_refresh(app_ctx_t *ctx){(void)ctx;++refreshes;return APP_REDRAW_FULL;}
static bool frontlight_ready(void){return light_ready;}
bool app_settings_frontlight_on(void){return light_on;}
void app_settings_set_frontlight_on(bool on){light_on=on;}
uint8_t app_settings_frontlight_brightness(void){return 30;}
uint8_t app_settings_frontlight_cct(void){return 50;}
static bool frontlight_apply(bool on,unsigned b,unsigned c){assert(on==light_on&&b==30&&c==50);++lights;return true;}
static int reader_image_slots(int page){(void)page;return image_slots;}
static int book_layout_link_at(size_t page,int x,int y){(void)page;(void)x;(void)y;return link_at;}
static bool note_open_link(int link){assert(link==link_at);++links;return true;}
static EpdRect book_layout_image_rect(void){return (EpdRect){0,0,684,1216};}
static EpdRect ui_rect_union(EpdRect a,EpdRect b){
 int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y;
 int r=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width;
 int bot=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;
 return (EpdRect){x,y,r-x,bot-y};
}
'''
unit += re.search(r'^#define s_toolbar .*', source, re.M)[0]+'\n'
unit += re.search(r'static uint8_t s_tap_draft\[[^;]+;', source)[0]+'\n'
unit += 'static int s_tap_selected=-1;static bool s_tap_picker;static char s_tap_error[64];\n'
unit += re.search(r'static const app_reader_key_action_t s_tap_order\[8\] = \{[\s\S]*?\};', source)[0]+'\n'
for name in ('ui_reader_tap_cancel_rect','ui_reader_tap_save_rect','ui_reader_tap_picker_back_rect','ui_reader_tap_option_rect'):
    unit += function(name,UI)+'\n'
for name in ('ui_reader_turn_mode_rect','ui_reader_turn_shake_rect'):
    unit += function(name,ROOT/'main/ui/ui_reader_settings.c')+'\n'
for name in ('reader_tap_grid_action','reader_turn_settings_action','toggle_reader_tools_panel','reader_key_action','reader_swipe_direction'):
    unit += function(name,BOOK)+'\n'
unit += r'''
static app_redraw_t reader_panel_action(app_ctx_t *ctx,uint16_t x,uint16_t y){
 ++panel_calls;return s_reader_panel==READER_PANEL_TAP_GRID?reader_tap_grid_action(ctx,x,y):APP_REDRAW_NONE;
}
'''
action = function('action_at',BOOK)
a = action.index('    if (s_view == READING) {')
b = action.index('    if (s_view == SHELF) {',a)
unit += 'static app_redraw_t reading_tap(app_ctx_t *ctx,uint16_t x,uint16_t y){\n'+action[a:b]+'return APP_REDRAW_NONE;}\n'
gesture = function('gesture_event', BOOK)
a = gesture.index('        if (s_view == READING && (!s_toolbar')
b = gesture.index(' else if (s_view == SHELF', a)
unit += 'static app_redraw_t reading_swipe(app_ctx_t *ctx,const ui_gesture_event_t *ev){\n'+gesture[a:b]+'return APP_REDRAW_NONE;}\n'
unit += r'''
static void tap(app_ctx_t *ctx,EpdRect r){assert(r.width>0&&r.height>0);reader_tap_grid_action(ctx,r.x+r.width/2,r.y+r.height/2);}
static void enter(app_ctx_t *ctx){EpdRect r=ui_reader_turn_mode_rect(2);reader_turn_settings_action(ctx,r.x+10,r.y+10);assert(s_reader_panel==READER_PANEL_TAP_GRID&&!s_tap_picker&&s_tap_selected==-1);}
static void test_reader_edges(app_ctx_t *ctx){
 const int widths[]={684,1216},left[]={0,24,0,96,12,96},right[]={0,24,96,0,96,12};
 unsigned checks=0;
 image_slots=false;link_at=-1;s_dictionary_selection.active=false;
 for(unsigned orientation=0;orientation<2;++orientation)for(unsigned margin=0;margin<6;++margin)for(unsigned fullscreen=0;fullscreen<2;++fullscreen){
  screen_width=widths[orientation];int height=widths[1-orientation];
  current_body=(EpdRect){left[margin],fullscreen?24:180,screen_width-left[margin]-right[margin],height-(fullscreen?36:300)};
  EpdRect body=current_body;
  for(unsigned cell=0;cell<9;++cell)configured[cell]=cell%2?APP_READER_KEY_NEXT:APP_READER_KEY_PREV;
  for(unsigned mode=0;mode<3;++mode)for(unsigned writing=0;writing<2;++writing)for(unsigned row=0;row<3;++row){
   configured_mode=(app_reader_tap_mode_t)mode;rtl=writing;
   int y=body.y+body.height*(2*row+1)/6;
   for(int x=0;x<screen_width;++x){
    int projected=x<body.x?body.x:(x>=body.x+body.width?body.x+body.width-1:x);
    int expected=book_reader_tap_direction(body,projected,y,mode==APP_READER_TAP_VERTICAL,rtl);
    if(mode==APP_READER_TAP_GRID){int cell=book_reader_grid_hit(body,projected,y);expected=configured[cell]==APP_READER_KEY_PREV?-1:1;}
    s_reader_panel=READER_PANEL_NONE;turns=0;
    assert(reading_tap(ctx,x,y)==APP_REDRAW_PAGE&&turns==1&&last_direction==expected);++checks;
   }
   const int outside[][2]={{screen_width,y},{65535,y},{0,body.y-1},{screen_width-1,body.y+body.height}};
   for(unsigned i=0;i<4;++i){turns=0;assert(reading_tap(ctx,outside[i][0],outside[i][1])==APP_REDRAW_NONE&&!turns);++checks;}
   for(unsigned side=0;side<2;++side){
    int x=side?screen_width-1:0;
    ui_gesture_event_t ev={.type=UI_GESTURE_SWIPE_L,.x0=x,.y0=y};
    turns=0;ctx->user_activity=false;
    assert(reading_swipe(ctx,&ev)==APP_REDRAW_PAGE&&turns==1&&last_direction==((rtl&&mode!=APP_READER_TAP_VERTICAL)?-1:1)&&ctx->user_activity);++checks;
   }
  }
  configured_mode=APP_READER_TAP_GRID;
  for(unsigned row=0;row<3;++row)for(unsigned side=0;side<2;++side){
   int x=side?screen_width-1:0,y=body.y+body.height*(2*row+1)/6;
   s_reader_panel=READER_PANEL_TAP_GRID;s_tap_picker=false;
   assert(reader_tap_grid_action(ctx,x,y)==APP_REDRAW_PAGE&&s_tap_picker&&s_tap_selected==(int)(row*3+(side?2:0)));++checks;
  }
 }
 printf("PASS: %u edge/interior dispatch checks across both widths, asymmetric margins, window/fullscreen, three modes and RTL; edge swipes and grid editing\n",checks);
}
int main(void){
 app_ctx_t ctx={.now_ms=123};
 // Every pixel belongs to exactly one cell, with matching boundaries in each orientation and full-screen/margin geometry.
 const EpdRect bodies[]={{36,96,612,1104},{96,24,492,1178},{0,0,1216,684},{71,112,1067,524},{0,0,1,1},{0,0,2,2}};
 for(unsigned k=0;k<sizeof(bodies)/sizeof(*bodies);++k){
  EpdRect body=bodies[k];unsigned area=0;
  for(unsigned i=0;i<9;++i){
   EpdRect r=book_reader_grid_cell(body,i);area+=r.width*r.height;
   if(r.width&&r.height){assert(book_reader_grid_hit(body,r.x,r.y)==(int)i);assert(book_reader_grid_hit(body,r.x+r.width-1,r.y+r.height-1)==(int)i);}
  }
  assert(area==(unsigned)(body.width*body.height));
  for(int y=body.y;y<body.y+body.height;++y)for(int x=body.x;x<body.x+body.width;++x){
   int hit=book_reader_grid_hit(body,x,y);assert(hit>=0&&hit<9);assert(ui_rect_hit(book_reader_grid_cell(body,hit),x,y));
  }
  assert(book_reader_grid_hit(body,body.x-1,body.y)==-1&&book_reader_grid_hit(body,body.x+body.width,body.y)==-1);
  assert(book_reader_grid_hit(body,body.x,body.y-1)==-1&&book_reader_grid_hit(body,body.x,body.y+body.height)==-1);
 }
 assert(!book_reader_grid_cell(current_body,9).width&&book_reader_grid_hit((EpdRect){0},0,0)==-1);
 for(unsigned i=0;i<9;++i)configured[i]=APP_READER_KEY_NEXT;
 enter(&ctx);assert(!saves&&configured_mode==APP_READER_TAP_HORIZONTAL);
 for(unsigned cell=0;cell<9;++cell)for(unsigned option=0;option<8;++option){
  EpdRect r=book_reader_grid_cell(current_body,cell);tap(&ctx,r);assert(s_tap_picker&&s_tap_selected==(int)cell);
  unsigned before=saves;
  assert(reader_tap_grid_action(&ctx,342,640)==APP_REDRAW_NONE&&s_tap_picker);
  tap(&ctx,ui_reader_tap_option_rect(option));assert(!s_tap_picker&&s_tap_draft[cell]==s_tap_order[option]);
  assert(saves==before&&configured[cell]==APP_READER_KEY_NEXT);
 }
 tap(&ctx,book_reader_grid_cell(current_body,0));tap(&ctx,ui_reader_tap_picker_back_rect());assert(!s_tap_picker);
 fail_save=true;tap(&ctx,ui_reader_tap_save_rect());assert(s_reader_panel==READER_PANEL_TAP_GRID&&s_tap_error[0]&&configured_mode==APP_READER_TAP_HORIZONTAL);
 fail_save=false;tap(&ctx,ui_reader_tap_cancel_rect());assert(s_reader_panel==READER_PANEL_TURN_SETTINGS);
 enter(&ctx);for(unsigned i=0;i<9;++i)assert(s_tap_draft[i]==APP_READER_KEY_NEXT);
 tap(&ctx,book_reader_grid_cell(current_body,0));assert(s_tap_picker);
 tap(&ctx,ui_reader_tap_cancel_rect());assert(s_reader_panel==READER_PANEL_TURN_SETTINGS&&!s_tap_picker&&configured_mode==APP_READER_TAP_HORIZONTAL);
 enter(&ctx);tap(&ctx,book_reader_grid_cell(current_body,4));assert(s_tap_picker);
 s_tap_draft[4]=APP_READER_KEY_TOOLS;tap(&ctx,ui_reader_tap_save_rect());assert(configured_mode==APP_READER_TAP_GRID&&configured[4]==APP_READER_KEY_TOOLS&&s_reader_panel==READER_PANEL_TURN_SETTINGS);
 for(unsigned mode=0;mode<2;++mode){EpdRect r=ui_reader_turn_mode_rect(mode);reader_turn_settings_action(&ctx,r.x,r.y);assert(configured_mode==(app_reader_tap_mode_t)mode&&configured[4]==APP_READER_KEY_TOOLS);}
 EpdRect r=ui_reader_turn_shake_rect();reader_turn_settings_action(&ctx,r.x,r.y);assert(shake);
 unsigned before=saves;assert(reader_turn_settings_action(&ctx,342,1160)==APP_REDRAW_NONE&&before==saves);
 configured_mode=APP_READER_TAP_GRID;
 for(unsigned cell=0;cell<9;++cell)for(unsigned option=0;option<8;++option){
  s_reader_panel=READER_PANEL_NONE;s_reader_tools_toggle=false;configured[cell]=s_tap_order[option];
  turns=homes=fullscreens=refreshes=lights=0;light_on=false;
  EpdRect r=book_reader_grid_cell(current_body,cell);app_redraw_t result=reading_tap(&ctx,r.x+r.width/2,r.y+r.height/2);
  assert(turns==(option<2));if(option<2)assert(last_direction==(option==0?-1:1));
  assert(homes==(option==4)&&fullscreens==(option==3)&&refreshes==(option==5)&&lights==(option==6));
  if(option==2)assert(s_reader_panel==READER_PANEL_TOOLS&&s_reader_tools_toggle);
  if(option==5)assert(result==APP_REDRAW_FULL);
  if(option>=6)assert(result==APP_REDRAW_NONE);
 }
 s_reader_panel=READER_PANEL_NONE;turns=0;configured[0]=APP_READER_KEY_PREV;rtl=true;
 assert(reading_tap(&ctx,100,200)==APP_REDRAW_PAGE&&last_direction==-1); // Explicit actions do not mirror for vertical text.
 image_slots=true;assert(reading_tap(&ctx,0,200)==APP_REDRAW_PAGE&&last_direction==-1); // 左右留白归边缘格，插图页同样适用。/ Side margins map to edge cells on image pages too.
 assert(reading_tap(&ctx,0,20)==APP_REDRAW_NONE); // 九宫格不扩到正文上下之外。/ Grid taps do not extend above or below the body.
 link_at=0;turns=0;assert(reading_tap(&ctx,100,200)==APP_REDRAW_PAGE&&links==1&&!turns);link_at=-1;
 enter(&ctx);turns=0;assert(reading_tap(&ctx,100,200)==APP_REDRAW_PAGE&&s_tap_picker&&!turns);
 test_reader_edges(&ctx);
 puts("PASS: exhaustive grid boundaries/rotation/margins, 72 draft edits and real body dispatches, gaps, Back/Cancel, failed saves, mode switches, footnotes and panel isolation");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-tap-grid-') as directory:
    folder=Path(directory)
    env,compiler=toolchain(folder)
    c=folder/'test.c';binary=folder/'test';c.write_text(unit)
    subprocess.run(compiler+['-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(ROOT/'tools/reader_settings_stubs'),'-I'+str(ROOT/'tools/settings_backup_stubs'),
        '-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main/settings'),str(c),'-o',str(binary)],env=env,check=True)
    subprocess.run([str(binary)],env=env,check=True)
