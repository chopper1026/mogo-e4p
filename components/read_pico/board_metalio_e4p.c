/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-INK4 Plus（ESP32-S31）板级：I2C、TCA9555、TPS65185、EPD 总线与电源轨。
 *
 * Metalio E-INK4 Plus (ESP32-S31) board: I2C, TCA9555, TPS65185, EPD bus and rails.
 * 与 read_pico_board.c 提供同名 BSP API，main/ 不需要感知换板。
 * Provides the same BSP API as read_pico_board.c so main/ stays board-agnostic.
 */

#include "read_pico_board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_display.h"
#include "epdiy.h"
#include "fca9555.h"
#include "read_pico_epd_timing.h"

#define TAG "read_pico"

/* ------------------------------------------------------------------------- */
/* 引脚 / 扩展器位（Metalio V1.3/1.4）                                        */
/* Pinout / expander bits (Metalio V1.3/1.4)                                  */
/* ------------------------------------------------------------------------- */

#define EPD_PIN_D0 GPIO_NUM_8
#define EPD_PIN_D1 GPIO_NUM_9
#define EPD_PIN_D2 GPIO_NUM_10
#define EPD_PIN_D3 GPIO_NUM_11
#define EPD_PIN_D4 GPIO_NUM_12
#define EPD_PIN_D5 GPIO_NUM_13
#define EPD_PIN_D6 GPIO_NUM_14
#define EPD_PIN_D7 GPIO_NUM_15

#define EPD_PIN_XCL GPIO_NUM_16
#define EPD_PIN_XLE GPIO_NUM_17
#define EPD_PIN_XOE GPIO_NUM_18
#define EPD_PIN_XSTL GPIO_NUM_4
#define EPD_PIN_SPV GPIO_NUM_35
#define EPD_PIN_CKV GPIO_NUM_40
#define EPD_PIN_MODE GPIO_NUM_19
#define EPD_PIN_BORDER GPIO_NUM_37

#define BOARD_I2C_SDA GPIO_NUM_0
#define BOARD_I2C_SCL GPIO_NUM_1
#define BOARD_IOE_INT GPIO_NUM_2

#define BOARD_TCA_ADDR 0x20
#define BOARD_TPS_ADDR 0x68
#define BOARD_TPS_I2C_HZ 100000

/* TCA9555 位号：手册 P0x/P1x → 0..15。 */
/* TCA9555 bit numbers: P0x/P1x -> 0..15. */
#define IOE_BIT(io) ((uint8_t)(1u << ((unsigned)(io) & 7u)))
#define IOE_PORT(io) ((int)((unsigned)(io) >> 3))

#define IO_VCOM_CTRL 0  // P00
#define IO_PWRUP 1      // P01
#define IO_WAKEUP 2     // P02
#define IO_MOTOR 3      // P03
#define IO_PA_EN 4      // P04
#define IO_CAM_SCR 5    // P05
#define IO_PWR_GOOD 7   // P07
#define IO_VOL_UP 8     // P10
#define IO_VOL_DOWN 9   // P11
#define IO_TP_RST 10    // P12
#define IO_PWR_PULSE 11 // P13
#define IO_BT_PA_PWR 12 // P14
#define IO_RTC_INT 14   // P16
#define IO_POWER 15     // P17

#define P0_TPS_MASK (IOE_BIT(IO_VCOM_CTRL) | IOE_BIT(IO_PWRUP) | IOE_BIT(IO_WAKEUP))
#define P0_OUTPUT_MASK \
    (P0_TPS_MASK | IOE_BIT(IO_MOTOR) | IOE_BIT(IO_PA_EN) | IOE_BIT(IO_CAM_SCR))
#define P0_INPUT_MASK ((uint8_t)~P0_OUTPUT_MASK)

#define P1_RAIL_MASK (IOE_BIT(IO_TP_RST) | IOE_BIT(IO_BT_PA_PWR))
#define P1_OUTPUT_MASK (P1_RAIL_MASK | IOE_BIT(IO_PWR_PULSE))
#define P1_INPUT_MASK ((uint8_t)~P1_OUTPUT_MASK)

#define EPD_VCOM_MV 1200

/* TPS65185 寄存器（与 Metalio tps65185.h 一致）。 */
/* TPS65185 registers (same as Metalio's tps65185.h). */
#define TPS_REG_ENABLE 0x01
#define TPS_REG_VCOM1 0x03
#define TPS_REG_VCOM2 0x04
#define TPS_REG_UPSEQ0 0x09
#define TPS_REG_UPSEQ1 0x0A
#define TPS_REG_PG 0x0F
#define TPS_REG_REVID 0x10

/* ------------------------------------------------------------------------- */
/* 状态                                                                       */
/* ------------------------------------------------------------------------- */

static i2c_master_bus_handle_t s_i2c_bus;
static fca9555_handle_t s_ioe;
static i2c_master_dev_handle_t s_tps;

static bool s_ioe_ok;
static bool s_tps_ok;
static bool s_rails_on;

static bool s_pwrup;
static bool s_vcom_ctrl;
static bool s_wakeup;
static uint8_t s_p0_extra; // Motor / PA / CAM 锁存
static uint8_t s_p1_out = (uint8_t)(IOE_BIT(IO_TP_RST) | IOE_BIT(IO_BT_PA_PWR));
static int s_vcom_mv = EPD_VCOM_MV;

static lcd_bus_config_t s_lcd_bus = {
    .clock = EPD_PIN_XCL,
    .ckv = EPD_PIN_CKV,
    .leh = EPD_PIN_XLE,
    .start_pulse = EPD_PIN_XSTL,
    .stv = EPD_PIN_SPV,
    .data = {
        EPD_PIN_D0, EPD_PIN_D1, EPD_PIN_D2, EPD_PIN_D3,
        EPD_PIN_D4, EPD_PIN_D5, EPD_PIN_D6, EPD_PIN_D7,
        EPD_PIN_D7, EPD_PIN_D7, EPD_PIN_D7, EPD_PIN_D7,
        EPD_PIN_D7, EPD_PIN_D7, EPD_PIN_D7, EPD_PIN_D7,
    },
};

/* ------------------------------------------------------------------------- */
/* I2C / TCA9555                                                              */
/* ------------------------------------------------------------------------- */

static esp_err_t ioe_write(uint8_t reg, uint8_t value) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    switch (reg) {
        case 2: return fca9555_set_output(s_ioe, 0, value);
        case 3: return fca9555_set_output(s_ioe, 1, value);
        case 4: return fca9555_set_inversion(s_ioe, 0, value);
        case 5: return fca9555_set_inversion(s_ioe, 1, value);
        case 6: return fca9555_set_config(s_ioe, 0, value);
        case 7: return fca9555_set_config(s_ioe, 1, value);
        default: return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t ioe_read(uint8_t reg, uint8_t* value) {
    if (!s_ioe_ok || value == NULL) return ESP_ERR_INVALID_STATE;
    return fca9555_read_reg(s_ioe, reg, value);
}

static uint8_t p0_value(void) {
    uint8_t v = s_p0_extra;
    if (s_pwrup) v |= IOE_BIT(IO_PWRUP);
    if (s_vcom_ctrl) v |= IOE_BIT(IO_VCOM_CTRL);
    if (s_wakeup) v |= IOE_BIT(IO_WAKEUP);
    return v;
}

static void p0_write(void) {
    esp_err_t err = ioe_write(2, p0_value()); // OUT0
    if (err != ESP_OK) ESP_LOGW(TAG, "TCA OUT0 write: %s", esp_err_to_name(err));
}

static void p1_write(void) {
    esp_err_t err = ioe_write(3, s_p1_out); // OUT1
    if (err != ESP_OK) ESP_LOGW(TAG, "TCA OUT1 write: %s", esp_err_to_name(err));
}

static esp_err_t ioe_init(void) {
    fca9555_config_t cfg = FCA9555_CONFIG_DEFAULT();
    cfg.i2c_addr = BOARD_TCA_ADDR;
    cfg.scl_speed_hz = 100000;
    cfg.int_gpio = BOARD_IOE_INT;
    if (fca9555_init(s_i2c_bus, &cfg, &s_ioe) != ESP_OK) return ESP_FAIL;

    s_ioe_ok = true;
    s_p0_extra = IOE_BIT(IO_CAM_SCR);
    s_pwrup = false;
    s_vcom_ctrl = false;
    s_wakeup = false;

    esp_err_t err = ioe_write(6, P0_INPUT_MASK); // CFG0
    if (err == ESP_OK) err = ioe_write(7, P1_INPUT_MASK); // CFG1
    if (err == ESP_OK) err = ioe_write(4, 0x00); // POL0
    if (err == ESP_OK) err = ioe_write(5, 0x00); // POL1
    if (err == ESP_OK) err = ioe_write(3, s_p1_out); // OUT1
    if (err == ESP_OK) p0_write();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9555 init: %s", esp_err_to_name(err));
        s_ioe_ok = false;
        return err;
    }

    // 读一遍输入松开 INT#。 / Read inputs to release INT#.
    (void)fca9555_clear_int(s_ioe);
    ESP_LOGI(TAG, "TCA9555 ready at 0x%02X", BOARD_TCA_ADDR);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* TPS65185                                                                   */
/* ------------------------------------------------------------------------- */

static esp_err_t tps_write(uint8_t reg, uint8_t value) {
    if (s_tps == NULL) return ESP_ERR_INVALID_STATE;
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_tps, buf, sizeof(buf), 50);
}

static esp_err_t tps_read(uint8_t reg, uint8_t* value) {
    if (s_tps == NULL || value == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_tps, &reg, 1, value, 1, 50);
}

static esp_err_t tps_probe(void) {
    if (s_i2c_bus == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_probe(s_i2c_bus, BOARD_TPS_ADDR, 50);
}

static esp_err_t tps_init(void) {
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_TPS_ADDR,
        .scl_speed_hz = BOARD_TPS_I2C_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &cfg, &s_tps);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TPS device add: %s", esp_err_to_name(err));
        s_tps = NULL;
        return err;
    }
    if (tps_probe() != ESP_OK) {
        ESP_LOGW(TAG, "TPS65185 not responding at 0x%02X", BOARD_TPS_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static void tps_set_vcom_mv(int mv) {
    if (mv < 0) mv = -mv;
    if (mv > 5000) mv = 5000;
    s_vcom_mv = mv;
    // datasheet: VCOM = -(VCOM1 + VCOM2 * 2) * 10 mV
    // 取 VCOM1 = (mv/10) & 0xFF，VCOM2 = (mv/10) >> 8。
    const int steps = mv / 10;
    (void)tps_write(TPS_REG_VCOM1, (uint8_t)(steps & 0xFF));
    (void)tps_write(TPS_REG_VCOM2, (uint8_t)((steps >> 8) & 0x01));
}

/* ------------------------------------------------------------------------- */
/* EPD 控制 GPIO                                                              */
/* ------------------------------------------------------------------------- */

static void epd_ctrl_gpios_low(void) {
    const gpio_num_t pins[] = {EPD_PIN_XOE, EPD_PIN_MODE, EPD_PIN_BORDER};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pins[i], 0);
    }
}

static void lcd_init(void) {
    const EpdDisplay_t* display = epd_get_display();
    const int init_pclk_mhz = display->bus_speed; // 8-bit：bus_speed 已是 MHz / bus_speed is MHz at 8-bit
    read_pico_epd_scan_t scan;
    read_pico_epd_scan(init_pclk_mhz, READ_PICO_EPD_SCAN_FULL, &scan);
    read_pico_epd_init_lcd(&s_lcd_bus, display->bus_width, display->width, display->height, &scan);
}

/* ------------------------------------------------------------------------- */
/* EpdBoardDefinition                                                         */
/* ------------------------------------------------------------------------- */

static void board_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
    if (mask->ep_output_enable) {
        gpio_set_level(EPD_PIN_XOE, state->ep_output_enable ? 1 : 0);
    }
    if (mask->ep_mode) {
        gpio_set_level(EPD_PIN_MODE, state->ep_mode ? 1 : 0);
    }
    if (mask->ep_output_enable || mask->ep_mode || mask->ep_stv) {
        p0_write();
    }
}

static void board_init(uint32_t epd_row_width) {
    (void)epd_row_width;

    // 深睡可能锁住并行总线，先释放。 / Release GPIO holds left by deep sleep.
    const gpio_num_t bus_pins[] = {
        EPD_PIN_D0, EPD_PIN_D1, EPD_PIN_D2, EPD_PIN_D3,
        EPD_PIN_D4, EPD_PIN_D5, EPD_PIN_D6, EPD_PIN_D7,
        EPD_PIN_XCL, EPD_PIN_XLE, EPD_PIN_XOE, EPD_PIN_XSTL,
        EPD_PIN_SPV, EPD_PIN_CKV, EPD_PIN_MODE, EPD_PIN_BORDER,
    };
    for (size_t i = 0; i < sizeof(bus_pins) / sizeof(bus_pins[0]); i++) {
        gpio_hold_dis(bus_pins[i]);
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    if (ioe_init() != ESP_OK) {
        ESP_LOGE(TAG, "no TCA9555; panel power unavailable");
    }
    vTaskDelay(pdMS_TO_TICKS(80));
    (void)tps_init();

    epd_ctrl_gpios_low();
    lcd_init();
    ESP_LOGI(TAG, "Metalio E-INK4 Plus bus initialized (8-bit, pclk %d MHz)",
             epd_get_display()->bus_speed);
}

static void board_deinit(void) {
    epd_lcd_deinit();
    if (s_tps) {
        i2c_master_bus_rm_device(s_tps);
        s_tps = NULL;
    }
    if (s_ioe) {
        fca9555_deinit(s_ioe);
        s_ioe = NULL;
        s_ioe_ok = false;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}

static void board_poweron(epd_ctrl_state_t* state) {
    if (s_rails_on) return;

    state->ep_output_enable = false;
    state->ep_stv = true;
    state->ep_mode = false;
    s_pwrup = false;
    s_vcom_ctrl = false;
    s_wakeup = true;
    p0_write();
    vTaskDelay(pdMS_TO_TICKS(50));

    if (s_tps != NULL) {
        esp_err_t probe = ESP_FAIL;
        for (int i = 0; i < 3 && probe != ESP_OK; i++) {
            probe = tps_probe();
            if (probe != ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
        }
        uint8_t revid = 0;
        (void)tps_read(TPS_REG_REVID, &revid);
        (void)tps_write(TPS_REG_UPSEQ0, 0xE4);
        (void)tps_write(TPS_REG_UPSEQ1, 0x55);
        ESP_LOGI(TAG, "TPS REVID=0x%02X probe=%s", revid, esp_err_to_name(probe));
    }

    s_pwrup = true;
    s_vcom_ctrl = true;
    p0_write();
    vTaskDelay(pdMS_TO_TICKS(20));

    bool pgood = false;
    for (int i = 0; i < 600 && !pgood; i++) {
        uint8_t in0 = 0;
        if (ioe_read(0, &in0) == ESP_OK && (in0 & IOE_BIT(IO_PWR_GOOD))) {
            pgood = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (!pgood) {
        ESP_LOGE(TAG, "TPS PWR_GOOD timeout");
        s_tps_ok = false;
        return;
    }

    if (s_tps != NULL) {
        (void)tps_write(TPS_REG_ENABLE, 0x3F);
        tps_set_vcom_mv(s_vcom_mv);
        for (int i = 0; i < 200; i++) {
            uint8_t pg = 0;
            if (tps_read(TPS_REG_PG, &pg) == ESP_OK && (pg & 0xFA) == 0xFA) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    state->ep_output_enable = true;
    epd_ctrl_state_t out_mask = {.ep_output_enable = true};
    board_set_ctrl(state, &out_mask);
    s_rails_on = true;
    s_tps_ok = true;
    ESP_LOGI(TAG, "EPD rails on, VCOM=-%d mV", s_vcom_mv);
}

static void board_poweroff(epd_ctrl_state_t* state) {
    epd_ctrl_state_t mask = {
        .ep_stv = true,
        .ep_output_enable = true,
        .ep_mode = true,
    };
    // 只有真正关电才撤 VCOM；release_drive 不碰它。
    // VCOM drops only on real power-off; release_drive never touches it.
    s_vcom_ctrl = false;
    s_pwrup = false;
    state->ep_stv = false;
    state->ep_output_enable = false;
    state->ep_mode = false;
    board_set_ctrl(state, &mask);
    vTaskDelay(pdMS_TO_TICKS(2));
    s_wakeup = false;
    p0_write();
    s_rails_on = false;
    s_tps_ok = false;
}

static void board_measure_vcom(epd_ctrl_state_t* state) { (void)state; }

static float board_temperature(void) { return 25.0f; }

static void board_set_vcom(int value) { tps_set_vcom_mv(value); }

const EpdBoardDefinition epd_board_read_pico = {
    .init = board_init,
    .deinit = board_deinit,
    .set_ctrl = board_set_ctrl,
    .poweron = board_poweron,
    .poweroff = board_poweroff,
    .measure_vcom = board_measure_vcom,
    .get_temperature = board_temperature,
    .set_vcom = board_set_vcom,
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};

/* ------------------------------------------------------------------------- */
/* BSP API（与 read_pico_board.c 同名）                                        */
/* ------------------------------------------------------------------------- */

i2c_master_bus_handle_t read_pico_i2c_bus(void) { return s_i2c_bus; }

sy7636a_handle_t read_pico_sy7636a(void) { return NULL; }

fca9555_handle_t read_pico_fca9555(void) { return s_ioe; }

static read_pico_i2c_census_t s_census;

void read_pico_i2c_census_take(void) {
    static const struct {
        uint8_t addr;
        const char* name;
    } map[READ_PICO_I2C_DEV_N] = {
        {BOARD_TCA_ADDR, "TCA9555"},
        {BOARD_TPS_ADDR, "TPS65185"},
        {0x38, "FT6336U"},
        {0x19, "SC7A20H"},
        {0x55, "BQ27220"},
    };

    s_census = (read_pico_i2c_census_t){0};
    s_census.all_online = true;
    for (int i = 0; i < READ_PICO_I2C_DEV_N; i++) {
        s_census.dev[i].name = map[i].name;
        s_census.dev[i].addr = map[i].addr;
        s_census.dev[i].online =
            s_i2c_bus != NULL && i2c_master_probe(s_i2c_bus, map[i].addr, 50) == ESP_OK;
        if (!s_census.dev[i].online) {
            s_census.offline_n++;
            s_census.all_online = false;
        }
    }
}

const read_pico_i2c_census_t* read_pico_i2c_census(void) { return &s_census; }

int read_pico_ioe_int_level(void) { return gpio_get_level(BOARD_IOE_INT) ? 1 : 0; }

static esp_err_t fill_ioe_status(read_pico_status_t* status, bool refresh_cfg) {
    (void)refresh_cfg;
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;

    fca9555_map_t map = {0};
    esp_err_t err = fca9555_read_all(s_ioe, &map);
    if (err != ESP_OK) return err;
    status->ioe_input = map.input;
    status->ioe_output = map.output;
    status->ioe_invert = map.invert;
    status->ioe_config = map.config;
    status->ioe_int_level = read_pico_ioe_int_level();
    status->rails_on = s_rails_on;
    return ESP_OK;
}

esp_err_t read_pico_get_ioe_status(read_pico_status_t* status) {
    return fill_ioe_status(status, false);
}

esp_err_t read_pico_get_ioe_status_full(read_pico_status_t* status) {
    return fill_ioe_status(status, true);
}

esp_err_t read_pico_get_status(read_pico_status_t* status) {
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    (void)fill_ioe_status(status, false);

    status->rails_on = s_rails_on;
    status->sy_live = s_tps_ok;
    status->sy.on = s_tps_ok;
    status->sy.vcom_manual = true;
    status->sy.vcom_mv = s_vcom_mv;
    status->sy.vldo = SY7636A_VLDO_1500;
    status->sy.temperature_c = 25;
    if (s_ioe_ok) {
        uint8_t in0 = 0;
        if (ioe_read(0, &in0) == ESP_OK) status->sy.pg = (in0 & IOE_BIT(IO_PWR_GOOD)) != 0;
    }
    return ESP_OK;
}

esp_err_t read_pico_toggle_vcomctl(void) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    s_vcom_ctrl = !s_vcom_ctrl;
    p0_write();
    return ESP_OK;
}

bool read_pico_rails_on(void) { return s_rails_on; }

bool read_pico_epd_release_drive(void) {
    if (!s_rails_on) return true;
    epd_ctrl_state_t* state = epd_ctrl_state();
    state->ep_mode = false;
    state->ep_output_enable = false;
    epd_ctrl_state_t mask = {.ep_mode = true, .ep_output_enable = true};
    board_set_ctrl(state, &mask);
    // VCOM_CTRL 保持到板级关电；每次推屏后拉低会让同一 burst 内后续扫描失去公共电压。
    // Keep VCOM_CTRL until board power-off; dropping it after each output leaves later scans in the same burst without a common voltage.
    return true;
}

esp_err_t read_pico_sd_detect(bool* present) {
    if (present == NULL) return ESP_ERR_INVALID_ARG;
    // Metalio 没有卡座 CD 脚；挂载层负责实际探测。 / No card-detect pin on Metalio.
    *present = true;
    return ESP_OK;
}

bool read_pico_sd_present(void) { return true; }

void read_pico_clear_ioe_int(void) {
    if (!s_ioe_ok) return;
    (void)fca9555_clear_int(s_ioe);
}

esp_err_t read_pico_ioe_sleep(bool host_off) {
    (void)host_off;
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    s_pwrup = false;
    s_vcom_ctrl = false;
    s_wakeup = false;
    p0_write();
    // 按键脚改输入、空闲保持高；关机脉冲回低。 / Keys input, pulse low.
    s_p1_out &= (uint8_t)~IOE_BIT(IO_PWR_PULSE);
    p1_write();
    // 只把按键脚改输入；P12/P14 仍保持输出高，浅睡期间触摸/功放不断电。
    // Only keys become inputs; keep P12/P14 driven high so touch/PA stay powered in light sleep.
    (void)ioe_write(7, P1_INPUT_MASK);
    (void)fca9555_clear_int(s_ioe);
    return ESP_OK;
}

esp_err_t read_pico_ioe_wake(void) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ioe_write(6, P0_INPUT_MASK);
    if (err == ESP_OK) err = ioe_write(7, P1_INPUT_MASK);
    if (err == ESP_OK) p0_write();
    if (err == ESP_OK) {
        s_p1_out |= (uint8_t)(IOE_BIT(IO_TP_RST) | IOE_BIT(IO_BT_PA_PWR));
        p1_write();
    }
    (void)fca9555_clear_int(s_ioe);
    return err;
}

esp_err_t read_pico_tp_rst(bool high) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    if (high) {
        s_p1_out |= IOE_BIT(IO_TP_RST);
    } else {
        s_p1_out &= (uint8_t)~IOE_BIT(IO_TP_RST);
    }
    p1_write();
    return ESP_OK;
}

esp_err_t read_pico_touch_reset(void) {
    esp_err_t err = read_pico_tp_rst(false);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    err = read_pico_tp_rst(true);
    vTaskDelay(pdMS_TO_TICKS(300));
    return err;
}

esp_err_t read_pico_pwr_pulse(bool high) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    if (high) {
        s_p1_out |= IOE_BIT(IO_PWR_PULSE);
    } else {
        s_p1_out &= (uint8_t)~IOE_BIT(IO_PWR_PULSE);
    }
    p1_write();
    return ESP_OK;
}
