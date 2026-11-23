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
    puts("reader: passive legacy reads, migration on edit, last deletion, unreadable sidecar and 73 internal bookmarks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='mogo-bookmarks-') as tmp:
    folder = Path(tmp)
    src, binary = folder / 'test.c', folder / 'test'
    src.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I'+str(ROOT/'tools/settings_backup_stubs'), '-I'+str(ROOT/'tools'),
                    str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], cwd=folder, check=True)
