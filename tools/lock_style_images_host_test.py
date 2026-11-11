#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：确定性调度下验证实际缩图任务、缓存、源文件竞争和取消。
English: Actual thumbnail worker/cache under deterministic scheduling, source races and cancellation."""
from pathlib import Path
import re,subprocess,tempfile
from run_reader_baseline import toolchain
ROOT=Path(__file__).resolve().parents[1]
header=re.sub(r'^#(?:include|pragma)[^\n]*\n','',(ROOT/'main/services/lock_style_images.h').read_text(),flags=re.M)
source=re.sub(r'^#include[^\n]*\n','',(ROOT/'main/services/lock_style_images.c').read_text(),flags=re.M)
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <errno.h>
#define LOCK_THUMB_ROOT "cache"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BOOK_IMAGE_FILE_MAX (50u*1024u*1024u)
typedef bool (*image_cancel_fn)(void *);
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool in_lock;
#define portENTER_CRITICAL(x) do{(void)(x);assert(!in_lock);in_lock=true;}while(0)
#define portEXIT_CRITICAL(x) do{(void)(x);assert(in_lock);in_lock=false;}while(0)
#define pdPASS 1
static void (*scheduled)(void *);
static int tasks,decodes,reads,mode,alloc_fail,task_fail;
static bool decoder_fail;
static int64_t now_us;
static size_t live,peak;
static void *heap_caps_malloc(size_t n,int caps){(void)caps;if(alloc_fail)return NULL;size_t *p=malloc(n+sizeof(size_t));assert(p);*p=n;live+=n;if(live>peak)peak=live;return p+1;}
static void *heap_caps_calloc(size_t n,size_t s,int caps){void *p=heap_caps_malloc(n*s,caps);if(p)memset(p,0,n*s);return p;}
static void release(void *p){if(p){size_t *n=(size_t *)p-1;live-=*n;free(n);}}
static int xTaskCreate(void (*fn)(void *),const char *name,int stack,void *arg,int priority,void *handle){(void)name;(void)stack;(void)arg;(void)priority;(void)handle;assert(!scheduled);if(task_fail)return 0;scheduled=fn;++tasks;return pdPASS;}
static void vTaskDelete(void *p){assert(!p&&!in_lock);}
static void vTaskDelay(int ticks){assert(ticks==1&&!in_lock);now_us+=1000;}
static int64_t esp_timer_get_time(void){return now_us;}
static const char *map_path(const char *path,char out[1024]){if(!strncmp(path,"/sdcard",7)){snprintf(out,1024,"card%s",path+7);return out;}return path;}
static FILE *open_file(const char *path,const char *mode){char out[1024];assert(!in_lock);++reads;return fopen(map_path(path,out),mode);}
static int stat_file(const char *path,struct stat *st){char out[1024];assert(!in_lock);return stat(map_path(path,out),st);}
static DIR *open_dir(const char *path){char out[1024];assert(!in_lock);return opendir(map_path(path,out));}
static void mutation(void);
static bool book_image_file_grayscale_cancel(const char *p,bool png,unsigned w,unsigned h,uint8_t *out,image_cancel_fn cancel,void *context){
 (void)p;(void)png;++decodes;assert(!in_lock);mutation();if(cancel(context)||decoder_fail)return false;
 for(unsigned i=0;i<w*h;++i)out[i]=(uint8_t)i;return true;
}
static bool book_lock_bookmark_thumbnail(const char *p,unsigned w,unsigned h,uint8_t *out,image_cancel_fn c,void *ctx){return book_image_file_grayscale_cancel(p,true,w,h,out,c,ctx);}
#define free release
#define fopen open_file
#define stat(p,s) stat_file(p,s)
#define opendir open_dir
'''+header+source+r'''
#undef fopen
#undef stat
#undef opendir
static void run(void){assert(scheduled);void (*fn)(void *)=scheduled;scheduled=NULL;fn(NULL);}
static void create(const char *path,int size){FILE *f=fopen(path,"wb");assert(f);for(int i=0;i<size;++i)fputc(i,f);assert(!fclose(f));}
static const char *a="/sdcard/pictures/a.jpg",*b="/sdcard/pictures/b.png",*c="/sdcard/pictures/c.jpg";
static void ask(const char *path){lock_image_request_t r={path,false,false};lock_images_request(&r,1);}
static void mutation(void){
 int saved=mode;mode=0;
 if(saved==1){ask(b);assert(s_running);}
 if(saved==2){lock_images_close();assert(!lock_images_quiesce(0));}
 if(saved==4){lock_image_state_t state;assert(lock_images_pixels(b,false,false,&state)&&state==LOCK_IMAGE_READY);}
 if(saved==3)create("card/pictures/a.jpg",19);
}
static const uint8_t *ready(const char *path){lock_image_state_t s;const uint8_t *p=lock_images_pixels(path,false,false,&s);assert(p&&s==LOCK_IMAGE_READY);return p;}
static void open_picker(void){lock_images_open();run();int count;bool done;lock_image_item_t *items=lock_images_catalogue(&count,&done);assert(done&&items&&count==3&&!strcmp(items[0].name,"a.jpg"));free(items);}
int main(void){
 mkdir("card",0700);mkdir("card/pictures",0700);create("card/pictures/a.jpg",10);create("card/pictures/b.png",11);create("card/pictures/c.jpg",12);
 create("card/pictures/._hidden.png",8);create("card/pictures/empty.jpg",0);create("card/pictures/readme.txt",10);mkdir("card/pictures/directory.jpg",0700);
 open_picker();assert(!decodes);
 lock_image_request_t r[]={{a,false,false},{a,false,false},{b,false,false},{c,false,false}};
 lock_images_request(r,4);assert(s_count==3&&decodes==0);run();assert(decodes==3);const uint8_t *p=ready(a);uint8_t expected[LOCK_THUMB_BYTES];memcpy(expected,p,sizeof(expected));
 int before=decodes,io=reads;ask(a);assert(ready(a)==p&&reads==io&&decodes==before);run();assert(decodes==before&&reads==io);
 // 普通离页再进入立即复用像素，无需等异步扫描结束。 / Ordinary leave/reentry reuses pixels immediately, including before the async scan returns.
 unsigned epoch=lock_images_epoch();lock_images_pause();assert(ready(a)==p&&live>0&&lock_images_epoch()==epoch);
 lock_images_open();assert(ready(a)==p&&decodes==before&&reads==io);run();int warm_count;bool warm_finished;
 lock_image_item_t *warm=lock_images_catalogue(&warm_count,&warm_finished);assert(warm&&warm_finished&&warm_count==3);free(warm);
 ask(a);run();assert(ready(a)==p&&decodes==before&&reads==io);
 // 扫描期间保留借用像素，只有UI取回目录才能释放变化源。 / The old borrow stays alive through scan; only the UI's take retires a modified source.
 lock_images_pause();create("card/pictures/a.jpg",14);lock_images_open();run();assert(ready(a)==p);
 warm=lock_images_catalogue(&warm_count,&warm_finished);free(warm);lock_image_state_t warm_state;
 assert(!lock_images_pixels(a,false,false,&warm_state)&&warm_state==LOCK_IMAGE_PENDING);ask(a);run();assert(decodes==++before);ready(a);
 lock_images_close();assert(live==0);open_picker();ask(a);run();assert(decodes==before&&!memcmp(expected,ready(a),sizeof(expected)));
 // 先发布后面的缓存命中图，再解码排在前面的冷图。 / A warm second image is published before decoding a cold first image.
 create("card/pictures/a.jpg",13);lock_images_close();open_picker();mode=4;
 lock_image_request_t ordered[]={{a,false,false},{b,false,false}};lock_images_request(ordered,2);run();assert(decodes==++before);
 // 损坏、截断或源替换均使缓存失效，严格校验内容和长度。 / Cache corruption/truncation/source replacement are misses; checksum and exact size are mandatory.
 cache_header_t h;char file[160];assert(identity(a,false,false,&h,file));FILE *f=fopen(file,"r+b");assert(f&&!fseek(f,sizeof(h)+7,SEEK_SET));int byte=fgetc(f);fseek(f,-1,SEEK_CUR);fputc(byte^64,f);fclose(f);
 lock_images_close();open_picker();ask(a);run();assert(decodes==++before);ready(a);
 create(file,4);lock_images_close();open_picker();ask(a);run();assert(decodes==++before);ready(a);
 create("card/pictures/a.jpg",18);lock_images_close();open_picker();ask(a);run();assert(decodes==++before);ready(a);
 struct utimbuf times={1234567,1234567};assert(!utime("card/pictures/a.jpg",&times));lock_images_close();open_picker();ask(a);run();assert(decodes==++before);ready(a);
 // 解码期间源变化时不发布结果，失败不随重绘反复重试。 / A changed source during decoding is never published; failures do not spin/retry on each repaint.
 create("card/pictures/a.jpg",17);lock_images_close();open_picker();mode=3;ask(a);run();lock_image_state_t state;assert(!lock_images_pixels(a,false,false,&state)&&state==LOCK_IMAGE_FAILED);before=decodes;ask(a);run();assert(decodes==before);
 // 解码期间切换请求，旧结果不得发布到复用槽位。 / Retarget while decoder owns an old request; stale pixels must not be attached to the reused slot.
 create("card/pictures/a.jpg",21);lock_images_close();open_picker();mode=1;ask(a);run();ready(b);assert(!lock_images_pixels(a,false,false,&state));
 lock_images_close();open_picker();mode=2;ask(a);run();assert(lock_images_quiesce(0)&&live==0&&!s_running);
 // 连续翻页时大预览生命周期和内存预算保持有界。 / Large preview lifetime and RAM budget remain bounded while flipping through the catalogue.
 open_picker();for(int i=0;i<30;++i){lock_image_request_t v[]={{i&1?a:b,false,true},{c,false,false}};lock_images_request(v,2);run();assert(live<=342u*608u/2+12u*LOCK_THUMB_BYTES);}
 ask(c);run();for(int i=0;i<RAM_SLOTS;++i)assert(!s_slots[i].large);lock_images_close();assert(live==0);
 // 内存不足和任务创建失败可结束等待，无泄漏和忙重试。 / OOM and worker creation failures settle visibly, without leaked buffers or hot retry loops.
 open_picker();alloc_fail=1;ask(a);run();assert(!lock_images_pixels(a,false,false,&state)&&state==LOCK_IMAGE_FAILED);alloc_fail=0;lock_images_close();assert(!live);
 task_fail=1;lock_images_open();int count;bool finished;assert(!lock_images_catalogue(&count,&finished)&&finished);ask(a);assert(!lock_images_pixels(a,false,false,&state)&&state==LOCK_IMAGE_FAILED);lock_images_close();assert(!live);task_fail=0;
 // 超过目录与内存容量时有界淘汰，保留最近使用项。 / Populate beyond RAM and catalogue capacity; eviction is bounded and recently used slots survive.
 for(int i=0;i<80;++i){char file[80];snprintf(file,sizeof(file),"card/pictures/extra%02d.jpg",i);create(file,i+1);}
 lock_images_open();run();lock_image_item_t *items=lock_images_catalogue(&count,&finished);assert(finished&&items&&count==LOCK_IMAGES_MAX);free(items);
 for(int i=0;i<30;++i){char path[80];snprintf(path,sizeof(path),"/sdcard/pictures/extra%02d.jpg",i);ask(path);run();ready(path);assert(live<=RAM_SLOTS*LOCK_THUMB_BYTES);}
 before=decodes;io=reads;ask("/sdcard/pictures/extra29.jpg");run();assert(decodes==before&&reads==io);
 ask("/sdcard/pictures/extra00.jpg");run();ready("/sdcard/pictures/extra00.jpg");assert(reads>io);
 lock_images_close();assert(!live);
 printf("PASS: deferred sorted scan; dedup/LRU/RAM and disk hits skip decoding; corrupt/source invalidation; cancel/close races; bounded previews; OOM/task failure. Peak tracked pixel/catalogue memory: %zu bytes\n",peak);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp);env,_=toolchain(d);(d/'test.c').write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(d/'test.c'),'-o',str(d/'test')],env=env,check=True)
    subprocess.run([str(d/'test')],cwd=d,env=env,check=True)
