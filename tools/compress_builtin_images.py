#!/usr/bin/env python3
"""构建时无损压缩内置图片。/ Losslessly pack built-in images at build time."""

import argparse
from pathlib import Path
import struct
import zlib


def pack_weather(raw):
    """WBZ1: geometry, ten absolute scene offsets, then independently zlib-packed scenes."""
    width, height, scenes, stride = 612, 448, 9, 77
    scene_bytes = height * stride
    if len(raw) != scenes * scene_bytes:
        raise ValueError("weather_backgrounds: expected nine 612 x 448 1bpp masks")
    header_size = 12 + (scenes + 1) * 4
    data, offsets = bytearray(), []
    for scene in range(scenes):
        offsets.append(header_size + len(data))
        start = scene * scene_bytes
        data.extend(zlib.compress(raw[start:start + scene_bytes], level=9))
    offsets.append(header_size + len(data))
    return struct.pack("<4s4H10I", b"WBZ1", width, height, scenes, stride, *offsets) + data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("lock_4bpp",):
        raw = (args.source / (name + ".bin")).read_bytes()
        if len(raw) != 684 * 1216 // 2:
            parser.error(f"{name}: expected a 684 x 1216 4bpp frame")
        compressed = zlib.compress(raw, level=9)
        # 固定头含原始长度和压缩长度；zlib 尾部校验原像素。/ The fixed header stores both lengths; zlib checks the original pixels.
        packed = struct.pack("<4sII", b"KBI1", len(raw), len(compressed)) + compressed
        destination = args.output / (name + ".kbz")
        if not destination.exists() or destination.read_bytes() != packed:
            destination.write_bytes(packed)
    packed = pack_weather((args.source / "weather_backgrounds.bin").read_bytes())
    destination = args.output / "weather_backgrounds.wbr"
    if not destination.exists() or destination.read_bytes() != packed:
        destination.write_bytes(packed)


if __name__ == "__main__":
    main()
