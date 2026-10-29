# device_power

设备电源接口。S31 产品直接编译 `device_power_metalio.c`，适配 BQ27220 电量、CX25601N 充电、PCF8563 RTC 与 TCA9555 电源键。`device_power_cw32.c` 保留旧板 CW32 主机协议实现，不参与产品构建。

Device power interfaces. The S31 product selects `device_power_metalio.c` for BQ27220
battery readings, CX25601N charging, PCF8563 RTC and TCA9555 power keys.
`device_power_cw32.c` retains the old board's CW32 host implementation outside the product build.

本地组件名为 `device_power`；上游来源为 `mindreset/read_pico_pmu`，保留其版权、许可和来源地址。
The local component is `device_power`, derived from `mindreset/read_pico_pmu`; retain its copyright, license and source URLs.

## 做什么 / What it does

- `device_power_refresh()`：读取身份、状态、诊断、一条排队事件、配置、时间和闹钟，即调试页显示的全部内容。/ Identity, status, diagnostics, one queued event, config, time and alarm — everything the debug page shows.
- `device_power_poll()`：只读取状态、快照电池和待处理事件数，开销足够低，可在页面 tick 中调用。/ Status, quick counters and the pending-event count only, cheap enough to call from a page's tick.
- `device_power_get()`：最新快照，各数据块附 `*_ok` 标志，页面可区分「未读到」与「读到零」。/ The latest snapshot, including per-block `*_ok` flags so a page can tell "not read" from "read as zero".
- `device_power_cmd()` / `device_power_action()`：原始命令与动作接口。/ Raw command and action entry points.
- 事件：`device_power_drain_events()` 丢弃开机残留事件；`device_power_take_key_short()` / `device_power_take_key_wakeup()` 供锁屏使用。/ Events: `device_power_drain_events()` to discard boot leftovers, `device_power_take_key_short()` / `device_power_take_key_wakeup()` for the lock screen.
- 电源交接：`device_power_report_ready()`、`device_power_report_sleep()`（等待 PMU 确认后再拉低 EN）、`device_power_power_off()`。/ Power handoff: `device_power_report_ready()`, `device_power_report_sleep()` (waits for the PMU to acknowledge before it drops EN), `device_power_power_off()`.
- 面板 VCOM：`device_power_vcom_get()` 只读。每台设备的面板标定值在出厂时写入 PMU；主机开机读取一次用于配置驱动，不再写回。/ Panel VCOM: `device_power_vcom_get()` is read-only. Each unit's panel calibration is bound on the PMU at manufacturing time; the host reads it once at boot to configure the display driver and never writes it back.

## 协议 / Protocol

`include/pmu_protocol.h` 保留原 CW32 线格式与共用数据结构：magic、帧类型、序号、命令码、载荷、CRC。上述 API 描述包含旧 CW32 后端行为；S31 不支持的操作仍返回原错误码，例如 `device_power_report_sleep()` 返回不支持，由应用睡眠模块处理。

`include/pmu_protocol.h` retains the CW32 wire format and shared structures: magic,
frame kind, sequence, command code, payload and CRC. The API descriptions above
include CW32 backend behavior. Unsupported S31 operations retain their existing errors;
for example, `device_power_report_sleep()` returns not supported and the application handles sleep.

## 文档 / Documentation

`docs/` 内为 PMU 主机对接说明：`llms-full_*.md` 为正文，`pmu_protocol_*.h` 与
`pmu_registers_*.json` 为配套的协议头与寄存器表，`pmu_datasheet_v1.0.8_*.pdf`
为印刷版。中英各一份。

`docs/` holds the host integration guide for the PMU: `llms-full_*.md` is the text,
`pmu_protocol_*.h` and `pmu_registers_*.json` are the matching protocol header and
register map, and `pmu_datasheet_v1.0.8_*.pdf` is the printed edition.
Chinese and English editions are provided.

## 旧板硬件 / Legacy hardware

| 项目 / Item | 规格 / Spec |
| --- | --- |
| 芯片 / Part | 跑小纸 Pico PMU 固件的 CW32L010 / CW32L010 running the Read Pico PMU firmware |
| 总线 / Bus | I2C，400 kHz，地址 `PMU_I2C_ADDR` / I2C, 400 kHz, address `PMU_I2C_ADDR` |
| 中断 / Interrupt | 经 FCA9555 扩展口（P0.2），也是浅睡唤醒源 / Routed through the FCA9555 expander (P0.2), also the light-sleep wake source |

## 许可 / License

Apache-2.0，见 [LICENSE](LICENSE)。

Apache-2.0, see [LICENSE](LICENSE).
