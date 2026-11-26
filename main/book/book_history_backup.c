#include "book_progress.h"
/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：把阅读进度、时长、书签、收藏、自定义书名和移出书架状态流式备份到 TF 卡。
 * English: Stream progress, reading time, bookmarks, favorites, titles and shelf removals to TF.
 */
#include "book_history_backup.h"
#include "book_bookmark_store.h"
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "restore_nvs.h"

#define HISTORY_MAX_RECORDS 512
#define HISTORY_MAX_VALUE 4096
#define HISTORY_RECORD_HEADER 20

static const char *const namespaces[] = {
    "rp_books", "rp_ticket", "rp_marks", "rp_favs", "rp_titles", "rp_shelf"
};

static uint32_t hash_bytes(uint32_t hash, const void *data, size_t len) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < len; ++i) hash = (hash ^ bytes[i]) * UINT32_C(16777619);
    return hash;
}

static bool book_key(const char *key, char prefix) {
    if (key[0] != prefix || key[1] != '_' || strlen(key) != 10) return false;
    for (int i = 2; i < 10; ++i)
        if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
    return true;
}

static bool valid_record(unsigned ns, uint8_t type, const char *key, const uint8_t *data, size_t len) {
    if (ns >= sizeof(namespaces) / sizeof(namespaces[0]) || !key[0] ||
        strnlen(key, 16) == 16 || !len || (ns != 2 && len > HISTORY_MAX_VALUE)) return false;
    if (ns == 0) {
        if (!strcmp(key, "seq")) return type == NVS_TYPE_U32 && len == 4;
        if (!strcmp(key, "last")) return type == NVS_TYPE_STR && len >= 1 &&
            data[len - 1] == 0 && strnlen((const char *)data, len) == len - 1;
        return book_key(key, 'b') && type == NVS_TYPE_BLOB && len >= 26 &&
            len <= 24 + 288 && !memcmp(data, "RPB", 3) && (data[3] == 1 || data[3] == 2) &&
            ((size_t)data[22] | ((size_t)data[23] << 8)) == len - 24 &&
            strnlen((const char *)data + 24, len - 24) == len - 25;
    }
    if (ns == 1) {
        uint32_t magic = 0;
        if (len >= 4) memcpy(&magic, data, 4);
        if (!strcmp(key, "stats")) return type == NVS_TYPE_BLOB && len == 32 && magic == UINT32_C(0x52505431);
        if (!strcmp(key, "heat32")) return type == NVS_TYPE_BLOB && len == 260 && magic == UINT32_C(0x52504832);
        if (!strcmp(key, "heatmap")) return type == NVS_TYPE_BLOB && len == 244 && magic == UINT32_C(0x52504831);
        return false;
    }
    if (ns == 2) {
        if (!book_key(key, 'm') || type != NVS_TYPE_BLOB) return false;
        const char *path = reader_bookmarks_owner(data, len);
        if (!path) return false;
        char expected[11];
        snprintf(expected, sizeof(expected), "m_%08lx", (unsigned long)hash_bytes(UINT32_C(2166136261), path, strlen(path)));
        return !strcmp(key, expected);
    }
    if (ns == 3) return book_key(key, 'f') && type == NVS_TYPE_U8 && len == 1 && data[0] <= 1;
    if (ns == 4) return book_key(key, 't') && type == NVS_TYPE_BLOB && len >= 4;
    // 新命名空间追加在末尾，旧备份的索引保持不变；完整路径与键必须属于同一本书。
    // Append the namespace to preserve legacy indices; the full path must match its hashed key.
    if (!book_key(key, 'h') || type != NVS_TYPE_STR || len < 2 || len > 288 ||
        data[0] != '/' || data[len - 1] != 0 || strnlen((const char *)data, len) != len - 1)
        return false;
    char expected[11];
    snprintf(expected, sizeof(expected), "h_%08lx",
             (unsigned long)hash_bytes(UINT32_C(2166136261), data, len - 1));
    return !strcmp(key, expected);
}

static esp_err_t read_value(nvs_handle_t h, unsigned ns, const nvs_entry_info_t *info, uint8_t **value, size_t *len) {
    *value = NULL;
    *len = 0;
    esp_err_t err = ESP_OK;
    if (info->type == NVS_TYPE_U8) *len = 1;
    else if (info->type == NVS_TYPE_U32) *len = 4;
    else if (info->type == NVS_TYPE_STR) err = nvs_get_str(h, info->key, NULL, len);
    else if (info->type == NVS_TYPE_BLOB) err = nvs_get_blob(h, info->key, NULL, len);
    else return ESP_ERR_INVALID_ARG;
    if ((info->type == NVS_TYPE_STR || info->type == NVS_TYPE_BLOB) && err != ESP_OK) return err;
    if (!*len || (ns != 2 && *len > HISTORY_MAX_VALUE)) return ESP_ERR_INVALID_SIZE;
    *value = malloc(*len);
    if (!*value) return ESP_ERR_NO_MEM;
    if (info->type == NVS_TYPE_U8) err = nvs_get_u8(h, info->key, *value);
    else if (info->type == NVS_TYPE_U32) err = nvs_get_u32(h, info->key, (uint32_t *)*value);
    else if (info->type == NVS_TYPE_STR) err = nvs_get_str(h, info->key, (char *)*value, len);
    else err = nvs_get_blob(h, info->key, *value, len);
    if (err != ESP_OK) { free(*value); *value = NULL; }
    return err;
}

#define CARD_NS 6
#define V2_HEADER 22

typedef struct { FILE *file; uint32_t hash, count; } history_writer_t;
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static esp_err_t write_record(history_writer_t *w, unsigned ns, uint8_t type, const char *key,
                              const void *value, size_t len) {
    uint8_t head[V2_HEADER] = {(uint8_t)ns, type};
    if (len > UINT32_MAX || w->count == UINT32_MAX) return ESP_ERR_INVALID_SIZE;
    memcpy(head + 2, key, strlen(key)); put32(head + 18, (uint32_t)len);
    if (fwrite(head, 1, sizeof(head), w->file) != sizeof(head) || fwrite(value, 1, len, w->file) != len) return ESP_FAIL;
    w->hash = hash_bytes(hash_bytes(w->hash, head, sizeof(head)), value, len);
    ++w->count;
    return ESP_OK;
}
static esp_err_t write_card(const reader_bookmarks_unbounded_t *marks, size_t len, void *ctx) {
    return write_record(ctx, CARD_NS, NVS_TYPE_BLOB, "", marks, len);
}

esp_err_t book_history_backup_write(FILE *file, char *bad_slot, size_t cap) {
    if (!file) return ESP_ERR_INVALID_ARG;
    if (fwrite("RPHIST2", 1, 8, file) != 8) return ESP_FAIL;
    history_writer_t w = {file, UINT32_C(2166136261), 0};
    for (unsigned ns = 0; ns < CARD_NS; ++ns) {
        nvs_handle_t h;
        esp_err_t err = nvs_open(namespaces[ns], NVS_READONLY, &h);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err != ESP_OK) return err;
        nvs_iterator_t it = NULL;
        err = nvs_entry_find_in_handle(h, NVS_TYPE_ANY, &it);
        while (err == ESP_OK) {
            nvs_entry_info_t info;
            err = nvs_entry_info(it, &info);
            uint8_t *value = NULL; size_t len = 0;
            if (err == ESP_OK) err = read_value(h, ns, &info, &value, &len);
            if (err == ESP_OK && !valid_record(ns, info.type, info.key, value, len)) err = ESP_ERR_INVALID_STATE;
            if (err == ESP_OK) err = write_record(&w, ns, info.type, info.key, value, len);
            free(value);
            if (err != ESP_OK) break;
            err = nvs_entry_next(&it);
        }
        nvs_release_iterator(it); nvs_close(h);
        if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    }
    esp_err_t err = book_bookmark_store_visit(write_card, &w, bad_slot, cap);
    if (err != ESP_OK) return err;
    uint8_t end[V2_HEADER] = {0xff}, seal[8];
    put32(seal, w.count); put32(seal + 4, w.hash);
    return fwrite(end, 1, sizeof(end), file) == sizeof(end) && fwrite(seal, 1, sizeof(seal), file) == sizeof(seal) ? ESP_OK : ESP_FAIL;
}

typedef enum { VALIDATE, STAGE_CARD, RESTORE_NVS } read_mode_t;
static esp_err_t read_stream(FILE *file, read_mode_t mode, restore_nvs_t *image) {
    char magic[8];
    if (!file || fread(magic, 1, 8, file) != 8) return ESP_ERR_INVALID_RESPONSE;
    bool v1 = !memcmp(magic, "RPHIST1", 8);
    if (!v1 && memcmp(magic, "RPHIST2", 8)) return ESP_ERR_INVALID_RESPONSE;
    uint32_t hash = UINT32_C(2166136261), count = 0;
    nvs_handle_t h = 0; int open_ns = -1;
    esp_err_t result = ESP_OK;
    size_t head_len = v1 ? HISTORY_RECORD_HEADER : V2_HEADER;
    for (;;) {
        uint8_t head[V2_HEADER] = {0};
        if (fread(head, 1, head_len, file) != head_len) { result = ESP_ERR_INVALID_RESPONSE; break; }
        unsigned ns = head[0];
        if (ns == 0xff) {
            for (unsigned i = 1; i < head_len; ++i) if (head[i]) result = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        uint8_t type = head[1];
        const char *key = (const char *)head + 2;
        size_t len = get32(head + 18);
        if (!memchr(key, 0, 16) || !len || ns > CARD_NS || count == UINT32_MAX ||
            (v1 && (ns == CARD_NS || len > HISTORY_MAX_VALUE || count == HISTORY_MAX_RECORDS)) ||
            (ns != 2 && ns != CARD_NS && len > HISTORY_MAX_VALUE) ||
            (ns == CARD_NS && (*key || type != NVS_TYPE_BLOB))) { result = ESP_ERR_INVALID_RESPONSE; break; }
        // 大记录先验证定长头，再分配整本书所需空间。/ Check a large record's header before allocating its entries.
        reader_bookmarks_unbounded_t header;
        size_t prefix = 0;
        if (ns == CARD_NS || len > HISTORY_MAX_VALUE) {
            prefix = sizeof(header);
            if (len < prefix || fread(&header, 1, prefix, file) != prefix || !reader_bookmarks_v3_valid(&header, len)) {
                result = ESP_ERR_INVALID_RESPONSE; break;
            }
        }
        if (ns == CARD_NS && mode == RESTORE_NVS) {
            // 预检或重放 NVS 时，卡记录只参与校验和，不分配整本书的缓冲区。
            // Card records contribute only to the seal during NVS rehearsal or replay; stream them without a book-sized allocation.
            if (strncmp(header.path, "/sdcard/", 8)) { result = ESP_ERR_INVALID_RESPONSE; break; }
            hash = hash_bytes(hash_bytes(hash, head, head_len), &header, prefix);
            uint8_t chunk[512];
            for (size_t left = len - prefix; left;) {
                size_t n = left < sizeof(chunk) ? left : sizeof(chunk);
                if (fread(chunk, 1, n, file) != n) { result = ESP_ERR_INVALID_RESPONSE; break; }
                hash = hash_bytes(hash, chunk, n); left -= n;
            }
            if (result != ESP_OK) break;
            ++count; continue;
        }
        uint8_t *value = malloc(len);
        if (!value) { result = ESP_ERR_NO_MEM; break; }
        if (prefix) memcpy(value, &header, prefix);
        if (fread(value + prefix, 1, len - prefix, file) != len - prefix ||
            (ns == CARD_NS ? strncmp(header.path, "/sdcard/", 8) != 0 : !valid_record(ns, type, key, value, len))) {
            free(value); result = ESP_ERR_INVALID_RESPONSE; break;
        }
        hash = hash_bytes(hash_bytes(hash, head, head_len), value, len); ++count;
        if (ns == CARD_NS && mode == STAGE_CARD) result = book_bookmark_restore_put((void *)value);
        if (ns < CARD_NS && mode == RESTORE_NVS && image) {
            result = restore_nvs_write(image, namespaces[ns], key, (nvs_type_t)type, value, len);
        } else if (ns < CARD_NS && mode == RESTORE_NVS) {
            if (open_ns != (int)ns) {
                if (open_ns >= 0) { result = nvs_commit(h); nvs_close(h); }
                open_ns = -1;
                if (result == ESP_OK) result = nvs_open(namespaces[ns], NVS_READWRITE, &h);
                if (result == ESP_OK) open_ns = (int)ns;
            }
            if (result == ESP_OK) {
                if (type == NVS_TYPE_U8) result = nvs_set_u8(h, key, value[0]);
                else if (type == NVS_TYPE_U32) { uint32_t v; memcpy(&v, value, 4); result = nvs_set_u32(h, key, v); }
                else if (type == NVS_TYPE_STR) result = nvs_set_str(h, key, (const char *)value);
                else result = nvs_set_blob(h, key, value, len);
            }
        }
        free(value);
        if (result != ESP_OK) break;
    }
    if (open_ns >= 0) {
        if (result == ESP_OK) result = nvs_commit(h);
        nvs_close(h); book_progress_notify_changed();
    }
    if (result != ESP_OK) return result;
    uint8_t seal[8]; size_t count_bytes = v1 ? 2 : 4;
    if (fread(seal, 1, count_bytes + 4, file) != count_bytes + 4) return ESP_ERR_INVALID_RESPONSE;
    uint32_t stored_count = v1 ? ((uint32_t)seal[0] | (uint32_t)seal[1] << 8) : get32(seal);
    return count == stored_count && hash == get32(seal + count_bytes) && fgetc(file) == EOF && !ferror(file) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

bool book_history_backup_validate(FILE *file) { return read_stream(file, VALIDATE, NULL) == ESP_OK; }
esp_err_t book_history_backup_restore(FILE *file, restore_nvs_t *image) {
    if (!file) return ESP_ERR_INVALID_ARG;
    if (!image) {
        long start = ftell(file);
        if (start < 0 || !book_history_backup_validate(file) || fseek(file, start, SEEK_SET))
            return ESP_ERR_INVALID_RESPONSE;
        esp_err_t card = book_bookmark_restore_begin();
        if (card == ESP_OK) card = read_stream(file, STAGE_CARD, NULL);
        if (card != ESP_OK) return card;
        if (fseek(file, start, SEEK_SET)) return ESP_FAIL;
        // TF 发布成功后才替换旧进度。/ Replace old progress only after card promotion succeeds.
        card = book_bookmark_restore_finish();
        if (card != ESP_OK) return card;
    }
    // Recovery replaces the old reading library. Merging leaves stale "last"
    // and per-book records behind, so the restored shelf can appear unchanged.
    // 恢复应替换旧阅读资料；合并会留下备份外的旧书进度与最近阅读排序。
    for (unsigned ns = 0; ns < sizeof(namespaces) / sizeof(namespaces[0]); ++ns) {
        if (image) {
            esp_err_t err = restore_nvs_erase(image, namespaces[ns], NULL);
            if (err != ESP_OK) return err;
            continue;
        }
        nvs_handle_t h;
        esp_err_t err = nvs_open(namespaces[ns], NVS_READONLY, &h);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err != ESP_OK) return err;
        nvs_close(h);
        err = nvs_open(namespaces[ns], NVS_READWRITE, &h);
        if (err != ESP_OK) return err;
        err = nvs_erase_all(h);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
        book_progress_notify_changed();
        if (err != ESP_OK) return err;
    }
    return read_stream(file, RESTORE_NVS, image);
}
