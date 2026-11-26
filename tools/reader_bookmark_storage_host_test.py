"""书签持久化、旧版迁移与失败隔离。/ Bookmark persistence, migration and failure isolation."""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
unit = r'''
#define main history_roundtrip
#include "book_history_backup_host_test.c"
#undef main
static struct { struct { char path[288]; uint32_t file_size; } book;
                struct { unsigned page,chapter; } position; } s_reader_session;
static char s_reader_notice[128];static int64_t s_reader_notice_until;
static unsigned book_layout_page_start_offset(unsigned page){return page;}
static int64_t esp_timer_get_time(void){return 0;}
static void copy_text(char *out,size_t n,const char *s){snprintf(out,n,"%s",s);}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){
    record_t *r=find_record(h-1,key); if(!r)return ESP_ERR_NVS_NOT_FOUND;
    *r=records[active][--counts[active]];return ESP_OK;
}
#include <time.h>
static bool fail_growth;
static void *try_growth(void *p,size_t n){return fail_growth?NULL:realloc(p,n);}
#define realloc try_growth
'''
for name in ('bookmark_key', 'bookmark_alloc', 'bookmark_from_legacy', 'bookmark_from_v2',
             'bookmark_read_handle', 'bookmark_card_book', 'bookmark_legacy_erase', 'bookmark_load',
             'bookmark_save_current', 'bookmark_remove_current', 'bookmark_save_failed', 'bookmark_toggle'):
    unit += function(name, BOOK) + '\n'
unit += r'''
typedef struct { const char *path; off_t size; mode_t mode; } card_file_t;
static card_file_t card_files[12];
static size_t card_file_count;
static void card_file_add(const char *path, off_t size, mode_t mode) {
    assert(card_file_count < sizeof(card_files) / sizeof(card_files[0]));
    card_files[card_file_count++] = (card_file_t){path, size, mode};
}
static int test_card_stat(const char *path, struct stat *st) {
    if (!strncmp(path, "/sdcard/", 8)) {
        for (size_t i = 0; i < card_file_count; ++i) {
            if (!strcmp(card_files[i].path, path)) {
                memset(st, 0, sizeof(*st));
                st->st_mode = card_files[i].mode;
                st->st_size = card_files[i].size;
                return 0;
            }
        }
        errno = ENOENT;
        return -1;
    }
    return stat(path, st);
}
#define stat(path, st) test_card_stat((path), (st))
'''
unit += function('bookmark_migrate_card_nvs', BOOK) + '\n'
unit += '#undef stat\n'
unit += r'''
int main(void){
    assert(!history_roundtrip()); counts[active]=0;
    const char *path="/sdcard/books/legacy.txt";
    strcpy(s_reader_session.book.path,path); s_reader_session.book.file_size=13;
    legacy_reader_bookmark_t old={.magic=0x52504d31,.file_size=13,.byte_off=31};strcpy(old.path,path);
    char key[11];bookmark_key(path,key);assert(!put(2,key,NVS_TYPE_BLOB,&old,sizeof(old)));
    reader_bookmarks_unbounded_t *m=NULL;
    assert(bookmark_load(&m)==ESP_OK && m->count==1 && m->entries[0].byte_off==31);free(m);
    assert(access(BOOKMARK_ROOT,F_OK)!=0 && find_record(2,key));
    assert(bookmark_toggle() && !find_record(2,key));assert(!bookmark_load(&m)&&m->count==2);free(m);
    slot_path_t file;assert(record_path(BOOKMARK_ROOT,path,"",file));
    FILE *f=fopen(file,"wb");assert(f && fputc(3,f)!=EOF && !fclose(f));assert(!put(2,key,NVS_TYPE_BLOB,&old,sizeof(old)));
    assert(bookmark_load(&m)==ESP_ERR_INVALID_STATE && !m && !bookmark_toggle());
    struct stat st;assert(!stat(file,&st) && st.st_size==1 && find_record(2,key));assert(!unlink(file));
    assert(!rmdir(BOOKMARK_ROOT) && !rmdir(BOOKMARK_PARENT));
    s_reader_session.position.page=31;assert(bookmark_toggle() && !find_record(2,key));
    assert(access(BOOKMARK_PARENT,F_OK)!=0);
    strcpy(s_reader_session.book.path,"/flash/books/internal.txt");
    for(unsigned i=0;i<73;++i){s_reader_session.position.page=i;assert(bookmark_toggle());}
    fail_growth=true;s_reader_session.position.page=80;s_reader_notice[0]=0;
    assert(!bookmark_toggle() && !strcmp(s_reader_notice,"书签保存失败"));
    assert(!bookmark_load(&m) && m->count==73 && m->entries[72].byte_off==72);free(m);

    // 批量迁移旧 TF 卡书签：仅迁移本卡大小匹配的记录，保留异卡、改大小、损坏侧车、内置书与写卡失败项。
    // Batch-migrate legacy TF bookmarks only for matching books on the mounted card; preserve other cards,
    // resized books, corrupt sidecars, internal books, and failed writes.
    counts[active]=0; card_file_count=0; fail_growth=false;
    const char *p_v1="/sdcard/books/v1.txt", *p_v2="/sdcard/books/v2.txt", *p_empty="/sdcard/books/empty.txt";
    const char *p_already="/sdcard/books/already.txt", *p_resized="/sdcard/books/resized.txt";
    const char *p_other="/sdcard/books/other_card.txt", *p_corrupt="/sdcard/books/corrupt.txt";
    const char *p_flash="/flash/books/stay.txt";
    card_file_add(p_v1, 101, S_IFREG); card_file_add(p_v2, 202, S_IFREG);
    card_file_add(p_empty, 303, S_IFREG); card_file_add(p_already, 404, S_IFREG);
    card_file_add(p_resized, 999, S_IFREG); card_file_add(p_corrupt, 606, S_IFREG);
    legacy_reader_bookmark_t b_v1={.magic=0x52504d31,.file_size=101,.chapter=1,.byte_off=44}; strcpy(b_v1.path,p_v1);
    reader_bookmarks_t b_v2={.magic=0x52504d32,.file_size=202,.count=2}; strcpy(b_v2.path,p_v2);
    b_v2.entries[0]=(reader_bookmark_entry_t){.chapter=0,.byte_off=10,.saved_s=1};
    b_v2.entries[1]=(reader_bookmark_entry_t){.chapter=2,.byte_off=88,.saved_s=2};
    reader_bookmarks_t b_empty={.magic=0x52504d32,.file_size=303,.count=0}; strcpy(b_empty.path,p_empty);
    reader_bookmarks_t b_already={.magic=0x52504d32,.file_size=404,.count=1}; strcpy(b_already.path,p_already);
    b_already.entries[0]=(reader_bookmark_entry_t){.chapter=0,.byte_off=7,.saved_s=1};
    legacy_reader_bookmark_t b_resized={.magic=0x52504d31,.file_size=505,.byte_off=12}; strcpy(b_resized.path,p_resized);
    legacy_reader_bookmark_t b_other={.magic=0x52504d31,.file_size=555,.byte_off=15}; strcpy(b_other.path,p_other);
    legacy_reader_bookmark_t b_corrupt={.magic=0x52504d31,.file_size=606,.byte_off=21}; strcpy(b_corrupt.path,p_corrupt);
    legacy_reader_bookmark_t b_flash={.magic=0x52504d31,.file_size=707,.byte_off=99}; strcpy(b_flash.path,p_flash);
    char k_v1[11],k_v2[11],k_empty[11],k_already[11],k_resized[11],k_other[11],k_corrupt[11],k_flash[11];
    bookmark_key(p_v1,k_v1); bookmark_key(p_v2,k_v2); bookmark_key(p_empty,k_empty);
    bookmark_key(p_already,k_already); bookmark_key(p_resized,k_resized);
    bookmark_key(p_other,k_other); bookmark_key(p_corrupt,k_corrupt); bookmark_key(p_flash,k_flash);
    assert(!put(2,k_v1,NVS_TYPE_BLOB,&b_v1,sizeof(b_v1)));
    assert(!put(2,k_v2,NVS_TYPE_BLOB,&b_v2,sizeof(b_v2)));
    assert(!put(2,k_empty,NVS_TYPE_BLOB,&b_empty,sizeof(b_empty)));
    assert(!put(2,k_already,NVS_TYPE_BLOB,&b_already,sizeof(b_already)));
    assert(!put(2,k_resized,NVS_TYPE_BLOB,&b_resized,sizeof(b_resized)));
    assert(!put(2,k_other,NVS_TYPE_BLOB,&b_other,sizeof(b_other)));
    assert(!put(2,k_corrupt,NVS_TYPE_BLOB,&b_corrupt,sizeof(b_corrupt)));
    assert(!put(2,k_flash,NVS_TYPE_BLOB,&b_flash,sizeof(b_flash)));
    reader_bookmarks_unbounded_t *seed_already=bookmark_alloc(1); assert(seed_already);
    seed_already->file_size=404; strcpy(seed_already->path,p_already);
    seed_already->entries[0]=(reader_bookmark_entry_t){.chapter=3,.byte_off=999,.saved_s=9};
    assert(book_bookmark_store_save(seed_already)==ESP_OK); free(seed_already);
    slot_path_t corrupt_slot, empty_slot;
    assert(record_path(BOOKMARK_ROOT,p_corrupt,"",corrupt_slot));
    assert(record_path(BOOKMARK_ROOT,p_empty,"",empty_slot));
    f=fopen(corrupt_slot,"wb"); assert(f && fputc(7,f)!=EOF && !fclose(f));
    fail_promotion=".tmp";
    assert(bookmark_migrate_card_nvs()==2);
    assert(!find_record(2,k_empty) && access(empty_slot,F_OK)!=0);
    assert(!find_record(2,k_already));
    assert(find_record(2,k_v1) && find_record(2,k_v2));
    fail_promotion=NULL;
    assert(bookmark_migrate_card_nvs()==2);
    assert(!find_record(2,k_v1) && !find_record(2,k_v2));
    assert(find_record(2,k_resized) && find_record(2,k_other) && find_record(2,k_corrupt) && find_record(2,k_flash));
    assert(!stat(corrupt_slot,&st) && st.st_size==1);
    strcpy(s_reader_session.book.path,p_v1); s_reader_session.book.file_size=101;
    assert(bookmark_load(&m)==ESP_OK && m->count==1 && m->entries[0].chapter==1 && m->entries[0].byte_off==44); free(m);
    strcpy(s_reader_session.book.path,p_v2); s_reader_session.book.file_size=202;
    assert(bookmark_load(&m)==ESP_OK && m->count==2 && m->entries[1].byte_off==88); free(m);
    strcpy(s_reader_session.book.path,p_already); s_reader_session.book.file_size=404;
    assert(bookmark_load(&m)==ESP_OK && m->count==1 && m->entries[0].byte_off==999); free(m);
    assert(bookmark_migrate_card_nvs()==0);
    puts("reader: passive legacy reads, migration on edit, proactive NVS migration, last deletion, unreadable sidecar and 73 internal bookmarks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='mogo-bookmarks-') as tmp:
    folder = Path(tmp)
    src, binary = folder / 'test.c', folder / 'test'
    src.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I'+str(ROOT/'tools/settings_backup_stubs'), '-I'+str(ROOT/'components/restore_nvs/include'), '-I'+str(ROOT/'tools'),
                    str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], cwd=folder, check=True)
