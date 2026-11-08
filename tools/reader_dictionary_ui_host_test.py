#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：运行真实面板编排，核对长按、释放、取消、关闭和迟到结果。
English: Run real panel orchestration for hold/release/cancel/dismiss and stale results.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1];BOOK=ROOT/'main/apps/app_book.c'
unit=r'''
#include <assert.h>
#define READER_WIDTH 684
#define READER_HEIGHT 1216
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_gesture.h"
#include "reader_selection.h"
#include "book_dictionary.h"
#include "ui_reader_dictionary.h"
#include "reader_session_test.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
enum {READING,SHELF,MODE_DU};
static int s_view=READING,s_mode;static const char *s_text="踽踽独行";static size_t s_text_len=12;
static bool s_toolbar,s_clear_confirm,storage;
static reader_selection_t s_dictionary_selection;static ui_reader_dictionary_t s_dictionary_view;
static book_dictionary_result_t s_dictionary_result;static size_t s_dictionary_history[128],s_dictionary_history_count;
static size_t s_dictionary_character,s_dictionary_chapter;static int64_t s_dictionary_feedback_ms;
static bool s_dictionary_feedback_pending,s_dictionary_presented;static EpdRect s_area;
static struct {struct {size_t page,chapter;}position;}s_reader_session;
static int64_t s_dictionary_notice_until;
static unsigned generation=1,queries,cancels,draws;static bool pending,ready,map_missing;
static book_layout_glyph_t glyphs[]={{0,3,10,20,30,40},{3,3,40,20,30,40},{6,3,70,20,30,40},{9,3,100,20,30,40}};
const book_layout_glyph_t *book_layout_glyphs(size_t page,size_t *n){(void)page;*n=4;return map_missing?NULL:glyphs;}
uint32_t book_layout_generation(void){return generation;}
static void reader_dictionary_cancel(void){++cancels;pending=false;}
static bool reader_dictionary_start(const char *q){assert(q&&*q);++queries;pending=true;return true;}
static bool reader_dictionary_poll(book_dictionary_result_t *r){if(!ready)return false;ready=false;pending=false;r->status=BOOK_DICTIONARY_OK;return true;}
static void middle_pending_clear(void){}
static void invalidate_prep(void){}
static void lock_draw(void){}
static void unlock_draw(void){}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static bool usb_storage_active(void){return storage;}
static const char *s_font_path="reader.ttf",*active_font="reader.ttf";
static const char *ttf_font_path(void){return active_font;}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++draws;}
EpdRect ui_reader_dictionary_selection(uint8_t *fb,const reader_selection_t *s,const book_layout_glyph_t *g,size_t n){(void)fb;uint32_t a,b;assert(reader_selection_range(s,g,n,&a,&b));return(EpdRect){10,20,120,40};}
int ui_reader_dictionary_action(const ui_reader_dictionary_t *v,int x,int y){(void)x;return y<v->top?0:-1;}
size_t ui_reader_dictionary_page_end(const book_dictionary_result_t *r,size_t off){(void)r;return off;}
size_t ui_reader_dictionary_content_size(const book_dictionary_result_t *r){return strlen(r->definition);}
static app_redraw_t dictionary_dismiss(void);
'''
# 真实 app_ctx_t 的轻量宿主契约。/ Lightweight host contract for the actual page context.
unit=unit.replace('#include "reader_session_test.h"','')
for name in ('dictionary_cancel','dictionary_dismiss','dictionary_long_drag','dictionary_query','dictionary_union','dictionary_handle','dictionary_gesture','dictionary_tick'):
    unit+=function(name,BOOK)+'\n'
unit+=r'''
static uint8_t fb[684*1216/2];static app_touch_t touch;
static app_ctx_t ctx;
static app_redraw_t event(ui_gesture_type_t type,int x,int y){ui_gesture_event_t e={.type=type,.x0=15,.y0=30,.x=x,.y=y};bool handled=false;return dictionary_gesture(&ctx,&e,&handled);}
int main(void){
 ctx.fb=fb;ctx.touch=&touch;ctx.now_ms=500;memset(fb,255,sizeof(fb));
 assert(dictionary_long_drag(&ctx));s_toolbar=true;assert(!dictionary_long_drag(&ctx));s_toolbar=false;
 unsigned before=queries;assert(event(UI_GESTURE_LONG_PRESS,15,30)==APP_REDRAW_AREA);
 assert(s_dictionary_selection.active&&s_dictionary_selection.dragging&&queries==before);
 ctx.now_ms=650;assert(event(UI_GESTURE_MOVE,115,30)==APP_REDRAW_AREA);assert(!strcmp(s_dictionary_selection.query,"踽踽独行")&&queries==before);
 assert(event(UI_GESTURE_RELEASE,115,30)==APP_REDRAW_PAGE&&queries==before+1&&s_dictionary_view.open);
 assert(event(UI_GESTURE_RELEASE,115,30)==APP_REDRAW_NONE&&queries==before+1);
 ready=true;assert(dictionary_tick(&ctx)==APP_REDRAW_PAGE&&!s_dictionary_view.loading);
 assert(event(UI_GESTURE_SWIPE_L,15,30)==APP_REDRAW_PAGE&&!s_dictionary_selection.active);
 assert(event(UI_GESTURE_LONG_PRESS,15,30)==APP_REDRAW_AREA);assert(event(UI_GESTURE_RELEASE,15,30)==APP_REDRAW_PAGE);
 assert(event(UI_GESTURE_TAP,15,30)==APP_REDRAW_PAGE&&!s_dictionary_selection.active);
 assert(event(UI_GESTURE_LONG_PRESS,15,30)==APP_REDRAW_AREA);before=queries;
 assert(event(UI_GESTURE_CANCEL,15,30)==APP_REDRAW_AREA&&!s_dictionary_selection.active&&queries==before);
 assert(event(UI_GESTURE_LONG_PRESS,15,30)==APP_REDRAW_AREA);assert(event(UI_GESTURE_RELEASE,15,30)==APP_REDRAW_PAGE);
 ++generation;ready=true;assert(dictionary_tick(&ctx)==APP_REDRAW_PAGE&&!s_dictionary_selection.active&&!pending);
 --generation;assert(event(UI_GESTURE_LONG_PRESS,15,30)==APP_REDRAW_AREA);storage=true;
 assert(dictionary_tick(&ctx)==APP_REDRAW_PAGE&&!s_dictionary_selection.active);storage=false;
 assert(event(UI_GESTURE_LONG_PRESS,15,30)==APP_REDRAW_AREA);active_font="changed.ttf";
 assert(dictionary_tick(&ctx)==APP_REDRAW_PAGE&&!s_dictionary_selection.active);active_font="reader.ttf";
 // 空白长按没有目录副作用。/ A blank hold has no TOC side effect.
 ui_gesture_event_t blank={.type=UI_GESTURE_LONG_PRESS,.x0=400,.y0=400,.x=400,.y=400};bool handled=false;
 assert(dictionary_gesture(&ctx,&blank,&handled)==APP_REDRAW_NONE&&handled&&!s_dictionary_selection.active);
 // 失败提示等到抬手后才清除，保持长拖期间不整页推屏。/ Defer notice dismissal until release, without page output during a held contact.
 map_missing=true;ctx.now_ms=1000;touch.touched=true;
 assert(event(UI_GESTURE_LONG_PRESS,15,30)==APP_REDRAW_AREA&&s_dictionary_notice_until==3400);
 ctx.now_ms=3500;assert(dictionary_tick(&ctx)==APP_REDRAW_NONE&&s_dictionary_notice_until);
 touch.touched=false;assert(dictionary_tick(&ctx)==APP_REDRAW_PAGE&&!s_dictionary_notice_until);map_missing=false;
 // 已显示的字典在端点拖动取消后，等待抬手才补归一页请求。
 // A displayed dictionary waits for release before normalizing a cancelled endpoint drag.
 s_dictionary_presented=true;touch.touched=true;
 assert(dictionary_tick(&ctx)==APP_REDRAW_NONE);
 s_dictionary_selection.dragging=true;touch.touched=false;assert(dictionary_tick(&ctx)==APP_REDRAW_NONE);
 s_dictionary_selection.dragging=false;assert(dictionary_tick(&ctx)==APP_REDRAW_PAGE);
 s_dictionary_presented=false;
 assert(event(UI_GESTURE_LONG_PRESS,15,30)==APP_REDRAW_AREA);assert(event(UI_GESTURE_RELEASE,15,30)==APP_REDRAW_PAGE);
 s_dictionary_view.loading=false;s_dictionary_view.can_browse=false;s_dictionary_view.top=0;
 before=queries;assert(event(UI_GESTURE_TAP,15,30)==APP_REDRAW_NONE&&queries==before);
 assert(dictionary_dismiss()==APP_REDRAW_PAGE&&!s_dictionary_selection.active);
 puts("PASS: actual reader orchestration, AREA drag, one query per release, cancel/no query, modal dismissal, stale layout and USB invalidation");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    work=Path(folder);(work/'test.c').write_text(unit)
    # 仅补齐测试上下文，不替换生产编排逻辑。/ Complete the host context without replacing production orchestration.
    header=(ROOT/'tools/ui_gesture_stubs/app.h').read_text()
    header+='\ntypedef enum { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE } app_redraw_t;\n'
    (work/'app.h').write_text(header)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections',
        '-I'+str(work),'-I'+str(ROOT/'tools/ui_gesture_stubs'),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main/services'),'-I'+str(ROOT/'tools/book_layout_stubs'),'-I'+str(ROOT/'tools/book_source_host_stubs'),
        str(work/'test.c'),str(ROOT/'main/book/reader_selection.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
