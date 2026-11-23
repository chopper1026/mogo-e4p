/* 中文：阅读书签的持久化布局，供阅读页和路径迁移共用。
 * English: Persisted bookmark layouts shared by the reader and path relocation. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "book_path.h"

// v2 的固定数组只用于读取旧记录；新记录按实际数量写入可变长度数组。
// The fixed v2 array is read-only compatibility data; new records use a variable-length array.
#define BOOKMARK_LEGACY_MAX 24

typedef struct {
    uint32_t magic, file_size, byte_off;
    uint16_t chapter;
    char path[BOOK_STORE_PATH_MAX];
} legacy_reader_bookmark_t;
typedef struct {
    uint16_t chapter, reserved;
    uint32_t byte_off, saved_s;
} reader_bookmark_entry_t;
typedef struct {
    uint32_t magic, file_size;
    uint16_t count, reserved;
    char path[BOOK_STORE_PATH_MAX];
    reader_bookmark_entry_t entries[BOOKMARK_LEGACY_MAX];
} reader_bookmarks_t;
/// Current unbounded bookmark record; entries contains exactly count items in serialized storage.
/// 当前无上限书签记录；持久化时 entries 恰好包含 count 项。
typedef struct {
    uint32_t magic, file_size, count, reserved;
    char path[BOOK_STORE_PATH_MAX];
    reader_bookmark_entry_t entries[];
} reader_bookmarks_unbounded_t;

static inline size_t reader_bookmarks_unbounded_header_size(void) {
    return offsetof(reader_bookmarks_unbounded_t, entries);
}
static inline bool reader_bookmarks_unbounded_size(uint32_t count, size_t *out) {
    const size_t header = reader_bookmarks_unbounded_header_size();
    if (!out || (size_t)count > (SIZE_MAX - header) / sizeof(reader_bookmark_entry_t)) return false;
    *out = header + (size_t)count * sizeof(reader_bookmark_entry_t);
    return true;
}

// 先校验固定头，再读取数量与路径；调用方仍须校验归属。/ Validate the fixed header before using count or path; callers check ownership.
static inline bool reader_bookmarks_v3_valid(const reader_bookmarks_unbounded_t *m, size_t len) {
    size_t expected;
    return m && len >= reader_bookmarks_unbounded_header_size() && m->magic == UINT32_C(0x52504d33) &&
        reader_bookmarks_unbounded_size(m->count, &expected) && expected == len &&
        m->path[0] == '/' && memchr(m->path, 0, sizeof(m->path));
}

/// 验证三代记录并返回其路径；记录须由对齐的存储提供。/ Validate all three layouts and return the owner path; storage must be aligned.
static inline const char *reader_bookmarks_owner(const void *raw, size_t len) {
    if (reader_bookmarks_v3_valid(raw, len)) return ((const reader_bookmarks_unbounded_t *)raw)->path;
    const char *path = NULL;
    if (len == sizeof(reader_bookmarks_t)) {
        const reader_bookmarks_t *m = raw;
        if (m->magic == UINT32_C(0x52504d32) && m->count <= BOOKMARK_LEGACY_MAX) path = m->path;
    } else if (len == sizeof(legacy_reader_bookmark_t)) {
        const legacy_reader_bookmark_t *m = raw;
        if (m->magic == UINT32_C(0x52504d31)) path = m->path;
    }
    return path && path[0] == '/' && memchr(path, 0, BOOK_STORE_PATH_MAX) ? path : NULL;
}
