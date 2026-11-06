#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实视图的可逆选区、边缘裁剪、完整分页与按钮命中。
English: Actual view checks for reversible selections, clipping, complete paging and hit targets.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include "ui_reader_dictionary.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned painted;
static enum EpdRotation rotation=EPD_ROT_PORTRAIT;
int epd_width(void) { return 1216; }
int epd_height(void) { return 684; }
enum EpdRotation epd_get_rotation(void) { return rotation; }
static int selected=2;
int ttf_font_select(int slot) { int previous=selected;selected=slot;return previous; }
int ttf_ascender_px(int px) { return px; }
int ttf_text_width_px(int px, const char *s) { assert(selected==0);return (int)reader_selection_utf8(s, strlen(s)) * px; }
bool ttf_font_has_text(const char *s) { return !strstr(s, "𠮷"); }
void ttf_draw_text_px(uint8_t *fb, int x, int y, int px, const char *s, int align, int fg, int bg) {
    (void)fb; (void)align; (void)fg; (void)bg;
    assert(x >= 0 && y <= 1216 && px > 0);
    assert(selected==0);
    assert(!*s || reader_selection_utf8(s, strlen(s)));
    ++painted;
}
void ui_fill_rect_fast(EpdRect r, uint8_t gray, uint8_t *fb) {
    (void)gray; (void)fb; assert(r.x >= 0 && r.y >= 0 && r.x+r.width <= 684 && r.y+r.height <= 1216);
}
void ui_draw_separator(uint8_t *fb, int y, int x, int width, uint8_t gray) {
    (void)fb; (void)gray; assert(x >= 0 && x+width <= 684 && y >= 0 && y < 1216);
}
int main(void) {
    size_t bytes=684*1216/2; uint8_t *guard=malloc(bytes+32), *original=malloc(bytes); assert(guard&&original);
    memset(guard,0xa5,bytes+32);uint8_t *fb=guard+16;memset(fb,0x6b,bytes);memcpy(original,fb,bytes);
    book_layout_glyph_t glyphs[]={{0,3,0,0,32,40},{3,3,652,1170,32,46}};
    reader_selection_t s={.active=true,.anchor=0,.focus=1};
    for(unsigned pass=0;pass<2;++pass){
        rotation=pass?EPD_ROT_INVERTED_PORTRAIT:EPD_ROT_PORTRAIT;
        EpdRect area=ui_reader_dictionary_selection(fb,&s,glyphs,2);
        assert(area.x==0&&area.y==0&&area.width==684&&area.height==1216&&memcmp(fb,original,bytes));
        EpdRect physical=ui_rotate_rect_to_fb((EpdRect){10,10,1,1});size_t at=(size_t)physical.y*608+physical.x/2;
        uint8_t mask=physical.x&1?0xf0:0x0f;assert((fb[at]&mask)==((original[at]^mask)&mask));
        physical=ui_rotate_rect_to_fb((EpdRect){400,400,1,1});at=(size_t)physical.y*608+physical.x/2;assert(fb[at]==original[at]);
        ui_reader_dictionary_selection(fb,&s,glyphs,2);assert(!memcmp(fb,original,bytes));
    }
    for(unsigned i=0;i<16;++i)assert(guard[i]==0xa5&&guard[bytes+16+i]==0xa5);
    book_dictionary_result_t r={.status=BOOK_DICTIONARY_OK};
    strcpy(r.reading,"yù\nyú\n更多读音");strcpy(r.variant,"鬱");strcpy(r.example,"这是完整例句。");
    for(unsigned i=0;i<500;++i)strcat(r.definition,"释义");
    ui_reader_dictionary_t v={.open=true,.top=632};
    size_t length=ui_reader_dictionary_content_size(&r),offset=0;unsigned pages=0;
    assert(length>strlen(r.definition));
    while(offset<length){v.offset=offset;ui_reader_dictionary_draw(fb,&v,&r,"踽踽独行");size_t next=ui_reader_dictionary_page_end(&r,offset);assert(next>offset&&next<=length);offset=next;assert(++pages<128);}
    assert(pages>1&&ui_reader_dictionary_action(&v,30,100)==0);
    assert(ui_reader_dictionary_action(&v,570,660)==0);
    assert(ui_reader_dictionary_action(&v,50,1130)==1&&ui_reader_dictionary_action(&v,300,1130)==3&&ui_reader_dictionary_action(&v,550,1130)==2);
    r.definition[0]=0;assert(ui_reader_dictionary_content_size(&r)>strlen(r.reading));
    for(int status=BOOK_DICTIONARY_OK;status<=BOOK_DICTIONARY_BUSY;++status){r.status=status;v.offset=0;ui_reader_dictionary_draw(fb,&v,&r,"𠮷");}
    ui_reader_dictionary_notice(fb);
    assert(painted&&selected==2);free(original);free(guard);
    puts("PASS: actual view, reversible XOR handles at screen edges, complete reading/definition/example paging and error states");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
    work = Path(folder)
    (work / "view.c").write_text((ROOT / "main/ui/ui_reader_dictionary.c").read_text())
    (work / "ui_kit.h").write_text('''#include "epdiy.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
void ui_fill_rect_fast(EpdRect, uint8_t, uint8_t *);
void ui_draw_separator(uint8_t *, int, int, int, uint8_t);
EpdRect ui_rotate_rect_to_fb(EpdRect);
''')
    (work / "ttf_font.h").write_text('''#include <stdbool.h>
#include <stdint.h>
#define TTF_FONT_SLOT_SYSTEM 0
int ttf_font_select(int);
int ttf_ascender_px(int);
int ttf_text_width_px(int,const char *);
bool ttf_font_has_text(const char *);
void ttf_draw_text_px(uint8_t *,int,int,int,const char *,int,int,int);
''')
    unit=UNIT.replace('int main(void)', function('ui_rotate_rect_to_fb', ROOT/'main/ui/ui_kit.c')+'\nint main(void)')
    (work / "test.c").write_text(unit)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I" + str(work), "-I" + str(ROOT / "main/ui"), "-I" + str(ROOT / "main/book"),
                    "-I" + str(ROOT / "tools/ui_gesture_stubs"), "-I" + str(ROOT / "tools/book_source_host_stubs"),
                    str(work / "test.c"), str(work / "view.c"), str(ROOT / "main/book/reader_selection.c"),
                    "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
