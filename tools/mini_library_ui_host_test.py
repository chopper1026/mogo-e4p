#!/usr/bin/env python3
"""Render the real fan deck using device fonts and 4bpp; check bounds and hit order."""
from pathlib import Path
import argparse
import platform
import subprocess
import tempfile
from PIL import Image
import reading_journal_ui_host_test as journal
import reader_title_style_preview as host
from run_reader_baseline import toolchain
ROOT=Path(__file__).resolve().parents[1]

def unit():
 code=journal.unit();code=code[:code.index(journal.source('main/services/reading_journal_model.c'))]
 code+='\n#include "ui_mini_library.h"\n'
 code+=journal.source('main/ui/ui_mini_library.c')
 code+=r'''
#undef ui_text_fixed_vc
static book_mini_item_t books[5];
static home_library_cover_t covers[5];
static uint8_t gray[176*240],bits[22*240];
static void snapshot(uint8_t *fb,const char *folder,const char *name,unsigned slot){char path[2048];snprintf(path,sizeof(path),"%s/%s-%u.raw",folder,name,slot);save(path,fb);}
int main(int argc,char **argv){
 assert(argc==2&&ttf_font_open_builtin()==ESP_OK);
 uint8_t *fb=malloc(684*1216/2);assert(fb);
 const char *titles[]={"瓦尔登湖","月亮与六便士","山茶文具店","人间草木","小王子"};
 for(unsigned i=0;i<5;++i){strcpy(books[i].title,titles[i]);strcpy(books[i].author,"作家");books[i].pct=31;books[i].has_progress=i%2;}
 for(unsigned y=0;y<240;++y)for(unsigned x=0;x<176;++x){gray[y*176+x]=(x/22)*32; if(x%16<8)bits[y*22+x/8]|=1u<<(x&7);}
 unsigned hits[5]={0};
 for(unsigned slot=0;slot<2;++slot)for(unsigned n=0;n<=5;++n)for(unsigned selected=0;selected<(n?n:1);++selected){
  EpdRect r={0,168+472*(int)slot,684,472};allowed_top=r.y;allowed_bottom=r.y+448;
  home_library_view_t v={books,covers,n,selected,false,false,""};
  for(unsigned i=0;i<5;++i)covers[i]=(home_library_cover_t){0};
  for(unsigned mode=0;mode<3;++mode){
   if(mode)for(unsigned i=0;i<5;++i)covers[i]=(home_library_cover_t){mode==1?gray:bits,176,240,mode==2};
   for(unsigned frame=0;frame<8;++frame){
    ui_mini_library_frame_t f={n?(selected+n-1)%n:0,frame<3?(frame+1)*256/3:256,frame<3?256:frame==3?128:frame==4?0:frame==5?128:256,frame<7};
    v.recent=frame<7&&frame%2;
    memset(fb,0x77,684*1216/2);ui_mini_library_draw(fb,r,&v,f);
    for(int y=0;y<1216;++y)if(y<r.y||y>=r.y+472)for(int x=0;x<684;++x)assert(pixel_at(fb,x,y)==0x70);
    for(int y=r.y+448;y<r.y+472;++y)for(int x=0;x<684;++x)assert(pixel_at(fb,x,y)==0xf0);
    if(n==5&&selected==0&&frame==7){char name[48];snprintf(name,sizeof(name),"fan-mode-%u",mode);snapshot(fb,argv[1],name,slot);}
   }
  }
  for(int y=r.y+70;y<r.y+310;y+=2)for(int x=24;x<660;x+=2){ui_mini_hit_t h=ui_mini_library_hit(r,&v,x,y);if(h.action==UI_MINI_SELECT||h.action==UI_MINI_OPEN){assert(h.index<n);++hits[h.index];}}
  for(unsigned i=0;i<n;++i){int x=342+((int)i*2-(int)n+1)*36;ui_mini_hit_t h=ui_mini_library_hit(r,&v,x,r.y+336);assert(h.action==UI_MINI_SELECT&&h.index==i);}
  assert(ui_mini_library_hit(r,&v,432,r.y+38).action==UI_MINI_RANDOM);
  assert(ui_mini_library_hit(r,&v,582,r.y+38).action==UI_MINI_RECENT);
  assert(ui_mini_library_hit(r,&v,620,r.y+435).action==UI_MINI_SHUFFLE);
  assert(ui_mini_library_hit(r,&v,550,r.y+430).action==UI_MINI_SHUFFLE);
  v.recent=true;assert(ui_mini_library_hit(r,&v,620,r.y+435).action==UI_MINI_NONE);
  assert(ui_mini_library_hit(r,&v,342,r.y-1).action==UI_MINI_NONE);
  if(n){assert(ui_mini_library_hit(r,&v,342,r.y+184).action==UI_MINI_OPEN);assert(ui_mini_library_hit(r,&v,612,r.y+400).action==UI_MINI_NONE);}
 }
 for(unsigned i=0;i<5;++i)assert(hits[i]);
 EpdRect r={0,640,684,472};allowed_top=640;allowed_bottom=UI_NAV_CLEAN_END;
 home_library_view_t v={books,covers,0,0,true,false,""};ui_mini_library_draw(fb,r,&v,(ui_mini_library_frame_t){0,256,256,false});snapshot(fb,argv[1],"empty-recent",1);
 strcpy(books[0].title,"这是一个很长很长的书名这是一个很长很长的书名这是一个很长很长的书名");
 strcpy(books[0].author,"这是一位名字很长的作者这是一位名字很长的作者");v.count=1;v.notice="设置保存失败，请稍后重试";
 ui_mini_library_draw(fb,r,&v,(ui_mini_library_frame_t){0,256,256,false});snapshot(fb,argv[1],"long",1);
 free(fb);printf("PASS: 0..5 books, every focus, both regions, eight frames with original three equal selection positions, grayscale/1bpp/placeholder, glyph/text bounds, all exposed cards, index/mode/open/empty targets (%u text checks)\n",text_checks);
}
'''
 return code

def run(output):
 output.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='mini-library-ui-') as d:
  w=Path(d);host.write_shims(w)
  with (w/'esp_heap_caps.h').open('a') as f:f.write('\nvoid *heap_caps_realloc(void *,size_t,int);\n')
  (w/'epdiy.h').write_text((w/'epdiy.h').read_text().replace('EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT','EPD_DRAW_ALIGN_LEFT=2, EPD_DRAW_ALIGN_CENTER=8, EPD_DRAW_ALIGN_RIGHT=4'))
  # Share the native zlib shim and glyph asset with the journal raster harness.
  journal.write_native_assets(w)
  (w/'test.c').write_text(unit());env,cc=toolchain(w)
  flags=['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-deprecated-declarations','-fsanitize=address,undefined','-g']
  for f in (w,*[ROOT/'main'/p for p in ('','ui','font','boot','diagnostics','display','settings','app','services','book')]):flags+=['-I'+str(f)]
  units=[w/'test.c',w/'assets.c']+[ROOT/'main/font'/n for n in ('ttf_font.c','ttf_font_source.c','ttf_block_cache.c','ttf_glyph_cache.c','ui_font.c','ui_hanzi.c')]
  linker='-Wl,-dead_strip' if platform.system()=='Darwin' else '-Wl,--gc-sections'
  subprocess.run(cc+flags+list(map(str,units))+[linker,'-lm','-lz','-o',str(w/'test')],env=env,check=True)
  subprocess.run([str(w/'test'),str(output)],env=env,check=True)
 for raw in output.glob('*.raw'):
  img=Image.frombytes('L',(684,1216),raw.read_bytes()).point(lambda v:v*17//16);slot=int(raw.stem[-1]);top=168+slot*472
  img.crop((0,top,684,top+472)).save(raw.with_suffix('.png'));raw.unlink()

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'build/mini-library-preview');a=p.parse_args();run(a.output)
