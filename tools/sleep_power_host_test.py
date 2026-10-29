#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：执行 S31 板级收口与应用下电路径，验证外设恢复和失败边界。
# English: Exercise S31 board parking and application power-off, including peripheral restore and failure boundaries.
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
board = ROOT / 'components/board_support/board_metalio_e4p.c'
sleep = ROOT / 'main/power/sleep.c'
text = board.read_text()
defines = '\n'.join(line for line in text.replace('\\\n', '').splitlines()
                    if re.match(r'#define (?:IOE_|IO_|P0_|P1_)', line))
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef int esp_err_t;
typedef void *cst836u_handle_t;
typedef void *sc7a20h_handle_t;
typedef int app_sleep_mode_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_TIMEOUT -4
#define ESP_ERR_NOT_SUPPORTED -5
#define APP_SLEEP_LIGHT 0
#define APP_SLEEP_DEEP 1
#define APP_SLEEP_OFF 2
#define CST836U_MODE_NORMAL 0
#define CST836U_MODE_DEEPSLEEP 3
#define HOST_EN_DROP_WAIT_MS 5000
#define CONFIG_PM_ESP_SLEEP_POWER_DOWN_CPU 1
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static void *s_ioe = (void *)1;
static bool s_ioe_ok, s_pwrup, s_vcom_ctrl, s_wakeup;
static uint8_t s_p0_extra, s_p1_out;
static bool rails_on;
static cst836u_handle_t s_tp;
static sc7a20h_handle_t s_acc;
static bool s_acc_was_on;
static uint8_t config[2], output[2];
static int step, fault, clears;
static bool buzzer, acc_on, transfer_ok = true, ota;
static bool cover_ok = true, avatar_ok = true;
static bool book_cover_jobs_quiesce(unsigned ms) { assert(ms == 2000); return cover_ok; }
static bool ui_avatar_quiesce(unsigned ms) { assert(ms == 2000); return avatar_ok; }
static int tp_mode, touch_wakes, acc_ups, acc_downs, cpu_requests;
static int off_calls, deep_calls, pauses, resumes, syncs, prepared, radio_stops;
static int error_result, waits;
static bool soft_supported;
static esp_err_t operation(void) { return ++step == fault ? ESP_FAIL : ESP_OK; }
static esp_err_t fca9555_set_output(void *h, int p, uint8_t v) {
    assert(h == s_ioe); esp_err_t e = operation(); if (!e) output[p] = v; return e;
}
static esp_err_t fca9555_set_config(void *h, int p, uint8_t v) {
    assert(h == s_ioe); esp_err_t e = operation(); if (!e) config[p] = v; return e;
}
static esp_err_t fca9555_set_inversion(void *h, int p, uint8_t v) {
    (void)h; (void)p; (void)v; return operation();
}
static esp_err_t fca9555_clear_int(void *h) { assert(h == s_ioe); ++clears; return ESP_OK; }
static bool board_buzzer_score_busy(void) { return buzzer; }
static int board_buzzer_score_stop(void) { buzzer = false; return ESP_OK; }
static bool sc7a20h_powered(void *h) { return h && acc_on; }
static int sc7a20h_power_down(void *h) { assert(h == s_acc); acc_on = false; ++acc_downs; return ESP_OK; }
static int sc7a20h_power_up(void *h) { assert(h == s_acc); acc_on = true; ++acc_ups; return ESP_OK; }
static int cst836u_get_mode(void *h) { assert(h == s_tp); return tp_mode; }
static int cst836u_set_mode(void *h, int m) { assert(h == s_tp); tp_mode = m; return ESP_OK; }
static int cst836u_wake(void *h);
static int sleep_cpu_configure(bool enabled) { assert(enabled); ++cpu_requests; return ESP_OK; }
static bool pico_online_busy(void) { return ota; }
static bool wifi_transfer_pause_for_sleep(void) { ++pauses; return transfer_ok; }
static void wifi_transfer_resume_after_sleep(void) { ++resumes; }
static int ble_pt_stop(int timeout) { assert(timeout == 2000); ++radio_stops; return ESP_OK; }
static int fl_pauses, fl_resumes;
static void frontlight_suspend(void) { ++fl_pauses; }
static void frontlight_resume(void) { ++fl_resumes; }
static int app_sync_time_to_pmu(void) { ++syncs; return ESP_OK; }
static void app_time_service_cancel(void) {}
static void pmu_selftest_prepare_powerdown(void) { ++prepared; rails_on = false; }
static int pmu_result(void);
static int device_power_power_off(void) { ++off_calls; return pmu_result(); }
static int device_power_report_sleep(void) {
    ++deep_calls;
    return soft_supported ? pmu_result() : ESP_ERR_NOT_SUPPORTED;
}
static void vTaskDelay(int ms) { waits += ms; }
'''
unit += defines + '\n'
for name in ('ioe_write', 'p0_value', 'p0_write', 'p1_write', 'board_ioe_sleep', 'board_ioe_wake'):
    unit += function(name, board) + '\n'
unit += r'''
static int cst836u_wake(void *h) {
    assert(h == s_tp && config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
    assert((output[1] & P1_RAIL_MASK) == P1_RAIL_MASK);
    tp_mode = CST836U_MODE_NORMAL; ++touch_wakes; return ESP_OK;
}
static int pmu_result(void) {
    assert(prepared && !rails_on && !acc_on && tp_mode == CST836U_MODE_DEEPSLEEP);
    assert(config[1] == P1_INPUT_MASK && !(output[0] & (P0_TPS_MASK | IOE_BIT(IO_MOTOR))));
    assert(!(output[1] & IOE_BIT(IO_PWR_PULSE)));
    return error_result;
}
'''
unit += function('storage_jobs_quiesce', ROOT / 'main/services/storage_jobs.c') + '\n'
unit += function('app_lifecycle_prepare_poweroff', ROOT / 'main/app/app_lifecycle.c') + '\n'
for name in ('app_sleep_bind', 'periph_suspend', 'periph_resume', 'app_enter_host_sleep'):
    unit += function(name, sleep) + '\n'
unit += r'''
static void reset(void) {
    s_ioe = (void *)1; s_ioe_ok = true; step = fault = clears = 0;
    config[0] = P0_INPUT_MASK; config[1] = P1_INPUT_MASK;
    s_pwrup = s_vcom_ctrl = s_wakeup = true;
    s_p0_extra = IOE_BIT(IO_MOTOR) | IOE_BIT(IO_CAM_SCR) | IOE_BIT(IO_PA_EN);
    output[0] = p0_value();
    s_p1_out = output[1] = P1_RAIL_MASK | IOE_BIT(IO_PWR_PULSE);
    rails_on = false; buzzer = acc_on = true; s_acc_was_on = false;
    tp_mode = CST836U_MODE_NORMAL;
    touch_wakes = acc_ups = acc_downs = off_calls = deep_calls = pauses = resumes = 0;
    syncs = prepared = radio_stops = waits = 0; error_result = ESP_FAIL;
    transfer_ok = true; ota = false; soft_supported = false;
    fl_pauses = fl_resumes = 0;
}
int main(void) {
    reset(); app_sleep_bind((void *)2, (void *)3); assert(cpu_requests == 1);
    // 当前板浅睡与下电均保留触摸/功放供电，马达和关机脉冲必须关闭。
    // Both light sleep and power-off retain touch/PA rails while stopping the motor and power pulse.
    for (int off = 0; off < 2; ++off) {
        reset(); periph_suspend(off);
        assert(!buzzer && !acc_on && tp_mode == CST836U_MODE_DEEPSLEEP);
        assert(config[1] == P1_INPUT_MASK && (output[1] & P1_RAIL_MASK) == P1_RAIL_MASK);
        assert(!(output[0] & (P0_TPS_MASK | IOE_BIT(IO_MOTOR))));
        assert((output[0] & (IOE_BIT(IO_CAM_SCR) | IOE_BIT(IO_PA_EN))) != 0);
        periph_resume();
        assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
        assert(acc_on && acc_ups == 1 && touch_wakes == 1 && tp_mode == CST836U_MODE_NORMAL);
        assert(fl_pauses == 1 && fl_resumes == 1);
    }
    reset(); acc_on = false; periph_suspend(false); periph_resume(); assert(!acc_on && !acc_ups);
    reset(); s_ioe_ok = false;
    assert(board_ioe_sleep(true) == ESP_ERR_INVALID_STATE);
    assert(board_ioe_wake() == ESP_ERR_INVALID_STATE && !step);
    // TCA 唤醒配置失败返回错误；故障消失后能够重新恢复方向和供电锁存。
    // TCA wake configuration faults return errors; clearing the fault permits direction and rail restoration.
    reset(); fault = 1; assert(board_ioe_wake() == ESP_FAIL);
    fault = 0; assert(board_ioe_wake() == ESP_OK);
    assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
    // S31 不支持 PMU 软睡：只尝试一次，再转入关机路径；失败后恢复外设。
    // S31 has no PMU soft-sleep handoff: try once, escalate to power-off, and restore peripherals on failure.
    for (int mode = APP_SLEEP_DEEP; mode <= APP_SLEEP_OFF; ++mode) for (int ack = 0; ack < 2; ++ack) {
        reset(); rails_on = true; error_result = ack ? ESP_OK : ESP_FAIL;
        assert(app_enter_host_sleep(mode) == (ack ? ESP_ERR_TIMEOUT : ESP_FAIL));
        assert(prepared == 1 && syncs == 1 && radio_stops == 1 && resumes == 1 && pauses == 1);
        assert(off_calls == 1 && deep_calls == (mode == APP_SLEEP_DEEP));
        assert(waits == (ack ? 5000 : 0));
        assert(acc_on && tp_mode == CST836U_MODE_NORMAL);
        assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
    }
    // 通用应用交接仍保留有界重试，成功确认却未掉电时按超时恢复。
    // The shared application handoff keeps bounded retries and restores on an acknowledged but stuck power-off.
    reset(); soft_supported = true;
    assert(app_enter_host_sleep(APP_SLEEP_DEEP) == ESP_FAIL && deep_calls == 3 && !off_calls && waits == 400);
    reset(); transfer_ok = false; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !prepared && !step);
    reset(); ota = true; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !pauses && !prepared);
    reset(); cover_ok = false; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !pauses && !prepared); cover_ok = true;
    reset(); avatar_ok = false; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !pauses && !prepared); avatar_ok = true;
    reset(); assert(app_enter_host_sleep(APP_SLEEP_LIGHT) == ESP_ERR_INVALID_ARG && !pauses && !prepared);
    puts("PASS: actual S31 peripheral parking/restore, motor isolation, touch/PA rails, TCA faults, soft-sleep escalation, bounded handoff and storage/OTA guards");
}
'''
# 单独执行实际 Metalio 关机脉冲，避免应用层替身掩盖底层失败或无限重试。
# Execute the real Metalio pulse train separately so application doubles cannot hide driver failures or unbounded retries.
power_backend = ROOT / 'components/device_power/device_power_metalio.c'
power_source = power_backend.read_text()
shutdown_unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED -5
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGW(...) ((void)0)
static unsigned edges, fail_edge, delays, elapsed, deep_sleeps;
static bool charger_fault;
static esp_err_t cx_charger_park(void){return charger_fault ? ESP_FAIL : ESP_OK;}
static bool level;
static esp_err_t board_pwr_pulse(bool high) {
    ++edges;
    assert(high == ((edges & 1) != 0));
    if (edges == fail_edge) return ESP_FAIL;
    level = high;
    return ESP_OK;
}
static void vTaskDelay(unsigned ms) { assert(ms == 100); ++delays; elapsed += ms; }
static void esp_deep_sleep_start(void) { ++deep_sleeps; }
'''
shutdown_unit += '\n'.join(re.findall(r'^#define PWR_.*$', power_source, re.M)) + '\n'
for name in ('device_power_report_sleep', 'device_power_power_off'):
    shutdown_unit += function(name, power_backend) + '\n'
shutdown_unit += r'''
int main(void) {
    assert(device_power_report_sleep() == ESP_ERR_NOT_SUPPORTED && !edges);
    charger_fault=true;assert(device_power_power_off()==ESP_FAIL && !edges && !deep_sleeps);
    charger_fault=false;
    for (unsigned fault = 1; fault <= 9; ++fault) {
        edges = delays = elapsed = deep_sleeps = 0; level = false; fail_edge = fault;
        assert(device_power_power_off() == ESP_FAIL);
        assert(edges == fault && delays == fault - 1 && !deep_sleeps);
    }
    edges = delays = elapsed = deep_sleeps = fail_edge = 0; level = false;
    assert(device_power_power_off() == ESP_OK);
    assert(edges == 300 && delays == 300 && elapsed == 30000 && deep_sleeps == 1 && !level);
    puts("PASS: actual Metalio unsupported soft sleep, pulse ordering, immediate error return, bounded trains and deep-sleep fallback");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    for name, source in (('handoff', unit), ('shutdown', shutdown_unit)):
        c, exe = Path(folder) / (name + '.c'), Path(folder) / name
        c.write_text(source)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
