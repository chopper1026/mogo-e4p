/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-INK4 Plus 的 read_pico_pmu 兼容层：
 * BQ27220 电量、CX25601N 充电状态、PCF8563 RTC、TCA9555 P17 电源键。
 * 电量在电压/电流之上滤波：负载补偿、EMA、分段曲线、单向限速，并持久化到 NVS。
 *
 * read_pico_pmu compatibility layer for Metalio E-INK4 Plus:
 * BQ27220 gauge, CX25601N charger status, PCF8563 RTC and TCA9555 P17 key.
 * The percent is filtered on top of voltage/current: load compensation, EMA, a piecewise
 * curve and one-direction rate limiting, persisted to NVS.
 * 没有 CW32 PMU，因此软睡/关机按新板电源路径实现或降级。
 * There is no CW32 PMU; soft sleep / power-off follow the new board's rails.
 */

#include "read_pico_pmu.h"

#include <string.h>
#include <stdio.h>
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
// StateOfCharge() 寄存器，地址按 TRM 复核。 / StateOfCharge() register; re-check the address against the TRM.
#define BQ_REG_SOC 0x2C
#define CX_REG_ICHG_LO 0x02
#define CX_REG_ICHG_HI 0x03
#define CX_REG_VREG_LO 0x04
#define CX_REG_VREG_HI 0x05
#define CX_REG_IINDPM_LO 0x06
#define CX_REG_IINDPM_HI 0x07
#define CX_REG_IPRECHG_LO 0x10
#define CX_REG_IPRECHG_HI 0x11
#define CX_REG_ITERM_LO 0x12
#define CX_REG_ITERM_HI 0x13
#define CX_REG_CHG_CTRL0 0x14
#define CX_REG_CHG_TMR 0x15
#define CX_REG_CHG_CTRL1 0x16
#define CX_REG_CHG_CTRL3 0x18
#define CX_REG_STATUS1 0x1E
#define CX_REG_PART_INFO 0x38
#define CX_REG_UNLOCK 0x70
// STATUS1 bits[4:3]：0 未充电/已满、1 涓流/预充/CC、2 恒压降流、3 top-off。
// 编码取自原厂驱动 cx25601n.h（CX25601N_CHG_STAT_*）。
// STATUS1 bits[4:3]: 0 not charging/full, 1 trickle/precharge/CC, 2 CV taper, 3 top-off.
// The encoding comes from the vendor driver's cx25601n.h (CX25601N_CHG_STAT_*).
#define CX_CHG_STAT_CC 1
#define CX_CHG_STAT_CV 2
#define CX_CHG_STAT_TOPOFF 3
// 原厂板级默认快速充电 1000 mA（MetalioPower_Init 的 kDefaultIchgMa），输入限流同值，
// 充电截止 4.4 V。不写这些寄存器时芯片停在上次遗留值，换电池/掉电后可能回落到默认档。
// Vendor board default fast charge is 1000 mA (MetalioPower_Init's kDefaultIchgMa), the
// input limit follows it and VREG is 4.4 V. Without these writes the chip keeps whatever
// an earlier firmware left behind and can fall back to its power-on defaults.
#define CX_CHG_ICHG_MA 1000
#define CX_CHG_VREG_MV 4400

#define PCF_REG_SECONDS 0x02
#define PCF_BIT_VL 0x80

#define KEY_LONG_MS 1000
#define KEY_DEBOUNCE_MS 30

// TCA9555 P17 = Port1 bit7。 / TCA9555 P17 = Port1 bit7.
#define KEY_POWER_MASK 0x80
#define PWR_PULSE_COUNT 15
#define PWR_PULSE_INTERVAL_MS 100
// 单轮 train 不掉电就按原厂再打；打满仍活着才进 ESP 深睡兜底。
// If one train does not cut power, repeat like the factory firmware; ESP deep sleep is the last resort.
#define PWR_OFF_TRAIN_LIMIT 10

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

/* ---- CX25601N 充电配置 / CX25601N charger setup ---- */

// 位域更新沿用原厂驱动的语义：mask 是字段宽度，shift 是字段起始位。
// Field update with the vendor driver's semantics: mask is the field width, shift its start bit.
static esp_err_t cx_update_bits(uint8_t reg, uint8_t mask, uint8_t shift, uint8_t field) {
    if (s_cx == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t cur = 0;
    esp_err_t err = dev_read(s_cx, reg, &cur, 1);
    if (err != ESP_OK) return err;
    const uint8_t m = (uint8_t)(mask << shift);
    cur = (uint8_t)((cur & (uint8_t)~m) | ((field << shift) & m));
    return dev_write(s_cx, reg, &cur, 1);
}

// 充电截止电压，10 mV 一档：低 5 位在 0x04[7:3]，高 4 位在 0x05[3:0]。
// Charge regulation in 10 mV steps: low 5 bits in 0x04[7:3], high 4 in 0x05[3:0].
static esp_err_t cx_set_vreg_mv(uint32_t mv) {
    if (mv < 3840) mv = 3840;
    if (mv > 4800) mv = 4800;
    const uint32_t code = mv / 10;
    esp_err_t err = cx_update_bits(CX_REG_VREG_LO, 0x1F, 3, (uint8_t)(code & 0x1F));
    if (err == ESP_OK) err = cx_update_bits(CX_REG_VREG_HI, 0x0F, 0, (uint8_t)((code >> 5) & 0x0F));
    return err;
}

// 充电电流 80 mA 一档，并像原厂一样把输入限流（20 mA 一档）设成同一个值。
// Charge current in 80 mA steps; the input limit (20 mA steps) follows it as the vendor does.
static esp_err_t cx_set_ichg_ma(uint32_t ma) {
    if (ma < 80) ma = 80;
    if (ma > 3040) ma = 3040;
    const uint32_t in_ma = ma < 100 ? 100 : (ma > 3000 ? 3000 : ma);
    const uint32_t icode = ma / 80;
    const uint32_t pcode = in_ma / 20;
    esp_err_t err = cx_update_bits(CX_REG_ICHG_LO, 0x03, 6, (uint8_t)(icode & 0x03));
    if (err == ESP_OK) err = cx_update_bits(CX_REG_ICHG_HI, 0x0F, 0, (uint8_t)((icode >> 2) & 0x0F));
    if (err == ESP_OK) err = cx_update_bits(CX_REG_IINDPM_LO, 0x0F, 4, (uint8_t)(pcode & 0x0F));
    if (err == ESP_OK) err = cx_update_bits(CX_REG_IINDPM_HI, 0x0F, 0, (uint8_t)((pcode >> 4) & 0x0F));
    return err;
}

// 私有寄存器区：按原厂顺序解锁（0x70 写 00/50/57/44，直到读回 0x03）。
// Private block: unlock in the vendor's order (0x70 = 00/50/57/44 until it reads back 0x03).
static void cx_unlock_private(void) {
    for (int i = 0; i < 10; i++) {
        static const uint8_t seq[] = {0x00, 0x50, 0x57, 0x44};
        for (size_t k = 0; k < sizeof(seq); k++) {
            dev_write(s_cx, CX_REG_UNLOCK, &seq[k], 1);
        }
        uint8_t v = 0;
        if (dev_read(s_cx, CX_REG_UNLOCK, &v, 1) == ESP_OK && v == 0x03) return;
    }
    ESP_LOGW(TAG, "CX25601N private unlock failed");
}

// 照搬原厂 hw_init_defaults()：关 DPDM 探测和看门狗，预充/终止用原厂码值，VREG 4.4 V，
// 充电与输入限流 1 A，充电期间不自动终止（原厂监控任务在 VBAT<VREG 时的稳态），最后开 EN_CHG。
// Mirrors the vendor hw_init_defaults(): DPDM probing and the watchdog stay off, precharge and
// termination keep the vendor codes, VREG is 4.4 V, charge and input limit are 1 A, termination
// stays disabled (the vendor monitor's steady state while VBAT < VREG), then EN_CHG is set.
static esp_err_t cx_charger_setup(void) {
    if (s_cx == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t part = 0;
    esp_err_t err = dev_read(s_cx, CX_REG_PART_INFO, &part, 1);
    if (err != ESP_OK) return err;

    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_TMR, 0x01, 6, 0);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 4, 0);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL1, 0x03, 0, 0);
    // 预充与终止电流沿用原厂码值，单位未公开。
    // Precharge and termination keep the vendor codes; their units are not public.
    if (err == ESP_OK) err = cx_update_bits(CX_REG_IPRECHG_LO, 0x0F, 4, 0x0C);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_IPRECHG_HI, 0x01, 0, 0x00);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_ITERM_LO, 0x1F, 3, 0x06);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_ITERM_HI, 0x01, 0, 0x00);
    if (err == ESP_OK) err = cx_set_vreg_mv(CX_CHG_VREG_MV);
    if (err == ESP_OK) err = cx_set_ichg_ma(CX_CHG_ICHG_MA);
    if (err != ESP_OK) return err;

    // 私有区三个值只由原厂驱动写入，含义未公开，照抄。
    // The three private values are written only by the vendor driver; their meaning is undocumented.
    cx_unlock_private();
    const uint8_t v86 = 0x06, v3a = 0x10, v46 = 0x20, zero = 0x00;
    dev_write(s_cx, 0x86, &v86, 1);
    dev_write(s_cx, 0x3A, &v3a, 1);
    dev_write(s_cx, 0x46, &v46, 1);
    dev_write(s_cx, CX_REG_UNLOCK, &zero, 1);

    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL0, 0x01, 0, 1);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL0, 0x01, 2, 0);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL3, 0x01, 2, 0);
    if (err == ESP_OK) err = cx_update_bits(0x1A, 0x01, 7, 1);
    if (err == ESP_OK) err = cx_update_bits(0x23, 0x07, 2, 0x07);
    if (err == ESP_OK) err = cx_update_bits(0x24, 0x01, 3, 1);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 5, 1);

    // 回读配置：串口一眼能看出快充有没有写进去。
    // Read the configuration back so the serial log shows whether fast charge took effect.
    uint8_t lo = 0, hi = 0;
    long ichg_ma = -1, iindpm_ma = -1, vreg_mv = -1;
    if (dev_read(s_cx, CX_REG_ICHG_LO, &lo, 1) == ESP_OK &&
        dev_read(s_cx, CX_REG_ICHG_HI, &hi, 1) == ESP_OK) {
        ichg_ma = (long)((((lo >> 6) & 0x03) | ((hi & 0x0F) << 2)) * 80);
    }
    if (dev_read(s_cx, CX_REG_IINDPM_LO, &lo, 1) == ESP_OK &&
        dev_read(s_cx, CX_REG_IINDPM_HI, &hi, 1) == ESP_OK) {
        iindpm_ma = (long)((((lo >> 4) & 0x0F) | ((hi & 0x0F) << 4)) * 20);
    }
    if (dev_read(s_cx, CX_REG_VREG_LO, &lo, 1) == ESP_OK &&
        dev_read(s_cx, CX_REG_VREG_HI, &hi, 1) == ESP_OK) {
        vreg_mv = (long)((((lo >> 3) & 0x1F) | ((hi & 0x0F) << 5)) * 10);
    }
    ESP_LOGI(TAG, "CX25601N part=0x%02X ichg=%ldmA iindpm=%ldmA vreg=%ldmV (%s)", part,
             ichg_ma, iindpm_ma, vreg_mv, err == ESP_OK ? "ok" : esp_err_to_name(err));
    return err;
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

/* ---- 电池滤波状态机 / Battery filter ---- */

// 滤波参数：真机日志（batt: ...）校准后可调。 / Filter tunables; calibrate from the "batt:" logs.
// 电池内阻：2026-10-25 卡上日志 13 次充放电切换（60 s 采样、ΔI≥80 mA）中位 180 mΩ。
// 原 100 mΩ 补偿不足，拔掉充电器那一分钟负载压降只补了一半，满电显示瞬间掉 3%。
// 取该组中位；2026-10-27 复核 14 次切换中位 206 mΩ，其中三次 1 A 大步 175–192 mΩ，维持 180。
// Pack resistance: the 2026-10-25 card log's 13 charge/discharge switches (60 s samples,
// ΔI ≥ 80 mA) median 180 mΩ. The old 100 mΩ under-compensated the load step, so the full
// display fell 3% in the first minute off the charger. Re-checked 2026-10-27: 14 switches
// median 206 mΩ, the three clean 1 A steps 175–192 mΩ; 180 stays.
#define BATT_R_MOHM 180
#define BATT_COMP_CAP_MV 150             // 负载补偿上限。 / Load compensation cap.
#define BATT_EMA_TAU_MS 25000            // 电压 EMA 时间常数。 / Voltage EMA time constant.
#define BATT_DEADBAND_PERMILLE 10        // 差距小于 1% 不动显示。 / Ignore sub-1% movement.
#define BATT_MAX_STEP_PERMILLE 10        // 单次可见变化最多 1%。 / At most 1% per visible step.
#define BATT_MIN_STEP_INTERVAL_MS 10000  // 两次可见变化至少隔 10 s。 / Steps sit at least 10 s apart.
#define BATT_RESTORE_TOL_PERMILLE 150    // 开机恢复容差。 / Boot-restore tolerance.
#define BATT_DONE_MIN_MV 4300            // 判满电压门槛，防状态位误判。 / Voltage gate for the done snap.
// 涓流判满：原厂在充电期间关掉终止，STATUS1 停在恒压码 2，top-off 码 3 不会出现，
// done 路径可能永不触发；高电压下持续近零电流即视为充满。
// Taper-full: the vendor keeps termination off while charging, so STATUS1 stays on the CV
// code 2 and the top-off code 3 never appears; the done path may never fire, so sustained
// near-zero current at a high voltage counts as full.
#define BATT_FULL_MV 4360                // 涓流判满的电压门槛。 / Voltage gate for the taper-full call.
#define BATT_FULL_MA 25                  // 高于该正向电流视为还在真充电。 / Above this positive current the pack is still really charging.
#define BATT_FULL_CONFIRM_MS 300000      // 持续近零电流多久判满。 / How long near-zero current must hold.
#define BATT_SAVE_INTERVAL_MS 300000     // NVS 周期保存间隔。 / Periodic NVS save cadence.
#define BATT_LOG_INTERVAL_MS 60000       // 校准日志间隔。 / Calibration log cadence.
// 卡上校准日志：不连电脑也能积累日常放电曲线；串口与卡写同一行。
// Calibration log on the card: daily discharge curves accumulate without a host;
// serial and card carry the same line.
#define BATT_LOG_PATH "/sdcard/batt.log"

// 4.4 V 高压电池 OCV-SOC 锚点（mV → ‰），mV 降序。2026-10-25 卡上日志实测充满平台
// 4374–4385 mV（289 点、|I|≤15 mA，中位 4379、免载最高 4385）：原 4400 mV 锚点不可达，
// 曲线满电只算到 98%，再被涓流判满硬拉到 100%，形成 2% 跳变。顶锚点改为 4380 mV。
// 中段锚点 2026-10-27 用回充对账校准（batt_副本.log，849 行、34.5 h）：满电拔线深放
// 8.4 h 至 4075 mV 后回满，净充 346 mAh；另一次夜间放到 4241 mV 回充 ~93 mAh。两条
// 回充腿与放电库仑积分互相一致（容量取规格 2050 mAh）：4300→955、4200→875、4100→790。
// 原锚点为无数据推测，中段偏低 6–11%。4.09 V 以下仍无放电数据，低段锚点保持原值，
// 不能按中段斜率外推——高压锂电池低段有膝盖，等一次放电到关机再校准。
// OCV-SOC anchors for the 4.4 V LiHV pack (mV -> permille), descending in mV. The
// 2026-10-25 card log measured a full-charge plateau of 4374–4385 mV (289 samples,
// |I| ≤ 15 mA, median 4379, 4385 unloaded max): the old 4400 mV anchor was unreachable, so
// the curve topped out near 98% and the taper-full snap pulled it to 100% as a 2% jump.
// The top anchor is now 4380 mV. Mid anchors were recalibrated 2026-10-27 by refill
// accounting (batt_副本.log, 849 rows, 34.5 h): a full pack discharged 8.4 h to 4075 mV
// took 346 mAh net to recharge, and a second overnight leg to 4241 mV took ~93 mAh. Both
// refill legs agree with the discharge coulomb integral (capacity per spec, 2050 mAh):
// 4300→955, 4200→875, 4100→790. The old mid anchors were guesses and read 6–11% low.
// Below 4.09 V there is still no discharge data, so the low anchors stay as-is; do not
// extrapolate the mid-section slope — LiHV packs have a low knee. Calibrate after one
// full discharge to shutdown.
static const int32_t s_soc_curve[][2] = {
    {4380, 1000}, {4300, 955}, {4200, 875}, {4100, 790}, {4000, 560},
    {3900, 450},  {3800, 340}, {3700, 230}, {3600, 140}, {3500, 80},
    {3400, 40},   {3300, 0},
};

// 滤波状态：跨 poll 保持，首采时初始化。 / Filter state held across polls, seeded by the first sample.
typedef struct {
    bool inited;
    int32_t ema_q8;        // 补偿后电压 EMA，×256 定点。 / EMA of compensated OCV, fixed-point ×256.
    int32_t disp_permille; // 对外显示值。 / The value consumers see.
    int64_t last_poll_us;
    int64_t last_move_us;
} batt_filter_t;

// 查表线性插值。 / Linear interpolation over the anchor table.
static int32_t batt_curve_soc(int32_t mv) {
    const size_t n = sizeof(s_soc_curve) / sizeof(s_soc_curve[0]);
    if (mv >= s_soc_curve[0][0]) return s_soc_curve[0][1];
    for (size_t i = 1; i < n; i++) {
        if (mv >= s_soc_curve[i][0]) {
            const int32_t span = s_soc_curve[i - 1][0] - s_soc_curve[i][0];
            const int32_t rise = s_soc_curve[i - 1][1] - s_soc_curve[i][1];
            return s_soc_curve[i][1] + (mv - s_soc_curve[i][0]) * rise / span;
        }
    }
    return s_soc_curve[n - 1][1];
}

// 一步滤波：负载补偿 → EMA → 查表 → 死区/单向/限速；纯函数，便于主机对拍。
// One filter step: load compensation -> EMA -> table -> deadband/one-direction/rate limit;
// pure so host tests can replay it.
static void batt_filter_step(batt_filter_t* f, int64_t now_us, uint16_t raw_mv, bool have_i,
                             int16_t raw_ma, bool charging, bool charge_done, int32_t saved_permille,
                             int32_t* out_mv, int32_t* out_permille) {
    // 负载补偿方向按充电标志，不依赖电流符号约定；封顶防电流尖峰。
    // Compensation direction follows the charging flag, not the current sign; capped against spikes.
    int32_t comp = 0;
    if (have_i) {
        comp = ((int32_t)(raw_ma < 0 ? -raw_ma : raw_ma) * BATT_R_MOHM) / 1000;
        if (comp > BATT_COMP_CAP_MV) comp = BATT_COMP_CAP_MV;
    }
    const int32_t ocv = charging ? (int32_t)raw_mv - comp : (int32_t)raw_mv + comp;

    // poll 间隔不固定（200/40/20/500/1500 ms 都有），按实际 dt 算系数。
    // Poll intervals vary (200/40/20/500/1500 ms); derive the factor from the real dt.
    int64_t dt_us = f->inited ? now_us - f->last_poll_us : 0;
    if (dt_us < 0) dt_us = 0;
    f->last_poll_us = now_us;

    if (!f->inited) {
        f->inited = true;
        f->ema_q8 = ocv * 256;
        const int32_t table = batt_curve_soc(ocv);
        // 首采恢复：保存值在表值容差内则沿用，深睡唤醒显示连续。
        // First-sample restore: keep the saved value inside tolerance, so deep-standby
        // wake keeps the display continuous.
        f->disp_permille = table;
        if (saved_permille >= 0 && saved_permille <= 1000 &&
            saved_permille - table <= BATT_RESTORE_TOL_PERMILLE &&
            table - saved_permille <= BATT_RESTORE_TOL_PERMILLE) {
            f->disp_permille = saved_permille;
        }
        f->last_move_us = now_us;
    } else {
        const int64_t tau_us = (int64_t)BATT_EMA_TAU_MS * 1000;
        f->ema_q8 += (int32_t)(((int64_t)ocv * 256 - f->ema_q8) * dt_us / (tau_us + dt_us));
    }

    // 充电完成即认为满电，直接对齐 100%；电压门槛挡住状态位误判。
    // Charge done means full: align to 100% directly; the voltage gate guards a misread status.
    if (charge_done && raw_mv >= BATT_DONE_MIN_MV) {
        f->disp_permille = 1000;
        f->last_move_us = now_us;
    } else {
        // 单向步进：放电只降不升，充电只升不降。
        // One-direction stepping: discharge only falls, charge only rises.
        int32_t target = batt_curve_soc(f->ema_q8 / 256);
        if (charging) {
            if (target < f->disp_permille) target = f->disp_permille;
        } else if (target > f->disp_permille) {
            target = f->disp_permille;
        }
        const int32_t delta = target - f->disp_permille;
        // 死区 + 限速：小于 1% 不动；动时单步最多 1%，两次至少隔 10 s。
        // Deadband + rate limit: sub-1% is ignored; steps cap at 1% and sit 10 s apart.
        if (delta != 0) {
            const int32_t abs_delta = delta < 0 ? -delta : delta;
            if (abs_delta >= BATT_DEADBAND_PERMILLE &&
                now_us - f->last_move_us >= (int64_t)BATT_MIN_STEP_INTERVAL_MS * 1000) {
                const int32_t step =
                    abs_delta > BATT_MAX_STEP_PERMILLE ? BATT_MAX_STEP_PERMILLE : abs_delta;
                f->disp_permille += delta < 0 ? -step : step;
                f->last_move_us = now_us;
            }
        }
    }

    *out_mv = f->ema_q8 / 256;
    *out_permille = f->disp_permille;
}

// 涓流判满的纯判定，便于主机对拍：CX done 位优先；否则充电中、高电压且正向电流
// 低于门槛持续满 BATT_FULL_CONFIRM_MS 才算满。负向电流（负载浪涌）不清窗口，
// 电压门槛兜底持续放电；状态经 *since_us 传入传出。
// Pure taper-full decision for host replay: a real CX done bit wins; otherwise
// charging at a high voltage with positive current under the gate must hold for
// BATT_FULL_CONFIRM_MS. Negative current (load surges) never clears the window —
// the voltage gate covers sustained drain — and the state travels via *since_us.
static bool batt_full_now(bool charge_done, bool charging, bool have_v, int32_t raw_mv,
                          bool have_i, int16_t raw_ma, int64_t now_us, int64_t *since_us) {
    if (charge_done) {
        *since_us = -1;
        return true;
    }
    if (!charging || !have_v || !have_i || raw_mv < BATT_FULL_MV || raw_ma >= BATT_FULL_MA) {
        *since_us = -1;
        return false;
    }
    if (*since_us < 0) *since_us = now_us;
    return now_us - *since_us >= (int64_t)BATT_FULL_CONFIRM_MS * 1000;
}

// BQ27220 内部库仑计 SOC，只读对比用。 / BQ27220 internal coulomb-counted SOC, read-only comparison.
static int bq_read_soc(void) {
    uint8_t pct = 0;
    if (dev_read(s_bq, BQ_REG_SOC, &pct, sizeof(pct)) != ESP_OK) return -1;
    return pct <= 100 ? pct : -1;
}

// 电量持久化到 NVS，供深睡唤醒后恢复显示。 / Persist the percent to NVS for deep-standby wake restore.
static void batt_save(int32_t permille) {
    nvs_handle_t nvs;
    if (nvs_open("pmu", NVS_READWRITE, &nvs) != ESP_OK) return;
    if (nvs_set_i32(nvs, "soc", permille) == ESP_OK) {
        nvs_commit(nvs);
    }
    nvs_close(nvs);
}

/* ---- 校准日志 / Calibration log ---- */

// 最新采样快照：轮询侧在主循环任务里填，日志任务只读副本；I2C 全部留在轮询侧。
// Latest sample snapshot: the poll side fills it on the loop task and the logger
// task only copies it out; every I2C access stays on the poll side.
typedef struct {
    bool valid;
    uint16_t raw_mv;
    int16_t ma;
    bool have_i;
    bool charging;
    bool charge_done;
    int8_t chg_stat;
    int32_t ocv_mv;
    int32_t disp_permille;
    int8_t ic_soc;
} batt_log_sample_t;
static batt_log_sample_t s_batt_log_sample;
static portMUX_TYPE s_batt_log_mux = portMUX_INITIALIZER_UNLOCKED;
// 日志任务句柄，deinit 时停止，避免反初始化后继续写旧采样。
// Logger task handle, stopped on deinit so a torn-down shim cannot keep logging stale samples.
static TaskHandle_t s_batt_log_task;
// 设置页的日志开关；默认关，关时任务照常休眠但不碰卡也不打印。
// Settings-page log switch; off by default, and while off the task keeps sleeping but touches
// neither the card nor the console.
static bool s_batt_log_enabled;

void read_pico_pmu_set_batt_log(bool enabled) { s_batt_log_enabled = enabled; }

// 校准日志任务：串口与 TF 卡写同一行，写卡只在本任务做，不卡主循环跟手；卡不在
// 或未挂载（含 USB 读卡让出）时静默跳过，逐行开关文件，拔卡/断电最多丢最后一行。
// Calibration logger task: serial and the TF card carry the same line, and only
// this task touches the card so the loop never stalls mid-drag; a missing or
// unmounted card (including USB export) skips silently, and the per-line
// open/close loses at most the last line on removal or power loss.
static void batt_log_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BATT_LOG_INTERVAL_MS));
        if (!s_batt_log_enabled) continue;
        batt_log_sample_t s;
        portENTER_CRITICAL(&s_batt_log_mux);
        s = s_batt_log_sample;
        portEXIT_CRITICAL(&s_batt_log_mux);
        if (!s.valid) continue;
        const time_t wall = time(NULL);
        char line[160];
        const int n = snprintf(line, sizeof(line),
            "t=%lld up=%lld raw=%u ma=%d chg=%d done=%d cs=%d ocv=%ld disp=%ld ic_soc=%d\n",
            wall >= 1704067200 ? (long long)wall : -1LL,
            (long long)(esp_timer_get_time() / 1000000),
            (unsigned)s.raw_mv, s.have_i ? (int)s.ma : 0, s.charging ? 1 : 0,
            s.charge_done ? 1 : 0, (int)s.chg_stat, (long)s.ocv_mv,
            (long)s.disp_permille, (int)s.ic_soc);
        if (n <= 1 || (size_t)n >= sizeof(line)) continue;
        ESP_LOGI(TAG, "batt: %.*s", n - 1, line);
        FILE *f = fopen(BATT_LOG_PATH, "a");
        if (f == NULL) continue;
        if (ftell(f) == 0) {
            fputs("# t up raw ma chg done cs ocv disp ic_soc\n", f);
        }
        fputs(line, f);
        fclose(f);
    }
}

static batt_filter_t s_batt_filter;
// NVS 恢复值，只在首采时被消费一次。 / NVS restore value, consumed once by the first sample.
static int32_t s_batt_saved_permille = -1;
static bool s_batt_charging_last;
static int64_t s_batt_last_save_us;
static int64_t s_batt_last_log_us;
// 最近一次 BQ27220 库仑计读数（60 s 一次，轮询侧更新）。 / Latest BQ27220 coulomb
// reading (60 s cadence, refreshed on the poll side).
static int8_t s_batt_ic_soc = -1;
// 涓流判满的确认窗口起点（µs，-1 表示未在窗口内）。 / Start of the taper-full
// confirmation window (µs; -1 when outside the window).
static int64_t s_batt_full_since_us = -1;

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
    const int64_t now = esp_timer_get_time();

    uint16_t mv = 0;
    int16_t ma = 0;
    const bool have_v = s_bq != NULL && bq_read_voltage(&mv) == ESP_OK && mv >= 2500 && mv <= 5000;
    const bool have_i = s_bq != NULL && bq_read_current(&ma) == ESP_OK;

    // 充电检测照旧：CX STATUS1 优先，电流作兜底。
    // Charger detection unchanged: CX STATUS1 first, current as the fallback.
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
    const bool charge_done_cx = have_chg && chg_stat == CX_CHG_STAT_TOPOFF;
    // 涓流判满兜底：CX 在零电流下也不报 done（见 BATT_FULL_* 注释），高电压持续
    // 近零电流同样进入滤波的判满对齐。
    // Taper-full fallback: the CX never reports done even at zero current (see
    // the BATT_FULL_* notes); sustained near-zero current at a high voltage also
    // enters the filter's charge-done alignment.
    const bool charge_done = batt_full_now(charge_done_cx, charging, have_v, (int32_t)mv,
                                           have_i, ma, now, &s_batt_full_since_us);
    s_snap.charge_state = charging ? PMU_CHARGE_CHARGING : PMU_CHARGE_NOT_CHARGING;
    if (charging) {
        s_snap.flags |= PMU_STATUS_CHARGING_ACTIVE;
    } else {
        s_snap.flags &= ~PMU_STATUS_CHARGING_ACTIVE;
    }

    if (have_v) {
        int32_t ocv_mv = (int32_t)mv;
        int32_t disp = 0;
        batt_filter_step(&s_batt_filter, now, mv, have_i, ma, charging, charge_done,
                         s_batt_saved_permille, &ocv_mv, &disp);
        s_batt_saved_permille = -1;  // 恢复值只喂首采。 / The restore value feeds the first sample only.
        s_snap.battery_mv = (uint16_t)ocv_mv;
        s_snap.battery_raw = mv;
        s_snap.soc_permille = (uint16_t)disp;
        s_snap.flags |= PMU_STATUS_BATTERY_VALID;
        s_snap.status_ok = true;

        // 保存：周期 + 充电状态切换。/ Periodic saves plus charge-state transitions.
        const bool charging_changed = charging != s_batt_charging_last;
        s_batt_charging_last = charging;
        if (charging_changed ||
            now - s_batt_last_save_us >= (int64_t)BATT_SAVE_INTERVAL_MS * 1000) {
            s_batt_last_save_us = now;
            batt_save(disp);
        }

        // 采样快照喂给日志任务；ic_soc 仍按 60 s 在轮询侧读，I2C 不离开主循环任务。
        // 日志关闭时连这次库仑计读也省掉，不产生无人消费的 I2C 流量。
        // Feed the snapshot to the logger task; ic_soc still reads at a 60 s cadence on the poll
        // side so I2C never leaves the loop task. With logging off even that coulomb read is
        // skipped, so no I2C traffic goes to a consumer that will not look at it.
        if (s_batt_log_enabled && now - s_batt_last_log_us >= (int64_t)BATT_LOG_INTERVAL_MS * 1000) {
            s_batt_last_log_us = now;
            s_batt_ic_soc = (int8_t)bq_read_soc();
        }
        const batt_log_sample_t sample = {
            .valid = true,
            .raw_mv = mv,
            .ma = ma,
            .have_i = have_i,
            .charging = charging,
            .charge_done = charge_done,
            .chg_stat = have_chg ? (int8_t)chg_stat : (int8_t)-1,
            .ocv_mv = ocv_mv,
            .disp_permille = disp,
            .ic_soc = s_batt_ic_soc,
        };
        portENTER_CRITICAL(&s_batt_log_mux);
        s_batt_log_sample = sample;
        portEXIT_CRITICAL(&s_batt_log_mux);
    } else {
        s_snap.flags &= ~PMU_STATUS_BATTERY_VALID;
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

    // 充电芯片按原厂配置写一次（1 A 快充、4.4 V 截止）；失败不阻塞 PMU，充电按遗留值继续。
    // Configure the charger once with the vendor defaults (1 A fast charge, 4.4 V regulation);
    // a failure never blocks the PMU and charging continues on whatever the chip holds.
    if (s_cx != NULL && cx_charger_setup() != ESP_OK) {
        ESP_LOGW(TAG, "CX25601N setup incomplete; charge registers keep previous values");
    }

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
    // 上次电量恢复值：深睡唤醒（复位重启）后显示连续。
    // Last-percent restore: keeps the display continuous after a deep-standby wake (reset boot).
    if (nvs_open("pmu", NVS_READONLY, &nvs) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(nvs, "soc", &v) == ESP_OK && v >= 0 && v <= 1000) {
            s_batt_saved_permille = v;
        }
        nvs_close(nvs);
    }

    s_ready = true;
    // 校准日志任务：起不来只影响串口/卡日志，不影响电量功能。
    // Calibration logger task: failing to start only costs the serial/card log.
    if (xTaskCreate(batt_log_task, "battlog", 4096, NULL, 1, &s_batt_log_task) != pdPASS) {
        ESP_LOGW(TAG, "battery log task not started");
    }
    ESP_LOGI(TAG, "Metalio PMU shim ready (BQ=%d CX=%d RTC=%d VCOM=%dmV)", s_bq != NULL,
             s_cx != NULL, s_rtc != NULL, s_vcom_mv);
    return ESP_OK;
}

esp_err_t read_pico_pmu_deinit(void) {
    if (s_batt_log_task != NULL) {
        vTaskDelete(s_batt_log_task);
        s_batt_log_task = NULL;
    }
    portENTER_CRITICAL(&s_batt_log_mux);
    s_batt_log_sample.valid = false;
    portEXIT_CRITICAL(&s_batt_log_mux);
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
    // 每轮 train 打 PWR_PULSE_COUNT 次；PMU 不掉电就按原厂反复打，打满才进深睡兜底。
    // Each train fires PWR_PULSE_COUNT pulses; repeat factory-style while the host lives on
    // and fall back to ESP deep sleep only after the trains are exhausted.
    for (unsigned train = 0; train < PWR_OFF_TRAIN_LIMIT; ++train) {
        for (int i = 0; i < PWR_PULSE_COUNT; i++) {
            esp_err_t err = read_pico_pwr_pulse(true);
            if (err != ESP_OK) return err;
            vTaskDelay(pdMS_TO_TICKS(PWR_PULSE_INTERVAL_MS));
            err = read_pico_pwr_pulse(false);
            if (err != ESP_OK) return err;
            vTaskDelay(pdMS_TO_TICKS(PWR_PULSE_INTERVAL_MS));
        }
        ESP_LOGW(TAG, "power-off train %u done, host still alive", train + 1);
    }
    ESP_LOGW(TAG, "power-off pulses exhausted; entering deep sleep");
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
