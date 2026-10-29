#!/usr/bin/env python3
"""中文：执行 Metalio 电池采样，验证读数有效性、充电来源和公共快照。
English: Exercise Metalio battery polling, sample validity, charging sources and public snapshots."""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
backend = root / 'components/device_power/device_power_metalio.c'
source = backend.read_text()
# 提取真实滤波参数与类型；采样流程直接执行产品实现。/ Extract real filter parameters/types and execute the product polling implementation.
defines = '\n'.join(line for line in source.splitlines()
                    if re.match(r'#define (?:BATT_|BQ_REG_|CX_REG_STATUS1\b|CX_CHG_STAT_TOPOFF\b)', line))
curve = re.search(r'^static const int32_t s_soc_curve\[.*?\};', source, re.M | re.S).group(0)
types = '\n'.join(re.search(r'typedef struct \{[^}]*\} ' + name + ';', source, re.S).group(0)
                  for name in ('batt_filter_t', 'batt_log_sample_t'))
code = r'''
#include "device_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define ESP_FAIL -1
#define ESP_OK 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
'''
code += defines + '\n' + curve + '\n' + types + '\n'
code += r'''
static void *s_bq, *s_cx;
static pmu_snapshot_t s_snap;
static batt_filter_t s_batt_filter;
static batt_log_sample_t s_batt_log_sample;
static int s_batt_log_mux;
static int32_t s_batt_saved_permille;
static bool s_batt_charging_last, s_batt_log_enabled;
static int64_t s_batt_last_save_us, s_batt_last_log_us, s_batt_full_since_us;
static int8_t s_batt_ic_soc;
static int64_t now_us;
static uint16_t raw_mv;
static int16_t raw_ma;
static uint8_t charger_state;
static bool voltage_fail, current_fail, charger_fail;
static unsigned saves, soc_reads;
static int64_t esp_timer_get_time(void) { return now_us; }
static void batt_save(int32_t value) { assert(value >= 0 && value <= 1000); ++saves; }
static int bq_read_soc(void) { ++soc_reads; return 63; }
static esp_err_t dev_read(void *dev, uint8_t reg, uint8_t *out, size_t len) {
    if (dev == s_cx) {
        assert(reg == CX_REG_STATUS1 && len == 1);
        if (charger_fail) return ESP_FAIL;
        *out = charger_state << 3; return ESP_OK;
    }
    assert(dev == s_bq && len == 2);
    uint16_t value;
    if (reg == BQ_REG_VOLTAGE) {
        if (voltage_fail) return ESP_FAIL;
        value = raw_mv;
    } else {
        assert(reg == BQ_REG_CURRENT);
        if (current_fail) return ESP_FAIL;
        value = (uint16_t)raw_ma;
    }
    out[0] = value & 0xff; out[1] = value >> 8; return ESP_OK;
}
'''
for name in ('bq_read_voltage', 'bq_read_current', 'batt_curve_soc', 'batt_filter_step', 'batt_full_now', 'poll_battery'):
    code += function(name, backend) + '\n'
code += r'''
static void reset(void) {
    memset(&s_snap, 0, sizeof(s_snap)); s_snap.present = true;
    memset(&s_batt_filter, 0, sizeof(s_batt_filter));
    s_bq = (void *)1; s_cx = (void *)2;
    s_batt_saved_permille = -1; s_batt_full_since_us = -1;
    s_batt_last_save_us = s_batt_last_log_us = now_us = 0;
    s_batt_charging_last = s_batt_log_enabled = false;
    voltage_fail = current_fail = charger_fail = false;
    raw_mv = 4000; raw_ma = 0; charger_state = saves = soc_reads = 0;
}
int main(void) {
    reset(); assert(pmu_battery_percent(&s_snap) == -1);
    poll_battery();
    assert(s_snap.status_ok && !s_snap.quick_ok && pmu_battery_percent(&s_snap) == 56);
    assert(s_snap.battery_raw == 4000 && !pmu_battery_charging(&s_snap));
    // 无效电压和 I2C 失败清有效位，不能继续显示上次电量。
    // Invalid voltage or failed I2C clears validity instead of presenting the last percent.
    const uint16_t invalid[] = {0, 2499, 5001, 65535};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reset(); poll_battery(); raw_mv = invalid[i]; poll_battery();
        assert(pmu_battery_percent(&s_snap) == -1);
    }
    reset(); poll_battery(); voltage_fail = true; poll_battery(); assert(pmu_battery_percent(&s_snap) == -1);
    reset(); s_bq = NULL; poll_battery(); assert(pmu_battery_percent(&s_snap) == -1);
    // CX 状态优先；读取失败或缺芯片时才用有符号电流判断充电。
    // CX status wins; signed current is the charging fallback when CX is absent or unreadable.
    reset(); raw_ma = 200; poll_battery(); assert(!pmu_battery_charging(&s_snap));
    reset(); charger_state = 1; raw_ma = -100; poll_battery(); assert(pmu_battery_charging(&s_snap));
    reset(); charger_fail = true; raw_ma = 21; poll_battery(); assert(pmu_battery_charging(&s_snap));
    reset(); s_cx = NULL; raw_ma = -200; poll_battery(); assert(!pmu_battery_charging(&s_snap));
    assert(s_batt_log_sample.ma == -200 && s_snap.battery_mv == 4036);
    reset(); charger_fail = current_fail = true; poll_battery();
    assert(!pmu_battery_charging(&s_snap) && pmu_battery_percent(&s_snap) == 56);
    // 充满对齐、恢复值只消费一次，日志关闭时不增加库仑计读数。
    // Full-charge alignment, one-time saved value consumption and no coulomb reads while logging is off.
    reset(); charger_state = CX_CHG_STAT_TOPOFF; raw_mv = 4400; poll_battery();
    assert(pmu_battery_percent(&s_snap) == 100 && saves == 1);
    reset(); raw_mv = 3900; s_batt_saved_permille = 430; poll_battery();
    assert(pmu_battery_percent(&s_snap) == 43 && s_batt_saved_permille == -1 && !soc_reads);
    s_batt_log_enabled = true; now_us = (int64_t)BATT_LOG_INTERVAL_MS * 1000; poll_battery();
    assert(soc_reads == 1 && s_batt_log_sample.ic_soc == 63);
    // 公共快照的兼容有效位与边界仍保留，不再依赖旧 CW32 解析器。
    // Keep public snapshot validity/boundary compatibility without the old CW32 parser.
    s_snap.quick_ok = true; s_snap.qb_flags = 3; s_snap.qb_soc = 0; assert(pmu_battery_percent(&s_snap) == 0);
    s_snap.qb_soc = 1000; assert(pmu_battery_percent(&s_snap) == 100);
    s_snap.qb_soc = 1001; assert(pmu_battery_percent(&s_snap) == -1);
    s_snap.qb_soc = 500; s_snap.qb_flags = 1; assert(pmu_battery_percent(&s_snap) == -1);
    s_snap.qb_flags = 7; assert(pmu_battery_charging(&s_snap));
    assert(pmu_battery_percent(NULL) == -1 && !pmu_battery_charging(NULL));
    puts("PASS: actual Metalio battery polling, invalid samples, signed BQ current, CX precedence/fallback, full charge, saved state and snapshot compatibility");
}
'''
with tempfile.TemporaryDirectory() as folder:
    out = Path(folder)
    (out / 'driver').mkdir()
    (out / 'driver/i2c_master.h').write_text('#pragma once\ntypedef void *i2c_master_bus_handle_t;\n')
    (out / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n')
    c, binary = out / 'test.c', out / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + folder, '-I' + str(root / 'components/device_power/include'),
                    str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
