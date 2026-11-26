#!/usr/bin/env python3
"""中文：实际电源识别器与图形边界回归。/ English: Real power input and raster boundary regression.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re, subprocess, sys, tempfile
ROOT=Path(__file__).resolve().parents[1]
power=(ROOT/'main/ui/ui_power_dialog.c').read_text()
if len(sys.argv)>2 and sys.argv[1]=='--input-c':
    Path(sys.argv[2]).write_text(re.sub(r'^#include "(?:shutdown_wallpaper|memory_manager|settings|book_ticket|book_lock_collage|book_lock_cover|lock_calendar)\.h"\n','',power[:power.index('EpdRect ui_power_dialog_rect(void)')],flags=re.M))
    raise SystemExit(0)
def function(path,name):
    text=(ROOT/path).read_text()
    m=re.search(r'^(?:static )?[A-Za-z_][A-Za-z_0-9 \t*]*\b'+name+r'\([^;{}]*\)\s*\{',text,re.M)
    assert m,name
    at=m.end();depth=1
    while depth:depth+=(text[at]=='{')-(text[at]=='}');at+=1
    return text[m.start():at]+'\n'
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ui_power_dialog.h"
#include "ui_kit.h"
#define W 684
#define H 1216
#include "ui_font.h"
#include "ui_hanzi.h"
static uint8_t fb[W*H/2],before[sizeof(fb)];
static uint8_t system_size=100;
static bool s_system_text_scale=true, s_system_ttf;
static int language;
static unsigned text_pixels;
static EpdRect text_bounds;
static bool check_text_bounds;
static const struct {const char *cn,*tw,*en;} locale[]={
#include "locale_catalog.inc"
};
#undef ui_tr
static const char *ui_tr(const char *s){
 if(language)for(size_t i=0;i<sizeof(locale)/sizeof(locale[0]);++i)
  if(!strcmp(s,locale[i].cn))return language==1?locale[i].tw:locale[i].en;
 return s;
}
uint8_t app_settings_system_font_size(void){return system_size;}
void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
void *heap_caps_calloc(size_t n,size_t size,int caps){(void)caps;return calloc(n,size);}
bool ui_hanzi_has(uint32_t cp){(void)cp;return false;}
bool ui_hanzi_get(uint32_t cp,uint8_t out[UI_HANZI_RECORD]){(void)cp;(void)out;return false;}
bool ttf_font_is_builtin(void){return true;}
int ttf_text_width_px(int px,const char *s){(void)px;(void)s;abort();}
void ttf_measure_line_px(int px,const char *s,int *a,int *b){(void)px;(void)s;(void)a;(void)b;abort();}
void ttf_draw_text_px(uint8_t *p,int x,int y,int px,const char *s,enum EpdFontFlags a,uint8_t fg,uint8_t bg){(void)p;(void)x;(void)y;(void)px;(void)s;(void)a;(void)fg;(void)bg;abort();}
void epd_draw_pixel(int x,int y,uint8_t v,uint8_t *p){if(check_text_bounds){assert(x>=text_bounds.x&&y>=text_bounds.y&&x<text_bounds.x+text_bounds.width&&y<text_bounds.y+text_bounds.height);++text_pixels;}if(x<0||y<0||x>=W||y>=H)return;size_t at=(size_t)y*W/2+x/2;int shift=(x&1)*4;p[at]=(p[at]&~(15<<shift))|((v>>4)<<shift);}
uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *p){assert(w==W&&h==H&&x>=0&&y>=0&&x<W&&y<H);return ((p[(size_t)y*W/2+x/2]>>((x&1)*4))&15)<<4;}
int epd_width(void){return W;}int epd_height(void){return H;}
int epd_rotated_display_width(void){return W;}int epd_rotated_display_height(void){return H;}
enum EpdRotation epd_get_rotation(void){return EPD_ROT_LANDSCAPE;}
void epd_draw_vline(int x,int y,int h,uint8_t v,uint8_t *p){for(int j=0;j<h;++j)epd_draw_pixel(x,y+j,v,p);}
void epd_draw_hline(int x,int y,int w,uint8_t v,uint8_t *p){for(int i=0;i<w;++i)epd_draw_pixel(x+i,y,v,p);}
void epd_fill_rect(EpdRect r,uint8_t v,uint8_t *p){for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x)epd_draw_pixel(x,y,v,p);}
void ui_fill_rect_fast(EpdRect r,uint8_t v,uint8_t *p){epd_fill_rect(r,v,p);}
void epd_draw_line(int x,int y,int xx,int yy,uint8_t v,uint8_t *p){assert(x==xx||y==yy);if(x==xx)epd_draw_vline(x,y,yy-y+1,v,p);else epd_draw_hline(x,y,xx-x+1,v,p);}
uint8_t ui_contrast_gray(uint8_t v){return v&0xf0;}
void ui_clear_page(uint8_t *p){memset(p,255,sizeof(fb));}
void ui_text(uint8_t *p,int x,int y,int px,const char *s,enum EpdFontFlags align,bool inverted){(void)p;(void)x;(void)y;(void)px;(void)s;(void)align;(void)inverted;}
'''
for name in ('epd_draw_circle','epd_fill_circle_helper','epd_fill_circle'):
    code+=function('components/epdiy/src/epdiy.c',name)
for name in ('ui_read_pixel','icon_row_decode', 'icon_nibble','ui_draw_icon','clamp_radius','draw_arc','ui_draw_round_rect_raw','ui_draw_round_rect','ui_inset_rect','ui_draw_control_frame','ui_fill_round_rect_raw','ui_fill_round_rect'):
    code+=function('main/ui/ui_kit.c',name)
for name in ('ui_text_effective_px','ui_text_fixed_context_width_px','ui_text_fixed_width_px',
             'ui_text_fixed_context_vc','ui_text_fixed_vc','ui_text_fit_px'):
    code+=function('main/ui/ui_kit.c',name)
code+=function('main/ui/ui_kit.c','ui_text_box').replace('void ui_text_box(', 'static void firmware_text_box(')
code+=r'''
void ui_text_box(uint8_t *p,EpdRect box,int px,const char *s,enum EpdFontFlags align,bool inverted){
 assert(box.x>=0&&box.y>=0&&box.x+box.width<=W&&box.y+box.height<=H);
 text_bounds=box;text_pixels=0;check_text_bounds=true;
 firmware_text_box(p,box,px,s,align,inverted);
 check_text_bounds=false;assert(text_pixels>0);
}
'''
settings=(ROOT/'main/settings/settings.h').read_text()
code+=settings[settings.index('typedef enum {\n    APP_OFF_SYSTEM'):settings.index('/// 与浅睡锁屏独立')]
code+=r'''
#define SHUTDOWN_ART_WIDTH 684u
#define SHUTDOWN_ART_HEIGHT 1216u
#define SHUTDOWN_ART_BYTES (684u*1216u/2u)
#define MM_OWNER_FILE_JOB 0
#define MM_POOL_PSRAM 1
static app_shutdown_wallpaper_t off_config;
static bool off_cache_ok=true,off_oom;
static const app_shutdown_wallpaper_t *app_settings_shutdown_wallpaper(void){return &off_config;}
static void *mm_alloc(int owner,int pool,size_t n,size_t align,size_t reserve){(void)owner;(void)pool;(void)align;(void)reserve;return off_oom?NULL:malloc(n);}
static bool mm_free(void *p){free(p);return true;}
static bool shutdown_wallpaper_read(const app_shutdown_wallpaper_t *v,uint8_t *p){(void)v;memset(p,0x33,SHUTDOWN_ART_BYTES);return off_cache_ok;}
'''
code+=r'''
static bool generated_ok=true,last_variant;
static unsigned generated_kind;
static bool generated(uint8_t *p,unsigned kind,bool variant){
 for(unsigned i=0;i<sizeof(fb);++i)assert(p[i]==255);
 generated_kind=kind;last_variant=variant;memset(p,(int)(kind*17),sizeof(fb));return generated_ok;
}
static bool book_ticket_preview(uint8_t *p,bool cover){return generated(p,4,cover);}
static bool book_lock_collage_prepare(uint8_t *p){return generated(p,5,false);}
static bool lock_calendar_draw_variant(uint8_t *p,bool month){return generated(p,6,month);}
static bool book_lock_cover_draw(uint8_t *p,bool crop){return generated(p,7,crop);}
'''
code+=re.sub(r'^#include[^\n]*\n','',power,flags=re.M)
code+=r'''
static ui_power_dialog_t dialog;
static app_touch_t touch;
static app_ctx_t ctx;
static void sample(int x,int y,int count,bool pressed,bool released){touch=(app_touch_t){.x=x,.y=y,.count=count,.touched=count>0};ctx=(app_ctx_t){.touch=&touch,.now_ms=ctx.now_ms+10,.pressed=pressed,.released=released};}
static ui_power_action_t feed(int x,int y,int count,bool pressed,bool released){sample(x,y,count,pressed,released);return ui_power_dialog_feed(&dialog,&ctx);}
static void check_preview(const char *folder){
 const char *suffix[]={"zh-CN","zh-TW","en"};
 const char *off_labels[]={"关机","關機","Power off"};
 for(language=0;language<3;++language)for(unsigned size=100;size<=200;size+=10){
  assert(!strcmp(ui_tr("关机"),off_labels[language]));
  system_size=(uint8_t)size;
  ui_power_dialog_draw(fb);
  if(folder && (size==100 || size==200)){
   char path[1024];int n=snprintf(path,sizeof(path),"%s/power-menu-%s-%u.pgm",folder,suffix[language],size);
   assert(n>0&&n<(int)sizeof(path));FILE *f=fopen(path,"wb");assert(f);
   fprintf(f,"P5\n%d %d\n255\n",W,H);
   for(int y=0;y<H;++y)for(int x=0;x<W;++x)fputc(epd_get_pixel(x,y,W,H,fb)*17/16,f);
   assert(!fclose(f));
  }
 }
}
int main(int argc,char **argv){
 off_config.style=APP_OFF_SYSTEM;ui_power_shutdown_prepare(fb);assert(fb[0]==255);
 const unsigned styles[]={APP_OFF_TICKET,APP_OFF_COLLAGE,APP_OFF_CALENDAR,APP_OFF_BOOK_COVER};
 for(unsigned i=0;i<sizeof(styles)/sizeof(styles[0]);++i)for(unsigned variant=0;variant<2;++variant){
  off_config.style=styles[i];off_config.ticket_background=off_config.calendar_style=off_config.cover_crop=variant;
  app_shutdown_wallpaper_t saved=off_config;memset(fb,0x11,sizeof(fb));ui_power_shutdown_prepare(fb);
  assert(generated_kind==i+4&&fb[0]==(i+4)*17&&last_variant==(i!=1&&(bool)variant));
  assert(!memcmp(&saved,&off_config,sizeof(saved)));
  generated_ok=false;ui_power_shutdown_prepare(fb);assert(fb[0]==255);generated_ok=true;
 }
 off_config.style=APP_OFF_CUSTOM;off_config.show_hint=false;ui_power_shutdown_prepare(fb);for(unsigned i=0;i<sizeof(fb);++i)assert(fb[i]==0x33);
 off_cache_ok=false;ui_power_shutdown_prepare(fb);assert(fb[0]==255);off_cache_ok=true;
 off_oom=true;ui_power_shutdown_prepare(fb);assert(fb[0]==255);off_oom=false;
 off_config.style=APP_OFF_COLLAGE;off_config.show_hint=true;ui_power_shutdown_prepare(fb);
 unsigned changed=0;
 const int tag_width=ui_text_fixed_width_px(36,"已关机 · 长按电源键开机")+16, tag_x=(W-tag_width)/2;
 // 拼贴书籍数量的白描边止于 y=1179；逐像素检查字签以外不变。
 // The collage count outline ends at y=1179; check every pixel outside the label.
 for(int y=0;y<H;++y)for(int x=0;x<W;++x)if(epd_get_pixel(x,y,W,H,fb)!=0x50){
  assert(x>=tag_x&&x<tag_x+tag_width&&y>=1180&&y<H);++changed;
 }
 assert(changed>(unsigned)tag_width*36*9/10);
 assert(epd_get_pixel(tag_x,1180,W,H,fb)==0xf0);
 ui_power_dialog_draw(fb);memcpy(before,fb,sizeof(fb));
 EpdRect off=ui_power_dialog_button_rect(UI_POWER_ACTION_SHUTDOWN);
 EpdRect restart=ui_power_dialog_button_rect(UI_POWER_ACTION_RESTART);
 EpdRect cancel=ui_power_dialog_button_rect(UI_POWER_ACTION_CANCEL);
 assert(off.x==64&&off.width==556&&off.height==116);
 assert(restart.x==off.x&&restart.y==cancel.y&&restart.width==266&&cancel.width==266);
 assert(restart.height==100&&cancel.height==100&&cancel.x-restart.x-restart.width==24);
 assert(cancel.x+cancel.width==W-off.x);
 assert(epd_get_pixel(off.x+22,off.y+30,W,H,fb)==0);
 assert(epd_get_pixel(restart.x+22,restart.y+30,W,H,fb)==0xf0);
 assert(epd_get_pixel(cancel.x+22,cancel.y+30,W,H,fb)==0xf0);
 for(ui_power_action_t action=UI_POWER_ACTION_CANCEL;action<=UI_POWER_ACTION_RESTART;++action){
  EpdRect box=ui_power_dialog_button_rect(action);
  const int x=box.x+box.width/2,y=box.y+box.height/2;
  assert(feed(x,y,1,true,false)==UI_POWER_ACTION_NONE);
  assert(feed(x+UI_TOUCH_SLOP_PX,y,1,false,false)==UI_POWER_ACTION_NONE);
  assert(feed(x+UI_TOUCH_SLOP_PX,y,0,false,true)==action);
  assert(feed(x,y,0,false,true)==UI_POWER_ACTION_NONE);
  // 起手空白、跨出边界再回来、按钮内大幅移动，都不能提交。
  // Blank starts, leaving an edge and returning, and movement within the button cannot commit.
  assert(feed(box.x-1,y,1,true,false)==UI_POWER_ACTION_NONE);
  assert(feed(box.x+1,y,0,false,true)==UI_POWER_ACTION_NONE);
  assert(feed(box.x+1,y,1,true,false)==UI_POWER_ACTION_NONE);
  assert(feed(box.x-1,y,1,false,false)==UI_POWER_ACTION_NONE);
  assert(feed(box.x+1,y,0,false,true)==UI_POWER_ACTION_NONE);
  assert(feed(x,y,1,true,false)==UI_POWER_ACTION_NONE);
  assert(feed(x+UI_TOUCH_SLOP_PX+1,y,1,false,false)==UI_POWER_ACTION_NONE);
  assert(feed(x,y,0,false,true)==UI_POWER_ACTION_NONE);
  // 字面以外的按钮区域也可触发。/ The target includes the area outside the label.
  assert(feed(box.x+15,box.y+15,1,true,false)==UI_POWER_ACTION_NONE);
  assert(feed(box.x+15,box.y+15,0,false,true)==action);
 }
 for(int fault=0;fault<5;++fault){
  const int x=off.x+off.width/2,y=off.y+off.height/2;
  assert(feed(x,y,1,true,false)==UI_POWER_ACTION_NONE);
  sample(x,y,1,false,false);
  if(fault==0)touch.count=2;
  if(fault==1)touch.x=W;
  if(fault==2)ctx.consumed=true;
  if(fault==3)ctx.touch=NULL;
  if(fault==4)ctx.now_ms=0;
  assert(ui_power_dialog_feed(&dialog,&ctx)==UI_POWER_ACTION_NONE&&dialog.swallow);
  assert(feed(x,y,0,false,true)==UI_POWER_ACTION_NONE&&!dialog.swallow);
 }
 ui_power_dialog_reset(&dialog,true);
 assert(feed(W/2,off.y,1,true,false)==UI_POWER_ACTION_NONE);
 assert(feed(W/2,off.y,0,false,true)==UI_POWER_ACTION_NONE);
 assert(!memcmp(before,fb,sizeof(fb)));
 check_preview(argc>1?argv[1]:NULL);
 ui_font_cache_clear();
 puts("PASS: three static release targets, tap interruption and bounds, 33 native locale/size layouts, unchanged shutdown art");
}
'''
if len(sys.argv)>1:
    assert len(sys.argv)==3 and sys.argv[1]=='--preview-dir', 'usage: power_dialog_host_test.py [--preview-dir DIR]'
    preview=Path(sys.argv[2]);preview.mkdir(parents=True,exist_ok=True)
else:
    preview=None
with tempfile.TemporaryDirectory(prefix='mogo-power-') as folder:
    work=Path(folder);c,binary=work/'test.c',work/'test';c.write_text(code)
    (work/'esp_heap_caps.h').write_text('#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,int);\nvoid *heap_caps_calloc(size_t,size_t,int);\n')
    font=(ROOT/'main/assets/builtin.ttf').read_bytes()
    assets=work/'assets.c'
    assets.write_text('#include <stdint.h>\nconst uint8_t data[] __asm__("_binary_builtin_ttf_start")={'+','.join(map(str,font))+'};\n')
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
        '-I'+str(work),'-I'+str(ROOT/'tools/ui_gesture_stubs'),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/font'),
        str(c),str(assets),str(ROOT/'main/font/ui_font.c'),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)]+([str(preview)] if preview else []),check=True)
