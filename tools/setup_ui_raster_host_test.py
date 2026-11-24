"""原生引导像素、三语文字边界及局刷等价性。/ Native setup pixels, localized fit and partial-frame equivalence."""
import argparse
from pathlib import Path
import tempfile

import file_ui_host_test as native
import reader_title_style_preview as host

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / 'main/apps/app_setup.c'


def unit():
    code = native.unit().split('typedef struct {char path[288];', 1)[0]
    code += '''
#include "ui_language.h"
#include "ui_font_sample.h"
#include "home_components.h"
#include "assets/setup_wenkai.h"
'''
    code += host.enum_block(ROOT / 'main/app/app_setup.h', 'APP_SETUP_RESUME', 'app_setup_mode_t')
    source = PAGE.read_text()
    code += source[source.index('static app_setup_mode_t mode;'):source.index('const app_desc_t* app_setup_start')]
    code += host.function('ui_language_name', ROOT / 'main/settings/settings.c')
    # 测试只传固定文案；设备的只读地址边界在行为测试中单独验证。/ All inputs here are literals; the behavior suite covers the rodata guard.
    locale = (ROOT / 'main/ui/ui_locale.c').read_text()
    code += locale[locale.index('typedef struct'):locale.index('const char* ui_tr(const')].replace(
        '    extern const char _rodata_start[], _rodata_end[];', '').replace(
        ' || (uintptr_t)key < (uintptr_t)_rodata_start ||\n        (uintptr_t)key >= (uintptr_t)_rodata_end', '')
    code += 'static uint32_t font_download_bytes(bool tc){return tc?15122300:25800076;}\n'
    code += 'static void ui_clear_rect_fast(uint8_t* f,EpdRect r){ui_fill_rect_fast(r,255,f);}\n'
    code += host.function('ui_setup_text_box', native.KIT)
    code += '\n#undef ui_tr\n#define ui_tr(key) ui_tr_for(preview,key)\n'
    code += host.function('draw_system_font_sample', ROOT / 'main/apps/app_device_settings.c')
    code += source[source.index('/* ---- 纸页'):source.index('static display_rect_t area_hint')]
    code += '''
int main(int argc,char **argv){
    assert(argc==2 && ttf_font_open_builtin()==ESP_OK);
    const size_t bytes=684*1216/2; uint8_t *fb=malloc(bytes),*partial=malloc(bytes);assert(fb&&partial);
    char path[1024];mode=APP_SETUP_RESUME;
    slots[0]=APP_HOME_WIDGET_READING; slots[1]=APP_HOME_WIDGET_WEATHER;
    for(int l=0;l<UI_LANG_COUNT;l++)for(int i=0;i<PAGE_COUNT;i++)for(int scale=100;scale<=200;scale+=100){
        preview=l;step=pages[i];wifi_ready=false;font_percent=scale;pending=step==SETUP_FONT;progress=35;render(NULL,fb);
        if(pending){
            // 原版 TTF 生成的未压缩像素摘要，不依赖设备解码器。/ Golden hashes of raw upstream TTF pixels, independent of the decoder.
            static const uint32_t expected[]={0x0217ef1b,0x73781d1e,0xe5d32754};
            uint32_t hash=2166136261u;
            for(int y=0;y<SETUP_SAMPLE_HEIGHT;++y)for(int x=0;x<SETUP_SAMPLE_WIDTH;x+=2){
                unsigned a=15-pixel_at(fb,LEFT+x,SAMPLE_TOP+y)/16;
                unsigned b=15-pixel_at(fb,LEFT+x+1,SAMPLE_TOP+y)/16;
                hash=(hash^((a<<4)|b))*16777619u;
            }
            assert(hash==expected[l]);
            progress=65;draw_meter(fb);memcpy(partial,fb,bytes);render(NULL,fb);
            assert(memcmp(fb,partial,bytes)==0);
        }
        snprintf(path,sizeof(path),"%s/setup-%d-%d-%d.raw",argv[1],l,i,scale);save(path,fb);
        if(step==SETUP_WIFI){
            wifi_ready=true;strcpy(wifi_ssid,"12345678901234567890123456789012");render(NULL,fb);
            snprintf(path,sizeof(path),"%s/wifi-connected-%d-%d.raw",argv[1],l,scale);save(path,fb);
        }
    }
    for(int l=0;l<UI_LANG_COUNT;l++)for(int scale=100;scale<=200;scale+=10){
        preview=l;font_percent=scale;
        EpdRect card={36,0,612,scale>=150?216:192};
        const char *expected=ui_tr_for(l,UI_FONT_SAMPLE_TEXT);
        assert(l==UI_LANG_CN || strcmp(expected,UI_FONT_SAMPLE_TEXT)!=0);
        ui_clear_page(fb);ui_clear_page(partial);
        draw_system_font_sample(fb,card);
        char copy[192];snprintf(copy,sizeof(copy),"%s",expected);
        int n=0,pitch=(card.height-16)/3;
        for(char *line=strtok(copy,"\\n");line;line=strtok(NULL,"\\n"),++n){
            assert(ui_font_has_text(line));
            EpdRect box={56,8+n*pitch,572,pitch};
            ui_text_box(partial,box,48,line,EPD_DRAW_ALIGN_LEFT,false);
            int top=box.y+pitch,bottom=box.y-1;
            for(int y=box.y;y<box.y+pitch;y++)for(int x=box.x;x<box.x+box.width;x++){
                if(pixel_at(fb,x,y)<0x90){if(y<top)top=y;if(y>bottom)bottom=y;}
            }
            assert(bottom-top+1>=30);
        }
        assert(n==3&&memcmp(fb,partial,bytes)==0);
        if(scale==100){snprintf(path,sizeof(path),"%s/settings-sample-%d.raw",argv[1],l);save(path,fb);}
    }
    free(partial);free(fb);ttf_font_unload();puts("PASS: setup layouts, real fonts, bounded text and DU meter repaint");
}
'''
    return code


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        native.run(args.output.resolve(), unit())
    else:
        with tempfile.TemporaryDirectory(prefix='setup-raster-') as folder:
            native.run(Path(folder), unit())
