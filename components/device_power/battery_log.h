/*
 * 中文：电量校准日志的数据、采样覆盖统计和文件输出；不访问 I2C，不修改电量算法。
 * English: Battery-calibration records, sampling coverage and file output; no I2C or battery-algorithm changes.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BATT_LOG_PERIOD_US (60LL * 1000000)
#define BATT_LOG_MAX_INTERVAL_US (5LL * 1000000)
#define BATT_LOG_QUEUE_SIZE 16
#define BATT_LOG_FILE_LIMIT (4L * 1024 * 1024)
#define BATT_LOG_LINE_SIZE 2048
#define BATT_LOG_CHECKPOINT_VERSION 1
#define BATT_LOG_CHECKPOINT_US (300LL * 1000000)

enum {
    BATT_LOG_SAMPLE = 1u << 0,
    BATT_LOG_BEGIN = 1u << 1,
    BATT_LOG_END = 1u << 2,
    BATT_LOG_CHARGER = 1u << 3,
    BATT_LOG_FULL = 1u << 4,
    BATT_LOG_VALIDITY = 1u << 5,
    BATT_LOG_GAP = 1u << 6,
    BATT_LOG_LOCK = 1u << 7,
    BATT_LOG_WAKE = 1u << 8,
    BATT_LOG_OFF = 1u << 9,
    BATT_LOG_RESTART = 1u << 10,
    BATT_LOG_UNDERVOLTAGE = 1u << 11,
    BATT_LOG_LOW = 1u << 12,
};

enum {
    BATT_LOG_FULL_NOT_CHARGING = 1u << 0,
    BATT_LOG_FULL_NO_V = 1u << 1,
    BATT_LOG_FULL_NO_I = 1u << 2,
    BATT_LOG_FULL_LOW_V = 1u << 3,
    BATT_LOG_FULL_HIGH_I = 1u << 4,
    BATT_LOG_FULL_DISCHARGE = 1u << 5,
    BATT_LOG_FULL_GAP = 1u << 6,
};

typedef struct {
    int64_t up_us;           ///< 实际采样时间 / Actual sample time
    int64_t wall_sec;        ///< 采样时日历时间，未知为 -1 / Sample wall time, -1 when unknown
    uint32_t read_us;        ///< 本轮采样耗时 / Time spent sampling
    bool have_v;            ///< 电压有效 / Voltage valid
    bool have_i;            ///< 电流有效 / Current valid
    bool have_chg;          ///< 充电芯片状态有效 / Charger status valid
    uint16_t raw_mv;         ///< 原始电压 / Raw voltage
    int16_t ma;             ///< 正充负放的电流 / Current, positive charging and negative discharging
    bool charging;          ///< 现有算法的充电判断 / Charging decision from the existing algorithm
    bool charge_done;       ///< 现有算法的判满结果 / Full decision from the existing algorithm
    uint8_t done_source;    ///< 0 无、1 CX、2 软件 / 0 none, 1 CX, 2 software
    uint8_t charger_status; ///< CX 原始状态字节 / Raw CX status byte
    int32_t comp_mv;        ///< 即时补偿电压 / Instant compensated voltage
    int32_t ocv_mv;         ///< EMA 电压 / EMA voltage
    int32_t curve_permille; ///< 查表值 / Curve target
    int32_t disp_permille;  ///< 显示值 / Display value
    int8_t ic_soc;          ///< 芯片 SOC，未知为 -1 / Gauge SOC, -1 when unknown
    int64_t ic_us;          ///< 芯片 SOC 的独立采样时间 / Separate gauge-SOC sample time
    bool have_filter;       ///< 本次采样已计算滤波值 / Filter values belong to this sample
    bool full_reset;        ///< 本轮中断了判满确认 / Full-confirmation window interrupted
    uint8_t full_gate;      ///< 判满阻断原因位图 / Full-confirmation blockers
    int32_t full_hold_ms;   ///< 已连续满足的时间，未开始为 -1 / Continuous hold, -1 when inactive
    uint8_t restore;        ///< 开机恢复决策，见组件 README / Boot restore decision, see README
} batt_log_sample_t;

typedef struct {
    int64_t start_us, max_gap_us;
    int64_t v_sum, i_sum;
    uint32_t samples, locked_samples, v_count, i_count, cx_changes, full_resets;
    int32_t v_min, v_max, i_min, i_max;
    uint32_t full_reset_mask;
    int64_t cx_first_us, cx_last_us;
} batt_log_stats_t;

// NVS 自带完整性校验；格式/范围校验后才能继承，缺测区间不估算。
// NVS supplies integrity checks; validate layout/ranges before resuming, never extrapolate gaps.
typedef struct {
    uint32_t version, active;
    uint64_t chain, boot;
    uint32_t session, seq;
    int64_t wall_sec, covered_us, unknown_us, locked_us, in_ma_us, out_ma_us;
} batt_log_checkpoint_t;

typedef struct {
    batt_log_sample_t sample; ///< 最近一轮采样，允许无效 / Latest sampling attempt, possibly invalid
    bool have_sample;         ///< 是否采过样 / Whether sampling has occurred
    bool locked;              ///< 锁屏等待状态 / Locked-wait state
    uint32_t session;         ///< 日志开关会话 / Logging session
    uint32_t seq;             ///< 本次开机内记录序号 / Record sequence within this boot
    uint32_t events;          ///< 事件位图 / Event mask
    uint32_t dropped;         ///< 队列累计丢弃条数 / Cumulative queue drops
    int64_t up_us;            ///< 事件时间 / Event time
    int64_t wall_sec;         ///< 事件日历时间 / Event wall time
    int64_t covered_us;       ///< 可用于积分的时间 / Time eligible for integration
    int64_t unknown_us;       ///< 无覆盖时间 / Time without coverage
    int64_t locked_us;        ///< 锁屏等待时间，含唤醒轮询 / Locked-wait time, including wake polling
    int64_t in_ma_us;         ///< 有效区间充入积分 / Charge integral over eligible intervals
    int64_t out_ma_us;        ///< 有效区间放出积分 / Discharge integral over eligible intervals
    batt_log_stats_t stats;   ///< 自窗口起点累计；事件行可重叠 / Window totals; event rows may overlap
    uint8_t low_mask;         ///< 本轮放电已见的低电压点 / Observed low-voltage milestones
    batt_log_checkpoint_t base; ///< 写任务附加的恢复基数 / Resume base attached by the writer
    uint32_t checkpoint_errors; ///< 写任务检查点失败数 / Writer checkpoint errors
} batt_log_record_t;

typedef struct {
    bool enabled, locked, have_sample, have_cursor, previous_valid;
    uint32_t session, seq, dropped;
    int64_t cursor_us, covered_us, unknown_us, locked_us;
    int64_t in_ma_us, out_ma_us;
    int64_t last_period_us, last_cx_emit_us;
    uint8_t low_mask;
    batt_log_stats_t stats;
    batt_log_sample_t latest;
    batt_log_record_t queue[BATT_LOG_QUEUE_SIZE];
    unsigned head, count;
} batt_log_state_t;

typedef struct {
    uint32_t session; ///< 最近成功写入的会话 / Last successfully written session
    bool header;      ///< 下一条必须重写元信息 / Metadata required before the next record
} batt_log_file_t;

bool batt_log_checkpoint_valid(const batt_log_checkpoint_t *c);
void batt_log_checkpoint_make(batt_log_checkpoint_t *out, const batt_log_checkpoint_t *base,
                              const batt_log_record_t *r, uint64_t boot);

/// 调用方持锁；开关会话重置积分，保留全开机序号及待写队列。/ Caller holds its lock; sessions reset accounting, retaining boot sequences and queued records.
void batt_log_set_enabled(batt_log_state_t *s, bool enabled, int64_t now, int64_t wall);
/// 调用方持锁；仅相邻有效且间隔不超过 5 秒的清醒采样参与估算。/ Caller holds its lock; only adjacent valid awake samples at most 5 seconds apart enter estimates.
void batt_log_observe(batt_log_state_t *s, const batt_log_sample_t *sample);
/// 事件边界中断积分；锁屏等待整体标为无覆盖，不能当作实测睡眠电流。/ Events break integration; locked waits are uncovered, never measured sleep current.
void batt_log_note(batt_log_state_t *s, uint32_t events, int64_t now, int64_t wall);
/// 调用方持锁弹出一条记录。/ Pop one record while holding the caller's lock.
bool batt_log_pop(batt_log_state_t *s, batt_log_record_t *record);
/// 有界保留队列；优先淘汰普通记录，返回本次是否发生丢弃。/ Bounded retention preferring critical records; returns whether any record was lost.
bool batt_log_retain(batt_log_state_t *s, const batt_log_record_t *record);
/// 格式 3；独立记录采样、事件和写入时间，未知值为 -1。/ Format 3 separates sample, event and write times, with -1 for unknown values.
int batt_log_format(char *line, size_t size, const batt_log_record_t *r, uint64_t boot,
                    int64_t write_us, uint32_t io_errors, uint32_t file_missed,
                    uint32_t file_seq);
/// 单写入者；检查轮转、写入、同步和关闭结果，失败后下一条重写元信息。/ Single writer; check rotation, writes, sync and close, requiring metadata after failures.
bool batt_log_append(batt_log_file_t *file, const char *path, const char *backup,
                     long limit, uint32_t session, const char *metadata, const char *line);
