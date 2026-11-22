#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""生成正常/损坏 ZIP 并运行解析器。/ Generate valid/malformed ZIPs and exercise the parser."""
import io
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def stored_size_offsets(data, name):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        pos = archive.start_dir
        for info in archive.infolist():
            assert data[pos:pos + 4] == b"PK\x01\x02"
            if info.filename == name:
                assert info.compress_type == zipfile.ZIP_STORED
                return info.header_offset + 18, pos + 20
            pos += 46 + sum(struct.unpack_from("<3H", data, pos + 28))
    raise ValueError(name)


def empty_stored_size_quirk(data, name):
    data = bytearray(data)
    for offset in stored_size_offsets(data, name):
        struct.pack_into("<I", data, offset, 2)
    return data


def main():
    with tempfile.TemporaryDirectory(prefix="zip-reader-") as temp:
        work = Path(temp)
        exe = work / "test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(ROOT / "tools/zip_host_stubs"), "-I" + str(ROOT / "main/book"),
                        str(ROOT / "tools/zip_reader_host_test.c"), str(ROOT / "main/book/zip_reader.c"),
                        "-lz", "-o", str(exe)], check=True)
        cases = 0

        def run(mode, data, name="entry.txt", expected=None):
            nonlocal cases
            path = work / "test.zip"
            path.write_bytes(data)
            command = [str(exe), mode, str(path), name]
            if expected is not None:
                payload = work / "expected"
                payload.write_bytes(expected)
                command.append(str(payload))
            subprocess.run(command, check=True)
            cases += 1

        def archive(content, method=zipfile.ZIP_DEFLATED, comment=b"", stream=False):
            class Streaming(io.BytesIO):
                def seekable(self): return False
                def seek(self, *args): raise OSError("stream")
            buffer = Streaming() if stream else io.BytesIO()
            with zipfile.ZipFile(buffer, "w", compression=method) as z:
                z.writestr("entry.txt", content)
                z.comment = comment
            return buffer.getvalue()

        payload = ("中文文本\n" * 1000).encode()
        for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            for data in (b"", b"hello", payload, bytes(range(256)) * 100, os.urandom(128 * 1024)):
                run("read", archive(data, method), expected=data)
                run("prefix", archive(data, method), expected=data)
        run("read", archive(payload, stream=True), expected=payload)
        run("read", archive(payload, comment=b"x" * 65535), expected=payload)
        run("read", archive(b"x" * (8 * 1024 * 1024)), expected=b"x" * (8 * 1024 * 1024))
        run("reject-extract", archive(b"x" * (8 * 1024 * 1024 + 1)))
        run("prefix", archive(b"x" * (8 * 1024 * 1024 + 1)), expected=b"x" * 257)
        good = archive(payload)
        cd = good.index(b"PK\x01\x02")
        eocd = good.rindex(b"PK\x05\x06")

        # Some EPUB producers set general-purpose bit 4 even on valid DEFLATE and stored entries.
        # 有些 EPUB 同时为合法的压缩和直存条目标记 bit 4，仍需逐条解压并核对 CRC。
        for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            flagged = bytearray(archive(payload, method))
            central = flagged.index(b"PK\x01\x02")
            for offset in (6, central + 8):
                flags = struct.unpack_from("<H", flagged, offset)[0]
                struct.pack_into("<H", flagged, offset, flags | 0x10)
            run("read", flagged, expected=payload)

        def changed(offset, fmt, value):
            data = bytearray(good)
            struct.pack_into(fmt, data, offset, value)
            return data

        for offset, fmt, value in ((cd + 8, "<H", 1), (cd + 10, "<H", 99),
                                   (cd + 20, "<I", 0xffffffff), (cd + 24, "<I", 0xffffffff),
                                   (cd + 42, "<I", cd), (cd + 34, "<H", 1),
                                   (eocd + 4, "<H", 1), (eocd + 10, "<H", 513),
                                   (eocd + 16, "<I", 0xffffffff), (cd + 28, "<H", 1025)):
            run("reject-open", changed(offset, fmt, value))
        for offset, fmt, value in ((0, "<I", 0), (8, "<H", 0), (26, "<H", 500),
                                   (28, "<H", 65535), (14, "<I", 0), (30, "<B", 33)):
            run("reject-extract", changed(offset, fmt, value))
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            z.writestr("dir/", b"")
            z.writestr("empty.txt", b"")
            z.writestr("entry.txt", payload)
            z.writestr("last/", b"")
        original = buffer.getvalue()
        patched = empty_stored_size_quirk(empty_stored_size_quirk(original, "dir/"), "last/")
        run("read", original, name="dir/", expected=b"")
        run("read", patched, expected=payload)
        for name in ("dir/", "last/"):
            run("reject-extract", patched, name=name)
            run("reject-prefix", patched, name=name)
        run("reject-open", empty_stored_size_quirk(original, "empty.txt"))
        local, central = stored_size_offsets(original, "dir/")
        for offset, value in ((local, 0), (central, 3), (central + 4, 1), (central - 4, 1)):
            invalid = bytearray(patched)
            struct.pack_into("<I", invalid, offset, value)
            run("reject-open", invalid)
        wrong_local_name = bytearray(patched)
        wrong_local_name[local + 12] = ord("x")
        run("reject-open", wrong_local_name)
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            z.writestr("dir/", b"xx")
            z.writestr("entry.txt", payload)
        data = bytearray(buffer.getvalue())
        local, central = stored_size_offsets(data, "dir/")
        for offset in (local, central):
            struct.pack_into("<III", data, offset - 4, 0, 2, 0)
        run("reject-open", data)
        damaged = bytearray(good)
        damaged[40] ^= 0xff
        run("reject-extract", damaged)
        wrong_crc = changed(cd + 16, "<I", 42)
        struct.pack_into("<I", wrong_crc, 14, 42)
        run("reject-extract", wrong_crc)
        too_small = changed(cd + 24, "<I", 1)
        struct.pack_into("<I", too_small, 22, 1)
        run("reject-extract", too_small)
        run("reject-extract", changed(18, "<I", 0xffffffff))
        run("reject-open", good[:-1])
        run("reject-open", b"not a ZIP")
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w"): pass
        run("empty", buffer.getvalue())
        for count in (8192, 8193):
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, "w") as z:
                for i in range(count): z.writestr(f"entry{i}.txt", b"")
            run("empty" if count == 8192 else "reject-open", buffer.getvalue())
        # ZIP64 扩展不依赖哨兵值也必须拒绝。/ Reject ZIP64 extras even without sentinel sizes.
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            info = zipfile.ZipInfo("entry.txt")
            info.extra = struct.pack("<HHQQ", 1, 16, 0, 0)
            z.writestr(info, b"")
        run("reject-open", buffer.getvalue())
        buffer = io.BytesIO()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(buffer, "w") as z:
                z.writestr("entry.txt", b"one")
                z.writestr("entry.txt", b"two")
        run("reject-open", buffer.getvalue())
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("正文/第一章.xhtml", payload)
        run("read", buffer.getvalue(), name="正文/第一章.xhtml", expected=payload)
        print(f"ZIP parser: {cases} normal, limit and malformed cases passed (host zlib inflate adapter)")


if __name__ == "__main__":
    main()
