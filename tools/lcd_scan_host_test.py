#!/usr/bin/env python3
"""中文：核对居中扫描的源行、列遮罩和相位。
English: Verify centered scan rows, column masks and phases.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]

scan = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "epdiy.h"
#define IRAM_ATTR
#define CONFIG_IDF_TARGET_ESP32S31 1
#define NUM_RENDER_THREADS 2
#define EPD_DRAW_EMPTY_LINE_QUEUE 1
#define MODE_PACKING_1PPB_DIFFERENCE 1
#define MODE_PACKING_2PPB 2
#define MODE_PACKING_8PPB 4
#define EPD_DRAW_INVALID_PACKING_MODE 8
#define int_min(a,b) ((a)<(b)?(a):(b))
#define Cache_Start_L1_DCache_Preload(a,b,c) ((void)0)
typedef struct {int size,element_size;} LineQueue_t;
typedef struct {
 EpdRect area,crop_to;int mode,display_width,display_height;
 const uint8_t *data_ptr;const bool *drawn_lines;int error;bool frame_ended;
 atomic_int lines_prepared;int lines_total;int line_threads[48];
 uint8_t *feed_line_buffers[2],*line_mask;LineQueue_t line_queues[2];
 bool lut_msb_first;uint8_t *static_line_buffer;
 const uint8_t *conversion_lut;const uint8_t *const *phase_luts;const int8_t *line_phase;
 const uint8_t *line_outer_lut;int line_inner_x0,line_inner_x1;
 int col_band_n;const int *col_band_x0,*col_band_x1;const int8_t *col_band_phase;
 void (*lut_lookup_func)(const uint32_t *,uint8_t *,const uint8_t *,uint32_t);
} RenderContext_t;
static RenderContext_t *active;
_Alignas(16) static uint8_t output[48][16];static int commits,starts;
static int s_prefill_lines=4;
static void lq_reset(LineQueue_t *q){(void)q;}
static uint8_t *lq_current(LineQueue_t *q){(void)q;return output[active->lines_prepared-1];}
static void lq_commit(LineQueue_t *q){(void)q;++commits;}
static bool retrieve_line_isr(RenderContext_t *c,uint8_t *b){(void)c;(void)b;return false;}
typedef bool (*line_cb_func_t)(RenderContext_t *,uint8_t *);
static void epd_lcd_line_source_cb(line_cb_func_t cb,RenderContext_t *c){(void)cb;(void)c;}
static void epd_lcd_start_frame(void){++starts;}
void epd_apply_line_mask_VE(uint8_t *row,const uint8_t *mask,int bytes);
static void epd_apply_line_mask(uint8_t *row,const uint8_t *mask,int bytes){epd_apply_line_mask_VE(row,mask,bytes);}
static int min(int a,int b){return a<b?a:b;}
#define DARK_BYTE 0x55
#define CLEAR_BYTE 0xaa
static void lookup(const uint32_t *p,uint8_t *out,const uint8_t *lut,uint32_t width){
 const uint8_t *in=(const uint8_t *)p;
 for(unsigned x=0;x<width;x+=4){out[x/4]=0;for(unsigned k=0;k<4;++k)out[x/4]|=((in[x+k]+*lut)&3)<<(6-2*k);}
}
'''
render_context = ROOT / "components/epdiy/src/output_common/render_context.c"
for name in ("get_buffer_params", "epd_populate_line_mask"):
    scan += function(name, render_context) + "\n"
scan += (ROOT / "components/epdiy/src/epdiy_riscv_ve_stub.c").read_text().replace(
    "#include <esp_attr.h>", "").replace('__attribute__((optimize("O3"))) ', "")
lut = ROOT / "components/epdiy/src/output_common/lut.c"
for signature in ("static inline uint8_t lookup_pixels_in_VE_LUT(",
                  "void IRAM_ATTR calc_epd_input_1ppB_1k_S3_VE(",
                  "void IRAM_ATTR calc_epd_input_1ppB_64k("):
    text = lut.read_text()
    start = text.index(signature)
    scan += text[start:text.index("\n}", start) + 2] + "\n"
scan += function("push_pixels_populate_line", ROOT / "components/epdiy/src/output_lcd/render_lcd.c") + "\n"
feeder = (ROOT / "components/epdiy/src/output_lcd/render_lcd.c").read_text()
scan += "static void " + feeder[feeder.index("lookup_line_with_outer("):feeder.rindex("\n#endif")].replace('__attribute__((optimize("O3"))) ', "")
scan += r'''
static unsigned pixel(const uint8_t *p,int x,bool msb){return (p[x/4]>>(msb?6-2*(x%4):2*(x%4)))&3;}
static void scan_test(void){
 uint8_t data[40][64],input[64],base=0,one=1,two=2;
 _Alignas(16) uint8_t mask[16];
 bool dirty[40];int8_t phases[40];const uint8_t *luts[]={&one,&two};
 const int x0[]={0,32},x1[]={32,64};const int8_t band_phase[]={0,-1};
 for(int y=0;y<40;++y){dirty[y]=y%3!=1;phases[y]=y%2;for(int x=0;x<64;++x)data[y][x]=(uint8_t)(x+y*3);}
 for(int i=0;i<16;++i)mask[i]=(uint8_t)(i%2?0xf3:0xcf);
 for(int mode=0;mode<4;++mode){
  RenderContext_t ctx={.area={0,0,64,40},.crop_to={0,3,64,34},.mode=1,.display_width=64,.display_height=40,
   .data_ptr=&data[0][0],.drawn_lines=dirty,.lines_total=44,
   .feed_line_buffers={input,input},.line_queues={{80,16},{80,16}},.line_mask=mask,
   .lut_lookup_func=lookup,.lut_msb_first=true,.conversion_lut=&base};
  if(mode==1||mode==3){ctx.phase_luts=luts;ctx.line_phase=phases;}
  for(int y=0;y<40;++y)phases[y]=mode==3&&y%4==0?-1:y%2;
  if(mode==3){ctx.line_outer_lut=&base;ctx.line_inner_x0=13;ctx.line_inner_x1=41;}
  if(mode==2){ctx.phase_luts=luts;ctx.col_band_n=2;ctx.col_band_x0=x0;ctx.col_band_x1=x1;ctx.col_band_phase=band_phase;}
  active=&ctx;commits=starts=0;memset(output,0xa5,sizeof(output));lcd_calculate_frame(&ctx,0);
  assert(commits==44&&starts==1&&!ctx.error);
  for(int y=0;y<44;++y)for(int x=0;x<64;++x){
   unsigned want=0;
   if(y>=3&&y<37&&dirty[y]){
    if(mode!=2||x<32){int phase=(mode==1||(mode==3&&x>=13&&x<41))?1+phases[y]:mode==2?1:0;
     want=mode==3&&x>=13&&x<41&&phases[y]<0?0:(data[y][x]+phase)&3;
     want&=(mask[x/4]>>(6-2*(x%4)))&3;
    }
   }
   if(pixel(output[y],x,true)!=want){fprintf(stderr,"mode=%d x=%d y=%d got=%u want=%u\n",mode,x,y,pixel(output[y],x,true),want);assert(0);}
  }
 }
}

static void mask_test(void){
 uint8_t from[32],to[32],dirty_columns[32],input[64];
 _Alignas(16) uint8_t mask[16];
 _Alignas(16) uint32_t diff[16];
 static uint8_t lut_64k[1 << 16];
 uint8_t lut_1k[1024]={0};lut_1k[0x0f*4]=1;
 lut_64k[0xff0f]=1;lut_64k[0x0fff]=4;
 memset(from,255,sizeof(from));
 const bool dirty_lines[]={true};
 for(int pack=0;pack<2;++pack)for(int x=0;x<8;++x){
  memset(to,255,sizeof(to));memset(dirty_columns,0,sizeof(dirty_columns));
  to[x/2]&=(uint8_t)~(0x0f << (4*(x%2)));
  assert(epd_interlace_4bpp_line_VE(to,from,(uint8_t*)diff,dirty_columns,64));
  epd_populate_line_mask(mask,dirty_columns,16,pack==0);
  RenderContext_t ctx={.area={0,0,64,1},.crop_to={0,0,64,1},.mode=MODE_PACKING_1PPB_DIFFERENCE,
   .display_width=64,.display_height=1,.data_ptr=(const uint8_t*)diff,
   .drawn_lines=dirty_lines,.lines_total=1,.feed_line_buffers={input,input},
   .line_queues={{80,16},{80,16}},.line_mask=mask,.lut_msb_first=pack==0,
   .conversion_lut=pack==0?lut_1k:lut_64k,
   .lut_lookup_func=pack==0?calc_epd_input_1ppB_1k_S3_VE:calc_epd_input_1ppB_64k};
  active=&ctx;commits=starts=0;lcd_calculate_frame(&ctx,0);
  assert(commits==1&&!ctx.error);
  for(int px=0;px<64;++px)assert(pixel(output[0],px,pack==0)==(px==x));
 }
 for(int x=0;x<8;++x){
  _Alignas(16) uint8_t line[16];
  RenderContext_t ctx={.area={x,0,1,1},.display_width=64,.line_mask=mask,.static_line_buffer=line};
  push_pixels_populate_line(&ctx,1);
  for(int px=0;px<64;++px)assert(pixel(line,px,true)==(px==x?2:0));
 }
}
int main(void){scan_test();mask_test();puts("PASS: centered feeder, crops, dirty masks, phases, bands and packing order");}
'''

with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "scan.c"
    binary = path.with_suffix("")
    path.write_text(scan)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-pointer-to-int-cast",
                    "-fsanitize=address,undefined", "-g", "-Itools/display_host_stubs", str(path), "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
