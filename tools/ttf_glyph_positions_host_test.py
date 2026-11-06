#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：复用真实字体测试环境，检查实际落笔与观察开关的像素一致性。
English: Reuse the real-font harness to verify actual pen cells and observer raster identity.
"""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'tools/title_font_host_test.py').read_text()
extra=r'''
static ttf_glyph_position_t observed[128];static size_t positions;
static void record_position(void *context,const ttf_glyph_position_t *p){
 assert(context==&positions&&positions<128);observed[positions++]=*p;
}
static void check_positions(void){
 const char *sample="字体设置";size_t length=strlen(sample);int px=48,base=ttf_text_width_px(px,sample);
 ttf_glyph_observer_t observer={record_position,&positions};
 for(int style=0;style<5;++style){
  record_coverage=true;memset(pixels,15,sizeof(pixels));positions=0;
  int tracking=style==1?-2:style==2?2:0;int target=base+11;
  if(style==0)ttf_draw_text_px(pixels,200,70,px,sample,EPD_DRAW_ALIGN_CENTER,0,15);
  else if(style==1)ttf_draw_text_px_spaced(pixels,20,70,px,sample,tracking,0,15);
  else if(style==2)ttf_draw_text_px_fitted(pixels,20,70,px,sample,tracking,target,0,15);
  else if(style==3)ttf_draw_text_px_fitted(pixels,20,70,px,sample,0,base-5,0,15);
  else ttf_draw_text_px_vertical(pixels,160,20,px,sample,1,0,15);
  uint8_t expected[sizeof(pixels)];memcpy(expected,pixels,sizeof(pixels));memset(pixels,15,sizeof(pixels));
  if(style==0)ttf_draw_text_px_observed(pixels,200,70,px,sample,EPD_DRAW_ALIGN_CENTER,0,15,&observer);
  else if(style==1)ttf_draw_text_px_spaced_observed(pixels,20,70,px,sample,tracking,0,15,&observer);
  else if(style==2)ttf_draw_text_px_fitted_observed(pixels,20,70,px,sample,tracking,target,0,15,&observer);
  else if(style==3)ttf_draw_text_px_fitted_observed(pixels,20,70,px,sample,0,base-5,0,15,&observer);
  else ttf_draw_text_px_vertical_observed(pixels,160,20,px,sample,1,0,15,&observer);
  assert(!memcmp(expected,pixels,sizeof(pixels))&&positions==4);
  int pen=style==0?200-base/2:20;int remainder=style==2?(target-base-tracking*3)%3:style==3?-5%3:0;
  for(size_t i=0;i<positions;++i){
   assert(observed[i].start==i*3&&observed[i].end==(i+1)*3&&observed[i].height>0);
   char one[4]={0};memcpy(one,sample+i*3,3);int advance=ttf_text_width_px(px,one);
   if(style<4){assert(observed[i].x==pen&&observed[i].y==70-ttf_ascender_px(px));
    int step=advance;if(i+1<positions){step+=tracking;if(style==2){step+=(target-base-tracking*3)/3;if((int)i<remainder)++step;}if(style==3){step+=-5/3;if((int)i<-remainder)--step;}}pen+=step;
   }else{assert(observed[i].x==160);if(i)assert(observed[i].y==observed[i-1].y+observed[i-1].height);}
  }
  if(style==2)assert(pen==20+target);
  if(style==3)assert(pen==20+base-5);
  assert(observed[positions-1].end==length);
 }
 // 竖排替换保留原文身份。/ Vertical substitutions retain original source identity.
 const char *mixed="字12，体";ttf_run_t runs[]={{0,0},{3,0}};ttf_draw_set_runs(runs,2);positions=0;
 ttf_draw_text_px_vertical_observed(pixels,160,20,px,mixed,1,0,15,&observer);ttf_draw_set_runs(NULL,0);
 assert(positions==5&&observed[1].codepoint=='1'&&observed[3].codepoint==0xff0c&&observed[4].end==strlen(mixed));
 puts("PASS: actual TTF normal/negative tracking/fitted quotient and remainder/vertical source spans and observer pixel identity");
}
'''

mixed=r'''
static void check_mixed_faces(const char *base_path,const char *other_path){
 assert(ttf_font_open(base_path)==ESP_OK);size_t bytes;uint8_t *data=owned_font_copy(other_path,&bytes);
 int slot=ttf_font_open_mem(data,bytes,"dictionary-mixed");assert(slot>0);
 const char *sample="字体设置";ttf_run_t runs[]={{0,0},{6,(uint8_t)slot}};
 ttf_glyph_observer_t observer={record_position,&positions};ttf_draw_set_runs(runs,2);
 record_coverage=true;memset(pixels,15,sizeof(pixels));
 ttf_draw_text_px(pixels,20,70,48,sample,EPD_DRAW_ALIGN_LEFT,0,15);
 uint8_t expected[sizeof(pixels)];memcpy(expected,pixels,sizeof(pixels));memset(pixels,15,sizeof(pixels));positions=0;
 ttf_draw_text_px_observed(pixels,20,70,48,sample,EPD_DRAW_ALIGN_LEFT,0,15,&observer);
 assert(positions==4&&!memcmp(expected,pixels,sizeof(pixels)));ttf_draw_set_runs(NULL,0);
 int pen=20;
 for(size_t i=0;i<positions;++i){ttf_font_select(i<2?0:slot);char one[4]={0};memcpy(one,sample+i*3,3);
  assert(observed[i].x==pen&&observed[i].width==ttf_text_width_px(48,one));pen+=observed[i].width;}
 assert(observed[2].width>observed[0].width);ttf_font_close_embedded();ttf_font_unload();
 puts("PASS: actual mixed resident font slots retain source offsets, exact pen advances and raster identity");
}
'''
source=source.replace("    unit=unit.replace('int main(int argc,char **argv){',helper+'int main(int argc,char **argv){')", "    unit=unit.replace('int main(int argc,char **argv){',helper+"+repr(extra+mixed)+"+'int main(int argc,char **argv){')\n    unit=unit.replace('unsigned initial_opens=opens;', 'check_positions();unsigned initial_opens=opens;')")
source=source.replace("    (work / 'test.c').write_text(unit)", "    unit=unit.replace('ttf_font_unload();check_font_memory();', 'ttf_font_unload();check_font_memory();check_mixed_faces(argv[1],argv[2]);')\n    (work / 'test.c').write_text(unit)")
exec(compile(source,str(ROOT/'tools/title_font_host_test.py'),'exec'),{'__file__':str(ROOT/'tools/title_font_host_test.py'),'__name__':'__main__'})
