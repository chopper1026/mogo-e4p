# mogo-e4p

> 当前开发入口：[上手指南](docs/ONBOARDING.md) · [代码结构与依赖边界](docs/ARCHITECTURE.md) · [测试与工具](tools/README.md)。下方阶段表保留移植历史；当前配置以构建清单与 sdkconfig.defaults 为准。

把 **Mogo**（ESP32-S3 阅读固件）移植到 **Metalio E-INK4 Plus**（ESP32-S31，4.7" 1216×684 并口墨水屏）的工作区。

当前产品构建仅支持 **ESP32-S31 / ESP-IDF v6.1 preview**；历史移植与设备验证记录见下文，当前目录职责见架构说明。

## 先看这个

完整方案在 [移植适配方案.md](./移植适配方案.md)，包含：

- 两块板的硬件/总线/电源/触摸/存储差异对照
- 推荐架构：Mogo 应用层 + Metalio 板级驱动 + 合并 epdiy S31 后端
- EPD、触摸、PMU、SD、USB、OTA 的逐项适配设计
- 文件级改造清单、M0–M8 分阶段计划、验收与风险清单

## 已完成（代码级）

| 阶段 | 内容 | 状态 |
|---|---|---|
| M0 | `esp32s31` target、16 MB / 5 MB OTA 分区、S3 专属代码隔离、RISC-V 编译 | ✅ 干净构建通过，镜像 ≈ 4.77 MB |
| M1 | epdiy 合并 Metalio S31 LCD IOMUX / AXI-GDMA / RMT 后端，保留 Mogo 的 `epd_lcd_set_line_timing` 等时序 API | ✅ 编译通过 |
| M1 | Metalio 板级 BSP：TPS65185 + TCA9555 + 8-bit 总线（GPIO8–15，20 MHz）+ `EpdBoardDefinition` | ✅ 编译通过 |
| M1 | 开机 EPD 测试图案（`CONFIG_MOGO_EPD_TEST_PATTERN`）：灰阶条、棋盘、细线、四角、中心圆、边框、20 次 GL16 局刷 | ✅ 已接入，默认开启 |
| M3 | FT6336U 触摸驱动，沿用 `cst836u_*` API；普通触点逆旋转 + 盖板三键归一化 | ✅ 编译通过 |
| M4 | SDMMC 4-bit（CLK24 / CMD25 / D0–D3 20–23）；USB MSC 同步改 4-bit + ANA_SW | ✅ 编译通过 |
| M5 | `read_pico_pmu_*` 兼容层：BQ27220 电量、CX25601N 充电、PCF8563 RTC、TCA P17 电源键、NVS VCOM | ✅ 编译通过 |
| M6 | OTA 分区地址与芯片 ID 更新；在线升级 board/layout 改为新板（旧升级源自然失效） | ✅ 编译通过 |
| 实机 | 2026-10-19 烧录并启动：TCA9555 0x20、TPS65185 REVID 0x65、FT6336U ID 0x64、SD 4-bit 挂载 15200 MB、BQ/CX/RTC 全部在线；测试图案 GC16 成功，UI 进入首页 | ✅ 启动通过 |
| 实机 | 触摸：FT6336U 已按竖屏逻辑坐标上报，改为恒等映射；普通触点和盖板三键均可命中 | ✅ |
| 实机 | 修复设置页/水波纹路径死锁：prefill 127 行超过 8-bit 行队列 62 个可用槽，现按队列容量截断 | ✅ |

## 尚未验证 / 下一步

1. **实机 M1**：烧写后先看测试图案，确认灰阶、方向、1 px 线、边框、GC16 耗时；标定 VCOM（默认 1200 mV）。
2. **实机 M3**：标定 FT6336U 四角/中心与盖板三键；当前映射按 Metalio 实测 raw 值写入，可能需要微调。
3. **实机 M4**：确认 4-bit SD 在 40 MHz 下稳定；USB MSC 导出/收回。
4. **实机 M5**：确认 BQ27220/CX25601N/PCF8563 地址与读数；软睡/关机仍走 `main/sleep.c` 的降级路径，PMU shim 的 `report_sleep` 返回不支持。
5. **M7**：RISC-V C stub 比 S3 PIE 慢，实测翻页/水波纹耗时后再优化热路径。
6. 测试完成后关闭 `CONFIG_MOGO_EPD_TEST_PATTERN`。

## 构建

```bash
. ~/esp/esp-idf/export.sh
idf.py --preview set-target esp32s31   # 首次
idf.py --preview build
```

ESP32-S31 在 IDF 6.1 中仍是 preview，`idf.py` 必须带 `--preview`。

产品与 OTA 使用 `sdkconfig.defaults`（flash 80 MHz、Octal PSRAM 200 MHz）。普通 CI 使用相同产品配置，再加载 `sdkconfig.ci` 的编译警告叠加项：
Product and OTA builds use `sdkconfig.defaults` (80 MHz flash, 200 MHz octal PSRAM). CI loads the same product defaults followed by the warning overlay:

```bash
idf.py --preview -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" set-target esp32s31
idf.py --preview -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" build
python3 tools/run_reader_baseline.py
```

宿主阅读回归保留 ASan/UBSan，覆盖诊断、预绘、背景、主循环、书籍、显示、导航与水波纹；配置和测量说明见 [阅读性能测量](docs/READER_PERFORMANCE.md)。CI 构建不能替代真机验收。
Host reader regressions retain ASan/UBSan across diagnostics, prefetch, backgrounds, the event loop, books, display, navigation and ripple effects. See the performance guide for measurement setup; CI builds do not replace device acceptance.

## 关键文件

- `components/epdiy/src/output_lcd/lcd_driver.c`：S31 LCD/GPU/RMT 后端（来自 Metalio 已验证实现）+ Mogo 行时序。
- `components/read_pico/board_metalio_e4p.c`：Metalio 板级 BSP（TPS/TCA/8-bit 总线）。
- `components/epdiy/src/epdiy_riscv_ve_stub.c`：RISC-V 替代 Xtensa PIE 的三个热路径函数。
- `components/cst836u/cst836u.c`：S31 下走 FT6336U，保留 CST836U API。
- `components/read_pico_pmu/read_pico_pmu_metalio.c`：BQ27220/CX25601N/PCF8563/TCA 电源键兼容层。
- `main/epd_test.c`：M1 面板测试图案。

## 代码边界 / Code boundaries

产品页面由 `main/CMakeLists.txt` 和 `main/app/app_registry.c` 定义。上游未参与产品构建的硬件演示页、内置演示正文及其 continuous_du 组件已移除；当前阅读翻页、水波纹、快捷栏、蓝牙、前光、震动、存储、升级与出厂流程保留。S31 使用静默蜂鸣器接口，不依赖 PWM 音频组件。
Product pages are defined by `main/CMakeLists.txt` and `main/app/app_registry.c`. Uncompiled upstream hardware demos, their sample text and continuous_du component have been removed. Reader turns, ripple transitions, quick controls, Bluetooth, frontlight, haptics, storage, updates and factory flows remain. S31 keeps a silent buzzer interface without the PWM audio dependency.

旧芯片名称不等于无用代码：`cst836u_*` 在 S31 驱动 FT6336U，`fca9555_*` 驱动 TCA9555，`read_pico_pmu_*` 包装电量、充电、RTC 与电源键。普通 CI 与 OTA 均验证 S31；未使用的旧板硬件实现应按依赖边界逐步清理，不能整目录删除。
Legacy chip names can denote active compatibility APIs: `cst836u_*` drives FT6336U on S31, `fca9555_*` drives TCA9555, and `read_pico_pmu_*` wraps battery, charging, RTC and power keys. Both CI and OTA build S31; prune old-board implementations along verified dependency boundaries.

产品构建仅接受 `esp32s31`，直接选择 Metalio 板级、电源兼容层和静默蜂鸣器实现；旧 S3 后端与 HPM 覆盖不再由产品构建选择。部分旧源码仍供宿主测试和离线工具使用，删除前需先迁移这些消费者。
Product builds accept only `esp32s31` and directly select the Metalio board, power compatibility layer and silent buzzer. Legacy S3 backends and HPM overrides are no longer product build choices. Some old sources still support host tests and offline tools; migrate those consumers before removing the files.

### 联网升级 / Online updates

设备默认从 `https://mogo.mantake.shop/api/v2/ota/devices/mogo-e4p/update.json` 获取 stable 升级清单，只接受该平台 `/files/firmware/ota/` 下的 HTTPS 镜像（以及已有的 GitHub 来源和显式配置的额外来源）。平台需发布 E4P 对应的 app 镜像，清单的 project、board、layout、版本、大小和 SHA-256 必须匹配设备；旧板 GitHub 清单不兼容 E4P。已有 `sdkconfig` 会保留旧设置，需通过 menuconfig 更新 Primary manifest URL 或重新生成配置。更改源不会更新已安装的固件，需要先刷入包含新配置的构建。

The device reads the stable E4P OTA feed from the URL above and accepts HTTPS images under the platform OTA path, the existing GitHub origins, or an explicitly configured extra origin. Publish an E4P app image with matching metadata and SHA-256. The legacy GitHub feed is not compatible with E4P. Existing `sdkconfig` values persist: update the primary URL in menuconfig or regenerate the configuration. Devices already installed need a build containing the new configuration before they can use the new source.
