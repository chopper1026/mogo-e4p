/*
 * 中文：阅读书签的 TF 卡原子文件存储；文件名按完整书籍路径散列，记录仍保存原路径校验归属。
 * English: Atomic TF-card bookmark storage; filenames hash the full book path while records retain ownership paths.
 *
 * 冻结：不扫描整张卡，不限制书签数量；实际边界仅来自 TF 卡容量和可分配内存。
 * Frozen: Do not scan the whole card or cap bookmark count; only card capacity and allocatable memory bound it.
 */
#include "book_bookmark_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef BOOKMARK_PARENT
#define BOOKMARK_PARENT "/sdcard/.readpico"
#endif
#ifndef BOOKMARK_ROOT
#define BOOKMARK_ROOT BOOKMARK_PARENT "/bookmarks"
#endif

static bool card_path(const char *path) {
    return path && !strncmp(path, "/sdcard/", 8) && strnlen(path, BOOK_STORE_PATH_MAX) < BOOK_STORE_PATH_MAX;
}

static uint64_t path_hash(const char *path) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    return hash;
}

// 活动目录与恢复临时目录均有固定位置，不接受备份提供的文件名。/ Active and staging roots are fixed; never accept filenames from a backup.
#define STAGING BOOKMARK_ROOT ".restore"
#define PREVIOUS BOOKMARK_ROOT ".old"
typedef char slot_path_t[sizeof(STAGING) + sizeof("/0123456789abcdef.bin.bak")];

static bool record_path(const char *root, const char *path, const char *suffix, slot_path_t out) {
    if (!card_path(path)) return false;
    int n = snprintf(out, sizeof(slot_path_t), "%s/%016llx.bin%s", root, (unsigned long long)path_hash(path), suffix);
    return n > 0 && (size_t)n < sizeof(slot_path_t);
}

static esp_err_t exists(const char *path, bool *present) {
    struct stat st;
    if (!stat(path, &st)) { *present = true; return ESP_OK; }
    *present = false;
    return errno == ENOENT ? ESP_OK : ESP_FAIL;
}
static esp_err_t remove_if_present(const char *path) {
    return !remove(path) || errno == ENOENT ? ESP_OK : ESP_FAIL;
}
static esp_err_t active_root(const char **root) {
    bool active, previous;
    if (exists(BOOKMARK_ROOT, &active) != ESP_OK) return ESP_FAIL;
    *root = BOOKMARK_ROOT;
    if (!active) {
        if (exists(PREVIOUS, &previous) != ESP_OK) return ESP_FAIL;
        if (previous) *root = PREVIOUS;
    }
    return ESP_OK;
}
static esp_err_t ensure_root(void) {
    const char *root;
    if (active_root(&root) != ESP_OK) return ESP_FAIL;
    if (strcmp(root, BOOKMARK_ROOT) && rename(root, BOOKMARK_ROOT)) return ESP_FAIL;
    if (mkdir(BOOKMARK_PARENT, 0777) && errno != EEXIST) return ESP_FAIL;
    return !mkdir(BOOKMARK_ROOT, 0777) || errno == EEXIST ? ESP_OK : ESP_FAIL;
}

// 仅在主文件缺失时读取旧副本；纯读取不会修复目录或触发写入。/ Consult the rollback copy only when the primary is absent; reads never write.
static esp_err_t read_slot(const char *filename, const char *owner, reader_bookmarks_unbounded_t **out,
                           uint32_t *book_size) {
    struct stat st;
    if (stat(filename, &st)) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    if (!S_ISREG(st.st_mode) || st.st_size < (off_t)sizeof(reader_bookmarks_unbounded_t) || (uint64_t)st.st_size > SIZE_MAX)
        return ESP_ERR_INVALID_STATE;
    size_t size = (size_t)st.st_size;
    FILE *f = fopen(filename, "rb");
    if (!f) return ESP_FAIL;
    reader_bookmarks_unbounded_t header;
    esp_err_t err = ESP_OK;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) err = ESP_FAIL;
    else if (!reader_bookmarks_v3_valid(&header, size) || !card_path(header.path) ||
             (owner && strcmp(header.path, owner))) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && book_size) *book_size = header.file_size;
    if (err == ESP_OK && out) {
        *out = malloc(size);
        if (!*out) err = ESP_ERR_NO_MEM;
        else {
            memcpy(*out, &header, sizeof(header));
            size_t payload = size - sizeof(header);
            if (fread((*out)->entries, 1, payload, f) != payload) err = ESP_FAIL;
        }
    }
    if (fclose(f)) err = ESP_FAIL;
    if (err != ESP_OK && out) { free(*out); *out = NULL; }
    return err;
}

esp_err_t book_bookmark_store_load(const char *path, uint32_t file_size, reader_bookmarks_unbounded_t **out) {
    if (!out || !card_path(path)) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    const char *root;
    slot_path_t name, bak;
    if (active_root(&root) != ESP_OK) return ESP_FAIL;
    record_path(root, path, "", name); record_path(root, path, ".bak", bak);
    uint32_t stored = 0;
    esp_err_t err = read_slot(name, path, out, &stored);
    if (err == ESP_ERR_NOT_FOUND) err = read_slot(bak, path, out, &stored);
    if (err == ESP_OK && stored != file_size) { free(*out); *out = NULL; return ESP_ERR_INVALID_SIZE; }
    return err;
}

static esp_err_t write_file(const char *name, const reader_bookmarks_unbounded_t *marks, size_t len) {
    FILE *f = fopen(name, "wb");
    if (!f) return ESP_FAIL;
    bool ok = fwrite(marks, 1, len, f) == len && !fflush(f) && !fsync(fileno(f));
    return !fclose(f) && ok ? ESP_OK : ESP_FAIL;
}

// FatFs 不能覆盖目标：先归一上次中断的状态，保留可读旧版，再发布新文件。
// FatFs cannot rename over an existing target. Normalize any prior interruption and keep a readable rollback copy until publication.
static esp_err_t prepare_slot(const char *root, const char *owner, slot_path_t name, slot_path_t bak, bool *had_old) {
    record_path(root, owner, "", name); record_path(root, owner, ".bak", bak);
    esp_err_t err = read_slot(name, owner, NULL, NULL);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) return err;
    *had_old = err == ESP_OK;
    // 已校验的主文件是已提交版本；其残余回滚文件不再是数据源。/ A validated primary is authoritative; its stale rollback slot is disposable.
    if (*had_old) return remove_if_present(bak);
    err = read_slot(bak, owner, NULL, NULL);
    if (err == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (rename(bak, name)) return ESP_FAIL;
    *had_old = true;
    return ESP_OK;
}

esp_err_t book_bookmark_store_save(const reader_bookmarks_unbounded_t *marks) {
    size_t len;
    if (!marks || !reader_bookmarks_unbounded_size(marks->count, &len) || !reader_bookmarks_v3_valid(marks, len) ||
        !card_path(marks->path)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ensure_root();
    slot_path_t name, bak, tmp;
    bool old;
    if (err == ESP_OK) err = prepare_slot(BOOKMARK_ROOT, marks->path, name, bak, &old);
    if (err != ESP_OK) return err;
    record_path(BOOKMARK_ROOT, marks->path, ".tmp", tmp);
    err = write_file(tmp, marks, len);
    if (err == ESP_OK && old && rename(name, bak)) err = ESP_FAIL;
    if (err == ESP_OK && rename(tmp, name)) err = ESP_FAIL;
    if (err != ESP_OK) { (void)remove_if_present(tmp); return err; }
    if (old) (void)remove_if_present(bak);
    return ESP_OK;
}

esp_err_t book_bookmark_store_remove(const char *path) {
    if (!card_path(path)) return ESP_ERR_INVALID_ARG;
    slot_path_t name, bak;
    bool old;
    const char *root;
    esp_err_t err = active_root(&root);
    if (err == ESP_OK) err = prepare_slot(root, path, name, bak, &old);
    return err == ESP_OK && old ? remove_if_present(name) : err;
}

esp_err_t book_bookmark_store_relocate(const char *old_path, const char *new_path, uint32_t file_size) {
    if (!card_path(old_path) || !card_path(new_path) || !strcmp(old_path, new_path)) return ESP_ERR_INVALID_ARG;
    reader_bookmarks_unbounded_t *marks = NULL, *dst = NULL;
    esp_err_t err = book_bookmark_store_load(old_path, file_size, &marks);
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_SIZE) return ESP_OK;
    if (err != ESP_OK) return err;
    memset(marks->path, 0, sizeof(marks->path)); strcpy(marks->path, new_path);
    err = book_bookmark_store_load(new_path, file_size, &dst);
    if (err == ESP_ERR_NOT_FOUND) err = book_bookmark_store_save(marks);
    else if (err == ESP_OK && (marks->count != dst->count ||
        memcmp(marks->entries, dst->entries, (size_t)marks->count * sizeof(*marks->entries)))) err = ESP_ERR_INVALID_STATE;
    free(marks); free(dst);
    return err == ESP_OK ? book_bookmark_store_remove(old_path) : err;
}

static bool slot_name(const char *s, const char *suffix) {
    return strspn(s, "0123456789abcdef") == 16 && !strcmp(s + 16, suffix);
}

esp_err_t book_bookmark_store_visit(book_bookmark_visit_fn visit, void *context, char *bad_slot, size_t cap) {
    const char *root;
    if (bad_slot && cap) *bad_slot = 0;
    if (!visit) return ESP_ERR_INVALID_ARG;
    if (active_root(&root) != ESP_OK) return ESP_FAIL;
    DIR *dir = opendir(root);
    if (!dir) return errno == ENOENT ? ESP_OK : ESP_FAIL;
    esp_err_t err = ESP_OK;
    while (err == ESP_OK) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (!e) { if (errno) err = ESP_FAIL; break; }
        bool backup = slot_name(e->d_name, ".bin.bak");
        if (!backup && !slot_name(e->d_name, ".bin")) continue;
        slot_path_t file;
        snprintf(file, sizeof(file), "%s/%.20s", root, e->d_name);
        if (backup) {
            bool primary;
            err = exists(file, &primary);
            if (err != ESP_OK || primary) continue;
            strcat(file, ".bak");
        }
        reader_bookmarks_unbounded_t *marks = NULL;
        err = read_slot(file, NULL, &marks, NULL);
        if (err == ESP_OK) {
            slot_path_t expected; size_t size;
            if (!record_path(root, marks->path, backup ? ".bak" : "", expected) || strcmp(file, expected) ||
                !reader_bookmarks_unbounded_size(marks->count, &size)) err = ESP_ERR_INVALID_STATE;
            else err = visit(marks, size, context);
        }
        if (err == ESP_ERR_INVALID_STATE && bad_slot && cap) snprintf(bad_slot, cap, "%.24s", e->d_name);
        free(marks);
    }
    return closedir(dir) == 0 ? err : ESP_FAIL;
}

// 只清理由本模块拥有的暂存/旧目录；意外内容保留并报错。/ Only clean owned staging/previous directories; preserve unexpected contents and fail.
static esp_err_t clean_directory(const char *root) {
    DIR *dir = opendir(root);
    if (!dir) return errno == ENOENT ? ESP_OK : ESP_FAIL;
    esp_err_t err = ESP_OK;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (!e) { if (errno) err = ESP_FAIL; break; }
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!slot_name(e->d_name, ".bin") && !slot_name(e->d_name, ".bin.bak") && !slot_name(e->d_name, ".bin.tmp")) { err = ESP_ERR_INVALID_STATE; break; }
        slot_path_t file;
        snprintf(file, sizeof(file), "%s/%.24s", root, e->d_name);
        if ((err = remove_if_present(file)) != ESP_OK) break;
    }
    if (closedir(dir)) err = ESP_FAIL;
    if (err == ESP_OK && rmdir(root)) err = ESP_FAIL;
    return err;
}

esp_err_t book_bookmark_restore_begin(void) {
    esp_err_t err = ensure_root();
    if (err == ESP_OK) err = clean_directory(STAGING);
    if (err == ESP_OK && mkdir(STAGING, 0777)) err = ESP_FAIL;
    return err;
}
esp_err_t book_bookmark_restore_put(const reader_bookmarks_unbounded_t *marks) {
    slot_path_t file; size_t len; bool duplicate;
    if (!marks || !reader_bookmarks_unbounded_size(marks->count, &len) || !reader_bookmarks_v3_valid(marks, len) ||
        !record_path(STAGING, marks->path, "", file)) return ESP_ERR_INVALID_ARG;
    if (exists(file, &duplicate) != ESP_OK || duplicate) return ESP_ERR_INVALID_STATE;
    return write_file(file, marks, len);
}
esp_err_t book_bookmark_restore_finish(void) {
    esp_err_t err = clean_directory(PREVIOUS);
    if (err != ESP_OK) return err;
    if (rename(BOOKMARK_ROOT, PREVIOUS) || rename(STAGING, BOOKMARK_ROOT)) return ESP_FAIL;
    (void)clean_directory(PREVIOUS);
    return ESP_OK;
}
