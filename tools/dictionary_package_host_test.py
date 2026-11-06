#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实安装器的有界清单、失败保旧与完整目录切换回归。
English: Exercise manifest bounds, rollback and complete-directory activation in the real installer.
"""
import importlib.util
import json
from pathlib import Path
import sqlite3
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder', ROOT / 'tools/build_dictionary.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
UNIT = r'''
#include "dictionary_package.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
static int failing=-1,calls;static bool cancel_now;
static const char *source;
static bool cancelled(void *context){(void)context;return cancel_now;}
static bool fetch(const dictionary_package_file_t *file,const char *path,void *context){
 (void)context;if(calls++==failing)return false;
 FILE *out=fopen(path,"wb");assert(out);
 if(!strcmp(file->name,"chinese.mgdict")){FILE *in=fopen(source,"rb");assert(in);char b[1024];size_t n;while((n=fread(b,1,sizeof(b),in)))assert(fwrite(b,1,n,out)==n);fclose(in);}
 else for(uint32_t i=0;i<file->size;++i)fputc('x',out);
 assert(!fclose(out));return true;
}
int main(int argc,char **argv){
 assert(argc==5);source=argv[2];FILE *in=fopen(argv[3],"rb");assert(in);char body[8193]={0};assert(fread(body,1,8192,in));fclose(in);
 dictionary_package_t p;assert(dictionary_package_parse(body,&p));assert(p.size>64);
 char bad[8193];strcpy(bad,body);char *format=strstr(bad,"\"format\": 1");assert(format);format[10]='2';assert(!dictionary_package_parse(bad,&p));
 assert(dictionary_package_parse(body,&p));
 char path[256];assert(dictionary_package_path(argv[1],path,sizeof(path)));assert(strstr(path,"/chinese.mgdict"));
 for(int i=0;i<DICTIONARY_PACKAGE_FILES;++i){failing=i;calls=0;assert(!dictionary_package_install(argv[1],&p,fetch,cancelled,NULL));assert(dictionary_package_path(argv[1],path,sizeof(path)));assert(!strstr(path,"installed/"));}
 failing=-1;calls=0;cancel_now=true;assert(!dictionary_package_install(argv[1],&p,fetch,cancelled,NULL));cancel_now=false;
 calls=0;assert(dictionary_package_install(argv[1],&p,fetch,cancelled,NULL));assert(dictionary_package_path(argv[1],path,sizeof(path)));assert(strstr(path,"installed/chinese.mgdict"));
 book_dictionary_result_t r;assert(book_dictionary_lookup(path,"的",&r,NULL,NULL)==BOOK_DICTIONARY_OK);
 failing=4;calls=0;assert(!dictionary_package_install(argv[1],&p,fetch,cancelled,NULL));assert(book_dictionary_lookup(path,"的",&r,NULL,NULL)==BOOK_DICTIONARY_OK);
 failing=-1;calls=0;assert(dictionary_package_install(argv[1],&p,fetch,cancelled,NULL));
 char active[256],previous[256];snprintf(active,sizeof(active),"%s/installed",argv[1]);snprintf(previous,sizeof(previous),"%s/.previous",argv[1]);
 // 模拟上次安装在两次改名之间掉电，下一次读取和安装均须恢复。
 // Simulate loss of power between directory renames; lookup and the next installation recover.
 char moved[256];snprintf(moved,sizeof(moved),"%s/interrupted",argv[1]);assert(!rename(active,moved));
 assert(dictionary_package_path(argv[1],path,sizeof(path))&&strstr(path,".previous/chinese.mgdict"));
 calls=0;assert(dictionary_package_install(argv[1],&p,fetch,cancelled,NULL));
 FILE *badfile=fopen(argv[4],"rb");assert(badfile);memset(body,0,sizeof(body));assert(fread(body,1,8192,badfile));fclose(badfile);
 assert(!dictionary_package_parse(body,&p));
 puts("PASS: real package manifest bounds, eight download failures, cancellation, format probe, directory commit and interrupted-rename recovery");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    work = Path(folder)
    conn = sqlite3.connect(':memory:')
    conn.execute('CREATE TABLE entries(key TEXT PRIMARY KEY,reading TEXT,definition TEXT,example TEXT,variant TEXT)')
    builder.merge(conn, '的', 'de', '原创安装测试')
    package = work / 'fixture.mgdict'
    builder.write_package(conn, package)
    names = ['chinese.LICENSE.txt', 'chinese.manifest.json', 'chinese.mgdict', 'licenses/CC-BY-SA-4.0.txt',
             'licenses/OpenCC-Apache-2.0.txt', 'licenses/OpenCC-NOTICE.txt', 'licenses/Unicode-3.0.txt', 'licenses/mapull-MIT.txt']
    manifest = {'schemaVersion': 1, 'format': 1, 'packageId': 'a' * 64, 'version': 'test', 'files': [
        {'name': name, 'size': package.stat().st_size if name.endswith('.mgdict') else 8, 'sha256': 'b' * 64,
         'url': '/api/v2/components/dictionary/packages/' + 'a' * 64 + '/files/' + name} for name in names]}
    (work / 'manifest.json').write_text(json.dumps(manifest))
    manifest['files'][0]['url'] = 'https://other.example/escape'
    (work / 'bad.json').write_text(json.dumps(manifest))
    (work / 'test.c').write_text(UNIT)
    cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
    flags = ['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
             '-I' + str(ROOT / 'main/book'), '-I' + str(ROOT / 'tools/book_layout_stubs'),
             '-I' + str(ROOT / 'tools/book_source_host_stubs'), '-I' + str(cjson)]
    subprocess.run(flags + ['-Wno-deprecated-declarations', '-c', str(cjson / 'cJSON.c'), '-o', str(work / 'json.o')], check=True)
    subprocess.run(flags + [str(work / 'test.c'), str(ROOT / 'main/book/dictionary_package.c'),
        str(ROOT / 'main/book/book_dictionary.c'), str(ROOT / 'main/book/reader_selection.c'), str(work / 'json.o'), '-lz', '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test'), str(work / 'dictionaries'), str(package), str(work / 'manifest.json'), str(work / 'bad.json')], check=True, timeout=30)
