# mogo-e4p

把 **Mogo**（ESP32-S3 阅读固件）移植到 **Metalio E-INK4 Plus**（ESP32-S31，4.7" 1216×684 并口墨水屏）的工作区。

当前状态：**移植工程已建立，M0–M5 的代码骨架完成，`esp32s31` 干净构建通过；等待实机验证。**

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

## 关键文件

- `components/epdiy/src/output_lcd/lcd_driver.c`：S31 LCD/GPU/RMT 后端（来自 Metalio 已验证实现）+ Mogo 行时序。
- `components/read_pico/board_metalio_e4p.c`：Metalio 板级 BSP（TPS/TCA/8-bit 总线）。
- `components/epdiy/src/epdiy_riscv_ve_stub.c`：RISC-V 替代 Xtensa PIE 的三个热路径函数。
- `components/cst836u/cst836u.c`：S31 下走 FT6336U，保留 CST836U API。
- `components/read_pico_pmu/read_pico_pmu_metalio.c`：BQ27220/CX25601N/PCF8563/TCA 电源键兼容层。
- `main/epd_test.c`：M1 面板测试图案。
