#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：使用真实字体、图标与阅读面板绘制生成设计预览。/ English: Render actual reader sheets, glyphs and icons for design review."""
import argparse
import platform
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
import reader_title_style_preview as host
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
KIT = ROOT / 'main/ui/ui_kit.c'


def unit():
    code = host.host_preamble()
    code = re.sub(r'^static [^\n]*\bui_font_\w+\([^\n]*\n', '', code, flags=re.M)
    code += r'''
#undef UI_ICON_CHEVRON_RIGHT
#undef UI_ICON_CHEVRON_LEFT
#undef UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT
#undef READER_SHEET_BACK_PX
#define READER_SHEET_BACK_PX 52
#include "ui_font.h"
#include "assets/ui_icons.h"
#include "ui_reader_settings.h"
#include "book/book_font_size.h"
#include "book/book_layout.h"
#include <limits.h>
void *heap_caps_realloc(void *old,size_t n,int caps){
 if(!old)return heap_caps_malloc(n,caps);
 for(unsigned i=0;i<8192;++i)if(allocations[i].p==old){
  size_t before=allocations[i].bytes;if(n>capacity-live+before)return NULL;
  void *next=realloc(old,n);if(!next)return NULL;
  allocations[i].p=next;allocations[i].bytes=n;live=live-before+n;return next;
 }
 abort();
}
static unsigned font_scale=100;
static unsigned app_settings_system_font_size(void){return font_scale;}
static void ui_click_feedback_register(uint8_t *fb,EpdRect r,int kind,int icon){(void)fb;(void)r;(void)kind;(void)icon;}
uint8_t ui_read_pixel(const uint8_t *fb,int x,int y);
'''
    code += host.epdiy_slices()
    code += host.function('ui_control_gray', ROOT / 'main/ui/ui_kit.h') + '\n'
    for name in ('clamp_radius', 'draw_arc', 'ui_draw_round_rect_raw', 'ui_draw_round_rect',
                 'ui_fill_round_rect_raw', 'ui_fill_round_rect', 'ui_inset_rect', 'ui_draw_control_frame',
                 'ui_draw_separator', 'ui_hairline', 'icon_row_decode', 'icon_nibble', 'ui_draw_icon', 'ui_read_pixel',
                 'ui_text_effective_px', 'ui_text_fixed_context_width_px', 'ui_text_fixed_width_px',
                 'ui_text_fixed_context_vc', 'ui_text_fixed_vc'):
        code += host.function(name, KIT) + '\n'
    code += host.function('ui_text_box', KIT).replace('void ui_text_box(', 'static void actual_text_box(') + '\n'
    code += r'''
static unsigned text_checks;
static void ui_text_box(uint8_t *fb,EpdRect box,int px,const char *text,enum EpdFontFlags align,bool inverted){
 assert(box.x>=0&&box.x+box.width<=684&&box.y>=0&&box.y+box.height<=1216);
 if(!ui_font_has_text(text)){fprintf(stderr,"Missing UI glyphs: %s\n",text);abort();}
 int fit=ui_text_effective_px(px),above,below;
 while(fit>1){ui_font_measure_line_px(fit,text,&above,&below);if(ui_text_fixed_width_px(fit,text)<=box.width&&above+below+2<=box.height)break;--fit;}
 assert(fit>=12);++text_checks;
 actual_text_box(fb,box,px,text,align,inverted);
}
static void copy_text(char *out,size_t cap,const char *text){snprintf(out,cap,"%s",text);}
static int s_px=36,s_reader_preview_px=36,s_reader_slider=-1;
static bool s_reader_fullscreen=true;
static ui_reader_settings_group_t s_reader_settings_group;
static int app_settings_book_weight_fine(void){return 0;}
static int app_settings_book_title_style(void){return 3;}
static bool app_settings_book_fonts(void){return true;}
static bool app_settings_book_antialias(void){return true;}
static unsigned guide_style;
static bool guide_follow;
static unsigned guide_invalidations;
static unsigned app_settings_book_reading_line(void){return guide_style;}
static void app_settings_set_book_reading_line(unsigned style){guide_style=style;}
static bool app_settings_book_reading_line_follow_text(void){return guide_follow;}
static void app_settings_set_book_reading_line_follow_text(bool on){guide_follow=on;}
static int guide_offset;
static int app_settings_book_reading_line_offset(void){return guide_offset;}
static void app_settings_set_book_reading_line_offset(int offset){guide_offset=offset;}
static void invalidate_prep(void){++guide_invalidations;}
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static unsigned app_settings_reader_full_pages(void){return 10;}
static bool app_settings_reader_turn_effect(void){return false;}
static bool app_settings_reader_vertical_turn(void){return false;}
static unsigned app_settings_reader_tap_mode(void){return app_settings_reader_vertical_turn()?1:0;}
#define APP_READER_TAP_GRID 2

static bool app_settings_reader_statusbar(void){return true;}
static bool app_settings_reader_hide_images(void){return false;}
static bool app_settings_book_vertical(void){return false;}
static bool app_settings_book_styles(void){return true;}
static bool app_settings_reader_touch_disabled(void){return false;}
static bool bookmark_is_current(void){return false;}
#define READER_WIDTH 684
#define READER_HEIGHT 1216
#define READER_BAR_TOP 1096
#define BOOK_TOOL_COUNT 5
#define READER_TOOL_ICON_PX 40
#define BOOK_PX_MIN BOOK_READER_PX_MIN
#define BOOK_PX_MAX BOOK_READER_PX_MAX
'''
    code += host.enum_block(BOOK, 'READER_PANEL_NONE', 'reader_panel_t')
    code += host.enum_block(ROOT / 'main/settings/settings.h', 'APP_BOOK_TITLE_STYLE_EMBLEM', 'app_book_title_style_t')
    code += host.string_table(BOOK, 's_title_style_names')
    code += host.function('reader_title_style_name', BOOK) + '\n'
    code += 'static reader_panel_t s_reader_panel;\n'
    for name in ('s_rule_follow', 's_rule_offset_up', 's_rule_offset_reset', 's_rule_offset_down'):
        code += re.search(r'^static const EpdRect ' + name + r' = .*;', BOOK.read_text(), re.M)[0] + '\n'
    for name in ('s_font_card', 's_weight_card', 's_title_style_card', 's_layout_card', 's_bookfont_card', 's_rule_card', 's_antialias_card'):
        code += re.search(r'^#define ' + name + r' .*', BOOK.read_text(), re.M)[0] + '\n'
    for name in ('reader_tools_area', 'tool_rect', 'reader_more_rect', 'reader_ui_extra', 'reader_font_top',
                 'reader_font_card', 'reader_slider_rect', 'reader_panel_text_vc', 'reader_panel_text', 'draw_sheet', 'reader_sheet_back_rect', 'draw_sheet_back',
                 'draw_pill_slider', 'draw_reading_font_name', 'draw_font_pair', 'reader_weight_value',
                 'draw_reading_switch', 'draw_font_settings', 'rule_style_rect', 'draw_rule_settings', 'draw_reader_more', 'reading_setting_rect',
                 'reading_settings_top', 'draw_reading_settings', 'draw_reader_tool_icon'):
        code += host.function(name, BOOK) + '\n'
    code += re.sub(r'^#include[^\n]*\n', '', (ROOT / 'main/ui/ui_reader_settings.c').read_text(), flags=re.M)
    # Only leaf sheets not shown in this preview are replaced. All new screens use actual production paint.
    for name in ('draw_weight_settings', 'draw_layout_settings', 'draw_font_picker',
                 'draw_reader_stats', 'draw_reader_bookmarks', 'draw_reader_stats_recent', 'draw_turn_settings',
                 'draw_tap_grid', 'draw_key_settings', 'draw_key_picker', 'draw_chrome_settings', 'draw_chrome_picker',
                 'draw_title_style_picker', 'draw_note_sheet'):
        code += f'static void {name}(uint8_t *fb){{(void)fb;}}\n'
    code += 'static void ui_text_set_system_scale(bool enabled){s_system_text_scale=enabled;}\n'
    code += host.function('draw_reader_panel', BOOK) + '\n'
    actions = host.function('reader_panel_action', BOOK)
    first = actions.index('    if (s_reader_panel == READER_PANEL_RULE_SETTINGS) {')
    last = actions.index('    if (s_reader_panel == READER_PANEL_FONT_PICKER)', first)
    code += 'static app_redraw_t guide_action(int x,int y){\n' + actions[first:last] + 'return APP_REDRAW_NONE;\n}\n'
    code += r'''
static void background(uint8_t *fb){
 memset(fb,255,684*1216/2);
 ui_text_fixed_vc(fb,36,40,26,"08:30",EPD_DRAW_ALIGN_LEFT,false);
 ui_text_fixed_vc(fb,648,40,26,"88%",EPD_DRAW_ALIGN_RIGHT,false);
 static const char *const lines[]={"阅读，是让时间慢下来。", "一页一页，读完一本书。", "每一次阅读，都有新的发现。", "", "从文字中看见更多，", "也在安静中重新认识自己。"};
 for(int i=0;i<16;++i)ttf_draw_text_px(fb,48,170+i*58,36,lines[i%6],EPD_DRAW_ALIGN_LEFT,0,15);
}
static void body_guides(uint8_t *fb,const char *output){
 static const char text[]="阅读，让时间慢下来。\n短句。\n\n    abc   \n每一次阅读，都有新的发现。";
 EpdRect body={48,176,588,900};
 uint8_t *reference=malloc(684*1216/2);assert(reference);
 for(unsigned vertical=0;vertical<2;++vertical){
  book_layout_set_vertical(vertical);book_layout_set_first_line_indent(2);
  book_layout_set_reading_line_offset(0);
  book_layout_set_spacing(150,50);book_layout_enable_selection(true);
  assert(book_layout_build(text,strlen(text),body,36));
  memset(reference,255,684*1216/2);book_layout_set_reading_line(0);
  book_layout_draw_page(reference,0,body,36);
  for(unsigned style=1;style<=3;++style)for(unsigned follow=0;follow<2;++follow){
   memset(fb,255,684*1216/2);
   book_layout_set_reading_line(style);book_layout_set_reading_line_follow_text(follow);
   book_layout_draw_page(fb,0,body,36);
   size_t count=0;const book_layout_glyph_t *glyphs=book_layout_glyphs(0,&count);assert(glyphs&&count);
   unsigned guide_pixels=0;
   if(follow&&!vertical){
    for(int y=body.y;y<body.y+body.height;++y)for(int x=body.x;x<body.x+body.width;++x){
     if(ui_read_pixel(fb,x,y)==ui_read_pixel(reference,x,y))continue;
     assert(ui_read_pixel(fb,x,y)==5);++guide_pixels;
     bool row=false,inside=false;
     for(size_t at=0;at<count;++at){
      const book_layout_glyph_t *g=&glyphs[at];
      if(y!=g->y+45&&y!=g->y+46)continue;
      if((unsigned char)text[g->start]<=0x20)continue;
      row=true;
      int first=INT_MAX,last=0;
      for(size_t i=0;i<count;++i)if(glyphs[i].y==g->y&&(unsigned char)text[glyphs[i].start]>0x20){
       if(glyphs[i].x<first)first=glyphs[i].x;
       if(glyphs[i].x+glyphs[i].width>last)last=glyphs[i].x+glyphs[i].width;
      }
      if(x>=first&&x<last)inside=true;
     }
     assert(row&&inside);
    }
    assert(guide_pixels>0);
   }
   char path[1024];snprintf(path,sizeof(path),"%s/body-guides-%u-%u-%u.raw",output,vertical,style,follow);save(path,fb);
  }
 }
 free(reference);book_layout_free();book_layout_set_vertical(false);
}
static void guide_text_ink(uint8_t *fb){
 const char text[]="pgjy\n文字下方";EpdRect body={48,176,588,160};
 uint8_t *reference=malloc(684*1216/2);assert(reference);
 book_layout_set_first_line_indent(0);
 for(unsigned spacing=110;spacing<=150;spacing+=40){
  book_layout_set_spacing(spacing,50);assert(book_layout_build(text,strlen(text),body,36));
  memset(reference,255,684*1216/2);book_layout_set_reading_line(0);book_layout_draw_page(reference,0,body,36);
  for(unsigned follow=0;follow<2;++follow)for(int offset=-8;offset<=8;offset+=8){
   memset(fb,255,684*1216/2);book_layout_set_reading_line(3);book_layout_set_reading_line_follow_text(follow);
   book_layout_set_reading_line_offset(offset);book_layout_draw_page(fb,0,body,36);
   for(int y=body.y;y<body.y+body.height;++y)for(int x=body.x;x<body.x+body.width;++x)
    if(ui_read_pixel(reference,x,y)==0)assert(ui_read_pixel(fb,x,y)==0);
  }
 }
 free(reference);book_layout_free();
}
int main(int argc,char **argv){
 assert(argc==3);assert(ttf_font_open(argv[2])==ESP_OK);assert(ttf_font_ready());
 uint8_t *fb=malloc(684*1216/2);assert(fb);
 static const char *const names[]={"toolbar","typography","settings-input","settings-page","settings-refresh","more"};
 for(font_scale=100;font_scale<=200;font_scale+=50)for(int frame=0;frame<6;++frame){
  background(fb);
  s_reader_panel=frame==0?READER_PANEL_TOOLS:frame==1?READER_PANEL_FONT_SETTINGS:frame==5?READER_PANEL_MORE:READER_PANEL_REFRESH_SETTINGS;
  s_reader_settings_group=frame-2;
  draw_reader_panel(fb);
  char path[1024];snprintf(path,sizeof(path),"%s/%s-%u.raw",argv[1],names[frame],font_scale);save(path,fb);
 }
 for(font_scale=100;font_scale<=200;font_scale+=50)for(guide_style=0;guide_style<4;++guide_style)for(unsigned follow=0;follow<2;++follow){
  guide_follow=follow;s_reader_panel=READER_PANEL_RULE_SETTINGS;
  background(fb);draw_reader_panel(fb);
  char path[1024];snprintf(path,sizeof(path),"%s/guides-%u-%u-%u.raw",argv[1],guide_style,follow,font_scale);save(path,fb);
 }
 s_reader_panel=READER_PANEL_RULE_SETTINGS;
 for(unsigned style=0;style<4;++style){
  EpdRect card=rule_style_rect(style);
  assert(guide_action(card.x+card.width/2,card.y+card.height/2)==APP_REDRAW_PAGE&&guide_style==style);
 }
 bool prior=guide_follow;
 assert(guide_action(600,980)==APP_REDRAW_PAGE&&guide_follow!=prior);
 assert(guide_action(56,950)==APP_REDRAW_PAGE&&guide_follow==prior);
 unsigned before=guide_invalidations;
 assert(guide_action(650,1050)==APP_REDRAW_NONE&&guide_invalidations==before);
 assert(guide_action(60,1130)==APP_REDRAW_PAGE&&guide_offset==-2);
 assert(guide_action(300,1130)==APP_REDRAW_PAGE&&guide_offset==0);
 assert(guide_action(500,1130)==APP_REDRAW_PAGE&&guide_offset==2);
 guide_style=0;before=guide_invalidations;
 assert(guide_action(60,1130)==APP_REDRAW_NONE&&guide_offset==2&&guide_invalidations==before);
 EpdRect back=reader_sheet_back_rect(580);
 assert(guide_action(back.x+1,back.y+1)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_FONT_SETTINGS);
 body_guides(fb,argv[1]);
 guide_text_ink(fb);
 printf("PASS: 42 actual reader renders, %u fitted UI text bounds/glyph checks; guide styles/follow/offset/back touch dispatch\n",text_checks);
 free(fb);return 0;
}
'''
    return code


def run(output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='reader-toolbar-') as directory:
        work = Path(directory)
        host.write_shims(work)
        (work / 'esp_heap_caps.h').write_text((work / 'esp_heap_caps.h').read_text() + '\nvoid *heap_caps_realloc(void *,size_t,int);\n')
        (work / 'miniz.h').write_text('''#pragma once
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
        payload = (ROOT / 'main/assets/ui-hanzi.bin').read_bytes()
        with (work / 'assets.c').open('a') as assets:
            assets.write('const uint8_t hanzi[] asm("_binary_ui_hanzi_bin_start")={' + ','.join(map(str, payload)) + '};\n')
            assets.write(f'asm(".globl _binary_ui_hanzi_bin_end\\n.set _binary_ui_hanzi_bin_end, _binary_ui_hanzi_bin_start + {len(payload)}");\n')
        (work / 'epdiy.h').write_text((work / 'epdiy.h').read_text().replace(
            'EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT',
            'EPD_DRAW_ALIGN_LEFT=2, EPD_DRAW_ALIGN_CENTER=8, EPD_DRAW_ALIGN_RIGHT=4'))
        (work / 'test.c').write_text(unit())
        env, compiler = toolchain(work)
        flags = compiler + ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
                            '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-I'+str(work), '-I'+str(ROOT/'main'), '-I'+str(ROOT/'main/font'),
                            '-I'+str(ROOT/'main/ui'), '-I'+str(ROOT/'main/settings'), '-I'+str(ROOT/'main/diagnostics')]
        units = [work/'test.c', work/'assets.c', ROOT/'main/book/book_layout.c'] + [ROOT/'main/font'/name for name in (
            'ttf_font.c', 'ttf_font_source.c', 'ttf_block_cache.c', 'ttf_glyph_cache.c', 'ui_font.c', 'ui_hanzi.c')]
        linker = '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections'
        subprocess.run(flags+list(map(str, units))+[linker, '-lm', '-lz', '-o', str(work/'test')], env=env, check=True)
        subprocess.run([str(work/'test'), str(output), str(ROOT/'main/assets/builtin.ttf')], env=env, check=True)
    for raw in output.glob('*.raw'):
        Image.frombytes('L', (684,1216), raw.read_bytes()).point(lambda v: min(255,v*255//240)).save(raw.with_suffix('.png'))
        raw.unlink()
    face='/System/Library/Fonts/Supplemental/Arial Unicode.ttf'
    if Path(face).exists():
        panels=[('toolbar','底部工具栏'),('typography','字体与排版'),('settings-page','设置 · 页面'),('settings-input','设置 · 操作')]
        canvas=Image.new('RGB',(4*342+5*24,716),'#ededeb');draw=ImageDraw.Draw(canvas)
        font=ImageFont.truetype(face,20)
        for index,(name,label) in enumerate(panels):
            x=24+index*366
            draw.text((x,22),label,font=font,fill='#222222')
            canvas.paste(Image.open(output/f'{name}-100.png').resize((342,608)),(x,68))
        canvas.save(output/'overview.png')
        panels=[('guides-3-0-100','实线 · 铺满整行'),('guides-3-1-100','实线 · 跟随正文'),('guides-1-1-200','跟随正文 · 200%字号')]
        canvas=Image.new('RGB',(3*342+4*24,716),'#ededeb');draw=ImageDraw.Draw(canvas)
        for index,(name,label) in enumerate(panels):
            x=24+index*366
            draw.text((x,22),label,font=font,fill='#222222')
            canvas.paste(Image.open(output/f'{name}.png').resize((342,608)),(x,68))
        canvas.save(output/'guides-overview.png')
        panels=[('body-guides-0-3-0','实线 · 铺满整行'),('body-guides-0-3-1','实线 · 跟随正文'),('body-guides-1-3-1','竖排 · 跟随正文')]
        canvas=Image.new('RGB',(3*342+4*24,716),'#ededeb');draw=ImageDraw.Draw(canvas)
        for index,(name,label) in enumerate(panels):
            x=24+index*366
            draw.text((x,22),label,font=font,fill='#222222')
            canvas.paste(Image.open(output/f'{name}.png').resize((342,608)),(x,68))
        canvas.save(output/'body-guides-overview.png')
    print(f'Reader previews: {output}')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/reader-toolbar-preview')
    run(parser.parse_args().output.resolve())
