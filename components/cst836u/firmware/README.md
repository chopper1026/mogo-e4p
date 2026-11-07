# FT6336U V04 固件来源 / Firmware provenance

固件与升级协议固定来自 [Metalio 原厂提交 874538eb54f7f7eb593cd40cd1ba6dc8497226e7](https://github.com/CloudZao/Metalio-E-INK4-Plus/commit/874538eb54f7f7eb593cd40cd1ba6dc8497226e7)，原路径为 `main/boards/metalio-e-ink4-plus/display/firmware/ft6336u_v04_app.bin`，逐字节保留。上游说明中的供应商文件名是 `LT0470GF0430M_V1.1_FT6336U_684x1216_FT6336U_V04_20261106_app.bin`。
The firmware and upgrade protocol are pinned to the Metalio commit above. The binary is preserved byte for byte from the stated upstream path and supplier filename.

| 项目 / Field | 值 / Value |
| --- | --- |
| 适用模组 / Module | Metalio E-INK4 Plus，LT0470GF0430M_V1.1，684×1216，FT6336U |
| 文件 / File | `ft6336u_v04_app.bin`，47,176 bytes |
| 实际应用写入 / Application written | 文件前47,096字节；剩余80字节只参与文件校验 / First 47,096 bytes; the remaining 80 bytes participate only in file validation |
| 版本 / Version | `0x04`（文件偏移 `0x10A`） |
| 应用 XOR / Application XOR | `0xFA` |
| 文件 CRC32 / File CRC32 | `0xAFF8E72B` |
| 文件 SHA-256 / File SHA-256 | `60a660a4f80bf7aa8000a2c597f83620ea9b815bec1db31be2c8002cce1ee089` |

上游仓库采用 MIT，完整声明保留于 [LICENSE.Metalio](../../board_i2c/LICENSE.Metalio)，版权为 Shenzhen Xinzhi Future Technology Co., Ltd. 与 Project Contributors（2025）。上游未提供这个触控二进制的算法源码或参数定义，不能把其内部数值当作已知的三键专用阈值。
The upstream repository carries the linked MIT notice and the stated 2025 copyrights. It supplies no algorithm source or parameter definitions for this controller binary; its numeric fields are not documented cover-key thresholds.

在 [驱动说明](../README.md) 中查看启用、验证和持久升级边界。该文件由 ESP 应用通过 I2C 写入独立触控芯片，不能交给 esptool，也不能用于其他面板。芯片 ID 与 Boot ID 仅验证控制器，不验证玻璃面板型号。
See the driver contract for activation, validation and persistent-update boundaries. The ESP application transfers this file over I2C to the separate controller; never pass it to esptool or use it on other panels. Chip/boot IDs identify the controller, not the glass module.
