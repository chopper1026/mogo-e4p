"""逐层返回真实阅读面板，不依赖可配置按键。/ Exercise real reader Back transitions independently of key bindings."""
from pathlib import Path
import subprocess
import tempfile
from reader_title_style_preview import enum_block, function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
unit = '''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct { int target; } app_ctx_t;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_PAGE } app_redraw_t;
typedef struct {app_redraw_t redraw; bool accepted;} app_back_result_t;
'''
unit += enum_block(BOOK, 'SHELF', 'book_view_t') + enum_block(BOOK, 'READER_PANEL_NONE', 'reader_panel_t')
unit += '''
enum { CATEGORY_IDLE };
static struct {int command;} s_category_ui;
static book_view_t s_view;
static reader_panel_t s_reader_panel, s_bookmark_parent;
static int s_reader_slider;
static bool s_open_error[1], s_shelf_categories, s_shelf_selecting, s_clear_confirm, s_batch_confirm;
static bool s_toc_jump_open,s_toc_jump_drag,s_text,s_tap_picker,s_bookmark_delete_confirm,s_bookmark_delete_error;
static bool s_bookmark_edit,s_reader_tools_toggle,s_landscape_notice_open,landscape_confirmed;
static struct {bool active;} s_dictionary_selection;
static void cancel_auto_resume(void){} static void middle_pending_clear(void){} static void reader_indent_hold_cancel(void){}
static void invalidate_prep(void){} static void clear_selection(void){} static void bookmark_selection_clear(void){}
static bool book_shelf_categories_menu_is_open(bool *v){return *v;}
static void book_shelf_categories_menu_close(bool *v){*v=false;}
static void set_reader_view(book_view_t v){s_view=v;}
static void ui_nav_request(app_ctx_t *ctx,int tab){assert(ctx->target==-1);ctx->target=tab;}
static app_redraw_t dismiss_open_error(void){s_open_error[0]=false;return APP_REDRAW_PAGE;}
static app_redraw_t dictionary_dismiss(void){s_dictionary_selection.active=false;return APP_REDRAW_PAGE;}
static app_redraw_t reader_landscape_notice_dismiss(app_ctx_t *ctx,bool confirm){(void)ctx;s_landscape_notice_open=false;landscape_confirmed=confirm;return APP_REDRAW_PAGE;}
static app_redraw_t shelf_back_level(app_ctx_t *ctx){(void)ctx;assert(false);return APP_REDRAW_NONE;}
'''
for name in ('toggle_reader_tools_panel', 'book_edge_back'):
    unit += function(name, BOOK) + '\n'
unit += '''
static app_redraw_t expect_back(app_ctx_t *ctx, bool accepted) {
    app_back_result_t back = book_edge_back(ctx); assert(back.accepted == accepted); return back.redraw;
}
int main(void) {
    app_ctx_t ctx={.target=-1}; s_view=READING; s_reader_panel=READER_PANEL_KEY_PICKER;
    reader_panel_t chain[]={READER_PANEL_KEY_SETTINGS,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_TOOLS,READER_PANEL_NONE};
    for(unsigned i=0;i<sizeof(chain)/sizeof(*chain);++i){
        assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && s_reader_panel==chain[i] && ctx.target==-1);
    }
    assert(s_reader_tools_toggle && expect_back(&ctx,true)==APP_REDRAW_NONE && ctx.target==0);
    ctx.target=-1;s_landscape_notice_open=true;landscape_confirmed=true;
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && !s_landscape_notice_open && !landscape_confirmed && ctx.target==-1);
    ctx.target=-1;s_reader_panel=READER_PANEL_TAP_GRID;s_tap_picker=true;
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && !s_tap_picker && s_reader_panel==READER_PANEL_TAP_GRID);
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && s_reader_panel==READER_PANEL_TURN_SETTINGS);
    s_reader_panel=READER_PANEL_BOOKMARKS;s_bookmark_parent=READER_PANEL_STATS;s_bookmark_edit=s_bookmark_delete_confirm=true;
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && !s_bookmark_delete_confirm && s_bookmark_edit);
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && !s_bookmark_edit && s_reader_panel==READER_PANEL_BOOKMARKS);
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && s_reader_panel==READER_PANEL_STATS);
    s_view=TOC;s_toc_jump_open=s_toc_jump_drag=s_text=true;
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && !s_toc_jump_open && !s_toc_jump_drag && s_view==TOC);
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && s_view==READING && s_reader_panel==READER_PANEL_TOOLS);
    s_view=SHELF;s_shelf_categories=s_shelf_selecting=true;
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && !s_shelf_categories && s_shelf_selecting);
    assert(expect_back(&ctx,true)==APP_REDRAW_PAGE && !s_shelf_selecting);
    assert(expect_back(&ctx,false)==APP_REDRAW_NONE && ctx.target==-1);
    s_view=CATEGORIES; s_category_ui.command=1;
    assert(expect_back(&ctx,false)==APP_REDRAW_NONE && ctx.target==-1);
    puts("PASS: one-layer reader back, nested pickers, bookmark origin, TOC, shelf root and body-to-home");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
    src = Path(tmp)/'reader.c'; exe = Path(tmp)/'reader'; src.write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
