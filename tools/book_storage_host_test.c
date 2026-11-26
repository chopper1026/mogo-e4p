/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * NVS 故障替身验证进度、序号及遗忘重试。/ Fault-injected NVS tests for progress, sequence and forget retries.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "book_progress.h"
#include "book_store.h"
#include "book_progress_store.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
static int mount_error;
esp_err_t book_store_prepare_data(void){return mount_error;}
bool epd_flash_begin(void){return true;}
void epd_flash_end(void){}
#include "nvs.h"
typedef struct { char key[16]; unsigned char data[512]; size_t len; } record_t;
static record_t records[8], *latest;
static char last[BOOK_STORE_PATH_MAX];
static bool has_last, has_sequence;
static uint32_t sequence;
static int blob_failure, seq_failure, last_failure, erase_failure, commit_failure, commits;
static record_t* record(const char* key, bool create) {
    for (int i=0;i<8;++i) if(records[i].len&&!strcmp(records[i].key,key))return &records[i];
    if(create)for(int i=0;i<8;++i)if(!records[i].len){strcpy(records[i].key,key);return &records[i];}
    return NULL;
}
esp_err_t nvs_open(const char* ns,int mode,nvs_handle_t* h){(void)mode;assert(!strcmp(ns,"rp_books"));*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,size_t* len){
    (void)h;record_t*r=record(key,false);if(!r)return ESP_ERR_NVS_NOT_FOUND;
    if(*len<r->len)return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out,r->data,r->len);*len=r->len;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,size_t len){
    (void)h;if(blob_failure)return blob_failure;record_t*r=record(key,true);assert(r&&len<=sizeof(r->data));
    memcpy(r->data,data,len);r->len=len;latest=r;return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h,const char* key,char* out,size_t* len){
    (void)h;assert(!strcmp(key,"last"));if(!has_last)return ESP_ERR_NVS_NOT_FOUND;
    if(*len<strlen(last)+1)return ESP_ERR_NVS_INVALID_LENGTH;
    strcpy(out,last);*len=strlen(last)+1;return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char* key,const char* value){
    (void)h;assert(!strcmp(key,"last"));if(last_failure)return last_failure;strcpy(last,value);has_last=true;return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t h,const char* key,uint32_t* value){
    (void)h;assert(!strcmp(key,"seq"));if(!has_sequence)return ESP_ERR_NVS_NOT_FOUND;*value=sequence;return ESP_OK;
}
esp_err_t nvs_set_u32(nvs_handle_t h,const char* key,uint32_t value){
    (void)h;assert(!strcmp(key,"seq"));if(seq_failure)return seq_failure;sequence=value;has_sequence=true;return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* key){
    (void)h;if(!strcmp(key,"last")){if(last_failure)return last_failure;if(!has_last)return ESP_ERR_NVS_NOT_FOUND;has_last=false;last[0]=0;return ESP_OK;}
    if(erase_failure)return erase_failure;
    record_t*r=record(key,false);if(!r)return ESP_ERR_NVS_NOT_FOUND;r->len=0;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;++commits;return commit_failure;}
/* 迭代器就是 records 槽位的指针；len 为 0 的槽位视为已擦除。
   The iterator is a pointer into records; a zero-length slot counts as erased. */
esp_err_t nvs_entry_find(const char* part,const char* ns,nvs_type_t type,nvs_iterator_t* it){
    (void)part;(void)ns;(void)type;if(!it)return ESP_ERR_INVALID_ARG;
    for(int i=0;i<8;++i)if(records[i].len){*it=(nvs_iterator_t)&records[i];return ESP_OK;}
    *it=NULL;return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_next(nvs_iterator_t* it){
    if(!it||!*it)return ESP_ERR_INVALID_ARG;
    for(record_t* p=(record_t*)*it+1;p<records+8;++p)if(p->len){*it=(nvs_iterator_t)p;return ESP_OK;}
    *it=NULL;return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t* out){
    if(!it||!out)return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));
    strcpy(out->key,((record_t*)it)->key);out->type=NVS_TYPE_BLOB;return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it){(void)it;}
static int listed;
static uint8_t listed_px;
static char listed_paths[8][BOOK_STORE_PATH_MAX];
static bool record_list_visit(const char* path,const book_progress_t* progress,void* ctx){
    listed_px=progress->px;
    if(listed<8)snprintf(listed_paths[listed],BOOK_STORE_PATH_MAX,"%s",path);
    ++listed;return ctx==NULL;
}
static void to_legacy(const char *path) {
    uint8_t raw[PROGRESS_CAP]; size_t len; char key[11]; progress_key(path,key);
    assert(!progress_store_get(path,raw,&len)); assert(!nvs_set_blob(1,key,raw,len));
    assert(!progress_store_put(path,NULL,0));
}
int main(void){
    char work[]="/tmp/mogo-fat-books-XXXXXX"; assert(mkdtemp(work) && !chdir(work));
    const char*sd="/sdcard/books/同名.txt",*flash="/flash/books/同名.txt";
    book_progress_watch_t* watch_a=book_progress_watch_create(sd);
    book_progress_watch_t* watch_b=book_progress_watch_create(flash);
    assert(watch_a&&watch_b&&!book_progress_watch_invalidated(watch_a));
    mount_error=ESP_FAIL;
    assert(book_progress_forget(flash)==ESP_FAIL);
    assert(!book_progress_watch_invalidated(watch_a)&&book_progress_watch_invalidated(watch_b));
    assert(book_progress_forget(sd)==ESP_FAIL&&book_progress_watch_invalidated(watch_a));
    mount_error=0;
    book_progress_watch_destroy(watch_b);book_progress_watch_destroy(watch_a);
    watch_a=book_progress_watch_create(sd);assert(watch_a&&!book_progress_watch_invalidated(watch_a));
    book_progress_watch_destroy(watch_a);
    book_progress_t p={.file_size=123456,.chapter=2047,.byte_off=9876,.px=48,.pct=83,.last_open_s=UINT32_MAX},q={.file_size=999};
    assert(!book_progress_load(sd,p.file_size,&q));
    assert(book_progress_save(sd,&p)==ESP_OK);
    assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==1);
    assert(q.chapter==p.chapter&&q.byte_off==p.byte_off&&q.px==48&&q.pct==83);
    assert(!book_progress_load(sd,p.file_size+1,&q)&&q.file_size==p.file_size);
    nvs_handle_t progress_handle;
    assert(nvs_open("rp_books", NVS_READONLY, &progress_handle) == ESP_OK);
    assert(book_progress_load_handle((uintptr_t)progress_handle, sd, p.file_size, &q) && q.last_open_s == 1);
    nvs_close(progress_handle);
    assert(book_progress_load_handle(0, sd, p.file_size, &q));
    p.last_open_s=0;assert(book_progress_save(sd,&p)==ESP_OK);
    assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==2);
    to_legacy(sd);
    latest->data[3]=1;assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==0&&q.byte_off==p.byte_off);
    latest->data[3]=3;assert(!book_progress_load(sd,p.file_size,&q));assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_STATE);
    latest->data[3]=2;assert(book_progress_set_last_path(sd)==ESP_OK);latest->data[24]='!';
    assert(book_progress_forget(sd)==ESP_ERR_INVALID_STATE&&!strcmp(last,sd));assert(book_progress_clear(sd)==ESP_ERR_INVALID_STATE);latest->data[24]='/';
    seq_failure=ESP_ERR_NVS_NOT_ENOUGH_SPACE;assert(book_progress_save(sd,&p)==seq_failure&&sequence==2);
    seq_failure=0;
    char key[11], tmp[40]; progress_key(sd,key);
    snprintf(tmp,sizeof(tmp),"progress/%02lx.tmp",strtoul(key+2,NULL,16)%32);
    assert(!mkdir(tmp,0700)); assert(book_progress_save(sd,&p)!=ESP_OK && sequence==3);
    assert(!rmdir(tmp));
    assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==2);blob_failure=0;
    assert(book_progress_save(sd,&p)==ESP_OK);assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==4);
    commit_failure=ESP_FAIL;assert(book_progress_save(sd,&p)==ESP_FAIL);
    assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==4);commit_failure=0;
    assert(book_progress_save(flash,&p)==ESP_OK);assert(book_progress_set_last_path(flash)==ESP_OK);
    assert(book_progress_forget(sd)==ESP_OK);assert(!book_progress_load(sd,p.file_size,&q));
    assert(book_progress_load(flash,p.file_size,&q)&&!strcmp(last,flash));
    assert(book_progress_save(sd,&p)==ESP_OK);assert(book_progress_set_last_path(sd)==ESP_OK);
    last_failure=ESP_FAIL;assert(book_progress_forget(sd)==ESP_FAIL&&has_last);assert(book_progress_load(sd,p.file_size,&q));
    assert(book_progress_set_last_path(flash)==ESP_FAIL&&!strcmp(last,sd));last_failure=0; to_legacy(sd); erase_failure=ESP_FAIL;
    assert(book_progress_forget(sd)==ESP_FAIL&&!has_last);assert(book_progress_load(sd,p.file_size,&q));erase_failure=0;
    assert(book_progress_forget(sd)==ESP_OK);assert(book_progress_forget(sd)==ESP_OK);assert(book_progress_load(flash,p.file_size,&q));
    assert(book_progress_save(sd,&p)==ESP_OK);assert(book_progress_set_last_path(sd)==ESP_OK);commit_failure=ESP_FAIL;
    assert(book_progress_forget(sd)==ESP_FAIL);commit_failure=0;assert(book_progress_forget(sd)==ESP_OK);
    assert(!book_progress_load(sd,p.file_size,&q)&&!has_last);
    char long_path[BOOK_STORE_PATH_MAX+1];memset(long_path,'x',sizeof(long_path));long_path[BOOK_STORE_PATH_MAX]=0;
    assert(book_progress_save(long_path,&p)==ESP_ERR_INVALID_ARG);long_path[BOOK_STORE_PATH_MAX-1]=0;
    assert(book_progress_save(long_path,&p)==ESP_OK);assert(book_progress_load(long_path,p.file_size,&q));
    assert(book_progress_set_last_path(long_path)==ESP_OK);char out[BOOK_STORE_PATH_MAX];
    assert(book_progress_last_path(out,sizeof(out))&&!strcmp(out,long_path));assert(!book_progress_last_path(out,2)&&!out[0]);
    assert(book_progress_forget(long_path)==ESP_OK&&!has_last);
    assert(book_progress_set_last_path("")==ESP_OK);assert(!book_progress_last_path(out,sizeof(out))&&!out[0]);
    p.px=17;assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_ARG);p.px=48;p.pct=101;assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_ARG);
    p.pct=83;sequence=UINT32_MAX;int before=commits;
    assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_STATE&&commits==before&&sequence==UINT32_MAX);
    // 枚举只返回仍保存进度的路径：sd 与 long_path 都已遗忘，flash 还在。
    // The walk lists only paths that still hold progress: sd and long_path were forgotten, flash remains.
    listed=0;
    assert(book_progress_list(record_list_visit,NULL)==ESP_OK);
    assert(listed==1&&!strcmp(listed_paths[0],flash));
    assert(book_progress_list(NULL,NULL)==ESP_ERR_INVALID_ARG);
    // 坏记录不会挡住后续有效记录；提前结束与空列表均可安全释放迭代器。
    // Bad records do not block later valid ones; early-stop and empty walks safely release iteration.
    sequence=10;assert(book_progress_save(sd,&p)==ESP_OK);
    to_legacy(sd); latest->data[3]=99;
    listed=0;assert(book_progress_list(record_list_visit,NULL)==ESP_OK);
    assert(listed==1&&!strcmp(listed_paths[0],flash));
    latest->data[3]=2;
    listed=0;assert(book_progress_list(record_list_visit,&listed)==ESP_OK&&listed==1);
    assert(book_progress_forget(sd)==ESP_OK&&book_progress_forget(flash)==ESP_OK);
    listed=0;assert(book_progress_list(record_list_visit,NULL)==ESP_OK&&listed==0);
    for(int px=18;px<=90;++px){
        p.px=(uint8_t)px;p.pct=83;assert(book_progress_save(sd,&p)==ESP_OK);
        assert(book_progress_load(sd,p.file_size,&q)&&q.px==px);
        listed=0;assert(book_progress_list(record_list_visit,NULL)==ESP_OK);
        assert(listed==1&&listed_px==px&&!strcmp(listed_paths[0],sd));
    }
    // 双份窗口：先拒绝写 FAT，再拒绝清旧键；重试不得重复枚举或改变 v1 的排序。
    // Fail the FAT write and then legacy cleanup; retries must not double-list or resequence v1.
    to_legacy(sd); latest->data[3]=1; size_t moved;
    mount_error=ESP_FAIL; assert(book_progress_migrate(2,&moved)!=ESP_OK && latest->len && !moved);
    assert(book_progress_load(sd,p.file_size,&q) && !q.last_open_s); mount_error=0;
    assert(!mkdir(tmp,0700) && book_progress_migrate(2,&moved)!=ESP_OK && latest->len && !rmdir(tmp));
    erase_failure=ESP_FAIL; assert(book_progress_migrate(2,&moved)==ESP_FAIL && latest->len && !moved);
    listed=0; assert(!book_progress_list(record_list_visit,NULL) && listed==1);
    // 校验损坏只回退可识别的旧键；不拿坏 FAT 文件当迁移成功。/ A damaged seal permits legacy reads but never authorizes cleanup.
    char primary[40]; strcpy(primary,tmp); strcpy(strrchr(primary,'.'),".dat");
    FILE *damaged=fopen(primary,"r+b"); assert(damaged && !fseek(damaged,4,SEEK_SET)); int seal=fgetc(damaged);
    assert(seal!=EOF && !fseek(damaged,4,SEEK_SET) && fputc(seal^1,damaged)!=EOF && !fclose(damaged));
    assert(book_progress_load(sd,p.file_size,&q) && !q.last_open_s);
    assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_STATE && latest->len);
    damaged=fopen(primary,"r+b"); assert(damaged && !fseek(damaged,4,SEEK_SET) && fputc(seal,damaged)!=EOF && !fclose(damaged));
    erase_failure=0; assert(!book_progress_migrate(2,&moved) && moved==1 && !latest->len);
    assert(book_progress_load_handle(0,sd,p.file_size,&q) && !q.last_open_s && q.byte_off==p.byte_off);
    // 模拟换名前后的落盘状态：只剩 bak 时可读，不完整 tmp 不能取代它。
    // Simulate the publication gap: a rollback copy stays readable and a torn temp is ignored.
    char dat[40],bak[40]; strcpy(dat,tmp); strcpy(bak,tmp); strcpy(strrchr(dat,'.'),".dat"); strcpy(strrchr(bak,'.'),".bak");
    assert(!rename(dat,bak)); FILE *t=fopen(tmp,"wb"); assert(t && fputc(1,t)!=EOF && !fclose(t));
    assert(book_progress_load(sd,p.file_size,&q) && q.byte_off==p.byte_off);
    assert(!book_progress_save(sd,&p) && access(bak,F_OK)!=0);
    // 普通文件改名也先校验目标；失败保留源与 last，重试不提升最近序号。
    // File-manager moves also verify the destination first; failure preserves the source and last, retries preserve ordering.
    assert(!book_progress_set_last_path(sd)); uint32_t order=sequence;
    assert(!book_progress_save(flash,&p)); assert(book_progress_move_path(sd,flash)==ESP_ERR_INVALID_STATE);
    assert(!strcmp(last,sd) && book_progress_load(sd,p.file_size,&q) && q.last_open_s==order);
    assert(!book_progress_forget(flash) && !book_progress_move_path(sd,flash));
    assert(!strcmp(last,flash) && !book_progress_load(sd,p.file_size,&q) && book_progress_load(flash,p.file_size,&q) && q.last_open_s==order);
    assert(!book_progress_move_path(flash,sd));
    // 数百本只增长桶文件，不产生任何 NVS 进度键。/ Hundreds of books grow buckets without any per-book NVS key.
    for (unsigned i=0;i<600;++i) { char path[60]; snprintf(path,sizeof(path),"/sdcard/books/%u.epub",i); assert(!book_progress_save(path,&p)); }
    listed=0; assert(!book_progress_list(record_list_visit,NULL) && listed==601);
    for (unsigned i=0;i<8;++i) assert(!records[i].len);
    // 恢复切换的两个目录名之间断电：旧目录仍是读源，重试暂存不能清掉它。
    // Power loss between root renames: read the previous generation and retain it while restaging.
    assert(!progress_stage_begin() && !rename("progress","prg-old"));
    assert(book_progress_load(sd,p.file_size,&q));
    assert(!progress_stage_begin() && book_progress_load(sd,p.file_size,&q) && !progress_stage_finish());
    listed=0;assert(!book_progress_list(record_list_visit,NULL) && !listed);
    assert(!rmdir("progress") && !chdir("/") && !rmdir(work));
    puts("book progress: legacy v1/v2, interrupted bucket/root promotion, cleanup retry, 601 FAT rows and zero per-book NVS keys passed");
}
