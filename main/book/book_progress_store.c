// 每桶独立校验、写临时副本后发布；纯读取不修复、不创建目录。/ Sealed buckets with checked temporary publication; reads never repair or create files.
#include "book_progress_store.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PROGRESS_PARENT
#define PROGRESS_PARENT "/flash"
#endif
#define LIVE PROGRESS_PARENT "/progress"
#define OLD PROGRESS_PARENT "/prg-old"
#define STAGE PROGRESS_PARENT "/prg-new"
#define BUCKETS 32
#define BUCKET_LIMIT (128u * 1024u)
static pthread_mutex_t store_lock = PTHREAD_MUTEX_INITIALIZER;
typedef char filename_t[sizeof(STAGE) + 12];
static uint32_t digest(const void* data, size_t n) {
    const uint8_t* p = data;
    uint32_t h = UINT32_C(2166136261);
    while (n--) h = (h ^ *p++) * UINT32_C(16777619);
    return h;
}
void progress_key(const char* path, char key[11]) {
    snprintf(key, 11, "b_%08lx", (unsigned long)digest(path, strlen(path)));
}
static unsigned bucket(const char* path) {
    return digest(path, strlen(path)) % BUCKETS;
}
static size_t row_len(const uint8_t* row) {
    return PROGRESS_HEADER + row[22] + ((size_t)row[23] << 8);
}
bool progress_record_valid(const char* key, const uint8_t* data, size_t len) {
    if (!key || !data || len <= PROGRESS_HEADER + 1 || len > PROGRESS_CAP ||
        memcmp(data, "RPB", 3) || (data[3] != 1 && data[3] != 2) ||
        (data[3] == 1 && len > PROGRESS_HEADER + 160) || row_len(data) != len)
        return false;
    const char* owner = (const char*)data + PROGRESS_HEADER;
    if (strnlen(owner, len - PROGRESS_HEADER) != len - PROGRESS_HEADER - 1) return false;
    char expected[11];
    progress_key(owner, expected);
    return !strcmp(expected, key);
}
static esp_err_t directory(const char* dir) {
    struct stat st;
    if (stat(dir, &st)) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static esp_err_t active(const char** root, bool writing) {
    *root = LIVE;
    esp_err_t e = directory(LIVE);
    if (e != ESP_ERR_NOT_FOUND) return e;
    e = directory(OLD);
    if (e == ESP_OK) {
        if (!writing)
            *root = OLD;
        else if (rename(OLD, LIVE))
            return ESP_FAIL;
    } else if (e == ESP_ERR_NOT_FOUND && writing)
        return mkdir(LIVE, 0777) ? ESP_FAIL : ESP_OK;
    return e;
}
static void name(filename_t out, const char* root, unsigned id, const char* ext) {
    snprintf(out, sizeof(filename_t), "%s/%02x.%s", root, id, ext);
}
// 文件头含格式及小端校验，覆盖整个负载，包括记录顺序。/ The header seals the entire ordered payload with a little-endian checksum.
typedef struct {
    uint8_t* data;
    size_t len;
} block_t;
static esp_err_t read_file(const char* file, unsigned id, block_t* b) {
    FILE* f = fopen(file, "rb");
    if (!f) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    long end;
    uint8_t head[8];
    esp_err_t e = ESP_ERR_INVALID_STATE;
    if (!fseek(f, 0, SEEK_END) && (end = ftell(f)) >= 8 && end <= (long)BUCKET_LIMIT + 8 &&
        !fseek(f, 0, SEEK_SET) && fread(head, 1, 8, f) == 8 && !memcmp(head, "PRG1", 4)) {
        b->len = (size_t)end - 8;
        b->data = malloc(b->len ? b->len : 1);
        if (!b->data)
            e = ESP_ERR_NO_MEM;
        else if (fread(b->data, 1, b->len, f) == b->len) {
            uint32_t h = digest(b->data, b->len);
            if (head[4] == (uint8_t)h && head[5] == (uint8_t)(h >> 8) &&
                head[6] == (uint8_t)(h >> 16) && head[7] == (uint8_t)(h >> 24))
                e = ESP_OK;
            for (size_t i = 0; e == ESP_OK && i < b->len;) {
                const uint8_t* r = b->data + i;
                if (b->len - i < PROGRESS_HEADER || row_len(r) > b->len - i ||
                    row_len(r) > PROGRESS_CAP || row_len(r) <= PROGRESS_HEADER ||
                    r[row_len(r) - 1]) {
                    e = ESP_ERR_INVALID_STATE;
                    break;
                }
                const char* p = (const char*)r + PROGRESS_HEADER;
                char key[11];
                progress_key(p, key);
                if (!progress_record_valid(key, r, row_len(r)) || bucket(p) != id)
                    e = ESP_ERR_INVALID_STATE;
                i += row_len(r);
            }
        }
    }
    if (ferror(f)) e = ESP_FAIL;
    if (fclose(f)) e = ESP_FAIL;
    if (e != ESP_OK) {
        free(b->data);
        *b = (block_t){ 0 };
    }
    return e;
}
static esp_err_t load(const char* root, unsigned id, block_t* b, bool* rollback) {
    filename_t file;
    name(file, root, id, "dat");
    esp_err_t e = read_file(file, id, b);
    *rollback = e == ESP_ERR_NOT_FOUND;
    if (*rollback) {
        name(file, root, id, "bak");
        e = read_file(file, id, b);
    }
    return e;
}
static esp_err_t locate(block_t* b, const char* path, size_t* pos) {
    uint32_t h = digest(path, strlen(path));
    for (*pos = 0; *pos < b->len; *pos += row_len(b->data + *pos)) {
        const char* owner = (const char*)b->data + *pos + PROGRESS_HEADER;
        if (digest(owner, strlen(owner)) == h)
            return !strcmp(owner, path) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    return ESP_ERR_NOT_FOUND;
}
static esp_err_t get_locked(const char* path, uint8_t out[PROGRESS_CAP], size_t* len) {
    const char* root;
    esp_err_t e = active(&root, false);
    block_t b = { 0 };
    bool bak;
    size_t pos;
    if (e == ESP_OK) e = load(root, bucket(path), &b, &bak);
    if (e == ESP_OK) e = locate(&b, path, &pos);
    if (e == ESP_OK) {
        *len = row_len(b.data + pos);
        memcpy(out, b.data + pos, *len);
    }
    free(b.data);
    return e;
}
esp_err_t progress_store_get(const char* path, uint8_t out[PROGRESS_CAP], size_t* len) {
    pthread_mutex_lock(&store_lock);
    esp_err_t e = get_locked(path, out, len);
    pthread_mutex_unlock(&store_lock);
    return e;
}
static esp_err_t discard(const char* path) {
    return !unlink(path) || errno == ENOENT ? ESP_OK : ESP_FAIL;
}
static esp_err_t update(
    const char* root, const char* path, const uint8_t* data, size_t len, bool insert_only) {
    if (data) {
        char key[11];
        progress_key(path, key);
        if (!progress_record_valid(key, data, len) ||
            strcmp(path, (const char*)data + PROGRESS_HEADER))
            return ESP_ERR_INVALID_ARG;
    } else if (len)
        return ESP_ERR_INVALID_ARG;
    unsigned id = bucket(path);
    block_t b = { 0 };
    bool rollback;
    esp_err_t e = load(root, id, &b, &rollback);
    bool had = e == ESP_OK;
    if (!had && e != ESP_ERR_NOT_FOUND) return e;
    size_t pos;
    e = locate(&b, path, &pos);
    if (e != ESP_OK && e != ESP_ERR_NOT_FOUND) goto done;
    if (e == ESP_OK && insert_only) {
        e = ESP_ERR_INVALID_STATE;
        goto done;
    }
    size_t prior = e == ESP_OK ? row_len(b.data + pos) : 0;
    e = ESP_OK;
    if ((!data && !prior) || (prior && len == prior && !memcmp(data, b.data + pos, len))) goto done;
    size_t total = b.len - prior + len;
    if (total > BUCKET_LIMIT) {
        e = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    if (len > prior) {
        uint8_t* p = realloc(b.data, total);
        if (!p) {
            e = ESP_ERR_NO_MEM;
            goto done;
        }
        b.data = p;
    }
    memmove(b.data + pos + len, b.data + pos + prior, b.len - pos - prior);
    if (len) memcpy(b.data + pos, data, len);
    filename_t file, bak, tmp;
    name(file, root, id, "dat");
    name(bak, root, id, "bak");
    name(tmp, root, id, "tmp");
    if (had && rollback && rename(bak, file)) {
        e = ESP_FAIL;
        goto done;
    }
    if ((e = discard(bak)) != ESP_OK) goto done;
    if (!total) {
        e = discard(file);
        goto done;
    }
    uint32_t h = digest(b.data, total);
    uint8_t head[8] = { 'P', 'R', 'G', '1', h, h >> 8, h >> 16, h >> 24 };
    FILE* f = fopen(tmp, "wb");
    if (!f) {
        e = ESP_FAIL;
        goto done;
    }
    bool ok = fwrite(head, 1, 8, f) == 8 && fwrite(b.data, 1, total, f) == total && !fflush(f) &&
              !fsync(fileno(f));
    if (fclose(f) || !ok) {
        e = ESP_FAIL;
        goto done;
    }
    block_t check = { 0 };
    e = read_file(tmp, id, &check);
    if (e == ESP_OK && (check.len != total || memcmp(check.data, b.data, total)))
        e = ESP_ERR_INVALID_STATE;
    free(check.data);
    if (e == ESP_OK && had && rename(file, bak)) e = ESP_FAIL;
    if (e == ESP_OK && rename(tmp, file)) e = ESP_FAIL;
    if (e == ESP_OK && had) (void)discard(bak);
done:
    free(b.data);
    return e;
}
esp_err_t progress_store_put(const char* path, const uint8_t* data, size_t len) {
    pthread_mutex_lock(&store_lock);
    const char* root;
    esp_err_t e = active(&root, data != NULL);
    if (e == ESP_OK)
        e = update(root, path, data, len, false);
    else if (e == ESP_ERR_NOT_FOUND && !data)
        e = ESP_OK;
    pthread_mutex_unlock(&store_lock);
    return e;
}
esp_err_t progress_store_visit(progress_store_visitor fn, void* ctx, bool skip_damaged) {
    esp_err_t e = ESP_OK;
    for (unsigned id = 0; e == ESP_OK && id < BUCKETS; ++id) {
        block_t b = { 0 };
        bool bak;
        const char* root;
        pthread_mutex_lock(&store_lock);
        e = active(&root, false);
        bool empty = e == ESP_ERR_NOT_FOUND;
        if (e == ESP_OK) e = load(root, id, &b, &bak);
        // 回调可能再次查询同本书，因此只锁住本桶的读取。/ Visitors may look up the same book; lock only while loading the bucket.
        pthread_mutex_unlock(&store_lock);
        if (empty) return ESP_OK;
        if (e == ESP_ERR_NOT_FOUND || (skip_damaged && e == ESP_ERR_INVALID_STATE)) {
            e = ESP_OK;
            continue;
        }
        for (size_t i = 0; e == ESP_OK && i < b.len; i += row_len(b.data + i)) {
            char key[11];
            progress_key((char*)b.data + i + PROGRESS_HEADER, key);
            e = fn(key, b.data + i, row_len(b.data + i), ctx);
        }
        free(b.data);
    }
    return e;
}
// 仅清理专属目录内的桶；遇未知内容时拒绝删除目录。/ Remove only owned bucket names; unknown contents prevent directory removal.
static esp_err_t clean(const char* root) {
    esp_err_t e = directory(root);
    if (e == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (e != ESP_OK) return e;
    const char* ext[] = { "dat", "tmp", "bak" };
    for (unsigned i = 0; i < BUCKETS; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            filename_t file;
            name(file, root, i, ext[j]);
            if (discard(file) != ESP_OK) return ESP_FAIL;
        }
    return rmdir(root) ? ESP_FAIL : ESP_OK;
}
esp_err_t progress_stage_begin(void) {
    pthread_mutex_lock(&store_lock);
    const char* root;
    esp_err_t e = active(&root, true);
    if (e == ESP_OK) e = clean(STAGE);
    if (e == ESP_OK && mkdir(STAGE, 0777)) e = ESP_FAIL;
    pthread_mutex_unlock(&store_lock);
    return e;
}
esp_err_t progress_stage_put(const uint8_t* data, size_t len) {
    // 调用者已用备份键校验该记录；暂存目录不供其他线程读取。/ The backup key was checked by the caller; staging is private to restore.
    return update(STAGE, (const char*)data + PROGRESS_HEADER, data, len, true);
}
void progress_stage_discard(void) {
    (void)clean(STAGE);
}
esp_err_t progress_stage_finish(void) {
    pthread_mutex_lock(&store_lock);
    const char* root;
    esp_err_t e = directory(STAGE);
    if (e == ESP_OK) e = active(&root, true);
    if (e == ESP_OK) e = clean(OLD);
    if (e == ESP_OK && (rename(LIVE, OLD) || rename(STAGE, LIVE))) e = ESP_FAIL;
    if (e == ESP_OK) (void)clean(OLD);
    pthread_mutex_unlock(&store_lock);
    return e;
}
