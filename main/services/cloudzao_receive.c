/*
 * 中文：仅处理当前列表，只保留供选择的元数据，不缓存任务 ID 或签名 URL；只有经过验证的 TF 文件可触发确认。
 * English: Consume only the current list, keep only selection metadata, never cache task IDs or signed URLs, and acknowledge only verified TF files.
 */
#include "cloudzao_receive.h"
#include "cJSON.h"
#include "psa/crypto.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIB (1024u * 1024u)
#define CZ_PATH_MAX 320u

typedef struct { char *data; size_t used; } json_buffer_t;
typedef struct {
    const char *task, *name, *url, *directory;
    size_t length;
    int64_t expires;
    cz_kind_t kind;
    uint8_t digest[32];
} item_t;

typedef struct {
    const cz_ops_t *ops;
    int fd;
    psa_hash_operation_t hash;
    size_t received, expected;
    cz_error_t error;
} writer_t;

bool cloudzao_token_valid(const char *token) {
    if (!token || !*token) return false;
    size_t n = strnlen(token, CLOUDZAO_TOKEN_MAX + 1);
    return n <= CLOUDZAO_TOKEN_MAX && strspn(token,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~+/=") == n;
}

bool cloudzao_delivery_id_valid(const char *id) {
    if (!id || !*id || strlen(id) > CLOUDZAO_DELIVERY_ID_MAX) return false;
    return id[strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")] == 0;
}

static bool collect_json(void *context, const void *bytes, size_t length) {
    json_buffer_t *buffer = context;
    if (length > CLOUDZAO_JSON_MAX - buffer->used || memchr(bytes, 0, length)) return false;
    memcpy(buffer->data + buffer->used, bytes, length);
    buffer->used += length;
    buffer->data[buffer->used] = 0;
    return true;
}

static const char *string_at(const cJSON *object, const char *name) {
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, name));
}

// 先限制结构递归深度，再让 cJSON 分配和解析。/ Bound structural recursion before cJSON allocates or parses.
static bool json_safe(const char *json) {
    unsigned depth = 0;
    bool quoted = false;
    for (const char *p = json; *p; ++p) {
        if (quoted && *p == '\\') {
            if (!p[1] || !strncmp(p + 1, "u0000", 5)) return false;
            ++p;
        } else if (*p == '"') quoted = !quoted;
        else if (!quoted && (*p == '[' || *p == '{')) {
            if (++depth > 16) return false;
        } else if (!quoted && (*p == ']' || *p == '}')) {
            if (!depth) return false;
            --depth;
        }
    }
    return !quoted && !depth;
}

cJSON *cloudzao_json_parse(const char *json, size_t used) {
    if (memchr(json, 0, used) || !json_safe(json)) return NULL;
    return cJSON_ParseWithLengthOpts(json, used + 1, NULL, true);
}

static cz_error_t read_list(const cz_ops_t *ops, char *json, cJSON **root) {
    *root = NULL;
    cz_error_t error = ops->stop(ops->context);
    if (error != CZ_OK) return error;
    json[0] = 0;
    json_buffer_t response = {.data = json};
    const cz_http_request_t request = {.limit = CLOUDZAO_JSON_MAX};
    error = ops->request(ops->context, CZ_LIST, &request, collect_json, &response);
    if (error != CZ_OK) return error;
    *root = cloudzao_json_parse(json, response.used);
    return cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(*root, "items")) ? CZ_OK : CZ_INVALID;
}

static bool equal(const char *value, const char *expected) { return value && !strcasecmp(value, expected); }

static bool mime_is(const char *value, const char *expected) {
    if (!value) return false;
    while (*value == ' ' || *value == '\t') ++value;
    size_t n = strlen(expected);
    if (strncasecmp(value, expected, n)) return false;
    value += n;
    while (*value == ' ' || *value == '\t') ++value;
    return !*value || *value == ';';
}

static size_t utf8_non_ascii_width(const unsigned char *text, size_t remaining) {
    const unsigned char c = text[0];
    const size_t width = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 :
                         c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!width || width > remaining) return 0;
    const unsigned char next = text[1];
    if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) ||
        (c == 0xf0 && next < 0x90) || (c == 0xf4 && next >= 0x90)) return 0;
    for (size_t i = 1; i < width; ++i) if ((text[i] & 0xc0) != 0x80) return 0;
    return width;
}

bool cloudzao_name_valid(const char *name) {
    if (!name) return false;
    size_t n = strlen(name);
    if (!n || n > CLOUDZAO_NAME_MAX || name[0] == '.' || name[0] == ' ' || name[n - 1] == ' ' || strstr(name, ".."))
        return false;
    for (size_t i = 0; i < n;) {
        const unsigned char c = name[i];
        if (c < 32 || c == 127 || strchr("/\\:*?\"<>|", c)) return false;
        const size_t width = c < 128 ? 1 : utf8_non_ascii_width((const unsigned char *)name + i, n - i);
        if (!width) return false;
        i += width;
    }
    return true;
}

bool cloudzao_oss_host(const char *host, size_t length) {
    static const char suffix[] = ".aliyuncs.com";
    const size_t suffix_len = sizeof(suffix) - 1;
    return host && length > suffix_len && !strncasecmp(host + length - suffix_len, suffix, suffix_len);
}

bool cloudzao_download_target(const char *url, cz_download_target_t *target) {
    if (!url) return false;
    const size_t url_len = strnlen(url, CLOUDZAO_URL_MAX + 1);
    if (url_len > CLOUDZAO_URL_MAX) return false;
    bool legacy_oss = !strncmp(url, "http://", 7);
    if (!legacy_oss && strncmp(url, "https://", 8)) return false;
    const char *host = url + (legacy_oss ? 7 : 8);
    const char *end = strchr(host, '/');
    if (!end || end == host || end - host > 253 || !memchr(host, '.', (size_t)(end - host))) return false;
    size_t host_len = (size_t)(end - host);
    // 云造当前给部分 OSS 资源返回 HTTP 签名 URL；只借用目标并继续使用 443/TLS。
    // CloudZao returns HTTP-signed URLs for some OSS objects; reuse the target only over 443/TLS.
    const bool oss = cloudzao_oss_host(host, host_len);
    if (legacy_oss && !oss) return false;
    bool letter = false;
    for (const char *p = host; p < end; ++p) {
        const unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letter = true;
        else if (!(c >= '0' && c <= '9') && c != '.' && c != '-') return false;
    }
    if (!letter) return false;
    size_t encoded_len = 0;
    bool query = false;
    const unsigned char *stop = (const unsigned char *)url + url_len;
    for (const unsigned char *p = (const unsigned char *)end; p < stop;) {
        if (*p <= ' ' || *p == 127 || *p == '\\' || *p == '#') return false;
        if (*p == '?') query = true;
        if (*p < 128) {
            ++encoded_len;
            ++p;
        } else {
            // 仅兼容 OSS 路径中的裸 UTF-8；签名查询串保持原字节。/ Accept raw UTF-8 only in OSS paths; keep signed queries byte-for-byte.
            const size_t width = oss && !query ? utf8_non_ascii_width(p, (size_t)(stop - p)) : 0;
            if (!width) return false;
            encoded_len += 3 * width;
            p += width;
        }
    }
    if ((size_t)(end - url) + encoded_len > CLOUDZAO_URL_MAX) return false;
    if (target) *target = (cz_download_target_t){.host = host, .path = end, .host_len = host_len,
        .encoded_len = encoded_len};
    return true;
}

static int hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

static size_t file_size(const cJSON *value, size_t max) {
    if (cJSON_IsNumber(value)) {
        double number = value->valuedouble;
        return number >= 1 && number <= max && (size_t)number == number ? (size_t)number : 0;
    }
    const char *str = cJSON_GetStringValue(value);
    size_t size = 0;
    if (!str) return 0;
    while (*str) {
        if (*str < '0' || *str > '9' || size > max / 10) return 0;
        size = size * 10 + (size_t)(*str++ - '0');
    }
    return size <= max ? size : 0;
}

// 空目录表示格式不支持；格式支持但元数据损坏时返回错误。/ An empty directory means unsupported; malformed metadata is an error.
static cz_error_t parse_item(const cJSON *object, item_t *item) {
    *item = (item_t){0};
    if (!cJSON_IsObject(object)) return CZ_INVALID;
    item->name = string_at(object, "name");
    const char *ext = item->name ? strrchr(item->name, '.') : NULL;
    const char *type = string_at(object, "kind");
    const char *format = string_at(object, "format");
    const char *mime = string_at(object, "mediaType");
    bool binary = mime_is(mime, "application/octet-stream");
    size_t max = 0;
    if (equal(type, "BOOK") &&
        ((equal(format, "EPUB") && equal(ext, ".epub") && (binary || mime_is(mime, "application/epub+zip"))) ||
         (equal(format, "TXT") && equal(ext, ".txt") && (binary || mime_is(mime, "text/plain"))))) {
        item->directory = "books"; max = 100 * MIB;
        item->kind = equal(ext, ".epub") ? CZ_KIND_EPUB : CZ_KIND_TXT;
    } else if (equal(type, "BADGE") && equal(ext, ".png") && (binary || mime_is(mime, "image/png"))) {
        item->directory = "pictures"; max = 20 * MIB; item->kind = CZ_KIND_PNG;
    } else if (equal(type, "FONT") && equal(ext, ".ttf") &&
        (binary || mime_is(mime, "font/ttf") || mime_is(mime, "application/x-font-ttf") ||
         mime_is(mime, "application/font-sfnt"))) {
        item->directory = "fonts"; max = 32 * MIB; item->kind = CZ_KIND_TTF;
    } else return CZ_OK;
    item->task = string_at(object, "deliveryId");
    item->url = string_at(object, "downloadUrl");
    const char *sha = string_at(object, "sha256");
    item->length = file_size(cJSON_GetObjectItemCaseSensitive(object, "sizeBytes"), max);
    if (!cloudzao_name_valid(item->name) || !cloudzao_delivery_id_valid(item->task) ||
        !cloudzao_download_target(item->url, NULL) || !item->length || !sha || strlen(sha) != 64) return CZ_INVALID;
    const cJSON *expires = cJSON_GetObjectItemCaseSensitive(object, "downloadExpiresAt");
    if (expires && !cJSON_IsNull(expires)) {
        if (!cJSON_IsNumber(expires) || expires->valuedouble < 1 || expires->valuedouble > 253402300799.0 ||
            (int64_t)expires->valuedouble != expires->valuedouble) return CZ_INVALID;
        item->expires = (int64_t)expires->valuedouble;
    }
    for (size_t i = 0; i < sizeof(item->digest); ++i) {
        int hi = hex_digit(sha[2*i]), lo = hex_digit(sha[2*i+1]);
        if (hi < 0 || lo < 0) return CZ_INVALID;
        item->digest[i] = (uint8_t)(hi * 16 + lo);
    }
    return CZ_OK;
}

static bool write_file(void *context, const void *bytes, size_t length) {
    writer_t *writer = context;
    writer->error = writer->ops->stop(writer->ops->context);
    if (writer->error != CZ_OK) return false;
    if (length > writer->expected - writer->received) writer->error = CZ_DIGEST;
    else if (write(writer->fd, bytes, length) != (ssize_t)length) writer->error = CZ_STORAGE;
    else if (psa_hash_update(&writer->hash, bytes, length) != PSA_SUCCESS) writer->error = CZ_RESOURCES;
    else {
        writer->received += length;
        if (writer->ops->stage) writer->ops->stage(writer->ops->context, CZ_ROW_DOWNLOAD, writer->received);
    }
    return writer->error == CZ_OK;
}

static cz_error_t check_hash(psa_hash_operation_t *hash, const item_t *item, size_t length) {
    uint8_t digest[32]; size_t size = 0;
    if (length != item->length) return CZ_DIGEST;
    if (psa_hash_finish(hash, digest, sizeof(digest), &size) != PSA_SUCCESS) return CZ_RESOURCES;
    return size == sizeof(digest) && !memcmp(digest, item->digest, size) ? CZ_OK : CZ_DIGEST;
}

static cz_error_t verify_existing(const cz_ops_t *ops, const item_t *item, const char *path, unsigned char *chunk) {
    FILE *file = fopen(path, "rb");
    if (!file) return CZ_STORAGE;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    cz_error_t error = psa_hash_setup(&hash, PSA_ALG_SHA_256) == PSA_SUCCESS ? CZ_OK : CZ_RESOURCES;
    size_t total = 0;
    while (error == CZ_OK && (error = ops->stop(ops->context)) == CZ_OK) {
        size_t n = fread(chunk, 1, CLOUDZAO_CHUNK, file);
        if (ferror(file)) { error = CZ_STORAGE; break; }
        if (n > item->length - total) { error = CZ_DIGEST; break; }
        if (!n) { error = check_hash(&hash, item, total); break; }
        if (psa_hash_update(&hash, chunk, n) != PSA_SUCCESS) { error = CZ_RESOURCES; break; }
        total += n;
    }
    if (fclose(file) && error == CZ_OK) error = CZ_STORAGE;
    psa_hash_abort(&hash);
    return error == CZ_DIGEST ? CZ_CONFLICT : error;
}

static bool ensure_dir(const char *path) {
    struct stat info;
    return !mkdir(path, 0775) || (errno == EEXIST && !stat(path, &info) && S_ISDIR(info.st_mode));
}

static bool make_path(char *path, const char *root, const char *directory, const char *name) {
    int n = snprintf(path, CZ_PATH_MAX, "%s/%s%s%s", root, directory, name ? "/" : "", name ? name : "");
    return n > 0 && n < (int)CZ_PATH_MAX;
}

static cz_error_t save_item(const cz_ops_t *ops, const item_t *item, unsigned char *chunk, bool *added) {
    cz_error_t error = ops->stop(ops->context);
    if (error != CZ_OK) return error;
    char path[CZ_PATH_MAX], part[CZ_PATH_MAX];
    if (!make_path(path, ops->root, item->directory, NULL) || !ensure_dir(path) ||
        !make_path(path, ops->root, item->directory, item->name)) return CZ_STORAGE;
    struct stat info;
    if (!stat(path, &info)) {
        if (!S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size != item->length) return CZ_CONFLICT;
        if (ops->stage) ops->stage(ops->context, CZ_ROW_VERIFY, 0);
        return verify_existing(ops, item, path, chunk);
    }
    if (errno != ENOENT) return CZ_STORAGE;
    if (item->expires && ops->now && ops->now(ops->context) >= item->expires) return CZ_LINK_EXPIRED;
    if (ops->free_bytes(ops->context) < item->length) return CZ_STORAGE;
    if (!make_path(part, ops->root, ".readpico", NULL) || !ensure_dir(part) ||
        !make_path(part, ops->root, ".readpico/cloudzao", NULL) || !ensure_dir(part) ||
        !make_path(part, ops->root, ".readpico/cloudzao", "incoming.part")) return CZ_STORAGE;
    if (unlink(part) && errno != ENOENT) return CZ_STORAGE;
    if ((error = ops->stop(ops->context)) != CZ_OK) return error;
    int fd = open(part, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) return CZ_STORAGE;
    writer_t writer = {.ops = ops, .fd = fd, .hash = PSA_HASH_OPERATION_INIT, .expected = item->length};
    if (psa_hash_setup(&writer.hash, PSA_ALG_SHA_256) != PSA_SUCCESS) error = CZ_RESOURCES;
    else {
        cz_http_request_t request = {.url = item->url, .limit = item->length};
        if (ops->stage) ops->stage(ops->context, CZ_ROW_DOWNLOAD, 0);
        error = ops->request(ops->context, CZ_DOWNLOAD, &request, write_file, &writer);
        if (writer.error != CZ_OK) error = writer.error;
    }
    if (error == CZ_OK) error = ops->stop(ops->context);
    if (error == CZ_OK) {
        if (ops->stage) ops->stage(ops->context, CZ_ROW_VERIFY, writer.received);
        error = check_hash(&writer.hash, item, writer.received);
    }
    psa_hash_abort(&writer.hash);
    if (error == CZ_OK && fsync(fd)) error = CZ_STORAGE;
    if (close(fd) && error == CZ_OK) error = CZ_STORAGE;
    if (error == CZ_OK) error = ops->stop(ops->context);
    if (error == CZ_OK) {
        // FatFs f_rename 不替换已有目录项；先检查，以保持宿主测试与设备行为一致。
        // FatFs f_rename does not replace an entry; precheck for parity with the host fixtures.
        if (!stat(path, &info)) error = CZ_CONFLICT;
        else if (errno != ENOENT || rename(part, path)) error = CZ_STORAGE;
        else *added = true;
    }
    if (!*added && ops->stop(ops->context) != CZ_MEDIA) unlink(part);
    return error;
}

static cz_error_t acknowledge(const cz_ops_t *ops, const item_t *item) {
    const cz_http_request_t request = {.delivery_id = item->task};
    cz_error_t error = ops->stop(ops->context);
    if (error == CZ_OK) error = ops->request(ops->context, CZ_ACK, &request, NULL, NULL);
    // 仅确认接口允许超时后重发；保存过的文件不重下。/ Only acknowledgement retries a timeout, without downloading again.
    if (error == CZ_TIMEOUT && ops->stop(ops->context) == CZ_OK)
        error = ops->request(ops->context, CZ_ACK, &request, NULL, NULL);
    return error;
}

static cz_error_t offer_fingerprint(const item_t *item, uint8_t out[32]) {
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    size_t written = 0;
    const uint32_t size = (uint32_t)item->length;
    const uint8_t kind = item->kind;
    // 不含签名 URL，刷新时可以换签；更名、改内容或不同任务永不继承旧勾选。
    // Exclude the signed URL so a fresh signature is accepted; renamed, changed or different tasks never inherit an old selection.
    const bool ok = psa_hash_setup(&hash, PSA_ALG_SHA_256) == PSA_SUCCESS &&
        psa_hash_update(&hash, (const uint8_t *)item->task, strlen(item->task) + 1) == PSA_SUCCESS &&
        psa_hash_update(&hash, (const uint8_t *)item->name, strlen(item->name) + 1) == PSA_SUCCESS &&
        psa_hash_update(&hash, (const uint8_t *)&size, sizeof(size)) == PSA_SUCCESS &&
        psa_hash_update(&hash, &kind, sizeof(kind)) == PSA_SUCCESS &&
        psa_hash_update(&hash, item->digest, sizeof(item->digest)) == PSA_SUCCESS &&
        psa_hash_finish(&hash, out, 32, &written) == PSA_SUCCESS && written == 32;
    psa_hash_abort(&hash);
    return ok ? CZ_OK : CZ_RESOURCES;
}

static cz_error_t preview_offers(const cz_ops_t *ops, const cJSON *list, cz_queue_t *queue) {
    queue->count = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, list) {
        if (queue->count >= CLOUDZAO_LIST_ROWS) break;
        cz_error_t error = ops->stop(ops->context);
        if (error) return error;
        item_t item = {0};
        const bool eligible = parse_item(entry, &item) == CZ_OK && item.kind != CZ_KIND_NONE;
        cz_offer_t *offer = &queue->offers[queue->count++];
        *offer = (cz_offer_t){.kind = eligible ? item.kind : CZ_KIND_NONE,
            .file = {.size = (uint32_t)item.length, .state = eligible ? CZ_ROW_WAIT : CZ_ROW_SKIPPED}};
        if (cloudzao_name_valid(item.name)) memcpy(offer->file.name, item.name, strlen(item.name) + 1);
        if (eligible && (error = offer_fingerprint(&item, offer->fingerprint)) != CZ_OK) return error;
    }
    return CZ_OK;
}

static bool is_chosen(const cz_queue_t *queue, unsigned i) {
    return queue->selection.checked[i] && queue->offers[i].kind != CZ_KIND_NONE;
}

static cz_error_t match_offers(const cz_ops_t *ops, const cJSON *list, const cz_queue_t *queue,
                               const cJSON *matches[CLOUDZAO_LIST_ROWS], unsigned remaining) {
    const cJSON *entry;
    cJSON_ArrayForEach(entry, list) {
        if (!remaining) break;
        cz_error_t error = ops->stop(ops->context);
        if (error) return error;
        item_t item = {0};
        if (parse_item(entry, &item) != CZ_OK || item.kind == CZ_KIND_NONE) continue;
        uint8_t fingerprint[32];
        if ((error = offer_fingerprint(&item, fingerprint))) return error;
        for (unsigned i = 0; i < queue->count; ++i) {
            if (!is_chosen(queue, i) || memcmp(fingerprint, queue->offers[i].fingerprint, sizeof(fingerprint))) continue;
            if (!matches[i]) { matches[i] = entry; --remaining; }
            // 相同任务重复返回时只使用一次。/ Only use a repeated task once.
            break;
        }
    }
    return CZ_OK;
}

cz_status_t cloudzao_run(const cz_ops_t *ops, cz_job_t job, cz_queue_t *queue, char *json, unsigned char *chunk) {
    cz_status_t status = {.busy = true};
    if (!queue || (job != CZ_JOB_PREVIEW && job != CZ_JOB_DOWNLOAD)) {
        status.error = CZ_INVALID;
        return status;
    }
    if (job == CZ_JOB_DOWNLOAD) {
        if (queue->count <= CLOUDZAO_LIST_ROWS)
            for (unsigned i = 0; i < queue->count; ++i) if (is_chosen(queue, i)) ++status.total;
        if (!status.total) { status.error = CZ_INVALID; return status; }
    }
    cJSON *root;
    status.error = read_list(ops, json, &root);
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (status.error == CZ_OK) status.error = ops->stop(ops->context);
    if (status.error == CZ_OK && job == CZ_JOB_PREVIEW) status.total = (unsigned)cJSON_GetArraySize(list);
    // 空清单没有需要计算摘要的条目。/ An empty preview has no entries to hash.
    if (status.error == CZ_OK && (job == CZ_JOB_DOWNLOAD || status.total) && psa_crypto_init() != PSA_SUCCESS)
        status.error = CZ_RESOURCES;
    if (status.error == CZ_OK && job == CZ_JOB_PREVIEW) {
        status.error = preview_offers(ops, list, queue);
    } else if (status.error == CZ_OK && job == CZ_JOB_DOWNLOAD) {
        const cJSON *matches[CLOUDZAO_LIST_ROWS] = {0};
        status.error = match_offers(ops, list, queue, matches, status.total);
        if (status.error == CZ_OK) {
            for (unsigned i = 0; i < queue->count; ++i) {
                if (!matches[i]) continue;
                char part[CZ_PATH_MAX];
                status.error = ops->stop(ops->context);
                if (status.error == CZ_OK &&
                    (!make_path(part, ops->root, ".readpico/cloudzao", "incoming.part") || (unlink(part) && errno != ENOENT)))
                    status.error = CZ_STORAGE;
                break;
            }
        }
        if (status.error == CZ_OK && ops->row) {
            unsigned index = 0;
            for (unsigned i = 0; i < queue->count; ++i)
                if (is_chosen(queue, i)) ops->row(ops->context, index++, &queue->offers[i].file, false);
        }
        if (status.error == CZ_OK) ops->progress(ops->context, &status);
        bool link_relisted = false;
        for (unsigned i = 0; i < queue->count && status.error == CZ_OK; ++i) {
            if (!is_chosen(queue, i)) continue;
            if ((status.error = ops->stop(ops->context)) != CZ_OK) break;
            cz_row_t row = queue->offers[i].file;
            if (!matches[i]) {
                ++status.skipped;
                ++status.unavailable;
                row.state = CZ_ROW_UNAVAILABLE;
            } else {
                item_t item = {0}; bool added = false;
                cz_error_t error = parse_item(matches[i], &item);
                cz_request_t operation = CZ_DOWNLOAD;
                row.state = CZ_ROW_CHECK;
                if (ops->row) ops->row(ops->context, status.processed, &row, true);
                if (error == CZ_OK) error = save_item(ops, &item, chunk, &added);
                if (error == CZ_LINK_EXPIRED && !link_relisted) {
                    link_relisted = true;
                    cJSON *fresh;
                    error = read_list(ops, json, &fresh);
                    if (error == CZ_OK) {
                        cJSON_Delete(root);
                        root = fresh;
                        memset(matches, 0, sizeof(matches));
                        error = match_offers(ops, cJSON_GetObjectItemCaseSensitive(root, "items"), queue, matches, status.total);
                        if (error == CZ_OK) error = matches[i] ? parse_item(matches[i], &item) : CZ_DELIVERY_MISSING;
                        if (error == CZ_OK) error = save_item(ops, &item, chunk, &added);
                    } else cJSON_Delete(fresh);
                }
                if (error == CZ_OK) {
                    ++status.saved;
                    if (added && !strcmp(item.directory, "books")) ops->book_added(ops->context);
                    operation = CZ_ACK;
                    if (ops->stage) ops->stage(ops->context, CZ_ROW_CONFIRM, item.length);
                    error = acknowledge(ops, &item);
                    if (error != CZ_OK) ++status.pending;
                    row.state = error == CZ_OK ? CZ_ROW_SAVED : CZ_ROW_PENDING;
                } else {
                    ++status.failed;
                    row.state = (error == CZ_CANCELLED || error == CZ_MEDIA || error == CZ_WIFI ||
                                 (error == CZ_TIMEOUT && ops->stop(ops->context) != CZ_OK))
                        ? CZ_ROW_INTERRUPTED : CZ_ROW_FAILED;
                }
                if (error != CZ_OK) {
                    status.item_error = error;
                    status.item_request = operation;
                }
                if (error == CZ_AUTH || error == CZ_CREDENTIAL_STORAGE || error == CZ_CLOCK ||
                    error == CZ_REFRESH_REUSED || error == CZ_REFRESH_INTERRUPTED)
                    status.error = error;
            }
            if (ops->row) ops->row(ops->context, status.processed, &row, false);
            ++status.processed;
            ops->progress(ops->context, &status);
            if (ops->record && row.name[0] && row.state != CZ_ROW_INTERRUPTED && row.state != CZ_ROW_UNAVAILABLE &&
                ops->stop(ops->context) == CZ_OK) ops->record(ops->context, &row);
        }
    }
    if (status.error == CZ_OK) status.error = ops->stop(ops->context);
    cJSON_Delete(root);
    return status;
}
