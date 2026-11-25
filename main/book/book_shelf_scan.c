/*
 * 中文：有界目录遍历、书籍候选和历史补入；页面持有目录数组和导航状态。
 * English: Bounded directory traversal, book candidates and history backfill; the page owns the catalog array and navigation state.
 */
#include "book_shelf_scan.h"
#include "book_shelf_manage.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "book_file_path.h"
#include "book_metadata.h"
#ifdef ESP_PLATFORM
#include "board_sd.h"
#endif
#include "book_progress.h"
#include "book_title.h"
#include "boot_state.h"
#include "book_search.h"
#include "settings.h"

bool book_shelf_hidden_read(nvs_handle_t handle, const char *path) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    char key[11], stored[BOOK_STORE_PATH_MAX];
    snprintf(key, sizeof(key), "h_%08lx", (unsigned long)hash);
    size_t len = sizeof(stored);
    return nvs_get_str(handle, key, stored, &len) == ESP_OK && len > 0 && len <= sizeof(stored) &&
        stored[len - 1] == 0 && !strcmp(stored, path);
}

// 截断必须停在UTF8字符边界。/ Truncation must stop at a UTF8 character boundary.
static void copy_text(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void clean_filename(char *dst, size_t cap, const char *filename) {
    copy_text(dst, cap, filename);
    char *ext = strrchr(dst, '.');
    if (ext) *ext = 0;
    char *start = dst;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != dst) memmove(dst, start, strlen(start) + 1);
    size_t len = strlen(dst);
    while (len && (dst[len - 1] == ' ' || dst[len - 1] == '\t')) dst[--len] = 0;
}

bool book_shelf_catalog_reserve(const book_shelf_catalog_t *catalog) {
    if ((size_t)*catalog->count < *catalog->capacity) return true;
    if (*catalog->count == INT_MAX) return false;
    // 300 本约需 512 个槽位；64 起步可减少冷扫描中的拷贝，同时只多占一页条目空间。
    // Three hundred books need about 512 slots; starting at 64 removes cold-scan copies while keeping small shelves bounded.
    size_t cap = *catalog->capacity ? *catalog->capacity * 2 : 64;
    if (cap > INT_MAX || cap > SIZE_MAX / sizeof(**catalog->entries)) return false;
    book_shelf_entry_t *entries = heap_caps_realloc(*catalog->entries, cap * sizeof(*entries),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) return false;
    *catalog->entries = entries;
    *catalog->capacity = cap;
    return true;
}

// 只有完整匹配的占位词不算署名；不能把包含“未知”的笔名误删。/ Match whole placeholders, not substrings of pen names.
static void clean_author(char *author) {
    size_t start = strspn(author, " \t\r\n");
    if (start) memmove(author, author + start, strlen(author + start) + 1);
    size_t n = strlen(author);
    while (n && strchr(" \t\r\n", author[n - 1])) author[--n] = 0;
    if (!strcmp(author, "未知") || !strcmp(author, "作者未知") || !strcmp(author, "未知作者") ||
        !strcasecmp(author, "unknown") || !strcasecmp(author, "author unknown")) author[0] = 0;
}

// 只接受书名后明确标注的“作者：姓名”，不从分隔符或普通书名推断。/ Accept explicit trailing bylines, never guess from separators or ordinary titles.
static const char *title_byline(const char *title) {
    const size_t bracket = strlen("》");
    for (const char *p = title; (p = strstr(p, "作者")); p += strlen("作者")) {
        bool after_title = (size_t)(p - title) >= bracket && !memcmp(p - bracket, "》", bracket);
        if (p != title && !after_title && p[-1] != ' ' && p[-1] != '\t') continue;
        const char *colon = p + strlen("作者");
        if (*colon == ':') return colon + 1;
        if (!strncmp(colon, "：", strlen("："))) return colon + strlen("：");
    }
    return "";
}

void book_shelf_resolve_author(book_shelf_entry_t *item, bool cache_only) {
    if (item->author_checked) return;
    clean_author(item->author);
    if (item->author[0]) { item->author_checked = true; return; }
    char title[sizeof(item->name)] = {0}, author[160] = {0};
    if (book_metadata_supported(item->path)) {
        if (!pico_boot_asset_allowed(item->path)) { item->author_checked = !cache_only; return; }
        esp_err_t err = cache_only ? book_metadata_cached(item->path, title, sizeof(title), author, sizeof(author))
                                   : book_metadata_read(item->path, title, sizeof(title), author, sizeof(author));
        if (err != ESP_OK) {
            if (cache_only) return;
            title[0] = author[0] = 0;
        }
    }
    clean_author(author);
    if (!author[0]) {
        copy_text(author, sizeof(author), title_byline(title));
        clean_author(author);
    }
    if (!author[0] && book_title_from_path(item->path, title, sizeof(title))) {
        copy_text(author, sizeof(author), title_byline(title));
        clean_author(author);
    }
    copy_text(item->author, sizeof(item->author), author);
    item->author_checked = true;
}

// 显式目录栈放在PSRAM，保持原深度上限和逐项导航轮询。
// Keep explicit directory frames in PSRAM with the original depth limit and per-entry navigation polling.
#define BOOK_SCAN_DEPTH_LIMIT 12
typedef struct {
    DIR *dir;
    char path[BOOK_STORE_PATH_MAX];
} shelf_scan_frame_t;

void book_shelf_scan_dir(const book_shelf_scan_t *scan, const char *root, bool is_flash,
                         nvs_handle_t favorites, nvs_handle_t hidden,
                         bool *truncated, bool *unreadable, bool *skipped) {
    if (*truncated) return;
    shelf_scan_frame_t *frames = heap_caps_calloc(BOOK_SCAN_DEPTH_LIMIT + 1, sizeof(*frames),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frames) { *truncated = true; return; }
    size_t root_len = strnlen(root, sizeof(frames[0].path));
    if (root_len >= sizeof(frames[0].path)) { *skipped = true; free(frames); return; }
    frames[0].dir = opendir(root);
    if (!frames[0].dir) { *unreadable = true; free(frames); return; }
    memcpy(frames[0].path, root, root_len + 1);
    int depth = 0;
    while (depth >= 0 && !*truncated) {
        if (scan->cancel && scan->cancel(scan->cancel_ctx)) break;
        errno = 0;
        struct dirent *ent = readdir(frames[depth].dir);
        if (!ent) {
            if (errno) *unreadable = true;
            closedir(frames[depth].dir);
            frames[depth].dir = NULL;
            --depth;
            continue;
        }
        if (ent->d_name[0] == '.') continue;
        char path[BOOK_STORE_PATH_MAX];
        int len = snprintf(path, sizeof(path), "%s/%s", frames[depth].path, ent->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) { *skipped = true; continue; }
        struct stat st;
        if (stat(path, &st) != 0) { *unreadable = true; continue; }
#ifdef ESP_PLATFORM
        if (!is_flash && !strcmp(root, app_settings_books_dir())) {
            bool hidden_entry = false;
            if (board_sd_path_hidden(path, &hidden_entry) != ESP_OK) { *unreadable = true; continue; }
            if (hidden_entry) continue;
        }
#endif
        if (S_ISDIR(st.st_mode)) {
            if (scan->category_add && !is_flash && depth == 0 && !strcmp(root, app_settings_books_dir()) && scan->category_add(path)<0) *skipped=true;
            if (depth == BOOK_SCAN_DEPTH_LIMIT) { *skipped = true; continue; }
            DIR *child = opendir(path);
            if (!child) { *unreadable = true; continue; }
            ++depth;
            frames[depth].dir = child;
            memcpy(frames[depth].path, path, (size_t)len + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        if (!book_file_path_supported(path)) continue;
        // 移出书架只隐藏文件，重新打开才恢复上架。
        // Removal hides a file from the shelf until the book is reopened.
        if (hidden && scan->hidden_read(hidden, path)) continue;
        if (strlen(ent->d_name) >= sizeof(((book_shelf_entry_t *)0)->name) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) { *skipped = true; continue; }
        if (!book_shelf_catalog_reserve(&scan->catalog)) { *truncated = true; break; }
        book_shelf_entry_t candidate = {0};
        book_shelf_entry_t *item = &candidate;
        copy_text(item->path, sizeof(item->path), path);
        bool cached = scan->cache && book_shelf_cache_lookup(scan->cache, item->path,
            (uint32_t)st.st_size, (int64_t)st.st_mtime, item->name, sizeof(item->name),
            item->author, sizeof(item->author));
        if (!cached && !book_title_from_path(item->path, item->name, sizeof(item->name)))
            clean_filename(item->name, sizeof(item->name), ent->d_name);
        // 首屏只读缓存，冷书留到可见行空闲时核对。/ Keep first paint cache-only; resolve cold visible rows on idle.
        if (scan->metadata) book_shelf_resolve_author(item, true);
        item->search_match = book_search_match(item->name, scan->query);
        item->size = st.st_size;
        item->is_flash = is_flash;
        book_progress_t p;
        item->has_progress = scan->progress_checked
            ? book_progress_load_handle((uintptr_t)scan->progress, item->path, item->size, &p)
            : book_progress_load(item->path, item->size, &p);
        item->pct = item->has_progress ? p.pct : 0;
        item->chapter = item->has_progress ? p.chapter : 0;
        item->recent = item->has_progress ? p.last_open_s : 0;
        item->favorite = favorites && scan->favorite_read(favorites, item->path);
        (*scan->catalog.entries)[(*scan->catalog.count)++] = candidate;
    }
    for (int i = 0; i <= depth; ++i)
        if (frames[i].dir) closedir(frames[i].dir);
    free(frames);
}

typedef struct {
    const book_shelf_scan_t *scan;
    nvs_handle_t favorites, hidden;
    bool truncated;
} shelf_backfill_t;

static bool shelf_backfill_visit(const char *path, const book_progress_t *progress, void *ctx) {
    shelf_backfill_t *backfill = ctx;
    const book_shelf_scan_t *scan = backfill->scan;
    if (backfill->truncated) return false;
    if (scan->cancel && scan->cancel(scan->cancel_ctx)) return false;
    if (!book_file_path_supported(path)) return true;
    for (int i = 0; i < *scan->catalog.count; ++i)
        if (!strcmp((*scan->catalog.entries)[i].path, path)) return true;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > UINT32_MAX || (uint32_t)st.st_size != progress->file_size)
        return true;
    if (backfill->hidden && scan->hidden_read(backfill->hidden, path)) return true;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strlen(base) >= sizeof(((book_shelf_entry_t *)0)->name)) return true;
    if (!book_shelf_catalog_reserve(&scan->catalog)) { backfill->truncated = true; return false; }
    book_shelf_entry_t candidate = {0};
    book_shelf_entry_t *item = &candidate;
    copy_text(item->path, sizeof(item->path), path);
    if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
        clean_filename(item->name, sizeof(item->name), base);
    if (!item->name[0]) return true;
    if (scan->cache) (void)book_shelf_cache_lookup(scan->cache, item->path,
        (uint32_t)st.st_size, (int64_t)st.st_mtime, item->name, sizeof(item->name),
        item->author, sizeof(item->author));
    if (scan->metadata) book_shelf_resolve_author(item, true);
    item->size = (uint32_t)st.st_size;
    item->is_flash = !strncmp(path, "/flash/", 7);
    item->has_progress = true;
    item->pct = progress->pct;
    item->chapter = progress->chapter;
    item->recent = progress->last_open_s;
    item->search_match = book_search_match(item->name, scan->query);
    item->favorite = backfill->favorites && scan->favorite_read(backfill->favorites, item->path);
    (*scan->catalog.entries)[(*scan->catalog.count)++] = candidate;
    return true;
}

bool book_shelf_scan_backfill(const book_shelf_scan_t *scan, bool *truncated) {
    nvs_handle_t favorites = 0, hidden = 0;
    (void)nvs_open("rp_favs", NVS_READONLY, &favorites);
    (void)nvs_open("rp_shelf", NVS_READONLY, &hidden);
    const int before = *scan->catalog.count;
    shelf_backfill_t backfill = {.scan = scan, .favorites = favorites, .hidden = hidden};
    (void)book_progress_list(shelf_backfill_visit, &backfill);
    if (favorites) nvs_close(favorites);
    if (hidden) nvs_close(hidden);
    if (truncated && backfill.truncated) *truncated = true;
    return *scan->catalog.count != before;
}

void book_shelf_scan_retries(const book_shelf_scan_t *scan, const struct book_shelf_retry *retries,
                             bool *truncated) {
    for (const book_shelf_retry_t *p = retries; p; p = p->next) {
        int i = 0;
        while (i < *scan->catalog.count && strcmp((*scan->catalog.entries)[i].path, p->entry.path)) ++i;
        if (i == *scan->catalog.count) {
            if (!book_shelf_catalog_reserve(&scan->catalog)) { *truncated = true; continue; }
            ++*scan->catalog.count;
        }
        (*scan->catalog.entries)[i] = p->entry;
        (*scan->catalog.entries)[i].selected = false;
        (*scan->catalog.entries)[i].search_match = book_search_match(p->entry.name, scan->query);
    }
}
