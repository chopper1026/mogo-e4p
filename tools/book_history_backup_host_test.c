/* SPDX-License-Identifier: Apache-2.0 */
/* Round-trip the real streaming history format through an in-memory NVS. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include "nvs.h"
#define BOOKMARK_PARENT "card"
static const char *fail_promotion;
static int card_rename(const char *from, const char *to) {
    if (fail_promotion && strstr(from, fail_promotion)) { errno=EIO; return -1; }
    struct stat st;
    if (!stat(to,&st)) { errno=EEXIST; return -1; }
    return rename(from,to);
}
#define rename card_rename
#include "../main/book/book_bookmark_store.c"
#undef rename
static size_t fail_history_allocation;
static void *history_alloc(size_t n){return n==fail_history_allocation?NULL:malloc(n);}
#define malloc history_alloc
#include "../main/book/book_history_backup.c"
#undef malloc

typedef struct { int ns; char key[16]; nvs_type_t type; size_t len; uint8_t data[8192]; } record_t;
static record_t records[2][32];
static int counts[2], active;
static unsigned progress_changes;
void book_progress_notify_changed(void) { ++progress_changes; }
struct nvs_iterator_opaque { int ns, index; };

static int namespace_index(const char *name) {
    for (unsigned i = 0; i < sizeof(namespaces) / sizeof(namespaces[0]); ++i)
        if (!strcmp(name, namespaces[i])) return (int)i;
    return -1;
}
static record_t *find_record(int ns, const char *key) {
    for (int i = 0; i < counts[active]; ++i)
        if (records[active][i].ns == ns && !strcmp(records[active][i].key, key)) return &records[active][i];
    return NULL;
}
static esp_err_t put(int ns, const char *key, nvs_type_t type, const void *data, size_t len) {
    if (ns < 0 || len > sizeof(records[0][0].data)) return ESP_ERR_INVALID_ARG;
    record_t *r = find_record(ns, key);
    if (!r) { assert(counts[active] < 32); r = &records[active][counts[active]++]; }
    memset(r, 0, sizeof(*r)); r->ns = ns; strcpy(r->key, key); r->type = type; r->len = len;
    memcpy(r->data, data, len); return ESP_OK;
}
static esp_err_t get(int ns, const char *key, nvs_type_t type, void *data, size_t *len) {
    record_t *r = find_record(ns, key);
    if (!r || r->type != type) return ESP_ERR_NVS_NOT_FOUND;
    if (data && *len < r->len) return ESP_ERR_INVALID_SIZE;
    if (data) memcpy(data, r->data, r->len);
    *len = r->len; return ESP_OK;
}
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    int id = namespace_index(ns);
    if (id < 0) return ESP_ERR_NVS_NOT_FOUND;
    if (mode == NVS_READONLY) {
        bool found = false;
        for (int i = 0; i < counts[active]; ++i) if (records[active][i].ns == id) found = true;
        if (!found) return ESP_ERR_NVS_NOT_FOUND;
    }
    *h = id + 1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle_t h) {
    int ns = h - 1;
    for (int i = 0; i < counts[active];)
        if (records[active][i].ns == ns) {
            memmove(&records[active][i], &records[active][i + 1],
                    (size_t)(--counts[active] - i) * sizeof(records[active][i]));
        } else ++i;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *v) { size_t n=1; return get(h-1,key,NVS_TYPE_U8,v,&n); }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v) { return put(h-1,key,NVS_TYPE_U8,&v,1); }
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *v) { size_t n=4; return get(h-1,key,NVS_TYPE_U32,v,&n); }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v) { return put(h-1,key,NVS_TYPE_U32,&v,4); }
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *v, size_t *n) { return get(h-1,key,NVS_TYPE_STR,v,n); }
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *v) { return put(h-1,key,NVS_TYPE_STR,v,strlen(v)+1); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *v, size_t *n) { return get(h-1,key,NVS_TYPE_BLOB,v,n); }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t n) { return put(h-1,key,NVS_TYPE_BLOB,v,n); }
esp_err_t nvs_entry_find_in_handle(nvs_handle_t h, nvs_type_t type, nvs_iterator_t *it) {
    assert(type == NVS_TYPE_ANY);
    for (int i = 0; i < counts[active]; ++i) if (records[active][i].ns == h - 1) {
        *it = malloc(sizeof(**it)); assert(*it); **it = (struct nvs_iterator_opaque){h-1,i}; return ESP_OK;
    }
    *it = NULL; return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_next(nvs_iterator_t *it) {
    for (int i = (*it)->index + 1; i < counts[active]; ++i)
        if (records[active][i].ns == (*it)->ns) { (*it)->index = i; return ESP_OK; }
    free(*it); *it = NULL; return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *info) {
    record_t *r = &records[active][it->index];
    memset(info,0,sizeof(*info)); strcpy(info->key,r->key); info->type=r->type; return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it) { free(it); }

int main(void) {
    char cwd[1024], tmp[]="/tmp/mogo-marks-XXXXXX";
    assert(getcwd(cwd,sizeof(cwd)) && mkdtemp(tmp) && !chdir(tmp));
    const char *path = "/sdcard/books/one.epub";
    uint32_t seq = 9;
    assert(put(0,"seq",NVS_TYPE_U32,&seq,4)==ESP_OK);
    assert(put(0,"last",NVS_TYPE_STR,path,strlen(path)+1)==ESP_OK);
    uint8_t progress[64] = {'R','P','B',2};
    size_t path_len = strlen(path)+1;
    progress[14]=48; progress[15]=42; progress[22]=(uint8_t)path_len;
    memcpy(progress+24,path,path_len);
    char key[11]; uint32_t path_hash = hash_bytes(UINT32_C(2166136261),path,strlen(path));
    snprintf(key,sizeof(key),"b_%08x",path_hash);
    assert(put(0,key,NVS_TYPE_BLOB,progress,24+path_len)==ESP_OK);
    const char *other = "/sdcard/books/two.txt";
    size_t other_len = strlen(other)+1;
    progress[10]=19; progress[15]=73; progress[22]=(uint8_t)other_len;
    memcpy(progress+24,other,other_len);
    snprintf(key,sizeof(key),"b_%08x",hash_bytes(UINT32_C(2166136261),other,strlen(other)));
    assert(put(0,key,NVS_TYPE_BLOB,progress,24+other_len)==ESP_OK);
    uint8_t stats[32]={0}; uint32_t stats_magic=UINT32_C(0x52505431); memcpy(stats,&stats_magic,4); stats[8]=77;
    assert(put(1,"stats",NVS_TYPE_BLOB,stats,sizeof(stats))==ESP_OK);
    uint8_t heat[244]={0}; uint32_t heat_magic=UINT32_C(0x52504831); memcpy(heat,&heat_magic,4); heat[4]=7;
    assert(put(1,"heatmap",NVS_TYPE_BLOB,heat,sizeof(heat))==ESP_OK);
    reader_bookmarks_t marks={.magic=0x52504d32,.file_size=17,.count=2}; strcpy(marks.path,path);
    snprintf(key,sizeof(key),"m_%08x",path_hash);
    assert(put(2,key,NVS_TYPE_BLOB,&marks,sizeof(marks))==ESP_OK);
    uint8_t favorite=1; snprintf(key,sizeof(key),"f_%08x",path_hash);
    assert(put(3,key,NVS_TYPE_U8,&favorite,1)==ESP_OK);
    uint8_t title[64]={0}; memcpy(title,path,path_len); strcpy((char *)title+path_len,"我的书");
    snprintf(key,sizeof(key),"t_%08x",path_hash);
    assert(put(4,key,NVS_TYPE_BLOB,title,path_len+strlen("我的书")+1)==ESP_OK);
    assert(counts[0]==9);

    FILE *f=tmpfile(); assert(f);
    assert(book_history_backup_write(f,NULL,0)==ESP_OK);
    rewind(f); assert(book_history_backup_validate(f));
    active=1; rewind(f);
    const char *stale = "/sdcard/books/stale.epub";
    assert(put(0,"last",NVS_TYPE_STR,stale,strlen(stale)+1)==ESP_OK);
    assert(put(3,"f_deadbeef",NVS_TYPE_U8,&favorite,1)==ESP_OK);
    // 旧备份不含移出状态，恢复时清除机器上后加的隐藏记录。/ Legacy restore clears later removals.
    snprintf(key,sizeof(key),"h_%08x",path_hash);
    assert(put(5,key,NVS_TYPE_STR,path,path_len)==ESP_OK);
    unsigned before_restore = progress_changes;
    assert(book_history_backup_restore(f)==ESP_OK);
    assert(progress_changes > before_restore);
    assert(counts[1]==counts[0]);
    assert(!find_record(3,"f_deadbeef"));
    assert(!find_record(5,key));
    for (int i=0;i<counts[0];++i) {
        record_t *a=&records[0][i], *b=find_record(a->ns,a->key);
        assert(b && b->type==a->type && b->len==a->len && !memcmp(a->data,b->data,a->len));
    }
    fclose(f);
    // 新备份完整恢复隐藏记录，并继续保留进度和收藏。/ New backups restore removal and reading data.
    active=0;
    assert(put(5,key,NVS_TYPE_STR,path,path_len)==ESP_OK);
    f=tmpfile(); assert(f);
    assert(book_history_backup_write(f,NULL,0)==ESP_OK);
    rewind(f); assert(book_history_backup_validate(f));
    active=1; rewind(f);
    assert(book_history_backup_restore(f)==ESP_OK && counts[1]==counts[0]);
    for (int i=0;i<counts[0];++i) {
        record_t *a=&records[0][i], *b=find_record(a->ns,a->key);
        assert(b && b->type==a->type && b->len==a->len && !memcmp(a->data,b->data,a->len));
    }
    assert(!valid_record(5,NVS_TYPE_STR,key,(const uint8_t*)other,other_len));
    assert(!valid_record(5,NVS_TYPE_STR,key,(const uint8_t*)path,path_len-1));
    assert(!valid_record(5,NVS_TYPE_BLOB,key,(const uint8_t*)path,path_len));
    assert(fseek(f,-1,SEEK_END)==0); assert(fputc(0xff,f)!=EOF); fflush(f);
    rewind(f); assert(!book_history_backup_validate(f));
    fclose(f);
    // 模拟 FatFs 不覆盖换名，并中断发布；旧副本仍能读取、导出和重试。/ Interrupt a FatFs-style promotion; the rollback copy remains readable, exportable and retryable.
    size_t bytes; assert(reader_bookmarks_unbounded_size(6000,&bytes) && bytes>UINT16_MAX);
    reader_bookmarks_unbounded_t *card=calloc(1,bytes), *got=NULL; assert(card);
    card->magic=0x52504d33; card->file_size=17; card->count=6000; strcpy(card->path,path);
    card->entries[5999].byte_off=77; assert(!book_bookmark_store_save(card));
    fail_promotion=".tmp"; card->entries[5999].byte_off=88; assert(book_bookmark_store_save(card)!=ESP_OK);
    fail_promotion=NULL; assert(!book_bookmark_store_load(path,17,&got) && got->entries[5999].byte_off==77); free(got);
    const char *builtin="/flash/books/test.txt";
    size_t internal_bytes; assert(reader_bookmarks_unbounded_size(450,&internal_bytes));
    reader_bookmarks_unbounded_t *internal=calloc(1,internal_bytes);assert(internal);
    internal->magic=card->magic; internal->count=450; strcpy(internal->path,builtin);
    snprintf(key,sizeof(key),"m_%08x",hash_bytes(UINT32_C(2166136261),builtin,strlen(builtin)));
    assert(!put(2,key,NVS_TYPE_BLOB,internal,internal_bytes));
    f=tmpfile();assert(f && !book_history_backup_write(f,NULL,0)); rewind(f);assert(book_history_backup_validate(f));
    assert(!book_bookmark_store_save(card));
    slot_path_t stale_bak; assert(record_path(BOOKMARK_ROOT,path,".bak",stale_bak));
    FILE *broken=fopen(stale_bak,"wb"); assert(broken && fputc(1,broken)!=EOF && !fclose(broken));
    assert(!book_bookmark_store_save(card) && access(stale_bak,F_OK)!=0);
    assert(!book_bookmark_store_check_move(path,other,17));
    assert(!book_bookmark_store_relocate(path,other,17));
    assert(book_bookmark_store_check_move(path,other,17)==ESP_ERR_INVALID_STATE);
    assert(!book_bookmark_store_save(card) && !book_bookmark_store_relocate(path,other,17));
    assert(book_bookmark_store_load(path,17,&got)==ESP_ERR_NOT_FOUND);
    active=0; rewind(f); fail_promotion=".restore";
    uint32_t before=521; assert(!put(0,"seq",NVS_TYPE_U32,&before,sizeof(before)));
    assert(book_history_backup_restore(f)!=ESP_OK); fail_promotion=NULL;
    assert(!memcmp(find_record(0,"seq")->data,&before,sizeof(before)) && !find_record(2,key));
    assert(!book_bookmark_store_load(other,17,&got) && got->entries[5999].byte_off==88); free(got);
    rewind(f); assert(!book_history_backup_restore(f));
    rewind(f); fail_history_allocation=bytes; assert(read_stream(f,RESTORE_NVS)==ESP_OK); fail_history_allocation=0; fclose(f);
    assert(find_record(2,key) && find_record(2,key)->len==internal_bytes);
    assert(!book_bookmark_store_load(path,17,&got) && got->count==6000 && got->entries[5999].byte_off==77); free(got);
    assert(book_bookmark_store_load(other,17,&got)==ESP_ERR_NOT_FOUND);
    // 真正的 v1 字节流（含旧 NVS 书签）；恢复必须替换卡上较新的记录。/ Actual v1 bytes with NVS bookmarks must supersede newer card records.
    f=tmpfile();assert(f); uint8_t head[20]={2,NVS_TYPE_BLOB},end[20]={0xff},seal[6]={1};
    snprintf((char*)head+2,16,"m_%08x",path_hash);head[18]=(uint8_t)sizeof(marks);head[19]=sizeof(marks)>>8;
    uint32_t h=hash_bytes(hash_bytes(UINT32_C(2166136261),head,20),&marks,sizeof(marks)); put32(seal+2,h);
    assert(fwrite("RPHIST1",1,8,f)==8 && fwrite(head,1,20,f)==20 && fwrite(&marks,1,sizeof(marks),f)==sizeof(marks) &&
        fwrite(end,1,20,f)==20 && fwrite(seal,1,6,f)==6);rewind(f);
    assert(!book_history_backup_restore(f) && find_record(2,(char*)head+2)); fclose(f);
    assert(book_bookmark_store_load(path,17,&got)==ESP_ERR_NOT_FOUND);
    slot_path_t file;assert(record_path(BOOKMARK_ROOT,path,"",file)); f=fopen(file,"wb");assert(f && fputc(1,f)!=EOF && !fclose(f));
    assert(book_bookmark_store_load(path,17,&got)==ESP_ERR_INVALID_STATE && !got);
    assert(book_bookmark_store_save(card)!=ESP_OK && book_bookmark_store_remove(path)!=ESP_OK);
    char bad[25]; f=tmpfile(); assert(f && book_history_backup_write(f,bad,sizeof(bad))!=ESP_OK && !strcmp(bad,strrchr(file,'/')+1)); fclose(f);
    assert(!unlink(file) && !rmdir(BOOKMARK_ROOT) && !rmdir(BOOKMARK_PARENT) && !chdir(cwd) && !rmdir(tmp));
    free(card);free(internal);
    puts("book history: v1/v2 restore, 6000 card / 450 internal bookmarks, failed promotion and retry, relocation and short-file rejection passed");
    return 0;
}
