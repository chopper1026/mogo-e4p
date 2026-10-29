# device_power

设备电源接口。S31 产品编译 `device_power_metalio.c`，适配 BQ27220 电量、CX25601N 充电、PCF8563 RTC 与 TCA9555 电源键。旧板 CW32 主机后端已移除。

Device power interfaces. The S31 product compiles `device_power_metalio.c` for BQ27220
battery readings, CX25601N charging, PCF8563 RTC and TCA9555 power keys. The legacy
CW32 host backend has been removed.

本地组件名为 `device_power`；上游来源为 `mindreset/read_pico_pmu`，保留其版权、许可和来源地址。
The local component is `device_power`, derived from `mindreset/read_pico_pmu`; retain its copyright, license and source URLs.

## 做什么 / What it does

- `device_power_refresh()` / `device_power_poll()`：轮询当前板的电源键、电量和充电状态；前者复用后者。/ Poll current-board power keys, battery and charging state; refresh delegates to poll.
- `device_power_get()`：提供带有效标记的兼容快照；电池电压读错或越界时清除有效位，调用方不能沿用旧电量。/ Return the compatibility snapshot with validity flags; invalid or failed voltage samples clear validity rather than presenting stale battery percentages.
- 电量：电压/电流负载补偿、EMA、曲线映射与单向限速，保留已有 NVS 电量恢复及按需校准日志。/ Battery: load compensation, EMA, curve mapping and one-direction rate limiting, retaining saved-percent recovery and optional calibration logging.
- `device_power_cmd()`：支持 RTC 读时与同步；未实现的旧命令返回 `ESP_ERR_NOT_SUPPORTED`。/ Supports RTC reads and sync; unimplemented legacy commands return `ESP_ERR_NOT_SUPPORTED`.
- 电源键：TCA9555 P17 采样，去抖后提供短按、长按和唤醒消费接口。/ Samples TCA9555 P17 and exposes debounced short/long press and wake-consumption APIs.
- 电源交接：`device_power_report_sleep()` 返回不支持，由应用按当前板路径转入关机；`device_power_power_off()` 发送关机脉冲，耗尽重试后进入 ESP 深睡。/ Soft sleep reports not supported so the application escalates along the current board's power-off path; power-off sends the pulse train and falls back to ESP deep sleep after its retry limit.
- 面板 VCOM：启动读取已有标定值，写入接口仅供出厂流程使用。/ Reads existing calibration at boot; the write API is reserved for factory setup.

## 充电与 RTC / Charging and RTC

CX25601N 请求 1 A，经原厂 80 mA 档位量化为 960 mA，输入限流跟随量化值；VREG 为 4.4 V。轮询按有效电压切换终止，输入存在且空闲/近零电流时补充充电，无输入且有效电压 ≤3.1 V 时按原厂关 BATFET。配置不完整时报告失败，并尽力恢复硬件终止与停充。
CX25601N quantizes the 1 A request to 960 mA using vendor 80 mA steps; the input limit follows that value and VREG is 4.4 V. Polling controls termination from valid voltage, kicks recharge on input-present idle/near-zero-current status and shuts BATFET on valid no-input voltage at or below 3.1 V. Incomplete configuration reports failure and attempts to restore hardware termination and disable charging.

维护使用现有轮询上的非阻塞状态机，恢复充电最早在 200 ms 后的下一次轮询执行，失败继续重试；不更改电量曲线、补偿与滤波。PCF8563 启动清 STOP 和 AF/TF，寄存器存 UTC，非法 BCD、日期和电压失效标志不用于系统校时。
Maintenance uses a nonblocking state machine on existing polling, resuming recharge on the next poll at least 200 ms later and retrying failed restoration. Battery curves, compensation and filtering remain unchanged. PCF8563 startup clears STOP and AF/TF, stores UTC and rejects invalid BCD/calendar dates or voltage-loss flags for system clock updates.

关机与反初始化前恢复硬件充电终止，并完成待恢复的充电使能；失败则交还调用方，不带着临时停充状态断电。
Before power-off or deinit, restore hardware termination and pending charge enable; failure returns to the caller instead of powering off with a temporary charge disable.

原厂来源与调度差异见 [board_i2c](../board_i2c/README.md)。
See board_i2c for vendor provenance and scheduling differences.

## 兼容边界 / Compatibility boundary

`include/pmu_protocol.h` 保留原 CW32 线格式常量、命令码与共用数据结构；`device_power.h` 保留应用正在使用的快照和 API。删除旧硬件后端不代表这些兼容标识也能删除。

`include/pmu_protocol.h` retains the CW32 wire-format constants, command codes and
shared structures. `device_power.h` retains application snapshot and API contracts.
Removing the old hardware backend does not remove these compatibility identifiers.

`docs/` 中的协议头、寄存器表、中英文说明和 PDF 是旧 CW32 协议参考资料，不是当前 Metalio 硬件规格。
The protocol headers, register tables, bilingual guides and PDFs under `docs/` are
historical CW32 protocol references, not current Metalio hardware specifications.

## 验证 / Validation

```sh
python3 tools/run_reader_baseline.py metalio_drivers battery battery_filter storage
```

采样与滤波测试执行当前 Metalio 实现；存储组同时覆盖 S31 外设睡眠恢复和应用下电交接。宿主测试不能替代真机电压、充电和关机验证。
Polling and filter tests execute the current Metalio implementation. The storage
suite also covers S31 peripheral sleep restoration and application power-off
handoff. Host tests do not replace device voltage, charging or power-off validation.

## 许可 / License

原有部分为 Apache-2.0，见 [LICENSE](LICENSE)；移植原厂部分保留 [MIT 声明](../board_i2c/LICENSE.Metalio)。
Existing portions use Apache-2.0; adapted vendor material retains the MIT notice linked above.
