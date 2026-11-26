/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 版本化小端进度记录，以完整路径校验哈希碰撞。
 * Versioned little-endian progress records with full-path collision checks.
 *
 * 冻结：为使书数不再挤占配置空间，每书记录放内部 FAT；rp_books 仅保留序号、last 及待迁移的旧键。
 * v2 改用持久序号，v1 仍可恢复但排序为 0；遗忘只清指定路径，部分失败允许重试。
 * Frozen: keep per-book records on internal FAT to decouple library size from NVS; retain rp_books for seq, last and legacy reads. Never overwrite collisions. Use
 * persistent v2 sequences; v1 resumes with order zero. Forget only the given path, retrying partial failures.
 */
#include "book_progress.h"
#include "book_font_size.h"
#include "book_store.h"
#include "book_progress_store.h"
#include "epdiy.h"


#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

#define PATH_CAP BOOK_STORE_PATH_MAX
#define HEADER_SIZE PROGRESS_HEADER
#define RECORD_CAP PROGRESS_CAP
static const char* TAG = "book_progress";
static const char* NS = "rp_books";

// 列表拓扑仅在 HTTP 停止并 join 后修改；HTTP 只原子标记已有节点。
// Mutate list topology only after HTTP stops and joins; HTTP only atomically marks existing nodes.
struct book_progress_watch {
    char path[PATH_CAP];
    atomic_bool invalidated;
    struct book_progress_watch* next;
};
static book_progress_watch_t* s_watches;
static atomic_uint s_progress_revision;
uint32_t book_progress_revision(void){return atomic_load_explicit(&s_progress_revision,memory_order_relaxed);}
void book_progress_notify_changed(void){atomic_fetch_add_explicit(&s_progress_revision,1,memory_order_relaxed);}

static bool path_valid(const char* path) {
    return path != NULL && path[0] != '\0' && strnlen(path, PATH_CAP) < PATH_CAP;
}

book_progress_watch_t* book_progress_watch_create(const char* path) {
    if (!path_valid(path)) return NULL;
    book_progress_watch_t* watch = malloc(sizeof(*watch));
    if (!watch) return NULL;
    strcpy(watch->path, path);
    atomic_init(&watch->invalidated, false);
    watch->next = s_watches;
    s_watches = watch;
    return watch;
}

bool book_progress_watch_invalidated(const book_progress_watch_t* watch) {
    return watch && atomic_load(&watch->invalidated);
}

void book_progress_watch_destroy(book_progress_watch_t* watch) {
    book_progress_watch_t** p = &s_watches;
    while (*p && *p != watch) p = &(*p)->next;
    if (*p) { *p = watch->next; free(watch); }
}

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static esp_err_t warn_error(esp_err_t err) {
    if (err != ESP_OK) ESP_LOGW(TAG, "progress persistence: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t legacy(
    nvs_handle_t h, const char* key, const char* owner, uint8_t* data, size_t* len) {
    if (!h) return ESP_ERR_NOT_FOUND;
    *len = RECORD_CAP;
    esp_err_t e = nvs_get_blob(h, key, data, len);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (e != ESP_OK) return e;
    return progress_record_valid(key, data, *len) &&
                   (!owner || !strcmp(owner, (char*)data + HEADER_SIZE))
               ? ESP_OK
               : ESP_ERR_INVALID_STATE;
}
static bool decode(const uint8_t* data, book_progress_t* out) {
    if (data[14] < BOOK_READER_PX_MIN || data[14] > BOOK_READER_PX_MAX || data[15] > 100)
        return false;
    *out = (book_progress_t){ .file_size = get32(data + 4),
                              .chapter = (uint16_t)(data[8] | (data[9] << 8)),
                              .byte_off = get32(data + 10),
                              .px = data[14],
                              .pct = data[15],
                              .last_open_s = data[3] == 2 ? get32(data + 16) : 0 };
    return true;
}
bool book_progress_load_handle(uintptr_t h, const char* path, uint32_t size, book_progress_t* out) {
    if (!path_valid(path) || !out) return false;
    uint8_t data[RECORD_CAP];
    size_t len;
    esp_err_t e = book_store_prepare_data();
    if (e == ESP_OK)
        e = progress_store_get(path, data, &len);
    else
        e = ESP_ERR_NOT_FOUND;  // 卷未挂载仍可续读旧键。/ Unmounted flash need not prevent legacy resume.
    if (e != ESP_OK) {
        char key[11];
        progress_key(path, key);
        e = legacy((nvs_handle_t)h, key, path, data, &len);
    }
    return e == ESP_OK && get32(data + 4) == size && decode(data, out);
}
bool book_progress_load(const char* path, uint32_t size, book_progress_t* out) {
    nvs_handle_t h = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) h = 0;
    bool ok = book_progress_load_handle(h, path, size, out);
    if (h) nvs_close(h);
    return ok;
}
typedef struct {
    book_progress_visit_fn visit;
    void* ctx;
    bool stopped;
} walk_t;
static esp_err_t visit_row(const char* key, const uint8_t* data, size_t len, void* ctx) {
    (void)key;
    (void)len;
    walk_t* w = ctx;
    book_progress_t value;
    if (decode(data, &value) && !w->visit((const char*)data + HEADER_SIZE, &value, w->ctx)) {
        w->stopped = true;
        return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t book_progress_list(book_progress_visit_fn visit, void* ctx) {
    if (!visit) return ESP_ERR_INVALID_ARG;
    walk_t w = { .visit = visit, .ctx = ctx };
    esp_err_t volume = book_store_prepare_data();
    if (volume == ESP_OK) {
        esp_err_t e = progress_store_visit(visit_row, &w, true);
        if (w.stopped) return ESP_OK;
        if (e != ESP_OK) return warn_error(e);
    }
    nvs_iterator_t it = NULL;
    esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_BLOB, &it);
    if (e == ESP_ERR_NVS_NOT_FOUND) return volume;
    if (e != ESP_OK) return warn_error(e);
    nvs_handle_t h = 0;
    e = nvs_open(NS, NVS_READONLY, &h);
    while (e == ESP_OK && it) {
        nvs_entry_info_t info;
        e = nvs_entry_info(it, &info);
        if (e != ESP_OK) break;
        uint8_t data[RECORD_CAP], other[RECORD_CAP];
        size_t len, n;
        if (legacy(h, info.key, NULL, data, &len) == ESP_OK) {
            esp_err_t fat = volume == ESP_OK
                                ? progress_store_get((char*)data + HEADER_SIZE, other, &n)
                                : ESP_ERR_NOT_FOUND;
            if (fat == ESP_ERR_NOT_FOUND || fat == ESP_ERR_INVALID_STATE)
                (void)visit_row(info.key, data, len, &w);
            else if (fat != ESP_OK) {
                e = fat;
                break;
            }
            if (w.stopped) break;
        }
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (h) nvs_close(h);
    return warn_error(e == ESP_ERR_NVS_NOT_FOUND ? volume : e);
}
static esp_err_t erase_legacy(nvs_handle_t h, const char* key) {
    esp_err_t e = nvs_erase_key(h, key);
    return e == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : e == ESP_OK ? nvs_commit(h) : e;
}
// 候选键先抄到 RAM 再改 NVS，避免在持有迭代器时触发页回收。/ Release the iterator before erasure can recycle its page.
esp_err_t book_progress_migrate(size_t limit, size_t* moved) {
    if (!moved) return ESP_ERR_INVALID_ARG;
    *moved = 0;
    if (!limit || limit > 8) return ESP_ERR_INVALID_ARG;
    // 略过上次冲突位置，先处理其他书；一圈后从头重试。/ Move past a conflicting key so other books can drain, then wrap to retry.
    static char after_failure[16];
    char keys[8][16];
    size_t count = 0;
    bool skipping = after_failure[0] != 0;
    bool skipped = skipping;
    nvs_iterator_t it = NULL;
    nvs_handle_t h = 0;
    esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_BLOB, &it);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (e != ESP_OK) return warn_error(e);
    e = nvs_open(NS, NVS_READWRITE, &h);
    uint8_t data[RECORD_CAP], other[RECORD_CAP];
    size_t len, n;
    while (e == ESP_OK && count < limit) {
        nvs_entry_info_t info;
        e = nvs_entry_info(it, &info);
        if (e != ESP_OK) break;
        if (skipping) {
            if (!strcmp(info.key, after_failure)) skipping = false;
        } else {
            esp_err_t row = legacy(h, info.key, NULL, data, &len);
            if (row == ESP_OK)
                strcpy(keys[count++], info.key);
            else if (
                row != ESP_ERR_INVALID_STATE && row != ESP_ERR_NVS_INVALID_LENGTH &&
                row != ESP_ERR_NOT_FOUND) {
                e = row;
                break;
            }
        }
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (!count && skipped) {
        after_failure[0] = 0;
        e = ESP_ERR_INVALID_STATE;
    }
    bool guarded = e == ESP_OK && count && epd_flash_begin();
    if (e == ESP_OK && count) e = guarded ? book_store_prepare_data() : ESP_ERR_INVALID_STATE;
    for (size_t i = 0; e == ESP_OK && i < count; ++i) {
        e = legacy(h, keys[i], NULL, data, &len);
        if (e != ESP_OK) break;
        const char* path = (char*)data + HEADER_SIZE;
        e = progress_store_get(path, other, &n);
        if (e == ESP_ERR_NOT_FOUND) {
            e = progress_store_put(path, data, len);
            if (e == ESP_OK) e = progress_store_get(path, other, &n);
        }
        // 双份只认可完全相同或本固件已提交的更新序号。/ Accept an identical copy or a verified newer sequence after interrupted cleanup.
        if (e == ESP_OK && !(n == len && !memcmp(other, data, len)) &&
            !(other[3] == 2 && get32(other + 16) > (data[3] == 2 ? get32(data + 16) : 0)))
            e = ESP_ERR_INVALID_STATE;
        if (e == ESP_OK) e = erase_legacy(h, keys[i]);
        if (e == ESP_OK)
            ++*moved;
        else if (e == ESP_ERR_INVALID_STATE)
            strcpy(after_failure, keys[i]);
    }
    if (e == ESP_OK && skipped && count < limit) {
        after_failure[0] = 0;
        e = ESP_ERR_INVALID_STATE;
    }
    if (guarded) epd_flash_end();
    if (h) nvs_close(h);
    return warn_error(e);
}
static esp_err_t owners(nvs_handle_t h, const char* key, const char* path, bool* had) {
    uint8_t data[RECORD_CAP];
    size_t len;
    esp_err_t e = legacy(h, key, path, data, &len);
    *had = e == ESP_OK;
    if (e != ESP_OK && e != ESP_ERR_NOT_FOUND) return e;
    e = book_store_prepare_data();
    if (e != ESP_OK) return e;
    e = progress_store_get(path, data, &len);
    return e == ESP_ERR_NOT_FOUND ? ESP_OK : e;
}
// 改名保持原始位置和排序；目的副本一致时允许事务重试。/ Rebase without changing position or order; identical destinations allow journal replay.
static esp_err_t read_owned(nvs_handle_t h, const char* path, uint8_t* data, size_t* len) {
    char key[11];
    progress_key(path, key);
    esp_err_t e = legacy(h, key, path, data, len);
    if (e != ESP_OK && e != ESP_ERR_NOT_FOUND) return e;
    esp_err_t fat = progress_store_get(path, data, len);
    return fat == ESP_ERR_NOT_FOUND ? e : fat;
}
esp_err_t book_progress_copy_path(const char* old, const char* next, bool copy) {
    if (!path_valid(old) || !path_valid(next)) return ESP_ERR_INVALID_ARG;
    char a[11], b[11];
    progress_key(old, a);
    progress_key(next, b);
    if (!strcmp(a, b)) return ESP_ERR_INVALID_STATE;
    if (copy && !epd_flash_begin()) return ESP_ERR_INVALID_STATE;
    esp_err_t e = book_store_prepare_data();
    nvs_handle_t h = 0;
    if (e == ESP_OK) {
        e = nvs_open(NS, NVS_READONLY, &h);
        if (e == ESP_ERR_NVS_NOT_FOUND) {
            h = 0;
            e = ESP_OK;
        }
    }
    uint8_t from[RECORD_CAP], to[RECORD_CAP];
    size_t n, m;
    if (e == ESP_OK) {
        esp_err_t src = read_owned(h, old, from, &n), dst = read_owned(h, next, to, &m);
        if (src != ESP_OK && src != ESP_ERR_NOT_FOUND)
            e = src;
        else if (dst != ESP_OK && dst != ESP_ERR_NOT_FOUND)
            e = dst;
        else if (!copy && dst == ESP_OK)
            e = ESP_ERR_INVALID_STATE;
        else if (copy && src == ESP_OK) {
            if (from[3] == 1) {
                from[3] = 2;
                put32(from + 16, 0);
            }
            n = strlen(next) + 1;
            from[22] = (uint8_t)n;
            from[23] = (uint8_t)(n >> 8);
            memcpy(from + HEADER_SIZE, next, n);
            n += HEADER_SIZE;
            if (dst == ESP_OK && (n != m || memcmp(from, to, n)))
                e = ESP_ERR_INVALID_STATE;
            else
                e = progress_store_put(next, from, n);
        }
    }
    if (h) nvs_close(h);
    if (copy) epd_flash_end();
    return warn_error(e);
}
esp_err_t book_progress_move_path(const char* old, const char* next) {
    esp_err_t e = book_progress_copy_path(old, next, true);
    nvs_handle_t h;
    if (e != ESP_OK) return e;
    e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_OK) {
        char last[PATH_CAP];
        size_t len = sizeof(last);
        e = nvs_get_str(h, "last", last, &len);
        nvs_close(h);
        if (e == ESP_OK && !strcmp(old, last)) e = book_progress_set_last_path(next);
    }
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    return e == ESP_OK ? book_progress_forget(old) : e;
}
static esp_err_t save_guarded(const char* path, const book_progress_t* progress) {
    if (!path_valid(path) || progress == NULL || progress->px < BOOK_READER_PX_MIN ||
        progress->px > BOOK_READER_PX_MAX || progress->pct > 100)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[RECORD_CAP] = { 'R', 'P', 'B', 2 };
    put32(data + 4, progress->file_size);
    data[8] = (uint8_t)progress->chapter;
    data[9] = (uint8_t)(progress->chapter >> 8);
    put32(data + 10, progress->byte_off);
    data[14] = progress->px;
    data[15] = progress->pct;
    size_t path_len = strlen(path) + 1;
    data[22] = (uint8_t)path_len;
    data[23] = (uint8_t)(path_len >> 8);
    memcpy(data + HEADER_SIZE, path, path_len);
    char key[11];
    progress_key(path, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return warn_error(err);
    bool had;
    err = owners(h, key, path, &had);
    uint32_t sequence = 0;
    if (err == ESP_OK) {
        err = nvs_get_u32(h, "seq", &sequence);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK && sequence == UINT32_MAX) err = ESP_ERR_INVALID_STATE;
    // 先提交序号预留，再写进度；失败可留空洞，但重启后不能倒序。
    // Commit the sequence reservation before progress; failures may leave gaps, never reverse order after restart.
    if (err == ESP_OK) err = nvs_set_u32(h, "seq", ++sequence);
    if (err == ESP_OK) err = nvs_commit(h);
    put32(data + 16, sequence);
    if (err == ESP_OK) err = progress_store_put(path,data,HEADER_SIZE + path_len);
    if (err == ESP_OK && had) err = erase_legacy(h,key);
    nvs_close(h);
    book_progress_notify_changed();
    return warn_error(err);
}
esp_err_t book_progress_save(const char* path, const book_progress_t* progress) {
    if (!epd_flash_begin()) return ESP_ERR_INVALID_STATE;
    esp_err_t e=save_guarded(path, progress);
    epd_flash_end(); return e;
}


static esp_err_t clear_guarded(const char* path) {
    if (!path_valid(path)) return ESP_ERR_INVALID_ARG;
    char key[11];
    progress_key(path, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return warn_error(err);
    bool had;
    err = owners(h, key, path, &had);
    if (err == ESP_OK && had) err = erase_legacy(h,key);
    if (err == ESP_OK) err = progress_store_put(path,NULL,0);
    nvs_close(h);
    book_progress_notify_changed();
    return warn_error(err);
}
esp_err_t book_progress_clear(const char* path) {
    if (!epd_flash_begin()) return ESP_ERR_INVALID_STATE;
    esp_err_t e=clear_guarded(path);
    epd_flash_end(); return e;
}


static esp_err_t forget_guarded(const char* path) {
    if (!path_valid(path)) return ESP_ERR_INVALID_ARG;
    char key[11];
    progress_key(path, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return warn_error(err);
    bool had;
    err = owners(h, key, path, &had);
    char last[PATH_CAP];
    size_t len = sizeof(last);
    bool matching_last = false;
    if (err == ESP_OK) {
        err = nvs_get_str(h, "last", last, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        else if (err == ESP_OK) matching_last = strcmp(last, path) == 0;
    }
    // 先移除自动恢复入口；失败不继续清进度，重试也不会波及其他书。
    // Remove automatic resume first; on failure retain progress, and retries never touch other books.
    if (err == ESP_OK && matching_last) {
        err = nvs_erase_key(h, "last");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(h);
    }
    if (err == ESP_OK && had) err = erase_legacy(h,key);
    if (err == ESP_OK) err = progress_store_put(path,NULL,0);
    nvs_close(h);
    book_progress_notify_changed();
    return warn_error(err);
}
esp_err_t book_progress_forget(const char* path) {
    if (!path_valid(path)) return ESP_ERR_INVALID_ARG;
    for (book_progress_watch_t* watch = s_watches; watch; watch = watch->next)
        if (!strcmp(watch->path, path)) atomic_store(&watch->invalidated, true);
    if (!epd_flash_begin()) return ESP_ERR_INVALID_STATE;
    esp_err_t e=forget_guarded(path);
    epd_flash_end(); return e;
}


bool book_progress_last_path(char* out, size_t cap) {
    if (out == NULL || cap == 0) return false;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, "last", out, &len);
    nvs_close(h);
    if (err != ESP_OK || !path_valid(out)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

esp_err_t book_progress_set_last_path(const char* path) {
    if (path == NULL || strnlen(path, PATH_CAP) >= PATH_CAP) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return warn_error(err);
    err = nvs_set_str(h, "last", path);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    book_progress_notify_changed();
    return warn_error(err);
}
