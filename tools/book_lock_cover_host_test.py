#!/usr/bin/env python3
"""纯封面像素、原比例与读卡回退 / Unobstructed native cover, aspect ratio and source fallback."""
import argparse
from pathlib import Path
import tempfile

import file_ui_host_test as native
import reading_journal_ui_host_test as journal
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]


def unit(book):
    code = journal.unit().split(journal.source('main/services/reading_journal_model.c'))[0]
    code += '\n#include "book/book_cover.h"\n#include <sys/stat.h>\n'
    code += '#define FIXTURE_PATH "' + str(book) + '"\n'
    code += '#include "book/book_lock_cover.h"\n'
    code += journal.source('main/ui/ui_image_dither.c')
    code += r'''
#include "ui_image_dither.h"
#define MM_OWNER_FILE_JOB 1
#define MM_POOL_PSRAM 1
static bool have_book=true, readable_cover=true, embedded=true, use_cache=false, replace_book=false, renamed=false, oom=false;
static unsigned bitmaps, reads, auto_draws, cache_reads, metadata_reads;
static const char *sample_title="山和海之间";
static const char *sample_author="林青";
static unsigned app_settings_system_contrast(void){return 100;}
static bool book_progress_last_path(char *out,size_t cap){snprintf(out,cap,"%s",FIXTURE_PATH);return have_book;}
static bool book_metadata_supported(const char *p){assert(!strcmp(p,FIXTURE_PATH));return embedded;}
static esp_err_t book_metadata_read(const char *p,char *t,size_t n,char *a,size_t m){
    assert(!strcmp(p,FIXTURE_PATH));++metadata_reads;snprintf(t,n,"%s",sample_title);snprintf(a,m,"%s",sample_author);return ESP_OK;
}
static bool book_title_get(const char *p,char *t,size_t n){assert(!strcmp(p,FIXTURE_PATH));if(renamed)snprintf(t,n,"%s","远山");return renamed;}
static bool book_title_from_path(const char *p,char *t,size_t n){assert(!strcmp(p,FIXTURE_PATH));snprintf(t,n,"%s",sample_title);return true;}
static esp_err_t book_metadata_cover_bounded(const char *p,uint8_t **data,size_t *n,bool *png,size_t budget){
    assert(!strcmp(p,FIXTURE_PATH)&&budget>0&&budget<=4*1024*1024);++reads;
    *data=malloc(1);assert(*data);*n=1;*png=false;return ESP_OK;
}
bool book_image_dimensions(const uint8_t *p,size_t n,bool png,unsigned *w,unsigned *h){
    assert(p && n==1 && !png);*w=680;*h=1120;return true;
}
static void sample_cover(unsigned w,unsigned h,uint8_t *pixel){
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        int nx=(int)(x*640u/w),ny=(int)(y*1000u/h),dx=nx-425,dy=ny-337;
        unsigned gray=ny<575?230-ny/16:217-(ny-574)/5;
        if(dx*dx+dy*dy<47*47)gray=249;
        int ridge1=475 + abs(nx-152)/3 + abs(nx-153)%49/4;
        int ridge2=535 + abs(nx-455)/4 + abs(nx-455)%57/5;
        if(ny>ridge1&&ny<570)gray=129+(unsigned)(ny-ridge1)/2;
        if(ny>ridge2&&ny<590)gray=89+(unsigned)(ny-ridge2)/3;
        if(ny>627&&ny<946&&(ny+nx/15)%33<2)gray+=20;
        if(nx<85&&ny>760)gray=24;
        pixel[(size_t)y*w+x]=(uint8_t)gray;
    }
}
bool book_image_grayscale(const uint8_t *p,size_t n,bool png,unsigned w,unsigned h,uint8_t *out){
    assert(p && n==1 && !png && ((w==560&&h==922)||(w==680&&h==1120)||(w==160&&h==263)));
    if(replace_book){FILE *f=fopen(FIXTURE_PATH,"ab");assert(f);fputc(1,f);fclose(f);}
    if(!readable_cover)return false;
    sample_cover(w,h,out);return true;
}
bool book_cover_load_list_gray(const char *p,const char *t,const char *a,uint8_t *out,
                               bool decode,bool *pending,unsigned *w,unsigned *h){
    assert(!strcmp(p,FIXTURE_PATH)&&t[0]&&!decode&&!pending);(void)a;++cache_reads;
    if(use_cache){*w=120;*h=240;sample_cover(*w,*h,out);}return use_cache;
}
static bool book_auto_cover_render(const char *p,const char *t,const char *a,unsigned w,unsigned h,uint8_t *out){
    assert(!strcmp(p,FIXTURE_PATH)&&t[0]&&w==176&&h==240);(void)a;
    if(renamed)assert(!strcmp(t,"远山"));++auto_draws;memset(out,0x55,w*h);return true;
}
static void *mm_alloc(int owner,int pool,size_t bytes,size_t align,size_t reserve){
    assert(owner==MM_OWNER_FILE_JOB&&pool==MM_POOL_PSRAM&&align==0&&bytes<=684*1216);
    assert(reserve==(bytes==176*240?0:1536u*1024u));
    if(oom)return NULL;
    void *p=malloc(bytes);assert(p);++bitmaps;return p;
}
static bool mm_free(void *p){assert(p&&bitmaps>0);--bitmaps;free(p);return true;}
'''
    code += function('book_cover_crop', ROOT/'main/image/image_decode.c') + '\n'
    code += journal.source('main/book/book_lock_cover.c')
    code += r'''
static void save_sample(const char *dir,const char *name,uint8_t *fb){char out[1024];snprintf(out,sizeof(out),"%s/%s.raw",dir,name);save(out,fb);}
static void check_frame(const uint8_t *fb, EpdRect rect, const uint8_t *source, unsigned stride, unsigned sw, unsigned sh){
    const image_gray_axis_t xs=image_gray_axis(sw,rect.width),ys=image_gray_axis(sh,rect.height);
    for(int y=0;y<FACE_H;++y){
        const bool row_inside=y>=rect.y&&y<rect.y+rect.height;
        const image_gray_row_t row=image_gray_row(source,stride,ys,row_inside?y-rect.y:0);
        for(int x=0;x<FACE_W;++x){
            uint8_t expected=0xf0;
            if(row_inside&&x>=rect.x&&x<rect.x+rect.width){
                const uint8_t tone=ui_image_cover_tone(image_gray_sample(row,xs,x-rect.x),100);
                expected=ui_image_dither_gray(tone,x,y)&0xf0;
            }
            assert(pixel_at(fb,x,y)==expected);
        }
    }
}
static unsigned preview_gray(const uint8_t *pixels,int x,int y){
    unsigned at=y*BOOK_LOCK_PREVIEW_W+x;
    return (pixels[at/2]>>((at&1u)*4))&15u;
}
static void check_preview(const uint8_t *pixels){
    uint8_t source[160*263];sample_cover(160,263,source);
    enum {X0=12,Y0=25,W=135,H=221};
    const image_gray_axis_t xs=image_gray_axis(160,W),ys=image_gray_axis(263,H);
    for(int y=0;y<BOOK_LOCK_PREVIEW_H;++y){
        const bool inside=y>=Y0&&y<Y0+H;
        const image_gray_row_t row=image_gray_row(source,160,ys,inside?y-Y0:0);
        for(int x=0;x<BOOK_LOCK_PREVIEW_W;++x){
            unsigned expected=15;
            if(inside&&x>=X0&&x<X0+W){
                uint8_t tone=ui_image_cover_tone(image_gray_sample(row,xs,x-X0),100);
                expected=ui_image_dither_gray(tone,x,y)>>4;
            }
            assert(preview_gray(pixels,x,y)==expected);
        }
    }
}
int main(int argc,char **argv){
    assert(argc==2);
    uint8_t *fb=malloc(FACE_W*FACE_H/2);assert(fb);memset(fb,0x12,FACE_W*FACE_H/2);
    uint8_t *preview=malloc(BOOK_LOCK_PREVIEW_BYTES);assert(preview);memset(preview,0x12,BOOK_LOCK_PREVIEW_BYTES);
    have_book=false;
    assert(!book_lock_cover_draw(fb,false)&&!book_lock_cover_preview(preview,false)&&!reads&&!bitmaps&&fb[0]==0x12&&preview[0]==0x12);
    have_book=true;
    assert(book_lock_cover_draw(fb,false)&&reads==1&&!metadata_reads&&!auto_draws&&!cache_reads&&!bitmaps);
    const EpdRect rect=cover_rect(560,922);
    assert(rect.width==576&&rect.height==948&&rect.x==54&&rect.y==110);
    uint8_t *source=malloc(560*922);assert(source);sample_cover(560,922,source);
    check_frame(fb,rect,source,560,560,922);save_sample(argv[1],"book-cover",fb);free(source);
    assert(book_lock_cover_preview(preview,false)&&reads==2&&!metadata_reads&&!bitmaps);
    check_preview(preview);
    assert(book_lock_cover_draw(fb,true)&&reads==3&&!bitmaps);
    source=malloc(680*1120);assert(source);sample_cover(680,1120,source);
    check_frame(fb,(EpdRect){0,0,684,1216},source+25,680,630,1120);
    save_sample(argv[1],"book-cover-crop",fb);free(source);
    assert(book_lock_cover_preview(preview,true)&&reads==4&&!bitmaps);
    uint8_t preview_source[160*263];sample_cover(160,263,preview_source);
    const image_gray_axis_t xs=image_gray_axis(147,160),ys=image_gray_axis(263,284);
    for(int y=0;y<284;++y){
        const image_gray_row_t row=image_gray_row(preview_source+6,160,ys,y);
        for(int x=0;x<160;++x){
            const uint8_t tone=ui_image_cover_tone(image_gray_sample(row,xs,x),100);
            assert(preview_gray(preview,x,y)==(ui_image_dither_gray(tone,x,y)>>4));
        }
    }
    const book_crop_t vertical=book_cover_crop(120,500,684,1216);
    assert(vertical.x==0&&vertical.y==143&&vertical.width==120&&vertical.height==213);
    EpdRect wide=cover_rect(560,160), tall=cover_rect(120,500);
    assert(wide.width==576&&wide.height==164&&wide.x==54);
    assert(tall.width==240&&tall.height==1000&&tall.x==222);
    assert(cover_rect(1,1000).width==1&&cover_rect(560,1).height==1);
    readable_cover=false;use_cache=true;
    assert(book_lock_cover_draw(fb,false)&&cache_reads==1&&metadata_reads==1&&!auto_draws&&!bitmaps);
    uint8_t *cached=malloc(120*240);assert(cached);sample_cover(120,240,cached);
    check_frame(fb,cover_rect(120,240),cached,120,120,240);free(cached);
    embedded=use_cache=false;renamed=true;
    assert(book_lock_cover_draw(fb,false)&&auto_draws==1&&!bitmaps);
    oom=true;assert(!book_lock_cover_draw(fb,false)&&!bitmaps);oom=false;
    embedded=replace_book=true;assert(!book_lock_cover_draw(fb,false)&&!bitmaps);
    assert(!remove(FIXTURE_PATH)&&!book_lock_cover_draw(fb,false));
    assert(!book_lock_cover_preview(preview,false)&&!bitmaps);
    free(preview);free(fb);
    puts("PASS: wallpaper and packed preview preserve the full cover or center-crop to fill; source validation, bounded fallbacks and allocation failure");
}
'''
    return code


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/book-cover-only-previews')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cover-wallpaper-') as tmp:
        book = Path(tmp) / 'sample.epub'
        book.write_bytes(b'fixture')
        native.run(args.output, unit(book), require_black=False)
