/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-INK4 Plus 的 read_pico_pmu 兼容层：
 * BQ27220 电量、CX25601N 充电状态、PCF8563 RTC、TCA9555 P17 电源键。
 *
 * read_pico_pmu compatibility layer for Metalio E-INK4 Plus:
 * BQ27220 gauge, CX25601N charger status, PCF8563 RTC and TCA9555 P17 key.
 * 没有 CW32 PMU，因此软睡/关机按新板电源路径实现或降级。
 * There is no CW32 PMU; soft sleep / power-off follow the new board's rails.
 */

#include "read_pico_pmu.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "fca9555.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define TAG "pmu"

#define BQ27220_ADDR 0x55
#define CX25601N_ADDR 0x6B
#define PCF8563_ADDR 0x51
#define I2C_TIMEOUT_MS 100

#define BQ_REG_VOLTAGE 0x08
#define BQ_REG_CURRENT 0x0C
#define CX_REG_STATUS1 0x1E

#define PCF_REG_SECONDS 0x02
#define PCF_BIT_VL 0x80

#define KEY_LONG_MS 1000
#define KEY_DEBOUNCE_MS 30

// TCA9555 P17 = Port1 bit7。 / TCA9555 P17 = Port1 bit7.
#define KEY_POWER_MASK 0x80
#define PWR_PULSE_COUNT 15
#define PWR_PULSE_INTERVAL_MS 100

// read_pico 组件在 final link 提供；不反向依赖，避免组件环。
// Provided by the read_pico component at final link; declared here to avoid a component cycle.
extern fca9555_handle_t read_pico_fca9555(void);
extern esp_err_t read_pico_pwr_pulse(bool high);

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_bq;
static i2c_master_dev_handle_t s_cx;
static i2c_master_dev_handle_t s_rtc;

static bool s_ready;
static pmu_snapshot_t s_snap;
static int s_vcom_mv = 1200;

static bool s_key_down;
static int64_t s_key_down_us;
static bool s_key_long_fired;
static bool s_key_short_pending;
static bool s_key_long_pending;

static uint8_t bcd_to_dec(uint8_t v) { return (uint8_t)(((v >> 4) * 10) + (v & 0x0F)); }
static uint8_t dec_to_bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static esp_err_t dev_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* data, size_t len) {
    if (dev == NULL || data == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = i2c_master_transmit(dev, &reg, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    return i2c_master_receive(dev, data, len, I2C_TIMEOUT_MS);
}

static esp_err_t dev_write(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t* data,
                           size_t len) {
    if (dev == NULL || data == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t buf[16];
    if (len + 1 > sizeof(buf)) return ESP_ERR_INVALID_ARG;
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(dev, buf, len + 1, I2C_TIMEOUT_MS);
}

static esp_err_t add_dev(uint8_t addr, i2c_master_dev_handle_t* out) {
    *out = NULL;
    if (i2c_master_probe(s_bus, addr, I2C_TIMEOUT_MS) != ESP_OK) return ESP_ERR_NOT_FOUND;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out);
}

static esp_err_t bq_read_voltage(uint16_t* mv) {
    uint8_t b[2] = {0};
    esp_err_t err = dev_read(s_bq, BQ_REG_VOLTAGE, b, sizeof(b));
    if (err != ESP_OK) return err;
    *mv = (uint16_t)(b[0] | (b[1] << 8));
    return ESP_OK;
}

static esp_err_t bq_read_current(int16_t* ma) {
    uint8_t b[2] = {0};
    esp_err_t err = dev_read(s_bq, BQ_REG_CURRENT, b, sizeof(b));
    if (err != ESP_OK) return err;
    *ma = (int16_t)(b[0] | (b[1] << 8));
    return ESP_OK;
}

static int voltage_to_soc(uint16_t mv) {
    const int empty = 3300;
    const int full = 4330;
    if (mv <= empty) return 0;
    if (mv >= full) return 1000;
    return (int)((int64_t)(mv - empty) * 1000 / (full - empty));
}

static esp_err_t rtc_read(struct tm* out, bool* valid) {
    uint8_t r[7] = {0};
    esp_err_t err = dev_read(s_rtc, PCF_REG_SECONDS, r, sizeof(r));
    if (err != ESP_OK) return err;
    *valid = (r[0] & PCF_BIT_VL) == 0;
    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd_to_dec(r[0] & 0x7F);
    out->tm_min = bcd_to_dec(r[1] & 0x7F);
    out->tm_hour = bcd_to_dec(r[2] & 0x3F);
    out->tm_mday = bcd_to_dec(r[3] & 0x3F);
    out->tm_wday = bcd_to_dec(r[4] & 0x07);
    out->tm_mon = bcd_to_dec(r[5] & 0x1F) - 1;
    out->tm_year = 100 + bcd_to_dec(r[6]);
    out->tm_isdst = 0;
    if (out->tm_mon < 0 || out->tm_mon > 11 || out->tm_mday < 1 || out->tm_mday > 31 ||
        out->tm_hour > 23 || out->tm_min > 59 || out->tm_sec > 59) {
        *valid = false;
    }
    return ESP_OK;
}

static esp_err_t rtc_write(time_t now) {
    struct tm tm;
    if (gmtime_r(&now, &tm) == NULL) return ESP_ERR_INVALID_ARG;
    const int year = tm.tm_year + 1900;
    if (year < 2000 || year > 2099) return ESP_ERR_INVALID_ARG;
    uint8_t r[7] = {
        dec_to_bcd((uint8_t)tm.tm_sec),
        dec_to_bcd((uint8_t)tm.tm_min),
        dec_to_bcd((uint8_t)tm.tm_hour),
        dec_to_bcd((uint8_t)tm.tm_mday),
        dec_to_bcd((uint8_t)tm.tm_wday),
        dec_to_bcd((uint8_t)(tm.tm_mon + 1)),
        dec_to_bcd((uint8_t)(year - 2000)),
    };
    return dev_write(s_rtc, PCF_REG_SECONDS, r, sizeof(r));
}

static void poll_key(void) {
    fca9555_handle_t ioe = read_pico_fca9555();
    if (ioe == NULL) return;
    uint8_t p1 = 0;
    if (fca9555_read_input(ioe, 1, &p1) != ESP_OK) return;

    const bool down = (p1 & KEY_POWER_MASK) == 0;
    const int64_t now = esp_timer_get_time();
    if (down && !s_key_down) {
        s_key_down = true;
        s_key_down_us = now;
        s_key_long_fired = false;
    } else if (down && s_key_down) {
        if (!s_key_long_fired && now - s_key_down_us >= (int64_t)KEY_LONG_MS * 1000) {
            s_key_long_fired = true;
            s_key_long_pending = true;
        }
    } else if (!down && s_key_down) {
        s_key_down = false;
        const int64_t held_us = now - s_key_down_us;
        if (!s_key_long_fired && held_us >= (int64_t)KEY_DEBOUNCE_MS * 1000) {
            s_key_short_pending = true;
        }
    }
}

static void poll_battery(void) {
    uint16_t mv = 0;
    int16_t ma = 0;
    const bool have_v = s_bq != NULL && bq_read_voltage(&mv) == ESP_OK && mv >= 2500 && mv <= 5000;
    const bool have_i = s_bq != NULL && bq_read_current(&ma) == ESP_OK;

    if (have_v) {
        s_snap.battery_mv = mv;
        s_snap.battery_raw = mv;
        s_snap.soc_permille = (uint16_t)voltage_to_soc(mv);
        s_snap.flags |= PMU_STATUS_BATTERY_VALID;
        s_snap.status_ok = true;
    } else {
        s_snap.flags &= ~PMU_STATUS_BATTERY_VALID;
    }

    uint8_t chg_stat = 0;
    bool have_chg = false;
    if (s_cx != NULL) {
        uint8_t st = 0;
        if (dev_read(s_cx, CX_REG_STATUS1, &st, 1) == ESP_OK) {
            chg_stat = (uint8_t)((st >> 3) & 0x03);
            have_chg = true;
        }
    }

    bool charging;
    if (have_chg) {
        charging = chg_stat != 0;
    } else if (have_i) {
        charging = ma > 20;
    } else {
        charging = false;
    }
    s_snap.charge_state = charging ? PMU_CHARGE_CHARGING : PMU_CHARGE_NOT_CHARGING;
    if (charging) {
        s_snap.flags |= PMU_STATUS_CHARGING_ACTIVE;
    } else {
        s_snap.flags &= ~PMU_STATUS_CHARGING_ACTIVE;
    }
}

esp_err_t read_pico_pmu_init(i2c_master_bus_handle_t bus_handle,
                             const read_pico_pmu_config_t* config) {
    (void)config;
    if (bus_handle == NULL) return ESP_ERR_INVALID_ARG;
    s_bus = bus_handle;
    memset(&s_snap, 0, sizeof(s_snap));

    (void)add_dev(BQ27220_ADDR, &s_bq);
    (void)add_dev(CX25601N_ADDR, &s_cx);
    (void)add_dev(PCF8563_ADDR, &s_rtc);

    s_snap.present = s_bq != NULL || s_cx != NULL || s_rtc != NULL;
    s_snap.identity_ok = s_snap.present;
    s_snap.config_ok = true;
    s_snap.time_ok = s_rtc != NULL;
    s_snap.alarm_ok = false;
    s_snap.power_state = PMU_PWR_RUNNING;
    s_snap.config.key_long_ms = KEY_LONG_MS;
    s_snap.config.key_debounce_ms = KEY_DEBOUNCE_MS;

    nvs_handle_t nvs;
    if (nvs_open("panel", NVS_READONLY, &nvs) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(nvs, "vcom", &v) == ESP_OK && v >= 500 && v <= 2500) {
            s_vcom_mv = v;
        }
        nvs_close(nvs);
    }

    s_ready = true;
    ESP_LOGI(TAG, "Metalio PMU shim ready (BQ=%d CX=%d RTC=%d VCOM=%dmV)", s_bq != NULL,
             s_cx != NULL, s_rtc != NULL, s_vcom_mv);
    return ESP_OK;
}

esp_err_t read_pico_pmu_deinit(void) {
    if (s_bq) i2c_master_bus_rm_device(s_bq);
    if (s_cx) i2c_master_bus_rm_device(s_cx);
    if (s_rtc) i2c_master_bus_rm_device(s_rtc);
    s_bq = s_cx = s_rtc = NULL;
    s_ready = false;
    return ESP_OK;
}

bool read_pico_pmu_ready(void) { return s_ready; }
const pmu_snapshot_t* read_pico_pmu_get(void) { return &s_snap; }

esp_err_t read_pico_pmu_poll(void) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    poll_key();
    poll_battery();
    s_snap.pending_events = (uint8_t)(s_key_short_pending || s_key_long_pending);
    return ESP_OK;
}

esp_err_t read_pico_pmu_refresh(void) { return read_pico_pmu_poll(); }

esp_err_t read_pico_pmu_cmd(uint16_t code, const uint8_t* payload, uint8_t plen) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (code == PMU_CMD_TIME_GET) {
        struct tm tm;
        bool valid = false;
        esp_err_t err = rtc_read(&tm, &valid);
        if (err != ESP_OK) return err;
        const time_t t = mktime(&tm);
        s_snap.unix_sec = (uint32_t)t;
        s_snap.unix_millis = 0;
        s_snap.time_synced = valid ? 1 : 0;
        s_snap.time_ok = valid;
        if (valid) {
            struct timeval tv = {.tv_sec = t, .tv_usec = 0};
            settimeofday(&tv, NULL);
        }
        return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (code == PMU_CMD_TIME_SYNC) {
        if (payload == NULL || plen < 4) return ESP_ERR_INVALID_ARG;
        const uint32_t sec = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                             ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
        return rtc_write((time_t)sec);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t read_pico_pmu_vcom_get(int* out_mv) {
    if (out_mv == NULL) return ESP_ERR_INVALID_ARG;
    *out_mv = s_vcom_mv;
    return ESP_OK;
}

esp_err_t read_pico_pmu_vcom_set(int mv) {
    if (mv < 500 || mv > 2500 || (mv % 10) != 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("panel", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(nvs, "vcom", mv);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) s_vcom_mv = mv;
    return err;
}

esp_err_t read_pico_pmu_uid_get(uint8_t out[PMU_CHIP_UID_LEN]) {
    (void)out;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t read_pico_pmu_event_ack(uint16_t event_id) {
    (void)event_id;
    return ESP_OK;
}

esp_err_t read_pico_pmu_action(uint8_t action, uint16_t delay_ms, uint16_t reason) {
    (void)delay_ms;
    (void)reason;
    switch (action) {
        case PMU_ACTION_HOST_NORMAL_RESTART:
        case PMU_ACTION_HOST_HARD_RESET:
            esp_restart();
            return ESP_OK;
        case PMU_ACTION_HOST_LOGICAL_OFF:
            return read_pico_pmu_power_off();
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

void read_pico_pmu_drain_events(void) {
    s_key_short_pending = false;
    s_key_long_pending = false;
    s_snap.pending_events = 0;
}

read_pico_pmu_key_action_t read_pico_pmu_take_key_action(void) {
    (void)read_pico_pmu_poll();
    read_pico_pmu_key_action_t action = READ_PICO_PMU_KEY_NONE;
    if (s_key_long_pending) {
        action = READ_PICO_PMU_KEY_LONG;
    } else if (s_key_short_pending) {
        action = READ_PICO_PMU_KEY_SHORT;
    }
    s_key_short_pending = false;
    s_key_long_pending = false;
    s_snap.pending_events = 0;
    return action;
}

bool read_pico_pmu_take_key_short(void) {
    (void)read_pico_pmu_poll();
    const bool got = s_key_short_pending;
    s_key_short_pending = false;
    s_snap.pending_events = 0;
    return got;
}

bool read_pico_pmu_take_key_wakeup(void) {
    (void)read_pico_pmu_poll();
    const bool got = s_key_short_pending || s_key_down;
    s_key_short_pending = false;
    s_snap.pending_events = 0;
    return got;
}

void read_pico_pmu_consume_key_press(void) {
    (void)read_pico_pmu_poll();
    s_key_short_pending = false;
    s_key_long_pending = false;
    // 键仍按着时连释放一起吞掉：标记长按已触发，松手不再生成短按。
    // Swallow the release while the key is still down: mark the hold fired so it yields no short press.
    if (s_key_down) s_key_long_fired = true;
    s_snap.pending_events = 0;
}

esp_err_t read_pico_pmu_report_sleep(void) {
    // 无 PMU EN 可拉；软睡由 main/sleep.c 的 esp_light_sleep 路径负责。
    // No PMU EN to drop; main/sleep.c handles light sleep directly.
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t read_pico_pmu_report_ready(void) {
    s_snap.power_state = PMU_PWR_RUNNING;
    return ESP_OK;
}

esp_err_t read_pico_pmu_power_off(void) {
    // P13 脉冲 15 次；成功后仍活着则进深睡。 / Pulse P13 15 times; deep-sleep if still alive.
    for (int i = 0; i < PWR_PULSE_COUNT; i++) {
        esp_err_t err = read_pico_pwr_pulse(true);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(PWR_PULSE_INTERVAL_MS));
        err = read_pico_pwr_pulse(false);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(PWR_PULSE_INTERVAL_MS));
    }
    ESP_LOGW(TAG, "power-off pulse sent; entering deep sleep");
    esp_deep_sleep_start();
    return ESP_OK;
}

/* 名称辅助沿用原 CW32 实现，页面与日志格式不变。 */
/* Keep the original CW32 name helpers so page and log formats stay unchanged. */

const char* pmu_power_name(uint8_t state) {
    switch (state) {
        case PMU_PWR_OFF: return "OFF";
        case PMU_PWR_POWERING_ON: return "PWR_ON";
        case PMU_PWR_BOOT_WAIT: return "BOOT_WAIT";
        case PMU_PWR_RUNNING: return "RUNNING";
        case PMU_PWR_SHUTDOWN_PENDING: return "SHUT_PEND";
        case PMU_PWR_RESETTING: return "RESET";
        case PMU_PWR_DOWNLOAD_MODE: return "DOWNLOAD";
        case PMU_PWR_FAULT: return "FAULT";
        case PMU_PWR_SOFT_SLEEP: return "SOFT_SLEEP";
        default: return "?";
    }
}

const char* pmu_charge_name(uint8_t state) {
    switch (state) {
        case PMU_CHARGE_UNKNOWN: return "UNK";
        case PMU_CHARGE_NOT_CHARGING: return "IDLE";
        case PMU_CHARGE_CHARGING: return "CHG";
        case PMU_CHARGE_FULL_INFERRED: return "FULL";
        case PMU_CHARGE_FAULT: return "FAULT";
        default: return "?";
    }
}

const char* pmu_event_name(uint8_t type) {
    switch (type) {
        case PMU_EVT_KEY_DOWN: return "KEY_DOWN";
        case PMU_EVT_KEY_UP: return "KEY_UP";
        case PMU_EVT_KEY_SHORT: return "KEY_SHORT";
        case PMU_EVT_KEY_LONG: return "KEY_LONG";
        case PMU_EVT_KEY_FORCE_OFF: return "KEY_FORCE";
        case PMU_EVT_CHARGE_STATE_CHANGED: return "CHARGE";
        case PMU_EVT_BATTERY_SAMPLE_READY: return "BAT_RDY";
        case PMU_EVT_BATTERY_LOW: return "BAT_LOW";
        case PMU_EVT_BATTERY_CRITICAL: return "BAT_CRIT";
        case PMU_EVT_BATTERY_SOC_LOW: return "SOC_LOW";
        case PMU_EVT_ALARM_FIRED: return "ALARM";
        case PMU_EVT_HOST_STARTED: return "HOST_START";
        case PMU_EVT_HOST_READY: return "HOST_RDY";
        case PMU_EVT_SHUTDOWN_REQUESTED: return "SHUT_REQ";
        case PMU_EVT_HOST_RESET_PERFORMED: return "HOST_RST";
        case PMU_EVT_COMMAND_COMPLETED: return "CMD_DONE";
        case PMU_EVT_CONFIG_RECOVERED: return "CFG_RCV";
        case PMU_EVT_CW_RESET: return "CW_RST";
        case PMU_EVT_OVERFLOW: return "OVERFLOW";
        default: return "EVT";
    }
}

const char* pmu_status_name(uint16_t status) {
    switch (status) {
        case PMU_STATUS_OK: return "OK";
        case PMU_STATUS_ACCEPTED: return "ACCEPTED";
        case PMU_STATUS_BUSY: return "BUSY";
        case PMU_STATUS_BAD_CRC: return "BAD_CRC";
        case PMU_STATUS_BAD_MAGIC: return "BAD_MAGIC";
        case PMU_STATUS_BAD_LENGTH: return "BAD_LEN";
        case PMU_STATUS_UNSUPPORTED_VERSION: return "BAD_VER";
        case PMU_STATUS_UNKNOWN_COMMAND: return "UNK_CMD";
        case PMU_STATUS_INVALID_ARGUMENT: return "BAD_ARG";
        case PMU_STATUS_SEQUENCE_CONFLICT: return "SEQ";
        case PMU_STATUS_STALE_SESSION: return "STALE";
        case PMU_STATUS_NOT_SUPPORTED: return "NOSUP";
        case PMU_STATUS_INVALID_STATE: return "BAD_ST";
        case PMU_STATUS_NOT_ARMED: return "NOARM";
        case PMU_STATUS_TOKEN_EXPIRED: return "EXPIRED";
        case PMU_STATUS_PERMISSION_DENIED: return "DENIED";
        default: return "ERR";
    }
}

const char* pmu_led_name(uint8_t color) {
    switch (color) {
        case PMU_LED_OFF: return "OFF";
        case PMU_LED_RED: return "RED";
        case PMU_LED_WHITE: return "WHT";
        case PMU_LED_RED_WHITE: return "BOTH";
        default: return "?";
    }
}

const char* pmu_action_name(uint8_t action) {
    switch (action) {
        case PMU_ACTION_HOST_LOGICAL_OFF: return "LOG_OFF";
        case PMU_ACTION_HOST_HARD_RESET: return "HRST";
        case PMU_ACTION_HOST_NORMAL_RESTART: return "RESTART";
        case PMU_ACTION_HOST_ENTER_DOWNLOAD_MODE: return "DL";
        case PMU_ACTION_HOST_EXIT_DOWNLOAD_MODE: return "EXIT_DL";
        case PMU_ACTION_HOST_POWER_ON: return "PWR_ON";
        case PMU_ACTION_HOST_SOFT_SLEEP: return "SLEEP";
        default: return "ACT";
    }
}
