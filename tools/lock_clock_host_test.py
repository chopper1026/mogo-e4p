#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production clock's RTC anchors, rollovers, deduplication and display retries."""
from pathlib import Path
import re
import subprocess
import tempfile
from run_reader_baseline import toolchain
ROOT=Path(__file__).resolve().parents[1]
UNIT=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
typedef struct {bool valid;int hour,minute,month,day,weekday;} ui_lock_clock_face_t;
typedef struct {int unused;} EpdiyHighlevelState;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int percent;bool charging;} pmu_snapshot_t;
typedef int esp_err_t;
#define ESP_OK 0
#define EPD_DRAW_SUCCESS 0
#define DISPLAY_REFRESH_DETAIL 1
#define DISPLAY_REFRESH_DEFAULT 0
enum EpdDrawError {GOOD=0,BAD=1};
static int64_t now_us,rtc_utc,sys_utc;
static bool rtc_ok,display_ok,powered,ioe_awake,ioe_wake_ok;
static unsigned reads,draws,presents,guards,poweroffs,ioe_wakes,ioe_parks;
static pmu_snapshot_t pmu={76,false};
static ui_lock_clock_face_t face;
static uint8_t framebuffer[684*1216/2];
static int epd_width(void){return 1216;}
static int epd_height(void){return 684;}
static int64_t esp_timer_get_time(void){return now_us;}
static int device_power_read_rtc(uint32_t *utc){++reads;*utc=(uint32_t)rtc_utc;return rtc_ok?0:-1;}
static int fake_gettimeofday(struct timeval *tv,void *tz){(void)tz;tv->tv_sec=sys_utc;tv->tv_usec=0;return 0;}
#define gettimeofday fake_gettimeofday
static const pmu_snapshot_t *device_power_get(void){return &pmu;}
static int pmu_battery_percent(const pmu_snapshot_t *p){return p->percent;}
static bool pmu_battery_charging(const pmu_snapshot_t *p){return p->charging;}
static EpdRect ui_lock_clock_area(void){return (EpdRect){160,288,384,672};}
static uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState *hl){assert(hl);return framebuffer;}
static void ui_lock_clock_draw(uint8_t *fb,const ui_lock_clock_face_t *f){assert(fb==framebuffer);face=*f;++draws;}
static int board_ioe_wake(void){assert(!powered&&!ioe_awake);++ioe_wakes;ioe_awake=true;return ioe_wake_ok?ESP_OK:-1;}
static int board_ioe_sleep(bool host_off){assert(!host_off&&!powered&&ioe_awake);ioe_awake=false;++ioe_parks;return ESP_OK;}
static void epd_poweron(void){assert(!powered&&ioe_awake);powered=true;}
static void epd_poweroff(void){assert(powered&&ioe_awake);powered=false;++poweroffs;}
static enum EpdDrawError display_refresh_area(EpdiyHighlevelState *hl,int intent,EpdRect area,unsigned flags){
 assert(hl&&powered&&intent==DISPLAY_REFRESH_DETAIL&&!flags&&area.x==160&&area.width==384&&area.y==288&&area.height==672);++presents;return display_ok?GOOD:BAD;
}
static void guard_draw_result(EpdiyHighlevelState *hl,enum EpdDrawError error){assert(hl&&error==(display_ok?GOOD:BAD));++guards;}
'''
UNIT+=re.sub(r'^#include[^\n]*\n','',(ROOT/'main/lock/lock_clock.c').read_text(),flags=re.M)
UNIT+=r'''
static void reset(int64_t utc){
 now_us=1000000;rtc_utc=sys_utc=utc;rtc_ok=display_ok=ioe_wake_ok=true;powered=ioe_awake=false;
 reads=draws=presents=guards=poweroffs=ioe_wakes=ioe_parks=0;pmu=(pmu_snapshot_t){76,false};
 s_painted_valid=s_anchor_valid=s_rtc_attempted=false;s_retry_us=s_anchor_us=s_rtc_read_us=0;
}
static void advance(int seconds){now_us+=(int64_t)seconds*1000000;rtc_utc+=seconds;sys_utc+=seconds;}
static void expect(int h,int m,int month,int day,int weekday){assert(face.valid&&face.hour==h&&face.minute==m&&face.month==month&&face.day==day&&face.weekday==weekday);}
int main(void){
 (void)board_ioe_wake;(void)board_ioe_sleep;
 EpdiyHighlevelState hl={0};
 assert(!lock_clock_draw(NULL)&&!lock_clock_tick(NULL));
 // 2026-11-10 16:48:00 CST. No display or RTC writes are used by the production module.
 reset(1794300480);assert(lock_clock_draw(framebuffer));expect(16,48,11,10,2);assert(reads==1);
 assert(framebuffer[0]==255&&framebuffer[sizeof(framebuffer)-1]==255);
 lock_clock_presented(true);assert(!lock_clock_tick(&hl));advance(29);assert(!lock_clock_tick(&hl)&&reads==1);
 advance(1);assert(!lock_clock_tick(&hl)&&reads==2);advance(30);assert(lock_clock_tick(&hl));expect(16,49,11,10,2);
 assert(presents==1&&guards==1&&poweroffs==1&&!powered&&!ioe_awake&&ioe_wakes==1&&ioe_parks==1);
 // Parked board rails must be restored for a real refresh, including scan-failure cleanup.
 // A partially failed restore must never reach panel power-on or acknowledge the new minute.
 reset(1794300480);lock_clock_draw(framebuffer);lock_clock_presented(true);advance(60);ioe_wake_ok=false;
 assert(lock_clock_tick(&hl)&&s_painted.minute!=s_drawn.minute);
 assert(!powered&&!ioe_awake&&!presents&&!poweroffs&&ioe_wakes==1&&ioe_parks==1);
 advance(1);assert(!lock_clock_tick(&hl)&&ioe_wakes==1);
 advance(1);ioe_wake_ok=true;assert(lock_clock_tick(&hl)&&s_painted.minute==s_drawn.minute);
 assert(presents==1&&poweroffs==1&&ioe_wakes==2&&ioe_parks==2&&!ioe_awake);
 advance(60);display_ok=false;assert(lock_clock_tick(&hl));
 assert(presents==2&&poweroffs==2&&ioe_wakes==3&&ioe_parks==3&&!powered&&!ioe_awake);
 // Hour, midnight, year and leap-day rollover, independent of process timezone.
 const struct {int64_t utc;int h,m,month,day,w;} cases[]={
  {1794322799,23,0,11,10,2}, // 22:59:59 -> 23:00
  {1794326399,0,0,11,11,3},
  {1798732799,0,0,1,1,5},
  {1709135999,0,0,2,29,4},
 };
 for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i){reset(cases[i].utc);lock_clock_draw(framebuffer);lock_clock_presented(true);advance(1);assert(lock_clock_tick(&hl));expect(cases[i].h,cases[i].m,cases[i].month,cases[i].day,cases[i].w);}
 // Failed entry and minute presents retry only after two seconds; never acknowledge an unpainted minute.
 reset(1794300480);lock_clock_draw(framebuffer);lock_clock_presented(false);display_ok=false;
 assert(lock_clock_tick(&hl)&&!s_painted_valid);advance(1);assert(!lock_clock_tick(&hl)&&presents==1);
 advance(1);display_ok=true;assert(lock_clock_tick(&hl)&&s_painted_valid&&presents==2);advance(2);assert(!lock_clock_tick(&hl));
 // Battery/charging changes are independent of the minute identity.
 pmu.percent=75;assert(lock_clock_tick(&hl));advance(2);pmu.charging=true;assert(lock_clock_tick(&hl));advance(2);assert(!lock_clock_tick(&hl));
 // RTC correction can move either way; invalid RTC falls back to the system clock without calibrating it.
 reset(1794300480);lock_clock_draw(framebuffer);lock_clock_presented(true);advance(30);rtc_utc+=3600;assert(lock_clock_tick(&hl));expect(17,48,11,10,2);
 advance(30);rtc_utc-=7200;assert(lock_clock_tick(&hl));expect(15,49,11,10,2);
 advance(30);rtc_ok=false;assert(lock_clock_tick(&hl));expect(16,49,11,10,2);
 reset(0);rtc_ok=false;lock_clock_draw(framebuffer);assert(!face.valid);lock_clock_presented(true);advance(60);assert(!lock_clock_tick(&hl));
 sys_utc=1794300480;advance(2);assert(lock_clock_tick(&hl));expect(16,48,11,10,2);
 reset(4102444800LL);lock_clock_draw(framebuffer);assert(!face.valid);
 // A monotonic regression forces a fresh RTC anchor rather than extrapolating a negative duration.
 reset(1794300480);lock_clock_draw(framebuffer);lock_clock_presented(true);now_us=0;rtc_utc+=60;
 assert(lock_clock_tick(&hl)&&reads==2);expect(16,49,11,10,2);
 puts("PASS: production clock RTC cadence/fallback/corrections, UTC+8 rollovers, invalid time, battery, successful-paint identity and bounded retries");
}
'''
with tempfile.TemporaryDirectory(prefix='lock-clock-') as folder:
 work=Path(folder);env,compiler=toolchain(work);(work/'test.c').write_text(UNIT)
 subprocess.run(compiler+['-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],env=env,check=True)
 subprocess.run([str(work/'test')],env=env,check=True)
