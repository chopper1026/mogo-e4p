"""生成三语霞鹜新晰黑大字样句（不是字库）。/ Generate offline LXGW Neo XiHei Screen specimens, not a font subset."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'main/assets/setup_font_samples.h'
WIDTH, PITCH = 600, 56
HEIGHT = 3 * PITCH
FACE_FILE = 'LXGWNeoXiHeiScreen.ttf'
FACE_SHA256 = '6f69412d8307c055b7f247e67669b0e4ab4b79bf199fe58ee93545429b586092'


def sentences():
    header = (ROOT / 'main/ui/ui_font_sample.h').read_text()
    key = json.loads(re.search(r'^#define UI_FONT_SAMPLE_TEXT (.+)$', header, re.M)[1])
    for line in (ROOT / 'main/ui/locale_catalog.inc').read_text().splitlines():
        if line.startswith('{'):
            row = json.loads('[' + line[1:-2] + ']')
            if row[0] == key:
                return row
    raise ValueError('Missing localized font specimen')


def specimen(path, text):
    rows = text.splitlines()
    assert len(rows) == 3 and all(0 < len(line.encode()) < 96 for line in rows)
    font = ImageFont.truetype(path, 44, layout_engine=ImageFont.Layout.BASIC)
    mask = Image.new('L', (WIDTH, HEIGHT))
    draw = ImageDraw.Draw(mask)
    with TTFont(path) as face:
        cmap = face.getBestCmap()
        for i, line in enumerate(rows):
            assert all(ord(c) in cmap for c in line), line
            left, top, right, bottom = font.getbbox(line, anchor='ls')
            assert right - left <= WIDTH - 2 and -43 <= top and bottom < PITCH - 43, line
            draw.text((1 - left, i * PITCH + 43), line, font=font, fill=255, anchor='ls')
    # 与墨水屏的十六级灰度一致，不在设备上加载第二套字库。/ Pack native 4-bit coverage without loading another face on the device.
    coverage = mask.tobytes()
    return bytes((a & 0xf0) | (b >> 4) for a, b in zip(coverage[::2], coverage[1::2]))


def encode(data):
    # 跳过空白，墨迹保留原始灰度；设备端无需解压缓冲。/ Skip blank pairs, preserving ink without a decode buffer.
    start = 0
    while start < len(data):
        ink = bool(data[start])
        end = start + 1
        while end < min(len(data), start + 128) and bool(data[end]) == ink:
            end += 1
        yield (0x80 if ink else 0) | (end - start - 1)
        if ink:
            yield from data[start:end]
        start = end


def resolve_font(source):
    path = source if source.is_file() else source / FACE_FILE
    if hashlib.sha256(path.read_bytes()).hexdigest() != FACE_SHA256:
        raise ValueError(f'Unexpected font: {path}')
    return path


def render(source):
    path = resolve_font(source)
    lines = ['/* 霞鹜新晰黑（屏显版）v1.305 样句；用 tools/gen_setup_font_samples.py 生成。',
             ' * Printed LXGW Neo XiHei Screen specimens. See THIRD_PARTY_NOTICES.md. */',
             '#pragma once', '#include <stdint.h>', '#include "ui_language.h"',
             f'#define SETUP_SAMPLE_WIDTH {WIDTH}', f'#define SETUP_SAMPLE_HEIGHT {HEIGHT}']
    for lang, text in zip(('CN', 'TW', 'EN'), sentences(), strict=True):
        data = bytes(encode(specimen(path, text)))
        lines.append(f'static const uint8_t setup_sample_{lang}[] = {{')
        for i in range(0, len(data), 24):
            lines.append('        ' + ','.join(f'0x{x:02x}' for x in data[i:i+24]) + ',')
        lines.append('};')
    lines.append('static const uint8_t* const setup_samples[UI_LANG_COUNT] = {')
    lines.extend(f'    [UI_LANG_{lang}] = setup_sample_{lang},' for lang in ('CN', 'TW', 'EN'))
    return '\n'.join(lines + ['};', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help=f'Path to {FACE_FILE} or directory containing it')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    header = render(args.source)
    if args.check:
        if header != OUTPUT.read_text():
            raise SystemExit('Specimens do not match')
    else:
        OUTPUT.write_text(header)
