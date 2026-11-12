#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify compressed assets against pre-compression hashes and real C render/search paths."""
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import reader_title_style_preview as host

ROOT = Path(__file__).resolve().parents[1]


def values(path, name):
    body = re.search(rf'\b{name}\[[^=]*=\s*\{{(.*?)\}};', path.read_text(), re.S)[1]
    return [int(n, 0) for n in re.findall(r'0x[0-9a-fA-F]+|\d+', body)]


def array(name, kind, data):
    return f'static const {kind} {name}[]={{' + ','.join(map(str, data)) + '};\n'


def fixtures():
    icons = ROOT / 'main/assets/ui_icons.h'
    offsets, data = values(icons, 'ui_icon_offsets'), bytes(values(icons, 'ui_icon_rle'))
    raw = bytearray()
    for start, end in zip(offsets, offsets[1:]):
        for _ in range(72):
            row = bytearray()
            while len(row) < 36:
                assert start < end
                cmd = data[start]; start += 1
                count = (cmd & 127) + 1
                take = 1 if cmd & 128 else count
                assert len(row) + count <= 36 and start + take <= end
                row.extend(data[start:start + take] * (count if cmd & 128 else 1))
                start += take
            raw.extend(row)
        assert start == end
    # These digests pin the original artwork, independent of the new encoders/decoders.
    assert hashlib.sha256(raw).hexdigest() == '21747b01c3fbf7c8fee06c518d856e7cc6495031fa760f7f80dd3d904141f547'
    result = array('reference_icons', 'uint8_t', raw)
    digits = ROOT / 'main/assets/calendar_digits.h'
    widths, offsets, data = [values(digits, n) for n in (
        'calendar_digit_widths', 'calendar_digit_offsets', 'calendar_digit_spans')]
    raw, raw_offsets = bytearray(), []
    for d, width in enumerate(widths):
        raw_offsets.append(len(raw)); at, end = offsets[d:d + 2]
        for _ in range(256):
            assert at < end
            count = data[at]; at += 1
            assert at + 2 * count <= end
            row = bytearray((width + 7) // 8); previous = 0
            for _ in range(count):
                start, length = data[at:at + 2]; at += 2
                assert length > 0 and previous <= start < start + length <= width
                for x in range(start, start + length): row[x // 8] |= 1 << (7 - x % 8)
                previous = start + length
            raw.extend(row)
        assert at == end
    assert hashlib.sha256(raw).hexdigest() == '2663b07e912832e6b97d65d49332c8bde2b1db8a136e3c91fd0872142f7b898a'
    result += array('reference_digits', 'uint8_t', raw) + array('reference_digit_offsets', 'uint32_t', raw_offsets)
    table = ROOT / 'components/book_search/search_table.h'
    pages, masks, starts, ranks, groups = [values(table, n) for n in (
        'search_pages', 'search_page_masks', 'search_page_starts', 'search_page_ranks', 'search_groups')]
    cps = []
    for i, page in enumerate(pages):
        assert starts[i] == len(cps)
        for word in range(8):
            assert ranks[i * 8 + word] == len(cps) - starts[i]
            cps.extend((page << 8) + word * 32 + bit for bit in range(32) if masks[i * 8 + word] & (1 << bit))
    assert len(cps) == len(groups) == 41923
    digest = hashlib.sha256(b''.join(struct.pack('<IH', cp, group) for cp, group in zip(cps, groups))).hexdigest()
    assert digest == '4d68d1e8c5469edba94111e1b0a79ab867f65aae66319484455468714e8db571'
    return result + array('reference_codepoints', 'uint32_t', cps)


def unit():
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "assets/ui_icons.h"
#include "assets/calendar_digits.h"
#include "search_table.h"
#define UI_GRAY_BLACK 0
#define UI_GRAY_WHITE 255
#define UI_GRAY_LIGHT 0x90
enum EpdRotation {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
static enum EpdRotation rotation;
static enum EpdRotation epd_get_rotation(void){return rotation;}
static int epd_width(void){return 192;}
static int epd_height(void){return 160;}
static void epd_draw_pixel(int x,int y,uint8_t value,uint8_t *fb){
    int px=x,py=y;
    if(rotation==EPD_ROT_PORTRAIT){px=191-y;py=x;}
    else if(rotation==EPD_ROT_INVERTED_LANDSCAPE){px=191-x;py=159-y;}
    else if(rotation==EPD_ROT_INVERTED_PORTRAIT){px=y;py=159-x;}
    if(px<0||px>=192||py<0||py>=160)return;
    unsigned index=(py*192+px)/2;
    if(px&1)fb[index]=(fb[index]&0x0f)|(value&0xf0);
    else fb[index]=(fb[index]&0xf0)|(value>>4);
}
static uint8_t epd_get_pixel(int x,int y,int width,int height,const uint8_t *fb){
    if(x<0||x>=width||y<0||y>=height)return 0xff;
    uint8_t b=fb[(y*width+x)/2];return (x&1)?b&0xf0:(b&15)<<4;
}
'''
    kit = ROOT / 'main/ui/ui_kit.c'
    code += host.function('ui_control_gray', ROOT / 'main/ui/ui_kit.h') + '\n'
    for name in ('ui_read_pixel', 'icon_row_decode', 'icon_nibble', 'ui_draw_icon'):
        code += host.function(name, kit) + '\n'
    code += host.function('number', ROOT / 'main/ui/ui_calendar.c') + '\n'
    code += host.function('lookup', ROOT / 'components/book_search/book_search.c') + '\n'
    code += fixtures()
    code += r'''
static int old_nibble(const uint8_t *mask,int x,int y){
    if(x<0)x=0;if(x>71)x=71;if(y<0)y=0;if(y>71)y=71;
    unsigned pixel=y*72+x;uint8_t b=mask[pixel/2];return pixel&1?b&15:b>>4;
}
static void old_icon(uint8_t *fb,int cx,int cy,int size,int icon,uint8_t gray){
    const uint8_t *mask=reference_icons+icon*2592;gray=ui_control_gray(gray);
    int32_t step=(72<<16)/size,half=step/2-(1<<15);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
        int32_t sy=y*step+half,sx=x*step+half;int x0=sx>>16,y0=sy>>16,fx=sx&65535,fy=sy&65535;
        int top=(old_nibble(mask,x0,y0)*(65536-fx)+old_nibble(mask,x0+1,y0)*fx)>>16;
        int bottom=(old_nibble(mask,x0,y0+1)*(65536-fx)+old_nibble(mask,x0+1,y0+1)*fx)>>16;
        int cover=(top*(65536-fy)+bottom*fy)>>16;if(!cover)continue;
        int px=cx-size/2+x,py=cy-size/2+y,bg=ui_read_pixel(fb,px,py)<<4;
        epd_draw_pixel(px,py,(bg*(15-cover)+gray*cover+7)/15,fb);
    }
}
static void old_number(uint8_t *fb,int value,int x,int top,int height){
    char digits[4];snprintf(digits,sizeof(digits),"%d",value);
    for(const char *p=digits;*p;++p){
        unsigned d=*p-'0';int sw=calendar_digit_widths[d],stride=(sw+7)/8,width=sw*height/256;
        const uint8_t *bits=reference_digits+reference_digit_offsets[d];
        for(int y=0;y<height;++y)for(int dx=0;dx<width;++dx){
            int sx=dx*sw/width,sy=y*256/height;
            if(bits[sy*stride+sx/8]&(1u<<(7-sx%8)))epd_draw_pixel(x+dx,top+y,0,fb);
        }
        x+=width+8*height/256;
    }
}
static int old_lookup(uint32_t cp){
    size_t lo=0,hi=41923;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(reference_codepoints[mid]<cp)lo=mid+1;else hi=mid;}
    return lo<41923&&reference_codepoints[lo]==cp?search_groups[lo]:-1;
}
static uint8_t actual[192*160/2+32],expected[sizeof(actual)];
static void background(unsigned seed){
    for(unsigned i=0;i<sizeof(actual);++i)actual[i]=(uint8_t)(i*37+seed*113);
    memcpy(expected,actual,sizeof(actual));
}
int main(void){
    const int sizes[]={1,12,24,36,48,72,96,127};
    const uint8_t grays[]={0,0x90,255};
    const int positions[][2]={{80,80},{0,0},{159,191}};
    for(unsigned icon=0;icon<UI_ICON_COUNT;++icon){
        const uint8_t *cursor=ui_icon_rle+ui_icon_offsets[icon],*end=ui_icon_rle+ui_icon_offsets[icon+1];
        for(unsigned row=0;row<72;++row){
            uint8_t decoded[36];const uint8_t *begin=cursor;
            assert(icon_row_decode(&cursor,end,decoded));
            assert(!memcmp(decoded,reference_icons+icon*2592+row*36,36));
            for(const uint8_t *cut=begin;cut<cursor;++cut){const uint8_t *p=begin;assert(!icon_row_decode(&p,cut,decoded));}
        }
        assert(cursor==end);
        for(unsigned s=0;s<sizeof(sizes)/sizeof(*sizes);++s)for(int rot=0;rot<4;++rot)
        for(unsigned pos=0;pos<3;++pos)for(unsigned gray=0;gray<3;++gray){
            rotation=rot;background(gray);
            ui_draw_icon(actual+16,positions[pos][0],positions[pos][1],sizes[s],icon,grays[gray]);
            old_icon(expected+16,positions[pos][0],positions[pos][1],sizes[s],icon,grays[gray]);
            assert(!memcmp(actual,expected,sizeof(actual)));
        }
    }
    uint8_t invalid[]={0xff,0};const uint8_t *p=invalid;uint8_t row[36];assert(!icon_row_decode(&p,invalid+2,row));
    background(1);ui_draw_icon(NULL,0,0,32,0,0);ui_draw_icon(actual+16,0,0,0,0,0);
    ui_draw_icon(actual+16,0,0,32,-1,0);ui_draw_icon(actual+16,0,0,32,UI_ICON_COUNT,0);
    assert(!memcmp(actual,expected,sizeof(actual)));
    const int heights[]={1,32,78,112,184,256,320};
    for(int value=0;value<100;++value)for(unsigned h=0;h<sizeof(heights)/sizeof(*heights);++h)
    for(int rot=0;rot<4;++rot){
        rotation=rot;background(value);
        number(actual+16,value,-17,-11,heights[h]);old_number(expected+16,value,-17,-11,heights[h]);
        assert(!memcmp(actual,expected,sizeof(actual)));
    }
    size_t index=0;
    for(uint32_t cp=0;cp<0x110000;++cp){
        int expected_group=-1;
        if(index<41923&&reference_codepoints[index]==cp)expected_group=search_groups[index++];
        assert(lookup(cp)==expected_group);
    }
    assert(index==41923&&lookup(UINT32_MAX)==-1);
    volatile unsigned sum=0;clock_t start=clock();
    for(unsigned n=0;n<20;++n)for(unsigned i=0;i<41923;++i)sum+=old_lookup(reference_codepoints[i]);
    double old_seconds=(double)(clock()-start)/CLOCKS_PER_SEC;start=clock();
    for(unsigned n=0;n<20;++n)for(unsigned i=0;i<41923;++i)sum+=lookup(reference_codepoints[i]);
    printf("PASS: %u icons across scales/rotations/backgrounds/clipping; 100 numbers; malformed rows; all Unicode mappings; ASan/UBSan\n", (unsigned)UI_ICON_COUNT);
    printf("Host lookup benchmark (not S31): original %.4fs, compact %.4fs, checksum %u\n",old_seconds,(double)(clock()-start)/CLOCKS_PER_SEC,sum);
}
'''
    return code


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='compact-assets-') as folder:
        path = Path(folder); (path / 'test.c').write_text(unit())
        subprocess.run(['cc', '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'main'), '-I' + str(ROOT / 'components/book_search'),
                        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
