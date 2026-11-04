#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：将批准的70x54像素图打包为固定4bpp常量。/ English: Pack approved 70x54 scenes as fixed 4bpp constants."""
import argparse
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("reading-corner", "valley-hike", "forest-battle", "ruin-expedition", "ocean-dive", "lakeside-camp", "reading-corner-empty")

def generated():
    lines = [
        "/*",
        " * SPDX-FileCopyrightText: 2026 chopper1026 (custom additions and compositions)",
        " * SPDX-License-Identifier: CC0-1.0 AND LicenseRef-Kiikoread-Personal-Study-1.0",
        " *",
        " * 中文：自动生成，勿手改；Kenney图块为CC0，新增部分许可见reading-scenes/README.md。",
        " * English: Generated; do not edit. Kenney tiles are CC0; see reading-scenes/README.md for added material.",
        " */",
        "#pragma once", "#include <stdint.h>", "static const uint8_t reading_scene_pixels[7][1890] = {",
    ]
    for name in NAMES:
        with Image.open(ROOT / "main/assets/reading-scenes" / (name + ".png")) as image:
            assert image.size == (70, 54) and image.convert("RGBA").getextrema()[3] == (255, 255), name
            pixels = image.convert("L").tobytes()
            assert set(pixels) <= {0, 5, 144, 153, 208, 255}, name
        packed = [(pixels[i] >> 4) | (pixels[i+1] & 240) for i in range(0, len(pixels), 2)]
        lines.append("    {")
        for offset in range(0, len(packed), 30):
            lines.append("        " + ",".join(f"0x{v:02x}" for v in packed[offset:offset+30]) + ",")
        lines.append("    },")
    return "\n".join(lines + ["};", ""])

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    path = ROOT / "main/assets/reading_scenes.h"
    data = generated()
    if args.check:
        assert path.read_text() == data, "Regenerate reading scenes"
    else:
        path.write_text(data)
    print("PASS: seven fixed 4bpp 70x54 scene bitmaps, 13230 bytes")
