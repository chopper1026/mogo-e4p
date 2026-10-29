# board_support

Metalio E-INK4 Plus（ESP32-S31）的板级支持：共享 I2C、epdiy 板定义、屏电源轨、TF 卡、触摸与加速度计装配，以及静默蜂鸣器接口。芯片驱动由独立组件提供。

Board support for Metalio E-INK4 Plus (ESP32-S31): shared I2C, the epdiy board definition,
screen rails, card storage, touch/accelerometer assembly and a silent buzzer interface.
Chip drivers live in separate components.

本地组件名为 `board_support`；上游来源为 `mindreset/read_pico`，保留其版权、许可和来源地址。
The local component is `board_support`, derived from `mindreset/read_pico`; retain its copyright, license and source URLs.

## 做什么 / What it does

- `board_init()`：初始化面板、探测 TF 卡、检查加速度计后使其掉电、启动 PMU 主机与触摸控制器。外设失败只清除对应的 `*_ready` 标志；仅面板失败视为致命。/ Brings up the panel, probes the card, checks the accelerometer and powers it back down, starts the PMU host and the touch controller. A failing peripheral only clears its `*_ready` flag; only the panel failing is fatal.
- `board_get_status()` / `board_get_ioe_status()`：扩展口引脚、电源轨状态与 SY7636A 寄存器的一份快照，供电源页和 IO 页使用。/ One snapshot of expander pins, rail state and the SY7636A registers, for the power and IO pages.
- 电源轨：`board_rails_on()`、`board_toggle_vcomctl()`，以及 epdiy 板级钩子。/ Rails: `board_rails_on()`, `board_toggle_vcomctl()`, plus the epdiy board hooks.
- 加速度计轴映射：芯片坐标 → 设备坐标（屏朝上为 +Z）。/ Accelerometer axis mapping: chip → device coordinates (screen up is +Z).
- `board_sd_*`：非阻塞的探卡、挂载、格式化与容量查询。/ Non-blocking card probe, mount, format, capacity.
- `board_buzzer_tone()`：S31 使用 `board_buzzer_stub.c` 的静默实现。/ S31 selects the silent implementation in `board_buzzer_stub.c`.
- `board_metalio_e4p.c`：产品板级实现；`board_legacy_s3.c`、`board_buzzer_legacy_s3.c` 与 `board_flash_hpm_legacy_s3.c` 保留旧硬件实现，均不参与 S31 产品构建。/ Product board implementation; the three `legacy_s3` files retain old hardware implementations outside the S31 product build.

## 引脚 / Pins

通用配置入口仍为 `idf.py menuconfig` → *Read Pico board configuration*。`CONFIG_BOARD_*` 使用 `sdkconfig.rename` 迁移旧配置值；S31 实际引脚以 `board_metalio_e4p.c`、`board_sd.c` 与根 README 为准。

The generic menu remains `idf.py menuconfig` → *Read Pico board configuration*.
`sdkconfig.rename` migrates existing values to `CONFIG_BOARD_*`; actual S31 pins
are defined by `board_metalio_e4p.c`, `board_sd.c` and the root README.

## 依赖 / Dependencies

`epdiy`、`sy7636a`、`fca9555`、`cst836u`、`sc7a20h`、`device_power`、`e0470_epaper_waveform`。

`epdiy`, `sy7636a`, `fca9555`, `cst836u`, `sc7a20h`, `device_power`,
`e0470_epaper_waveform`.

## 许可 / License

Apache-2.0，见 [LICENSE](LICENSE)。

Apache-2.0, see [LICENSE](LICENSE).
