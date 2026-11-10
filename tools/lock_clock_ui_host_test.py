#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Render the production lock clock with native 4bpp pixels, fonts and battery icon; check numeral masks and refresh bounds."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image, ImageChops, ImageDraw, ImageFont
import reader_title_style_preview as host
from run_reader_baseline import toolchain
ROOT=Path(__file__).resolve().parents[1]

def run(work):
    work.mkdir(parents=True,exist_ok=True)
    host.write_shims(work)
    with (work/'esp_heap_caps.h').open('a') as header:header.write('\nvoid *heap_caps_realloc(void *,size_t,int);\n')
    code=host.host_preamble()
    begin,end=code.index('static struct {void *p;'),code.index('static enum EpdRotation')
    code=code[:begin]+r'''
void *heap_caps_malloc(size_t n,int c){(void)c;return malloc(n);}
void *heap_caps_calloc(size_t n,size_t z,int c){(void)c;return calloc(n,z);}
void *heap_caps_realloc(void *p,size_t n,int c){(void)c;return realloc(p,n);}
void heap_caps_free(void *p){free(p);}
size_t heap_caps_get_free_size(int c){(void)c;return 16*1024*1024;}
size_t heap_caps_get_largest_free_block(int c){(void)c;return 16*1024*1024;}
void lock_pin_wipe(void *p,unsigned n){memset(p,0,n);}
'''+code[end:]
    code+='\n#undef UI_ICON_CHEVRON_RIGHT\n#undef UI_ICON_CHEVRON_LEFT\n#undef UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT\n#include "assets/ui_icons.h"\n#include "ui_lock_clock.h"\n'
    code+=host.epdiy_slices()+host.function('ui_control_gray',ROOT/'main/ui/ui_kit.h')+'\n'
    for name in ('clamp_radius','draw_arc','ui_draw_round_rect_raw','ui_draw_round_rect','ui_fill_round_rect_raw','ui_fill_round_rect','ui_read_pixel','icon_nibble','ui_draw_icon'):
        code+=host.function(name,ROOT/'main/ui/ui_kit.c')+'\n'
    code+=r'''
#define UI_NAV_BOLT_ICON_PX 26
typedef struct {int percent;bool charging;} pmu_snapshot_t;
static pmu_snapshot_t sample={76,false};
static const pmu_snapshot_t *device_power_get(void){return &sample;}
static int pmu_battery_percent(const pmu_snapshot_t *p){return p->percent;}
static bool pmu_battery_charging(const pmu_snapshot_t *p){return p->charging;}
'''
    code+=host.function('ui_nav_battery_draw',ROOT/'main/ui/ui_nav.c')+'\n'
    code+=re.sub(r'^#include[^\n]*\n','',(ROOT/'main/ui/ui_lock_clock.c').read_text(),flags=re.M)
    code+='\n#include "assets/clock_digits.h"\n' if '#include "assets/clock_digits.h"' not in code else ''
    # Generated data must precede the production functions.
    code=code.replace('EpdRect ui_lock_clock_area(void)', '#include "assets/clock_digits.h"\nEpdRect ui_lock_clock_area(void)',1)
    code+=r'''
static void check_bounds(uint8_t *fb,ui_lock_clock_face_t face){
    memset(fb,0x55,684*1216/2);ui_lock_clock_draw(fb,&face);EpdRect a=ui_lock_clock_area();
    for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
        if(x<a.x||x>=a.x+a.width||y<a.y||y>=a.y+a.height)assert(pixel_at(fb,x,y)==0x50);
}
int main(int argc,char **argv){
 assert(argc==2&&ttf_font_open_builtin()==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);char path[1024];
 ui_lock_clock_face_t face={true,16,48,11,10,2};
 for(int digit=0;digit<10;++digit){face.hour=digit*2;face.minute=digit*6;face.weekday=digit%7;check_bounds(fb,face);}
 face=(ui_lock_clock_face_t){true,16,48,11,10,2};
 const char *names[]={"clock-lockscreen-host","clock-midnight","clock-last-minute","clock-uncalibrated","clock-charging"};
 for(int i=0;i<5;++i){
  if(i==1){face.hour=face.minute=0;face.month=1;face.day=1;face.weekday=5;}
  if(i==2){face.hour=23;face.minute=59;face.month=12;face.day=31;face.weekday=4;}
  if(i==3)face.valid=false;
  if(i==4){face=(ui_lock_clock_face_t){true,16,48,11,10,2};sample.charging=true;}
  check_bounds(fb,face);memset(fb,255,684*1216/2);ui_lock_clock_draw(fb,&face);
  snprintf(path,sizeof(path),"%s/%s.raw",argv[1],names[i]);save(path,fb);
 }
 free(fb);ttf_font_unload();puts("PASS: production 4bpp lock-clock pixels, all digits, valid/invalid dates, battery and refresh-region containment");
}
'''
    (work/'test.c').write_text(code)
    env,compiler=toolchain(work)
    flags=compiler+['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-fsanitize=address,undefined','-g']
    for path in (work,ROOT/'main',ROOT/'main/ui',ROOT/'main/font',ROOT/'main/boot',ROOT/'main/diagnostics',ROOT/'main/display',ROOT/'main/settings'):flags+=['-I'+str(path)]
    units=[work/'test.c',work/'assets.c']+[ROOT/'main/font'/name for name in ('ttf_font.c','ttf_font_source.c','ttf_block_cache.c','ttf_glyph_cache.c')]
    subprocess.run(flags+list(map(str,units))+['-Wl,-dead_strip','-lm','-o',str(work/'test')],env=env,check=True)
    subprocess.run([str(work/'test'),str(work)],env=env,check=True)
    for raw in work.glob('clock*.raw'):
        Image.frombytes('L',(684,1216),raw.read_bytes()).point(lambda v:v*17//16).save(raw.with_suffix('.png'))
    # Independently rasterize the approved original font, not the generated header.
    font=ImageFont.truetype(str(ROOT/'main/assets/clock-font/ChakraPetch-Bold.ttf'),224)
    expected=Image.new('L',(684,1216),255)
    for text,baseline in [('16',480),('48',688)]:
        for column,digit in enumerate(text):
            glyph=Image.new('L',(177,312),255);ImageDraw.Draw(glyph).text((16,224),digit,font=font,anchor='ls',fill=0)
            bounds=ImageChops.invert(glyph).getbbox();crop=glyph.crop(bounds)
            expected.paste(crop,(194+column*151+(145-crop.width)//2,baseline-224+bounds[1]))
    expected=expected.point(lambda v:(v>>4)*17)
    actual=Image.open(work/'clock-lockscreen-host.png')
    assert ImageChops.difference(expected.crop((0,288,684,700)),actual.crop((0,288,684,700))).getbbox() is None,'Approved numeral pixels changed'
    print('PASS: native clock numerals match the approved Chakra Petch 224px source pixels')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path);args=parser.parse_args()
    if args.output:run(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='lock-clock-ui-') as directory:run(Path(directory))
