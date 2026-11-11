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

## 电量显示 / Battery display

电压补偿采用 BQ27220 的有符号电流：正向充电减压、负向放电加压，内阻按 180 mΩ、补偿封顶 150 mV。CX 状态仍决定充电标志，但不决定补偿方向，避免插拔时两个芯片读数暂时不同步导致反向补偿。日志 `comp` 与实际滤波共用同一换算。
Compensate voltage using signed BQ27220 current: subtract while charging and add while discharging, using 180 milliohms capped at 150 mV. CX still selects the charging flag but not the compensation sign, avoiding reversed compensation when chip readings temporarily disagree during cable changes. Logged `comp` uses the same conversion as the filter.

普通区间每 60 秒最多变化 1%，充放电切换后重新计时；确认充满仍直接对齐 100%。放电目标不高于 14% 时保留每 10 秒最多下降 1%，空电目标允许清掉不足 1% 的尾数，不让死区长期显示 1%。这是显示平滑，不是延长实际续航。
Normal levels move at most 1% every 60 seconds, rearming after charging transitions; confirmed full still aligns directly to 100%. Discharge targets at or below 14% retain 1% steps every ten seconds, and an empty target can clear a sub-1% remainder. Smoothing does not extend physical runtime.

曲线使用负载补偿和 EMA 后的电压，不是静置实测 OCV。4.38 V 满电平台及 4.30/4.20/4.10 V 的 95.5/87.5/79% 保留；它们来自早期满电平台和回充对账。4.05/4.00/3.95 V 的 73.3/69.5/62.9% 保留 2026-11-05 连续清醒段的相对估算，以 4.10 V / 79% 和假定 2050 mAh 为基准，并非绝对电量实测。
The curve uses load-compensated, EMA-filtered voltage, not measured rested OCV. Retain the 4.38 V full plateau and 95.5/87.5/79% at 4.30/4.20/4.10 V from earlier plateau/refill observations. The 73.3/69.5/62.9% targets at 4.05/4.00/3.95 V retain the 2026-11-05 relative awake-discharge estimate, anchored to 4.10 V / 79% and an assumed 2050 mAh capacity, not measured absolute SOC.

3.95–3.30 V 改用 2026-11-11 提供日志的连续放电形状，替代旧低段直线与偏高尾段。保持 3.95 V / 62.9% 和 3.30 V / 0% 两个边界，在两者之间按剩余采样放电量归一，不把日志末行当成真实断电点，也不重新估计额定容量。关键目标如下：
The 3.95–3.30 V range follows the continuous discharge supplied on 2026-11-11, replacing the provisional line and overstated tail. Keep 3.95 V / 62.9% and 3.30 V / 0%, normalizing sampled remaining charge between them without treating the last row as the physical cutoff or estimating rated capacity.

| 补偿/EMA 电压 / Filtered voltage | 显示目标 / Target |
| --- | --- |
| 3.90 / 3.85 / 3.80 V | 55.8 / 50.4 / 40.6% |
| 3.775 / 3.75 / 3.725 V | 32.4 / 22.4 / 16.9% |
| 3.70 / 3.675 / 3.65 V | 13.3 / 10.4 / 5.7% |
| 3.625 / 3.60 / 3.50 V | 4.0 / 3.3 / 1.8% |
| 3.40 / 3.30 V | 0.7 / 0% |

可复核来源：输入日志 SHA-256 `a1a70c633afe1390ba858ea02b1df9fdb901fe189d3eb985066d62e11d5aca6f`，格式 2，`boot=75406354784d6057`、`session=1`、`seq=2..1181`；1,180 条连续清醒记录共 19.684 小时、约 1,660.266 mAh，区间内 `unknown_ms` 不增长，序号无缺失、无新增丢队列/写卡错误。`file_missed=1` 是首条前已发生的文件缺失，区间内保持不变。取第一次下降跨越目标电压的相邻记录，线性插值 `out_uah_est`：3.95 V 为 287037 µAh，3.30 V 为 1645474.917 µAh。每个锚点的千分比为 `round(629 × (1645474.917 − Q(V)) / 1358437.917)`；最终显示仍经过死区和单向限速。
Reproducible source: log SHA-256 `a1a70c633afe1390ba858ea02b1df9fdb901fe189d3eb985066d62e11d5aca6f`, format 2, `boot=75406354784d6057`, `session=1`, `seq=2..1181`. Its 1,180 continuous awake records cover 19.684 hours and about 1660.266 mAh with no growth in unknown time, sequence gaps, queue drops or write errors. `file_missed=1` predates the first row and stays fixed. Linearly interpolate `out_uah_est` at each first descending voltage crossing: 287037 uAh at 3.95 V and 1645474.917 uAh at 3.30 V. Anchor permille is `round(629 × (1645474.917 − Q(V)) / 1358437.917)`; the visible percentage still applies deadband and one-way rate limits.

3.30 V 以下到末行仍记录约 14.8 mAh，保留为显示余量。较早日志有长时间锁屏缺测，后续充电跨重启且未记录确认充满；这些段、校时时钟跳变和芯片 SOC 均不参与本次曲线拟合。单台设备、单次连续放电只能校正该段形状；容量老化、温度、其他负载、充电阶段及新曲线设备体验仍须验证，不能据此宣称绝对电量准确度或续航已提高。
About 14.8 mAh remains recorded below 3.30 V through the last row, retained as display reserve. Earlier logs have long sleep gaps; later charging spans restarts without confirmed full. Those segments, wall-clock jumps and gauge SOC do not enter this fit. One device and one continuous discharge support this segment's shape only; aging, temperature, other loads, charging and the new display still require validation, with no claim of absolute SOC accuracy or improved runtime.

高压近零电流判满需要连续有效采样；超过 5 秒的采样空白、读数失败或明显放电会重新确认，保留硬件 top-off 和原电压门槛。显示算法不修改充电电流、截止电压、终止策略或欠压保护。
High-voltage near-zero-current full confirmation requires continuous valid sampling. Gaps longer than five seconds, read failures or material discharge restart confirmation, retaining hardware top-off and existing voltage gates. Display logic does not change charge current, regulation voltage, termination policy or undervoltage protection.

NVS 百分比带 `soc_ver` 算法版本，当前为 2；旧的无版本、版本 1 和其他不兼容记录不恢复，首次按新曲线估算，不清空其他设置。以后在五分钟周期、充电状态变化、累计变化至少 2%（至少隔一分钟）及正常重启/关机前保存有效值，相同值不重复写。保存失败不记作成功；意外断电仍可能丢失最近变化，启动时仍按电压容差决定是否恢复。
NVS percentages carry algorithm revision 2 in `soc_ver`. Unversioned, revision-1 and other incompatible records are ignored so the first start estimates from the new curve without erasing other settings. Save valid values every five minutes, on charger changes, after at least 2% movement (at least one minute apart), and before normal restart/shutdown; duplicate values skip writes. Failed saves are not cached as successes. Unexpected power loss may still lose recent movement, and boot restoration remains subject to voltage tolerance.

## 电源键采样 / Power-key sampling

主循环启动默认暂停的快采任务，正常页面启用后每轮只读 P17，轮间等待 10 ms；原有 20 ms 按下复查、30 ms 短按门限与 1 s 长按门限保持不变。事件与直读共用互斥锁及原有短按/长按待处理标记，长按优先；快采不执行电量采样、文件写入或绘屏。主循环恢复处理前保留待处理动作，不承诺在硬件推屏中途立即锁屏。
The loop starts an initially paused task which reads only P17 on active pages and waits 10 ms between polls. Existing 20 ms press confirmation, 30 ms short-press and 1 s long-press thresholds remain. Sampling and direct reads share a mutex and the existing short/long pending flags, with long presses taking priority. The task does no battery, file or display work. Pending actions survive until the loop can handle them; locking need not interrupt a hardware refresh already in progress.

`device_power_key_sampler_enable(false)` 等待在途读取结束，丢弃旧动作及当前按住状态的释放；浅睡、下电和 USB/设备独占前必须完成此交接，避免 TCA Input 读操作清掉唤醒中断。嵌套调用按返回的原状态恢复。锁屏保留直读唤醒，解锁先消费唤醒键再恢复快采；任务创建失败保留原轮询，反初始化暂停任务，后续初始化可复用。页面导航暂停快采，下一轮按新页面归属恢复。
`device_power_key_sampler_enable(false)` joins in-flight reads and discards stale actions and the current held press's release. Complete this handoff before sleep, power-off or USB/device-exclusive use so TCA Input reads cannot clear wake interrupts. Nested callers restore the returned previous state. Locks retain direct wake reads and consume the wake press before resuming sampling. Failed task creation retains direct polling; deinit pauses the reusable task. Navigation pauses sampling until the next iteration establishes the new page's ownership.

浅睡专用的 `device_power_take_key_wakeup()` 每次仍读取电源键，但可复用采样起点距今不足 10 ms、且电压/电流/现有充电芯片状态全部有效的电量样本，减少同一轮等待边界内的重复总线访问。样本过期、读取失败、时钟回退或充电维护/恢复已经到期时立即重新采样。活动页的 `device_power_poll()` 不复用；2 秒浅睡定时兜底、充电寄存器策略和欠压阈值不变。减少访问次数不是已测得的节电比例。

The sleep-only `device_power_take_key_wakeup()` still reads POWER on every call, but may reuse a battery sample whose start is less than 10 ms old with valid voltage, current and status from any present charger. This avoids duplicate bus access at the same wait boundary. Expiry, read failure, clock rollback or due charger maintenance/resume forces a fresh sample. Active-page `device_power_poll()` never reuses samples; the two-second sleep timer, charger register policy and undervoltage threshold remain unchanged. Fewer accesses are not a measured power-saving percentage.

## 主机浅睡诊断 / Host light-sleep diagnostics

主机等待入口在 `main/power/sleep.c`。排查构建可开启 `CONFIG_APP_SLEEP_DIAGNOSTICS`（默认关闭）：每次等待只在 RAM 累计，退出并恢复外设后，向 NVS `sleep_diag/last` 覆盖保存一份汇总，并输出 `SLEEP_SUMMARY origin=live`。不逐切片写 Flash/TF，不新增唤醒定时器，定时切片仍为 2 秒；保存失败仅记录 `save_error`，不阻止恢复。

The host wait entry lives in `main/power/sleep.c`. Investigation builds can enable `CONFIG_APP_SLEEP_DIAGNOSTICS` (off by default): each wait accumulates in RAM, then overwrites one NVS `sleep_diag/last` summary after leaving the wait and restoring peripherals, logging `SLEEP_SUMMARY origin=live`. There are no per-slice flash/card writes or extra wake timers; slices remain two seconds. Save failures only set `save_error` and never block recovery.

同一诊断构建启动时读取记录，校验格式后输出 `origin=saved`。这是上一份成功保存的等待记录，**不是本次启动已完成浅睡的证据**；记录的 `elf` 是采集固件 ELF SHA-256 前缀（最多 16 位，长度受 SDK 配置影响），`unix_s` 依赖设备校时，`start_us` 是当次启动的单调时钟。保存失败时旧记录仍可能存在。无记录、读取失败或格式不符时不伪造记录，不清空原 NVS。

The same diagnostic build reads and validates the record at boot, logging `origin=saved`. This is the last successfully saved wait, **not evidence that the current boot has already slept**. `elf` identifies the collecting firmware with an ELF SHA-256 prefix (up to 16 hexadecimal characters, depending on SDK configuration); `unix_s` depends on device clock synchronization, and `start_us` belongs to that boot's monotonic clock. A failed save can leave an older record. Missing, unreadable or invalid records are never fabricated or used to erase NVS.

| 字段 / Fields | 含义 / Meaning |
| --- | --- |
| `attempts`, `ok`, `failed` | 实际浅睡调用次数及返回结果；资源交还失败不计为已睡。/ Actual sleep calls and results; rejected resource handoff is not sleep. |
| `timer`, `gpio`, `other` | 仅统计成功调用的唤醒原因；混合原因分别计数，和可能大于 `ok`。/ Causes from successful calls only; combined causes count separately, so the sum can exceed `ok`. |
| `lt80ms`, `min_call_us`, `max_call_us` | 成功调用的短返回次数和时长范围；电源键及时唤醒也可能短于 80 ms，并非都属异常。/ Short successful returns and duration bounds; prompt key wakes can legitimately be below 80 ms. |
| `ok_call_us`, `rejected_call_us` | SDK 调用的累计停留时间，包含进出开销，不是精确硬件睡眠时间或电流测量。/ Accumulated SDK call residence including entry/exit overhead, not exact hardware sleep time or current. |
| `total_us`, `outside_call_us` | 总等待入口耗时及调用外耗时，含前置准备、轮询与恢复；不含最终保存/汇总日志，不等同于单次唤醒开销。/ Total entry duration and time outside calls, including preparation, polling and recovery; excludes final save/summary logging and is not per-wake overhead. |
| `ble_blocked`, `timer_errors`, `irq_low` | 蓝牙未停栈、定时器配置失败、共享中断清除后仍低的轮询次数。/ Bluetooth teardown block, timer setup failures and polls with shared IRQ still low after clearing. |
| `key_latched`, `key_read_errors`, `unresolved_gpio` | 唤醒后保全的 P17 低、输入读取失败，以及未判明为按键或有效拿起的 GPIO 返回。/ Captured post-wake P17 lows, input read failures and GPIO returns not identified as a key or accepted pickup. |
| `exit` | 0 按键，1 拿起，2 文件任务未归还，3 共享中断持续低，4 定时器连续失败，5 浅睡连续失败。恢复活动页不表示真的按了电源键。/ 0 key, 1 pickup, 2 file jobs busy, 3 stuck IRQ, 4 repeated timer failures, 5 repeated sleep failures. Recovery exits do not prove a physical key press. |

成功 GPIO 唤醒后，先保全实际读到的 TCA P17 低电平，再沿用原按键复查及拿起判断，避免已读到按下却因复查期间松手而丢失；睡眠失败不使用陈旧的唤醒原因，其他共享中断不直接变成电源键。如果按键在第一次读取前已经释放，仍可能无法判明来源，本轮没有消除这个硬件信息缺口，因此不延长定时切片、不改变 IO 方向或拿起设置。

After a successful GPIO wake, preserve an actually observed TCA P17 low before the existing key recheck and pickup decision, avoiding loss when release occurs during rechecking. Rejected sleep never consumes stale wake causes, and unrelated shared interrupts never become keys solely from the GPIO cause. A key released before the first read can still be unidentifiable; this hardware-information gap remains, so this change does not lengthen slices or alter IO directions or pickup settings.

仅有 USB 数据线时，可先连线采集短期行为日志，再拔线等待、主动按电源键唤醒后重连；如果重连丢失串口输出，可在连接稳定后重新启动并采集 `origin=saved`。重连本身不会自动补发，保存尚未完成就复位仍可能丢本次记录。USB 供电/充电与纯电池条件分开记录；串口掉线不能证明低功耗，板载电量计或等待时长也不能证明节电百分比。诊断关闭后不读写这份记录、不输出汇总；恢复正式构建时应关闭本开关及独立的逐次 `KIIKO_SLEEP_DIAG`。

With only a USB cable, first capture short-term connected behavior, then disconnect, wait, wake with POWER and reconnect. If serial output is lost, restart after the connection is stable and capture `origin=saved`. Reconnection alone does not replay it, and a reset before saving finishes can lose the current record. Label USB-powered/charging and battery-only conditions separately: serial loss, gauge readings and wait durations do not establish a power-saving percentage. With diagnostics off, this record is neither read nor written and no summary is emitted; disable this option and the independent per-return `KIIKO_SLEEP_DIAG` for production builds.

## 充电与 RTC / Charging and RTC

CX25601N 请求 1 A，经原厂 80 mA 档位量化为 960 mA，输入限流跟随量化值；VREG 为 4.4 V。轮询按有效电压切换终止，输入存在且空闲/近零电流时补充充电，无输入且有效电压 ≤3.1 V 时按原厂关 BATFET。配置不完整时报告失败，并尽力恢复硬件终止与停充。
CX25601N quantizes the 1 A request to 960 mA using vendor 80 mA steps; the input limit follows that value and VREG is 4.4 V. Polling controls termination from valid voltage, kicks recharge on input-present idle/near-zero-current status and shuts BATFET on valid no-input voltage at or below 3.1 V. Incomplete configuration reports failure and attempts to restore hardware termination and disable charging.

维护使用现有轮询上的非阻塞状态机，恢复充电最早在 200 ms 后的下一次轮询执行，失败继续重试；不更改电量曲线、补偿与滤波。PCF8563 启动清 STOP 和 AF/TF，寄存器存 UTC，非法 BCD、日期和电压失效标志不用于系统校时。
Maintenance uses a nonblocking state machine on existing polling, resuming recharge on the next poll at least 200 ms later and retrying failed restoration. Battery curves, compensation and filtering remain unchanged. PCF8563 startup clears STOP and AF/TF, stores UTC and rejects invalid BCD/calendar dates or voltage-loss flags for system clock updates.

关机与反初始化前恢复硬件充电终止，并完成待恢复的充电使能；失败则交还调用方，不带着临时停充状态断电。
Before power-off or deinit, restore hardware termination and pending charge enable; failure returns to the caller instead of powering off with a temporary charge disable.

原厂来源与调度差异见 [board_i2c](../board_i2c/README.md)。
See board_i2c for vendor provenance and scheduling differences.

## 电量校准日志 / Battery calibration log

设置中的「电量校准日志」默认关闭。开启后使用格式 3，清醒状态每分钟记录新采样及区间统计；有效性、充电开关、输入状态、判满结果、锁屏等待边界、关机/重启请求额外记录。关闭补一条会话结束记录；停止新的芯片 SOC 采样，后台仍尝试交付已产生的记录。写任务不新增睡眠唤醒定时器，欠压保护不等待 TF 或 NVS。
Calibration logging defaults off. Format 3 records awake minute samples and interval statistics, with extra records for validity, charging/input/full transitions, locked-wait boundaries and shutdown/restart requests. Disabling adds an end record and stops new gauge-SOC reads while queued records may still be delivered. The writer adds no sleep wake timer; undervoltage protection never waits for TF or NVS.

串口 `batt-meta` 与卡上 `# battlog` 包含固件版本、ELF SHA-256、编译时间、启动编号、复位原因、曲线散列、状态版本、限速/判满/恢复参数以及启动时读取的 `saved_ver/saved_soc`。同一正式版本的测试包由 ELF 散列区分。按 `boot/session/seq` 分析：每次开启增加 `session`，`seq` 在本次开机内递增。旧格式与格式 3 可以共存，逐行识别 `fmt`；不要把不认识的字段或格式默认为零。同步/关闭失败后重试可能产生同序号重复行，按这个三元组去重；损坏或不完整的行不能参与积分。
Metadata identifies the firmware/ELF/build/boot/reset, curve and state versions, rate/full/restore settings, and the saved version/value read at startup. Group by boot/session and order by sequence; each enable starts a session. Legacy and format-3 rows coexist and must be parsed by their own format. Failed sync/close retries can duplicate a boot/session/sequence; deduplicate by that key and reject malformed/truncated rows.

| 字段 / Field | 含义 / Meaning |
| --- | --- |
| `t`, `up_ms` | 事件的日历秒和本次开机单调毫秒；未校时 `t=-1`。积分只使用单调时间。/ Event wall seconds and boot-monotonic milliseconds; unknown wall time is -1. Integration uses monotonic time only. |
| `sample_t`, `sample_ms`, `read_us` | 最近采样尝试时间和总线读取耗时；普通事件保留旧样本，欠压事件使用真正触发保护的原始读数。/ Latest attempt times and bus-read duration; ordinary events retain the previous sample, while undervoltage events capture actual protection inputs. |
| `write_ms`, `age_ms` | 本次输出开始时间及样本年龄，含队列/重试延迟；不是落盘完成时间。/ Output-start time and sample age including queue/retry delays, not persistence completion. |
| `valid` | 位 1 电压、2 电流、4 CX、8 芯片 SOC。无效数字为 -1；有效 -1 mA 仍须由位图区分。/ Bits 1 voltage, 2 current, 4 CX, 8 gauge SOC; flags distinguish invalid -1 from a real -1 mA. |
| `raw`, `ma`, `cx`, `chg` | 原始 mV、有符号 mA（正充负放）、完整 CX 状态字节、现有充电判断；低三位保留输入编码。/ Raw mV, signed mA, full charger status and existing charging decision, including low-three-bit input code. |
| `done`, `done_src` | 软件判满及来源：0 无、1 CX top-off、2 高压低电流持续判定。不是容量真值。/ Software full decision and source: none, CX top-off, or sustained taper; not capacity ground truth. |
| `full_hold_ms`, `full_gate` | 连续判满窗口已满足时长（封顶 300000 ms，未开始为 -1）及本轮阻断位图，见下文。/ Continuous full-confirmation time, capped at 300000 ms and -1 when inactive, plus blockers below. |
| `comp`, `ocv`, `curve`, `disp` | 负载补偿 mV、EMA mV、曲线目标和显示千分比。欠压紧急快照尚未计算滤波和判满时这些值及 `done` 为 -1；`ocv` 不是静置 OCV。/ Compensation/EMA mV and target/display permille; uncomputed emergency filter/full values are -1, and EMA is not rested OCV. |
| `restore` | 首次有效采样的恢复决策：0 待判定、1 接受保存值、2 无记录/读取失败、3 版本不符、4 值非法、5 超出曲线容差。后续行保留本次启动决定。/ Boot restore: pending, accepted, missing/read error, wrong version, invalid value, or outside tolerance; retained on subsequent samples. |
| `ic_soc`, `ic_age_ms` | 每分钟独立读取的芯片 SOC 及年龄；只读对照，失败为 -1。/ Separate minute gauge-SOC sample and age, comparison only, -1 after failure. |
| `locked`, `lock_ms` | 当前是否锁屏等待及累计时长，包含定时唤醒轮询，不是精确 CPU 睡眠时间。/ Locked wait and cumulative duration including timer polling, not precise CPU sleep. |
| `covered_ms`, `unknown_ms` | 会话内可积分/无覆盖时间；锁屏、无效端点、超过 5 秒的间隔及事件边界不外推容量。覆盖率可用 covered/(covered+unknown)，不含跨启动缺口。/ Eligible/uncovered session time; no extrapolation across locks, invalid endpoints, gaps over 5 seconds or event boundaries. Session coverage excludes inter-boot gaps. |
| `in_uah_est`, `out_uah_est` | 有效清醒区间梯形积分，跨零分开充入/放出，单位 µAh，仍是采样估算。/ Separate charge/discharge trapezoidal estimates in µAh over eligible awake intervals. |
| `win_ms`, `n`, `locked_n` | 区间起点、采样次数、其中锁屏采样次数。首次/周期记录后开新窗口，其他事件携带当前累计窗口，不能重复相加。/ Window start, sample count and locked subset. First/periodic rows reset the window; event rows overlap and must not be added together. |
| `v_n/v_min/v_avg/v_max`, `i_n/i_min/i_avg/i_max` | 各自有效次数、最小值、算术平均和最大值；无有效样本为 -1。均值按采样次数，非时间加权；积分仍按实际时间。含锁屏采样的窗口不能当作睡眠电流均值。/ Valid counts, extrema and sample-weighted means; not time-weighted means or measured sleep averages. |
| `max_gap_ms` | 窗口内相邻采样的最大间隔，包含跨窗口边界的首个间隔。/ Largest adjacent-sample interval, including the first interval crossing a window boundary. |
| `cx_changes/cx_first_ms/cx_last_ms` | 有效 CX 字节切换次数及首末时间；普通充电阶段抖动每分钟最多额外输出一次，输入/充电开关及充电阶段位之外的状态边沿立即输出。/ Charger-byte change count and first/last times; phase chatter is coalesced, input/charging and non-phase status edges remain immediate. |
| `full_resets/full_reset_mask` | 本窗口判满确认被打断的次数和原因并集，短暂尖峰也会保留。/ Window full-confirmation interruptions and combined causes, retaining brief spikes. |
| `low_mask` | 本次放电首次见到原始电压 ≤3500/3400/3300/3200 mV，分别为位 1/2/4/8；要求有效电压、电流和无输入状态，回弹不重复，充电后重置。/ Once-per-discharge low milestones; valid voltage/current and absent input required, reset on charging. |
| `dropped`, `io_errors`, `file_missed` | 采样队列丢弃数、文件/格式失败尝试数、文件积压溢出或格式错误导致的未交付数。暂时无卡但仍缓冲的行不计丢失。/ Producer drops, I/O/format failure attempts, and permanent backlog/format losses; buffered rows are not yet losses. |
| `file_seq`, `checkpoint_errors` | 本行输出前同一启动最后确认写入并同步关闭成功的序号（不保证之前无缺口），以及 NVS 检查点保存失败数。/ Last confirmed file sequence before this output, not a gap-free watermark, and NVS checkpoint failures. |

`full_gate/full_reset_mask` 位图：1 未充电、2 电压无效、4 电流无效、8 电压低于 4360 mV、16 电流 ≥25 mA、32 电流 ≤−25 mA、64 采样空白超过 5 秒或时间倒退。CX 硬件 top-off 优先，阻断位是软件涓流条件的诊断，不覆盖硬件判定。
Full blockers: 1 not charging, 2 invalid voltage, 4 invalid current, 8 below 4360 mV, 16 at least 25 mA, 32 at most −25 mA, 64 sample gap over five seconds or time reversal. These diagnose taper confirmation; CX top-off remains authoritative for the existing algorithm.

`event` 位图保留 1 定期/首次采样、2 开启、4 关闭、8 充电/CX 变化、16 判满变化、32 有效性变化、64 清醒长空白、128 锁屏开始、256 锁屏结束、512 关机请求、1024 重启请求、2048 欠压请求，新增 4096 低电提前点。请求不证明设备已断电；3.1 V 保护仍立即执行，日志可能来不及落盘。提前点也可能由瞬时负载触发，应结合区间极值分析。
The existing event bits 1–2048 retain their meanings; 4096 adds low-voltage milestones. Requests do not prove shutdown. The 3.1 V protection still executes immediately and may precede persistence; low milestones can also reflect transient loads.

### 跨重启检查点 / Cross-boot checkpoints

写任务只在开启日志后读写 NVS `battlog/checkpoint`，约每 5 分钟以及开启/关闭、低电点、关机/重启/欠压请求保存累计快照；判满保存至少间隔 1 分钟。格式、长度和数值异常不继承，失败不影响电量与保护。恢复的是最近成功检查点，不保证是断电前最后样本；写入低电提前点也无法保证意外断电前成功。
The writer uses NVS `battlog/checkpoint` only after logging is enabled, around every five minutes and at enable/end, low milestones and shutdown/restart/undervoltage requests. Full-decision checkpoints are limited to once per minute. Invalid layouts/ranges are rejected; failures never change battery/protection behavior. The last successful checkpoint may precede the final sample.

元信息 `chain` 是日志连续启用链的标识，不是已完成的完整充放电循环。首次开启可继承上次 `active` 检查点：`resume=1`，`prev_boot/prev_session/prev_seq/prev_t` 指向恢复源，`base_*` 为其累计覆盖时间和充放电估算；本行链总量为 `base_* + 本会话累计值`。不能把已包含的旧会话总量再加一遍。显式关闭再开启会新建链。
`chain` identifies a logging chain, not a complete physical charge cycle. A boot's first enable may resume an active checkpoint; `prev_*` identifies that checkpoint and `base_*` holds its totals. Chain estimates equal the base plus current-session totals; do not add the old session again. Explicit disable/re-enable starts a new chain.

`between_boots=unknown` 明确表示上次检查点之后到本次开启之间未连续观测，时间校正、断电和尚未保存的尾段都不能补积分。`base_unknown_ms` 仅包括之前会话内已计量的缺测时间，不包括这个跨启动缺口；不得用这些累计量声称绝对容量或整夜待机耗电。
The interval after the previous checkpoint through the new enable remains unobserved, including unsaved tails, power-off time and clock changes. `base_unknown_ms` excludes that unmeasured inter-boot gap. The chain does not establish absolute capacity or overnight standby consumption.

### 文件交付与边界 / File delivery and limits

保留约 4 MiB 的 `batt.log` 和上一份 `batt.log.1`，旧行继续保留至正常轮转。每条检查写入、刷新、同步和关闭。采样队列与文件积压各有 16 条，优先保留开启/关闭、判满、低电、关机/重启/欠压事件；全是关键事件时仍可能淘汰最旧关键记录，并计数。
Files retain roughly 4 MiB plus one rotated backup. Each write checks flush, sync and close. The producer and file-backlog queues each hold 16 records, preferring lifecycle/full/low/protection events; an all-critical overflow can still evict the oldest critical record and is counted.

存储未就绪或写入失败时保留有限积压，新的串口输出和检查点继续处理；后续通知到来且间隔达到一分钟时重试，挂载恢复或显式 flush 可立即重试一次，不新增重试定时器。`flush` 只有无积压、无在途写入且本次启动未发生记录丢失才成功；单条成功序号不表示无缺口。TF 卸载/格式化仍用原互斥门控等待在途文件 I/O，超时中止存储操作。断电仍可能丢记录或损坏文件。
Storage failures retain bounded backlog while new serial output/checkpoints continue. Subsequent notifications retry at most once a minute; remount or explicit flush permits an immediate retry, without a timer. Flush succeeds only after all pending writes finish with no boot-time record loss. Existing storage ownership gates still join in-flight file I/O before unmount/format. Power loss can still lose records or damage files.

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
python3 tools/run_reader_baseline.py power_key loop metalio_drivers battery battery_filter battery_log battery_log_task storage
```

采样与滤波测试执行当前 Metalio 实现；存储组同时覆盖 S31 外设睡眠恢复和应用下电交接。宿主测试不能替代真机电压、充电和关机验证。
Polling and filter tests execute the current Metalio implementation. The storage
suite also covers S31 peripheral sleep restoration and application power-off
handoff. Host tests do not replace device voltage, charging or power-off validation.

## 许可 / License

原有部分为 Apache-2.0，见 [LICENSE](LICENSE)；移植原厂部分保留 [MIT 声明](../board_i2c/LICENSE.Metalio)。
Existing portions use Apache-2.0; adapted vendor material retains the MIT notice linked above.

时钟锁屏通过 `device_power_read_rtc()` 只读PCF8563的UTC时间；无效日期、掉电标志或总线错误返回失败，不改系统时间、快照或寄存器。清醒绘屏期间可临时开启按键快采，随后用 `device_power_key_sampler_pause_for_sleep()` 等待在途读取并保留待处理按键，再由锁屏等待循环消费；常规独占/切页仍使用会丢弃旧动作的 `device_power_key_sampler_enable(false)`。
The clock reads UTC through `device_power_read_rtc()` without modifying time, snapshots or registers. Its awake scan pauses the key sampler with `device_power_key_sampler_pause_for_sleep()` to retain pending wake input; normal ownership changes keep the existing discard-on-disable API.
