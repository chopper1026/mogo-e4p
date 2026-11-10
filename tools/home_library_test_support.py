"""Shared mini-library doubles for tests extracting real Home callbacks."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]

def doubles():
    ui = (ROOT / 'main/ui/ui_mini_library.h').read_text()
    ui = ui[ui.index('typedef struct {'):]
    ui = ui[:ui.index('void ui_mini_library_draw')]
    home=(ROOT/'main/apps/app_dashboard.c').read_text()
    motion=home[home.index('enum { MINI_IDLE'):home.index('static ui_mini_library_frame_t mini_frame')]
    return '\n#include "home_library.h"\n' + ui + motion + r'''
static book_mini_item_t mini_books[5];
static home_library_view_t mini_view={.books=mini_books,.notice=""};
static bool mini_changed,mini_generation_needed,mini_save_ok=true;
static unsigned mini_polls,mini_generations,mini_shuffles;
home_library_view_t home_library_view(void){return mini_view;}
void home_library_select(unsigned i){assert(i<mini_view.count);mini_view.selected=i;}
bool home_library_recent(bool recent){if(!mini_save_ok)return false;mini_view.recent=recent;mini_generation_needed=true;return true;}
void home_library_shuffle(void){++mini_shuffles;mini_changed=true;}
bool home_library_poll(bool (*cancel)(void *),void *ctx){(void)cancel;(void)ctx;++mini_polls;bool changed=mini_changed;mini_changed=false;return changed;}
bool home_library_needs_generation(void){return mini_generation_needed;}
void home_library_cover_generation(unsigned gen){assert(gen);++mini_generations;mini_generation_needed=false;}
static bool app_poll_main_navigation_from(app_ctx_t *ctx,unsigned tab){(void)ctx;assert(tab==0);return false;}
'''

def ui_doubles():
    return r'''
static void ui_mini_library_draw(uint8_t *fb,EpdRect r,const home_library_view_t *v,ui_mini_library_frame_t f){(void)v;(void)f;ui_fill_rect_fast(r,0x33,fb);}
static ui_mini_hit_t mini_hit;
static ui_mini_hit_t ui_mini_library_hit(EpdRect r,const home_library_view_t *v,int x,int y){(void)r;(void)v;(void)x;(void)y;return mini_hit;}
'''
