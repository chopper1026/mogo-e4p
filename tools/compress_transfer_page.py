#!/usr/bin/env python3
"""构建时无损压缩传书网页。/ Losslessly gzip the transfer page at build time."""

import argparse
import gzip
from io import BytesIO
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    buffer = BytesIO()
    # 不写源文件名和构建时间，避免重建产生无意义差异。/ Omit source names and timestamps for reproducible builds.
    with gzip.GzipFile(filename="", fileobj=buffer, mode="wb", compresslevel=9, mtime=0) as output:
        output.write(args.source.read_bytes())
    packed = buffer.getvalue()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != packed:
        args.output.write_bytes(packed)


if __name__ == "__main__":
    main()
