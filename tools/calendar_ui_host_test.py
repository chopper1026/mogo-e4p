#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：执行实际日历绘制、内置字库和边界检查，导出两款原生PNG。
English: Exercise real calendar drawing, builtin glyph coverage and bounds with native PNG exports.
"""
import argparse
from pathlib import Path
import tempfile
import file_ui_host_test as native
import reader_title_style_preview as host
import reading_journal_ui_host_test as journal
ROOT=Path(__file__).resolve().parents[1]

def unit():
    code=native.unit().split('typedef struct {char path[288];',1)[0]
    code+='\n#include "time/calendar.h"\n'
    code+=host.function('epd_draw_rect',ROOT/'components/epdiy/src/epdiy.c')+'\n'
    code+=journal.source('main/time/calendar.c').replace('#define COUNT', '#include "assets/calendar_data.h"\n#define COUNT')+'\n'
    code+=r'''
#include "assets/calendar_digits.h"
static bool check_almanac;
static EpdRect calendar_text_boxes[128];
static unsigned calendar_box_count,calendar_gods,calendar_grid_days,calendar_activity_items;
static void calendar_checked_text(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align,bool white){
    if(y>450&&y<1160&&s[0]&&strspn(s,"0123456789")==strlen(s))++calendar_grid_days;
    const char *const labels[]={"财神","喜神","福神","吉神宜趋","凶煞宜忌"};
    for(unsigned i=0;i<5;++i)if(!strcmp(s,labels[i]))calendar_gods|=1u<<i;
    if(check_almanac){
        if(y>=580&&y<=690&&(x==192||x==492)){
            ++calendar_activity_items;
            for(const char *p=s;(p=strstr(p,"　"));p+=3)++calendar_activity_items;
        }
        int width=ui_text_fixed_width_px(px,s),above=0,below=0;
        ui_font_measure_line_px(px,s,&above,&below);
        int left=x-(align==EPD_DRAW_ALIGN_CENTER?width/2:align==EPD_DRAW_ALIGN_RIGHT?width:0);
        int top=y+(above-below)/2-above;
        EpdRect box={left,top,width,above+below};
        for(unsigned i=0;i<calendar_box_count;++i){
            EpdRect b=calendar_text_boxes[i];
            if(box.x<b.x+b.width&&b.x<box.x+box.width&&box.y<b.y+b.height&&b.y<box.y+box.height){
                fprintf(stderr,"Overlapping almanac text: %s at %d,%d\n",s,x,y);abort();
            }
        }
        assert(calendar_box_count<128);calendar_text_boxes[calendar_box_count++]=box;
        if(y>=900&&y<=1040&&x==218)assert(left>=218&&left+width<=608&&px==26);
    }
    checked_text(fb,x,y,px,s,align,white);
}
#define ui_text_fixed_vc calendar_checked_text
'''
    code+=journal.source('main/ui/ui_calendar.c')+'\n'
    code+=r"""
#include <sys/time.h>
typedef enum {APP_CALENDAR_ALMANAC,APP_CALENDAR_MONTH} app_calendar_style_t;
static int64_t clock_ms;
static app_calendar_style_t variant;
typedef struct {bool time_synced;uint32_t unix_sec;} pmu_snapshot_t;
static pmu_snapshot_t pmu;
static const pmu_snapshot_t *device_power_get(void){return &pmu;}
static app_calendar_style_t app_settings_calendar_style(void){return variant;}
static int test_gettimeofday(struct timeval *v,void *zone){(void)zone;v->tv_sec=clock_ms/1000;v->tv_usec=(clock_ms%1000)*1000;return 0;}
#define gettimeofday test_gettimeofday
"""
    code+=journal.source('main/lock/lock_calendar.c')+'\n#undef gettimeofday\n'
    code+=r'''
static void render_face(uint8_t *fb,const calendar_date_t *d,bool monthly){
    check_almanac=!monthly;calendar_box_count=calendar_gods=calendar_grid_days=calendar_activity_items=0;
    ui_calendar_draw(fb,d,monthly);check_almanac=false;
    if(d){assert(calendar_grid_days==(monthly?(unsigned)d->days:0));assert(calendar_gods==(monthly?0:31));}
}
static void screenshot(uint8_t *fb,const char *folder,const char *name){char path[2048];snprintf(path,sizeof(path),"%s/%s.raw",folder,name);save(path,fb);}
int main(int argc,char **argv){
    assert(argc==2&&ttf_font_open_builtin()==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);
    calendar_date_t d;
    clock_ms=1794239999000LL;variant=APP_CALENDAR_ALMANAC;
    assert(!lock_calendar_draw(NULL));assert(lock_calendar_draw(fb));uint64_t key=lock_calendar_key();
    assert((key&UINT64_C(0xffffffff00000000))==UINT64_C(0x43414c0400000000));
    assert(key==lock_calendar_frame_key());clock_ms+=1000;
    assert(key!=lock_calendar_key()&&key==lock_calendar_frame_key());
    assert(lock_calendar_draw(fb)&&lock_calendar_key()==lock_calendar_frame_key());
    key=lock_calendar_key();variant=APP_CALENDAR_MONTH;assert(key!=lock_calendar_key());
    assert(lock_calendar_draw(fb));key=lock_calendar_key();
    clock_ms=0;pmu=(pmu_snapshot_t){.time_synced=true,.unix_sec=1794240000};assert(lock_calendar_key()==key);
    pmu.time_synced=false;assert(lock_calendar_key()!=key&&lock_calendar_draw(fb));
    assert(number_width(30,112)<220);

    const int dates[][3]={{2026,11,10},{2026,8,31},{2026,2,1},{2025,7,25},{2024,2,29},{2000,1,1},{2099,12,31},{2026,11,7},{2026,11,22}};
    for(unsigned i=0;i<sizeof(dates)/sizeof(dates[0]);++i){
        assert(calendar_date(dates[i][0],dates[i][1],dates[i][2],&d));
        for(unsigned style=0;style<2;++style){
            render_face(fb,&d,style);char name[96];snprintf(name,sizeof(name),"%s-%04d-%02d-%02d",style?"monthly":"almanac",d.year,d.month,d.day);screenshot(fb,argv[1],name);
            if(!i&&!style)assert(calendar_activity_items==19);
            assert(pixel_at(fb,0,0)==0xf0&&pixel_at(fb,683,1215)==0xf0);
        }
    }
    for(unsigned style=0;style<2;++style){render_face(fb,NULL,style);screenshot(fb,argv[1],style?"monthly-invalid":"almanac-invalid");}
    // 三年内每月的首末日，覆盖全部字形、四/五/六行布局与最长宜忌。
    // First/last days across three years cover glyphs, four/five/six-row grids and long folk lists.
    for(int y=2024;y<=2026;++y)for(int m=1;m<=12;++m){
        assert(calendar_date(y,m,1,&d));
        for(int edge=0;edge<2;++edge){if(edge)assert(calendar_date(y,m,d.days,&d));render_face(fb,&d,false);render_face(fb,&d,true);}
    }
    for(unsigned i=0;i<sizeof(calendar_yiji)/sizeof(calendar_yiji[0]);++i){
        assert(ui_font_has_text(calendar_yiji[i]));
        d.yi=d.ji=calendar_yiji[i];calendar_box_count=0;check_almanac=true;
        almanac_activities(fb,&d);check_almanac=false;
    }
    // 全部720组神煞列表保留固定字号并限制在两行内。/ All 720 spirit pairs stay within two rows at the fixed text size.
    for(unsigned i=0;i<720;++i){
        assert(calendar_date(2026,11,10,&d));
        d.auspicious=calendar_folk[calendar_spirit_pairs[i][0]];d.inauspicious=calendar_folk[calendar_spirit_pairs[i][1]];
        render_face(fb,&d,false);
    }
    for(unsigned i=0;i<sizeof(calendar_folk)/sizeof(calendar_folk[0]);++i)assert(ui_font_has_text(calendar_folk[i]));
    free(fb);ttf_font_unload();puts("PASS: almanac without month grid, three god directions, 720 spirit pairs, no text overlap, 72 edge dates, native glyphs/bounds, monthly grid and clock/cache behavior; ASan/UBSan");
}
'''
    return code

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args()
    if args.output: native.run(args.output.resolve(),unit())
    else:
        with tempfile.TemporaryDirectory(prefix='calendar-ui-') as folder: native.run(Path(folder),unit())
