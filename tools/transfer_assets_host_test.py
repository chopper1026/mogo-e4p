#!/usr/bin/env python3
"""验证实际网页响应及无损压缩。/ Verify the real page response and lossless compression."""

import gzip
from pathlib import Path
import subprocess
import sys
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]


with tempfile.TemporaryDirectory(prefix="transfer-assets-") as directory:
    work = Path(directory)
    packed = work / "upload.html.gz"
    command = [sys.executable, str(ROOT / "tools/compress_transfer_page.py"),
               "--source", str(ROOT / "components/wifi_transfer/upload.html"), "--output", str(packed)]
    subprocess.run(command, check=True)
    raw = (ROOT / "components/wifi_transfer/upload.html").read_bytes()
    assert len(packed.read_bytes()) < len(raw) // 2
    assert gzip.decompress(packed.read_bytes()) == raw
    assert packed.read_bytes()[4:8] == bytes(4)
    stamp = packed.stat().st_mtime_ns
    subprocess.run(command, check=True)
    assert packed.stat().st_mtime_ns == stamp
    source = (ROOT / "components/wifi_transfer/wifi_transfer.c").read_text()
    declarations = "\n".join(line for line in source.splitlines()
                             if line.startswith("extern const char upload_"))
    (work / "page.S").write_text('.data\n.globl _binary_upload_html_gz_start\n'
        '_binary_upload_html_gz_start:\n.incbin "' + str(packed) + '"\n'
        '.globl _binary_upload_html_gz_end\n_binary_upload_html_gz_end:\n')
    unit = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
typedef int esp_err_t;
typedef struct { int unused; } httpd_req_t;
#define ESP_OK 0
static int type_error,header_error,send_error,sent,headers;
static const char *body;
static ssize_t body_size;
static int httpd_resp_set_type(httpd_req_t *req,const char *type){
    assert(req&&!strcmp(type,"text/html; charset=utf-8"));return type_error;
}
static int httpd_resp_set_hdr(httpd_req_t *req,const char *name,const char *value){
    assert(req&&!strcmp(name,"Content-Encoding")&&!strcmp(value,"gzip"));++headers;return header_error;
}
static int httpd_resp_send(httpd_req_t *req,const char *data,ssize_t size){
    assert(req&&headers==1&&size>0);++sent;body=data;body_size=size;return send_error;
}
'''
    unit += declarations + "\n" + function("index_handler", ROOT / "components/wifi_transfer/wifi_transfer.c")
    unit += r'''
int main(int argc,char **argv){
    assert(argc==2);httpd_req_t req={0};
    type_error=-2;assert(index_handler(&req)==-2&&headers==0&&sent==0);
    type_error=0;header_error=-3;assert(index_handler(&req)==-3&&sent==0);
    header_error=0;headers=0;assert(index_handler(&req)==ESP_OK&&sent==1);
    assert(body==upload_start&&body_size==upload_end-upload_start);
    FILE *out=fopen(argv[1],"wb");assert(out);
    assert(fwrite(body,1,(size_t)body_size,out)==(size_t)body_size);assert(!fclose(out));
    headers=0;send_error=-4;assert(index_handler(&req)==-4&&sent==2);
    return 0;
}
'''
    (work / "test.c").write_text(unit)
    env, compiler = toolchain(work)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", str(work / "test.c"), str(work / "page.S"),
        "-o", str(work / "test")], env=env, check=True)
    response = work / "response.gz"
    subprocess.run([str(work / "test"), str(response)], env=env, check=True)
    assert response.read_bytes() == packed.read_bytes()
    assert gzip.decompress(response.read_bytes()) == raw
    print(f"PASS: transfer page {len(raw)} -> {packed.stat().st_size} bytes; reproducible gzip, "
          "real handler headers/body, byte-identical HTML and response errors (ASan/UBSan)")
