#!/usr/bin/env python3
"""中文：执行真实 RTC 标记和设置维护入口，验证开机标记截止与触点路由。
English: Execute real RTC marks and Settings maintenance routing for boot cutoff and tap handling.
SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
CRASH = ROOT / 'components/board_support'
SETTINGS = ROOT / 'main/apps/app_device_settings.c'

BOOT = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int64_t clock_us;
int64_t esp_timer_get_time(void) { return clock_us; }
#include "board_crashlog.c"
int main(void) {
    rp_crash_log_clear();
    assert(!rp_crash_log_count());
    const char *phases[] = {"hw","vcom","splash","font","lock","resume","sdfont","scan","excerpt","enter","frame"};
    for (unsigned i = 0; i < sizeof(phases)/sizeof(phases[0]); ++i) {
        clock_us = (int64_t)(i + 1) * 1000000;
        rp_boot_mark(phases[i]);
    }
    assert(rp_crash_log_count() == 11 && strstr(rp_crash_log_line(0), "boot frame 11000ms"));
    uint32_t serial = rp_crash_log_serial();
    for (unsigned i = 0; i < 10000; ++i) { rp_boot_mark("scan"); rp_boot_mark("excerpt"); }
    assert(rp_crash_log_serial() == serial && rp_crash_log_count() == 11);
    rp_crash_log("still records runtime");
    assert(rp_crash_log_serial() == serial + 1);
    rp_crash_log_clear();
    rp_boot_mark("scan");
    assert(!rp_crash_log_count());
    // 模拟下次启动仅重置普通 BSS，RTC 数据由原接口管理。/ Simulate the next boot's BSS reset; RTC stays owned by its existing API.
    s_boot_marks_done = false;
    clock_us = 23000;
    rp_boot_mark(NULL);
    assert(strstr(rp_crash_log_line(0), "boot ? 23ms"));
    char long_name[512]; memset(long_name,'x',sizeof(long_name)-1); long_name[sizeof(long_name)-1]=0;
    rp_boot_mark(long_name);
    assert(strlen(rp_crash_log_line(0)) < RP_CRASH_LOG_LEN);
    for (unsigned i = 0; i < 100; ++i) rp_crash_log("event %u", i);
    assert(rp_crash_log_count() == RP_CRASH_LOG_LINES);
    puts("PASS: all 11 boot marks, bounded RTC ring, post-frame cutoff and normal runtime logging");
}
'''

gesture = function('on_gesture', SETTINGS)
gesture = gesture[gesture.index('    if(y>=SETTINGS_MAINTENANCE_Y'):gesture.index('    if(y>=SETTINGS_POWER_Y')]
render = function('render', SETTINGS)
# 主列表内容与固定页头分开绘制；分别检查维护行和提示的顺序。
# The scrolling list and fixed header paint separately; check the maintenance rows and notice order independently.
root_render = re.sub(r'\s+', '', render[render.index('    paint_main_list(fb);'):])
assert root_render.index('if(s_notice[0])') > root_render.index(
    'ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,160},UI_GRAY_WHITE,fb);')

rows = re.search(r'^#define SETTINGS_MAINTENANCE_ROWS \d+$', SETTINGS.read_text(), re.M).group()
main_list = function('paint_main_list', SETTINGS)
assert 'maintenance_labels[]={"保存与恢复","系统升级"}' in main_list
assert 'maintenance_icons,maintenance_labels,maintenance_values,SETTINGS_MAINTENANCE_ROWS);' in main_list

ACTIONS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
enum { SETTINGS_MAIN, SETTINGS_CONFIG, SETTINGS_UPGRADE };
#define SETTINGS_MAINTENANCE_Y 500
#define SETTINGS_ROW_H 68
''' + rows + r'''

static int s_page, upgrade_opens;
static void upgrade_open(void) { ++upgrade_opens; s_page = SETTINGS_UPGRADE; }
static bool s_config_confirm;
static char s_notice[96];
static app_redraw_t tap(int y) {
    int row;
''' + gesture + r'''
    return APP_REDRAW_NONE;
}
int main(void) {
    s_config_confirm = true;
    s_notice[0] = 'x';
    assert(tap(SETTINGS_MAINTENANCE_Y)==APP_REDRAW_PAGE && s_page==SETTINGS_CONFIG);
    assert(!s_config_confirm && !s_notice[0]);
    s_page=SETTINGS_MAIN;
    assert(tap(SETTINGS_MAINTENANCE_Y+SETTINGS_ROW_H)==APP_REDRAW_PAGE && s_page==SETTINGS_UPGRADE && upgrade_opens==1);
    assert(tap(SETTINGS_MAINTENANCE_Y+2*SETTINGS_ROW_H)==APP_REDRAW_NONE);
    puts("PASS: Settings maintenance renders and routes only backup/restore and system upgrade");
}
'''

with tempfile.TemporaryDirectory(prefix='mogo-boot-panel-') as directory:
    work = Path(directory)
    (work/'freertos').mkdir()
    (work/'esp_attr.h').write_text('#define RTC_NOINIT_ATTR\n')
    (work/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
    (work/'freertos/FreeRTOS.h').write_text('typedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(p) ((void)(p))\n#define portEXIT_CRITICAL(p) ((void)(p))\n')
    (work/'freertos/task.h').write_text('')
    for name, unit in [('boot', BOOT), ('actions', ACTIONS)]:
        (work/f'{name}.c').write_text(unit, encoding='utf-8')
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        '-I'+str(work),'-I'+str(CRASH),'-I'+str(CRASH/'include'),'-I'+str(ROOT/'main/ui'),
                        str(work/f'{name}.c'),'-o',str(work/name)],check=True)
        subprocess.run([str(work/name)],check=True)
