/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：锁屏选图的可取消文件任务，12张内存小图和有界TF缓存；任务不使用字库或绘屏。
 * English: Cancellable lock-picker I/O, twelve RAM thumbnails and a bounded SD cache; no fonts or display calls.
 */
#include "lock_style_images.h"
#include "image_decode.h"
#include "book_lock_bookmark.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef LOCK_THUMB_ROOT
#define LOCK_THUMB_ROOT "/sdcard/.readpico"
#endif
#define CACHE_DIR LOCK_THUMB_ROOT "/thumbnails"
#define RAM_SLOTS 12
#define DISK_SLOTS 128
#define CACHE_VERSION 1
#define CACHE_MAGIC UINT32_C(0x4c544831)
typedef struct {
    uint32_t magic, version, width, height, bookmark, bytes;
    uint64_t key, checksum;
} cache_header_t;
typedef struct {
    char path[288];
    bool bookmark, large, probed;
    lock_image_state_t state;
    uint8_t *pixels;
    uint64_t key;
    unsigned used;
} image_slot_t;
static image_slot_t s_slots[RAM_SLOTS];
static int s_wanted[LOCK_IMAGE_REQUESTS], s_count;
static unsigned s_generation, s_revision, s_used, s_epoch;
static bool s_running, s_paused = true, s_scan, s_scanned;
static lock_image_item_t *s_catalogue;
static int s_catalogue_count;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool cancelled(void *context) {
    unsigned generation = *(unsigned *)context;
    portENTER_CRITICAL(&s_lock);
    bool stop = s_paused || generation != s_generation;
    portEXIT_CRITICAL(&s_lock);
    return stop;
}
static uint64_t hash(uint64_t h, const void *data, size_t size) {
    const uint8_t *p = data;
    while (size--) h = (h ^ *p++) * UINT64_C(1099511628211);
    return h;
}
static uint64_t source_key(const char *path,const struct stat *st) {
    uint64_t key=hash(UINT64_C(14695981039346656037),path,strlen(path)+1);
    int64_t size=st->st_size,modified=st->st_mtime;
    return hash(hash(key,&size,sizeof(size)),&modified,sizeof(modified));
}
static bool identity(const char *path, bool bookmark, bool large, cache_header_t *h, char file[160]) {
    struct stat st;
    const char *ext = strrchr(path, '.');
    if (strncmp(path,"/sdcard/",8) || !ext ||
        (strcasecmp(ext,".png") && strcasecmp(ext,".jpg") && strcasecmp(ext,".jpeg")) ||
        stat(path,&st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) return false;
    uint64_t key=source_key(path,&st);
    *h=(cache_header_t){.magic=CACHE_MAGIC,.version=CACHE_VERSION,.width=large?342:160,
        .height=large?608:284,.bookmark=bookmark,.key=key};
    h->bytes=h->width*h->height/2;
    // 128个直接映射槽位限制磁盘增长；完整身份校验使碰撞只导致重建，不会串图。
    // Direct-mapped slots bound disk use; full identities turn collisions into misses, never wrong art.
    unsigned slot=(unsigned)(hash(key,&bookmark,sizeof(bookmark))%DISK_SLOTS);
    snprintf(file,160,CACHE_DIR "/%03u.bin",slot);
    return true;
}
static bool cache_read(const char *file,const cache_header_t *wanted,uint8_t *pixels) {
    FILE *f=fopen(file,"rb");if(!f)return false;
    cache_header_t h;
    bool ok=fread(&h,1,sizeof(h),f)==sizeof(h) && h.magic==wanted->magic && h.version==wanted->version &&
        h.width==wanted->width && h.height==wanted->height && h.bookmark==wanted->bookmark &&
        h.bytes==wanted->bytes && h.key==wanted->key && fread(pixels,1,h.bytes,f)==h.bytes &&
        fgetc(f)==EOF && !ferror(f) && h.checksum==hash(UINT64_C(14695981039346656037),pixels,h.bytes);
    fclose(f);return ok;
}
static void cache_write(const char *file,cache_header_t h,const uint8_t *pixels) {
    if((mkdir(LOCK_THUMB_ROOT,0777)&&errno!=EEXIST)||(mkdir(CACHE_DIR,0777)&&errno!=EEXIST))return;
    char temp[168];snprintf(temp,sizeof(temp),"%s.tmp",file);
    FILE *f=fopen(temp,"wb");if(!f)return;
    h.checksum=hash(UINT64_C(14695981039346656037),pixels,h.bytes);
    bool ok=fwrite(&h,1,sizeof(h),f)==sizeof(h)&&fwrite(pixels,1,h.bytes,f)==h.bytes;
    if(fclose(f))ok=false;
    if(ok){(void)remove(file);if(!rename(temp,file))return;}
    (void)remove(temp);
}
static uint8_t *load_image(const char *path,bool bookmark,bool large,unsigned generation,bool cache_only,bool *miss,uint64_t *key) {
    *miss=false;
    cache_header_t h,after;char file[160],next[160];
    if(cancelled(&generation)||!identity(path,bookmark,large,&h,file))return NULL;
    *key=h.key;
    uint8_t *pixels=heap_caps_malloc(h.bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!pixels)return NULL;
    if(!large&&cache_read(file,&h,pixels)) {
        if(!cancelled(&generation)&&identity(path,bookmark,large,&after,next)&&after.key==h.key)return pixels;
        free(pixels);return NULL;
    }
    if(cache_only){*miss=true;free(pixels);return NULL;}
    uint8_t *gray=heap_caps_malloc((size_t)h.width*h.height,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    const char *ext=strrchr(path,'.');
    bool ok=gray&&!cancelled(&generation) && (bookmark
        ? book_lock_bookmark_thumbnail(path,h.width,h.height,gray,cancelled,&generation)
        : book_image_file_grayscale_cancel(path,!strcasecmp(ext,".png"),h.width,h.height,gray,cancelled,&generation));
    if(ok) for(unsigned i=0;i<h.width*h.height;i+=2)
        pixels[i/2]=(uint8_t)(((gray[i]+8u)/17u)|(((gray[i+1]+8u)/17u)<<4));
    free(gray);
    // 不发布解码中被替换的文件，也不让取消/半写结果成为缓存。
    // Reject sources replaced during decoding and never cache cancelled/partial output.
    ok=ok&&!cancelled(&generation)&&identity(path,bookmark,large,&after,next)&&after.key==h.key;
    if(!ok){free(pixels);return NULL;}
    if(!large)cache_write(file,h,pixels);
    return pixels;
}
static int compare_items(const void *a,const void *b) {
    const lock_image_item_t *x=a,*y=b;
    int order=strcasecmp(x->name,y->name);return order?order:strcmp(x->path,y->path);
}
static lock_image_item_t *scan_images(unsigned generation,int *count) {
    *count=0;
    lock_image_item_t *items=heap_caps_calloc(LOCK_IMAGES_MAX,sizeof(*items),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!items)return NULL;
    const char *roots[]={"/sdcard/pictures","/sdcard/images","/sdcard"};
    for(unsigned r=0;r<3&&!cancelled(&generation);++r){
        DIR *dir=opendir(roots[r]);if(!dir)continue;
        struct dirent *e;
        while(*count<LOCK_IMAGES_MAX&&!cancelled(&generation)&&(e=readdir(dir))){
            const char *ext=strrchr(e->d_name,'.');
            if(e->d_name[0]=='.'||!ext||(strcasecmp(ext,".png")&&strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))continue;
            lock_image_item_t *item=&items[*count];
            if(snprintf(item->path,sizeof(item->path),"%s/%s",roots[r],e->d_name)>=(int)sizeof(item->path))continue;
            struct stat st;
            if(stat(item->path,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>BOOK_IMAGE_FILE_MAX)continue;
            item->key=source_key(item->path,&st);
            snprintf(item->name,sizeof(item->name),"%.*s",(int)sizeof(item->name)-1,e->d_name);
            size_t length=strlen(item->name);
            if(strlen(e->d_name)>length){while(length&&((unsigned char)e->d_name[length]&0xc0)==0x80)--length;item->name[length]=0;}
            ++*count;
        }
        closedir(dir);
    }
    qsort(items,*count,sizeof(*items),compare_items);
    return items;
}
static void worker(void *arg) {
    (void)arg;
    for(;;){
        unsigned generation;int slot=-1;bool scan,cache_only=false;char path[288];bool bookmark=false,large=false;
        portENTER_CRITICAL(&s_lock);
        generation=s_generation;scan=!s_paused&&s_scan;
        // 先检查整批缓存，再解码缺图；第一张冷图不能挡住后面的热图。
        // Check every cache before decoding misses, so a cold first image cannot block warm peers.
        if(!s_paused&&!scan)for(int pass=0;pass<2&&slot<0;++pass)
            for(int i=0;i<s_count;++i)if(s_slots[s_wanted[i]].state==LOCK_IMAGE_PENDING&&
                (pass||!s_slots[s_wanted[i]].probed)){slot=s_wanted[i];cache_only=!pass;break;}
        if(!scan&&slot<0){s_running=false;portEXIT_CRITICAL(&s_lock);break;}
        if(slot>=0){memcpy(path,s_slots[slot].path,sizeof(path));bookmark=s_slots[slot].bookmark;large=s_slots[slot].large;}
        portEXIT_CRITICAL(&s_lock);
        if(scan){
            int count;lock_image_item_t *items=scan_images(generation,&count);
            portENTER_CRITICAL(&s_lock);
            if(!s_paused&&generation==s_generation){s_catalogue=items;items=NULL;s_catalogue_count=count;s_scan=false;s_scanned=true;++s_revision;}
            portEXIT_CRITICAL(&s_lock);
            free(items);
        }else{
            bool miss=false;
            uint64_t key=0;
            uint8_t *pixels=load_image(path,bookmark,large,generation,cache_only,&miss,&key);
            portENTER_CRITICAL(&s_lock);
            if(!s_paused&&generation==s_generation){
                s_slots[slot].probed=true;
                s_slots[slot].key=key;
                s_slots[slot].pixels=pixels;s_slots[slot].state=pixels?LOCK_IMAGE_READY:miss?LOCK_IMAGE_PENDING:LOCK_IMAGE_FAILED;
                pixels=NULL;if(!miss)++s_revision;
            }
            portEXIT_CRITICAL(&s_lock);
            free(pixels);
        }
        vTaskDelay(1);
    }
    vTaskDelete(NULL);
}
static void start_worker(void) {
    portENTER_CRITICAL(&s_lock);
    bool start=!s_running&&!s_paused;
    if(start)s_running=true;
    portEXIT_CRITICAL(&s_lock);
    if(start&&xTaskCreate(worker,"lock_images",12288,NULL,1,NULL)!=pdPASS){
        portENTER_CRITICAL(&s_lock);
        s_running=false;s_scan=false;s_scanned=true;
        for(int i=0;i<s_count;++i)if(s_slots[s_wanted[i]].state==LOCK_IMAGE_PENDING)s_slots[s_wanted[i]].state=LOCK_IMAGE_FAILED;
        ++s_revision;portEXIT_CRITICAL(&s_lock);
    }
}
void lock_images_close(void) {
    uint8_t *old[RAM_SLOTS];lock_image_item_t *catalogue;
    portENTER_CRITICAL(&s_lock);
    s_paused=true;++s_generation;++s_epoch;s_count=0;s_scan=s_scanned=false;
    catalogue=s_catalogue;s_catalogue=NULL;s_catalogue_count=0;
    for(int i=0;i<RAM_SLOTS;++i){old[i]=s_slots[i].pixels;memset(&s_slots[i],0,sizeof(s_slots[i]));}
    ++s_revision;portEXIT_CRITICAL(&s_lock);
    free(catalogue);for(int i=0;i<RAM_SLOTS;++i)free(old[i]);
}
void lock_images_pause(void) {
    uint8_t *old[RAM_SLOTS]={0};lock_image_item_t *catalogue;
    portENTER_CRITICAL(&s_lock);
    s_paused=true;++s_generation;s_count=0;s_scan=s_scanned=false;
    catalogue=s_catalogue;s_catalogue=NULL;s_catalogue_count=0;
    for(int i=0;i<RAM_SLOTS;++i)if(s_slots[i].large||s_slots[i].state!=LOCK_IMAGE_READY){
        old[i]=s_slots[i].pixels;memset(&s_slots[i],0,sizeof(s_slots[i]));
    }
    portEXIT_CRITICAL(&s_lock);
    free(catalogue);for(int i=0;i<RAM_SLOTS;++i)free(old[i]);
}
unsigned lock_images_epoch(void) {
    portENTER_CRITICAL(&s_lock);unsigned epoch=s_epoch;portEXIT_CRITICAL(&s_lock);return epoch;
}
void lock_images_open(void) {
    lock_images_pause();
    portENTER_CRITICAL(&s_lock);s_paused=false;s_scan=true;portEXIT_CRITICAL(&s_lock);
    start_worker();
}
lock_image_item_t *lock_images_catalogue(int *count,bool *finished) {
    uint8_t *old[RAM_SLOTS]={0};
    portENTER_CRITICAL(&s_lock);
    lock_image_item_t *items=s_catalogue;s_catalogue=NULL;*count=s_catalogue_count;*finished=s_scanned;
    // 只有UI取回目录时才能释放旧像素；工作者不能让正在绘制的借用指针失效。
    // Only the UI may retire pixels after taking a scan; workers cannot invalidate paint borrows.
    if(items)for(int i=0;i<RAM_SLOTS;++i)if(s_slots[i].state==LOCK_IMAGE_READY){
        bool valid=false;
        for(int j=0;j<*count;++j)if(!strcmp(s_slots[i].path,items[j].path)&&s_slots[i].key==items[j].key){valid=true;break;}
        if(!valid){
            old[i]=s_slots[i].pixels;s_slots[i].pixels=NULL;s_slots[i].probed=false;s_slots[i].state=LOCK_IMAGE_PENDING;
            ++s_revision;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    for(int i=0;i<RAM_SLOTS;++i)free(old[i]);
    return items;
}
void lock_images_request(const lock_image_request_t *requests,int count) {
    if(count<0)count=0;
    if(count>LOCK_IMAGE_REQUESTS)count=LOCK_IMAGE_REQUESTS;
    lock_image_request_t unique[LOCK_IMAGE_REQUESTS];int unique_count=0;
    for(int i=0;i<count;++i){
        if(!requests[i].path||!requests[i].path[0])continue;
        bool duplicate=false;
        for(int j=0;j<unique_count;++j)if(!strcmp(unique[j].path,requests[i].path)&&unique[j].bookmark==requests[i].bookmark&&unique[j].large==requests[i].large)duplicate=true;
        if(!duplicate)unique[unique_count++]=requests[i];
    }
    requests=unique;count=unique_count;
    uint8_t *old[LOCK_IMAGE_REQUESTS]={0};int wanted[LOCK_IMAGE_REQUESTS],n=0;
    bool retained[RAM_SLOTS]={0};
    portENTER_CRITICAL(&s_lock);
    if(s_paused){portEXIT_CRITICAL(&s_lock);return;}
    // 先固定所有命中项，再分配空槽，避免刚命中的图被同批请求淘汰。
    // Pin every hit before allocating misses so a batch cannot evict its own hits.
    for(int i=0;i<count;++i){
        wanted[i]=-1;
        if(!requests[i].path||!requests[i].path[0]||strlen(requests[i].path)>=sizeof(s_slots[0].path))continue;
        for(int j=0;j<RAM_SLOTS;++j)if(!strcmp(requests[i].path,s_slots[j].path)&&requests[i].bookmark==s_slots[j].bookmark&&requests[i].large==s_slots[j].large){wanted[i]=j;retained[j]=true;break;}
    }
    ++s_generation;
    for(int i=0;i<count;++i){
        if(!requests[i].path||!requests[i].path[0]||strlen(requests[i].path)>=sizeof(s_slots[0].path))continue;
        int slot=wanted[i];
        if(slot<0){
            for(int j=0;j<RAM_SLOTS;++j)if(!retained[j]&&(slot<0||s_slots[j].used<s_slots[slot].used))slot=j;
            if(slot<0)continue;
            old[i]=s_slots[slot].pixels;memset(&s_slots[slot],0,sizeof(s_slots[slot]));
            snprintf(s_slots[slot].path,sizeof(s_slots[slot].path),"%s",requests[i].path);
            s_slots[slot].bookmark=requests[i].bookmark;s_slots[slot].large=requests[i].large;
            retained[slot]=true;
        }
        s_slots[slot].used=++s_used;s_wanted[n++]=slot;
    }
    s_count=n;
    // 大预览只保留当前一张，退出即释放，避免12个大图占满PSRAM。
    // Retain only the requested large preview; never fill the LRU with full previews.
    uint8_t *large_old[RAM_SLOTS]={0};
    for(int j=0;j<RAM_SLOTS;++j)if(!retained[j]&&s_slots[j].large){large_old[j]=s_slots[j].pixels;memset(&s_slots[j],0,sizeof(s_slots[j]));}
    portEXIT_CRITICAL(&s_lock);
    for(int i=0;i<count;++i)free(old[i]);
    for(int j=0;j<RAM_SLOTS;++j)free(large_old[j]);
    start_worker();
}
const uint8_t *lock_images_pixels(const char *path,bool bookmark,bool large,lock_image_state_t *state) {
    const uint8_t *pixels=NULL;*state=LOCK_IMAGE_PENDING;
    portENTER_CRITICAL(&s_lock);
    for(int i=0;path&&i<RAM_SLOTS;++i)if(!strcmp(path,s_slots[i].path)&&bookmark==s_slots[i].bookmark&&large==s_slots[i].large){*state=s_slots[i].state;pixels=s_slots[i].pixels;break;}
    portEXIT_CRITICAL(&s_lock);return pixels;
}
unsigned lock_images_revision(void) {
    portENTER_CRITICAL(&s_lock);unsigned revision=s_revision;portEXIT_CRITICAL(&s_lock);return revision;
}
bool lock_images_quiesce(unsigned timeout_ms) {
    portENTER_CRITICAL(&s_lock);s_paused=true;++s_generation;bool running=s_running;portEXIT_CRITICAL(&s_lock);
    int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    while(running){
        if(esp_timer_get_time()>=deadline)return false;
        vTaskDelay(1);
        portENTER_CRITICAL(&s_lock);running=s_running;portEXIT_CRITICAL(&s_lock);
    }
    return true;
}
