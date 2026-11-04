#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 生成原创测试书并校验编码和 EPUB 结构，不访问设备。/ Generate original fixtures and verify encoding and EPUB structure without device access.

import argparse
import base64
import hashlib
import json
import posixpath
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
import zipfile
import zlib


TITLES = ["第一章 河边清晨", "第二章 雨后小路", "第三章 山间书屋", "第四章 灯下归途"]
PARAGRAPH = (
    "清晨的河水映着天光，小舟从石桥下面慢慢经过。"
    "我们带上纸笔，沿着岸边的小路记录今天看到的景色。"
    "树叶上的水珠落在石阶上，远处的屋顶升起一缕白烟。"
    "走到书屋门前，大家停下来读了一页书，又把心里的问题写在纸上。"
    "每一次出发都有新的发现，每一次归来都有值得保存的记忆。"
)
NS = {
    "opf": "http://www.idpf.org/2007/opf",
    "ocf": "urn:oasis:names:tc:opendocument:xmlns:container",
    "ncx": "http://www.daisy.org/z3986/2005/ncx/",
    "x": "http://www.w3.org/1999/xhtml",
}


def paragraphs(chapter, count):
    return [f"记录 {chapter + 1}-{i + 1:03d}：{PARAGRAPH}" for i in range(count)]


def text_book(count):
    return "\n\n".join(
        title + "\n\n" + "\n\n".join(paragraphs(chapter, count))
        for chapter, title in enumerate(TITLES)
    ) + "\n"


def zip_entry(archive, name, content, compression=zipfile.ZIP_DEFLATED):
    # 固定时间戳和权限，重复生成保持同样字节。/ Fix timestamps and permissions for byte-identical regeneration.
    info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
    info.compress_type = compression
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    archive.writestr(info, content.encode("utf-8") if isinstance(content, str) else content)


def grayscale_png(width=384, height=128):
    # 无滤波的16阶灰阶带与棋盘格，全部由整数生成。/ Generate unfiltered 16-level bands and checkerboards using integers only.
    rows = b"".join(b"\0" + bytes(
        (x * 16 // width) * 17 if y < height // 2 else
        (0 if (x // 16 + y // 16) % 2 else 255)
        for x in range(width)) for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def make_epub(path, navigation, div_heavy=False, mixed_images=False):
    version = "2.0" if navigation == "ncx" else "3.0"
    toc_item = (
        '<item id="toc" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
        if navigation == "ncx" else
        '<item id="toc" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
    )
    items = "".join(
        f'<item id="c{i}" href="text/ch{i}.xhtml" media-type="application/xhtml+xml"/>'
        for i in range(1, 5)
    )
    if mixed_images:
        items += '<item id="gray" href="images/gray.png" media-type="image/png"/>'
    spine = "".join(f'<itemref idref="c{i}"/>' for i in range(1, 5))
    toc_attr = ' toc="toc"' if navigation == "ncx" else ""
    modified = '<meta property="dcterms:modified">2026-01-01T00:00:00Z</meta>' if version == "3.0" else ""
    package = f'''<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="{NS['opf']}" version="{version}" unique-identifier="book-id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="book-id">urn:read-pico:fixture:{navigation}:{int(div_heavy)}</dc:identifier>
<dc:title>河边记事</dc:title><dc:language>zh-CN</dc:language><dc:creator>Read Pico fixtures</dc:creator>{modified}
</metadata><manifest>{toc_item}{items}</manifest><spine{toc_attr}>{spine}</spine></package>'''
    container = f'''<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="{NS['ocf']}" version="1.0"><rootfiles>
<rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
</rootfiles></container>'''
    with zipfile.ZipFile(path, "w") as archive:
        zip_entry(archive, "mimetype", "application/epub+zip", zipfile.ZIP_STORED)
        zip_entry(archive, "META-INF/container.xml", container)
        zip_entry(archive, "OEBPS/content.opf", package)
        if mixed_images:
            zip_entry(archive, "OEBPS/images/gray.png", grayscale_png())
        for index, title in enumerate(TITLES, 1):
            body = "".join(
                f'<div class="paragraph"><div><span>{escape(p)}</span></div></div>'
                if div_heavy else f"<p>{escape(p)}</p>"
                for p in paragraphs(index - 1, 60)
            )
            if mixed_images:
                body = "".join(
                    f'<p>图文记录 {index}-{i:03d}：清晨的河水映着天光。</p>'
                    '<p><img src="../images/gray.png" alt="十六阶灰阶与棋盘格"/></p>'
                    for i in range(1, 61)
                )
            chapter = f'''<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="{NS['x']}" xml:lang="zh-CN"><head><title>{title}</title></head>
<body><h1 id="start">{title}</h1>{body}<p>实体检查：&amp; &lt; &gt; &#160;，段落终点。</p></body></html>'''
            zip_entry(archive, f"OEBPS/text/ch{index}.xhtml", chapter)
        if navigation == "ncx":
            points = "".join(
                f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>{title}</text></navLabel>'
                f'<content src="text/ch{i}.xhtml#start"/></navPoint>'
                for i, title in enumerate(TITLES, 1)
            )
            toc = f'''<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="{NS['ncx']}" version="2005-1"><head>
<meta name="dtb:uid" content="urn:read-pico:fixture:ncx:0"/>
<meta name="dtb:depth" content="1"/><meta name="dtb:totalPageCount" content="0"/>
<meta name="dtb:maxPageNumber" content="0"/></head>
<docTitle><text>河边记事</text></docTitle><navMap>{points}</navMap></ncx>'''
            zip_entry(archive, "OEBPS/toc.ncx", toc)
        else:
            links = "".join(
                f'<li><a href="text/ch{i}.xhtml#start">{title}</a></li>'
                for i, title in enumerate(TITLES, 1)
            )
            toc = f'''<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="{NS['x']}" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="zh-CN">
<head><title>目录</title></head><body><nav epub:type="toc" id="toc"><h1>目录</h1>
<ol>{links}</ol></nav></body></html>'''
            zip_entry(archive, "OEBPS/nav.xhtml", toc)


def require(condition, message):
    if not condition:
        raise ValueError(message)


MOBI_JPEG = base64.b64decode(
    "/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4n"
    "ICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/wAALCAABAAEBAREA/8QAFAABAAAAAAAAAAAAAAAAAAAACf/E"
    "ABQQAQAAAAAAAAAAAAAAAAAAAAD/2gAIAQEAAD8AKp//2Q==")
MOBI_RECORD_SIZE = 4096


def palmdoc_pack(data):
    """Literal/run + greedy back references; keeps the fixture inside the PalmDOC grammar."""
    out = bytearray()
    index = 0
    total = len(data)
    while index < total:
        best_len = 0
        best_dist = 0
        for back in range(1, min(index, 2047) + 1):
            if data[index - back] != data[index]:
                continue
            length = 0
            while (length < 10 and index + length < total
                   and data[index - back + length] == data[index + length]):
                length += 1
            if length > best_len:
                best_len, best_dist = length, back
                if best_len == 10:
                    break
        if best_len >= 3:
            token = 0x8000 | (best_dist << 3) | (best_len - 3)
            out += bytes(((token >> 8) & 0xFF, token & 0xFF))
            index += best_len
            continue
        byte = data[index]
        if 0x09 <= byte <= 0x7F:
            out.append(byte)
        elif byte == 0:
            out.append(0)
        else:
            # 0x80 以上的字节只能用 1..8 的原文段搬。/ Bytes above 0x7F only travel as a literal run.
            out += bytes((1, byte))
        index += 1
    return bytes(out)


def palmdoc_unpack(data):
    """Reference decoder used to self-check the fixture before it is written."""
    out = bytearray()
    at = 0
    while at < len(data):
        c = data[at]
        at += 1
        if c == 0 or 0x09 <= c <= 0x7F:
            out.append(c)
        elif c <= 0x08:
            out += data[at:at + c]
            at += c
        elif c >= 0xC0:
            out += bytes((0x20, c ^ 0x80))
        else:
            pair = (c << 8) | data[at]
            at += 1
            dist = (pair >> 3) & 0x7FF
            length = (pair & 7) + 3
            for _ in range(length):
                out.append(out[-dist])
    return bytes(out)


def huff_records():
    """Degenerate 8-bit HUFF/CDIC tables: code N decodes to byte N.

    与真实文件同构（dict1/dict2/CDIC 三件套），足以驱动完整的解码路径。
    Same shape as a real file (dict1/dict2/CDIC) so the decoding path is fully exercised.
    """
    dict1 = b"".join(struct.pack(">I", ((2 * byte) << 8) | 0x88) for byte in range(256))
    dict2 = b"".join(
        struct.pack(">II", 0, 255 if length == 8 else 0) for length in range(1, 33))
    huff = b"HUFF" + struct.pack(">III", 24, 24, 24 + len(dict1)) + bytes(8) + dict1 + dict2
    offsets = b"".join(struct.pack(">H", 512 + 3 * byte) for byte in range(256))
    phrases = b"".join(struct.pack(">H", 0x8001) + bytes((byte,)) for byte in range(256))
    cdic = b"CDIC" + struct.pack(">III", 16, 256, 8) + offsets + phrases
    return huff, cdic


def vwi(value):
    """MOBI 变宽整数：7 位一组，最后一字节置最高位。/ MOBI variable width integer."""
    parts = [value & 0x7F]
    value >>= 7
    while value:
        parts.append(value & 0x7F)
        value >>= 7
    parts.reverse()
    parts[-1] |= 0x80
    return bytes(parts)


def ncx_records(entries):
    """造一份 ncx 编译出的 INDX 目录：主记录 + 子记录 + CTOC 字符串块。

    entries 是 (正文偏移, 标题)；与真实 MOBI 同构，供解析器回归。
    entries are (text offset, label); the layout mirrors a real MOBI so the reader can be regressed.
    """
    # CTOC：每条是「长度前缀 + 字符串」。
    ctoc = bytearray()
    keys = []
    for _, label in entries:
        raw = label.encode("utf-8")
        keys.append(len(ctoc))
        ctoc += vwi(len(raw)) + raw

    # 子记录：条目在前，IDXT 偏移表在后。
    sub = bytearray(0x38)
    sub[0:4] = b"INDX"
    struct.pack_into(">I", sub, 4, 0x38)          # len = 头部长度，子记录没有 TAGX
    struct.pack_into(">I", sub, 0x18, len(entries))  # count = 条目数
    offsets = []
    for (pos, _), key in zip(entries, keys):
        offsets.append(len(sub))
        name = b""                                    # 条目名不参与目录，留空
        # 控制位：bit0 = pos，bit2 = noffs；随后按标签顺序写变宽整数。
        sub += bytes((len(name),)) + name + bytes((0x05,)) + vwi(pos) + vwi(key)
    while len(sub) % 2:
        sub.append(0)
    idxt = len(sub)
    struct.pack_into(">I", sub, 0x14, idxt)       # start = IDXT 起点
    sub += b"IDXT"
    for offset in offsets:
        sub += struct.pack(">H", offset)
    while len(sub) % 4:
        sub.append(0)

    # 主记录：头部 + TAGX（4 个标签 + 1 个分隔项，1 个控制字节）。
    main = bytearray(0xC0)
    main[0:4] = b"INDX"
    struct.pack_into(">I", main, 4, 0xC0)
    struct.pack_into(">I", main, 0x18, 1)         # count = 1 条子记录
    struct.pack_into(">I", main, 0x34, 1)         # nctoc = 1 块字符串
    tags = [(1, 1, 0x01, 0), (2, 1, 0x02, 0), (3, 1, 0x04, 0), (4, 1, 0x08, 0),
            (0, 0, 0x00, 1)]
    tagx = b"TAGX" + struct.pack(">II", 12 + 4 * len(tags), 1)
    for tag in tags:
        tagx += bytes(tag)
    main += tagx
    return bytes(main), bytes(sub), bytes(ctoc)


def mobi_text(with_toc):
    """两遍拼装：所有 filepos 定宽十位，改写后长度不变。

    filepos 是字节偏移，所以每一步都按 UTF-8 编码后计量。
    filepos counts bytes, so every step is measured after UTF-8 encoding.
    """
    bodies = []
    for chapter, title in enumerate(TITLES):
        body = "".join(f"<p>{escape(p)}</p>" for p in paragraphs(chapter, 40))
        art = '<p><img recindex="00002" alt="art"></p>' if chapter == 2 else ""
        bodies.append(f"<h2>{title}</h2>{body}{art}".encode("utf-8"))
    head = ('<html><head><guide><reference type="toc" title="目录" filepos={:010d} />'
            "</guide></head><body>")
    title_page = '<p align="center"><b>河边记事</b></p><mbp:pagebreak/>'.encode("utf-8")

    def toc_page(offsets):
        anchors = "".join(f"<a filepos={offset:010d}>{title}</a><br/>"
                          for offset, title in zip(offsets, TITLES))
        return ("<p>" + anchors + "</p><mbp:pagebreak/>").encode("utf-8")

    if with_toc:
        placeholder = toc_page([0] * len(TITLES))
        toc_start = len(head.format(0).encode("utf-8")) + len(title_page)
        cursor = toc_start + len(placeholder)
        offsets = []
        for body in bodies:
            offsets.append(cursor)
            cursor += len(body) + len(b"<mbp:pagebreak/>")
        toc = toc_page(offsets)
        require(len(toc) == len(placeholder), "fixed-width TOC keeps its length")
        prefix = head.format(toc_start).encode("utf-8")
    else:
        prefix = b"<html><head></head><body>"
        toc = b""
    return prefix + title_page + toc + b"<mbp:pagebreak/>".join(bodies) + b"</body></html>"


def build_record0(compression, text_bytes_len, text_records, last_content, first_image,
                  huff_offset, huff_count, title, author, cover, ncx_index=0):
    exth_body = b""
    for kind, payload in ((503, title.encode("utf-8")), (100, author.encode("utf-8")),
                          (201, struct.pack(">I", 0)), (202, struct.pack(">I", 1))):
        if kind in (201, 202) and cover is None:
            continue
        exth_body += struct.pack(">II", kind, len(payload) + 8) + payload
    padding = (-(12 + len(exth_body))) % 4
    count = sum(1 for kind in (503, 100, 201, 202)
                if not (kind in (201, 202) and cover is None))
    exth = b"EXTH" + struct.pack(">II", 12 + len(exth_body), count) + exth_body + bytes(padding)
    r0 = bytearray(0x10 + 232)
    struct.pack_into(">H", r0, 0, compression)
    struct.pack_into(">I", r0, 4, text_bytes_len)
    struct.pack_into(">H", r0, 8, text_records)
    struct.pack_into(">H", r0, 10, MOBI_RECORD_SIZE)
    struct.pack_into(">H", r0, 12, 0)
    r0[0x10:0x14] = b"MOBI"
    struct.pack_into(">I", r0, 0x14, 232)
    struct.pack_into(">I", r0, 0x18, 2)
    struct.pack_into(">I", r0, 0x1C, 65001)
    struct.pack_into(">I", r0, 0x20, 0x5A5A1234)
    struct.pack_into(">I", r0, 0x24, 6)
    for field in range(0x28, 0x50, 4):
        struct.pack_into(">I", r0, field, 0xFFFFFFFF)
    struct.pack_into(">I", r0, 0x50, text_records + 1)
    struct.pack_into(">I", r0, 0x5C, 9)
    struct.pack_into(">I", r0, 0x68, 6)
    struct.pack_into(">I", r0, 0x6C, first_image or 0)
    struct.pack_into(">I", r0, 0x70, huff_offset)
    struct.pack_into(">I", r0, 0x74, huff_count)
    struct.pack_into(">I", r0, 0x80, 0x40)
    struct.pack_into(">I", r0, 0xA8, 0xFFFFFFFF)
    struct.pack_into(">I", r0, 0xAC, 0xFFFFFFFF)
    struct.pack_into(">H", r0, 0xC0, 1)
    struct.pack_into(">H", r0, 0xC2, last_content)
    struct.pack_into(">I", r0, 0xC4, 1)
    struct.pack_into(">I", r0, 0xE0, 0xFFFFFFFF)
    struct.pack_into(">I", r0, 0xF0, 0)  # 无尾部附加条目 / no trailing record entries
    # 0xF4 指向 ncx 编译出的 INDX；0xFFFFFFFF 表示书里没有正式目录。
    struct.pack_into(">I", r0, 0xF4, ncx_index if ncx_index else 0xFFFFFFFF)
    struct.pack_into(">I", r0, 0x54, len(r0) + len(exth))
    struct.pack_into(">I", r0, 0x58, len(title.encode("utf-8")))
    r0 += exth
    r0 += title.encode("utf-8")
    while len(r0) % 4:
        r0.append(0)
    return bytes(r0)


def make_mobi(path, compression, with_toc, with_cover, with_ncx=False):
    text = mobi_text(with_toc)
    chunks = [text[i:i + MOBI_RECORD_SIZE] for i in range(0, len(text), MOBI_RECORD_SIZE)]
    if compression == 2:
        payloads = [palmdoc_pack(chunk) for chunk in chunks]
        for chunk, payload in zip(chunks, payloads):
            require(palmdoc_unpack(payload) == chunk, "PalmDOC round trip")
        extra = []
        huff_offset = huff_count = 0
    elif compression == 17480:
        payloads = list(chunks)
        huff, cdic = huff_records()
        huff_offset = 1 + len(chunks)
        huff_count = 2
        extra = [huff, cdic]
    else:
        payloads = list(chunks)
        extra = []
        huff_offset = huff_count = 0
    # ncx 目录：与真实文件一致，只有 INDX 记录，正文里没有 filepos 目录页。
    # The ncx TOC mirrors real files: only INDX records, no filepos page in the body.
    ncx_index = 0
    if with_ncx:
        entries = []
        for title in TITLES:
            at = text.find(f"<h2>{title}</h2>".encode("utf-8"))
            if at >= 0:
                entries.append((at, title))
        main_indx, sub_indx, ctoc = ncx_records(entries)
        ncx_index = 1 + len(chunks) + len(extra)
        extra += [main_indx, sub_indx, ctoc]
    # 图片记录始终存在：正文里的 recindex 必须能解析；with_cover 只决定 EXTH 是否声明封面。
    # Image records always exist so recindex resolves; with_cover only toggles the EXTH cover entries.
    first_image = 1 + len(chunks) + len(extra)
    images = [MOBI_JPEG, MOBI_JPEG]
    records = [build_record0(compression, len(text), len(chunks), len(chunks),
                             first_image, huff_offset, huff_count,
                             "河边记事", "Read Pico fixtures",
                             first_image if with_cover else None, ncx_index)]
    records += payloads + extra + images
    header = bytearray(78)
    name = "河边记事".encode("utf-8")[:31]
    header[0:len(name)] = name
    header[60:64] = b"BOOK"
    header[64:68] = b"MOBI"
    struct.pack_into(">I", header, 68, 0x1234)
    struct.pack_into(">H", header, 76, len(records))
    table = bytearray()
    cursor = 78 + 8 * len(records) + 2
    offsets = []
    for index, record in enumerate(records):
        offsets.append(cursor)
        table += struct.pack(">I", cursor) + b"\x00" + struct.pack(">I", index)[1:]
        cursor += len(record)
    with path.open("wb") as handle:
        handle.write(bytes(header) + bytes(table) + bytes(2))
        for record in records:
            handle.write(record)
    return offsets, text


def check_mobi(path, compression, with_toc, with_cover, with_ncx=False):
    data = path.read_bytes()
    require(data[60:68] == b"BOOKMOBI", "PDB identity")
    count = struct.unpack_from(">H", data, 76)[0]
    offsets = [struct.unpack_from(">I", data, 78 + index * 8)[0] for index in range(count)]
    require(offsets == sorted(offsets), "record offsets are ordered")
    r0 = data[offsets[0]:offsets[1]]
    require(struct.unpack_from(">H", r0, 0)[0] == compression, "compression field")
    require(r0[0x10:0x14] == b"MOBI", "MOBI identifier")
    text_length = struct.unpack_from(">I", r0, 4)[0]
    text_records = struct.unpack_from(">H", r0, 8)[0]
    first_text = struct.unpack_from(">H", r0, 0xC0)[0]
    rebuilt = b""
    for index in range(first_text, first_text + text_records):
        end = offsets[index + 1] if index + 1 < count else len(data)
        payload = data[offsets[index]:end]
        rebuilt += palmdoc_unpack(payload) if compression == 2 else payload
    require(len(rebuilt) == text_length, "text length matches the inflate")
    require("河边记事" in rebuilt.decode("utf-8"), "UTF-8 payload survives compression")
    if with_toc:
        require(b"filepos=" in rebuilt, "TOC anchors present")
        require(b"<guide" in rebuilt, "guide TOC reference present")
    else:
        require(b"filepos=" not in rebuilt, "pagebreak fixture carries no anchors")
    if compression == 17480:
        huff_at = struct.unpack_from(">I", r0, 0x70)[0]
        require(data[offsets[huff_at]:offsets[huff_at] + 4] == b"HUFF", "HUFF record")
        require(data[offsets[huff_at + 1]:offsets[huff_at + 1] + 4] == b"CDIC", "CDIC record")
    if with_cover:
        first_image = struct.unpack_from(">I", r0, 0x6C)[0]
        require(data[offsets[first_image]:offsets[first_image] + 2] == b"\xff\xd8", "JPEG cover")
    if with_ncx:
        ncx = struct.unpack_from(">I", r0, 0xF4)[0]
        require(data[offsets[ncx]:offsets[ncx] + 4] == b"INDX", "NCX index record")
        require(data[offsets[ncx] + 0xC0:offsets[ncx] + 0xC4] == b"TAGX", "TAGX table")
    else:
        require(struct.unpack_from(">I", r0, 0xF4)[0] == 0xFFFFFFFF, "no NCX declared")



def check_epub(path, navigation, div_heavy, mixed_images=False):
    with zipfile.ZipFile(path) as archive:
        require(archive.testzip() is None, "ZIP CRC failure")
        first = archive.infolist()[0]
        require(first.filename == "mimetype" and first.compress_type == zipfile.ZIP_STORED, "mimetype order/compression")
        require(archive.read("mimetype") == b"application/epub+zip", "mimetype content")
        require(all(i.compress_type == zipfile.ZIP_DEFLATED for i in archive.infolist()[1:]), "deflated fixture entries")
        for name in archive.namelist()[1:]:
            if not name.endswith(".png"):
                ET.fromstring(archive.read(name))
        if mixed_images:
            require(archive.read("OEBPS/images/gray.png") == grayscale_png(), "image bytes")
        container = ET.fromstring(archive.read("META-INF/container.xml"))
        opf_path = container.find("ocf:rootfiles/ocf:rootfile", NS).attrib["full-path"]
        package = ET.fromstring(archive.read(opf_path))
        items = {item.attrib["id"]: item for item in package.findall("opf:manifest/opf:item", NS)}
        for item in items.values():
            require(posixpath.join(posixpath.dirname(opf_path), item.attrib["href"]) in archive.namelist(), "missing manifest resource")
        spine = package.findall("opf:spine/opf:itemref", NS)
        require([item.attrib["idref"] for item in spine] == ["c1", "c2", "c3", "c4"], "spine order")
        toc_path = "OEBPS/" + ("toc.ncx" if navigation == "ncx" else "nav.xhtml")
        toc = ET.fromstring(archive.read(toc_path))
        if navigation == "ncx":
            targets = [x.attrib["src"] for x in toc.findall(".//ncx:content", NS)]
            labels = [x.text for x in toc.findall(".//ncx:navLabel/ncx:text", NS)]
        else:
            require(not any(name.endswith(".ncx") for name in archive.namelist()), "nav-only fixture has NCX")
            require(items["toc"].attrib.get("properties") == "nav", "missing nav property")
            links = toc.findall(".//x:nav/x:ol/x:li/x:a", NS)
            targets = [x.attrib["href"] for x in links]
            labels = [x.text for x in links]
        require(labels == TITLES, "TOC labels/order")
        for target in targets:
            href, anchor = target.split("#")
            chapter = ET.fromstring(archive.read(posixpath.join("OEBPS", href)))
            require(any(node.attrib.get("id") == anchor for node in chapter.iter()), "TOC anchor missing")
            if div_heavy:
                require(len(chapter.findall(".//x:div", NS)) == 120, "nested div coverage")
            if mixed_images:
                images = chapter.findall(".//x:img", NS)
                require(len(images) == 60, "mixed image coverage")
                for image in images:
                    name = posixpath.normpath(posixpath.join("OEBPS", posixpath.dirname(href), image.attrib["src"]))
                    require(name == "OEBPS/images/gray.png" and name in archive.namelist(), "image reference")


def main():
    parser = argparse.ArgumentParser(description="Generate and self-check original TXT/EPUB test books.")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "build/book-fixtures")
    args = parser.parse_args()
    books = args.output / "books"
    books.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, encoding, count, purpose in [
        ("河边记事_UTF8.txt", "utf-8", 100, "UTF-8 without BOM; 20+ page turns and progress persistence"),
        ("河边记事_UTF8_BOM.txt", "utf-8-sig", 100, "UTF-8 BOM removal and chapter detection"),
        ("河边记事_GBK.txt", "gbk", 100, "GBK decoding; Chinese filenames and chapter detection"),
        ("flash_small.txt", "utf-8", 4, "Under 1 MiB; flash fallback without TF card"),
    ]:
        path = books / name
        expected = text_book(count)
        path.write_bytes(expected.encode(encoding))
        data = path.read_bytes()
        require(data.decode(encoding) == expected, "TXT encoding round trip")
        require(data.startswith(b"\xef\xbb\xbf") == (encoding == "utf-8-sig"), "TXT BOM")
        require(all(title in data.decode(encoding) for title in TITLES), "TXT chapter labels")
        if encoding == "gbk":
            try:
                data.decode("utf-8")
            except UnicodeDecodeError:
                pass
            else:
                raise ValueError("GBK fixture incorrectly accepts UTF-8")
        require(len(data) < 1024 * 1024, "TXT exceeds flash test limit")
        manifest.append(dict(file=f"books/{name}", encoding=encoding, chapters=TITLES, purpose=purpose))
    for name, navigation, div_heavy in [
        ("河边记事_ncx.epub", "ncx", False),
        ("河边记事_nav.epub", "nav", False),
        ("河边记事_div.epub", "nav", True),
        ("河边记事_尾部空白.epub", "nav", False),
    ]:
        path = books / name
        make_epub(path, navigation, div_heavy)
        if "尾部空白" in name:
            with path.open("ab") as archive:
                archive.write(b"\r\n")
        check_epub(path, navigation, div_heavy)
        manifest.append(dict(file=f"books/{name}", encoding="utf-8", chapters=TITLES, navigation=navigation,
                             purpose="Trailing CRLF after ZIP directory" if "尾部空白" in name else
                             "Nested div paragraph boundaries" if div_heavy else f"{navigation} TOC and spine order"))
    for name, compression, with_toc, with_cover, with_ncx in [
        ("河边记事_palmdoc.mobi", 2, True, True, False),
        ("河边记事_huff.mobi", 17480, True, True, False),
        ("河边记事_plain.mobi", 1, True, False, False),
        ("河边记事_ncx.mobi", 2, False, True, True),
        ("河边记事_无目录.mobi", 2, False, True, False),
    ]:
        path = books / name
        make_mobi(path, compression, with_toc, with_cover, with_ncx)
        check_mobi(path, compression, with_toc, with_cover, with_ncx)
        manifest.append(dict(file=f"books/{name}", encoding="utf-8", chapters=TITLES, compression=compression,
                             purpose="HUFF/CDIC compressed records" if compression == 17480 else
                                     "Uncompressed text records" if compression == 1 else "PalmDOC text records",
                             navigation="ncx index" if with_ncx else "filepos TOC page" if with_toc else "none"))
    name = "河边记事_图文.epub"
    make_epub(books / name, "nav", mixed_images=True)
    check_epub(books / name, "nav", False, mixed_images=True)
    manifest.append(dict(file=f"books/{name}", encoding="utf-8", chapters=TITLES, navigation="nav",
                         purpose="60 inline grayscale PNGs per chapter; mixed text/image page-turn baseline"))
    for item in manifest:
        data = (args.output / item["file"]).read_bytes()
        item.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    output = dict(schema=1, generator="tools/gen_book_fixtures.py", original_content=True,
                  verification="Host encoding/XML/ZIP/reference checks passed; on-device pagination and rendering not verified.",
                  fixtures=manifest)
    (args.output / "manifest.json").write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(manifest)} books; encoding/XML/ZIP/TOC self-checks passed.")
    print(f"Output: {args.output.resolve()}")


if __name__ == "__main__":
    main()
