/* SPDX-License-Identifier: Apache-2.0
 * 中文：可选的 RTC 锁屏复位现场；只写内存，不改变睡眠、供电或看门狗策略。
 * English: Optional retained lock/reset breadcrumbs; no sleep, power or watchdog policy changes.
 */
#pragma once

#include <stdint.h>
#include "esp_attr.h"

typedef enum {
    LOCK_TRACE_ADMISSION = 1,
    LOCK_TRACE_PARKED,
    LOCK_TRACE_SLEEP_ENTER,
    LOCK_TRACE_SLEEP_RETURN,
    LOCK_TRACE_CACHE_RESTORED,
    LOCK_TRACE_CLOCK_SAMPLE,
    LOCK_TRACE_CLOCK_DRAW,
    LOCK_TRACE_POWER_ON,
    LOCK_TRACE_SCAN,
    LOCK_TRACE_GUARD,
    LOCK_TRACE_POWER_OFF,
    LOCK_TRACE_CLOCK_DONE,
    LOCK_TRACE_EXIT,
    LOCK_TRACE_PMU_ENTER,
    LOCK_TRACE_MPLL_ENTER,
    LOCK_TRACE_MPLL_RETURN,
    LOCK_TRACE_PMU_RETURN,
} lock_sleep_phase_t;

void lock_sleep_trace_boot(void);
void lock_sleep_trace_begin(void);
void lock_sleep_trace_mark(lock_sleep_phase_t phase, int32_t detail);
void lock_sleep_trace_replay(int64_t uptime_us);
