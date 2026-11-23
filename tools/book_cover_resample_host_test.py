#!/usr/bin/env python3
"""中文：实际双线性映射与流式 PNG 的边界、损坏文件和内存失败。
English: Actual bilinear mapping and streamed PNG bounds, corrupt input and allocation failures.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re,struct,subprocess,tempfile,zlib
root=Path(__file__).resolve().parents[1]
source=(root/'main/image/image_decode.c').read_text()
def fn(name):
    start=re.search(r'^(?:static )?(?:inline )?[^\n]+\b'+name+r'\([^;]*?\)\s*\{',source,re.M).start()
    brace=source.index('{',start);depth=1;at=brace+1
    # 已选函数无代码中的花括号字符串。/ These helpers have no braces inside string literals.
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start:at]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"
static int fail_after=-1,live;
static unsigned luma_calls;
static void *cover_alloc(size_t n){if(fail_after==0)return NULL;if(fail_after>0)--fail_after;void *p=malloc(n);if(p)++live;return p;}
static void tracked_free(void *p){if(p)--live;free(p);}
#define free tracked_free
static void decode_yield(unsigned at){(void)at;}

'''
unit += '#include "' + str(root/'main/image/image_decode.h') + '"\n'
unit+=fn('luminance').replace('return', '++luma_calls; return')+'\n'
for name in ('scale_gap','scale_axis'):unit+=fn(name)+'\n'
unit+='typedef struct {uint16_t at;uint8_t frac;} scale_step_t;\n'
for name in ('scale_map','scale_next','bilinear4','book_cover_crop'):unit+=fn(name)+'\n'
unit+=fn('image_enhance_comic')+'\n'
unit+='enum {JPEG_IMAGE_SCALE_0=0, JPEG_IMAGE_SCALE_1_8=3};\n'+fn('jpeg_target_scale')+'\n'
unit+='typedef bool (*image_cancel_fn)(void *);\ntypedef struct {const uint8_t *data;size_t size,pos;FILE *file;image_cancel_fn cancelled;void *context;} png_input_t;\n'
for name in ('png_read_mem','png_gray_input_cancel','png_gray_input'):unit+=fn(name)+'\n'
unit+=r'''
#undef free
int main(int argc,char **argv){
 assert(argc==4);
 uint8_t comic[256],native_tone[256];
 memset(comic,255,sizeof(comic));memset(comic,60,64);comic[100]=168;
 memcpy(native_tone,comic,sizeof(comic));
 image_enhance_comic(native_tone,16,16,false);
 assert(native_tone[0]==15 && native_tone[100]>100 && native_tone[100]<168 && native_tone[255]==255);
 image_enhance_comic(comic,16,16,true);
 assert(comic[0]==15 && comic[100]==native_tone[100]-17 && comic[99]==255 && !live);
 memset(comic,255,sizeof(comic));memset(comic,60,64);comic[100]=168;
 fail_after=0;image_enhance_comic(comic,16,16,true);fail_after=-1;
 assert(!memcmp(comic,native_tone,sizeof(comic)) && !live);
 for(unsigned i=0;i<256;++i)comic[i]=(uint8_t)i;
 image_enhance_comic(comic,1,256,true);
 for(unsigned i=1;i<256;++i)assert(comic[i]>=comic[i-1]);
 memset(comic,0,sizeof(comic));comic[255]=255;image_enhance_comic(comic,16,16,true);
 assert(comic[0]==0 && comic[255]==255 && !live);
 memset(comic,210,sizeof(comic));image_enhance_comic(comic,16,16,true);
 for(unsigned i=0;i<256;++i)assert(comic[i]==210);

 assert(jpeg_target_scale(684,1216,684,1216)==0);
 assert(jpeg_target_scale(700,600,176,240)==0);
 assert(jpeg_target_scale(5000,960,176,240)==1);
 assert(jpeg_target_scale(1920,1920,176,240)==2);
 assert(jpeg_target_scale(2816,3840,176,240)==3);
 const uint8_t grid[]={0, 64, 128, 255};
 image_gray_axis_t axis=image_gray_axis(2,3);
 assert(image_gray_sample(image_gray_row(grid,2,axis,0),axis,0)==0);
 assert(image_gray_sample(image_gray_row(grid,2,axis,1),axis,1)==111);
 assert(image_gray_sample(image_gray_row(grid,2,axis,2),axis,2)==255);
 axis=image_gray_axis(2,2);
 assert(image_gray_sample(image_gray_row(grid,2,axis,0),axis,1)==64);
 assert(image_gray_sample(image_gray_row(grid,2,axis,1),axis,0)==128);
 for(unsigned src=1;src<90;src++)for(unsigned dst=1;dst<130;dst++)for(unsigned i=0;i<dst;i++){
  int at,f;scale_axis(i,dst,src,scale_gap(dst,src),&at,&f);assert(at>=0&&(unsigned)at<src&&f>=0&&f<256);
 }
 FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 uint8_t *data=malloc(size);assert(fread(data,1,size,f)==(size_t)size);fclose(f);
 uint8_t pixels[80];memset(pixels,0xaa,sizeof(pixels));assert(png_gray_input(data,size,NULL,8,8,pixels,NULL));assert(!live);
 assert(pixels[0]==0&&pixels[63]==240&&pixels[27]>80&&pixels[27]<140);
 for(int i=64;i<80;i++)assert(pixels[i]==0xaa);
 assert(png_gray_input(data,size,NULL,2,2,pixels,NULL));assert(!live&&pixels[0]==40&&pixels[3]==200);
 assert(png_gray_input(data,size,NULL,1,1,pixels,NULL));assert(!live);
 for(int i=0;i<5;i++){fail_after=i;assert(!png_gray_input(data,size,NULL,8,8,pixels,NULL));assert(!live);}
 fail_after=-1;
 // IDAT 截断发生在分配映射及两行缓存之后，验证 longjmp 释放路径。
 // IDAT truncation occurs after map/row allocation, exercising longjmp cleanup.
 assert(!png_gray_input(data,45,NULL,8,8,pixels,NULL));assert(!live);
 f=fopen(argv[1],"rb");assert(png_gray_input(NULL,size,f,8,8,pixels,NULL));fclose(f);assert(!live);
 uint8_t masks[80];memset(masks,0xaa,sizeof(masks));
 assert(png_gray_input(data,size,NULL,8,8,pixels,masks));assert(!live);
 for(int i=0;i<64;++i)assert(masks[i]==255);
 for(int i=64;i<80;++i)assert(masks[i]==0xaa);
 for(int i=0;i<7;++i){fail_after=i;assert(!png_gray_input(data,size,NULL,8,8,pixels,masks));assert(!live);}fail_after=-1;
 free(data);
 f=fopen(argv[2],"rb");assert(f);fseek(f,0,SEEK_END);size=ftell(f);rewind(f);
 uint8_t *native=malloc(684u*1216u+16);memset(native,0xaa,684u*1216u+16);
 assert(png_gray_input(NULL,size,f,684,1216,native,NULL));fclose(f);assert(!live);
 for(unsigned y=0;y<1216;++y)for(unsigned x=0;x<684;++x){
  unsigned gray=y<608?((x+y)&1?220:255):(x%256);
  assert(native[y*684+x]==gray);
 }
 for(unsigned i=684u*1216u;i<684u*1216u+16;++i)assert(native[i]==0xaa);
 free(native);
 f=fopen(argv[3],"rb");assert(f);fseek(f,0,SEEK_END);size=ftell(f);rewind(f);
 data=malloc(size);assert(data&&fread(data,1,size,f)==(size_t)size);rewind(f);
 assert(png_gray_input(NULL,size,f,4,4,pixels,NULL));fclose(f);assert(!live);
 for(int i=0;i<16;++i)assert(pixels[i]==255);
 memset(pixels,0xaa,sizeof(pixels));
 luma_calls=0;
 assert(png_gray_input(data,size,NULL,4,4,pixels,NULL));free(data);assert(!live&&luma_calls==16u*8u);
 for(int i=0;i<16;++i)assert(pixels[i]==64);
 for(int i=16;i<80;++i)assert(pixels[i]==0xaa);
 puts("book_cover_resample: PASS (up/down/degenerate scaling, streamed PNG, bounds, truncation, OOM, native 684x1216 detail, thin-line reduction with half the row conversions and target JPEG scale)");
}
'''
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
raw=b''.join(b'\0'+b''.join(bytes([x*32+y*48]*3+[255]) for x in range(4)) for y in range(4))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',4,4,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);(tmp/'test.c').write_text(unit);(tmp/'fixture.png').write_bytes(png)
    native_raw=b''.join(b'\0'+b''.join(bytes([220 if (x+y)&1 else 255]*3+[255]) if y<608 else bytes([x%256]*3+[255]) for x in range(684)) for y in range(1216))
    native_png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',684,1216,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(native_raw))+chunk(b'IEND',b'')
    (tmp/'native.png').write_bytes(native_png)
    lines=b''.join(b'\0'+b''.join(bytes([0 if x%4==3 or y%4==3 else 255]*3) for x in range(16)) for y in range(16))
    (tmp/'lines.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',16,16,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(lines))+chunk(b'IEND',b''))
    vendor=root/'managed_components/espressif__libpng/libpng'
    if not vendor.exists():vendor=Path('/private/tmp/kiikoread-rc90-lock-build-20261011/managed_components/espressif__libpng/libpng')
    assert vendor.exists(),'Build IDF once to resolve libpng'
    (tmp/'pnglibconf.h').write_bytes((vendor/'scripts/pnglibconf.h.prebuilt').read_bytes())
    names='png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwrite pngwio pngwtran pngwutil'.split()
    subprocess.run(['cc','-std=gnu11','-O2','-w','-fsanitize=address,undefined','-DPNG_ARM_NEON_OPT=0','-DPNG_INTEL_SSE_OPT=0','-I'+str(tmp),'-I'+str(vendor),str(tmp/'test.c')]+[str(vendor/(n+'.c')) for n in names]+['-lz','-lm','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test'),str(tmp/'fixture.png'),str(tmp/'native.png'),str(tmp/'lines.png')],check=True)
