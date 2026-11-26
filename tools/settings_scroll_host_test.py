"""中文：设置主列表跟手、采样合并、定点补白与子页边界回归。
English: Root list tracking, coalesced input, targeted cleanup and unchanged selector bounds.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
from refresh_test_support import area_boundary
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/apps/app_device_settings.c').read_text()

def block(text, start):
    at = text.index('{', start)
    depth, quote, comment, escape = 0, None, None, False
    while at < len(text):
        char, pair = text[at], text[at:at+2]
        if comment == 'line':
            if char == '\n': comment = None
        elif comment == 'block':
            if pair == '*/': comment = None; at += 1
        elif quote:
            if escape: escape = False
            elif char == '\\': escape = True
            elif char == quote: quote = None
        elif pair == '//': comment = 'line'; at += 1
        elif pair == '/*': comment = 'block'; at += 1
        elif char in "\"'": quote = char
        elif char == '{': depth += 1
        elif char == '}':
            depth -= 1
            if not depth: return text[start:at+1]
        at += 1
    raise AssertionError('Unclosed function')

def function(name):
    match = re.search(r'^static [^\n;{}]+\b'+name+r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    return block(source, match.start())

start = source.index('if (s_page == SETTINGS_MAIN) {', source.index('static app_redraw_t on_gesture'))
event_block = block(source, start)
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epdiy.h"
#include "app.h"
#define EXT_RAM_BSS_ATTR
#include "display_geometry.h"
#include "ui_gesture.h"
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {SETTINGS_MAIN,SETTINGS_UPGRADE,SETTINGS_SHELF_STYLE,
      SETTINGS_SYSTEM_FONT,SETTINGS_READING_BG,SETTINGS_AVATAR,SETTINGS_TEXT_EDIT,SETTINGS_PIN_ENTRY,
      SETTINGS_MEMORY};
enum EpdDrawMode {MODE_GC16,MODE_GL16,MODE_DU};
#include "ui_nav_layout.h"
#define UI_TOUCH_SLOP_PX 24
#define UI_LOCK_WIDTH 684
#define SETTINGS_SCROLL_MAX 833
typedef int EpdWaveform;
enum {APP_MAIN_REFRESH_NORMAL,APP_MAIN_REFRESH_FAST,APP_MAIN_REFRESH_WATER};
static const int E0470_WAVEFORM=0,E0470_FOLLOW_WAVEFORM=1,settings_soft=2;
static int s_page,s_main_scroll,selected_mode,paint_mode;
static int app_settings_main_refresh_mode(void){return selected_mode;}
static const EpdWaveform *display_main_normal_waveform(void){return &settings_soft;}
static bool s_scroll_present_pending,s_input_layout,s_input_settle;
static bool s_memory_present_pending;
static EpdRect s_input_area;
static int paints,pushes,row_taps,captures,cleans,presented,fail;
static int memory_paints;
static void memory_render_content(uint8_t *fb){(void)fb;++memory_paints;}
static EpdRect pushed;
static enum EpdDrawMode pushed_mode;
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++paints;paint_mode=selected_mode;}
static int update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){
 (void)hl;assert(wave==&E0470_WAVEFORM||wave==&E0470_FOLLOW_WAVEFORM||wave==&settings_soft);if(s_scroll_present_pending==false&&s_page!=SETTINGS_UPGRADE&&s_page!=SETTINGS_TEXT_EDIT&&s_page!=SETTINGS_PIN_ENTRY&&s_page!=SETTINGS_MEMORY)assert(wave==(selected_mode==0?&settings_soft:&E0470_WAVEFORM));++pushes;pushed=area;pushed_mode=mode;if(!fail)presented=s_main_scroll;return fail;
}
static int update_display_area_full_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){return update_display_area_diff_with(hl,wave,mode,area);}
// 主列表滚动走脏区膨胀版本；边界由 display 宿主测试覆盖，这里只转发。
// The main list scroll uses the dilated variant; the display host test covers its bounds, so forward here.
static int update_display_area_diff_padded(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area,int pad_y,int pad_x){(void)pad_y;(void)pad_x;return update_display_area_diff_with(hl,wave,mode,area);}
static void guard_draw_result(void *hl,int status){(void)hl;assert(status==fail);}
static bool display_present_begin(void){return true;}
static void display_present_end(bool held){assert(held);}
static void display_main_transition_cancel(void){}
static EpdRect ui_pinpad_full(void){return(EpdRect){0,0,684,1216};}
static EpdRect upgrade_progress_area(void){return(EpdRect){52,468,580,64};}
''' + '\nenum EpdDrawError {EPD_DRAW_SUCCESS};\n' + area_boundary(root)
unit += (root/'main/display/display_list.h').read_text().replace('#pragma once','').replace('#include "display_refresh.h"','')
unit += source[source.index('enum { LIST_TOP'):source.index('static void list_drag_reset')]
unit += r'''
static bool has_rules=true;
static void paint_main_list(uint8_t *fb){(void)fb;++paints;paint_mode=selected_mode;}
bool display_list_paint(EpdiyHighlevelState *hl,EpdRect a,void (*paint)(uint8_t *)){
 (void)hl;assert(a.y==160&&a.y+a.height==UI_NAV_TOP);paint(NULL);return true;}
bool display_list_capture_rules(const EpdiyHighlevelState *hl,EpdRect a,int l,int r,display_rule_span_t *rows){
 (void)hl;(void)a;assert(l==88&&r==596&&s_main_scroll==presented);++captures;
 if(has_rules)rows[presented%LIST_HEIGHT]=(display_rule_span_t){88,632};
 return has_rules;}
enum EpdDrawError display_list_clean_rules(EpdiyHighlevelState *hl,EpdRect a,const display_rule_span_t *rows){
 (void)hl;(void)a;bool any=false;for(int i=0;i<LIST_HEIGHT;++i)any|=rows[i].end!=0;assert(any);++cleans;return 0;}
'''
for name in ('list_drag_reset','list_drag_event','list_drag_tick','list_drag_present','memory_area_hint','settings_present'):
    unit += function(name)+'\n'
unit += '\nstatic app_redraw_t event(app_ctx_t *ctx,const ui_gesture_event_t *ev){(void)ctx;'+event_block+'if(ev->type==UI_GESTURE_TAP)++row_taps;return APP_REDRAW_NONE;}\n'
unit += r'''
static ui_gesture_t recognizer;
static app_touch_t touch;
static app_ctx_t ctx;
static app_redraw_t feed(int x,int y,bool down,bool press,int64_t now){
 touch=(app_touch_t){.count=down?1:0,.touched=down,.x=x,.y=y};
 ctx=(app_ctx_t){.touch=&touch,.pressed=press,.released=!down,.now_ms=now};
 ui_gesture_event_t gesture;
 if(!ui_gesture_feed_options(&recognizer,&ctx,&gesture,s_page==SETTINGS_MAIN))return APP_REDRAW_NONE;
 app_redraw_t redraw=event(&ctx,&gesture);
 if(redraw==APP_REDRAW_AREA)assert(settings_present(&ctx,redraw));
 return redraw;
}
static void reset(int page,int offset){
 s_page=page;presented=s_main_scroll=offset;list_drag_reset();s_scroll_present_pending=false;ui_gesture_reset(&recognizer);
}
static void tick(void){app_redraw_t redraw=list_drag_tick(&ctx);if(redraw==APP_REDRAW_AREA)assert(settings_present(&ctx,redraw));}
int main(void){
 for(int mode=0;mode<3;++mode){
  selected_mode=mode;reset(SETTINGS_MAIN,100);int before=pushes,taps=row_taps,c=cleans;
  feed(300,800,true,true,0);feed(300,776,true,false,5);tick();assert(pushes==before);
  feed(300,775,true,false,10);tick();assert(presented==125&&pushes==before+1&&paint_mode==mode&&pushed_mode==MODE_GL16);
  for(int y=770;y>=600;y-=10)feed(300,y,true,false,800-y);
  feed(300,740,true,false,205);assert(pushes==before+1);tick();assert(presented==160&&pushes==before+2);
  // 小于整划阈值的松手仍提交最后位置，不吞成轻点。/ A short lift commits the last position, not a tap.
  feed(300,750,false,false,220);assert(presented==150&&pushes==before+3&&row_taps==taps);
  #if CONFIG_SETTINGS_SCROLL_SKIP_CLEANUP
  tick();tick();tick();assert(cleans==c&&!captures&&pushes==before+3);
  #else
  assert(s_old_rules[100].end&&s_old_rules[125].end&&s_old_rules[160].end&&!s_old_rules[300].end);
  tick();assert(cleans==c);tick();tick();assert(cleans==c+1&&pushes==before+3);
  #endif
  feed(300,700,true,true,400);feed(300,700,false,false,420);assert(row_taps==taps+1);
  reset(SETTINGS_MAIN,10);feed(300,300,true,true,0);feed(300,410,true,false,20);tick();assert(presented==0);
  reset(SETTINGS_MAIN,800);feed(300,800,true,true,0);feed(300,200,false,false,20);assert(presented==833);
  c=cleans;feed(300,500,true,true,30);tick();feed(300,505,false,false,40);tick();assert(cleans==c);
  reset(SETTINGS_MAIN,100);before=pushes;feed(300,800,true,true,0);feed(460,680,true,false,20);feed(460,680,false,false,25);tick();assert(pushes==before);
  for(int y=150;y<=UI_NAV_TOUCH_TOP;y+=UI_NAV_TOUCH_TOP-150){reset(SETTINGS_MAIN,100);feed(300,y,true,true,0);feed(300,y-125,true,false,20);tick();assert(pushes==before);}
  reset(SETTINGS_MAIN,0);feed(300,800,true,true,0);feed(300,700,true,false,10);tick();feed(300,610,true,false,20);
  touch.count=2;ctx.pressed=false;ui_gesture_event_t canceled;assert(ui_gesture_feed(&recognizer,&ctx,&canceled)&&canceled.type==UI_GESTURE_CANCEL);
  event(&ctx,&canceled);tick();assert(presented==100&&cleans==c);
  reset(SETTINGS_MAIN,0);has_rules=false;feed(300,800,true,true,0);feed(300,500,false,false,10);tick();tick();assert(presented==300&&cleans==c);has_rules=true;
  reset(SETTINGS_MAIN,0);feed(300,800,true,true,0);feed(300,500,true,false,20);fail=1;tick();fail=0;tick();assert(presented==0&&s_main_scroll==0&&!s_drag.dragging);
  reset(SETTINGS_MAIN,0);feed(300,800,true,true,0);feed(300,500,false,false,20);assert(!settings_present(&ctx,APP_REDRAW_PAGE));tick();assert(cleans==c);
  reset(SETTINGS_MAIN,100);feed(300,800,true,true,0);feed(300,800,true,false,600);feed(300,760,true,false,700);tick();
  assert(s_main_scroll==140);feed(300,760,false,false,710);ctx.last_input_ms=720;tick();assert(!s_drag.dragging&&cleans==c);
  const int pages[]={SETTINGS_SHELF_STYLE,SETTINGS_SYSTEM_FONT,SETTINGS_READING_BG,SETTINGS_AVATAR};
  for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);++i){
   s_page=pages[i];s_scroll_present_pending=true;assert(settings_present(&ctx,APP_REDRAW_AREA));
   assert(pushed_mode==MODE_GL16&&pushed.y==(s_page==SETTINGS_SHELF_STYLE?242:190)&&pushed.y+pushed.height==UI_NAV_TOP);
  }
  s_page=SETTINGS_MEMORY;s_memory_present_pending=true;
  int memory_before=memory_paints;before=pushes;
  assert(settings_present(&ctx,APP_REDRAW_AREA)&&!s_memory_present_pending);
  assert(memory_paints==memory_before+1&&pushes==before+1&&pushed_mode==MODE_GL16);
  assert(pushed.y==218&&pushed.y+pushed.height==UI_NAV_REFRESH_END);
  assert(!settings_present(&ctx,APP_REDRAW_AREA)&&pushes==before+1);
 }
 s_page=SETTINGS_UPGRADE;assert(settings_present(&ctx,APP_REDRAW_AREA)&&pushed.y==468&&pushed.height==64);
 assert(!settings_present(&ctx,APP_REDRAW_NONE));
 puts("PASS: real gesture tracking, coalescing, reversal, final lift, bounded cleanup, cancellation, failure and unchanged subpage bounds");
}
'''
with tempfile.TemporaryDirectory(prefix='mogo-settings-scroll-') as directory:
    path=Path(directory)/'settings.c';binary=path.with_suffix('');path.write_text(unit)
    for skip_cleanup in (0, 1):
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                        f'-DCONFIG_SETTINGS_SCROLL_SKIP_CLEANUP={skip_cleanup}',
                        '-I'+str(root/'tools/ui_gesture_stubs'),'-I'+str(root/'main/ui'),'-I'+str(root/'main/display'),str(path),str(root/'main/ui/ui_gesture.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
