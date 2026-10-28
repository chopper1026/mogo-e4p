#!/usr/bin/env python3
"""中文：执行实际 S31 后端，验证显示、电源与睡眠的资源边界。
English: Exercise the actual S31 backend across display, power and sleep boundaries.
"""
from pathlib import Path
import re
import subprocess
import tempfile

from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "components/read_pico/board_metalio_e4p.c"
source = BOARD.read_text()
# 保留实际引脚和寄存器定义，合并多行宏后提取。/ Keep real pins/registers and join continued macros before extraction.
defines = "\n".join(line for line in source.replace("\\\n", "").splitlines()
                    if re.match(r"#define (?:IOE_|IO_|P0_|P1_|TPS_REG_|BOARD_TCA_ADDR\b|BOARD_IOE_INT\b|EPD_PIN_XOE\b|EPD_PIN_MODE\b)", line))
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef struct { bool ep_mode, ep_output_enable, ep_stv; } epd_ctrl_state_t;
typedef struct { int i2c_addr, scl_speed_hz, int_gpio; } fca9555_config_t;
#define FCA9555_CONFIG_DEFAULT() ((fca9555_config_t){0})
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define TAG "test"
#define GPIO_NUM_18 18
#define GPIO_NUM_19 19
#define GPIO_NUM_2 2
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static void *s_ioe = (void *)1, *s_tps = (void *)2;
static void *s_i2c_bus = (void *)3;
static bool s_ioe_ok, s_tps_ok, s_rails_on, s_pwrup, s_vcom_ctrl, s_wakeup;
static uint8_t s_p0_extra, s_p1_out, output[2], config[2], inversion[2];
static int s_vcom_mv = 1200;
static unsigned writes, reads, clears, waits, probes, tps_writes;
static int fail_config = -1;
static bool failed_output, pgood, failed_probe;
static int init_fault_reg = -1, init_failures;
static unsigned init_allocations, bus_resets;
static bool allocation_failure;
static bool init_write_fails(int reg) {
    if (reg != init_fault_reg || !init_failures) return false;
    --init_failures; return true;
}
static int fca9555_init(void *bus, const fca9555_config_t *cfg, void **out) {
    assert(bus == s_i2c_bus && !*out && cfg->i2c_addr == 0x20 && cfg->scl_speed_hz == 100000 && cfg->int_gpio == 2);
    ++init_allocations; if (allocation_failure) return ESP_FAIL;
    *out = (void *)1; return ESP_OK;
}
static int i2c_master_bus_reset(void *bus) {
    assert(bus == s_i2c_bus && !s_ioe_ok); ++bus_resets; return ESP_OK;
}
static int pins[64];
static epd_ctrl_state_t state;
static epd_ctrl_state_t *epd_ctrl_state(void) { return &state; }
static int fca9555_set_output(void *h, int port, uint8_t value) {
    assert(h == s_ioe && port >= 0 && port < 2); ++writes;
    if (failed_output || init_write_fails(2 + port)) return ESP_FAIL;
    output[port] = value; return ESP_OK;
}
static int fca9555_set_config(void *h, int port, uint8_t value) {
    assert(h == s_ioe && port >= 0 && port < 2);
    if (port == fail_config || init_write_fails(6 + port)) return ESP_FAIL;
    config[port] = value; return ESP_OK;
}
static int fca9555_set_inversion(void *h, int port, uint8_t value) {
    assert(h == s_ioe && port >= 0 && port < 2);
    if (init_write_fails(4 + port)) return ESP_FAIL;
    inversion[port] = value; return ESP_OK;
}
static int fca9555_read_reg(void *h, uint8_t reg, uint8_t *value) {
    assert(h == s_ioe && reg == 0 && value); ++reads;
    *value = pgood ? 0x80 : 0; return ESP_OK;
}
static int fca9555_clear_int(void *h) { assert(h == s_ioe); ++clears; return ESP_OK; }
static void gpio_set_level(int pin, int level) { assert(pin >= 0 && pin < 64); pins[pin] = level; }
static void vTaskDelay(unsigned ms) { waits += ms; }
static int tps_probe(void) { ++probes; return failed_probe ? ESP_FAIL : ESP_OK; }
static int tps_read(uint8_t reg, uint8_t *value) { (void)reg; *value = 0xfa; return ESP_OK; }
static int tps_write(uint8_t reg, uint8_t value) { (void)reg; (void)value; ++tps_writes; return ESP_OK; }
static void tps_set_vcom_mv(int mv) { assert(mv == s_vcom_mv); }
'''
unit += defines + "\n"
for name in ("ioe_write", "ioe_read", "p0_value", "p0_write", "p1_write", "ioe_init",
             "board_set_ctrl", "board_poweron", "board_poweroff",
             "read_pico_epd_release_drive", "read_pico_sd_detect",
             "read_pico_sd_present", "read_pico_sd_has_cd", "read_pico_ioe_sleep",
             "read_pico_ioe_wake", "read_pico_tp_rst", "read_pico_touch_reset",
             "read_pico_pwr_pulse", "read_pico_has_motor", "read_pico_motor"):
    unit += function(name, BOARD) + "\n"
unit += r'''
static void reset(void) {
    s_ioe_ok = true; s_tps_ok = s_rails_on = false;
    s_pwrup = s_vcom_ctrl = s_wakeup = false;
    s_p0_extra = IOE_BIT(IO_CAM_SCR) | IOE_BIT(IO_PA_EN);
    s_p1_out = P1_RAIL_MASK;
    memset(output, 0, sizeof(output)); memset(config, 0, sizeof(config));
    memset(inversion, 0, sizeof(inversion)); memset(pins, 0, sizeof(pins));
    writes = reads = clears = waits = probes = tps_writes = 0;
    fail_config = -1; failed_output = failed_probe = false; pgood = true;
    init_fault_reg = -1; init_failures = 0;
    init_allocations = bus_resets = 0; allocation_failure = false;
    s_ioe = (void *)1;
    state = (epd_ctrl_state_t){0};
}
int main(void) {
    (void)i2c_master_bus_reset;
    // 每个配置寄存器及最后的 OUT0 都必须成功；暂时故障重试但只分配一个句柄。
    // Every configuration register and final OUT0 must succeed; transient retries allocate one handle only.
    for (int reg = 2; reg <= 7; ++reg) for (int failures = 0; failures <= 3; ++failures) {
        reset(); s_ioe = NULL; s_ioe_ok = false;
        init_fault_reg = reg; init_failures = failures;
        int result = ioe_init();
        assert(init_allocations == 1 && bus_resets == (unsigned)(failures < 3 ? failures : 2));
        assert(waits == bus_resets * 10);
        if (failures < 3) {
            assert(result == ESP_OK && s_ioe_ok && clears == 1);
            assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
            assert(!inversion[0] && !inversion[1]);
            assert(output[0] == IOE_BIT(IO_CAM_SCR) && output[1] == P1_RAIL_MASK);
            assert(!s_pwrup && !s_vcom_ctrl && !s_wakeup);
        } else {
            assert(result == ESP_FAIL && !s_ioe_ok && !clears);
            // 持续故障后仍保留单一所有权，下次初始化可恢复且不重复分配。
            // Retain one owned handle after persistent failure; a later initialization recovers without reallocating.
            assert(ioe_init() == ESP_OK && s_ioe_ok && init_allocations == 1);
        }
    }
    reset(); s_ioe = NULL; s_ioe_ok = false; allocation_failure = true;
    assert(ioe_init() == ESP_FAIL && !s_ioe_ok && !s_ioe && init_allocations == 1 && !bus_resets);
    reset(); assert(read_pico_epd_release_drive() && !writes && !tps_writes);
    board_poweron(&state);
    assert(s_rails_on && s_tps_ok && s_pwrup && s_vcom_ctrl && s_wakeup);
    assert((output[0] & P0_TPS_MASK) == P0_TPS_MASK && pins[EPD_PIN_XOE]);
    assert(probes == 1 && waits == 70);
    unsigned before = writes, tps_before = tps_writes;
    board_poweron(&state); assert(writes == before && tps_writes == tps_before);
    // S31 扫描收尾只撤 MODE/XOE，VCOM 与其他锁存保持到真正关电。
    // S31 output completion clears MODE/XOE while VCOM and other latches remain until power-off.
    state.ep_mode = true;
    board_set_ctrl(&state, &(epd_ctrl_state_t){.ep_mode = true});
    uint8_t keep = output[0];
    for (int i = 0; i < 100; ++i) {
        assert(read_pico_epd_release_drive());
        assert(!state.ep_mode && !state.ep_output_enable);
        assert(!pins[EPD_PIN_MODE] && !pins[EPD_PIN_XOE]);
        assert(s_rails_on && s_vcom_ctrl && output[0] == keep && tps_writes == tps_before);
    }
    state.ep_mode = state.ep_output_enable = true;
    board_set_ctrl(&state, &(epd_ctrl_state_t){.ep_mode = true});
    assert(pins[EPD_PIN_MODE] && !pins[EPD_PIN_XOE]);
    board_poweroff(&state);
    assert(!s_rails_on && !s_tps_ok && !(output[0] & P0_TPS_MASK));
    assert((output[0] & P0_OUTPUT_MASK) == s_p0_extra && !pins[EPD_PIN_XOE] && !pins[EPD_PIN_MODE]);
    // PGOOD 失效的等待有上界；不上报轨已就绪，且关电仍能撤掉请求。
    // Bound failed PGOOD waits; never publish ready rails, and power-off still clears requests.
    reset(); pgood = false; failed_probe = true; board_poweron(&state);
    assert(!s_rails_on && !s_tps_ok && probes == 3 && reads == 600 && waits == 700);
    board_poweroff(&state); assert(!(output[0] & P0_TPS_MASK));
    for (int host_off = 0; host_off < 2; ++host_off) {
        reset(); board_poweron(&state); assert(read_pico_motor(true) == ESP_OK);
        s_p1_out |= IOE_BIT(IO_PWR_PULSE);
        assert(read_pico_ioe_sleep(host_off) == ESP_OK);
        assert(!(output[0] & (P0_TPS_MASK | IOE_BIT(IO_MOTOR))));
        assert((output[0] & (IOE_BIT(IO_PA_EN) | IOE_BIT(IO_CAM_SCR))) == s_p0_extra);
        assert(!(output[1] & IOE_BIT(IO_PWR_PULSE)) && (output[1] & P1_RAIL_MASK) == P1_RAIL_MASK);
        assert(config[1] == P1_INPUT_MASK);
        assert(read_pico_ioe_wake() == ESP_OK);
        assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
        assert((output[1] & P1_RAIL_MASK) == P1_RAIL_MASK && clears == 2);
    }
    // 缺失扩展器与方向写入失败不假报唤醒成功。
    // Missing expanders and failed direction writes must not report a successful wake.
    reset(); s_ioe_ok = false;
    assert(read_pico_ioe_sleep(false) == ESP_ERR_INVALID_STATE);
    assert(read_pico_ioe_wake() == ESP_ERR_INVALID_STATE);
    assert(read_pico_motor(true) == ESP_ERR_INVALID_STATE && !writes);
    for (int port = 0; port < 2; ++port) {
        reset(); fail_config = port; assert(read_pico_ioe_wake() == ESP_FAIL);
        fail_config = -1; assert(read_pico_ioe_wake() == ESP_OK);
    }
    reset(); board_poweron(&state); keep = output[0];
    assert(read_pico_has_motor() && read_pico_motor(true) == ESP_OK);
    assert(output[0] == (uint8_t)(keep | IOE_BIT(IO_MOTOR)));
    assert(read_pico_motor(false) == ESP_OK && output[0] == keep);
    failed_output = true; assert(read_pico_motor(true) == ESP_FAIL); failed_output = false;
    unsigned delay_before = waits;
    assert(read_pico_touch_reset() == ESP_OK && waits - delay_before == 310);
    assert((output[1] & P1_RAIL_MASK) == P1_RAIL_MASK);
    assert(read_pico_pwr_pulse(true) == ESP_OK && (output[1] & IOE_BIT(IO_PWR_PULSE)));
    assert(read_pico_pwr_pulse(false) == ESP_OK && !(output[1] & IOE_BIT(IO_PWR_PULSE)));
    // S31 没有 CD，存在接口不代表物理卡在线；存储层另测 CMD13 拔卡。
    // S31 has no CD; presence hooks do not prove physical media. Storage tests cover CMD13 removal.
    bool present = false;
    assert(read_pico_sd_detect(NULL) == ESP_ERR_INVALID_ARG);
    assert(read_pico_sd_detect(&present) == ESP_OK && present);
    assert(read_pico_sd_present() && !read_pico_sd_has_cd());
    puts("PASS: actual S31 VCOM retention, masked GPIO, power timeout, motor isolation, sleep/wake rails, touch reset, pulse and no-CD contract");
}
'''
with tempfile.TemporaryDirectory(prefix="s31-board-") as folder:
    c, binary = Path(folder) / "test.c", Path(folder) / "test"
    c.write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
