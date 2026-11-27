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
#define READER_CHAPTER_BAR_HEIGHT 84
#define READER_CHAPTER_BAR_GAP 18
#define READER_CHAPTER_BAR_SHADOW 4
#define READER_CHAPTER_BAR_TOP (READER_BAR_TOP-READER_CHAPTER_BAR_GAP-READER_CHAPTER_BAR_HEIGHT)
#define BOOK_TOC_ROWS 8
'''
unit += re.search(r'^#define BOOK_TOOL_COUNT .*', source, re.M)[0] + '\n'
a = source.index('typedef enum {\n    READER_PANEL_NONE')
b = source.index('} reader_panel_t;', a) + len('} reader_panel_t;')
unit += source[a:b] + '\n'
unit += r'''
static reader_panel_t s_reader_panel;
static int s_reader_settings_group,s_line_spacing,s_reader_slider,s_bookmark_page,s_bookmark_parent;
static bool s_reader_tools_toggle,s_toc_jump_open,s_toc_jump_drag;
static bool s_bookmark_edit,s_bookmark_delete_confirm,s_bookmark_delete_error;
static bool selection_present;
static void bookmark_selection_clear(void){selection_present=false;}
static char s_message[64];
static size_t toc_position;
static unsigned saved,bookmarked,invalidated,chapter_jumps;
static int last_jump_dir;
static bool can_prev_chapter=true,can_next_chapter=true;
static int view;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void invalidate_prep(void){++invalidated;}
static void save_progress(void){++saved;}
static void set_reader_view(int next){view=next;}
static size_t current_toc_position(void){return toc_position;}
static bool bookmark_toggle(void){++bookmarked;return true;}
static int app_settings_book_line_spacing(void){return 135;}
static bool reader_chapter_jump_available(int index){return index?can_next_chapter:can_prev_chapter;}
static app_redraw_t reader_jump_chapter(app_ctx_t *ctx,int dir){
 (void)ctx;if((dir<0&&!can_prev_chapter)||(dir>0&&!can_next_chapter))return APP_REDRAW_NONE;
 ++chapter_jumps;last_jump_dir=dir;return APP_REDRAW_PAGE;
}
'''
for name in ('tool_rect', 'reader_chapter_bar_rect', 'reader_chapter_jump_rect', 'reader_more_rect', 'reader_sheet_back_rect'):
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
 s_bookmark_page=3;selection_present=true;
 saved=bookmarked=invalidated=chapter_jumps=0;last_jump_dir=0;can_prev_chapter=can_next_chapter=true;view=READING;toc_position=19;
}
static void check_tool(app_ctx_t *ctx,int index){
 assert(invalidated==1&&!s_reader_tools_toggle);
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
 assert(!reader_chapter_jump_rect(-1).width&&!reader_chapter_jump_rect(2).width);
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
  // 悬浮章节跳转栏：上一章/下一章、禁用态、中间标题不关闭工具栏、间隙与上方正文收起工具栏。
  // Floating chapter bar: prev/next jumps, disabled boundaries, inert center, and gap/body dismiss.
  EpdRect bar=reader_chapter_bar_rect(), prev=reader_chapter_jump_rect(0), next=reader_chapter_jump_rect(1);
  assert(bar.x>=36&&bar.x+bar.width<=width-36&&bar.y>=0&&bar.y+bar.height+READER_CHAPTER_BAR_GAP==READER_BAR_TOP);
  assert(prev.x==bar.x&&next.x+next.width==bar.x+bar.width&&prev.x+prev.width<next.x);
  reset();EpdRect fb_rect={0};
  assert(feedback_at(prev.x+10,prev.y+10,&fb_rect)==210&&!memcmp(&fb_rect,&prev,sizeof(prev)));
  assert(tap(&ctx,prev.x+10,prev.y+10)==APP_REDRAW_PAGE&&chapter_jumps==1&&last_jump_dir==-1&&s_reader_panel==READER_PANEL_TOOLS);
  reset();assert(feedback_at(next.x+10,next.y+10,&fb_rect)==211&&!memcmp(&fb_rect,&next,sizeof(next)));
  assert(tap(&ctx,next.x+10,next.y+10)==APP_REDRAW_PAGE&&chapter_jumps==1&&last_jump_dir==1&&s_reader_panel==READER_PANEL_TOOLS);
  reset();can_prev_chapter=false;assert(feedback_at(prev.x+10,prev.y+10,&fb_rect)==-1);
  assert(tap(&ctx,prev.x+10,prev.y+10)==APP_REDRAW_NONE&&!chapter_jumps&&s_reader_panel==READER_PANEL_TOOLS);
  reset();can_next_chapter=false;assert(feedback_at(next.x+10,next.y+10,&fb_rect)==-1);
  assert(tap(&ctx,next.x+10,next.y+10)==APP_REDRAW_NONE&&!chapter_jumps&&s_reader_panel==READER_PANEL_TOOLS);
  reset();assert(tap(&ctx,bar.x+bar.width/2,bar.y+bar.height/2)==APP_REDRAW_NONE&&!chapter_jumps&&s_reader_panel==READER_PANEL_TOOLS);
  reset();assert(tap(&ctx,width/2,bar.y-1)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_NONE&&s_reader_tools_toggle);
  reset();toc_position=SIZE_MAX;EpdRect toc=tool_rect(0);tap(&ctx,toc.x,toc.y);assert(ctx.leaf==0);
  reset();assert(tap(&ctx,width/2,READER_BAR_TOP-1)==APP_REDRAW_PAGE);
  assert(s_reader_panel==READER_PANEL_NONE&&s_reader_tools_toggle&&invalidated==1&&!saved&&!bookmarked);
 }
 width=684;height=1216;
 for(int i=0;i<2;++i){
  EpdRect r=reader_more_rect(i);
  for(int corner=0;corner<4;++corner){
   reset();s_reader_panel=READER_PANEL_MORE;
   app_redraw_t redraw=tap(&ctx,r.x+((corner&1)?r.width-1:0),r.y+((corner&2)?r.height-1:0));
   assert(!saved&&!bookmarked);
   if(i==0)assert(redraw==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_STATS);
   if(i==1)assert(redraw==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_BOOKMARKS&&!s_bookmark_page&&!s_bookmark_edit&&!s_bookmark_delete_confirm&&!s_bookmark_delete_error&&!selection_present);
  }
  const int gaps[][2]={{35,r.y},{648,r.y},{36,r.y-1},{36,r.y+r.height}};
  for(unsigned point=0;point<sizeof(gaps)/sizeof(gaps[0]);++point){
   reset();s_reader_panel=READER_PANEL_MORE;
   assert(tap(&ctx,gaps[point][0],gaps[point][1])==APP_REDRAW_NONE&&s_reader_panel==READER_PANEL_MORE&&!invalidated);
  }
 }
 reset();s_reader_panel=READER_PANEL_MORE;tap(&ctx,64,861);assert(s_reader_panel==READER_PANEL_TOOLS&&invalidated==1);
 reset();s_reader_panel=READER_PANEL_MORE;assert(tap(&ctx,342,1170)==APP_REDRAW_NONE&&s_reader_panel==READER_PANEL_MORE&&!invalidated);
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

# 真实章节快捷跳转回归：覆盖目录项（含 TXT 偏移/EPUB 锚点）、无目录回退物理章、首末边界与加载失败恢复。
# Exercise real chapter jump resolution, title normalization, boundary disabling, and load-failure handling.
jump_unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {bool user_activity;int64_t now_ms;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
enum {SHELF,READING,READER_PANEL_NONE,READER_PANEL_TOOLS};
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
static int s_view=READING,s_reader_panel=READER_PANEL_TOOLS;
static const char *s_text="body";
static bool s_clear_confirm,s_reader_chrome_fp_valid,s_reader_footer_pending,s_reader_image_refresh_pending;
static bool s_reader_footer_strong,s_water_turn_pending,s_reader_turn_pending,s_reader_text_frame;
static bool s_reader_stable_frame,s_reader_tools_toggle;
static int64_t s_stats_activity_ms,s_reader_notice_until;
static size_t s_selected_toc;
static char s_reader_notice[128],s_message[128],s_chapter_heading_title[128];
static struct {
 struct {size_t chapter,page;} position;
 struct {char chapter_title[128];} book;
} s_reader_session;
typedef struct {size_t chapter,source_offset;const char *anchor;const char *title;} nav_entry_t;
static nav_entry_t nav_items[8];
static size_t nav_count,ch_count,cur_toc;
static char raw_ch_titles[8][64];
static unsigned saves,invalidates,repositions,loads,skips;
static int last_skip_dir;
static size_t loaded_chapter,loaded_source;
static const char *loaded_anchor;
static bool fail_load,drop_text_on_load,drop_text_on_skip;
static size_t book_chapter_count(void){return ch_count;}
static size_t book_navigation_count(void){return nav_count;}
static size_t current_toc_position(void){return cur_toc;}
static size_t book_navigation_chapter(size_t i){assert(i<nav_count);return nav_items[i].chapter;}
static const char *book_navigation_anchor(size_t i){assert(i<nav_count);return nav_items[i].anchor;}
static size_t book_navigation_source_offset(size_t i){assert(i<nav_count);return nav_items[i].source_offset;}
static esp_err_t book_navigation_title(size_t i,char *out,size_t cap){
 if(i>=nav_count||!nav_items[i].title)return ESP_FAIL;
 snprintf(out,cap,"%s",nav_items[i].title);return ESP_OK;
}
static esp_err_t book_chapter_title(size_t i,char *out,size_t cap){
 if(i>=ch_count||!raw_ch_titles[i][0])return ESP_FAIL;
 snprintf(out,cap,"%s",raw_ch_titles[i]);return ESP_OK;
}
static void copy_text(char *dst,size_t cap,const char *src){snprintf(dst,cap,"%s",src);}
static void save_progress(void){++saves;}
static void invalidate_prep(void){++invalidates;}
static void reader_excerpt_reposition(void){++repositions;}
static bool load_chapter_at(app_ctx_t *ctx,size_t chapter,size_t off,bool end,const char *anchor,size_t source){
 (void)ctx;assert(off==0&&!end);++loads;
 if(fail_load){if(drop_text_on_load)s_text=NULL;return false;}
 s_reader_session.position.chapter=chapter;s_reader_session.position.page=0;
 loaded_chapter=chapter;loaded_anchor=anchor;loaded_source=source;
 return true;
}
static void skip_hidden_image_pages(app_ctx_t *ctx,int dir){
 (void)ctx;++skips;last_skip_dir=dir;if(drop_text_on_skip)s_text=NULL;
}
'''
for name in ('reader_chapter_jump_target', 'reader_chapter_jump_available',
             'reader_chapter_bar_title', 'reader_jump_chapter'):
    jump_unit += function(name, BOOK) + '\n'
jump_unit += r'''
static void reset_jump(void){
 s_view=READING;s_reader_panel=READER_PANEL_TOOLS;s_text="body";s_clear_confirm=false;
 s_reader_chrome_fp_valid=true;s_reader_footer_pending=true;s_reader_image_refresh_pending=true;
 s_reader_footer_strong=s_water_turn_pending=s_reader_turn_pending=s_reader_text_frame=s_reader_stable_frame=true;
 s_reader_tools_toggle=true;s_stats_activity_ms=0;s_reader_notice_until=0;s_selected_toc=0;
 s_reader_notice[0]=s_message[0]=s_chapter_heading_title[0]=0;
 memset(&s_reader_session,0,sizeof(s_reader_session));
 saves=invalidates=repositions=loads=skips=0;last_skip_dir=0;
 loaded_chapter=SIZE_MAX;loaded_source=SIZE_MAX;loaded_anchor=NULL;
 fail_load=drop_text_on_load=drop_text_on_skip=false;
}
int main(void){
 app_ctx_t ctx={.now_ms=1000};
 // 1. 目录模式：含坏目录项跳过、EPUB 锚点、TXT 源偏移、扉页(SIZE_MAX)与首末边界。
 // 1. TOC mode: skip invalid targets, preserve EPUB anchors & TXT offsets, SIZE_MAX cover, boundaries.
 ch_count=4;nav_count=4;
 nav_items[0]=(nav_entry_t){0,120,NULL,"  第一章\n\t开端  "};
 nav_items[1]=(nav_entry_t){99,SIZE_MAX,NULL,"坏目录项"};
 nav_items[2]=(nav_entry_t){1,SIZE_MAX,"sec2","第二章 旅途"};
 nav_items[3]=(nav_entry_t){3,840,NULL,"第三章 尾声"};
 reset_jump();cur_toc=SIZE_MAX;
 assert(!reader_chapter_jump_available(0)&&reader_chapter_jump_available(1));
 assert(reader_jump_chapter(&ctx,-1)==APP_REDRAW_NONE&&!loads);
 assert(reader_jump_chapter(&ctx,1)==APP_REDRAW_PAGE&&loads==1&&loaded_chapter==0&&loaded_source==120&&loaded_anchor==NULL);
 assert(s_selected_toc==0&&!strcmp(s_reader_session.book.chapter_title,"  第一章\n\t开端  "));
 assert(skips==1&&last_skip_dir==1&&saves==2&&repositions==1&&!s_reader_tools_toggle&&s_reader_panel==READER_PANEL_TOOLS);
 cur_toc=0;
 char title[64];reader_chapter_bar_title(title,sizeof(title));
 assert(!strcmp(title,"第一章 开端"));
 assert(!reader_chapter_jump_available(0)&&reader_chapter_jump_available(1));
 // 跳过坏目录项 index 1，直接命中 index 2（EPUB 锚点）。/ Skip invalid entry 1 and hit entry 2 with EPUB anchor.
 assert(reader_jump_chapter(&ctx,1)==APP_REDRAW_PAGE&&loaded_chapter==1&&loaded_source==SIZE_MAX&&loaded_anchor&&!strcmp(loaded_anchor,"sec2")&&s_selected_toc==2);
 cur_toc=2;
 assert(reader_chapter_jump_available(0)&&reader_chapter_jump_available(1));
 // 上一章同样跳过坏目录项回到 index 0，并保持 skip_hidden_image_pages 正向(1)。
 // Previous chapter also skips invalid entry 1 back to index 0, with forward hidden-image skip.
 assert(reader_jump_chapter(&ctx,-1)==APP_REDRAW_PAGE&&loaded_chapter==0&&loaded_source==120&&last_skip_dir==1&&s_selected_toc==0);
 cur_toc=3;
 assert(reader_chapter_jump_available(0)&&!reader_chapter_jump_available(1));
 assert(reader_jump_chapter(&ctx,1)==APP_REDRAW_NONE);

 // 2. 无目录时退回物理章节（含单章禁止两端跳转）。
 // 2. Fallback to raw chapter indices when TOC is absent, including single-chapter disabling.
 nav_count=0;ch_count=1;reset_jump();
 assert(!reader_chapter_jump_available(0)&&!reader_chapter_jump_available(1));
 ch_count=3;strcpy(raw_ch_titles[1],"物理第二章");
 reset_jump();s_reader_session.position.chapter=1;
 reader_chapter_bar_title(title,sizeof(title));assert(!strcmp(title,"物理第二章"));
 assert(reader_chapter_jump_available(0)&&reader_chapter_jump_available(1));
 assert(reader_jump_chapter(&ctx,1)==APP_REDRAW_PAGE&&loaded_chapter==2&&loaded_source==SIZE_MAX&&!loaded_anchor);
 assert(reader_jump_chapter(&ctx,-1)==APP_REDRAW_PAGE&&loaded_chapter==1&&last_skip_dir==1);

 // 3. 加载失败：保留正文时提示错误，正文丢失时回书架并关面板。
 // 3. Load failure: toast error when body remains, return to shelf if body was lost.
 reset_jump();s_reader_session.position.chapter=1;fail_load=true;ctx.now_ms=3000;
 assert(reader_jump_chapter(&ctx,1)==APP_REDRAW_PAGE&&s_view==READING&&s_reader_panel==READER_PANEL_TOOLS);
 assert(strstr(s_reader_notice,"章节加载失败")&&s_reader_notice_until==5000&&!s_reader_tools_toggle);
 reset_jump();s_reader_session.position.chapter=1;fail_load=true;drop_text_on_load=true;
 assert(reader_jump_chapter(&ctx,1)==APP_REDRAW_PAGE&&s_view==SHELF&&s_reader_panel==READER_PANEL_NONE);
 reset_jump();s_reader_session.position.chapter=1;drop_text_on_skip=true;
 assert(reader_jump_chapter(&ctx,1)==APP_REDRAW_PAGE&&s_view==SHELF&&s_reader_panel==READER_PANEL_NONE);
 puts("PASS: floating chapter jump resolves TOC/anchors/offsets/raw chapters, trims title whitespace, honors boundaries and recovers from load failures");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    path = Path(folder)
    (path / 'jump_test.c').write_text(jump_unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'jump_test.c'), '-o', str(path / 'jump_test')], check=True)
    subprocess.run([str(path / 'jump_test')], check=True)
