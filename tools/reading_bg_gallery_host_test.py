"""阅读背景目录、分页缓存和导航边界。/ Exercise gallery enumeration, page ownership and navigation bounds."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
source = ROOT/'main/apps/app_device_settings.c'
s = source.read_text()
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
typedef struct {int x,y,width,height;} EpdRect;
#define BOOK_IMAGE_FILE_MAX (50u*1024*1024)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static int s_wallpaper_page, decoded;
static bool fail_alloc;
static void *heap_caps_realloc(void *p,size_t size,int caps){assert(caps==3);return fail_alloc?NULL:realloc(p,size);}
static bool book_image_file_grayscale(const char *p,bool png,int w,int h,uint8_t *gray){
    assert(w==64 && h==68 && !png && strstr(p,".jpg"));memset(gray,++decoded,w*h);return true;
}
'''
unit += s[s.index('typedef struct { char path[288]; char name[96]; } wallpaper_item_t;'):s.index('static void reading_bg_thumbnail_draw(')]
unit += function('wallpaper_scan_dir', source)
unit += r'''
int main(void){
    assert(wallpaper_scan_dir("pictures") && s_wallpaper_count==73);
    assert(s_wallpaper_capacity>=73 && !reading_bg_thumbnail_at(0));
    assert(!reading_bg_thumbnail_tick(true) && !decoded);
    for(int i=0;i<6;++i){assert(reading_bg_thumbnail_tick(false)==(i==5));assert(decoded==i+1);}
    assert(reading_bg_thumbnail_at(0)[0]==1 && !reading_bg_thumbnail_tick(false));
    s_wallpaper_page=1;assert(!reading_bg_thumbnail_at(0));
    assert(!reading_bg_thumbnail_tick(false) && reading_bg_thumbnail_at(0)[0]==7 && !reading_bg_thumbnail_at(1));
    s_wallpaper_page=12;assert(reading_bg_thumbnail_tick(false) && decoded==8 && !reading_bg_thumbnail_at(1));
    EpdRect bottom=reading_bg_card_rect(5),button=reading_bg_restore_rect();
    assert(bottom.y+bottom.height<button.y && button.y+button.height<1096);
    free(s_wallpapers);s_wallpapers=NULL;s_wallpaper_capacity=0;s_wallpaper_count=0;fail_alloc=true;
    assert(!wallpaper_scan_dir("pictures") && !s_wallpaper_count);
    reading_bg_thumbnails_release();puts("background: 73 images, stale-page exclusion, one decode per idle tick, one final refresh, footer bounds and OOM passed");
}
'''
with tempfile.TemporaryDirectory(prefix='mogo-gallery-') as tmp:
    folder = Path(tmp)
    pictures = folder/'pictures'; pictures.mkdir()
    for i in range(73):
        (pictures/f'{i}.jpg').write_bytes(b'picture')
    (pictures/'._hidden.jpg').touch(); (pictures/'empty.jpg').touch()
    src, binary = folder/'test.c', folder/'test'
    src.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
                    '-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],cwd=folder,check=True)
