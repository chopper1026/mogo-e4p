# cst836u

ESP-IDF 下的 **CST836U** 自电容触摸 C 驱动（海栎创）。

ESP-IDF C driver for the **CST836U** self-cap touch controller (Hynitron).

面向手持与墨水屏设备：两点报点、轮询 INT、深度睡眠。手册中的待机手势唤醒依赖对应固件，本驱动不实现。

Intended for handheld / e-paper devices: 2-point report, IRQ poll, and deep sleep. Datasheet standby (gesture wake) needs matching firmware and is not in this driver.

组件名 / Registry name: `mindreset/cst836u`

## S31 / FT6336U 检测阈值

Metalio E-INK4 Plus / ESP32-S31 实际使用 FT6336U（I2C `0x38`），沿用本组件 API；下方 CST836U 芯片说明是旧硬件参考。当前板级总线参数以根 README 和 `board_i2c` 为准。
Metalio E-INK4 Plus / ESP32-S31 uses an FT6336U at I2C `0x38` behind this component API. The CST836U description below is legacy hardware reference; use the root README and `board_i2c` for current bus settings.

`idf.py --preview menuconfig` 的 **Touch diagnostics** 提供 `CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT`。自1.0.7起，产品配置 `sdkconfig.defaults` 开启下述V04升级并选择 `100`，保留模组原阈值；Kconfig通用默认值仍为 `100`。此前75%已验证寄存器读回与启动，但钢化膜样机改善不明显，保留用于对照。
**Touch diagnostics** exposes `CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT`. Since 1.0.7, product defaults enable the V04 updater below and select `100` to preserve module thresholds; the generic Kconfig default remains `100`. The previous 75% setting passed register readback and boot but showed little tempered-glass improvement; retain it for comparisons.

| 值 / Value | 行为 / Behavior |
| --- | --- |
| `100`（产品默认 / product default） | 保留模组原值，不增加阈值、模式或型号细分寄存器访问。/ Preserve the module default without extra threshold, mode or chip-variant register access. |
| `75`（对照 / comparison） | 正常模式检测阈值降至复位实际值的 75%，向上取整。/ Reduce the normal-mode detection threshold to 75% of its actual reset value, rounded up. |
| `50` | 降至复位实际值的 50%，向上取整；需要更严格检查误触。/ Reduce it to 50%, rounded up; check false touches more carefully. |

允许范围 `50..100`。降低 `0x80 / ID_G_THGROUP` 是增强弱触摸响应的候选办法，但这是芯片通用阈值，不保证独立作用于盖板三键。回读成功仅证明寄存器接受了数值，不证明本模组三键灵敏度提高。不要把比例理解为成功率或可支持的膜厚。
The allowed range is `50..100`. Lowering `0x80 / ID_G_THGROUP` is a candidate for weak-touch response, but it is a general chip threshold, not a proven cover-key-only control. Readback confirms register acceptance, not increased key sensitivity on this module. The percentage is not a success rate or supported glass thickness.

初始化和每次唤醒均先复位，确认 FT6336U 型号字节 `64/26/02` 和正常工作模式，再读取原值并计算；不采用网上的固定默认值，不写零，不因多次唤醒叠加降低。无法识别或初始读取失败时保留原行为；写入或读回失败则恢复原值并验证，无法确认恢复时初始化/唤醒返回错误，停止向上层提供触点。阈值调校本身不写触摸芯片 Flash、NVS 或校准数据；下面的独立V04开关才控制固件升级。
Initialization and each wake reset first, confirm FT6336U identity bytes `64/26/02` and normal mode, then scale the actual value. No assumed default, zero threshold or compounded wake reductions. Unknown identity/mode or failed initial reads preserve existing behavior. Failed writes/readback restore and verify the original; unverified recovery fails initialization/wake and suppresses touch delivery. Threshold tuning itself writes no controller Flash, NVS or calibration data; the separate V04 switch below controls firmware updates.

寄存器依据：[FT6336GU 外部寄存器表](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/Ft6336GU_Firmware%20%E5%A4%96%E9%83%A8%E5%AF%84%E5%AD%98%E5%99%A8_20151112-%20EN.xlsx)，2015-11-12。表中 `0x80` 是正常模式可读写触摸阈值；`0xA9` 明确属于量产测试，不纳入阈值实现；阈值调校不进入工厂、自动校准或升级模式。
Register reference: the 2015-11-12 FT6336GU external register table linked above defines normal-mode read/write threshold `0x80`; `0xA9` is explicitly for production testing and excluded. Threshold tuning enters no factory, automatic calibration or upgrade mode.

复跑软件边界检查：`python3 tools/run_reader_baseline.py metalio_drivers`。它编译真实驱动的100%、75% 和 50% 路径，覆盖识别、取整、零值、复位/唤醒、部分写入、读回和恢复失败；不是电容检测测试。
Run `python3 tools/run_reader_baseline.py metalio_drivers` to compile the actual 100%, 75% and 50% driver paths and check identity, rounding, zero values, reset/wake, partial writes, readback and rollback failures. This is not a capacitive hardware test.

扩展样机对照从 `100` 开始，再逐档降低；同一厚膜每键至少 50 次，记录最终响应、重复/错键、空闲误触和屏幕操作，分别检查电池/充电、冷启动/唤醒和刷屏期间。出现误触就重新构建并刷入 `100` 配置。现有用户反馈不替代上述量化和多机验证；设备操作仍需相应授权。
For additional hardware comparisons, start at `100`, then reduce one step at a time. On the same thick protector, try each key at least 50 times and record actions, repeats/wrong keys, idle false touches and screen operations under battery/charging, cold boot/wake and display refresh. Rebuild and flash with `100` if false touches occur. Existing user feedback does not replace quantitative or multi-device validation; device operations still require authorization.

## S31 / FT6336U V04 开机升级

`CONFIG_TOUCH_FT6336U_UPGRADE_ON_BOOT` 的Kconfig通用默认关闭，1.0.7产品配置明确开启，仅用于 LT0470GF0430M_V1.1、684×1216、FT6336U 模组。开启后才编译升级器并嵌入 [V04原厂镜像](firmware/README.md)。它向独立触控芯片写入持久固件；回退 ESP 应用或关闭此选项不会回退触控固件，100%也只是保留当前触控固件的复位阈值。
The generic Kconfig default for `CONFIG_TOUCH_FT6336U_UPGRADE_ON_BOOT` is off; product defaults explicitly enable it in 1.0.7 for the named module. Enabling it builds the updater and embeds the linked vendor V04 image. This persistently updates the separate controller; reverting the ESP app or disabling the option does not revert the controller. A 100% threshold preserves the currently installed controller firmware's reset value.

升级在 `cst836u_init()` 硬复位之后、普通模式配置和触摸采样之前执行，整段持有共享I2C递归锁。先校验完整镜像，再读取应用ID `64/26/02` 与版本。已为V04跳过，已识别的更高版本保留，明确的其他芯片拒绝写入。应用失效时允许原厂的Boot识别回退；仅Boot ID `79/1C`通过后擦除。芯片ID不能代替模组型号确认。
The updater runs after the init hardware reset and before normal configuration/sampling, holding the shared recursive I2C lock throughout. It validates the image, reads application IDs `64/26/02` and version, skips V04, retains identified newer versions and rejects identified other chips. Invalid applications use the vendor boot fallback; erasure requires boot ID `79/1C`. Controller IDs do not identify the panel module.

每包最多32字节并核对状态，最后校验应用XOR、退出Boot并确认芯片ID和版本。失败尝试退出Boot，最多完整重试一次；第二轮即使读到V04也整片重写，以免部分写入被误判成功。两轮后失败则触摸初始化返回错误，不交付触点；本次开机再次创建触摸驱动不会重复升级。`cst836u_wake()` 只恢复普通触摸，不进入升级器。
Packets contain at most 32 bytes with status checks, followed by application XOR, boot exit and ID/version verification. Failure requests boot exit and retries once; the second attempt rewrites a reported V04 to avoid accepting a partial write. Final failure stops touch initialization. Recreating the driver cannot repeat an upgrade in the same boot, and wake never invokes the updater.

本地对照使用根目录 `sdkconfig.touch-v04` 叠加产品配置：升级开启、阈值100%、触摸诊断关闭。版本沿用当前正式版号，构建产物放在忽略目录：
For a local comparison, load `sdkconfig.touch-v04` after product defaults: updater on, threshold 100%, touch diagnostics off. Retain the current release version and keep outputs ignored:

```sh
idf.py --preview -B build/touch-v04-product \
  -DSDKCONFIG="$PWD/build/touch-v04-product/sdkconfig" \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.touch-v04" \
  set-target esp32s31
idf.py --preview -B build/touch-v04-product build
python3 tools/flash_fast.py prepare --build-dir build/touch-v04-product
```

`prepare`只登记本机构建，不打开串口。设备刷入需要单独的明确指令；实际升级期间保持稳定供电，首次启动会增加I2C升级时间。成功日志为 `touch_upgrade: updated and verified V04`，随后驱动应报告 `FW=0x04`；已有V04记录跳过，更高版本记录保留。构建和宿主测试不证明钢化膜三键改善、真实时序或断电恢复；应按上方每键50次的样机对照补验。
Preparation only registers the build and opens no serial port. Flashing requires a separate explicit instruction; maintain stable power during the controller update and allow extra first-boot time. Successful logs include `updated and verified V04` and driver `FW=0x04`; current/newer versions log skip/retention. Builds and host tests do not establish tempered-glass sensitivity, physical timing or power-loss recovery; use the per-key comparisons described above.

V04+100%配置已在一台设备完成真实升级、版本读回和首页启动，用户确认钢化膜下底部三键“效果很好”。这是单机主观反馈；尚未完成每键50次统计、多机、误触、重复冷启动/唤醒和断电恢复验证。
The V04/100% configuration completed a physical upgrade, version readback and Home boot on one device; the user reported good bottom-key response with tempered glass. This is subjective single-device feedback, without per-key 50-trial statistics, multi-device, false-touch, repeated cold-boot/wake or power-loss validation.

软件故障注入入口为 `python3 tools/run_reader_baseline.py touch_upgrade metalio_drivers`：编译真实升级协议、传输适配及驱动，验证镜像、版本门控、型号拒绝、Boot回退、包范围、失败重试、总线释放及唤醒不升级。原驱动回归同时覆盖关闭升级的配置路径。
The stated test entry compiles the real protocol, transport and driver to exercise image/version/identity guards, boot recovery, packet bounds, retries, lock release and no wake upgrades. Existing driver regressions also cover the updater-disabled configuration.

## 能力 / Features

- Handle：调用方传入已有 `i2c_master` 总线、7 位地址、可选 INT / RST 脚。/ Handle-based API: pass an existing `i2c_master` bus, 7-bit address, optional INT / RST GPIO.
- 上电读取 `0xA6`：固件 / 编号 / 模组 / 项目 / 型号。/ Read FW / ID / module / project / type at `0xA6`.
- 从 `0x00` 解析两点（按下 / 抬起 / 移动）。/ Two-point parse from register `0x00` (down / up / move).
- 本驱动实现的手册工作模式：/ Work modes from the datasheet that this firmware actually implements:
  - 动态报点 `0xFE00`，典型 **&lt; 5 mA**。/ Dynamic report `0xFE00`, typical **&lt; 5 mA**.
  - 深度睡眠 `0xA503`，典型 **10 µA**；不再应答 I2C，RST 后重新写入 `0xFE00`。/ Deep sleep `0xA503`, typical **10 µA**; I2C stops, wake with RST then `0xFE00`.
- RST 可以是本机 GPIO，也可以是 `reset_fn`（位于 IO 扩展上）。/ RST via a native GPIO or a `reset_fn` (IO expander).
- IRQ 开漏、低有效。/ IRQ is open-drain, active low.

## 硬件 / Hardware

| 项目 / Item | 规格 / Spec |
|---|---|
| 芯片 / Part | CST836U（海栎创）/ CST836U (Hynitron) |
| 总线 / Bus | I2C，400 kHz |
| 地址 / Address | `0x15`（写 `0x2A`，读 `0x2B`）/ `0x15` (write `0x2A`, read `0x2B`) |
| INT | 任意 GPIO；开漏低有效，10 kΩ 上拉 / Any GPIO; open-drain, active low, 10 kΩ pull-up |
| RST | 任意 GPIO；在扩展口上则用 `reset_fn` / Any GPIO, or `reset_fn` if the line sits on an expander |
| 供电 / Supply | 2.8 V ~ 3.6 V |

手册还定义了待机模式（约 1.5 mA，50 Hz 扫描 + 手势 IRQ）。许多模组的出厂固件并未实现。

Datasheet standby (~1.5 mA, 50 Hz scan + gesture IRQ) is a third mode. Stock firmware on many modules does not implement it.

## 安装 / Install

```yaml
dependencies:
  mindreset/cst836u: "^1.0.0"
```

或：/ Or:

```bash
idf.py add-dependency "mindreset/cst836u^1.0.0"
```

## 用法 / Quick start

```c
#include "cst836u.h"

cst836u_handle_t tp = NULL;
cst836u_hw_t hw = CST836U_HW_DEFAULT();
hw.bus = i2c_bus;
hw.int_gpio = GPIO_NUM_43;
hw.rst_gpio = GPIO_NUM_NC;   // 或本机 RST 脚。/ Or a native RST pin.
hw.reset_fn = board_tp_reset; // rst_gpio 为 NC 时用。/ Optional, used when rst_gpio is NC.

if (cst836u_create(&hw, &tp) == ESP_OK) {
    cst836u_touch_t t;
    cst836u_read(tp, &t);
}

// 主机休眠：约 10 µA，直到 RST。/ Host suspend: ~10 µA until RST.
cst836u_set_mode(tp, CST836U_MODE_DEEPSLEEP);
// ...
cst836u_wake(tp);
```

扩展口复位：

IO-expander reset:

```c
static esp_err_t board_tp_reset(void) {
    expander_set_rst(0);
    vTaskDelay(pdMS_TO_TICKS(10));
    return expander_set_rst(1);
}
```

## 接口分组 / API groups

| 分组 / Group | 函数 / Functions |
|---|---|
| 生命周期 / Lifecycle | `cst836u_create`, `cst836u_del` |
| 读数 / Read | `cst836u_read`, `cst836u_get_info` |
| 电源 / Power | `cst836u_set_mode`, `cst836u_get_mode`, `cst836u_reset`, `cst836u_wake` |
| 中断 / IRQ | `cst836u_int_asserted`, `cst836u_int_level`, `cst836u_int_gpio` |

详见 [`include/cst836u.h`](include/cst836u.h)。

See [`include/cst836u.h`](include/cst836u.h).

## 示例 / Example

[`examples/touch_read`](examples/touch_read) 初始化 I2C 并输出触点坐标；接有 RST 时会演示睡眠与唤醒。引脚在 `idf.py menuconfig` 中配置。

[`examples/touch_read`](examples/touch_read) brings up I2C, logs points, and (if RST is wired) sleeps then wakes. Pins are in `idf.py menuconfig`.

## 许可 / License

既有CST836U驱动采用Apache-2.0；Metalio适配、V04协议及固件来源见[第三方声明](../../THIRD_PARTY_NOTICES.md)与[镜像来源](firmware/README.md)，保留原厂MIT声明。
The original CST836U driver uses Apache-2.0. See the linked notices and image provenance for Metalio adaptations, V04 protocol and firmware, retaining the vendor MIT notice.
