# Chakra Petch clock digits

`ChakraPetch-Bold.ttf` is the unmodified Google Fonts source for the approved clock numerals:
https://github.com/google/fonts/blob/main/ofl/chakrapetch/ChakraPetch-Bold.ttf

Copyright 2018 The Chakra Petch Project Authors. The complete SIL Open Font License 1.1 is in [OFL.txt](OFL.txt).

Source SHA-256: `65fbf76d95651697275e19db4d717c0e95a789ddd3476478b05292104db278a0`.

`python3 tools/generate_clock_digits.py` rasterizes 0–9 at 224 pixels and generates `main/assets/clock_digits.h`. The firmware includes only those 4bpp run-encoded masks, not the TTF. `--check` verifies the committed data. Rendering uses Pillow/FreeType; the native UI regression independently compares the masks against the source font at the same size.

数字采用固定145px字格、6px间距，墨迹各自居中，上下两行对齐；保留原始字号，不放大固件160px字形，也不改字体引擎上限。TTF仅供离线生成，设备不加载该文件。
