#!/usr/bin/env python3
"""中文：分类真实控制器、键盘、内置字体及4bpp截图。/ English: Real category controller, keyboard, builtin font and 4bpp screenshots."""
import argparse
import platform
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image
import reader_title_style_preview as host
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "main/ui/ui_kit.c"
BOOK = ROOT / "main/apps/app_book.c"
NAV = ROOT / "main/ui/ui_nav.c"
SETTINGS = ROOT / "main/apps/app_device_settings.c"


def adapter():
    notepad = (ROOT / "main/ui/ui_shelf_notepad.c").read_text().replace(
        '#include "ui_shelf_notepad.h"',
        'enum { UI_NOTEPAD_ROWS = 10, UI_NOTEPAD_INK_STEPS = 3 };\n'
        'void ui_notepad_line(uint8_t *,int,bool,bool,bool,bool,const char *,const char *,unsigned);')
    code = notepad + r'''
typedef enum { SHELF, MANAGE, BULK, CATEGORIES, EDIT, SEARCH, IMPORT } book_view_t;
typedef book_shelf_entry_t shelf_entry_t;
static book_category_ui_t s_category_ui;
static book_shelf_categories_t s_shelf_categories;
static book_collection_t s_collection;
static uint32_t s_collection_bound_generation,s_collection_backup_revision,s_store_revision;
static book_collection_t s_legacy_collection;
static bool s_folder_ready=true,s_shelf_cache_valid,s_save_failed,physical;
static book_folder_plan_t s_folder_preview;
static struct {struct {char path[288];}book;} s_reader_session;
static const char *s_folder_old,*s_folder_next;
static char test_root[288];
static void retry_progress(void);
static bool app_reading_journal_poll(void){return false;}
static bool app_reading_journal_flush(void){return true;}
unsigned book_store_revision(void){return s_store_revision;}
esp_err_t book_path_state_prepare(const char *a,const char *b){(void)a;(void)b;return ESP_OK;}
esp_err_t book_path_state_relocate(const char *a,const char *b,uint32_t z,book_path_state_extra_fn extra){return extra(a,b,z);}

static bool binding_fail;
static book_view_t s_view,s_category_parent;
static bool s_category_bulk,s_scan_pending;
static int s_pressed_control,s_count,s_visible_count,s_presented_view=-1;
static shelf_entry_t s_shelf[20],s_managed;
static char s_batch_message[128],s_cache_library[40]="card-a";
static bool pending,usb_active,sd_present=true,sd_mounted=true;
static char sd_identity[40]="card-a";
typedef struct {bool present,mounted;char identity[40];} board_sd_info_t;
static void board_sd_get_info(board_sd_info_t *sd){sd->present=sd_present;sd->mounted=sd_mounted;copy(sd->identity,40,sd_identity);}
static bool usb_storage_active(void){return usb_active;}
static bool test_pending(const char *root){(void)root;return pending;}
static const char *app_settings_books_dir(void){return physical?test_root:"/sdcard/books";}
static int shelf_style;
static unsigned shelf_titles;
static int app_settings_shelf_style(void){return shelf_style;}
static bool app_settings_shelf_show_titles(uint8_t style){return style==1?shelf_titles&1:style==2?shelf_titles&2:false;}
static uint8_t app_settings_system_contrast(void){return 100;}
#define APP_SHELF_STYLE_NOTEPAD 6
static int shelf_rows(void){return shelf_style==APP_SHELF_STYLE_NOTEPAD?UI_NOTEPAD_ROWS:shelf_style==5?4:9;}
#include "book_shelf_covers.h"
#include "book_cover_auto.h"
#include "ui_image_dither.h"
static book_shelf_covers_t s_shelf_covers,s_category_covers;
static uint32_t s_category_cover_key;
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
static book_cover_job_t cover_request;
unsigned book_cover_jobs_begin(void){static unsigned generation;return ++generation;}
void book_cover_jobs_cancel(unsigned generation){(void)generation;}
bool book_cover_jobs_submit(unsigned generation,unsigned row,const book_cover_job_t *request){(void)generation;(void)row;cover_request=*request;return true;}
int book_cover_jobs_take(unsigned generation,unsigned row,book_cover_result_t *result){
    (void)generation;(void)row;unsigned w=BOOK_COVER_W,h=BOOK_COVER_H;
    result->pixels=malloc(w*h);assert(result->pixels);
    assert(book_auto_cover_render(cover_request.path,cover_request.title,cover_request.author,w,h,result->pixels));
    result->width=w;result->height=h;return 1;
}
static bool category_prepare_covers(app_ctx_t *ctx){(void)ctx;return false;}
static void copy_text(char *out,size_t cap,const char *s){copy(out,cap,s);}
static void clear_selection(void){book_shelf_clear_selection(s_shelf,s_count);}
static unsigned selected_count(void){return book_shelf_selected_count(s_shelf,s_count);}
static bool s_recent_sort;
static book_shelf_query_t shelf_query(void){return book_shelf_categories_query(&s_shelf_categories,0,s_recent_sort);}
static void invalidate_covers(void){book_shelf_covers_cancel(&s_shelf_covers);book_shelf_covers_release(&s_shelf_covers);}
static bool shelf_collection_load(bool retry){(void)retry;return s_collection.ready;}
static void shelf_category_selection_save(void){}
static bool shelf_collection_sync(bool complete){(void)complete;if(binding_fail)return false;
    bool ok=book_shelf_categories_bind(&s_shelf_categories,&s_collection);if(ok)s_collection_bound_generation=s_collection.generation;return ok;}
static void shelf_categories_recount(void){book_collection_apply(&s_collection,s_shelf,s_count);book_shelf_categories_recount(&s_shelf_categories,s_shelf,s_count);}
static bool transfer_idle=true,cover_idle=true;
static esp_err_t recovery_error=ESP_OK;
static unsigned recovery_calls,recovery_invalidations;
static bool network_service_stop_local_wifi(void){return transfer_idle;}
static void cancel_auto_resume(void){}
bool book_cover_jobs_quiesce(unsigned timeout){assert(timeout==2000);return cover_idle;}
static esp_err_t category_extra_state(const char *old,const char *next,uint32_t size){(void)old;(void)next;(void)size;return ESP_OK;}
static esp_err_t test_recover(const char *root,esp_err_t (*extra)(const char *,const char *,uint32_t)){
    assert(!strcmp(root,"/sdcard/books")&&extra==category_extra_state);++recovery_calls;if(recovery_error==ESP_OK)pending=false;return recovery_error;
}
static void pending_drop_invalidated(void){++recovery_invalidations;}
void book_store_notify_changed(void){++recovery_invalidations;}
static void book_lock_collage_invalidate(void){++recovery_invalidations;}
static bool legacy_category_recover(void);
static void scan_shelf(app_ctx_t *ctx){(void)ctx;
 if(!physical){assert(shelf_collection_sync(false));shelf_categories_recount();return;}
 book_shelf_categories_t scan={0};book_shelf_categories_init(&scan,test_root);s_count=0;
 DIR *d=opendir(test_root);assert(d);struct dirent *e;
 while((e=readdir(d))){if(e->d_name[0]=='.')continue;char path[288];snprintf(path,sizeof(path),"%s/%s",test_root,e->d_name);
 struct stat st;assert(!stat(path,&st));if(S_ISREG(st.st_mode)){copy(s_shelf[s_count++].path,288,path);continue;}
 book_shelf_categories_add(&scan,path);DIR *sub=opendir(path);assert(sub);struct dirent *f;
 while((f=readdir(sub)))if(f->d_name[0]!='.')snprintf(s_shelf[s_count++].path,288,"%s/%s",path,f->d_name);closedir(sub);}
 closedir(d);const char *names[20];int n=0;for(int i=2;i<scan.count;++i)names[n++]=book_shelf_categories_name(&scan,i);
 assert(!book_collection_folders(&s_collection,names,n,s_folder_old,s_folder_next));
 assert(book_shelf_categories_publish(&s_shelf_categories,&scan,&s_collection));book_shelf_categories_dispose(&scan);
 for(int i=0;i<s_count;++i){s_shelf[i].selected=false;s_shelf[i].category_id=book_shelf_folder_id(&s_shelf_categories,s_shelf[i].path);}
}
static esp_err_t category_check_extra(const char *a,const char *b,uint32_t z){(void)a;(void)b;(void)z;return ESP_OK;}
#define book_category_pending test_pending
#define book_category_recover test_recover
static void search_begin(void){s_view=SEARCH;}
static void category_begin(app_ctx_t *,bool,bool);
static void category_start_books(app_ctx_t *,uint32_t,bool);
static void category_close(app_ctx_t *);
enum { MODE_DU=1, MODE_GL16=5 };
enum { DISPLAY_REFRESH_DEFAULT, DISPLAY_REFRESH_FEEDBACK, DISPLAY_REFRESH_DETAIL, DISPLAY_REFRESH_SETTLE };
static EpdRect s_area;
static int s_mode,s_du_count;
static bool s_input_settle,s_reader_tools_toggle,s_shelf_key_paint_pending;
// 分类和书架保持竖屏，宿主夹具不触发阅读方向切换。/ Category and shelf fixtures stay portrait without reader rotation.
static bool reader_orientation_sync(void){return false;}
static bool s_dictionary_feedback_pending;
static app_redraw_t dictionary_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev,bool *handled){
    (void)ctx;(void)ev;*handled=false;return APP_REDRAW_NONE;
}
static bool s_shelf_key_mode;
static int s_shelf_key_pressed;
static unsigned input_refreshes,ink_errors,opened_after_refreshes;
enum EpdDrawError {EPD_DRAW_SUCCESS,TEST_DRAW_ERROR};
static bool fail_ink,in_ink_group;
static bool display_present_begin(void){assert(!in_ink_group);in_ink_group=true;return true;}
static void display_present_end(bool held){assert(held&&in_ink_group);in_ink_group=false;}
static int input_intent;
static bool reader_indent_present(app_ctx_t *ctx,app_redraw_t redraw){(void)ctx;(void)redraw;return false;}
static enum EpdDrawError display_refresh_area(display_state_t *hl,int intent,EpdRect area,unsigned flags){
    (void)hl;assert(flags==DISPLAY_REFRESH_DEFAULT);assert(area.width&&area.height);
    if(hl)assert(in_ink_group&&intent==DISPLAY_REFRESH_FEEDBACK);
    ++input_refreshes;input_intent=intent;return fail_ink?TEST_DRAW_ERROR:EPD_DRAW_SUCCESS;
}
static void guard_draw_result(display_state_t *hl,int error){(void)hl;if(error){assert(fail_ink);++ink_errors;}}
'''
    for name in ("sort_shelf", "legacy_category_recover", "category_cover_draw", "shelf_layout", "shelf_category_remember", "shelf_category_restore", "category_reset", "category_begin",
                 "category_start_books", "category_select", "category_close", "category_back",
                 "category_input_paint", "category_gesture", "category_storage_ready", "category_error", "category_plan", "category_refresh_catalog", "category_tick"):
        code += host.function(name, ROOT / "main/apps/app_book.c") + "\n"
    # 真实输入呈现出口须在通用阅读出口之前接住分类键盘。/ The real input-present branch must catch category keys before reader routing.
    prefix = host.function("present", ROOT / "main/apps/app_book.c").split(
        "    if (redraw == APP_REDRAW_NONE", 1)[0]
    code += prefix.replace("static bool present(", "static bool input_present(", 1) + "return false; }\n"
    return code + r'''
static void clean_library(book_collection_t *s){
    char prefix[288];copy(prefix,sizeof(prefix),s->prefix);book_collection_close(s);
    for(int i=0;i<2;++i){char path[300];snprintf(path,sizeof(path),"%s.%d",prefix,i);unlink(path);}
}
static uint32_t new_category(book_collection_t *s,const char *name){uint32_t id=0;unsigned n=0;assert(book_collection_create(s,name,NULL,0,false,&id,&n)==ESP_OK);return id;}
static void adapter_setup(const char *directory){
    category_reset();book_shelf_categories_dispose(&s_shelf_categories);
    if(s_collection.ready)clean_library(&s_collection);
    char prefix[288];snprintf(prefix,sizeof(prefix),"%s/adapter",directory);
    assert(book_collection_open(&s_collection,prefix)==ESP_OK);
    memset(s_shelf,0,sizeof(s_shelf));s_count=3;
    for(int i=0;i<s_count;++i){snprintf(s_shelf[i].path,288,"/sdcard/books/book%d.txt",i);snprintf(s_shelf[i].name,256,"图书 %d",i);s_shelf[i].search_match=true;}
    assert(book_collection_sync(&s_collection,"/sdcard/books",NULL,0,s_shelf,s_count,true)==ESP_OK);
    (void)new_category(&s_collection,"已有分类");
    book_shelf_categories_init(&s_shelf_categories,"/sdcard/books");assert(shelf_collection_sync(false));shelf_categories_recount();
    pending=usb_active=s_shelf_selecting=s_recent_sort=false;sd_present=sd_mounted=true;shelf_style=0;copy(sd_identity,40,"card-a");
    s_view=SHELF;s_managed=s_shelf[0];
}
static void adapter_keyboard_click(app_ctx_t *ctx,int x,int y){
    ui_gesture_t gesture={0};ui_gesture_event_t e;
    app_touch_t touch={.touched=true,.count=1,.x=x,.y=y};
    ctx->touch=&touch;ctx->pressed=true;ctx->released=ctx->consumed=false;
    assert(ui_gesture_feed(&gesture,ctx,&e)&&e.type==UI_GESTURE_PRESS);
    unsigned before=input_refreshes;app_redraw_t redraw=category_gesture(ctx,&e);
    assert(redraw==APP_REDRAW_AREA&&input_present(ctx,redraw));
    assert(input_refreshes==before+1&&input_intent==DISPLAY_REFRESH_FEEDBACK);
    ctx->consumed=true;assert(category_tick(ctx)==APP_REDRAW_NONE);
    touch.touched=false;touch.count=0;ctx->pressed=ctx->consumed=false;ctx->released=true;ctx->now_ms+=40;
    assert(ui_gesture_feed(&gesture,ctx,&e)&&e.type==UI_GESTURE_TAP&&!gesture.active);
    redraw=category_gesture(ctx,&e);
    assert(redraw==APP_REDRAW_AREA&&input_present(ctx,redraw));
    assert(input_refreshes==before+2);
    ctx->touch=NULL;ctx->released=false;ctx->now_ms+=40;
}
static void adapter_keyboard_checks(uint8_t *fb){
    app_ctx_t ctx={.fb=fb,.now_ms=1000};
    book_shelf_categories_init(&s_shelf_categories,"/sdcard/books");
    for(int rename=0;rename<2;++rename){
        s_view=SHELF;category_begin(&ctx,false,false);
        if(rename){s_category_ui.source=book_collection_at(&s_collection,0)->id;s_category_ui.screen=CATEGORY_ACTIONS;}
        ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=rename?200:620,.y0=rename?488:108};
        assert(category_gesture(&ctx,&e)==APP_REDRAW_PAGE&&s_category_ui.screen==CATEGORY_EDIT);
        if(rename){s_category_ui.draft[0]=0;ui_text_edit_place(&s_category_ui.input,0);}
        adapter_keyboard_click(&ctx,60,1026);assert(input_intent==DISPLAY_REFRESH_DETAIL);
        adapter_keyboard_click(&ctx,52,774);assert(!strcmp(s_category_ui.draft,"1"));
        adapter_keyboard_click(&ctx,628,930);assert(!s_category_ui.draft[0]);
        adapter_keyboard_click(&ctx,60,1026);assert(input_intent==DISPLAY_REFRESH_DETAIL);
        adapter_keyboard_click(&ctx,486,852);adapter_keyboard_click(&ctx,186,852);
        assert(ui_keyboard_pending());adapter_keyboard_click(&ctx,316,1026);
        assert(s_category_ui.draft[0]&&!ui_keyboard_pending());
        adapter_keyboard_click(&ctx,248,582);assert(input_intent==DISPLAY_REFRESH_DETAIL);
        adapter_keyboard_click(&ctx,480,1026);assert(input_intent==DISPLAY_REFRESH_DETAIL);
        adapter_keyboard_click(&ctx,52,774);
        assert(s_category_ui.draft[strlen(s_category_ui.draft)-1]=='q');
        s_input_settle=true;s_du_count=7;unsigned before=input_refreshes;
        assert(input_present(&ctx,APP_REDRAW_AREA));
        assert(input_intent==DISPLAY_REFRESH_SETTLE&&!s_input_settle&&!s_du_count&&input_refreshes==before+1);
        category_reset();
    }
    assert(!input_present(&ctx,APP_REDRAW_PAGE));
    for(int view=EDIT;view<=SEARCH;++view){
        s_view=view;s_mode=MODE_DU;assert(input_present(&ctx,APP_REDRAW_AREA));
        assert(input_intent==DISPLAY_REFRESH_FEEDBACK);
    }
    s_view=SHELF;assert(!input_present(&ctx,APP_REDRAW_AREA));
    book_shelf_categories_dispose(&s_shelf_categories);
    puts("category keyboard: PASS (actual create/rename gesture recognition, painting and presentation, numeric, Chinese and English input)");
}

static void adapter_checks(uint8_t *fb,const char *directory){
    app_ctx_t ctx={.fb=fb,.now_ms=1000};font_scale=100;adapter_setup(directory);
    adapter_keyboard_checks(fb);book_shelf_categories_init(&s_shelf_categories,"/sdcard/books");assert(shelf_collection_sync(false));
    book_shelf_categories_dispose(&s_shelf_categories);clean_library(&s_collection);physical=true;
    snprintf(test_root,sizeof(test_root),"%s/books",directory);assert(!mkdir(test_root,0700));char path[288],dst[288],prefix[288];
    snprintf(prefix,sizeof(prefix),"%s/prefs",directory);assert(!book_collection_open(&s_collection,prefix));
    snprintf(path,sizeof(path),"%s/test.epub",test_root);FILE *file=fopen(path,"wb");assert(file);fclose(file);
    book_shelf_categories_init(&s_shelf_categories,test_root);scan_shelf(&ctx);
    s_view=SHELF;category_begin(&ctx,false,false);book_category_ui_edit(&s_category_ui,&s_shelf_categories,false);
    copy(s_category_ui.draft,sizeof(s_category_ui.draft),"科幻");s_category_ui.command=CATEGORY_CREATE;
    app_touch_t held={.touched=true,.count=1};ctx.touch=&held;assert(category_tick(&ctx)==APP_REDRAW_NONE);ctx.touch=NULL;
    assert(category_tick(&ctx)==APP_REDRAW_PAGE && s_category_ui.screen==CATEGORY_BOOKS);
    uint32_t target=s_category_ui.destination;assert(target);s_shelf[0].selected=true;
    snprintf(dst,sizeof(dst),"%s/科幻/test.epub",test_root);file=fopen(dst,"wb");assert(file);fclose(file);
    s_category_ui.command=CATEGORY_MOVE;category_tick(&ctx);assert(!access(path,F_OK)&&!access(dst,F_OK)&&s_shelf[0].selected);
    assert(s_category_ui.screen==CATEGORY_PREVIEW && s_category_ui.conflict_preview && s_folder_preview.error);
    assert(!book_category_ui_back(&s_category_ui) && s_category_ui.screen==CATEGORY_BOOKS && s_shelf[0].selected);
    assert(!unlink(dst));s_category_ui.command=CATEGORY_MOVE;category_tick(&ctx);assert(access(path,F_OK)&&!access(dst,F_OK)&&s_view==SHELF);
    category_begin(&ctx,false,false);s_category_ui.source=target;s_category_ui.command=CATEGORY_PREVIEW_DELETE;category_tick(&ctx);
    assert(s_category_ui.screen==CATEGORY_PREVIEW&&s_folder_preview.moves==1&&!access(dst,F_OK));s_category_ui.command=CATEGORY_REMOVE;category_tick(&ctx);
    assert(!access(path,F_OK)&&access(dst,F_OK)&&!book_collection_find(&s_collection,target));
    usb_active=true;assert(!category_storage_ready());usb_active=false;
    copy(sd_identity,40,"card-b");assert(!category_storage_ready());copy(sd_identity,40,"card-a");
    sd_mounted=false;assert(!category_storage_ready());sd_mounted=true;pending=true;assert(!category_storage_ready());pending=false;
    assert(category_storage_ready());s_category_ui.command=CATEGORY_REMOVE;book_category_ui_suspend(&s_category_ui);
    assert(s_category_ui.command==CATEGORY_IDLE);
    physical=false;recovery_invalidations=0;
    category_reset();book_shelf_categories_dispose(&s_shelf_categories);clean_library(&s_collection);
    puts("category adapter: PASS (real storage, create/pick/assign, rollback/retry, selection/create transaction, persistent unclassify/delete, card/USB guards)");
}
'''


def shelf_fixture():
    code = r'''
#define row_rect shelf_row_rect
#define BOOK_GRID_ROWS 9
#define BOOK_BULK_ROWS 6
#define SHELF_BOOK_LIFT_PX 16
#define SHELF_FURNITURE_BOTTOM 226
#define SHELF_LIST_BOUNCE_PX 8
#define SHELF_FAST_COVER_WIDTH 164
#define SHELF_FAST_COVER_STRIDE 21
#define SHELF_CATEGORY_ROWS BOOK_SHELF_CATEGORY_MENU_ROWS
#define UI_NAV_TAB_ICON_PX 44
enum {APP_MAIN_REFRESH_NORMAL};
static int app_settings_main_refresh_mode(void){return APP_MAIN_REFRESH_NORMAL;}
static bool shelf_fast;
static bool app_settings_main_fast_refresh(void){return shelf_fast;}
static void display_main_transition_gray_bands(const EpdRect *r,int n){assert(n==1&&r->y==(s_shelf_selecting?272:192));}
static void unlock_draw(void){}
static void render(app_ctx_t *,uint8_t *);
static bool fast_cover_matches(int row,EpdRect r){(void)row;(void)r;return false;}
static void draw_shelf_guard(uint8_t *fb,EpdRect r,int row){(void)row;ui_draw_acrylic_guard(fb,r);}
static int leaves(void){int rows=shelf_rows();return s_visible_count?1+(s_visible_count-1)/rows:1;}
static const char *shelf_category_name(int i){return book_shelf_categories_name(&s_shelf_categories,i);}
static int shelf_category_at(int n){return book_shelf_categories_at(&s_shelf_categories,n);}
static int shelf_category_available(void){return book_shelf_categories_available(&s_shelf_categories);}
static unsigned opened_books,requested_nav;
void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;requested_nav=(unsigned)tab+1;}
static char s_message[128],s_shelf_warning[128],s_manage_message[128],s_open_error[128];
static void draw_open_error(uint8_t *fb){(void)fb;assert(false);}
static app_redraw_t open_error_gesture(const ui_gesture_event_t *ev){(void)ev;assert(false);return APP_REDRAW_NONE;}
static bool s_save_failed,s_batch_confirm,s_file_removed,s_delete_confirm,s_clear_confirm;
enum { APP_READER_TAP_GRID=1 };
static int app_settings_reader_tap_mode(void){return 0;}
static EpdRect reader_area(void){return (EpdRect){0};}
static int book_reader_grid_tap_y_in_area(EpdRect a,EpdRect b,int y){(void)a;(void)b;return y;}
static void retry_progress(void){}
static bool open_book(app_ctx_t *ctx,const char *path){(void)ctx;assert(path[0]&&!in_ink_group);opened_after_refreshes=input_refreshes;++opened_books;return true;}
static bool s_shelf_page_pending;
static app_redraw_t shelf_turn_page(app_ctx_t *ctx,int delta){
    const int next=ctx->leaf+delta;
    if (!delta || next<0 || next>=leaves()) return APP_REDRAW_NONE;
    ctx->leaf=next;return APP_REDRAW_AREA;
}
'''
    for name in ("ui_content_width", "ui_text_box", "ui_pager_area", "ui_pager_rect", "ui_pager_hit", "ui_pager_target", "ui_pager_draw"):
        code += host.function(name, KIT) + "\n"
    code += host.function("book_cover_crop", ROOT / "main/image/image_decode.c") + "\n"
    code += host.function("icon", NAV) + host.function("ui_nav_draw", NAV) + host.function("ui_nav_hit", NAV)
    for name in ("fit_text", "fit_fixed_text", "shelf_label", "row_rect", "shelf_cover_image", "shelf_cover_frame_ready", "draw_favorite_icon",
                 "draw_shelf_favorite_icon", "cover_favorite_needs_white_edge", "draw_shelf_cover", "draw_list_title", "draw_list_row",
                 "draw_shelf_furniture", "draw_shelf_titles", "shelf_header_action_rect", "shelf_category_title_rect", "shelf_manage_rect", "shelf_import_rect", "draw_shelf_header_button", "draw_shelf_pager", "shelf_page_arrow_rect",
                 "shelf_category_box", "draw_category_menu", "shelf_category_gesture", "toggle_selection", "main_page_visible"):
        code += host.function(name, BOOK) + "\n"
    for name in ("settings_card", "shelf_style_card", "shelf_style_id", "shelf_style_name", "draw_compact_shelf_style"):
        code += host.function(name, SETTINGS) + "\n"
    tail = host.function("render", BOOK)
    tail = tail[tail.index("    ui_clear_page(fb);\n    // 留白保持纯白"):]
    code += "static void render(app_ctx_t *ctx,uint8_t *fb){\n" + tail + "\n"
    code += host.function("play_notepad_title_ink", BOOK) + "\n"
    action = host.function("action_at", BOOK)
    action = action[action.index("    if (s_view == SHELF) {\n        if(s_shelf_selecting"):]
    code += "static app_redraw_t action_at(app_ctx_t *ctx,uint16_t x,uint16_t y){\n" + action + "\n"
    control = host.function("control_at", BOOK)
    guards = control[control.index("\n") + 1:control.index("    if (s_view == BULK)")]
    control = control[control.index("    if (s_view == SHELF && ui_rect_hit(shelf_manage_rect()"):]
    code += "static int control_at(app_ctx_t *ctx,uint16_t x,uint16_t y,EpdRect *rect){\n" + guards + control + "\n"
    code += r'''
static app_redraw_t paint_control(app_ctx_t *ctx,EpdRect rect){(void)ctx;(void)rect;assert(false);return APP_REDRAW_NONE;}
static bool present(app_ctx_t *ctx,app_redraw_t redraw){assert(false);return input_present(ctx,redraw);}
#define READING 20
#define READER_PANEL_TOOLS 1
#define READER_BAR_TOP 1000
#define READER_WIDTH 684
static bool s_toolbar;
static int s_reader_panel;
static EpdRect body_rect(void){assert(false);return (EpdRect){0};}
static int book_reader_tap_x(EpdRect r,int w,int x,int y){(void)r;(void)w;(void)x;(void)y;assert(false);return -1;}
static bool app_settings_reader_vertical_turn(void){assert(false);return false;}
static int reader_swipe_direction(ui_gesture_type_t type,bool vertical){(void)type;(void)vertical;assert(false);return 0;}
static app_redraw_t turn_page(app_ctx_t *ctx,int d){(void)ctx;(void)d;assert(false);return APP_REDRAW_NONE;}
static app_redraw_t bulk_turn_page(app_ctx_t *ctx,int d){(void)ctx;(void)d;assert(false);return APP_REDRAW_NONE;}
'''
    code += host.function("press_control", BOOK) + "\n"
    gesture = host.function("gesture_event", BOOK)
    code += gesture.split("    bool indent_handled=false;", 1)[0]
    code += gesture[gesture.index("    EpdRect start_rect = {0}, end_rect = {0};"):] + "\n"
    return code + r'''
static void shelf_snapshot(app_ctx_t *ctx,const char *dir,const char *name){
    book_shelf_covers_cancel(&s_shelf_covers);book_shelf_covers_release(&s_shelf_covers);
    for(int row=0;shelf_style!=APP_SHELF_STYLE_NOTEPAD&&row<shelf_rows()&&ctx->leaf*shelf_rows()+row<s_visible_count;++row){
        int i=ctx->leaf*shelf_rows()+row;book_cover_job_t request={0};copy(request.path,288,s_shelf[i].path);copy(request.title,256,s_shelf[i].name);copy(request.author,128,s_shelf[i].author);
        book_shelf_covers_prepare(&s_shelf_covers,row,i,true,false,0,0,ctx->now_ms);
        assert(book_shelf_covers_collect(&s_shelf_covers,row,&request));
    }
    int page=ctx->leaf,selected=book_shelf_categories_selected(&s_shelf_categories);unsigned n=selected_count();render(ctx,ctx->fb);
    assert(ctx->leaf==page&&book_shelf_categories_selected(&s_shelf_categories)==selected&&selected_count()==n);
    if(!s_shelf_selecting&&shelf_style!=APP_SHELF_STYLE_NOTEPAD){
        for(int y=160;y<184;++y)for(int x=36;x<648;++x)assert(ui_read_pixel(ctx->fb,x,y)==15);
        for(int y=1056;y<1096;++y)for(int x=36;x<648;++x)assert(ui_read_pixel(ctx->fb,x,y)==15);
        EpdRect hit;
        assert(control_at(ctx,160,176,&hit)==-1&&control_at(ctx,516,168,&hit)==-1&&control_at(ctx,616,1082,&hit)==-1);
    }
    char path[1024];snprintf(path,sizeof(path),"%s/%s-%u.raw",dir,name,font_scale);save(path,ctx->fb);
}
static void check_category_outline(app_ctx_t *ctx){
    unsigned selected_rows=0;
    EpdRect box=shelf_category_box();
    for(int row=0;row<SHELF_CATEGORY_ROWS;++row){
        int index=shelf_category_at(book_shelf_categories_menu_page(&s_shelf_categories)*SHELF_CATEGORY_ROWS+row);
        if(index<0)break;
        int x=box.x+box.width/2,y=box.y+8+row*83;
        int edge=index==book_shelf_categories_selected(&s_shelf_categories)?0:15;
        selected_rows+=edge==0;
        assert(ui_read_pixel(ctx->fb,x,y)==edge&&ui_read_pixel(ctx->fb,x,y+1)==edge);
        assert(ui_read_pixel(ctx->fb,x,y+4)==15);
    }
    assert(selected_rows==1);
}
static app_redraw_t shelf_press_pager(app_ctx_t *ctx,int control){
    const EpdRect r=ui_pager_rect(control);
    return action_at(ctx,r.x+r.width/2,r.y+r.height/2);
}
static void notepad_paper_checks(const uint8_t *fb,bool fast){
    // 卷边接缝与各行留白保持纯白，纸边和虚线仍有对比。/ Keep the join and row gaps white, with contrast on edges and dashes.
    for(int x=46;x<637;++x)for(int y=260;y<=262;++y)
        assert(ui_read_pixel(fb,x,y)==15);
    for(int slot=0;slot<UI_NOTEPAD_ROWS;++slot){
        const EpdRect row=ui_notepad_row(slot,false);
        const int rule=row.y+row.height-1;
        for(int y=row.y;y<rule;++y)for(int x=380;x<390;++x)
            assert(ui_read_pixel(fb,x,y)==15);
        assert(ui_read_pixel(fb,row.x,row.y+row.height/2)==(fast?5:6));
        if(slot<UI_NOTEPAD_ROWS-1){
            assert(ui_read_pixel(fb,row.x+11,rule)==0);
            assert(ui_read_pixel(fb,row.x+20,rule)==15);
        }
    }
}
static void notepad_paging_checks(app_ctx_t *ctx){
    const size_t bytes=684*1216/2;
    uint8_t *first=malloc(bytes);assert(first);
    const int count=s_count,visible=s_visible_count;
    s_count=s_visible_count=20;ctx->leaf=0;
    for(int i=0;i<10;++i){
        s_shelf[i+10]=s_shelf[i];
        s_shelf[i+10].pct=(s_shelf[i].pct+50)%101;
    }
    assert(leaves()==2);
    render(ctx,ctx->fb);memcpy(first,ctx->fb,bytes);
    assert(shelf_press_pager(ctx,UI_PAGER_PREVIOUS)==APP_REDRAW_NONE&&ctx->leaf==0);
    assert(shelf_press_pager(ctx,UI_PAGER_NEXT)==APP_REDRAW_AREA&&ctx->leaf==1);
    render(ctx,ctx->fb);
    const EpdRect row=ui_notepad_row(0,false);
    unsigned changed=0;
    for(int x=568;x<628;++x)for(int y=row.y;y<row.y+row.height;++y)
        changed+=ui_read_pixel(first,x,y)!=ui_read_pixel(ctx->fb,x,y);
    assert(changed>0);
    assert(shelf_press_pager(ctx,UI_PAGER_NEXT)==APP_REDRAW_NONE&&ctx->leaf==1);
    assert(shelf_press_pager(ctx,UI_PAGER_PREVIOUS)==APP_REDRAW_AREA&&ctx->leaf==0);
    render(ctx,ctx->fb);assert(!memcmp(first,ctx->fb,bytes));
    assert(shelf_press_pager(ctx,UI_PAGER_LAST)==APP_REDRAW_AREA&&ctx->leaf==1);
    assert(shelf_press_pager(ctx,UI_PAGER_FIRST)==APP_REDRAW_AREA&&ctx->leaf==0);
    notepad_paper_checks(first,false);
    free(first);s_count=count;s_visible_count=visible;
}
static void notepad_ink_pixels(app_ctx_t *ctx,const char *output){
    const size_t bytes=684*1216/2;
    uint8_t *expected=malloc(bytes);assert(expected);
    char original[sizeof(s_shelf[9].name)];copy(original,sizeof(original),s_shelf[9].name);
    copy(s_shelf[9].name,sizeof(s_shelf[9].name),
         "从这里开始一段特别长的旅途这本书的名字一定会在书架这里截断");
    for(unsigned scale=100;scale<=200;scale+=25){
        font_scale=scale;
        render(ctx,ctx->fb);
        memcpy(expected,ctx->fb,bytes);
        int short_width=0;
        for(int row=0;row<10;row+=9){
            const EpdRect paper=ui_notepad_row(row,false),ink=ui_notepad_title_ink(ctx->fb,row);
            assert(ink.x>=48&&ink.x+ink.width<=376&&ink.y>paper.y&&ink.y+ink.height<paper.y+paper.height);
            if(!row)short_width=ink.width;
            else assert(ink.width>short_width+80&&ui_text_fixed_width_px(28,s_shelf[9].name)>320);
            const int cx=ink.x+ink.width/2,cy=ink.y+ink.height/2;
            bool passes_over_title=false;
            EpdRect whole={0};
            for(unsigned step=0;step<UI_NOTEPAD_INK_STEPS;++step){
                const EpdRect dirty=ui_notepad_ink_step(ctx->fb,row,ink,step);
                assert(dirty.x>=paper.x+3&&dirty.x+dirty.width<=386&&dirty.x+dirty.width>=dirty.x+1);
                assert(dirty.y>=paper.y+3&&dirty.height>0&&dirty.y+dirty.height<=paper.y+paper.height-3);
                assert(dirty.x>=cx-56&&dirty.x+dirty.width<=cx+57);
                assert(dirty.y>=cy-30&&dirty.y+dirty.height<=cy+31);
                whole=step?ui_rect_union(whole,dirty):dirty;
                unsigned added=0;
                for(int y=dirty.y;y<dirty.y+dirty.height;++y)for(int x=dirty.x;x<dirty.x+dirty.width;++x){
                    if(ui_read_pixel(expected,x,y)==ui_read_pixel(ctx->fb,x,y))continue;
                    passes_over_title|=x>=ink.x&&x<ink.x+ink.width&&y>=ink.y&&y<ink.y+ink.height;
                    assert(ui_read_pixel(expected,x,y)==15&&!ui_read_pixel(ctx->fb,x,y));
                    epd_draw_pixel(x,y,0,expected);++added;
                }
                assert(added>30&&!memcmp(expected,ctx->fb,bytes));
            }
            assert(passes_over_title&&whole.height>=39&&whole.height<=58);
            assert(abs(whole.width-2*whole.height)<=5);
            if(scale==100&&row==0)assert(whole.height==43);
            assert(abs(whole.x+whole.width/2-cx)<=3&&abs(whole.y+whole.height/2-cy)<=3);
        }
        char path[1024];snprintf(path,sizeof(path),"%s/shelf-notepad-oval-%u.raw",output,scale);save(path,ctx->fb);
    }
    copy(s_shelf[9].name,sizeof(s_shelf[9].name),original);
    free(expected);
}

static void notepad_ink_gestures(app_ctx_t *ctx){
    font_scale=100;s_presented_view=SHELF;ctx->hl=(display_state_t *)ctx;
    s_pressed_control=-1;s_shelf_selecting=false;s_view=SHELF;
    const unsigned first=input_refreshes,books=opened_books;
    const int visible=s_visible_count;
    const EpdRect row=ui_notepad_row(0,false);
    ui_gesture_event_t ev={.type=UI_GESTURE_PRESS,.x0=75,.x=75,.y0=row.y+28,.y=row.y+28};
    render(ctx,ctx->fb);
    assert(gesture_event(ctx,&ev)==APP_REDRAW_NONE&&s_pressed_control==-1);
    ev.type=UI_GESTURE_CANCEL;assert(gesture_event(ctx,&ev)==APP_REDRAW_NONE);
    ev.type=UI_GESTURE_SWIPE_R;assert(gesture_event(ctx,&ev)==APP_REDRAW_NONE&&ctx->leaf==0);
    ev.type=UI_GESTURE_TAP;ev.x=22;
    assert(gesture_event(ctx,&ev)==APP_REDRAW_NONE);ev.x=ev.x0;
    ev.type=UI_GESTURE_LONG_PRESS;assert(gesture_event(ctx,&ev)==APP_REDRAW_PAGE&&s_view==MANAGE);
    assert(input_refreshes==first&&opened_books==books);
    s_view=SHELF;
    s_shelf_selecting=true;ev.type=UI_GESTURE_TAP;
    assert(gesture_event(ctx,&ev)==APP_REDRAW_PAGE&&s_shelf[0].selected);
    clear_selection();s_shelf_selecting=false;
    s_shelf[0].removed=true;
    assert(gesture_event(ctx,&ev)==APP_REDRAW_PAGE&&s_view==MANAGE);
    s_view=SHELF;s_shelf[0].removed=s_clear_confirm=s_file_removed=s_delete_confirm=false;
    s_visible_count=0;
    assert(gesture_event(ctx,&ev)==APP_REDRAW_NONE);
    s_visible_count=visible;
    assert(input_refreshes==first&&opened_books==books);
    render(ctx,ctx->fb);
    assert(gesture_event(ctx,&ev)==APP_REDRAW_PAGE&&opened_books==books+1);
    assert(input_refreshes==first+UI_NOTEPAD_INK_STEPS&&opened_after_refreshes==input_refreshes);
    assert(input_intent==DISPLAY_REFRESH_FEEDBACK);
    render(ctx,ctx->fb);fail_ink=true;
    assert(gesture_event(ctx,&ev)==APP_REDRAW_PAGE&&opened_books==books+2);
    assert(input_refreshes==first+UI_NOTEPAD_INK_STEPS+1&&opened_after_refreshes==input_refreshes&&ink_errors==1);
    fail_ink=false;
    ctx->request_menu=true;
    assert(gesture_event(ctx,&ev)==APP_REDRAW_PAGE&&input_refreshes==first+UI_NOTEPAD_INK_STEPS+1);
    ctx->request_menu=false;opened_books=books;ctx->hl=NULL;s_presented_view=-1;
}

static void shelf_title_checks(app_ctx_t *ctx){
    const size_t bytes=684*1216/2;
    uint8_t *before=malloc(bytes);assert(before);
    for (int style=1;style<=2;++style) for(int selection=0;selection<=1;++selection){
        shelf_style=style;s_shelf_selecting=selection;
        shelf_titles=3u ^ (1u<<(style-1));
        render(ctx,ctx->fb);memcpy(before,ctx->fb,bytes);
        shelf_titles=3;render(ctx,ctx->fb);
        const int height=selection?18:24;
        unsigned ink[3]={0};
        for (int y=0;y<1216;++y) for(int x=0;x<684;++x){
            if(ui_read_pixel(ctx->fb,x,y)==ui_read_pixel(before,x,y))continue;
            int row=0;
            for (;row<3;++row){
                const int top=row_rect(row*3).y+SHELF_FURNITURE_BOTTOM;
                if(y>=top&&y<top+height){++ink[row];break;}
            }
            assert(row<3);
        }
        assert(ink[0]&&ink[1]&&ink[2]);
        shelf_titles=3u ^ (1u<<(style-1));render(ctx,ctx->fb);
        assert(!memcmp(before,ctx->fb,bytes));
    }
    shelf_style=5;s_shelf_selecting=false;shelf_titles=0;
    render(ctx,ctx->fb);memcpy(before,ctx->fb,bytes);
    shelf_titles=3;render(ctx,ctx->fb);assert(!memcmp(before,ctx->fb,bytes));
    shelf_titles=0;free(before);
}
static void shelf_checks(uint8_t *fb,const char *directory,const char *output){
    app_ctx_t ctx={.fb=fb,.now_ms=1000};font_scale=100;adapter_setup(directory);
    // 浏览过的分类保留初始排序，不受其他分类后来改动默认值影响。/ A visited category keeps its initial sort when another changes the default.
    sort_shelf(&ctx);int custom=2;assert(book_shelf_categories_select(&s_shelf_categories,custom));s_recent_sort=true;sort_shelf(&ctx);
    assert(book_shelf_categories_select(&s_shelf_categories,0)&&!shelf_query().recent_sort);
    pending=true;sd_mounted=false;assert(!legacy_category_recover()&&!recovery_calls);sd_mounted=true;
    usb_active=true;assert(!legacy_category_recover()&&!recovery_calls);usb_active=false;
    transfer_idle=false;assert(!legacy_category_recover()&&!recovery_calls);transfer_idle=true;
    cover_idle=false;assert(!legacy_category_recover()&&!recovery_calls);cover_idle=true;
    recovery_error=ESP_FAIL;assert(!legacy_category_recover()&&recovery_calls==1&&!recovery_invalidations);
    recovery_error=ESP_OK;assert(legacy_category_recover()&&recovery_calls==2&&recovery_invalidations==3);
    assert(legacy_category_recover()&&recovery_calls==2);
    s_count=18;
    const char *titles[]={"山海经","朝花夕拾","人间词话","小王子","论语","围城","边城","诗经","红楼梦"};
    for(int i=0;i<s_count;++i){memset(&s_shelf[i],0,sizeof(s_shelf[i]));snprintf(s_shelf[i].path,288,"/sdcard/books/book%d.txt",i);
        copy(s_shelf[i].name,256,titles[i%9]);copy(s_shelf[i].author,128,"作者未知");s_shelf[i].search_match=true;s_shelf[i].favorite=i==1;}
    assert(book_collection_sync(&s_collection,"/sdcard/books",NULL,0,s_shelf,s_count,true)==ESP_OK);
    uint32_t target=book_collection_at(&s_collection,0)->id;unsigned changed=0;
    for(int i=0;i<12;++i)s_shelf[i].selected=true;
    assert(book_collection_assign(&s_collection,s_shelf,s_count,true,target,&changed)==ESP_OK&&changed==12);
    assert(shelf_collection_sync(false));shelf_categories_recount();clear_selection();
    shelf_style=2;s_view=SHELF;category_select(&ctx,target);ctx.leaf=0;
    shelf_title_checks(&ctx);
    shelf_style=APP_SHELF_STYLE_NOTEPAD;
    const char *notepad_titles[]={"到站请送命","长安的荔枝","我与地坛","冬牧场","且听风吟",
        "一句顶一万句","百年孤独","献给阿尔吉侬的花束","看不见的城市","月亮与六便士"};
    const char *notepad_authors[]={"翻云袖","马伯庸","史铁生","李娟","村上春树",
        "刘震云","加西亚·马尔克斯","丹尼尔·凯斯","伊塔洛·卡尔维诺","毛姆"};
    const unsigned notepad_progress[]={28,68,42,100,15,83,36,54,19,46};
    for(int i=0;i<10;++i){
        copy(s_shelf[i].name,256,notepad_titles[i]);copy(s_shelf[i].author,128,notepad_authors[i]);
        s_shelf[i].has_progress=true;s_shelf[i].pct=notepad_progress[i];
    }
    for(unsigned scale=100;scale<=200;scale+=100){
        font_scale=scale;
        ui_clear_page(fb);
        for(int i=0;i<4;++i){
            EpdRect card=shelf_style_card(i);uint8_t id=shelf_style_id(i);
            assert(card.y>=190&&card.y+card.height<UI_NAV_TOP);
            for(int j=0;j<4;++j)if(j!=i)assert(!ui_rect_hit(shelf_style_card(j),card.x+card.width/2,card.y+card.height/2));
            assert(ui_font_has_text(shelf_style_name(id)));
            settings_card(fb,card,24,id==APP_SHELF_STYLE_NOTEPAD?0xd0:UI_GRAY_WHITE,0x70);
            if(i>=2)draw_compact_shelf_style(fb,card,id,id==APP_SHELF_STYLE_NOTEPAD);
        }
        assert(shelf_style_id(3)==APP_SHELF_STYLE_NOTEPAD);
        assert(!strcmp(shelf_style_name(APP_SHELF_STYLE_NOTEPAD),"便签本")&&ui_font_has_text("无封面 · 每页 10 本"));
        for(int select=0;select<=1;++select){
            s_shelf_selecting=select;
            shelf_snapshot(&ctx,output,select?"shelf-notepad-select":"shelf-notepad");
            const EpdRect first=ui_notepad_row(0,select);
            for(int slot=-1;slot<10;++slot){
                const int top=slot<0?first.y-38:ui_notepad_row(slot,select).y;
                const int bottom=slot<0?first.y-2:top+first.height-1;
                int left=543,right=543;
                for(int y=top;y<bottom;++y){
                    for(int x=368;x<543;++x)if(ui_read_pixel(fb,x,y)<12&&x<left)left=x;
                    for(int x=543;x<647;++x)if(ui_read_pixel(fb,x,y)<12&&x>right)right=x;
                }
                assert(left>=390&&left<=395&&right>=623&&right<=630);
                if(slot>=0){
                    EpdRect hit;int control=control_at(&ctx,300,top+30,&hit);
                    assert(select?control==-1:control==slot&&hit.y==top);
                }
            }
            EpdRect tenth=ui_notepad_row(9,select);
            unsigned before=selected_count();
            assert(action_at(&ctx,300,tenth.y+32)==APP_REDRAW_PAGE);
            assert(select?selected_count()==before+1:opened_books==1);
            clear_selection();opened_books=0;
        }
    }
    s_shelf_selecting=false;
    font_scale=100;
    EpdRect title=shelf_category_title_rect();
    assert(shelf_category_gesture(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_TAP,.x0=title.x+20,.y0=title.y+40})==APP_REDRAW_AREA);
    shelf_snapshot(&ctx,output,"shelf-notepad-category");check_category_outline(&ctx);
    book_shelf_categories_menu_close(&s_shelf_categories);
    for(int action=1;action<=3;++action){
        EpdRect r=shelf_header_action_rect(action);
        unsigned before=input_refreshes;
        assert(action_at(&ctx,r.x+r.width/2,r.y+r.height/2)==APP_REDRAW_PAGE);
        assert(input_refreshes==before&&s_view==(action==1?SHELF:action==2?BULK:IMPORT));
        assert(s_shelf_selecting==(action==1));s_shelf_selecting=false;s_view=SHELF;
    }
    notepad_paging_checks(&ctx);
    notepad_ink_pixels(&ctx,output);
    notepad_ink_gestures(&ctx);
    shelf_fast=true;font_scale=100;shelf_snapshot(&ctx,output,"shelf-notepad-fast");
    notepad_paper_checks(fb,true);
    unsigned black=0;
    for(int y=174;y<260;++y)for(int x=24;x<660;++x){
        int value=ui_read_pixel(fb,x,y);assert(value==0||value==15);black+=value==0;
    }
    assert(black>1000);shelf_fast=false;
    for(unsigned scale=100;scale<=200;scale+=25){
        font_scale=scale;shelf_style=2;s_view=SHELF;s_shelf_selecting=false;category_select(&ctx,target);ctx.leaf=0;clear_selection();
        shelf_snapshot(&ctx,output,"shelf-grid");
        EpdRect hit,title=shelf_category_title_rect(),previous={0};
        for(int action=0;action<4;++action){
            EpdRect r=shelf_header_action_rect(action);
            assert(r.y==80&&r.width==80&&r.height==80&&r.x+r.width<=648);
            assert(!ui_rect_hit(title,r.x+r.width/2,r.y+r.height/2));
            if(action)assert(r.x-previous.x-previous.width==12);
            int control=control_at(&ctx,r.x+r.width/2,r.y+r.height/2,&hit);
            assert(control==(action<2?114+action:118+action)&&!memcmp(&hit,&r,sizeof(r)));
            assert(control_at(&ctx,r.x+r.width/2,159,&hit)==control);
            if(action<3)assert(control_at(&ctx,r.x+r.width+6,120,&hit)==-1);
            uint8_t *before=malloc(684*1216/2);assert(before);memcpy(before,fb,684*1216/2);
            s_pressed_control=control;draw_shelf_header_button(fb,r,action);
            for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
                if(!ui_rect_hit(r,x,y))assert(ui_read_pixel(fb,x,y)==ui_read_pixel(before,x,y));
            s_pressed_control=-1;draw_shelf_header_button(fb,r,action);
            assert(!memcmp(before,fb,684*1216/2));free(before);
            previous=r;
        }
        assert(action_at(&ctx,516,120)==APP_REDRAW_PAGE&&s_view==BULK);s_view=SHELF;
        assert(action_at(&ctx,608,120)==APP_REDRAW_PAGE&&s_view==IMPORT);s_view=SHELF;
        assert(action_at(&ctx,516,168)==APP_REDRAW_NONE&&s_view==SHELF);
        uint32_t generation=s_collection.generation;
        assert(action_at(&ctx,616,1082)==APP_REDRAW_NONE&&s_view==SHELF&&s_collection.generation==generation);
        assert(action_at(&ctx,424,120)==APP_REDRAW_PAGE&&s_shelf_selecting&&!selected_count());
        assert(!main_page_visible(&ctx));
        assert(action_at(&ctx,500,1148)==APP_REDRAW_NONE&&!requested_nav&&s_view==SHELF);
        assert(action_at(&ctx,320,1148)==APP_REDRAW_NONE&&!requested_nav&&s_view==SHELF);
        assert(action_at(&ctx,100,1204)==APP_REDRAW_NONE&&!requested_nav&&s_view==SHELF);
        EpdRect first=row_rect(0);assert(gesture_event(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_TAP,.x0=first.x+80,.y0=first.y+80})==APP_REDRAW_PAGE&&selected_count()==1&&!opened_books);
        shelf_snapshot(&ctx,output,"shelf-grid-selected");
        assert(gesture_event(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_TAP,.x0=160,.y0=110})==APP_REDRAW_NONE&&selected_count()==1&&!book_shelf_categories_menu_is_open(&s_shelf_categories));
        assert(gesture_event(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_TAP,.x0=160,.y0=222})==APP_REDRAW_AREA&&book_shelf_categories_menu_is_open(&s_shelf_categories));
        shelf_snapshot(&ctx,output,"shelf-category-switch");
        check_category_outline(&ctx);
        book_shelf_categories_menu_close(&s_shelf_categories);
        assert(action_at(&ctx,356,120)==APP_REDRAW_PAGE&&s_view==CATEGORIES&&s_category_ui.screen==CATEGORY_SELECTED_ACTIONS&&s_category_ui.source==target);
        book_category_ui_draw(&s_category_ui,&s_shelf_categories,fb);
        ui_gesture_event_t remove={.type=UI_GESTURE_TAP,.x0=200,.y0=376};
        assert(category_gesture(&ctx,&remove)==APP_REDRAW_PAGE&&s_category_ui.command==CATEGORY_UNCLASSIFY&&selected_count()==1);
        s_category_ui.command=CATEGORY_IDLE;category_back(&ctx);assert(s_view==SHELF&&s_shelf_selecting&&selected_count()==1);
        assert(gesture_event(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_SWIPE_L})==APP_REDRAW_AREA&&ctx.leaf==1&&selected_count()==1);
        assert(action_at(&ctx,140,1148)==APP_REDRAW_PAGE&&s_view==CATEGORIES&&s_category_ui.review&&selected_count()==1&&!requested_nav);
        category_back(&ctx);assert(s_view==SHELF&&s_shelf_selecting&&selected_count()==1);
        assert(action_at(&ctx,600,120)==APP_REDRAW_PAGE&&!s_shelf_selecting&&!selected_count());
        assert(main_page_visible(&ctx));
        assert(action_at(&ctx,500,168)==APP_REDRAW_NONE&&s_view==SHELF);
        assert(action_at(&ctx,332,120)==APP_REDRAW_PAGE&&s_view==SEARCH);s_view=SHELF;
        shelf_style=5;category_select(&ctx,target);shelf_snapshot(&ctx,output,"shelf-list");
        assert(action_at(&ctx,424,120)==APP_REDRAW_PAGE&&s_shelf_selecting);assert(action_at(&ctx,100,340)==APP_REDRAW_PAGE&&selected_count()==1);
        shelf_snapshot(&ctx,output,"shelf-list-selected");clear_selection();s_shelf_selecting=false;
        category_select(&ctx,BOOK_COLLECTION_ALL);shelf_style=2;ctx.leaf=0;
        for(int i=0;i<s_count;++i)s_shelf[i].new_book=false;
        shelf_snapshot(&ctx,output,"shelf-all");
        for(int y=160;y<192;++y)for(int x=36;x<648;++x)assert(ui_read_pixel(fb,x,y)==15);
        assert(action_at(&ctx,608,168)==APP_REDRAW_NONE&&s_view==SHELF);
        assert(action_at(&ctx,516,168)==APP_REDRAW_NONE&&s_view==SHELF);
        s_shelf[0].new_book=true;shelf_snapshot(&ctx,output,"shelf-new-books");
        assert(action_at(&ctx,160,176)==APP_REDRAW_NONE&&s_view==SHELF&&!selected_count());
        s_shelf[0].new_book=false;
        copy(s_shelf_warning,sizeof(s_shelf_warning),"分类资料暂不可用，仍可浏览全部书籍");
        shelf_snapshot(&ctx,output,"shelf-warning");
        s_shelf_selecting=true;shelf_snapshot(&ctx,output,"shelf-grid-selected-warning");
        shelf_style=5;shelf_snapshot(&ctx,output,"shelf-list-selected-warning");
        s_shelf_selecting=false;s_shelf_warning[0]=0;
    }
    book_shelf_covers_cancel(&s_shelf_covers);book_shelf_covers_release(&s_shelf_covers);
    book_shelf_categories_dispose(&s_shelf_categories);clean_library(&s_collection);
    puts("category shelf: PASS (real grid/list/notepad pixels and taps, ten rows, shared author/progress anchors, three-phase oval over the title center and tap/hold/cancel/error routing, fast curl dots, compact thumbnail, selection and per-category state)");
}
#undef row_rect
'''


def unit():
    code = host.host_preamble()
    code = code.replace("#define UI_GRAY_LIGHT 224", "#define UI_GRAY_LIGHT 0x90")
    begin, end = code.index("static struct {void *p;"), code.index("static enum EpdRotation")
    code = code[:begin] + r'''
void *heap_caps_malloc(size_t n,int c){(void)c;return malloc(n);}
void *heap_caps_calloc(size_t n,size_t z,int c){(void)c;return calloc(n,z);}
void *heap_caps_realloc(void *p,size_t n,int c){(void)c;return realloc(p,n);}
void heap_caps_free(void *p){free(p);}
size_t heap_caps_get_free_size(int c){(void)c;return 16*1024*1024;}
size_t heap_caps_get_largest_free_block(int c){(void)c;return 16*1024*1024;}
void lock_pin_wipe(void *p,unsigned n){memset(p,0,n);}
''' + code[end:]
    code = re.sub(r"^static [^\n]*\bui_font_\w+\([^\n]*\n", "", code, flags=re.M)
    code += r'''
#undef UI_ICON_CHEVRON_RIGHT
#undef UI_ICON_CHEVRON_LEFT
#undef UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_shelf_categories.h"
#include "book_search.h"
#include "ui_font.h"
#include "esp_heap_caps.h"
#include "assets/ui_icons.h"
#include "book_category_ui.h"
#include "ui_nav_layout.h"
#define UI_GAP 12
#define UI_BTN_H 84
#define UI_PX_CAPTION 28
#define UI_CONTENT_BOTTOM 1076
enum {UI_PAGER_NONE=-1,UI_PAGER_FIRST,UI_PAGER_PREVIOUS,UI_PAGER_NEXT,UI_PAGER_LAST};
static unsigned font_scale=100;
static bool s_shelf_selecting;
static unsigned app_settings_system_font_size(void){return font_scale;}
uint8_t ui_read_pixel(const uint8_t *fb,int x,int y);
EpdRect ui_rotate_rect_to_fb(EpdRect rect);
void ui_clear_page(uint8_t *f){memset(f,255,684*1216/2);}
'''
    code += host.epdiy_slices()
    code += host.function("ui_control_gray", ROOT / "main/ui/ui_kit.h")
    for name in ("clamp_radius", "draw_arc", "ui_draw_round_rect_raw", "ui_draw_round_rect",
                 "ui_fill_round_rect_raw", "ui_fill_round_rect", "ui_inset_rect", "ui_draw_control_frame",
                 "ui_draw_separator", "ui_hairline", "icon_row_decode", "icon_nibble", "ui_draw_icon", "ui_read_pixel",
                 "ui_text_effective_px", "ui_text_fixed_context_width_px", "ui_text_fixed_width_px",
                 "ui_text_fixed_context_vc", "ui_text_fixed_vc", "ui_text_fixed_ink_vc", "ui_text_vc", "ui_blit_mono_bitmap", "draw_frosted", "ui_draw_acrylic_guard",
                 "ui_rotate_rect_to_fb", "clip_refresh_rect", "ui_clear_rect_fast", "ui_rect_union"):
        code += host.function(name, KIT) + "\n"
    code += r'''
void ui_nav_status(uint8_t *fb){ui_text_fixed_vc(fb,36,38,26,"08:30",EPD_DRAW_ALIGN_LEFT,false);}
void ui_nav_back(uint8_t *fb,int x,int y){ui_draw_icon(fb,x+26,y+28,36,UI_ICON_CHEVRON_LEFT,0);}
bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static unsigned checks;
static void checked_text(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align,bool white){
    if(!ui_font_has_text(s)){fprintf(stderr,"Missing category glyph: %s\n",s);abort();}
    int w=ui_text_fixed_width_px(px,s),left=x,above=0,below=0;ui_font_measure_line_px(px,s,&above,&below);
    if(align==EPD_DRAW_ALIGN_CENTER)left-=w/2;else if(align==EPD_DRAW_ALIGN_RIGHT)left-=w;
    int base=y+(above-below)/2;
    assert(left>=24&&left+w<=660&&base-above>=60&&base+below<1196);++checks;
    if(strstr(s,"分类资料暂不可用"))assert(base-above>=(s_shelf_selecting?1000:984)&&base+below<(s_shelf_selecting?1020:1006));
    ui_text_fixed_vc(fb,x,y,px,s,align,white);
}
#define ui_text_fixed_vc checked_text
'''
    code += re.sub(r"^#(?:include[^\n]*|pragma once)\n", "", (ROOT / "main/apps/book_selection_ui.h").read_text(), flags=re.M)
    code += re.sub(r"^#include[^\n]*\n", "", (ROOT / "main/apps/book_category_ui.c").read_text(), flags=re.M)
    code += adapter()
    code += shelf_fixture()
    code += r'''
#undef ui_text_fixed_vc
static int tap(book_category_ui_t *u,book_shelf_categories_t *c,int x,int y){ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=x,.y0=y};return book_category_ui_gesture(u,c,&e,1000);}
static int tap_selection(book_category_ui_t *u,book_shelf_categories_t *c,book_selection_control_t control){EpdRect r=book_selection_rect(control);return tap(u,c,r.x+r.width/2,r.y+r.height/2);}
static void disabled_button_checks(uint8_t *fb,EpdRect r){
    unsigned ink=0,white=0,total=0;
    for(int y=r.y+12;y<r.y+r.height-12;++y)for(int x=r.x+16;x<r.x+r.width-16;++x){
        unsigned gray=ui_read_pixel(fb,x,y);assert(gray>=9);++total;if(gray<15)++ink;else ++white;
    }
    assert(ink>20&&white>total*2/3);
}
static void keyboard_click(book_category_ui_t *u,book_shelf_categories_t *c,uint8_t *fb,int x,int y){
    ui_gesture_event_t e={.type=UI_GESTURE_PRESS,.x0=x,.y0=y,.x=x,.y=y};
    assert(book_category_ui_gesture(u,c,&e,1000)==1);
    assert(book_category_ui_input_paint(u,fb).area.width);
    e.type=UI_GESTURE_TAP;assert(book_category_ui_gesture(u,c,&e,1040)==1);
    assert(book_category_ui_input_paint(u,fb).area.width);
}
static void snapshot(book_category_ui_t *u,book_shelf_categories_t *c,uint8_t *fb,const char *dir,const char *name){
    if(u->screen==CATEGORY_BOOKS){
        book_shelf_covers_cancel(&s_category_covers);book_shelf_covers_release(&s_category_covers);
        for(int row=0;row<book_category_ui_rows(true);++row){
            int i=book_category_ui_book_at(u,u->page*book_category_ui_rows(true)+row);if(i<0)break;
            book_cover_job_t request={0};copy(request.path,288,u->books[i].path);copy(request.title,256,u->books[i].name);
            book_shelf_covers_prepare(&s_category_covers,row,i,true,false,0,0,1000);assert(book_shelf_covers_collect(&s_category_covers,row,&request));
        }
    }
    book_category_ui_t before=*u;book_category_ui_draw(u,c,fb);assert(!memcmp(&before,u,sizeof(before)));
    char path[1024];snprintf(path,sizeof(path),"%s/%s-%u.raw",dir,name,font_scale);save(path,fb);
}
int main(int argc,char **argv){
    assert(argc==3&&ttf_font_open_builtin()==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);
    ui_clear_page(fb);command_text(fb,(EpdRect){36,1004,612,80},28,"添加 0 本",false);
    assert(ui_read_pixel(fb,36,1004)==15);
    unsigned disabled_ink=0;
    for(int y=1016;y<1072;++y)for(int x=48;x<636;++x)if(ui_read_pixel(fb,x,y)<15)++disabled_ink;
    assert(disabled_ink>20&&disabled_ink<612*80/3);
    book_collection_t library={0};char prefix[288];snprintf(prefix,sizeof(prefix),"%s/ui-library",argv[2]);
    assert(book_collection_open(&library,prefix)==ESP_OK);
    book_shelf_entry_t books[18]={0};
    for(int i=0;i<18;++i){snprintf(books[i].path,288,"/sdcard/books/book%d.txt",i);snprintf(books[i].name,256,"特别图书 %02d",i);books[i].search_match=true;}
    copy(books[3].name,256,"一本有很长很长名字的图书用于确认完整书名不会在选书页面被隐藏并且可以打开查看详细名称");
    assert(book_collection_sync(&library,"/sdcard/books",NULL,0,books,18,true)==ESP_OK);
    uint32_t a=new_category(&library,"文学"),b=new_category(&library,"科学");unsigned changed=0;
    books[0].selected=books[1].selected=true;assert(book_collection_assign(&library,books,18,true,a,&changed)==ESP_OK);
    for(int i=0;i<14;++i){char name[97];snprintf(name,sizeof(name),"分类 %d",i);(void)new_category(&library,name);}
    (void)new_category(&library,"一二三四五六七八九十一二三四五六七八九十一二三四");
    book_collection_apply(&library,books,18);
    book_shelf_categories_t categories={0};book_shelf_categories_init(&categories,"/sdcard/books");
    assert(book_shelf_categories_bind(&categories,&library));book_shelf_categories_recount(&categories,books,18);
    for(unsigned scale=100;scale<=200;scale+=25){
        font_scale=scale;book_shelf_clear_selection(books,18);book_category_ui_t u;
        book_category_ui_begin(&u,false);snapshot(&u,&categories,fb,argv[1],"manager");
        for(int y=1056;y<1120;++y)for(int x=180;x<504;++x)assert(ui_read_pixel(fb,x,y)==15);
        assert(!tap(&u,&categories,342,1088)&&u.command==CATEGORY_IDLE&&u.screen==CATEGORY_LIST);
        int center=224+728/book_category_ui_rows(false)/2;
        assert(tap(&u,&categories,620,center)==1&&u.screen==CATEGORY_ACTIONS&&u.source==a);
        snapshot(&u,&categories,fb,argv[1],"actions");
        assert(tap(&u,&categories,200,488)==1&&u.screen==CATEGORY_EDIT&&!strcmp(u.draft,"文学"));
        snapshot(&u,&categories,fb,argv[1],"rename");assert(tap(&u,&categories,620,105)==1&&u.command==CATEGORY_RENAME);
        book_category_ui_suspend(&u);assert(u.command==CATEGORY_IDLE);assert(!book_category_ui_back(&u)&&u.screen==CATEGORY_ACTIONS);
        assert(tap(&u,&categories,200,600)==1&&u.screen==CATEGORY_ORDER);snapshot(&u,&categories,fb,argv[1],"order");
        assert(tap(&u,&categories,342,572)==1&&u.command==CATEGORY_DOWN);u.command=CATEGORY_IDLE;assert(!book_category_ui_back(&u));
        assert(tap(&u,&categories,200,712)==1&&u.command==CATEGORY_PREVIEW_DELETE);book_folder_plan_t preview={0};u.preview=&preview;u.screen=CATEGORY_PREVIEW;u.command=CATEGORY_IDLE;snapshot(&u,&categories,fb,argv[1],"delete-confirm");
        assert(tap(&u,&categories,500,890)==1&&u.command==CATEGORY_REMOVE);u.command=CATEGORY_IDLE;
        book_category_ui_end(&u);book_category_ui_begin(&u,true);snapshot(&u,&categories,fb,argv[1],"target");
        assert(tap(&u,&categories,200,center)==1&&u.command==CATEGORY_MOVE&&u.destination==0);u.command=CATEGORY_IDLE;
        assert(tap(&u,&categories,620,108)==1&&u.screen==CATEGORY_EDIT);snapshot(&u,&categories,fb,argv[1],"new-category");
        assert(tap(&u,&categories,620,108)==1&&u.command==CATEGORY_IDLE&&u.notice[0]);
        assert(ui_text_edit_insert(&u.input,"新建分类"));assert(tap(&u,&categories,620,108)==1&&u.command==CATEGORY_CREATE);u.command=CATEGORY_IDLE;
        book_category_ui_end(&u);book_category_ui_begin(&u,false);u.books=books;u.count=18;u.cover=category_cover_draw;book_category_ui_books(&u,a,false);
        assert(u.filter==0&&book_category_ui_book_count(&u)==16);snapshot(&u,&categories,fb,argv[1],"add-empty-selection");
        disabled_button_checks(fb,book_selection_rect(BOOK_SELECTION_SUBMIT));
        assert(book_selection_rect(BOOK_SELECTION_SUBMIT).y+book_selection_rect(BOOK_SELECTION_SUBMIT).height==1196);
        assert(!tap_selection(&u,&categories,BOOK_SELECTION_SUBMIT)&&u.command==CATEGORY_IDLE);
        assert(!tap(&u,&categories,320,1148)&&u.command==CATEGORY_IDLE);
        assert(!tap(&u,&categories,320,322)&&!book_shelf_selected_count(books,18));
        assert(tap(&u,&categories,200,book_rect(&u,0).y+book_rect(&u,0).height/2)==1&&book_shelf_selected_count(books,18)==1);
        ui_gesture_event_t swipe={.type=UI_GESTURE_SWIPE_U};assert(book_category_ui_gesture(&u,&categories,&swipe,1000)==1);
        assert(tap(&u,&categories,200,book_rect(&u,0).y+book_rect(&u,0).height/2)==1&&book_shelf_selected_count(books,18)==2);
        copy(u.query,sizeof(u.query),"no-match");u.page=0;assert(!book_category_ui_book_count(&u));snapshot(&u,&categories,fb,argv[1],"search-empty");
        assert(tap_selection(&u,&categories,BOOK_SELECTION_REVIEW)==1&&u.review&&book_category_ui_book_count(&u)==2);snapshot(&u,&categories,fb,argv[1],"review");
        assert(tap(&u,&categories,480,322)==1&&!book_shelf_selected_count(books,18));
        u.review=false;u.query[0]=0;u.filter=b;snapshot(&u,&categories,fb,argv[1],"filter-empty");
        assert(tap(&u,&categories,300,648)==1&&u.filter==BOOK_COLLECTION_ALL);
        assert(tap(&u,&categories,200,222)==1&&u.screen==CATEGORY_FILTER);snapshot(&u,&categories,fb,argv[1],"filter");
        assert(!book_category_ui_back(&u)&&u.screen==CATEGORY_BOOKS);
        u.filter=0;u.page=0;assert(tap(&u,&categories,620,book_rect(&u,0).y+book_rect(&u,0).height/2)==1&&u.screen==CATEGORY_BOOK_INFO);u.book_info=3;
        snapshot(&u,&categories,fb,argv[1],"full-book-name");
        char original[256];copy(original,sizeof(original),books[3].name);memset(books[3].name,'W',255);books[3].name[255]=0;
        books[3].category_id=book_collection_at(&library,book_collection_count(&library)-1)->id;
        snapshot(&u,&categories,fb,argv[1],"full-book-name-wide");copy(books[3].name,sizeof(books[3].name),original);books[3].category_id=0;
        assert(!book_category_ui_back(&u));
        assert(tap(&u,&categories,480,322)==1&&book_shelf_selected_count(books,18)==16);
        snapshot(&u,&categories,fb,argv[1],"add-selected");
        assert(tap_selection(&u,&categories,BOOK_SELECTION_SUBMIT)==1&&u.command==CATEGORY_MOVE);u.command=CATEGORY_IDLE;
        book_category_ui_books(&u,BOOK_COLLECTION_ALL,true);assert(u.review&&u.selecting_only);snapshot(&u,&categories,fb,argv[1],"review-before-target");
        assert(tap_selection(&u,&categories,BOOK_SELECTION_SUBMIT)==1&&u.screen==CATEGORY_LIST&&u.picker);
        book_category_ui_refresh(&u,&categories,book_collection_at(&library,book_collection_count(&library)-1)->id);
        snapshot(&u,&categories,fb,argv[1],"last-page-long-name");u.blocked=true;copy(u.notice,sizeof(u.notice),"分类资料不可用，原书籍不受影响");
        snapshot(&u,&categories,fb,argv[1],"recovery");assert(tap(&u,&categories,300,480)==1&&u.command==CATEGORY_RECOVER);book_category_ui_end(&u);
    }
    book_shelf_categories_dispose(&categories);clean_library(&library);adapter_checks(fb,argv[2]);shelf_checks(fb,argv[2],argv[1]);free(fb);
    printf("category UI: PASS (%u real-font bounds/glyph checks, 100/125/150/175/200%%, disabled ink, pure render, cross-page/search/filter selection, review, explicit scopes and full names)\n",checks);
}
'''
    return code


def run(output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="category-ui-") as directory:
        work = Path(directory)
        host.write_shims(work)
        (work / "esp_err.h").write_text((ROOT / "tools/book_storage_stubs/esp_err.h").read_text())
        (work / "esp_heap_caps.h").write_text((work / "esp_heap_caps.h").read_text() + "\nvoid *heap_caps_realloc(void *,size_t,int);\n")
        (work / "epdiy.h").write_text((work / "epdiy.h").read_text().replace(
            "EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT", "EPD_DRAW_ALIGN_LEFT=2, EPD_DRAW_ALIGN_CENTER=8, EPD_DRAW_ALIGN_RIGHT=4"))
        (work / "miniz.h").write_text('''#pragma once
#include <stddef.h>
#include <zlib.h>
typedef struct {int state;} tinfl_decompressor;
typedef int tinfl_status;
#define TINFL_STATUS_DONE 0
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4
#define tinfl_init(s) ((s)->state=0)
static inline tinfl_status tinfl_decompress(tinfl_decompressor *s,const unsigned char *in,size_t *n,unsigned char *base,unsigned char *out,size_t *m,int flags){(void)s;(void)base;(void)flags;uLongf size=*m;int result=uncompress(out,&size,in,*n);*m=size;return result==Z_OK?0:-1;}
''')
        payload = (ROOT / "main/assets/ui-hanzi.bin").read_bytes()
        with (work / "assets.c").open("a") as assets:
            assets.write('const uint8_t hanzi[] asm("_binary_ui_hanzi_bin_start")={' + ",".join(map(str, payload)) + '};\n')
            assets.write(f'asm(".globl _binary_ui_hanzi_bin_end\\n.set _binary_ui_hanzi_bin_end, _binary_ui_hanzi_bin_start + {len(payload)}");\n')
        (work / "test.c").write_text(unit())
        env, compiler = toolchain(work)
        flags = compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-g"]
        for folder in (work, ROOT / "main", ROOT / "main/ui", ROOT / "main/font", ROOT / "main/apps", ROOT / "main/book", ROOT / "main/diagnostics", ROOT / "main/display", ROOT / "main/settings", ROOT / "main/app", ROOT / "main/boot", ROOT / "main/services", ROOT / "components/book_search/include"):
            flags += ["-I" + str(folder)]
        units = [work / "test.c", work / "assets.c"] + [ROOT / "main/font" / name for name in ("ttf_font.c", "ttf_font_source.c", "ttf_block_cache.c", "ttf_glyph_cache.c", "ui_font.c", "ui_hanzi.c")]
        units += [ROOT / name for name in ("main/ui/ui_text_edit.c", "main/ui/ui_text_input.c", "main/ui/ui_keyboard.c", "main/ui/ui_ime.c", "main/ui/ui_gesture.c", "main/ui/ui_image_dither.c", "components/book_search/book_search.c", "main/book/book_shelf_category_state.c", "main/book/book_shelf_model.c", "main/book/book_collection.c", "main/book/book_category_ops.c", "main/book/book_folder_plan.c", "main/book/book_shelf_covers.c", "main/book/book_cover_auto.c")]
        flags += ["-ffunction-sections", "-fdata-sections"]
        linker = "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
        subprocess.run(flags + list(map(str, units)) + [linker, "-lm", "-lz", "-o", str(work / "test")], env=env, check=True)
        subprocess.run([str(work / "test"), str(output), str(work)], env=env, check=True)
    for raw in output.glob("*.raw"):
        Image.frombytes("L", (684, 1216), raw.read_bytes()).point(lambda v: v * 17 // 16).save(raw.with_suffix(".png"))
        raw.unlink()
    print(f"category screenshots: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/category-ui")
    run(parser.parse_args().output.resolve())
