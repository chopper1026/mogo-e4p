/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：真实 UTF-8 选区的正反向、跨行、上限与字格命中测试。
 * English: Actual UTF-8 selection tests for reverse/cross-line ranges, limits and cell hits. */
#include "reader_selection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const char *text="踽踽独行\n𠮷\xEF\xB8\x80";
    book_layout_glyph_t g[]={{0,3,10,10,20,20},{3,3,30,10,20,20},{6,3,10,40,20,20},{9,3,30,40,20,20},
        {13,4,10,70,20,20},{17,3,30,70,1,20}};
    reader_selection_t s={0};
    assert(!reader_selection_begin(&s,text,strlen(text),g,6,0,1,2,2));
    assert(reader_selection_begin(&s,text,strlen(text),g,6,0,1,15,15));assert(!strcmp(s.query,"踽"));
    assert(reader_selection_move(&s,text,strlen(text),g,6,45,45));assert(!strcmp(s.query,"踽踽独行"));
    reader_selection_cancel(&s);assert(!s.active);
    assert(reader_selection_begin(&s,text,strlen(text),g,6,0,1,45,45));
    assert(reader_selection_move(&s,text,strlen(text),g,6,15,15));assert(!strcmp(s.query,"踽踽独行"));
    assert(reader_selection_begin(&s,text,strlen(text),g,6,0,1,15,75));assert(!strcmp(s.query,"𠮷\xEF\xB8\x80"));
    assert(reader_selection_begin(&s,text,strlen(text),g,6,0,1,30,75));assert(!strcmp(s.query,"𠮷\xEF\xB8\x80"));
    assert(reader_selection_utf8("\xf4\x8f\xbf\xbf",4)==1);
    char one[READER_SELECTION_BYTES+1];
    size_t next=reader_selection_character("𠮷\xEF\xB8\x80踽",0,one);
    assert(next==7&&!strcmp(one,"𠮷\xEF\xB8\x80"));
    assert(reader_selection_character("𠮷\xEF\xB8\x80踽",next,one)==10&&!strcmp(one,"踽"));
    assert(reader_selection_character("　\n踽",0,one)==7&&!strcmp(one,"踽"));
    const char *invalid[]={"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xe4\xb8"};
    for(unsigned i=0;i<4;++i)assert(!reader_selection_utf8(invalid[i],strlen(invalid[i])));
    char many[34];memset(many,'a',33);many[33]=0;book_layout_glyph_t cells[33];
    for(int i=0;i<33;++i)cells[i]=(book_layout_glyph_t){i,1,i*5,0,5,10};
    assert(reader_selection_begin(&s,many,33,cells,33,1,2,1,1));
    assert(!reader_selection_move(&s,many,33,cells,33,163,1));assert(!strcmp(s.query,"a"));
    assert(reader_selection_move(&s,many,33,cells,33,158,1)&&strlen(s.query)==32);
    puts("PASS: actual UTF-8 selection, exact/reverse/cross-line ranges, extension codepoint, variation cluster and bounded rejection");
}
