# 日历资源来源 / Calendar resource provenance

| 输入 / Input | 固定来源 / Pinned source | 许可 / License |
| --- | --- | --- |
| `lunar-javascript/lunar.cjs` | npm `lunar-javascript@1.7.7`, upstream `6tail/lunar-javascript`; SHA-256 `9750324bfe1aa63c146f8c72b1143df924466c11c8a5277d7d9225c541a18aaa` | [MIT](lunar-javascript/LICENSE), Copyright (c) 2018 6tail |
| `libre-bodoni/digits.ttf` | Libre Bodoni 2.005, Google Fonts `ofl/librebodoni/LibreBodoni[wght].ttf`, 2026-11-10取得；固定wght=700后仅保留0–9，保留字体内部版权；subset SHA-256 `63c030ddb31c5e9c5dd32d5793a12d494f4ab5ddd456d7bf99a7130b06a57f94` | [SIL OFL 1.1](libre-bodoni/OFL.txt), The Libre Bodoni Project Authors |

两份输入已随仓库保存；重生成不依赖在线最新版。`generate_calendar_data.cjs` 与 `generate_calendar_digits.py` 分别生成固件只读表和数字位图。完整OFL字体不是固件运行时依赖；子集与生成字形继续受OFL约束。
Both inputs are vendored for offline reproducibility. The generators produce firmware tables and numeral bitmaps. The complete font is not a runtime dependency; the subset and generated glyphs retain OFL terms.
