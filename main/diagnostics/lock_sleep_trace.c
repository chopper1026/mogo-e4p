/* SPDX-License-Identifier: Apache-2.0 */
#include "sdkconfig.h"
#if CONFIG_APP_LOCK_SLEEP_TRACE
#include "lock_sleep_trace.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdbool.h>

#define LOCK_TRACE_MAGIC UINT32_C(0x4c535431)
typedef struct {
    uint32_t magic, phase, sleeps, ticks, sequence, reset_reason;
    int32_t detail;
} retained_lock_trace_t;
static RTC_NOINIT_ATTR volatile retained_lock_trace_t s_trace;
static retained_lock_trace_t s_previous;
static bool s_previous_valid;
static unsigned s_reports;
static int s_boot_reset;

static void report(void) {
    ESP_LOGW("lock_trace", "previous_lock valid=%d reset=%d first_reset=%lu phase=%lu detail=%ld sleeps=%lu ticks=%lu sequence=%lu",
             s_previous_valid, s_boot_reset, (unsigned long)s_previous.reset_reason,
             (unsigned long)s_previous.phase, (long)s_previous.detail,
             (unsigned long)s_previous.sleeps, (unsigned long)s_previous.ticks,
             (unsigned long)s_previous.sequence);
}

void lock_sleep_trace_boot(void) {
    s_boot_reset = (int)esp_reset_reason();
    s_previous_valid = s_trace.magic == LOCK_TRACE_MAGIC &&
        s_trace.phase >= LOCK_TRACE_ADMISSION && s_trace.phase <= LOCK_TRACE_PMU_RETURN;
    if (s_previous_valid) {
        // USB 重连可能再复位一次；保留本轮锁屏后的第一次复位原因，直到下一次主动锁屏。
        // Keep the first post-lock reset across a later USB reset until another lock begins.
        if (!s_trace.reset_reason) s_trace.reset_reason = (uint32_t)s_boot_reset;
        s_previous = s_trace;
    } else s_previous = (retained_lock_trace_t){0};
    s_reports = 0;
    report();
}

void lock_sleep_trace_begin(void) {
    s_trace.magic = 0;
    s_trace.phase = LOCK_TRACE_ADMISSION;
    s_trace.sleeps = s_trace.ticks = s_trace.sequence = s_trace.reset_reason = 0;
    s_trace.detail = 0;
    s_trace.magic = LOCK_TRACE_MAGIC;
}

void IRAM_ATTR lock_sleep_trace_mark(lock_sleep_phase_t phase, int32_t detail) {
    // PMU/MPLL 包装器也会调用；这里仅使用 RTC RAM 和整数，不依赖 cache、锁或日志。
    // PMU/MPLL wrappers call this with cache unavailable: RTC RAM and integer stores only.
    if (s_trace.magic != LOCK_TRACE_MAGIC) return;
    if (phase == LOCK_TRACE_SLEEP_ENTER) ++s_trace.sleeps;
    if (phase == LOCK_TRACE_CLOCK_SAMPLE) ++s_trace.ticks;
    s_trace.detail = detail;
    s_trace.phase = (uint32_t)phase;
    ++s_trace.sequence;
}

void lock_sleep_trace_replay(int64_t uptime_us) {
    static const int64_t at[] = {INT64_C(10000000), INT64_C(30000000), INT64_C(60000000)};
    if (s_reports < sizeof(at) / sizeof(at[0]) && uptime_us >= at[s_reports]) {
        ++s_reports;
        report();
    }
}
#endif
