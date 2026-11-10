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
    code+='\n#include "assets/calendar_digits.h"\n#define ui_text_fixed_vc checked_text\n'
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
static void screenshot(uint8_t *fb,const char *folder,const char *name){char path[2048];snprintf(path,sizeof(path),"%s/%s.raw",folder,name);save(path,fb);}
int main(int argc,char **argv){
    assert(argc==2&&ttf_font_open_builtin()==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);
    calendar_date_t d;
    clock_ms=1794239999000LL;variant=APP_CALENDAR_ALMANAC;
    assert(!lock_calendar_draw(NULL));assert(lock_calendar_draw(fb));uint64_t key=lock_calendar_key();
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
            ui_calendar_draw(fb,&d,style);char name[96];snprintf(name,sizeof(name),"%s-%04d-%02d-%02d",style?"monthly":"almanac",d.year,d.month,d.day);screenshot(fb,argv[1],name);
            assert(pixel_at(fb,0,0)==0xf0&&pixel_at(fb,683,1215)==0xf0);
        }
    }
    for(unsigned style=0;style<2;++style){ui_calendar_draw(fb,NULL,style);screenshot(fb,argv[1],style?"monthly-invalid":"almanac-invalid");}
    // 三年内每月的首末日，覆盖全部字形、四/五/六行布局与最长宜忌。
    // First/last days across three years cover glyphs, four/five/six-row grids and long folk lists.
    for(int y=2024;y<=2026;++y)for(int m=1;m<=12;++m){
        assert(calendar_date(y,m,1,&d));
        for(int edge=0;edge<2;++edge){if(edge)assert(calendar_date(y,m,d.days,&d));ui_calendar_draw(fb,&d,false);ui_calendar_draw(fb,&d,true);}
    }
    for(unsigned i=0;i<sizeof(calendar_yiji)/sizeof(calendar_yiji[0]);++i)assert(ui_font_has_text(calendar_yiji[i]));
    free(fb);ttf_font_unload();puts("PASS: real calendar faces, 72 edge dates, built-in glyphs/text bounds, six-row months, leap months and invalid-time prompts; ASan/UBSan");
}
'''
    return code

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args()
    if args.output: native.run(args.output.resolve(),unit())
    else:
        with tempfile.TemporaryDirectory(prefix='calendar-ui-') as folder: native.run(Path(folder),unit())
