#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：以原生字体和4bpp绘图检查最近阅读组件，并导出设计预览。
English: Check recent reading with native fonts and 4bpp drawing, exporting design previews.
"""
import argparse
from pathlib import Path
import tempfile

import file_ui_host_test as native
import reader_title_style_preview as host

ROOT = Path(__file__).resolve().parents[1]


def unit(source):
    code = native.unit().split('typedef struct {char path[288];', 1)[0]
    code += host.function('ui_text_fixed_ink_vc', ROOT / 'main/ui/ui_kit.c') + '\n'
    code += host.function('ui_image_dither_gray', ROOT / 'main/ui/ui_image_dither.c') + '\n'
    code += r'''
#define HOME_REGION_TOP 168
#define HOME_REGION_HEIGHT 472
#define APP_HOME_SLOT_COUNT 2
#define BOOK_EXCERPT_CAP 128
typedef struct {char path[288],title[128],author[128];uint8_t *cover;
    bool cover_fast;uint16_t cover_width,cover_height;bool has_progress;
    struct {unsigned pct;} progress;} home_book_t;
static home_book_t s_current,s_recent[6];
static char s_excerpt[BOOK_EXCERPT_CAP];
static EpdRect s_pressed;
static bool s_press_pending;
static EpdRect text_boxes[16];
static unsigned box_count;
static void checked_home_text(uint8_t *fb,int x,int y,int px,const char *text,const char *sample,
                             enum EpdFontFlags align,uint8_t ink,uint8_t bg){
    int above=0,below=0;ui_font_measure_line_px(px,text,&above,&below);
    int width=ui_text_fixed_context_width_px(px,text,sample);
    int left=x-(align==EPD_DRAW_ALIGN_CENTER?width/2:align==EPD_DRAW_ALIGN_RIGHT?width:0);
    EpdRect box={left,y+(above-below)/2-above,width,above+below+1};
    assert(ui_font_has_text(text)&&left>=24&&left+width<=660);
    assert(box.y>=allowed_top&&box.y+box.height<=allowed_bottom);
    for(unsigned i=0;i<box_count;++i){EpdRect p=text_boxes[i];
        assert(box.x>=p.x+p.width||p.x>=box.x+box.width||box.y>=p.y+p.height||p.y>=box.y+box.height);
    }
    assert(box_count<16);text_boxes[box_count++]=box;
    ui_text_fixed_context_aa_vc(fb,x,y,px,text,sample,align,ink,bg);
}
#define ui_text_fixed_context_aa_vc checked_home_text
'''
    names = ('home_region', 'continue_card_pressed', 'fit', 'label', 'label_on',
             'home_frame', 'featured', 'draw_cover', 'title_lines', 'render_reading')
    for name in names:
        code += host.function(name, source) + '\n'
    code += r'''
#undef ui_text_fixed_context_aa_vc
static uint8_t cover[176*240];
static void sample_cover(uint8_t *fb){
    ui_clear_page(fb);ui_fill_rect_fast((EpdRect){0,0,176,240},255,fb);
    for(int y=115;y<240;++y)for(int x=0;x<176;++x){
        if(y>145+abs(x-120)/2)epd_draw_pixel(x,y,144,fb);
        if(y>184+abs(x-32)/3)epd_draw_pixel(x,y,48,fb);
    }
    ui_text_fixed_context_aa_vc(fb,88,47,30,"瓦尔登湖","瓦尔登湖",EPD_DRAW_ALIGN_CENTER,0,255);
    ui_text_fixed_context_aa_vc(fb,88,84,16,"亨利·戴维·梭罗","亨利·戴维·梭罗",EPD_DRAW_ALIGN_CENTER,0,255);
    for(int y=0;y<240;++y)for(int x=0;x<176;++x)cover[y*176+x]=pixel_at(fb,x,y)*17/16;
}
static void snapshot(uint8_t *fb,const char *folder,const char *name){
    char path[2048];snprintf(path,sizeof(path),"%s/%s.raw",folder,name);save(path,fb);
}
int main(int argc,char **argv){
    assert(argc==2&&ttf_font_open_builtin()==ESP_OK);
    uint8_t *fb=malloc(684*1216/2);assert(fb);sample_cover(fb);
    const char *titles[]={"瓦尔登湖","月亮与六便士：一个人追寻自我的漫长旅途","The Moon and Sixpence"};
    const unsigned percentages[]={0,42,100};
    for(unsigned slot=0;slot<2;++slot)for(unsigned shape=0;shape<3;++shape)
    for(unsigned pct=0;pct<3;++pct)for(unsigned scale=100;scale<=200;scale+=50){
        font_percent=scale;s_current=(home_book_t){.cover=cover,.cover_width=176,.cover_height=240,.has_progress=true};
        strcpy(s_current.path,"/sdcard/books/sample.epub");strcpy(s_current.title,titles[shape]);
        strcpy(s_current.author,shape==1?"威廉·萨默塞特·毛姆":"亨利·戴维·梭罗");
        s_current.progress.pct=percentages[pct];
        strcpy(s_excerpt,"我步入丛林，因为我希望生活得有意义。");
        EpdRect r=home_region(slot);memset(fb,0x77,684*1216/2);
        ui_fill_rect_fast(r,255,fb);allowed_top=r.y;allowed_bottom=r.y+r.height;box_count=0;
        render_reading(fb,slot);
        for(int y=0;y<1216;++y)if(y<r.y||y>=r.y+r.height)for(int x=0;x<684;++x)assert(pixel_at(fb,x,y)==0x70);
        if(scale==100&&pct==1){char name[64];snprintf(name,sizeof(name),"reading-slot%u-title%u",slot,shape);snapshot(fb,argv[1],name);}
    }
    // 空书摘、未知作者、无封面和空记录必须走生产回退。/ Exercise production fallbacks for missing data.
    for(unsigned state=0;state<4;++state){
        s_current.author[0]=0;s_current.cover=NULL;s_current.cover_width=s_current.cover_height=0;
        if(state==1)s_excerpt[0]=0;
        if(state==2)strcpy(s_excerpt,"人类的幸福和欢乐在于奋斗，而最有价值的是为理想而奋斗，生活在于不断前行。");
        if(state==3){memset(&s_current,0,sizeof(s_current));s_excerpt[0]=0;}
        ui_clear_page(fb);allowed_top=168;allowed_bottom=640;box_count=0;render_reading(fb,0);
        char name[64];snprintf(name,sizeof(name),"reading-fallback%u",state);snapshot(fb,argv[1],name);
    }
    free(fb);ttf_font_unload();
    puts("PASS: 54 native recent-reading layouts, both slots, 100/150/200% UI settings, title/text bounds, missing data and ASan/UBSan");
}
'''
    return code


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--source', type=Path, default=ROOT / 'main/apps/app_dashboard.c')
    args = parser.parse_args()
    if args.output:
        native.run(args.output.resolve(), unit(args.source))
    else:
        with tempfile.TemporaryDirectory(prefix='home-reading-') as directory:
            native.run(Path(directory), unit(args.source))
