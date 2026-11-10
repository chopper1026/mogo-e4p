#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check retained lock/reset breadcrumbs, first-reset preservation and bounded replay."""
from pathlib import Path
import re
import subprocess
import tempfile
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'main/diagnostics/lock_sleep_trace.h'
SOURCE = ROOT / 'main/diagnostics/lock_sleep_trace.c'
UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define CONFIG_APP_LOCK_SLEEP_TRACE 1
#define RTC_NOINIT_ATTR
#define IRAM_ATTR
static unsigned logs;
static int reason;
#define ESP_LOGW(...) (++logs)
static int esp_reset_reason(void) {return reason;}
'''
UNIT += re.sub(r'^#(?:include|pragma)[^\n]*\n', '', HEADER.read_text(), flags=re.M)
UNIT += re.sub(r'^#include[^\n]*\n', '', SOURCE.read_text(), flags=re.M)
UNIT += r'''
int main(void) {
    reason=1;lock_sleep_trace_boot();assert(!s_previous_valid&&logs==1);
    lock_sleep_trace_mark(LOCK_TRACE_SCAN,-7);assert(!s_trace.magic);
    lock_sleep_trace_begin();assert(s_trace.magic==LOCK_TRACE_MAGIC&&!s_trace.reset_reason);
    lock_sleep_trace_mark(LOCK_TRACE_SLEEP_ENTER,0);
    lock_sleep_trace_mark(LOCK_TRACE_MPLL_ENTER,0);
    lock_sleep_trace_mark(LOCK_TRACE_MPLL_RETURN,0);
    lock_sleep_trace_mark(LOCK_TRACE_SLEEP_RETURN,-5);
    assert(s_trace.phase==LOCK_TRACE_SLEEP_RETURN&&s_trace.detail==-5&&s_trace.sleeps==1);
    lock_sleep_trace_mark(LOCK_TRACE_CLOCK_SAMPLE,0);
    lock_sleep_trace_mark(LOCK_TRACE_SCAN,-9);
    assert(s_trace.ticks==1&&s_trace.sequence==6);
    reason=4;lock_sleep_trace_boot();assert(s_previous_valid&&s_previous.reset_reason==4);
    assert(s_previous.phase==LOCK_TRACE_SCAN&&s_previous.detail==-9&&s_previous.sleeps==1);
    reason=11;lock_sleep_trace_boot();assert(s_previous.reset_reason==4&&s_boot_reset==11);
    unsigned before=logs;
    lock_sleep_trace_replay(9999999);assert(logs==before);
    lock_sleep_trace_replay(10000000);lock_sleep_trace_replay(29999999);assert(logs==before+1);
    lock_sleep_trace_replay(30000000);lock_sleep_trace_replay(60000000);
    lock_sleep_trace_replay(INT64_MAX);assert(logs==before+3);
    lock_sleep_trace_begin();assert(!s_trace.reset_reason&&!s_trace.sleeps&&!s_trace.ticks);
    lock_sleep_trace_mark(LOCK_TRACE_EXIT,3);reason=3;lock_sleep_trace_boot();
    assert(s_previous.phase==LOCK_TRACE_EXIT&&s_previous.detail==3&&s_previous.reset_reason==3);
    s_trace.phase=999;lock_sleep_trace_boot();assert(!s_previous_valid&&!s_previous.magic);
    puts("PASS: RTC lock stages, first reset across USB reconnect, counters, new-lock reset, invalid state and bounded replay");
}
'''
with tempfile.TemporaryDirectory(prefix='lock-sleep-trace-') as folder:
    work = Path(folder)
    env, compiler = toolchain(work)
    source = work / 'test.c'
    source.write_text(UNIT)
    subprocess.run(compiler + ['-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', str(source), '-o', str(work/'test')],
                   env=env, check=True)
    subprocess.run([str(work/'test')], env=env, check=True)
