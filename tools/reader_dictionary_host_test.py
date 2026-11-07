#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：构建原创测试包并运行实际解析器，破坏每个格式边界。
English: Build an original fixture and exercise the actual parser at every format boundary.
"""
import importlib.util
import json
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile
import zlib
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('builder',ROOT/'tools/build_dictionary.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    work=Path(folder);db=sqlite3.connect(':memory:');db.execute('CREATE TABLE entries(key TEXT PRIMARY KEY,reading TEXT,definition TEXT,example TEXT,variant TEXT)')
    builder.merge(db,'踽','jǔ','原创测试释义');builder.merge(db,'𠮷','','扩展字测试');builder.merge(db,'踽踽独行','jǔ jǔ dú xíng','原创词语测试')
    package=work/'fixture.mgdict';builder.write_package(db,package)
    same=work/'same.mgdict';builder.write_package(db,same);assert package.read_bytes()==same.read_bytes()
    binary=ROOT/'build/dictionary-tests/dictionary'
    subprocess.run([str(binary),'fixture',str(package)],check=True)
    subprocess.run([str(binary),'paths',str(work)],check=True)
    original=package.read_bytes();cases=[]
    for n in (0,7,32,63,len(original)-1):cases.append(original[:n])
    for offset,value in ((8,999),(12,0xffffffff),(16,0xffffffff),(20,0),(24,0),(28,0),(32,0xffffffff),(36,900)):
        data=bytearray(original);struct.pack_into('<I',data,offset,value);struct.pack_into('<I',data,60,zlib.crc32(data[:60]));cases.append(data)
    for offset in (64,68,72,76,80,82,len(original)-3):
        data=bytearray(original);data[offset]^=255;cases.append(data)
    for i,data in enumerate(cases):
        bad=work/f'bad-{i}.mgdict';bad.write_bytes(data);subprocess.run([str(binary),'bad',str(bad)],check=True)
    for line in ('not json','{"word":"bad"}'):
        source=work/'bad.jsonl';source.write_text(line+'\n')
        try:builder.import_wiktionary(db,source)
        except json.JSONDecodeError:pass
    print(f'PASS: deterministic package and {len(cases)} truncation/version/offset/CRC/UTF-8 corruptions')
