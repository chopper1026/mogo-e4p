<!--
SPDX-FileCopyrightText: 2026 chopper1026
SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
-->

# 阅读手账像素图 / Reading Journal Scenes

六幅70×54场景及阅读角空景，设备以3倍整数绘制。PNG为批准的原图；`python3 tools/generate_reading_scenes.py`生成`main/assets/reading_scenes.h`，`--check`验证可复现性。

Six approved 70x54 scenes and an empty reading corner are drawn at integer 3x scale. PNGs are the source; regenerate the header with the command above, or verify using `--check`.

人物、树木、门框、怪物等图块来自 [Kenney 1-Bit Pack 1.2](https://kenney.nl/assets/1-bit-pack)，第三方部分为CC0，原许可证见`Kenney-License.txt`。`scenes.json`逐项记录原图块、翻转/缩放及补绘；原包SHA-256为`129a0e74e1dc9091a769d5118be5c88780484c7a3771ee33ac9ef0df02583a9a`。

Characters, trees, doorways and other tiles come from Kenney's CC0 pack; retain its original license. The manifest distinguishes reused tiles from custom terrain, props and compositions. The full compositions are neither unmodified Kenney images nor wholly original works; CC0 applies only to the third-party portions. Newly added project material follows the root licensing policy.

PNG、`scenes.json`和生成表中明确列出的补绘与构图，版权为2026 chopper1026，适用`LicenseRef-Kiikoread-Personal-Study-1.0`。Kenney图块仍适用CC0-1.0；混合资源的双许可表达式按这两个部分分别适用，不改变第三方授权。原许可证保留原始字节与换行。

Custom additions and compositions identified in the PNGs, `scenes.json` and generated table are copyright 2026 chopper1026 under `LicenseRef-Kiikoread-Personal-Study-1.0`. Kenney tiles retain CC0-1.0; the combined asset expression applies each license to its respective portions without relicensing third-party material. The original license keeps its original bytes and line endings.
