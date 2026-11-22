#!/usr/bin/env python3
"""Generate 4-bit gray spans for the notepad's continuous paper curl."""

import argparse
import itertools
import math
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "main/assets/shelf_notepad_roll.h"
WIDTH, HEIGHT = 636, 86


def make_art():
    # 纸面位于 z=0，顶部沿同一张纸连续卷起。/ The sheet lies at z=0 and rises continuously into the curl.
    radius, hinge, lift = 17.6, 36.6, 5.6
    path = []
    for i in range(35, 0, -1):
        u = i / 35
        y = hinge + (57 - hinge) * u
        z = lift * (1 - u * u * (3 - 2 * u))
        dy = -lift * 6 * u * (1 - u) / (57 - hinge)
        norm = math.hypot(dy, 1)
        shade = 255 - 30 * math.exp(-((y - 49) / 3) ** 2)
        path.append((y, z, -dy / norm, 1 / norm, 0, shade))
    for i in range(133):
        t = 6.98 * i / 132
        d = t / math.tau
        r = radius * (1 - .295 * d ** 1.48)
        dr = -radius * .295 * 1.48 * d ** .48 / math.tau
        y, z = hinge - r * math.sin(t), lift + radius - r * math.cos(t)
        dy = -dr * math.sin(t) - r * math.cos(t)
        dz = -dr * math.cos(t) + r * math.sin(t)
        length = math.hypot(dy, dz)
        path.append((y, z, dz / length, -dy / length, t, None))

    sample = 2
    width, height = WIDTH * sample, HEIGHT * sample
    xs, ys = width / 410, height / 57
    x0, x1 = 12 * 410 / WIDTH, 624 * 410 / WIDTH
    depths, values = [-1000.] * (width * height), [255.] * (width * height)

    def vertex(x, point, side, edge=False, rim=False):
        y, z, ny, nz, t, shade = point
        ny, nz = ny * side, nz * side
        if shade is None:
            lambert = max(0, (-.69 * ny + .77 * nz) / math.hypot(.34, .69, .77))
            grazing = 1 - abs((.27 * ny + nz) / math.hypot(.25, .27, 1))
            shade = 204 + 50 * lambert - 16 * grazing ** 4
            shade += (255 - shade) * max(0, 1 - t / .44)
            shade -= ((13 if side == 1 else 0) + (6 if t > math.pi * 1.82 else 0)) * min(1, t * 2)
        thickness = .405 if rim else .39
        y, z = y + thickness * ny, z + thickness * nz
        shade = 185 if rim else 189 if edge else min(255, max(174, shade))
        return (x - .25 * z) * xs, (y - .27 * z) * ys, z, shade

    def triangle(a, b, c):
        abx, aby, acx, acy = b[0]-a[0], b[1]-a[1], c[0]-a[0], c[1]-a[1]
        det = abx * acy - aby * acx
        if abs(det) < 1e-6:
            return
        x0 = max(0, math.floor(min(a[0], b[0], c[0])))
        x1 = min(width, math.ceil(max(a[0], b[0], c[0])))
        y0 = max(0, math.floor(min(a[1], b[1], c[1])))
        y1 = min(height, math.ceil(max(a[1], b[1], c[1])))
        for y in range(y0, y1):
            dy = y + .5 - a[1]
            for x in range(x0, x1):
                dx = x + .5 - a[0]
                u, v = (dx * acy - dy * acx) / det, (abx * dy - aby * dx) / det
                if u < 0 or v < 0 or u + v > 1:
                    continue
                z = a[2] + (b[2] - a[2]) * u + (c[2] - a[2]) * v
                i = y * width + x
                if z > depths[i]:
                    depths[i] = z
                    values[i] = a[3] + (b[3] - a[3]) * u + (c[3] - a[3]) * v

    def quad(a, b, c, d):
        triangle(a, b, c)
        triangle(a, c, d)

    # 深度决定可见面；侧面是纸厚形成的边，不另画圆形端盖。/ Resolve visible surfaces by depth, with paper edges instead of a circular cap.
    for j, (a, b) in enumerate(zip(path, path[1:]), 1):
        for side in (-1, 1):
            quad(vertex(x0, a, side), vertex(x1, a, side), vertex(x1, b, side), vertex(x0, b, side))
        for x in (x0, x1):
            quad(vertex(x, a, -1, True), vertex(x, a, 1, True),
                 vertex(x, b, 1, True), vertex(x, b, -1, True))
        if j <= 35:
            for x in (x0, x1 - .64):
                quad(vertex(x, a, 1, rim=True), vertex(x + .64, a, 1, rim=True),
                     vertex(x + .64, b, 1, rim=True), vertex(x, b, 1, rim=True))
    tip = path[-1]
    quad(vertex(x0, tip, -1, True), vertex(x1, tip, -1, True),
         vertex(x1, tip, 1, True), vertex(x0, tip, 1, True))

    for y in range(0, height, sample):
        yield [round(sum(values[(y + dy) * width + x + dx] for dy in range(sample) for dx in range(sample)) /
                     (17 * sample ** 2)) for x in range(0, width, sample)]


def generate():
    starts, runs = [0], []
    for row in make_art():
        for shade, group in itertools.groupby(row):
            span = len(list(group))
            runs.append(span << 4 | shade)
        starts.append(len(runs))
    def lines(values, length):
        return "\n".join("    " + ", ".join(map(str, values[i:i+length])) + ","
                         for i in range(0, len(values), length))
    return ("/* 由 tools/gen_shelf_notepad_roll.py 生成，不手改。 / Generated; do not edit. */\n"
            "#pragma once\n#include <stdint.h>\n\n"
            f"#define NOTEPAD_ROLL_W {WIDTH}\n#define NOTEPAD_ROLL_H {HEIGHT}\n"
            "static const uint16_t notepad_roll_rows[] = {\n" + lines(starts, 16) + "\n};\n"
            "static const uint16_t notepad_roll_spans[] = {\n" + lines(runs, 12) + "\n};\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = generate()
    if args.check:
        if OUT.read_text() != content:
            raise SystemExit(f"stale generated artwork: {OUT}")
    else:
        OUT.write_text(content)
