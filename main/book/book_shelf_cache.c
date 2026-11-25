/*
 * 中文：书架稳定条目的持久化缓存；只缓存显示字段，不缓存进度、收藏或隐藏状态。
 * English: Persistent cache for stable shelf fields; progress, favorites and hidden state stay live.
 */
#include "book_shelf_scan.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"

#define BOOK_SHELF_CACHE_MAGIC UINT32_C(0x53434631) /* SCF1 */
#define BOOK_SHELF_CACHE_VERSION UINT16_C(2)
#define BOOK_SHELF_CACHE_MAX_RECORDS UINT32_C(2048)
#ifndef BOOK_SHELF_CACHE_DIR
#define BOOK_SHELF_CACHE_DIR "/flash"
#endif

typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char name[256];
    char author[128];
    uint32_t size;
    int64_t modified;
} book_shelf_cache_record_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t count;
    uint64_t checksum;
} book_shelf_cache_header_t;

struct book_shelf_cache {
    char path[160];
    book_shelf_cache_record_t *records;
    uint32_t count;
};

static uint64_t cache_hash_update(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static uint64_t cache_hash(const void *data, size_t size) {
    return cache_hash_update(UINT64_C(1469598103934665603), data, size);
}

static bool cache_record_strings_valid(const book_shelf_cache_record_t *record) {
    return memchr(record->path, 0, sizeof(record->path)) &&
        memchr(record->name, 0, sizeof(record->name)) &&
        memchr(record->author, 0, sizeof(record->author));
}

static void cache_path(char *out, size_t cap, const char *identity) {
    char safe[41] = {0};
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)(identity && *identity ? identity : "builtin");
         *p && used + 1 < sizeof(safe); ++p)
        safe[used++] = isalnum(*p) || *p == '-' || *p == '_' ? (char)*p : '_';
    safe[used] = 0;
    snprintf(out, cap, BOOK_SHELF_CACHE_DIR "/.mogo-shelf-catalog-%s.bin", safe);
}

book_shelf_cache_t *book_shelf_cache_open(const char *identity) {
    book_shelf_cache_t *cache = calloc(1, sizeof(*cache));
    if (!cache) return NULL;
    cache_path(cache->path, sizeof(cache->path), identity);
    FILE *file = fopen(cache->path, "rb");
    if (!file) return cache;
    book_shelf_cache_header_t header;
    bool valid = fread(&header, 1, sizeof(header), file) == sizeof(header) &&
        header.magic == BOOK_SHELF_CACHE_MAGIC && header.version == BOOK_SHELF_CACHE_VERSION &&
        header.record_size == sizeof(book_shelf_cache_record_t) &&
        header.count <= BOOK_SHELF_CACHE_MAX_RECORDS;
    if (valid) {
        if (fseek(file, 0, SEEK_END) != 0) valid = false;
        long length = valid ? ftell(file) : -1;
        if (length < 0 || (uint64_t)length != sizeof(header) +
            (uint64_t)header.count * sizeof(book_shelf_cache_record_t)) valid = false;
        if (valid && header.count) {
            cache->records = heap_caps_calloc(header.count, sizeof(*cache->records),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!cache->records || fseek(file, (long)sizeof(header), SEEK_SET) != 0 ||
                fread(cache->records, sizeof(*cache->records), header.count, file) != header.count)
                valid = false;
        }
        if (valid) {
            uint64_t hash = header.count ? cache_hash(cache->records,
                (size_t)header.count * sizeof(*cache->records)) : cache_hash(NULL, 0);
            for (uint32_t i = 0; i < header.count && valid; ++i)
                valid = cache_record_strings_valid(&cache->records[i]);
            valid = valid && hash == header.checksum;
            if (valid) cache->count = header.count;
        }
    }
    fclose(file);
    if (!valid) {
        free(cache->records);
        cache->records = NULL;
        cache->count = 0;
    }
    return cache;
}

bool book_shelf_cache_lookup(const book_shelf_cache_t *cache, const char *path, uint32_t size,
                             int64_t modified, char *name, size_t name_cap,
                             char *author, size_t author_cap) {
    if (!cache || !path) return false;
    for (uint32_t i = 0; i < cache->count; ++i) {
        const book_shelf_cache_record_t *record = &cache->records[i];
        if (record->size != size || record->modified != modified || strcmp(record->path, path)) continue;
        if (name && name_cap) snprintf(name, name_cap, "%s", record->name);
        if (author && author_cap) snprintf(author, author_cap, "%s", record->author);
        return true;
    }
    return false;
}

bool book_shelf_cache_save(book_shelf_cache_t *cache, const book_shelf_entry_t *entries, int count) {
    if (!cache || count < 0 || (count && !entries)) return false;
    char temporary[sizeof(cache->path) + 5];
    int written_path = snprintf(temporary, sizeof(temporary), "%s.tmp", cache->path);
    if (written_path < 0 || (size_t)written_path >= sizeof(temporary)) return false;
    FILE *file = fopen(temporary, "wb");
    if (!file) return false;
    book_shelf_cache_header_t header = {
        .magic = BOOK_SHELF_CACHE_MAGIC, .version = BOOK_SHELF_CACHE_VERSION,
        .record_size = sizeof(book_shelf_cache_record_t), .count = 0, .checksum = cache_hash(NULL, 0),
    };
    bool ok = fwrite(&header, 1, sizeof(header), file) == sizeof(header);
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int i = 0; ok && i < count && header.count < BOOK_SHELF_CACHE_MAX_RECORDS; ++i) {
        struct stat st;
        if (stat(entries[i].path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
            (uint64_t)st.st_size > UINT32_MAX) continue;
        book_shelf_cache_record_t record = {0};
        snprintf(record.path, sizeof(record.path), "%s", entries[i].path);
        snprintf(record.name, sizeof(record.name), "%s", entries[i].name);
        snprintf(record.author, sizeof(record.author), "%s", entries[i].author);
        record.size = (uint32_t)st.st_size;
        record.modified = (int64_t)st.st_mtime;
        ok = fwrite(&record, 1, sizeof(record), file) == sizeof(record);
        if (ok) {
            hash = cache_hash_update(hash, &record, sizeof(record));
            ++header.count;
        }
    }
    if (ok) {
        header.checksum = hash;
        ok = fseek(file, 0, SEEK_SET) == 0 && fwrite(&header, 1, sizeof(header), file) == sizeof(header);
    }
    ok = ok && fflush(file) == 0 && fclose(file) == 0;
    if (!ok) {
        remove(temporary);
        return false;
    }
    /* FatFs may reject rename when the destination already exists. Keep the
     * first atomic attempt, then replace the old cache explicitly. */
    if (rename(temporary, cache->path) != 0) {
        if (remove(cache->path) != 0 || rename(temporary, cache->path) != 0) {
            remove(temporary);
            return false;
        }
    }
    return true;
}

void book_shelf_cache_close(book_shelf_cache_t *cache) {
    if (!cache) return;
    free(cache->records);
    free(cache);
}
