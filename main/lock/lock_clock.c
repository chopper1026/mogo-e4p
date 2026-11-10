/* SPDX-License-Identifier: Apache-2.0
 * 中文：只读RTC锚点，按分钟身份去重；绘屏时恢复并重新收口IOE供电，触摸、前光与网络保持休眠。
 * English: Read-only RTC anchors and minute identities; bracket scans with IOE rail restore/parking while touch, light and radios stay parked.
 */
#include "lock_clock.h"
#if CONFIG_APP_LOCK_SLEEP_TRACE
#include "lock_sleep_trace.h"
#endif
#include "ui_lock_clock.h"
#include "device_power.h"
#include "board_hardware.h"
#include "display.h"
#include "display_refresh.h"
#include "esp_timer.h"
#include <sys/time.h>
#include <time.h>
#include <string.h>

typedef struct { int64_t minute; int power; } clock_identity_t;
static clock_identity_t s_drawn, s_painted;
static bool s_painted_valid;
static int64_t s_retry_us, s_anchor_us, s_rtc_read_us;
static uint32_t s_anchor_utc;
static bool s_anchor_valid, s_rtc_attempted;
static bool utc_valid(int64_t utc) { return utc >= INT64_C(1704067200) && utc < INT64_C(4102444800); }
static clock_identity_t sample(ui_lock_clock_face_t *face, bool force) {
    const int64_t now = esp_timer_get_time();
    if (force || !s_rtc_attempted || now < s_rtc_read_us || now-s_rtc_read_us >= INT64_C(30000000)) {
        uint32_t utc;
        s_rtc_attempted=true;s_rtc_read_us=now;
        if (device_power_read_rtc(&utc)==ESP_OK && utc_valid(utc)) {
            s_anchor_utc=utc;s_anchor_us=now;s_anchor_valid=true;
        } else s_anchor_valid=false;
    }
    struct timeval tv;gettimeofday(&tv,NULL);
    int64_t utc = s_anchor_valid && now>=s_anchor_us
        ? (int64_t)s_anchor_utc+(now-s_anchor_us)/1000000 : (int64_t)tv.tv_sec;
    *face=(ui_lock_clock_face_t){0};
    if (utc_valid(utc)) {
        time_t china=(time_t)(utc+8*3600);struct tm day;
        if (gmtime_r(&china,&day))
            *face=(ui_lock_clock_face_t){.valid=true,.hour=day.tm_hour,.minute=day.tm_min,
                .month=day.tm_mon+1,.day=day.tm_mday,.weekday=day.tm_wday};
    }
    const pmu_snapshot_t *pmu=device_power_get();
    return (clock_identity_t){.minute=face->valid?utc/60:-1,
        .power=(pmu_battery_percent(pmu)+1)*2+(pmu_battery_charging(pmu)?1:0)};
}
bool lock_clock_draw(uint8_t *fb) {
    if (!fb) return false;
    // 密码取消后重建整张白底；分钟 tick 才只清时钟区域。/ Clear the complete PIN backdrop here; minute ticks clear only the clock area.
    memset(fb,255,(size_t)epd_width()*epd_height()/2);
    ui_lock_clock_face_t face;s_drawn=sample(&face,true);
    ui_lock_clock_draw(fb,&face);s_painted_valid=false;s_retry_us=0;
    return true;
}
void lock_clock_presented(bool success) {
    if (success) { s_painted=s_drawn;s_painted_valid=true; }
}
bool lock_clock_tick(EpdiyHighlevelState *hl) {
    if (!hl) return false;
    const int64_t now=esp_timer_get_time();
    if (now<s_retry_us && s_retry_us-now<=INT64_C(2000000)) return false;
#if CONFIG_APP_LOCK_SLEEP_TRACE
    lock_sleep_trace_mark(LOCK_TRACE_CLOCK_SAMPLE, 0);
#endif
    ui_lock_clock_face_t face;clock_identity_t current=sample(&face,false);
    if (s_painted_valid && current.minute==s_painted.minute && current.power==s_painted.power) return false;
    s_drawn=current;
#if CONFIG_APP_LOCK_SLEEP_TRACE
    lock_sleep_trace_mark(LOCK_TRACE_CLOCK_DRAW, 0);
#endif
    ui_lock_clock_draw(epd_hl_get_framebuffer(hl),&face);
    // 浅睡已关闭P14供电；只恢复IOE，不能调用会唤醒前光/触摸的完整外设入口。
    // Standby cuts P14: restore the IOE rails without resuming the frontlight or touch.
    const esp_err_t wake_error=board_ioe_wake();
    if (wake_error!=ESP_OK) {
        (void)board_ioe_sleep(false);
        s_retry_us=esp_timer_get_time()+INT64_C(2000000);
        return true;
    }
#if CONFIG_APP_LOCK_SLEEP_TRACE
    lock_sleep_trace_mark(LOCK_TRACE_POWER_ON, 0);
#endif
    epd_poweron();
#if CONFIG_APP_LOCK_SLEEP_TRACE
    lock_sleep_trace_mark(LOCK_TRACE_SCAN, 0);
#endif
    enum EpdDrawError result=display_refresh_area(hl,DISPLAY_REFRESH_DETAIL,ui_lock_clock_area(),DISPLAY_REFRESH_DEFAULT);
#if CONFIG_APP_LOCK_SLEEP_TRACE
    lock_sleep_trace_mark(LOCK_TRACE_GUARD, (int32_t)result);
#endif
    guard_draw_result(hl,result);
#if CONFIG_APP_LOCK_SLEEP_TRACE
    lock_sleep_trace_mark(LOCK_TRACE_POWER_OFF, (int32_t)result);
#endif
    epd_poweroff();
    (void)board_ioe_sleep(false);
#if CONFIG_APP_LOCK_SLEEP_TRACE
    lock_sleep_trace_mark(LOCK_TRACE_CLOCK_DONE, (int32_t)result);
#endif
    lock_clock_presented(result==EPD_DRAW_SUCCESS);
    s_retry_us=esp_timer_get_time()+INT64_C(2000000);
    return true;
}
