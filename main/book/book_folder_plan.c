/* 中文：先检查整批目标，再调用同卷日志事务；不从阅读历史猜测来源。
 * English: Check every destination before journaled same-volume operations; never infer sources from reading history. */
#include "book_folder_plan.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_file_path.h"
#ifdef ESP_PLATFORM
#include "board_sd.h"
#include "esp_heap_caps.h"
#include "ff.h"
#endif

#define CAP BOOK_STORE_PATH_MAX
#define LIMIT (BOOK_COLLECTION_BOOKS + BOOK_COLLECTION_CATEGORIES + 1)
static bool join(const char* dir, const char* name, char out[CAP]) {
    int n = snprintf(out, CAP, "%s/%s", dir, name);
    return n > 0 && n < CAP;
}
static bool under(const char* dir, const char* path) {
    size_t n = strlen(dir);
    return n && !strncmp(dir, path, n) && path[n] == '/' && path[n + 1];
}
static bool direct(const char* root, const char* dir) {
    return under(root, dir) && dir[strlen(root) + 1] != '.' && !strchr(dir + strlen(root) + 1, '/');
}
#ifdef ESP_PLATFORM
static unsigned folded(const unsigned char** p) {
    unsigned c = *(*p)++;
    if (c < 128) return ff_wtoupper(c);
    unsigned n = c < 224 ? 1 : c < 240 ? 2 : 3;
    c &= n == 1 ? 31 : n == 2 ? 15 : 7;
    while (n-- && (**p & 192) == 128) c = (c << 6) | (*(*p)++ & 63);
    return ff_wtoupper(c);
}
#endif
static uint32_t target_hash(const char* name) {
    const unsigned char* p = (const unsigned char*)name;
    uint32_t h = UINT32_C(2166136261);
    while (*p) {
#ifdef ESP_PLATFORM
        unsigned c = folded(&p);
#else
        unsigned c = (unsigned)toupper(*p++);
#endif
        h = (h ^ c) * UINT32_C(16777619);
    }
    return h;
}
static bool same_name(const char* a, const char* b) {
#ifdef ESP_PLATFORM
    const unsigned char *x = (const unsigned char*)a, *y = (const unsigned char*)b;
    while (*x && *y)
        if (folded(&x) != folded(&y)) return false;
    return !*x && !*y;
#else
    return !strcasecmp(a, b);
#endif
}
void book_folder_clear(book_folder_plan_t* p) {
    free(p->items);
    memset(p, 0, sizeof(*p));
}
void book_folder_begin(book_folder_plan_t* p, const char* root, book_path_state_extra_fn check) {
    book_folder_clear(p);
    p->check = check;
    if (!root || !*root || strlen(root) >= CAP)
        p->error = ESP_ERR_INVALID_ARG;
    else
        strcpy(p->root, root);
}
static book_folder_item_t* append(
    book_folder_plan_t* p, const char* from, const char* to, book_folder_action_t action) {
    if (p->count >= LIMIT || strlen(from) >= CAP || strlen(to) >= CAP) {
        p->error = ESP_ERR_INVALID_SIZE;
        return NULL;
    }
    if (p->count == p->capacity) {
        size_t n = p->capacity ? p->capacity + p->capacity / 2 : 8;
        if (n > LIMIT) n = LIMIT;
#ifdef ESP_PLATFORM
        void* buf =
            heap_caps_realloc(p->items, n * sizeof(*p->items), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        void* buf = realloc(p->items, n * sizeof(*p->items));
#endif
        if (!buf) {
            p->error = ESP_ERR_NO_MEM;
            return NULL;
        }
        p->items = buf;
        p->capacity = n;
    }
    book_folder_item_t* r = &p->items[p->count++];
    memset(r, 0, sizeof(*r));
    r->action = action;
    strcpy(r->from, from);
    strcpy(r->to, to);
    r->target_hash = target_hash(to);
    return r;
}
static void reject(book_folder_plan_t* p, book_folder_item_t* r, esp_err_t err) {
    r->action = FOLDER_CONFLICT;
    r->error = err;
    if (!p->error) p->error = err;
}
void book_folder_directory(book_folder_plan_t* p, const char* name, bool reuse) {
    char target[CAP];
    bool fits = join(p->root, name, target);
    book_folder_item_t* r = append(p, "", fits ? target : name, FOLDER_CREATE);
    if (!r) return;
    if (!fits || !direct(p->root, target) || !book_category_name_valid(name)) {
        reject(p, r, ESP_ERR_INVALID_ARG);
        return;
    }
    for (size_t i = 0; i + 1 < p->count; ++i)
        if (p->items[i].target_hash == r->target_hash && same_name(p->items[i].to, target)) {
            reject(p, r, ESP_ERR_INVALID_STATE);
            return;
        }
    struct stat st;
    if (!stat(target, &st)) {
        if (!reuse || !S_ISDIR(st.st_mode)) {
            reject(p, r, ESP_ERR_INVALID_STATE);
            return;
        }
#ifdef ESP_PLATFORM
        bool hidden = false;
        if (board_sd_path_hidden(target, &hidden) != ESP_OK || hidden) {
            reject(p, r, ESP_ERR_NOT_SUPPORTED);
            return;
        }
#endif
        // FAT 可用大小写别名查到不同拼写；元数据只接受卡上的确切路径。
        // Reject case aliases: reading records require the spelling actually present on FAT.
        DIR* d = opendir(p->root);
        bool exact = false;
        struct dirent* e;
        if (d) {
            while ((e = readdir(d)))
                if (!strcmp(e->d_name, name)) {
                    exact = true;
                    break;
                }
            closedir(d);
        }
        if (exact)
            r->action = FOLDER_REUSE;
        else
            reject(p, r, ESP_ERR_INVALID_STATE);
    } else if (errno != ENOENT)
        reject(p, r, ESP_FAIL);
}
// FAT 也识别不同大小写和短别名；旧归属只匹配卡上逐段拼写一致的原路径。
// FAT also resolves case variants and short aliases. Legacy metadata belongs to the exact recorded path.
static esp_err_t legacy_source(const char* root, const char* source) {
    char dir[CAP];
    strcpy(dir, root);
    const char* part = source + strlen(root) + 1;
    while (*part) {
        const char* end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        DIR* d = opendir(dir);
        if (!d) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        bool matched = false;
        struct dirent* e;
        errno = 0;
        while ((e = readdir(d)))
            if (strlen(e->d_name) == n && !memcmp(part, e->d_name, n)) {
                matched = true;
                break;
            }
        int read_error = errno;
        if (closedir(d) || (!matched && read_error)) return ESP_FAIL;
        if (!matched) return ESP_ERR_NOT_FOUND;
        if (!end) break;
        memcpy(dir, source, (size_t)(end - source));
        dir[end - source] = 0;
#ifdef ESP_PLATFORM
        bool hidden = false;
        if (board_sd_path_hidden(dir, &hidden) != ESP_OK) return ESP_FAIL;
        if (hidden) return ESP_ERR_NOT_FOUND;
#endif
        part = end + 1;
    }
    return ESP_OK;
}
void book_folder_add_move(
    book_folder_plan_t* p, const char* source, const char* directory, bool migration) {
    char target[CAP];
    const char* base = strrchr(source, '/');
    bool fits = base && join(directory, base + 1, target);
    book_folder_item_t* r = append(p, source, fits ? target : directory, FOLDER_MOVE);
    if (!r) return;
    if (!under(p->root, source) || strpbrk(source, "\\:") || strstr(source, "/.") ||
        strstr(source, "//") || !book_file_path_supported(source)) {
        if (migration)
            r->action = FOLDER_SKIP;
        else
            reject(p, r, ESP_ERR_NOT_SUPPORTED);
        return;
    }
    if (!fits || (strcmp(p->root, directory) && !direct(p->root, directory))) {
        reject(p, r, ESP_ERR_INVALID_ARG);
        return;
    }
    struct stat st;
    if (stat(source, &st)) {
        if (migration && errno == ENOENT)
            r->action = FOLDER_SKIP;
        else
            reject(p, r, ESP_ERR_NOT_FOUND);
        return;
    }
    if (migration) {
        esp_err_t err = legacy_source(p->root, source);
        if (err == ESP_ERR_NOT_FOUND) {
            r->action = FOLDER_SKIP;
            return;
        }
        if (err != ESP_OK) {
            reject(p, r, err);
            return;
        }
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) {
        reject(p, r, ESP_ERR_INVALID_SIZE);
        return;
    }
#ifdef ESP_PLATFORM
    bool hidden = false;
    if (board_sd_path_hidden(source, &hidden) != ESP_OK) {
        reject(p, r, ESP_FAIL);
        return;
    }
    if (hidden) {
        if (migration)
            r->action = FOLDER_SKIP;
        else
            reject(p, r, ESP_ERR_NOT_SUPPORTED);
        return;
    }
#endif
    r->size = (uint32_t)st.st_size;
    r->modified = st.st_mtime;
    if (!strcmp(source, target) || (strcmp(directory, p->root) && under(directory, source))) {
        r->action = FOLDER_KEEP;
        strcpy(r->to, source);
        r->target_hash = target_hash(source);
        return;
    }
    bool planned = false;
    for (size_t i = 0; i + 1 < p->count; ++i) {
        const book_folder_item_t* o = &p->items[i];
        if (o->action == FOLDER_CREATE && !strcmp(o->to, directory)) planned = true;
        if (o->action != FOLDER_SKIP && o->target_hash == r->target_hash &&
            same_name(o->to, target)) {
            reject(p, r, ESP_ERR_INVALID_STATE);
            return;
        }
    }
    if (!planned && (stat(directory, &st) || !S_ISDIR(st.st_mode))) {
        reject(p, r, ESP_ERR_NOT_FOUND);
        return;
    }
    if (!stat(target, &st) || errno != ENOENT) {
        reject(p, r, ESP_ERR_INVALID_STATE);
        return;
    }
    esp_err_t err = book_path_state_prepare(source, target);
    if (err == ESP_OK && p->check) err = p->check(source, target, r->size);
    if (err != ESP_OK) {
        reject(p, r, err);
        return;
    }
    ++p->moves;
}
void book_folder_delete(book_folder_plan_t* p, const char* source) {
    DIR* dir = direct(p->root, source) ? opendir(source) : NULL;
    if (!dir) {
        p->error = ESP_ERR_NOT_FOUND;
        return;
    }
#ifdef ESP_PLATFORM
    bool hidden = false;
    if (board_sd_path_hidden(source, &hidden) != ESP_OK || hidden) {
        closedir(dir);
        p->error = ESP_ERR_NOT_SUPPORTED;
        return;
    }
#endif
    for (;;) {
        errno = 0;
        struct dirent* e = readdir(dir);
        if (!e) {
            if (errno) p->error = ESP_FAIL;
            break;
        }
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[CAP];
        if (!join(source, e->d_name, path)) {
            p->error = ESP_ERR_INVALID_SIZE;
            break;
        }
        book_folder_add_move(p, path, p->root, false);
        if (p->error == ESP_ERR_NO_MEM || p->count >= LIMIT) break;
    }
    if (closedir(dir)) p->error = ESP_FAIL;
    append(p, source, "", FOLDER_REMOVE);
}
void book_folder_migrate(book_folder_plan_t* p, const book_collection_t* legacy) {
    for (int i = 0; i < book_collection_count(legacy); ++i)
        book_folder_directory(p, book_collection_at(legacy, i)->name, true);
    uint32_t id;
    const char* source;
    for (size_t i = 0; (source = book_collection_record(legacy, i, &id)); ++i) {
        if (!id) continue;
        const book_collection_category_t* c = book_collection_find(legacy, id);
        char folder[CAP];
        if (!c || !join(p->root, c->name, folder)) {
            p->error = ESP_ERR_INVALID_SIZE;
            return;
        }
        book_folder_add_move(p, source, folder, true);
        if (p->error == ESP_ERR_NO_MEM || p->count >= LIMIT) return;
    }
}
bool book_folder_same_plan(const book_folder_plan_t* a, const book_folder_plan_t* b) {
    if (a->error || b->error || a->count != b->count || strcmp(a->root, b->root)) return false;
    return !a->count || !memcmp(a->items, b->items, a->count * sizeof(*a->items));
}
esp_err_t book_folder_execute(book_folder_plan_t* p, book_path_state_extra_fn relocate) {
    if (p->error) return p->error;
    for (size_t i = 0; i < p->count; ++i) {
        book_folder_item_t* r = &p->items[i];
        esp_err_t err = ESP_OK;
        if (r->action == FOLDER_KEEP || r->action == FOLDER_SKIP || r->action == FOLDER_REUSE)
            continue;
        if (book_category_pending(p->root)) return ESP_ERR_INVALID_STATE;
        if (r->action == FOLDER_CREATE) {
            char created[CAP];
            err = book_category_create(p->root, strrchr(r->to, '/') + 1, created);
        } else if (r->action == FOLDER_MOVE) {
            struct stat st;
            char dir[CAP];
            strcpy(dir, r->to);
            *strrchr(dir, '/') = 0;
            if (stat(r->from, &st) || st.st_size != r->size || st.st_mtime != r->modified)
                return ESP_ERR_INVALID_STATE;
            err = book_category_move(p->root, r->from, dir, relocate);
            if (!err) ++p->completed;
        } else if (r->action == FOLDER_REMOVE)
            err = rmdir(r->from) ? ESP_FAIL : ESP_OK;
        else
            continue;
        if (err != ESP_OK) return err;
        p->changed = true;
    }
    return ESP_OK;
}
