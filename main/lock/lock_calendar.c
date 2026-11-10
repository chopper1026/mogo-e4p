/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 */
/* 中文：将设备时间交给离线历法和纯绘制；不唤醒网络、不增加帧缓存。
 * English: Bridge device time to offline calendar drawing without networking or another framebuffer.
 */
#include "lock_calendar.h"
#include "ui_calendar.h"
#include "settings.h"
#include "device_power.h"
#include <sys/time.h>
static uint64_t s_frame_key;
static uint64_t date_key(const calendar_date_t *date,bool valid) {
    uint64_t day=valid?(uint64_t)(date->year*10000+date->month*100+date->day):0;
    return UINT64_C(0x43414c0300000000)|(day<<1)|(uint64_t)app_settings_calendar_style();
}
static bool today(calendar_date_t *date) {
    struct timeval tv;gettimeofday(&tv,NULL);
    int64_t ms=(int64_t)tv.tv_sec*1000+tv.tv_usec/1000;
    if(ms<INT64_C(1704067200000)){
        const pmu_snapshot_t *pmu=device_power_get();
        if(pmu&&pmu->time_synced)ms=(int64_t)pmu->unix_sec*1000;
    }
    return calendar_from_utc(ms,date);
}
uint64_t lock_calendar_key(void) {
    calendar_date_t date;bool valid=today(&date);
    return date_key(&date,valid);
}
uint64_t lock_calendar_frame_key(void) { return s_frame_key?s_frame_key:lock_calendar_key(); }
bool lock_calendar_draw(uint8_t *fb) {
    if(!fb)return false;
    calendar_date_t date;bool valid=today(&date);
    s_frame_key=date_key(&date,valid);
    ui_calendar_draw(fb,valid?&date:NULL,app_settings_calendar_style()==APP_CALENDAR_MONTH);
    return true;
}
