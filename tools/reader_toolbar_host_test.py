#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：验证实际底栏分发、更多入口与横竖屏命中。/ English: Exercise actual toolbar routing, More entries and orientation bounds."""
from pathlib import Path
import re
import subprocess
import tempfile

from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
source = BOOK.read_text()
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int leaf;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {READING,TOC};
enum {UI_READER_SETTINGS_INPUT,UI_READER_SETTINGS_PAGE,UI_READER_SETTINGS_REFRESH};
static int width=684,height=1216;
#define READER_WIDTH width
#define READER_HEIGHT height
#define READER_BAR_TOP (height-120)
#define BOOK_TOC_ROWS 8
'''
unit += re.search(r'^#define BOOK_TOOL_COUNT .*', source, re.M)[0] + '\n'
a = source.index('typedef enum {\n    READER_PANEL_NONE')
b = source.index('} reader_panel_t;', a) + len('} reader_panel_t;')
unit += source[a:b] + '\n'
unit += r'''
static reader_panel_t s_reader_panel;
static int s_reader_settings_group,s_line_spacing,s_reader_slider,s_bookmark_page;
static bool s_reader_tools_toggle,s_toc_jump_open,s_toc_jump_drag;
static bool s_bookmark_edit,s_bookmark_delete_confirm,s_bookmark_delete_error;
static uint32_t s_bookmark_selected;
static char s_message[64];
static size_t toc_position;
static unsigned saved,bookmarked,invalidated,fullscreen;
static int view;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void invalidate_prep(void){++invalidated;}
static void save_progress(void){++saved;}
static void set_reader_view(int next){view=next;}
static size_t current_toc_position(void){return toc_position;}
static bool bookmark_toggle(void){++bookmarked;return true;}
static int app_settings_book_line_spacing(void){return 135;}
static app_redraw_t toggle_reader_fullscreen(app_ctx_t *ctx){(void)ctx;++fullscreen;s_reader_panel=READER_PANEL_NONE;return APP_REDRAW_FULL;}
'''
for name in ('tool_rect', 'reader_more_rect', 'reader_sheet_back_rect'):
    unit += function(name, BOOK) + '\n'
action = function('reader_panel_action', BOOK)
a = action.index('    if (s_reader_panel == READER_PANEL_TOOLS) {')
b = action.index('    if (s_reader_panel == READER_PANEL_STATS) {', a)
unit += 'static app_redraw_t tap(app_ctx_t *ctx,uint16_t x,uint16_t y){\n' + action[a:b]
unit += 'return APP_REDRAW_NONE;}\n'
control = function('control_at', BOOK)
a = control.index('    if (s_view == READING) {')
b = control.index('    if (s_view != SHELF)', a)
unit += 'static int s_view=READING;\nstatic int feedback_at(int x,int y,EpdRect *rect){\n' + control[a:b]
unit += 'return -1;}\n'
unit += r'''
static void reset(void){
 s_reader_panel=READER_PANEL_TOOLS;s_reader_settings_group=UI_READER_SETTINGS_PAGE;
 s_line_spacing=99;s_reader_slider=5;s_reader_tools_toggle=false;
 s_toc_jump_open=s_toc_jump_drag=true;strcpy(s_message,"old message");
 s_bookmark_edit=s_bookmark_delete_confirm=s_bookmark_delete_error=true;
 s_bookmark_page=3;s_bookmark_selected=UINT32_MAX;
 saved=bookmarked=invalidated=fullscreen=0;view=READING;toc_position=19;
}
static void check_tool(app_ctx_t *ctx,int index){
 assert(invalidated==1&&!fullscreen&&!s_reader_tools_toggle);
 assert(saved==(unsigned)(index==0)&&bookmarked==(unsigned)(index==1));
 if(index==0){assert(view==TOC&&ctx->leaf==2&&!s_message[0]&&!s_toc_jump_open&&!s_toc_jump_drag);assert(s_reader_panel==READER_PANEL_NONE);}
 else {
  assert(view==READING);
  const reader_panel_t panels[]={READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_FONT_SETTINGS,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_MORE};
  assert(s_reader_panel==panels[index]);
  if(index==2)assert(s_reader_slider==-1&&s_line_spacing==135);
  if(index==3)assert(s_reader_settings_group==UI_READER_SETTINGS_INPUT);
 }
}
int main(void){
 app_ctx_t ctx={0};
 assert(BOOK_TOOL_COUNT==5&&!tool_rect(-1).width&&!tool_rect(5).width);
 assert(!reader_more_rect(-1).width&&!reader_more_rect(3).width);
 for(int landscape=0;landscape<2;++landscape){
  width=landscape?1216:684;height=landscape?684:1216;
  for(int x=0;x<width;++x)for(int y=READER_BAR_TOP;y<height;++y){
   reset();int expected=-1;
   for(int i=0;i<BOOK_TOOL_COUNT;++i)if(ui_rect_hit(tool_rect(i),x,y)){assert(expected==-1);expected=i;}
   EpdRect feedback={0};assert(feedback_at(x,y,&feedback)==(expected<0?-1:200+expected));
   if(expected>=0){EpdRect paint=tool_rect(expected);assert(!memcmp(&paint,&feedback,sizeof(paint)));}
   app_redraw_t redraw=tap(&ctx,x,y);
   if(expected>=0){assert(redraw==APP_REDRAW_PAGE);check_tool(&ctx,expected);}
   else assert(redraw==APP_REDRAW_NONE&&!invalidated&&s_reader_panel==READER_PANEL_TOOLS);
  }
  reset();toc_position=SIZE_MAX;EpdRect toc=tool_rect(0);tap(&ctx,toc.x,toc.y);assert(ctx.leaf==0);
  reset();assert(tap(&ctx,width/2,READER_BAR_TOP-1)==APP_REDRAW_PAGE);
  assert(s_reader_panel==READER_PANEL_NONE&&s_reader_tools_toggle&&invalidated==1&&!saved&&!bookmarked);
 }
 width=684;height=1216;
 for(int i=0;i<3;++i){
  EpdRect r=reader_more_rect(i);
  for(int corner=0;corner<4;++corner){
   reset();s_reader_panel=READER_PANEL_MORE;
   app_redraw_t redraw=tap(&ctx,r.x+((corner&1)?r.width-1:0),r.y+((corner&2)?r.height-1:0));
   assert(!saved&&!bookmarked);
   if(i==0)assert(redraw==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_STATS);
   if(i==1)assert(redraw==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_BOOKMARKS&&!s_bookmark_page&&!s_bookmark_edit&&!s_bookmark_delete_confirm&&!s_bookmark_delete_error&&!s_bookmark_selected);
   if(i==2)assert(redraw==APP_REDRAW_FULL&&fullscreen==1&&s_reader_panel==READER_PANEL_NONE);
  }
  const int gaps[][2]={{35,r.y},{648,r.y},{36,r.y-1},{36,r.y+r.height}};
  for(unsigned point=0;point<sizeof(gaps)/sizeof(gaps[0]);++point){
   reset();s_reader_panel=READER_PANEL_MORE;
   assert(tap(&ctx,gaps[point][0],gaps[point][1])==APP_REDRAW_NONE&&s_reader_panel==READER_PANEL_MORE&&!invalidated);
  }
 }
 reset();s_reader_panel=READER_PANEL_MORE;tap(&ctx,64,861);assert(s_reader_panel==READER_PANEL_TOOLS&&invalidated==1);
 reset();s_reader_panel=READER_PANEL_MORE;tap(&ctx,342,1170);assert(s_reader_panel==READER_PANEL_NONE&&fullscreen==1);
 reset();s_reader_panel=READER_PANEL_MORE;assert(tap(&ctx,342,1200)==APP_REDRAW_NONE&&s_reader_panel==READER_PANEL_MORE&&!invalidated);
 reset();s_reader_panel=READER_PANEL_MORE;tap(&ctx,342,803);assert(s_reader_panel==READER_PANEL_NONE&&invalidated==1);
 puts("PASS: five actual toolbar actions, every portrait/landscape bar pixel, feedback geometry, TOC anchor, typography initialization, More destinations/reset, inert gutters and dismiss without turning");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
