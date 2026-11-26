/* 中文：逻辑分类真实存储与故障注入回归，不连接设备。/ English: Real logical-category storage and fault-injection regressions, without hardware. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_collection.h"

static int allocation_fail = -1, write_fail, sync_fail, unlink_fail;
static int read_fail = -1;
static FILE *read_error_stream;
void *heap_caps_calloc(size_t n, size_t bytes, int caps) {
    assert(caps == 3);
    if (allocation_fail == 0) return NULL;
    if (allocation_fail > 0) --allocation_fail;
    return calloc(n, bytes);
}
void *heap_caps_realloc(void *p, size_t bytes, int caps) {
    assert(caps == 3);
    if (allocation_fail == 0) return NULL;
    if (allocation_fail > 0) --allocation_fail;
    return realloc(p, bytes);
}
static size_t test_write(const void *p, size_t n, size_t count, FILE *f) {
    if (write_fail) { --write_fail; return fwrite(p, n, count / 2, f); }
    return fwrite(p, n, count, f);
}
static int test_sync(int fd) { if (sync_fail) { --sync_fail; errno = EIO; return -1; } return fsync(fd); }
static int test_unlink(const char *p) { if (unlink_fail) { --unlink_fail; errno = EIO; return -1; } return unlink(p); }
static bool fail_read(FILE *f) {
    if (read_fail < 0) return false;
    if (read_fail-- > 0) return false;
    read_error_stream = f; errno = EIO; return true;
}
static size_t test_read(void *p, size_t n, size_t count, FILE *f) {
    return fail_read(f) ? 0 : fread(p, n, count, f);
}
static int test_getc(FILE *f) { return fail_read(f) ? EOF : fgetc(f); }
static int test_error(FILE *f) { return f == read_error_stream || ferror(f); }
#define fwrite test_write
#define fsync test_sync
#define unlink test_unlink
#define fread test_read
#define fgetc test_getc
#define ferror test_error
static const char *backup_root;
#define BOOK_COLLECTION_BACKUP_ROOT backup_root
#include "../main/book/book_collection.c"
#undef fwrite
#undef fsync
#undef unlink
#undef fread
#undef fgetc
#undef ferror

static void corrupt(const char *prefix, int slot) {
    char path[320]; snprintf(path, sizeof(path), "%s.%d", prefix, slot);
    FILE *f = fopen(path, "wb"); assert(f); assert(fwrite("broken", 1, 6, f) == 6); assert(!fclose(f));
}
static void book(book_shelf_entry_t *b, const char *root, const char *leaf) {
    memset(b, 0, sizeof(*b)); snprintf(b->path, sizeof(b->path), "%s/%s", root, leaf); b->search_match = true;
}
static void test_collection_backup(const char *directory) {
    backup_root = directory;
    const char *card = "01-1234-12345678-90ab", *other = "02-1234-12345678-90ab";
    book_collection_t built = {0}, current = {0}, different = {0};
    uint32_t id; unsigned changed;
    book_shelf_entry_t entry;
    book(&entry, "/sdcard/books", "a.epub"); entry.selected = true;
    assert(collection_backup_open(&built, "builtin", false) == ESP_OK);
    assert(collection_backup_open(&current, card, false) == ESP_OK);
    assert(collection_backup_open(&different, other, false) == ESP_OK);
    assert(book_collection_create(&built, "Internal", NULL, 0, false, &id, &changed) == ESP_OK);
    assert(book_collection_create(&current, "Card", &entry, 1, true, &id, &changed) == ESP_OK);
    assert(book_collection_create(&different, "Other card", NULL, 0, false, &id, &changed) == ESP_OK);
    FILE *file = tmpfile(); assert(file && book_collection_backup_write(file, card) == ESP_OK);
    assert(book_collection_rename(&current, id, "Changed") == ESP_OK);
    uint32_t rev = book_collection_backup_revision();
    rewind(file);
    assert(book_collection_backup_read(file, card, false,NULL) == ESP_OK && book_collection_backup_revision() == rev);
    rewind(file);
    assert(book_collection_backup_read(file, other, true,NULL) == ESP_OK && book_collection_backup_revision() == rev + 1);
    assert(collection_backup_open(&different, other, false) == ESP_OK);
    assert(!strcmp(book_collection_at(&different, 0)->name, "Other card"));
    rewind(file);
    assert(book_collection_backup_read(file, card, true,NULL) == ESP_OK && book_collection_backup_revision() == rev + 3);
    assert(collection_backup_open(&current, card, false) == ESP_OK);
    assert(!strcmp(book_collection_at(&current, 0)->name, "Card"));
    assert(book_collection_assignment(&current, entry.path) == id);
    rewind(file); assert(fputc('x', file) == 'x'); rewind(file);
    assert(book_collection_backup_read(file, card, true,NULL) == ESP_ERR_INVALID_RESPONSE &&
           book_collection_backup_revision() == rev + 3);
    assert(fclose(file) == 0);
    assert(collection_backup_open(&current, card, true) == ESP_OK);
    const char *names[]={"文学","历史"};
    assert(!book_collection_folders(&current,names,2,NULL,NULL));
    id=book_collection_legacy_id(&current,names[1]);assert(!book_collection_order(&current,id,-1));
    file=tmpfile();assert(file && !book_folder_backup_write(file,card));
    assert(!book_collection_order(&current,id,1));rev=book_collection_backup_revision();
    bool needs = false;
    rewind(file);assert(!book_folder_backup_read(file,other,false,&needs) && !needs);
    rewind(file);assert(!book_folder_backup_read(file,card,false,&needs) && needs);
    rewind(file);assert(!book_folder_backup_read(file,other,true,NULL) && book_collection_backup_revision()==rev);
    rewind(file);assert(!book_folder_backup_read(file,card,false,NULL) && book_collection_backup_revision()==rev);
    rewind(file);assert(!book_folder_backup_read(file,card,true,NULL) && book_collection_backup_revision()==rev+1);
    assert(!collection_backup_open(&current,card,true) && book_collection_at(&current,0)->id==id);
    assert(!book_collection_record(&current,0,&id));assert(!fclose(file));
    for(int i=0;i<2;++i){char path[320];snprintf(path,sizeof(path),"%s/.mogo-folders-%s.%d",directory,card,i);assert(!unlink(path));}
    const char *libraries[] = {"builtin", card, other};
    for (size_t i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) {
        char path[320]; snprintf(path, sizeof(path), "%s/.mogo-shelf-%s.%d", directory, libraries[i], j);
        if (unlink(path)) assert(errno == ENOENT);
    }
    book_collection_close(&built); book_collection_close(&current); book_collection_close(&different);
    puts("PASS: collection backup round trip, library identity isolation and corrupt header rejection");
}
int main(void) {
    char directory[] = "/tmp/mogo-collections-XXXXXX"; assert(mkdtemp(directory));
    test_collection_backup(directory);
    char root[288], prefix[288], second[288], folder[288];
    snprintf(root, sizeof(root), "%s/books", directory); assert(!mkdir(root, 0700));
    snprintf(prefix, sizeof(prefix), "%s/library-card-a", directory);
    snprintf(second, sizeof(second), "%s/library-card-b", directory);
    snprintf(folder, sizeof(folder), "%s/文学", root); assert(!mkdir(folder, 0700));
    const char *folders[] = {folder};
    book_shelf_entry_t books[4]; book(&books[0], root, "文学/same.txt"); book(&books[1], root, "same.txt");
    book(&books[2], "/flash", "builtin.epub"); books[2].is_flash = true;
    FILE *original = fopen(books[0].path, "wb"); assert(original); fputs("original book\n", original); assert(!fclose(original));
    book_collection_t s = {0}; assert(book_collection_open(&s, prefix) == ESP_OK);
    assert(!book_collection_migrated(&s));
    assert(book_collection_sync(&s, root, folders, 1, books, 3, false) == ESP_OK && !book_collection_migrated(&s));
    assert(book_collection_sync(&s, root, folders, 1, books, 3, true) == ESP_OK);
    assert(book_collection_count(&s) == 1 && books[0].category_id && !books[1].category_id && !books[2].category_id);
    uint32_t literature = books[0].category_id, id = 0; unsigned changed = 99;
    uint32_t generation = s.generation;
    assert(book_collection_sync(&s, root, folders, 1, books, 3, true) == ESP_OK && s.generation == generation);
    books[0].selected = books[1].selected = books[2].selected = true;
    assert(book_collection_create(&s, " 科幻 ", books, 3, true, &id, &changed) == ESP_OK && changed == 3);
    assert(strcmp(book_collection_find(&s, id)->name, "科幻") == 0);
    assert(book_collection_assignment(&s, books[0].path) == id && book_collection_assignment(&s, books[1].path) == id);
    assert(book_collection_assignment(&s, books[2].path) == id);
    generation = s.generation;
    assert(book_collection_assign(&s, books, 3, true, id, &changed) == ESP_OK && !changed && s.generation == generation);
    assert(book_collection_rename(&s, id, "科幻小说") == ESP_OK);
    assert(book_collection_order(&s, id, -1) == ESP_OK && book_collection_at(&s, 0)->id == id);
    assert(book_collection_remove(&s, id) == ESP_OK && !book_collection_assignment(&s, books[0].path));
    uint32_t removed_id = id;
    book_collection_close(&s); assert(book_collection_open(&s, prefix) == ESP_OK);
    assert(!book_collection_find(&s, removed_id) && book_collection_count(&s) == 1);
    for (int i = 0; i < 3; ++i) assert(!book_collection_assignment(&s, books[i].path));
    assert(book_collection_create(&s, "科幻小说", books, 3, true, &id, &changed) == ESP_OK && changed == 3 && id > removed_id);
    assert(book_collection_order(&s, id, -1) == ESP_OK && book_collection_at(&s, 0)->id == id);
    book_collection_close(&s); assert(book_collection_open(&s, prefix) == ESP_OK);
    assert(book_collection_assignment(&s, books[0].path) == id);
    // 任一副本短暂读错都阻断写入，不回退旧数据再覆盖新副本。/ Transient errors on either copy block writes instead of rolling back and overwriting newer data.
    generation = s.generation;
    for (int fail = 0; fail < 6; ++fail) {
        read_fail = fail;
        assert(book_collection_open(&s, prefix) == ESP_FAIL && s.blocked && !s.ready);
        read_fail = -1; read_error_stream = NULL;
        assert(book_collection_open(&s, prefix) == ESP_OK && s.generation == generation);
        assert(book_collection_assignment(&s, books[0].path) == id);
    }
    assert(book_collection_sync(&s, root, folders, 1, books, 3, true) == ESP_OK && books[0].category_id == id);
    assert(book_collection_remove(&s, literature) == ESP_OK);
    assert(book_collection_sync(&s, root, folders, 1, books, 3, true) == ESP_OK && book_collection_count(&s) == 1);
    book(&books[3], root, "文学/new.epub");
    assert(book_collection_sync(&s, root, folders, 1, books, 4, true) == ESP_OK && books[3].new_book && !books[3].category_id);
    books[3].selected = true;
    generation = s.generation; write_fail = 1;
    assert(book_collection_assign(&s, books, 4, true, 0, &changed) != ESP_OK && !changed && s.generation == generation);
    assert(books[0].selected && book_collection_assignment(&s, books[0].path) == id);
    book_collection_close(&s); assert(book_collection_open(&s, prefix) == ESP_OK && book_collection_assignment(&s, books[0].path) == id);
    sync_fail = 1;
    assert(book_collection_assign(&s, books, 4, true, 0, &changed) != ESP_OK && book_collection_assignment(&s, books[0].path) == id);
    allocation_fail = 0;
    assert(book_collection_rename(&s, id, "另一名称") == ESP_ERR_NO_MEM);
    allocation_fail = -1;
    assert(!strcmp(book_collection_find(&s, id)->name, "科幻小说"));
    for (int fail = 0; fail < 7; ++fail) {
        generation = s.generation;
        allocation_fail = fail;
        esp_err_t result = book_collection_assign(&s, books, 4, true, 0, &changed);
        allocation_fail = -1;
        if (result == ESP_OK) {
            assert(changed == 3 && s.generation == generation + 1);
            for (int i = 0; i < 4; ++i) assert(!book_collection_assignment(&s, books[i].path));
            assert(book_collection_assign(&s, books, 3, true, id, &changed) == ESP_OK && changed == 3);
        } else assert(!changed && s.generation == generation);
        assert(book_collection_assignment(&s, books[0].path) == id && books[0].selected);
    }
    FILE *backup = tmpfile(); assert(backup); assert(book_collection_export(&s, backup) == ESP_OK); rewind(backup);
    book_collection_t other = {0}; assert(book_collection_open(&other, second) == ESP_OK && !book_collection_count(&other));
    assert(book_collection_import(&other, backup) == ESP_OK && book_collection_assignment(&other, books[1].path) == id);
    assert(!fclose(backup)); book_collection_close(&other);
    assert(book_collection_open(&other, second) == ESP_OK && book_collection_assignment(&other, books[1].path) == id);
    assert(book_collection_assign(&other, books, 4, true, 0, &changed) == ESP_OK);
    assert(book_collection_assignment(&s, books[1].path) == id); book_collection_close(&other);
    assert(book_collection_rename(&s, id, "新名称") == ESP_OK);
    int latest = s.slot; book_collection_close(&s); corrupt(prefix, latest);
    assert(book_collection_open(&s, prefix) == ESP_OK && s.recovered && !strcmp(book_collection_find(&s, id)->name, "科幻小说"));
    book_collection_close(&s); corrupt(prefix, 1-latest);
    assert(book_collection_open(&s, prefix) != ESP_OK && s.blocked && !s.ready);
    assert(book_collection_create(&s, "不覆盖坏索引", NULL, 0, false, &id, &changed) != ESP_OK);
    book_collection_close(&s);
    char clean[97];
    assert(book_collection_name("  中文 Name  ", clean) && !strcmp(clean, "中文 Name"));
    assert(!book_collection_name("全部书籍", clean) && !book_collection_name("未分类", clean));
    assert(!book_collection_name("a/b", clean) && !book_collection_name("a\n", clean));
    assert(!book_collection_name("\xc0\x80", clean) && !book_collection_name("\xed\xa0\x80", clean));
    assert(book_collection_name("一二三四五六七八九十一二三四五六七八九十一二三四", clean));
    assert(!book_collection_name("一二三四五六七八九十一二三四五六七八九十一二三四五", clean));
    // 旧目录超长、保留名或修剪后重名仍独立迁移，不因截断合并。/ Long/reserved/trim-colliding legacy folders migrate independently without truncation merges.
    char migration_prefix[288], legacy_paths[6][288];
    snprintf(migration_prefix, sizeof(migration_prefix), "%s/legacy-alias", directory);
    const char *names[] = {"一二三四五六七八九十一二三四五六七八九十一二三四五", "一二三四五六七八九十一二三四五六七八九十一二三四六",
                           "未分类", "一二三四五六七八九十一二三四五六七八 1", "文学", " 文学 "};
    const char *legacy_folders[6]; book_shelf_entry_t legacy_books[6];
    for (int i = 0; i < 6; ++i) {
        snprintf(legacy_paths[i], sizeof(legacy_paths[i]), "%s/%s", root, names[i]);
        legacy_folders[i] = legacy_paths[i]; char leaf[288];
        snprintf(leaf, sizeof(leaf), "%s/book.txt", names[i]); book(&legacy_books[i], root, leaf);
    }
    assert(book_collection_open(&s, migration_prefix) == ESP_OK);
    write_fail = 1;
    assert(book_collection_sync(&s, root, legacy_folders, 6, legacy_books, 6, true) != ESP_OK && !book_collection_migrated(&s));
    assert(book_collection_sync(&s, root, legacy_folders, 6, legacy_books, 6, true) == ESP_OK && book_collection_count(&s) == 6);
    for (int i = 0; i < 6; ++i) {
        assert(legacy_books[i].category_id && book_collection_name(book_collection_find(&s, legacy_books[i].category_id)->name, clean));
        for (int j = 0; j < i; ++j) assert(legacy_books[i].category_id != legacy_books[j].category_id);
    }
    assert(!strcmp(book_collection_find(&s, legacy_books[3].category_id)->name, names[3]));
    assert(!strcmp(book_collection_find(&s, legacy_books[4].category_id)->name, names[4]));
    book_collection_close(&s); assert(book_collection_open(&s, migration_prefix) == ESP_OK);
    generation = s.generation;
    assert(book_collection_sync(&s, root, legacy_folders, 6, legacy_books, 6, true) == ESP_OK && s.generation == generation);
    book_collection_close(&s);
    // 初次空扫描后出现目录：普通扫描保留未分类，显式重扫补入且不改变已有手工归属。
    // Folders arriving after an empty migration need an explicit import, retaining existing manual assignments.
    char rescan_prefix[288], late[288], empty[288];
    snprintf(rescan_prefix,sizeof(rescan_prefix),"%s/rescan",directory);
    snprintf(late,sizeof(late),"%s/后来目录",root);snprintf(empty,sizeof(empty),"%s/空目录",root);
    const char *late_folders[]={late,empty};
    book_shelf_entry_t late_books[3];
    book(&late_books[0],root,"后来目录/one.txt");book(&late_books[1],root,"后来目录/two.txt");book(&late_books[2],root,"root.txt");
    assert(book_collection_open(&s,rescan_prefix)==ESP_OK);
    assert(book_collection_sync(&s,root,NULL,0,NULL,0,true)==ESP_OK && book_collection_migrated(&s));
    assert(book_collection_sync(&s,root,late_folders,2,late_books,3,true)==ESP_OK && !book_collection_count(&s));
    late_books[1].selected=true;
    assert(book_collection_create(&s,"手工分类",late_books,3,true,&id,&changed)==ESP_OK && changed==1);
    generation=s.generation;
    assert(book_collection_rescan(&s,root,late_folders,2,late_books,3,false)==ESP_OK && s.generation==generation);
    for(int failure=0;failure<3;++failure){
        write_fail=failure==0;sync_fail=failure==1;allocation_fail=failure==2?0:-1;
        assert(book_collection_rescan(&s,root,late_folders,2,late_books,3,true)!=ESP_OK && s.generation==generation);
        write_fail=sync_fail=0;allocation_fail=-1;
        assert(book_collection_count(&s)==1 && !book_collection_assignment(&s,late_books[0].path));
        assert(book_collection_assignment(&s,late_books[1].path)==id);
    }
    assert(book_collection_rescan(&s,root,late_folders,2,late_books,3,true)==ESP_OK && book_collection_count(&s)==3);
    uint32_t imported=late_books[0].category_id;
    assert(imported && late_books[1].category_id==id && !late_books[2].category_id);
    assert(book_collection_rename(&s,imported,"改名保留")==ESP_OK);
    late_books[0].selected=true;late_books[1].selected=false;
    assert(book_collection_assign(&s,late_books,3,true,0,&changed)==ESP_OK && changed==1);
    generation=s.generation;
    assert(book_collection_rescan(&s,root,late_folders,2,late_books,3,true)==ESP_OK && s.generation==generation);
    assert(!late_books[0].category_id && late_books[1].category_id==id);
    assert(!strcmp(book_collection_find(&s,imported)->name,"改名保留"));
    book_collection_close(&s);assert(book_collection_open(&s,rescan_prefix)==ESP_OK);
    assert(book_collection_legacy_id(&s,"后来目录")==imported && book_collection_assignment(&s,late_books[1].path)==id);
    assert(book_collection_remove(&s,imported)==ESP_OK);
    assert(book_collection_rescan(&s,root,late_folders,2,late_books,3,true)==ESP_OK);
    assert(late_books[0].category_id>imported && late_books[1].category_id==id);
    uint32_t renamed=late_books[0].category_id;
    assert(book_collection_rename(&s,renamed,"冲突目录")==ESP_OK);
    char collision_folder[288], matching_folder[288];
    snprintf(collision_folder,sizeof(collision_folder),"%s/冲突目录",root);
    snprintf(matching_folder,sizeof(matching_folder),"%s/手工分类",root);
    const char *extra_folders[]={collision_folder,matching_folder};
    book_shelf_entry_t extra_books[2];book(&extra_books[0],root,"冲突目录/new.txt");book(&extra_books[1],root,"手工分类/new.txt");
    assert(book_collection_sync(&s,root,extra_folders,2,extra_books,2,true)==ESP_OK);
    assert(book_collection_rescan(&s,root,extra_folders,2,extra_books,2,true)==ESP_OK);
    assert(extra_books[0].category_id && extra_books[0].category_id!=renamed && extra_books[1].category_id==id);
    assert(!strcmp(book_collection_find(&s,renamed)->name,"冲突目录"));
    assert(strcmp(book_collection_find(&s,extra_books[0].category_id)->name,"冲突目录"));
    generation=s.generation;
    assert(book_collection_rescan(&s,root,extra_folders,2,extra_books,2,true)==ESP_OK && s.generation==generation);
    book_collection_close(&s);
    original = fopen(books[0].path, "rb"); assert(original); char content[32]; assert(fgets(content, sizeof(content), original));
    assert(!strcmp(content, "original book\n") && !fclose(original));
    assert(!unlink(books[0].path) && !rmdir(folder) && !rmdir(root));
    for (int slot = 0; slot < 2; ++slot) { char path[320]; snprintf(path, sizeof(path), "%s.%d", prefix, slot); unlink(path); snprintf(path, sizeof(path), "%s.%d", second, slot); unlink(path); snprintf(path, sizeof(path), "%s.%d", migration_prefix, slot); unlink(path); snprintf(path, sizeof(path), "%s.%d", rescan_prefix, slot); unlink(path); }
    assert(!rmdir(directory));
    puts("logical collections: PASS (migration, stable IDs, same filenames, internal books, persistent removal, no file mutations, failed-batch rollback, card isolation, corrupt copies, UTF-8 and allocation/write failures)");
}
