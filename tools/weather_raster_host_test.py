#!/usr/bin/env python3
"""中文：真实全宽天气组件的矩形边界、旋转、动作与内存回归。
English: Real full-width weather bounds, rotation, actions and memory regressions.
"""
from pathlib import Path
import os
import re
from fontTools.ttLib import TTFont
import subprocess
import tempfile

import reader_title_style_preview as host
from run_reader_baseline import toolchain
from generate_weather_backgrounds import host_definition

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([os.sys.executable, str(ROOT / "tools/generate_weather_backgrounds.py"), "--check"], check=True)
backgrounds = (ROOT / "main/assets/weather_backgrounds.bin").read_bytes()
for scene in range(6):
    mask = backgrounds[scene * 77 * 448:(scene + 1) * 77 * 448]
    assert any(mask[y * 77] & 1 for y in range(448)), (scene, "left edge was clipped")
    assert any(mask[y * 77 + 76] & 8 for y in range(448)), (scene, "right edge was clipped")
    assert all(mask[y * 77 + 76] & 0xf0 == 0 for y in range(448)), (scene, "nonzero row padding")
KIT = ROOT / "main/ui/ui_kit.c"
with TTFont(ROOT / "main/assets/builtin.ttf") as font:
    codepoints = set(font.getBestCmap())
assert all(ord(ch) in codepoints for ch in "石家庄河北省"), "missing default-font weather-city glyphs"
for source in ("main/ui/ui_weather.c", "main/apps/app_components.c", "main/services/weather_service.c"):
    strings = []
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', (ROOT / source).read_text()):
        if token[0].startswith('"'):
            strings.append(token[0])
    missing = sorted({ch for ch in "".join(strings) if ord(ch) > 127 and ord(ch) not in codepoints})
    assert not missing, (source, "missing built-in UI glyphs", "".join(missing))
unit = host.host_preamble() + host.epdiy_slices()
unit += host_definition()
unit += r'''
void *heap_caps_realloc(void *p,size_t n,int caps){
    if(!p)return heap_caps_malloc(n,caps);
    size_t old=0;
    for(int i=0;i<8192;i++)if(allocations[i].p==p){old=allocations[i].bytes;break;}
    void *q=heap_caps_malloc(n,caps);
    if(q){memcpy(q,p,old<n?old:n);heap_caps_free(p);}return q;
}
'''
unit += """
#undef UI_ICON_CHEVRON_RIGHT
#undef UI_ICON_CHEVRON_LEFT
#undef UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT
#include "assets/ui_icons.h"
#include "weather_service.h"
#include "ui_weather.h"
#include "esp_heap_caps.h"
#include <math.h>
uint8_t ui_read_pixel(const uint8_t *,int,int);
static uint8_t ui_control_gray(uint8_t g){return g<0x80?0:g>=0xf0?255:0x90;}
"""
unit += "\n".join(host.function(name, KIT) for name in (
    "clamp_radius", "draw_arc", "ui_draw_round_rect_raw", "ui_draw_round_rect",
    "ui_fill_round_rect_raw", "ui_fill_round_rect", "ui_inset_rect", "icon_nibble",
    "ui_draw_icon", "ui_read_pixel"))
unit += r"""
static unsigned temperature_labels;
static unsigned loading_labels,cached_labels,failed_labels,clock_labels,city_labels,refresh_labels;
void ui_text_fixed_context_aa_vc(uint8_t *f,int x,int y,int px,const char *s,const char *sample,
                                enum EpdFontFlags align,uint8_t ink,uint8_t paper){
    (void)sample;int a,b;ttf_measure_line_px(px,s,&a,&b);
    if(px>=58&&strchr(s,'/'))++temperature_labels;
    if(strstr(s,"正在"))++loading_labels;
    if(strstr(s,"缓存"))++cached_labels;
    if(strstr(s,"更新失败"))++failed_labels;
    if(strstr(s,"请先校准"))++clock_labels;
    if(!strcmp(s,"选择城市"))++city_labels;
    if(!strcmp(s,"刷新")){assert(px>=30);++refresh_labels;}
    int width=ttf_text_width_px(px,s);
    int left=align==EPD_DRAW_ALIGN_RIGHT?x-width:align==EPD_DRAW_ALIGN_CENTER?x-width/2:x;
    assert(left>=0&&left+width<=684);
    ttf_draw_text_px(f,x,y+(a-b)/2,px,s,align,ink>>4,paper>>4);
}
int ui_text_fixed_width_px(int px,const char *s){return ttf_text_width_px(px,s);}
"""
production = (ROOT / "main/ui/ui_weather.c").read_text()
unit += "\n".join(line for line in production.splitlines() if not line.startswith("#include"))
unit += r"""
#include "weather_test_data.h"
static void equivalent_halo(uint8_t *f,uint8_t *reference){
    memset(s_scratch,255,684*472/2);
    for(size_t i=0;i<684u*472u/2u;i+=97)s_scratch[i]=i%3?0xdd:0xef;
    for(int y=7;y<16;++y)memset(s_scratch+y*342+2,0,120);
    s_scratch[0]=s_scratch[684*472/2-1]=0;
    for(int rotation=0;rotation<2;++rotation)for(int pass=0;pass<3;++pass){
        display_rotation=rotation?EPD_ROT_INVERTED_PORTRAIT:EPD_ROT_PORTRAIT;
        EpdRect region={0,pass==0?168:pass==1?640:236,684,472};
        memset(f,0x99,684*1216/2);memset(reference,0x99,684*1216/2);
        clear_foreground_halo(f,region);
        for(int y=0;y<472;++y)for(int x=0;x<684;++x)if(scratch_pixel(x,y)<224)
            for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx)
                if(dx*dx+dy*dy<=9&&x+dx>=0&&x+dx<684&&y+dy>=0&&y+dy<472)
                    epd_draw_pixel(x+dx,y+region.y+dy,255,reference);
        assert(!memcmp(f,reference,684*1216/2));
    }
}
static void outside(const uint8_t *f,const uint8_t *before,EpdRect region){
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)
        if(x<region.x||x>=region.x+region.width||y<region.y||y>=region.y+region.height)
            assert(pixel_at(f,x,y)==pixel_at(before,x,y));
}
static void perimeter(const uint8_t *f,EpdRect region,int code){
    weather_scene_t scene=weather_scene(code);
    const uint8_t *mask=scene>=WEATHER_SCENE_SUN&&scene<=WEATHER_SCENE_STORM?
        weather_backgrounds_asset+(scene-WEATHER_SCENE_SUN)*77*448:NULL;
    for(int y=0;y<region.height;++y)for(int x=0;x<region.width;++x){
        if(x!=0&&x!=region.width-1&&y!=0&&y!=region.height-1)continue;
        int sx=x*612/684,sy=y*448/472;
        uint8_t expected=mask&&(mask[sy*77+sx/8]&(1u<<(sx%8)))?0x50:0xf0;
        assert(pixel_at(f,x,region.y+y)==expected);
    }
}
static void actions(EpdRect r){
    for(int y=-1;y<=r.height;++y)for(int x=-1;x<=r.width;++x){
        ui_weather_action_t expected=UI_WEATHER_NONE;
        if(x>=0&&x<r.width&&y>=0&&y<r.height){
            if(y>=16&&y<70){
                if(x>=448&&x<648)expected=x<548?UI_WEATHER_TODAY:UI_WEATHER_TOMORROW;
                else if(x>=24&&x<432)expected=UI_WEATHER_CITY;
            }else if(y>=r.height-80&&y<r.height-16){
                if(x>=600&&x<664)expected=UI_WEATHER_REFRESH;
            }
        }
        assert(ui_weather_hit(r,x,r.y+y)==expected);
    }
}
int main(int argc,char **argv){
    assert(argc==2&&ttf_font_open(argv[1])==ESP_OK);
    uint8_t *f=malloc(684*1216/2),*before=malloc(684*1216/2);assert(f&&before);
    app_weather_view_t view={.has_city=true,.has_forecast=true};
    int64_t now=weather_timestamp("2026-11-02T00:30:00Z");
    assert(weather_location_parse(WEATHER_TEST_CITY,strlen(WEATHER_TEST_CITY),&view.city));
    assert(weather_forecast_parse(WEATHER_TEST_FORECAST,strlen(WEATHER_TEST_FORECAST),now,&view.city,&view.forecast));
    size_t old=live;assert(ui_weather_prepare()&&live-old==684u*472u/2u);
    assert(ui_weather_prepare()&&live-old==684u*472u/2u);
    equivalent_halo(f,before);
    const int codes[]={100,104,305,400,501,302,999};
    const EpdRect regions[]={{0,168,684,472},{0,640,684,472},{0,236,684,472}};
    for(unsigned rotation=0;rotation<2;rotation++){
        display_rotation=rotation?EPD_ROT_INVERTED_PORTRAIT:EPD_ROT_PORTRAIT;
        for(unsigned slot=0;slot<3;++slot){
            EpdRect region=regions[slot];
            actions(region);
            for(unsigned i=0;i<sizeof(codes)/sizeof(*codes);i++)for(int tomorrow=0;tomorrow<2;tomorrow++){
                view.forecast.days[tomorrow].code=codes[i];
                memset(f,0xaa,684*1216/2);memcpy(before,f,684*1216/2);
                app_weather_view_t original=view;
                uint8_t *scratch=s_scratch;
                ui_weather_draw(f,region,tomorrow,&view,now);outside(f,before,region);
                assert(!memcmp(&view,&original,sizeof(view))&&s_scratch==scratch);
                assert(memcmp(f,before,684*1216/2)!=0);
                perimeter(f,region,codes[i]);
            }
            view.has_forecast=false;
            ui_weather_draw(f,region,false,&view,0);outside(f,before,region);
            perimeter(f,region,999);
            view.has_forecast=true;
            for(int state=APP_WEATHER_IDLE;state<=APP_WEATHER_FAILED;++state){
                view.state=state;
                ui_weather_draw(f,region,false,&view,now);outside(f,before,region);
                view.has_forecast=false;
                ui_weather_draw(f,region,false,&view,now);outside(f,before,region);
                view.has_forecast=true;
            }
            view.forecast.stale=true;strcpy(view.notice,"更新失败");
            view.forecast.days[0].min_c=-90;view.forecast.days[0].max_c=65;
            ui_weather_draw(f,region,false,&view,now);outside(f,before,region);
            view.forecast.stale=false;view.notice[0]=0;
            view.has_city=false;ui_weather_draw(f,region,false,&view,0);outside(f,before,region);
            view.has_city=true;
        }
    }
    ui_weather_release();
    display_rotation=EPD_ROT_PORTRAIT;
    EpdRect region=regions[1];
    view.forecast.days[0].code=104;
    memset(before,255,684*1216/2);ui_weather_draw(before,region,false,&view,now);
    assert(ui_weather_prepare());memset(f,255,684*1216/2);ui_weather_draw(f,region,false,&view,now);
    unsigned backdrop_pixels=0;
    for(int y=region.y;y<region.y+region.height;y++)for(int x=0;x<684;x++)
        if(pixel_at(before,x,y)==0xf0&&pixel_at(f,x,y)!=0xf0){
            assert(pixel_at(f,x,y)<=0x60);++backdrop_pixels;
        }
    assert(backdrop_pixels>1000);
    old=live;ui_weather_release();assert(old-live==684u*472u/2u);
    size_t available=capacity;capacity=live;assert(!ui_weather_prepare());capacity=available;
    memset(f,255,684*1216/2);memcpy(before,f,684*1216/2);ui_weather_draw(f,region,false,&view,now);outside(f,before,region);
    perimeter(f,region,999);
    const EpdRect invalid[]={{0,-1,684,472},{0,745,684,472},{1,640,684,472},{0,640,612,448}};
    for(unsigned i=0;i<4;++i){
        memcpy(before,f,684*1216/2);ui_weather_draw(f,invalid[i],false,&view,now);
        assert(!memcmp(before,f,684*1216/2));
        assert(ui_weather_hit(invalid[i],630,1086)==UI_WEATHER_NONE);
    }
    ui_weather_release();free(f);free(before);
    assert(temperature_labels>=28);
    assert(loading_labels&&cached_labels&&failed_labels&&clock_labels&&city_labels&&!refresh_labels);
    puts("PASS: full-width weather, seven scenes, two slots and detail, two rotations, rectangular perimeters, navigation isolation, exhaustive action boundaries, 161424-byte scratch and allocation fallback");
}
"""
with tempfile.TemporaryDirectory(prefix="weather-raster-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    os.environ.update(env)
    host.write_shims(work)
    with (work / "esp_heap_caps.h").open("a") as output:
        output.write("\nvoid *heap_caps_realloc(void *,size_t,int);\n")
    (work / "test.c").write_text(unit)
    flags = compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
        "-Wno-unused-function", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-g"]
    for directory in (work, ROOT / "main/ui", ROOT / "main/font", ROOT / "main", ROOT / "main/boot",
                      ROOT / "main/diagnostics", ROOT / "main/display", ROOT / "main/settings",
                      ROOT / "main/services", ROOT / "tools", ROOT / "managed_components/espressif__cjson/cJSON"):
        flags += ["-I" + str(directory)]
    subprocess.run(flags + ["-c", str(ROOT / "main/font/ttf_font.c"), "-o", str(work / "ttf.o")], check=True)
    sources = [work / "test.c", work / "assets.c", work / "ttf.o",
        ROOT / "main/font/ttf_font_source.c", ROOT / "main/font/ttf_block_cache.c",
        ROOT / "main/font/ttf_glyph_cache.c", ROOT / "main/services/weather_model.c",
        ROOT / "managed_components/espressif__cjson/cJSON/cJSON.c"]
    subprocess.run(flags + list(map(str, sources)) + ["-Wl,-dead_strip", "-lm", "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test"), str(ROOT / "main/assets/builtin.ttf")], check=True)
