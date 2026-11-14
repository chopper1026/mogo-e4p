"""真实翻页、重排与恢复回归，确保不跨过未读文字。/ Actual turns/reflow must not skip unread text.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int64_t now_ms;int leaf;bool user_activity;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE} app_redraw_t;
enum {READING,TOC,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS};
#define BOOK_TOC_ROWS 7
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 96
enum { MARGIN_LEFT, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOTTOM, MARGIN_COUNT };
static int s_margin[MARGIN_COUNT], s_line_spacing;
#define MODE_GL16 1
#define E0470_TURN_RTL 1
#define E0470_TURN_LTR -1
#define ESP_LOGI(...) ((void)0)
/* F4 触摸诊断埋点与题头重算：本测试不验证其行为，桩保持调用面即可。
   F4 touch-diag logging and the heading refresh are stubbed; this test never asserts them. */
#define TD_LOGI(...) ((void)0)
#define TD_TIME(tag, expr) ((void)(tag), (expr))
static char s_requested_open[288];
static bool s_requested_open_home;
static bool s_chrome_visible[2]={true,true};
static const char *s_text="text";
static size_t s_text_len=900,s_block_count,s_jump_offset;
static void *s_blocks;
static struct {bool active;} s_dictionary_selection;
static app_redraw_t dictionary_dismiss(void){s_dictionary_selection.active=false;return APP_REDRAW_PAGE;}
static int s_px=48,s_reader_panel=READER_PANEL_NONE,s_reader_slider=-1,s_view=READING;
#define s_toolbar (s_reader_panel!=READER_PANEL_NONE)
static bool s_clear_confirm,s_reader_text_frame,s_reader_turn_pending;
static bool s_reader_stable_frame,s_reader_footer_strong,s_excerpt_forward;
static bool s_water_turn_pending,s_reader_cleanup,draw_locked,load_failed;
static int s_water_turn_dir;
static unsigned s_stats_pending_turns,s_session_turns,s_turns,s_unsaved,journal_turns;
static void app_reading_journal_record(const char *path,const char *title,uint32_t size,unsigned chapter,uint32_t ms,uint32_t turns){
 assert(!strcmp(path,"/book.epub")&&!strcmp(title,"Book")&&size==500&&chapter==s_chapter&&!ms&&turns==1);journal_turns+=turns;
}
static int64_t s_last_turn_ms,s_stats_activity_ms;
static bool s_save_failed;
static unsigned layout_span=100,reflows,paints,saves,failed_reflows;
static uint8_t full_pages;
static const char *app_settings_book_background_path(void){return "";}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static void lock_draw(void){assert(!draw_locked);draw_locked=true;}
static void unlock_draw(void){assert(draw_locked);draw_locked=false;}
static void invalidate_prep(void){}
static bool book_file_path_supported(const char *path){return path&&*path;}
// 合并后的 toggle/turn 正文会调题头重算；本测试不验证它，桩为空并把依赖的全局补齐。
// The merged toggle/turn bodies call the heading refresh; this test never asserts it, so stub
// it out and provide the globals the body touches.
static void refresh_chapter_lead(void){}
static EpdRect body_rect(void){return(EpdRect){0,0,684,s_chrome_visible[0]?100:150};}
static size_t book_layout_page_start_offset(size_t page){return page*layout_span;}
static size_t book_layout_page_for_offset(size_t off){return off/layout_span;}
static size_t book_layout_page_count(void){return (s_text_len+layout_span-1)/layout_span;}
static bool book_layout_build_blocks(const char *text,size_t len,const void *blocks,size_t count,EpdRect r,int px){
 (void)text;(void)len;(void)blocks;(void)count;(void)px;assert(draw_locked);++reflows;
 if(failed_reflows){--failed_reflows;return false;}layout_span=r.height;return true;
}
static void save_progress(void){++saves;}
static unsigned excerpt_repositions;
static void reader_excerpt_reposition(void){++excerpt_repositions;}
static size_t book_chapter_count(void){return 2;}
static bool load_chapter(app_ctx_t *ctx,size_t chapter,int off,bool last){(void)ctx;(void)off;if(load_failed)return false;s_chapter=chapter;s_page=last?book_layout_page_count()-1:0;return true;}
static void set_reader_view(int view){s_view=view;}
static void skip_hidden_image_pages(app_ctx_t *ctx,int dir){(void)ctx;(void)dir;}
static uint8_t app_settings_reader_full_pages(void){return full_pages;}
static int effect=1;
static int app_settings_reader_turn_effect(void){return effect;}
static int test_images;
static int book_layout_page_image_count(size_t page){(void)page;return test_images;}
static app_redraw_t paint_reading(app_ctx_t *ctx,int mode){(void)ctx;assert(mode==MODE_GL16);++paints;return APP_REDRAW_AREA;}
'''
unit += r"""
static int indent=2,adjustment,layout_indent=2,layout_adjustment;
static int app_settings_book_indent(void){return indent;}
static int app_settings_book_indent_adjust(void){return adjustment;}
static void app_settings_set_book_indent(uint8_t em){indent=em;}
static void app_settings_set_book_indent_adjust(int8_t px){adjustment=px;}
static void book_layout_set_first_line_indent(unsigned em){assert(draw_locked);layout_indent=em;}
static void book_layout_set_first_line_indent_adjust(int px){assert(draw_locked);layout_adjustment=px;}
"""
unit += f'\n#include "{ROOT / "main/book/reader_turn.c"}"\n'
unit += function('reader_turn_save', BOOK) + '\n'
unit += function('reader_turn_load', BOOK) + '\n'
from reader_reflow_test_code import reflow_code
unit += reflow_code("indent")
unit += function('apply_reader_indent', BOOK) + '\n'
for name in ('app_book_request_open', 'app_book_request_open_from_home', 'app_book_request_resume',
             'reader_turn_chrome', 'turn_page'):
    if name == 'turn_page':
        unit += '#define turn_page actual_turn_page\n' + function(name, BOOK) + '\n#undef turn_page\n'
        unit += 'static app_redraw_t turn_page(app_ctx_t *ctx,int dir){app_redraw_t result=actual_turn_page(ctx,dir);assert(journal_turns==s_stats_pending_turns);return result;}\n'
    else:
        unit += function(name, BOOK) + '\n'
unit += r'''
static void reset(bool full,bool tools,size_t page,size_t chapter){
 s_text="text";s_view=READING;s_chrome_visible[0]=s_chrome_visible[1]=!full;layout_span=full?150:100;s_page=page;s_chapter=chapter;
 strcpy(s_reader_session.book.path,"/book.epub");strcpy(s_reader_session.book.title,"Book");s_reader_session.book.file_size=500;journal_turns=0;
 s_reader_panel=tools?READER_PANEL_TOOLS:READER_PANEL_NONE;s_reader_slider=2;
 reflows=paints=saves=s_stats_pending_turns=s_session_turns=s_turns=s_unsaved=excerpt_repositions=0;
 failed_reflows=0;load_failed=false;full_pages=0;s_reader_text_frame=s_reader_stable_frame=true;
 s_reader_turn_pending=s_water_turn_pending=s_reader_cleanup=false;s_jump_offset=0;
}
int main(void){
 app_ctx_t ctx={.now_ms=1000};
 reset(true,false,2,0);size_t anchor=book_layout_page_start_offset(s_page);
 assert(apply_reader_indent(&ctx,2,-7)==APP_REDRAW_PAGE&&adjustment==-7&&layout_adjustment==-7&&indent==2);
 assert(book_layout_page_start_offset(s_page)==anchor&&saves==1);
 assert(apply_reader_indent(&ctx,2,-7)==APP_REDRAW_NONE&&saves==1);
 failed_reflows=1;
 assert(apply_reader_indent(&ctx,3,12)==APP_REDRAW_NONE&&indent==2&&adjustment==-7&&layout_indent==2&&layout_adjustment==-7&&saves==1);
 assert(book_layout_page_start_offset(s_page)==anchor&&!draw_locked);
 assert(apply_reader_indent(&ctx,2,101)==APP_REDRAW_NONE&&saves==1);

 // Reopen/resume preserve their return origin; bar preferences are global and unchanged.
 for(int hidden=0;hidden<2;++hidden){
  s_chrome_visible[0]=!hidden;s_chrome_visible[1]=hidden;
  assert(app_book_request_open("/sdcard/book.epub")&&!s_requested_open_home);
  assert(app_book_request_resume("/sdcard/book.epub")&&s_requested_open_home);
  assert(app_book_request_open_from_home("/sdcard/other.epub")&&s_requested_open_home);
  assert(s_chrome_visible[0]==!hidden&&s_chrome_visible[1]==!!hidden);
 }
 assert(!app_book_request_open(NULL)&&!app_book_request_open(""));
 for(int full=0;full<2;++full)for(int tools=0;tools<2;++tools)for(int dir=-1;dir<=1;dir+=2){
  reset(full,tools,2,0);size_t destination=book_layout_page_start_offset(s_page+dir);
  app_redraw_t r=turn_page(&ctx,dir);
  assert(s_chrome_visible[0]==!full&&s_chrome_visible[1]==!full&&s_reader_panel==READER_PANEL_NONE&&s_reader_slider==-1);
  assert(s_stats_pending_turns==1&&s_session_turns==1&&paints==1);
  assert(s_excerpt_forward==(dir>0));
  assert(excerpt_repositions==(unsigned)(dir<0));
  assert(book_layout_page_start_offset(s_page)==destination);
  assert(!reflows&&r==APP_REDRAW_PAGE&&!s_water_turn_pending&&!s_reader_turn_pending);
 }
 // 普通文字恢复正式版的翻页与周期清屏标志。/ Restore release text-turn and periodic-cleanup state.
 effect=0;
 for(int tools=0;tools<2;++tools)for(int full=0;full<2;++full)for(int prior=0;prior<2;++prior){
  reset(full,tools,2,0);s_reader_text_frame=prior;full_pages=5;s_turns=4;
  assert(turn_page(&ctx,1)!=APP_REDRAW_NONE&&!s_water_turn_pending&&s_reader_cleanup);
 }
 reset(true,false,2,0);test_images=1;s_reader_slider=-1;assert(turn_page(&ctx,1)==APP_REDRAW_AREA);
 test_images=0;effect=1;
 // Mixed/image targets and leaving an image never run the text ripple waveform.
 reset(true,false,2,0);test_images=2;s_reader_slider=-1;assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_water_turn_pending&&!s_reader_turn_pending);
 reset(true,false,2,0);test_images=0;s_reader_text_frame=s_reader_stable_frame=false;s_reader_slider=-1;
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_water_turn_pending&&!s_reader_turn_pending);
 test_images=0;
 reset(false,true,0,0);assert(turn_page(&ctx,-1)==APP_REDRAW_PAGE);
 assert(s_chrome_visible[0]&&!s_toolbar&&s_page==0&&!paints&&!s_stats_pending_turns&&!excerpt_repositions);
 reset(false,true,8,1);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE);
 assert(s_chrome_visible[0]&&!s_toolbar&&s_page==8&&!s_stats_pending_turns);
 reset(true,true,5,1);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&!s_toolbar&&!reflows);
 reset(false,true,8,0);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&s_chapter==1&&s_page==0&&s_chrome_visible[0]&&!s_toolbar);
 reset(false,true,0,1);assert(turn_page(&ctx,-1)==APP_REDRAW_PAGE&&s_chapter==0&&s_page==8&&s_chrome_visible[0]&&!s_toolbar);
 assert(excerpt_repositions==1);
 reset(false,true,8,0);load_failed=true;assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&s_view==TOC&&!paints&&!s_stats_pending_turns);
 assert(!excerpt_repositions);
 reset(false,true,0,1);load_failed=true;assert(turn_page(&ctx,-1)==APP_REDRAW_PAGE&&s_view==TOC&&!paints&&!excerpt_repositions);
 reset(false,true,2,0);failed_reflows=1;assert(turn_page(&ctx,1)==APP_REDRAW_PAGE);
 assert(s_text&&s_chrome_visible[0]&&!s_toolbar&&layout_span==100&&s_page==3&&reflows==0);
 reset(false,true,2,0);failed_reflows=2;assert(turn_page(&ctx,1)==APP_REDRAW_PAGE);
 assert(s_text&&s_view==READING&&s_page==3&&reflows==0&&paints==1&&s_stats_pending_turns==1);
 puts("PASS: open/resume origin and independent visibility; anchored indentation; turns retain bar state, dismiss tools and preserve chapter/boundary positions");
}
'''
from reader_session_test_state import position_state
unit = position_state(page=0) + unit
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    c, exe = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined',str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
