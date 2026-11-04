/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：MOBI/PRC 书源：PDB 记录表、PalmDOC/HUFF-CDIC 解压、EXTH 元数据与 filepos 目录。
 * English: MOBI/PRC source: PDB record table, PalmDOC/HUFF-CDIC inflate, EXTH metadata, filepos TOC.
 *
 * 冻结：只读；不处理 DRM；正文按 text length 截断；单章一次解压，不整本常驻内存。
 * Frozen: Read-only; no DRM; the text stops at the PalmDOC text length; one chapter is inflated at a
 * time and the whole book is never held in RAM.
 */
#include "book_mobi.h"
#include "book_cover.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MOBI_CHAPTER_MAX 2048
#define MOBI_TITLE_MAX 120
#define MOBI_PATH_MAX 512
#define MOBI_RECORD_MAX (1u << 20)       ///< 单条 PDB 记录上限 / Largest PDB record accepted
#define MOBI_HEAD_MAX (1u << 18)         ///< 记录 0 上限 / Largest record 0 accepted
#define MOBI_TEXT_MAX (32u << 20)        ///< 解压正文上限 / Largest decompressed text accepted
#define MOBI_TOC_WINDOW (96u * 1024u)    ///< 目录页扫描窗口 / Window scanned around the TOC page
#define MOBI_FRONT_WINDOW (256u * 1024u) ///< 无 guide 时的书前扫描窗口 / Front window without a guide
#define MOBI_SCAN_CARRY 512              ///< 跨记录标签的回看长度 / Look-back covering split tags
#define MOBI_IMAGE_SCAN 64               ///< 缺首图记录时的探测条数 / Records probed for a missing first image

static const char *TAG = "book_mobi";

typedef struct {
    uint32_t begin, end; ///< 解压正文中的半开区间 / Half-open range in the decompressed text
    char *title;         ///< UTF-8 标题 / UTF-8 title
} mobi_chapter_t;

typedef struct {
    uint32_t offset;
    const char *title; ///< 池内标题，未命名时为 NULL / Interned title, NULL when unlabelled
} mobi_anchor_t;

typedef struct {
    uint8_t *data;
    size_t len, cap, limit;
    bool failed;
} buf_t;

typedef struct {
    const uint8_t *data;
    uint32_t len;
    bool shared; ///< 词典短语可直接输出 / Dictionary phrase that is plain bytes
    bool busy;   ///< 正在展开，用于断环 / Expanding now, used to break cycles
} cdic_t;

struct book_mobi {
    FILE *file;
    char path[MOBI_PATH_MAX];
    uint64_t file_size;
    uint32_t *records; ///< PDB 记录起始偏移 / PDB record start offsets
    size_t record_count;
    uint16_t compression;
    uint16_t record_size;
    uint16_t encryption;
    uint32_t text_length;
    uint32_t first_text;  ///< 第一条正文记录 / First text record
    uint32_t first_image; ///< 第一条图片记录 / First image record
    uint32_t encoding;
    uint32_t huff_offset, huff_count;
    uint32_t ncx_index; ///< INDX/NCX 主记录 / Main INDX record holding the NCX
    uint8_t *scratch; ///< 单条正文解压缓冲 / One-record inflate scratch
    mobi_chapter_t *chapters;
    size_t chapter_count;
    char *book_title;
    char *book_author;
    // HUFF/CDIC 表，只有 17480 压缩时才装载。/ HUFF/CDIC tables, loaded only for 17480.
    struct {
        uint8_t codelen;
        bool term;
        uint64_t maxcode;
    } dict1[256];
    uint64_t mincode[33], maxcode[33];
    cdic_t *dict;
    size_t dict_count, dict_cap;
    uint8_t **pool; ///< CDIC 原始记录，词典短语指向它 / Raw CDIC records the phrases point into
    size_t pool_count, pool_cap;
};

/* ---- 基础读取 / Basic IO ---- */

static void *psram(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

static bool read_at(FILE *file, uint64_t offset, void *dst, size_t bytes) {
    if (fseek(file, (long)offset, SEEK_SET) != 0) return false;
    return fread(dst, 1, bytes, file) == bytes;
}

/// 记录表加文件句柄的最小视图；封面与元数据路径只用到这些字段，不必落一整个书源。
/// The record table plus handle alone; cover and metadata paths need nothing else from a full source.
typedef struct {
    FILE *file;
    uint64_t file_size;
    uint32_t *records;
    size_t record_count;
} mobi_io_t;

/// 读取一条 PDB 记录；调用方 free。/ Read one PDB record; the caller frees it.
static uint8_t *io_record(mobi_io_t *io, size_t index, size_t *bytes) {
    if (bytes) *bytes = 0;
    if (index >= io->record_count) return NULL;
    uint64_t begin = io->records[index];
    uint64_t end = index + 1 < io->record_count ? io->records[index + 1] : io->file_size;
    if (end <= begin || end > io->file_size || end - begin > MOBI_RECORD_MAX) return NULL;
    size_t n = (size_t)(end - begin);
    uint8_t *data = psram(n);
    if (!data) return NULL;
    if (!read_at(io->file, begin, data, n)) { free(data); return NULL; }
    if (bytes) *bytes = n;
    return data;
}

static uint8_t *record_bytes(book_mobi_t *book, size_t index, size_t *bytes) {
    mobi_io_t io = {book->file, book->file_size, book->records, book->record_count};
    return io_record(&io, index, bytes);
}

/* ---- 解压 / Inflate ---- */

/// PalmDOC LZ77；写满 cap 即停，记录尾部的附加条目不会被当成正文。
/// PalmDOC LZ77; stops at cap so the trailing record entries never leak into the text.
static size_t palmdoc_inflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    size_t i = 0, o = 0;
    while (i < n && o < cap) {
        uint8_t c = in[i++];
        if (c == 0 || (c >= 0x09 && c <= 0x7f)) {
            out[o++] = c;
        } else if (c <= 0x08) {
            size_t run = c;
            while (run-- && i < n && o < cap) out[o++] = in[i++];
        } else if (c >= 0xc0) {
            if (o + 2 > cap) break;
            out[o++] = ' ';
            out[o++] = (uint8_t)(c ^ 0x80);
        } else {
            if (i >= n) break;
            uint32_t pair = ((uint32_t)c << 8) | in[i++];
            size_t distance = (pair >> 3) & 0x7ff;
            size_t run = (pair & 7) + 3;
            if (!distance || distance > o) break;
            while (run-- && o < cap) { out[o] = out[o - distance]; ++o; }
        }
    }
    return o;
}

static bool buf_reserve(buf_t *b, size_t extra) {
    if (b->failed) return false;
    if (b->len + extra > b->limit) { b->failed = true; return false; }
    if (b->len + extra <= b->cap) return true;
    size_t cap = b->cap ? b->cap : 1024;
    while (cap < b->len + extra && cap < b->limit) cap *= 2;
    if (cap > b->limit) cap = b->limit;
    uint8_t *grown = heap_caps_realloc(b->data, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!grown) { b->failed = true; return false; }
    b->data = grown;
    b->cap = cap;
    return true;
}

static bool buf_add(buf_t *b, const void *src, size_t n) {
    if (!n) return !b->failed;
    if (!buf_reserve(b, n)) return false;
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return true;
}

static bool buf_byte(buf_t *b, char c) { return buf_add(b, &c, 1); }

static bool pool_keep(book_mobi_t *book, uint8_t *data) {
    if (book->pool_count == book->pool_cap) {
        size_t cap = book->pool_cap ? book->pool_cap * 2 : 4;
        uint8_t **grown = heap_caps_realloc(book->pool, cap * sizeof(*grown),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!grown) return false;
        book->pool = grown;
        book->pool_cap = cap;
    }
    book->pool[book->pool_count++] = data;
    return true;
}

static bool dict_append(book_mobi_t *book, const cdic_t *entry) {
    if (book->dict_count == book->dict_cap) {
        size_t cap = book->dict_cap ? book->dict_cap * 2 : 512;
        cdic_t *grown = heap_caps_realloc(book->dict, cap * sizeof(*grown),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!grown) return false;
        book->dict = grown;
        book->dict_cap = cap;
    }
    book->dict[book->dict_count++] = *entry;
    return true;
}

static bool huff_inflate(book_mobi_t *book, const uint8_t *data, size_t len, buf_t *out, int depth);

/// 展开一条词典短语；嵌套条目递归解码后再当作普通字节。
/// Expand one dictionary phrase; a nested entry is decoded recursively and then treated as bytes.
static bool huff_phrase(book_mobi_t *book, cdic_t *entry, buf_t *out, int depth) {
    if (entry->shared) return buf_add(out, entry->data, entry->len);
    if (entry->busy || depth > 6) return false;
    entry->busy = true;
    bool ok = huff_inflate(book, entry->data, entry->len, out, depth);
    entry->busy = false;
    return ok;
}

static bool huff_inflate(book_mobi_t *book, const uint8_t *data, size_t len, buf_t *out, int depth) {
    if (!len) return false;
    uint8_t *window = heap_caps_calloc(1, len + 16, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!window) return false;
    memcpy(window, data, len);
    long bits_left = (long)len * 8;
    size_t pos = 0;
    uint64_t x = be64(window);
    long n = 32;
    bool ok = true;
    for (;;) {
        if (n <= 0) {
            pos += 4;
            if (pos + 8 > len + 16) break;
            x = be64(window + pos);
            n += 32;
        }
        uint32_t code = (uint32_t)((x >> (unsigned)n) & 0xffffffffull);
        unsigned codelen = book->dict1[code >> 24].codelen;
        uint64_t maxcode = book->dict1[code >> 24].maxcode;
        bool term = book->dict1[code >> 24].term;
        if (!term) {
            while (codelen <= 32 && (uint64_t)code < book->mincode[codelen]) ++codelen;
            if (codelen > 32) { ok = false; break; }
            maxcode = book->maxcode[codelen];
        }
        n -= (long)codelen;
        bits_left -= (long)codelen;
        if (bits_left < 0) break;
        uint64_t index = (maxcode - code) >> (32 - codelen);
        if (index >= book->dict_count) { ok = false; break; }
        if (!huff_phrase(book, &book->dict[index], out, depth + 1)) { ok = false; break; }
        if (out->failed) { ok = false; break; }
    }
    free(window);
    return ok;
}

static bool huff_load_table(book_mobi_t *book, size_t index) {
    size_t n = 0;
    uint8_t *huff = record_bytes(book, index, &n);
    if (!huff) return false;
    bool ok = false;
    do {
        if (n < 24 || memcmp(huff, "HUFF", 4) != 0) break;
        uint32_t off1 = be32(huff + 8), off2 = be32(huff + 12);
        if ((uint64_t)off1 + 1024 > n || (uint64_t)off2 + 256 > n) break;
        bool bad = false;
        for (unsigned i = 0; i < 256; ++i) {
            uint32_t v = be32(huff + off1 + i * 4);
            unsigned codelen = v & 0x1f;
            if (!codelen) { bad = true; break; }
            uint32_t maxcode = v >> 8;
            book->dict1[i].codelen = (uint8_t)codelen;
            book->dict1[i].term = (v & 0x80) != 0;
            // 参考实现用任意精度整数；这里相应放宽到 64 位，避免 33 位的 maxcode 被截断。
            // The reference uses arbitrary-precision integers; 64 bits keeps a 33-bit maxcode intact.
            book->dict1[i].maxcode = (((uint64_t)maxcode + 1u) << (32 - codelen)) - 1;
        }
        if (bad) break;
        for (unsigned i = 1; i <= 32; ++i) {
            uint32_t low = be32(huff + off2 + (i - 1) * 8);
            uint32_t high = be32(huff + off2 + (i - 1) * 8 + 4);
            book->mincode[i] = (uint64_t)low << (32 - i);
            book->maxcode[i] = (((uint64_t)high + 1u) << (32 - i)) - 1;
        }
        ok = true;
    } while (0);
    free(huff);
    return ok;
}

static bool huff_load_cdic(book_mobi_t *book, size_t index) {
    size_t n = 0;
    uint8_t *cdic = record_bytes(book, index, &n);
    if (!cdic) return false;
    if (n < 16 || memcmp(cdic, "CDIC", 4) != 0 || be32(cdic + 4) != 0x10) { free(cdic); return false; }
    uint32_t phrases = be32(cdic + 8), bits = be32(cdic + 12);
    if (bits > 24) { free(cdic); return false; }
    uint64_t room = phrases > book->dict_count ? phrases - book->dict_count : 0;
    uint64_t count = 1ull << bits;
    if (room < count) count = room;
    if (16 + count * 2 > n) { free(cdic); return false; }
    bool ok = pool_keep(book, cdic);
    for (uint64_t i = 0; ok && i < count; ++i) {
        uint32_t at = 16u + be16(cdic + 16 + (size_t)i * 2);
        if ((uint64_t)at + 2 > n) { ok = false; break; }
        uint16_t head = be16(cdic + at);
        uint32_t bytes = head & 0x7fff;
        if ((uint64_t)at + 2 + bytes > n) { ok = false; break; }
        cdic_t entry = {cdic + at + 2, bytes, (head & 0x8000) != 0, false};
        if (!dict_append(book, &entry)) ok = false;
    }
    return ok;
}

static bool huff_ready(book_mobi_t *book) {
    if (book->dict_count) return true;
    if (!book->huff_offset || !book->huff_count) return false;
    if (!huff_load_table(book, book->huff_offset)) return false;
    for (uint32_t i = 1; i < book->huff_count; ++i)
        if (!huff_load_cdic(book, book->huff_offset + i)) return false;
    return book->dict_count != 0;
}

/// 解压一条正文记录，写满 cap 即停。/ Inflate one text record, never past cap.
static size_t text_record(book_mobi_t *book, size_t index, uint8_t *out, size_t cap) {
    size_t n = 0;
    uint8_t *raw = record_bytes(book, index, &n);
    if (!raw) return 0;
    size_t got = 0;
    if (book->compression == 1) {
        got = n < cap ? n : cap;
        memcpy(out, raw, got);
    } else if (book->compression == 2) {
        got = palmdoc_inflate(raw, n, out, cap);
    } else if (book->compression == 17480 && huff_ready(book)) {
        buf_t sink = {out, 0, cap, cap, false};
        got = huff_inflate(book, raw, n, &sink, 0) ? sink.len : 0;
    }
    free(raw);
    return got;
}

/* ---- 头部解析 / Header parsing ---- */

typedef struct {
    uint64_t file_size;
    uint16_t compression;
    uint32_t text_length;
    uint16_t text_records;
    uint16_t record_size;
    uint16_t encryption;
    uint32_t encoding;
    uint32_t first_text;
    uint32_t first_image;
    uint32_t huff_offset, huff_count;
    uint32_t ncx_index;
    uint32_t exth_flags;
    uint32_t cover_offset, thumb_offset;
    bool has_cover, has_thumb;
    char title[MOBI_TITLE_MAX + 1];
    char author[MOBI_TITLE_MAX + 1];
} mobi_head_t;

static const uint16_t cp1252_high[32] = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};

static uint32_t cp1252(uint32_t byte) {
    if (byte >= 0x80 && byte < 0xa0) return cp1252_high[byte - 0x80];
    return byte;
}

static size_t utf8_put(char *dst, size_t cap, uint32_t cp) {
    char utf8[4];
    size_t width = 0;
    if (cp < 0x80) utf8[width++] = (char)cp;
    else if (cp < 0x800) {
        utf8[width++] = (char)(0xc0 | (cp >> 6));
        utf8[width++] = (char)(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        utf8[width++] = (char)(0xe0 | (cp >> 12));
        utf8[width++] = (char)(0x80 | ((cp >> 6) & 63));
        utf8[width++] = (char)(0x80 | (cp & 63));
    } else {
        utf8[width++] = (char)(0xf0 | (cp >> 18));
        utf8[width++] = (char)(0x80 | ((cp >> 12) & 63));
        utf8[width++] = (char)(0x80 | ((cp >> 6) & 63));
        utf8[width++] = (char)(0x80 | (cp & 63));
    }
    if (width >= cap) return 0;
    memcpy(dst, utf8, width);
    return width;
}

/// 解码 EXTH/PDB 文本：声明 UTF-8 时原样透传，否则按 CP1252 转码。
/// Decode EXTH/PDB text: pass UTF-8 through, otherwise transcode CP1252.
static void head_text(char *dst, size_t cap, const uint8_t *src, size_t n, uint32_t encoding) {
    if (!dst || !cap) return;
    dst[0] = 0;
    if (!src) return;
    size_t used = 0;
    for (size_t i = 0; i < n && src[i];) {
        uint32_t cp = src[i];
        size_t step = 1;
        if (encoding == 65001) {
            if (cp >= 0x80) {
                size_t width = cp >= 0xf0 ? 4 : cp >= 0xe0 ? 3 : cp >= 0xc0 ? 2 : 0;
                if (!width || i + width > n) { cp = 0xfffd; }
                else {
                    bool ok = true;
                    for (size_t k = 1; k < width; ++k)
                        if ((src[i + k] & 0xc0) != 0x80) ok = false;
                    if (!ok) { cp = 0xfffd; }
                    else {
                        if (used + width + 1 > cap) break;
                        memcpy(dst + used, src + i, width);
                        used += width;
                        i += width;
                        continue;
                    }
                }
            }
        } else {
            cp = cp1252(cp);
        }
        size_t width = utf8_put(dst + used, cap - used - 1, cp);
        if (!width) break;
        used += width;
        i += step;
    }
    dst[used] = 0;
}

static void head_exth(mobi_head_t *head, const uint8_t *r0, size_t len, size_t at) {
    if (at + 12 > len || memcmp(r0 + at, "EXTH", 4) != 0) return;
    uint32_t total = be32(r0 + at + 4);
    uint32_t count = be32(r0 + at + 8);
    if (total > len - at) total = (uint32_t)(len - at);
    size_t end = at + total, p = at + 12;
    while (p + 8 <= end && count--) {
        uint32_t type = be32(r0 + p), size = be32(r0 + p + 4);
        if (size < 8 || p + size > end) break;
        const uint8_t *value = r0 + p + 8;
        size_t bytes = size - 8;
        if (type == 503) head_text(head->title, sizeof(head->title), value, bytes, head->encoding);
        else if (type == 100) head_text(head->author, sizeof(head->author), value, bytes, head->encoding);
        else if (type == 201 && bytes >= 4) { head->cover_offset = be32(value); head->has_cover = true; }
        else if (type == 202 && bytes >= 4) { head->thumb_offset = be32(value); head->has_thumb = true; }
        p += size;
    }
}

/// 解析 PDB/MOBI 头部；只需记录 0，不建章节索引。
/// Parse the PDB/MOBI header from record 0 only; no chapter index is built.
static esp_err_t head_parse(FILE *file, mobi_head_t *head, uint32_t **records, size_t *record_count) {
    memset(head, 0, sizeof(*head));
    if (records) *records = NULL;
    if (record_count) *record_count = 0;
    struct stat st;
    if (fstat(fileno(file), &st) != 0 || st.st_size < 78) return ESP_ERR_INVALID_RESPONSE;
    head->file_size = (uint64_t)st.st_size;
    uint8_t pdb[78];
    if (!read_at(file, 0, pdb, sizeof(pdb))) return ESP_ERR_INVALID_RESPONSE;
    // 类型 BOOK/TEXt 且创建者 MOBI 才算 Palm 数据库型电子书。
    // Only a BOOK/TEXt database with the MOBI creator counts as a Palm e-book.
    if (memcmp(pdb + 60, "BOOK", 4) != 0 && memcmp(pdb + 60, "TEXt", 4) != 0)
        return ESP_ERR_NOT_SUPPORTED;
    if (memcmp(pdb + 64, "MOBI", 4) != 0) return ESP_ERR_NOT_SUPPORTED;
    size_t count = be16(pdb + 76);
    if (!count) return ESP_ERR_INVALID_RESPONSE;
    uint32_t *table = psram(count * sizeof(*table));
    if (!table) return ESP_ERR_NO_MEM;
    uint8_t chunk[8 * 64];
    for (size_t i = 0; i < count; i += 64) {
        size_t batch = count - i < 64 ? count - i : 64;
        if (!read_at(file, 78 + i * 8, chunk, batch * 8)) { free(table); return ESP_ERR_INVALID_RESPONSE; }
        for (size_t k = 0; k < batch; ++k) table[i + k] = be32(chunk + k * 8);
    }
    for (size_t i = 0; i < count; ++i) {
        if (table[i] >= head->file_size || (i && table[i] < table[i - 1])) {
            free(table);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    uint64_t r0_end = count > 1 ? table[1] : head->file_size;
    if (r0_end <= table[0] || r0_end - table[0] > MOBI_HEAD_MAX) { free(table); return ESP_ERR_INVALID_RESPONSE; }
    size_t r0_len = (size_t)(r0_end - table[0]);
    uint8_t *r0 = psram(r0_len);
    if (!r0) { free(table); return ESP_ERR_NO_MEM; }
    if (!read_at(file, table[0], r0, r0_len)) { free(r0); free(table); return ESP_ERR_INVALID_RESPONSE; }
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    do {
        if (r0_len < 16 + 0x18 || memcmp(r0 + 16, "MOBI", 4) != 0) break;
        uint32_t header_len = be32(r0 + 20);
        if (header_len < 0x18 || 16u + header_len > r0_len) break;
        head->compression = be16(r0);
        head->text_length = be32(r0 + 4);
        head->text_records = be16(r0 + 8);
        head->record_size = be16(r0 + 10);
        head->encryption = be16(r0 + 12);
        head->encoding = be32(r0 + 28);
        if (head->encryption) { err = ESP_ERR_NOT_SUPPORTED; break; }
        if (!head->text_length || head->text_length > MOBI_TEXT_MAX) break;
        if (head->compression != 1 && head->compression != 2 && head->compression != 17480) break;
        if (!head->record_size) head->record_size = 4096;
        if (head->record_size > 4096) break;
        head->first_text = 1;
        // 记录 0 内的字段一律按记录 0 起点寻址：MOBI 头的 "offset" 表就是从 0x10 起的。
        // Record-0 fields are addressed from the record start: the MOBI table already starts at 0x10.
        if (r0_len >= 0xc4) {
            uint32_t first = be16(r0 + 0xc0);
            if (first && first < count) head->first_text = first;
        }
        if (r0_len >= 0x84 && header_len >= 0x74) {
            head->first_image = be32(r0 + 0x6c);
            head->huff_offset = be32(r0 + 0x70);
            head->huff_count = be32(r0 + 0x74);
            head->exth_flags = be32(r0 + 0x80);
            if (head->first_image >= count) head->first_image = 0;
            if (head->huff_offset >= count) { head->huff_offset = 0; head->huff_count = 0; }
        }
        // 0xF4 指向由 ncx 生成的第一条 INDX 记录；书里没有正式目录时这里是 0xFFFFFFFF。
        // 0xF4 points at the first INDX record built from the ncx; 0xFFFFFFFF when there is none.
        if (r0_len >= 0xf8 && header_len >= 0xe4) {
            uint32_t ncx = be32(r0 + 0xf4);
            if (ncx != 0xFFFFFFFFu && ncx < count) head->ncx_index = ncx;
        }
        if (!head->first_image && head->text_records) {
            // 老文件不写首图记录：正文之后第一条 JPEG/PNG 就是图。/ Old files omit the field.
            size_t start = (size_t)head->first_text + head->text_records;
            size_t stop = start + MOBI_IMAGE_SCAN < count ? start + MOBI_IMAGE_SCAN : count;
            for (size_t i = start; i < stop; ++i) {
                uint8_t sig[2];
                if (table[i] + 2 > head->file_size) break;
                if (!read_at(file, table[i], sig, 2)) break;
                if ((sig[0] == 0xff && sig[1] == 0xd8) || (sig[0] == 0x89 && sig[1] == 0x50)) {
                    head->first_image = (uint32_t)i;
                    break;
                }
            }
        }
        if (r0_len >= 0x5c) {
            uint32_t at = be32(r0 + 0x54), bytes = be32(r0 + 0x58);
            if (at < r0_len && bytes && (uint64_t)at + bytes <= r0_len)
                head_text(head->title, sizeof(head->title), r0 + at, bytes, head->encoding);
        }
        // EXTH 紧跟 MOBI 头，以 "EXTH" 魔数校验；标志位只是提示，不能当唯一依据。
        // EXTH follows the MOBI header; the magic is the gate because the flag is only a hint.
        head_exth(head, r0, r0_len, 16u + header_len);
        err = ESP_OK;
    } while (0);
    free(r0);
    if (err != ESP_OK) { free(table); return err; }
    if (records) *records = table;
    else free(table);
    if (record_count) *record_count = count;
    return ESP_OK;
}

/* ---- 目录扫描 / Navigation scan ---- */

typedef void (*scan_fn)(void *ctx, const char *data, size_t len, uint32_t base);

/// 按记录流式扫描解压正文；每块带上前一块的尾巴，跨记录的标签才能读全。
/// Stream the decompressed text record by record, carrying the previous tail so split tags read whole.
static bool text_scan(book_mobi_t *book, uint32_t from, uint32_t limit, scan_fn fn, void *ctx) {
    if (limit > book->text_length) limit = book->text_length;
    if (from >= limit || !book->record_size) return false;
    size_t rs = book->record_size;
    size_t first = from / rs, last = ((size_t)limit - 1) / rs;
    uint8_t *window = psram(rs + MOBI_SCAN_CARRY);
    if (!window) return false;
    size_t carry = 0;
    bool any = false;
    for (size_t r = first; r <= last; ++r) {
        size_t n = text_record(book, (size_t)book->first_text + r, window + MOBI_SCAN_CARRY, rs);
        if (!n) break;
        uint64_t base = (uint64_t)r * rs;
        uint64_t start = base >= carry ? base - carry : 0;
        size_t head = (size_t)(base - start);
        fn(ctx, (const char *)window + MOBI_SCAN_CARRY - head, head + n, (uint32_t)start);
        any = true;
        carry = n < MOBI_SCAN_CARRY ? n : MOBI_SCAN_CARRY;
        memmove(window, window + MOBI_SCAN_CARRY + n - carry, carry);
    }
    free(window);
    return any;
}

/// 大小写无关的属性取值；MOBI 里的 filepos 常常不加引号。
/// Case-insensitive attribute lookup; MOBI usually leaves filepos unquoted.
static bool attr_span(const char *tag, size_t len, const char *name,
                      const char **value, size_t *value_len) {
    size_t name_len = strlen(name);
    for (size_t i = 0; i + name_len + 1 < len; ++i) {
        char prev = tag[i];
        if (prev != ' ' && prev != '\t' && prev != '\r' && prev != '\n' && prev != '<') continue;
        if (strncasecmp(tag + i + 1, name, name_len) != 0) continue;
        size_t at = i + 1 + name_len;
        while (at < len && (tag[at] == ' ' || tag[at] == '\t')) ++at;
        if (at >= len || tag[at] != '=') continue;
        ++at;
        while (at < len && (tag[at] == ' ' || tag[at] == '\t')) ++at;
        if (at >= len) return false;
        char quote = tag[at];
        size_t begin, end;
        if (quote == '"' || quote == '\'') {
            begin = ++at;
            while (at < len && tag[at] != quote) ++at;
            end = at;
        } else {
            begin = at;
            while (at < len && tag[at] != ' ' && tag[at] != '\t' && tag[at] != '>' && tag[at] != '/') ++at;
            end = at;
        }
        if (end <= begin) return false;
        *value = tag + begin;
        *value_len = end - begin;
        return true;
    }
    return false;
}

static bool attr_u32(const char *tag, size_t len, const char *name, uint32_t *out) {
    const char *value;
    size_t n;
    if (!attr_span(tag, len, name, &value, &n) || n > 10) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        v = v * 10 + (uint32_t)(value[i] - '0');
    }
    *out = v;
    return true;
}

static bool entity_at(const char *s, size_t n, size_t *used, uint32_t *cp) {
    static const struct { const char *name; uint32_t cp; } table[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", 0xa0}, {"ldquo", 0x201c}, {"rdquo", 0x201d}, {"mdash", 0x2014},
    };
    if (n < 3 || s[0] != '&') return false;
    size_t limit = n < 12 ? n : 12;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        size_t len = strlen(table[i].name);
        if (limit > len && !strncasecmp(s + 1, table[i].name, len) && s[1 + len] == ';') {
            *cp = table[i].cp;
            *used = len + 2;
            return true;
        }
    }
    if (s[1] == '#') {
        size_t at = 2, start;
        uint32_t v = 0;
        bool hex = false;
        if (at < limit && (s[at] == 'x' || s[at] == 'X')) { hex = true; ++at; }
        start = at;
        while (at < limit && s[at] != ';') {
            unsigned digit;
            char c = s[at];
            if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
            else if (hex && c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
            else if (hex && c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
            else return false;
            v = v * (hex ? 16 : 10) + digit;
            if (v > 0x10ffff) return false;
            ++at;
        }
        if (at >= limit || s[at] != ';' || at == start) return false;
        *cp = v ? v : 0x20;
        *used = at + 1;
        return true;
    }
    return false;
}

/// 标题清洗：去掉嵌套标签、折叠空白、解码实体，按 UTF-8 边界截断。
/// Clean a label: drop nested tags, collapse whitespace, decode entities, cut on a UTF-8 boundary.
static size_t title_clean(const char *src, size_t n, char *out, size_t cap) {
    size_t used = 0;
    bool gap = false;
    for (size_t i = 0; i < n;) {
        if (src[i] == '<') {
            size_t j = i + 1;
            while (j < n && src[j] != '>') ++j;
            i = j < n ? j + 1 : n;
            continue;
        }
        uint32_t cp;
        size_t step = 1;
        if (src[i] == '&') {
            size_t eaten = 0;
            if (entity_at(src + i, n - i, &eaten, &cp)) step = eaten;
            else cp = (uint8_t)src[i];
        } else if ((uint8_t)src[i] >= 0x80) {
            unsigned char c = (unsigned char)src[i];
            size_t width = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
            if (i + width > n) break;
            bool ok = true;
            for (size_t k = 1; k < width; ++k)
                if (((uint8_t)src[i + k] & 0xc0) != 0x80) ok = false;
            if (!ok) { ++i; continue; }
            if (gap && used) {
                if (used + 1 >= cap) break;
                out[used++] = ' ';
            }
            gap = false;
            if (used + width + 1 > cap) break;
            memcpy(out + used, src + i, width);
            used += width;
            i += width;
            continue;
        } else {
            cp = (uint8_t)src[i];
        }
        i += step;
        if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == 0xa0) {
            gap = used != 0;
            continue;
        }
        if (gap && used) {
            if (used + 1 >= cap) break;
            out[used++] = ' ';
            gap = false;
        }
        size_t width = utf8_put(out + used, cap - used - 1, cp);
        if (!width) break;
        used += width;
    }
    out[used] = 0;
    return used;
}

typedef struct {
    mobi_anchor_t *items;
    size_t count, cap;
    uint32_t limit;
} anchor_list_t;

static void anchor_add(anchor_list_t *list, uint32_t offset) {
    if (list->count >= MOBI_CHAPTER_MAX) return;
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 32;
        mobi_anchor_t *grown = heap_caps_realloc(list->items, cap * sizeof(*grown),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!grown) return;
        list->items = grown;
        list->cap = cap;
    }
    list->items[list->count].offset = offset;
    list->items[list->count].title = NULL;
    ++list->count;
}

static void anchors_free(anchor_list_t *list) {
    for (size_t i = 0; i < list->count; ++i) free((void *)list->items[i].title);
    free(list->items);
    list->items = NULL;
    list->count = list->cap = 0;
}

/// 收集 <a filepos=NNN>标题</a>；filepos 是正文中的绝对偏移。
/// Collect <a filepos=NNN>label</a>; filepos is an absolute offset into the text.
static void scan_anchor(void *ctx, const char *data, size_t len, uint32_t base) {
    (void)base; // filepos 本身就是绝对正文偏移。/ filepos is already an absolute text offset.
    anchor_list_t *list = ctx;
    for (size_t i = 0; i + 2 < len; ++i) {
        if (data[i] != '<') continue;
        char c = data[i + 1];
        if (c != 'a' && c != 'A') continue;
        char after = data[i + 2];
        if (after != ' ' && after != '\t' && after != '\r' && after != '\n') continue;
        size_t tag_end = i + 2;
        while (tag_end < len && data[tag_end] != '>') ++tag_end;
        if (tag_end >= len) break;
        uint32_t filepos;
        if (!attr_u32(data + i, tag_end - i + 1, "filepos", &filepos)) { i = tag_end; continue; }
        if (filepos >= list->limit) { i = tag_end; continue; }
        // 标题必须跟在同一个窗口里；被记录边界切开的由回看缓冲重新读到。
        // The label must sit in this window; a split one is re-read through the look-back buffer.
        size_t close = tag_end + 1;
        while (close + 3 < len && strncasecmp(data + close, "</a", 3) != 0) ++close;
        if (close + 3 >= len) { i = tag_end; continue; }
        char title[MOBI_TITLE_MAX + 1];
        size_t used = title_clean(data + tag_end + 1, close - tag_end - 1, title, sizeof(title));
        size_t slot = list->count;
        anchor_add(list, filepos);
        if (list->count > slot && used) {
            char *copy = psram(used + 1);
            if (copy) {
                memcpy(copy, title, used + 1);
                list->items[slot].title = copy;
            }
        }
        i = close + 2;
    }
}

static void scan_pagebreak(void *ctx, const char *data, size_t len, uint32_t base) {
    anchor_list_t *list = ctx;
    static const char needle[] = "mbp:pagebreak";
    const size_t want = sizeof(needle) - 1;
    for (size_t i = 0; i + want + 1 <= len; ++i) {
        const char *hit;
        if (data[i] == '<') hit = data + i + 1;
        else if (data[i] == 'm') hit = data + i;
        else continue;
        if (strncasecmp(hit, needle, want) != 0) continue;
        // 章节必须从 '<' 起，否则下一页会先冒出一截标签残骸。
        // A chapter must start at '<', or the next page opens with the leftover tag text.
        uint32_t offset = base + (uint32_t)(hit - data) - 1;
        if (!offset || offset >= list->limit) continue;
        anchor_add(list, offset);
        i += want;
    }
}

static int anchor_cmp(const void *a, const void *b) {
    const mobi_anchor_t *left = a, *right = b;
    if (left->offset == right->offset) return 0;
    return left->offset < right->offset ? -1 : 1;
}

/// guide 里的 <reference type="toc" filepos=N>；目录页通常就在那儿。
/// The guide's <reference type="toc" filepos=N>; the TOC page usually sits there.
static uint32_t guide_toc_offset(book_mobi_t *book) {
    uint32_t limit = book->text_length < 8192 ? book->text_length : 8192;
    if (!limit) return 0;
    uint8_t *head = psram(limit + 1);
    if (!head) return 0;
    size_t got = 0, rs = book->record_size;
    while (got < limit) {
        size_t n = text_record(book, (size_t)book->first_text + got / rs, book->scratch, rs);
        if (!n) break;
        size_t take = n < limit - got ? n : limit - got;
        memcpy(head + got, book->scratch, take);
        got += take;
        if (n < rs) break;
    }
    uint32_t found = 0;
    if (got) {
        head[got] = 0;
        const char *p = (const char *)head;
        const char *guide = strcasestr(p, "<guide");
        const char *end = guide ? strcasestr(guide, "</guide>") : NULL;
        if (guide && end) {
            const char *ref = guide;
            while (ref < end) {
                const char *tag = strcasestr(ref, "<reference");
                if (!tag || tag >= end) break;
                const char *close = memchr(tag, '>', (size_t)(end - tag));
                if (!close) break;
                const char *type;
                size_t type_len;
                if (attr_span(tag, (size_t)(close - tag), "type", &type, &type_len) &&
                    type_len == 3 && !strncasecmp(type, "toc", 3)) {
                    uint32_t offset;
                    if (attr_u32(tag, (size_t)(close - tag), "filepos", &offset)) found = offset;
                    break;
                }
                ref = close + 1;
            }
        }
    }
    free(head);
    return found;
}

/* ---- INDX/NCX 目录索引 / INDX/NCX navigation index ---- */

static bool chapters_build(book_mobi_t *book, anchor_list_t *anchors);

// MOBI 的正式目录不写在正文里，而是 ncx 编译成的 INDX 记录：主记录带 TAGX 标签表，
// 其后是若干条子 INDX（真正的条目与 IDXT 偏移表），再往后是 CTOC 字符串块。
// A MOBI TOC is not in the body: ncx compiles into INDX records - a main record with the TAGX
// table, one or more sub-INDX records holding the entries and their IDXT table, then CTOC strings.
typedef struct {
    uint8_t tag, values, mask, end;
} tagx_t;

typedef struct {
    uint32_t key; ///< CTOC 内字节偏移，跨块时按 0x10000 累加 / Byte offset inside CTOC, plus 0x10000 per block
    const char *text;
    uint32_t len; ///< 字符串长度；CTOC 是长度前缀，不是 NUL 结尾 / Length-prefixed, not NUL-terminated
} ctoc_t;

/// 变宽整数：7 位一组，最高位为 1 的字节是最后一个。
/// Variable width integer: 7 bits per byte, the byte with bit 7 set terminates it.
static bool vwi_read(const uint8_t *data, size_t len, size_t *at, uint32_t *out) {
    uint32_t value = 0;
    size_t i = *at;
    for (unsigned n = 0; n < 5 && i < len; ++n) {
        uint8_t byte = data[i++];
        value = (value << 7) | (byte & 0x7f);
        if (byte & 0x80) {
            *at = i;
            *out = value;
            return true;
        }
    }
    return false;
}

static unsigned bit_count(uint8_t value) {
    unsigned count = 0;
    while (value) { count += value & 1u; value >>= 1; }
    return count;
}

typedef struct {
    bool ok;
    uint32_t len;   ///< TAGX 起点 / Start of the TAGX block
    uint32_t start; ///< IDXT 起点 / Start of the IDXT block
    uint32_t count; ///< 后续子记录数或条目数 / Following records, or entry count
    uint32_t nctoc; ///< CTOC 记录数 / Number of CTOC records
} indx_head_t;

static indx_head_t indx_head(const uint8_t *data, size_t len) {
    indx_head_t head = {0};
    if (len < 0x38 || memcmp(data, "INDX", 4) != 0) return head;
    head.len = be32(data + 4);
    head.start = be32(data + 20);
    head.count = be32(data + 24);
    head.nctoc = be32(data + 52);
    head.ok = head.len < len;
    return head;
}

/// 在一条子记录里按 TAGX 走完所有标签，取回 pos/noffs/hlvl。
/// Walk every tag of one entry through the TAGX control bytes and keep pos/noffs/hlvl.
static bool index_entry(const uint8_t *data, size_t len, size_t begin, size_t end,
                        const tagx_t *tags, size_t ntags, size_t control_bytes,
                        uint32_t *pos, uint32_t *noffs, unsigned *level) {
    if (begin >= len || end > len || begin >= end) return false;
    size_t text_len = data[begin];
    size_t control = begin + 1 + text_len; ///< 控制位从条目正文之后开始 / Control bytes follow the name
    if (control > end) return false;
    size_t data_at = control + control_bytes;
    size_t ctrl_index = 0;
    for (size_t i = 0; i < ntags; ++i) {
        if (tags[i].end) { ++ctrl_index; continue; }
        if (control + ctrl_index >= end) return false;
        uint8_t raw = data[control + ctrl_index];
        uint8_t value = raw & tags[i].mask;
        if (!value) continue;
        size_t values = 0;
        bool counted = false;
        if (value == tags[i].mask) {
            if (bit_count(tags[i].mask) > 1) {
                // 掩码全置位且含多位：后面先跟一个变宽整数，表示接下来占多少字节。
                // All mask bits set and multi-bit: a VWI follows with the byte count of the values.
                uint32_t bytes = 0;
                if (!vwi_read(data, len, &data_at, &bytes)) return false;
                size_t consumed = 0;
                while (consumed < bytes) {
                    size_t before = data_at;
                    uint32_t ignored = 0;
                    if (!vwi_read(data, len, &data_at, &ignored)) return false;
                    consumed += data_at - before;
                    ++values;
                }
                counted = true;
            } else {
                values = 1;
                counted = true;
            }
        } else {
            // 把掩码右移到最低位，得到这一项实际有几个值。
            // Shift the mask down to bit 0 to get the real value count.
            uint8_t mask = tags[i].mask;
            uint8_t shifted = value;
            while (!(mask & 1u)) { mask >>= 1; shifted >>= 1; }
            values = (size_t)shifted * (tags[i].values ? tags[i].values : 1);
            counted = true;
        }
        for (size_t v = 0; counted && v < values; ++v) {
            for (unsigned repeat = 0; repeat < (tags[i].values ? tags[i].values : 1); ++repeat) {
                uint32_t number = 0;
                if (!vwi_read(data, len, &data_at, &number)) return false;
                if (v == 0 && repeat == 0) {
                    if (tags[i].tag == 1) *pos = number;
                    else if (tags[i].tag == 3) *noffs = number;
                    else if (tags[i].tag == 4) *level = number;
                }
            }
        }
    }
    return true;
}

static int ctoc_cmp(const void *a, const void *b) {
    const ctoc_t *left = a, *right = b;
    if (left->key == right->key) return 0;
    return left->key < right->key ? -1 : 1;
}

/// 目录标题按书内编码转成 UTF-8，并把坏字节换成 U+FFFD。
/// Convert a TOC label to UTF-8 in the book encoding, replacing invalid bytes with U+FFFD.
static size_t label_utf8(uint32_t encoding, const uint8_t *src, size_t len, char *out, size_t cap) {
    size_t used = 0;
    if (!cap) return 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        size_t step = 1;
        if (encoding != 65001) {
            cp = cp1252(src[i]);
        } else if (src[i] < 0x80) {
            cp = src[i];
        } else {
            size_t width = src[i] >= 0xf0 ? 4 : src[i] >= 0xe0 ? 3 : src[i] >= 0xc0 ? 2 : 0;
            bool valid = width && i + width <= len;
            for (size_t k = 1; valid && k < width; ++k)
                if ((src[i + k] & 0xc0) != 0x80) valid = false;
            if (!valid) { cp = 0xfffd; }
            else {
                if (used + width + 1 > cap) break;
                memcpy(out + used, src + i, width);
                used += width;
                i += width;
                continue;
            }
        }
        size_t width = utf8_put(out + used, cap - used - 1, cp);
        if (!width) break;
        used += width;
        i += step;
    }
    out[used] = 0;
    return used;
}

/// 把 NCX 目录读成章节表；失败时返回 false，交给 filepos 与分页兜底。
/// Read the NCX into the chapter list; false lets the filepos and pagebreak paths take over.
static bool chapters_from_ncx(book_mobi_t *book) {
    if (!book->ncx_index) return false;
    size_t idx_len = 0;
    uint8_t *main = record_bytes(book, book->ncx_index, &idx_len);
    if (!main) return false;
    indx_head_t head = indx_head(main, idx_len);
    if (!head.ok) { free(main); return false; }
    if (head.len + 12 > idx_len || memcmp(main + head.len, "TAGX", 4) != 0) { free(main); return false; }
    uint32_t tagx_end = be32(main + head.len + 4);
    uint32_t control_bytes = be32(main + head.len + 8);
    if (tagx_end < 12 || head.len + tagx_end > idx_len || !control_bytes || control_bytes > 4) {
        free(main);
        return false;
    }
    size_t tag_count = (tagx_end - 12) / 4;
    tagx_t tags[16];
    if (tag_count > sizeof(tags) / sizeof(tags[0])) tag_count = sizeof(tags) / sizeof(tags[0]);
    for (size_t i = 0; i < tag_count; ++i) {
        const uint8_t *raw = main + head.len + 12 + i * 4;
        tags[i] = (tagx_t){raw[0], raw[1], raw[2], raw[3]};
    }

    // CTOC：字符串块接在全部子记录之后，每块 key 增加 0x10000。
    // CTOC blocks follow every sub-record; each block shifts the key by 0x10000.
    if (head.nctoc > 8) { free(main); return false; }
    uint8_t *sources[8] = {0};
    size_t source_len[8] = {0};
    ctoc_t *map = NULL;
    size_t map_count = 0, map_cap = 0;
    bool ok = true;
    for (uint32_t j = 0; ok && j < head.nctoc; ++j) {
        size_t block = (size_t)book->ncx_index + head.count + 1 + j;
        sources[j] = record_bytes(book, block, &source_len[j]);
        if (!sources[j]) { ok = false; break; }
        size_t at = 0;
        while (at < source_len[j] && sources[j][at]) {
            size_t begin = at;
            uint32_t name_len = 0;
            if (!vwi_read(sources[j], source_len[j], &at, &name_len)) { ok = false; break; }
            if ((uint64_t)at + name_len > source_len[j]) { ok = false; break; }
            if (map_count == map_cap) {
                size_t cap = map_cap ? map_cap * 2 : 256;
                ctoc_t *grown = heap_caps_realloc(map, cap * sizeof(*grown),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!grown) { ok = false; break; }
                map = grown;
                map_cap = cap;
            }
            map[map_count].key = (uint32_t)begin + j * 0x10000u;
            map[map_count].text = (const char *)sources[j] + at;
            map[map_count].len = name_len;
            ++map_count;
            at += name_len;
        }
    }
    if (ok && map_count) qsort(map, map_count, sizeof(*map), ctoc_cmp);

    anchor_list_t list = {NULL, 0, 0, book->text_length};
    for (uint32_t r = 1; ok && r <= head.count; ++r) {
        size_t sub_len = 0;
        uint8_t *sub = record_bytes(book, (size_t)book->ncx_index + r, &sub_len);
        if (!sub) { ok = false; break; }
        indx_head_t sh = indx_head(sub, sub_len);
        if (!sh.ok || !sh.count || sh.start + 4 > sub_len) { free(sub); ok = false; break; }
        if ((uint64_t)sh.start + 4 + (uint64_t)sh.count * 2 > sub_len) { free(sub); ok = false; break; }
        for (uint32_t e = 0; e < sh.count && ok; ++e) {
            size_t begin = be16(sub + sh.start + 4 + (size_t)e * 2);
            size_t end = e + 1 < sh.count ? be16(sub + sh.start + 4 + (size_t)(e + 1) * 2) : sh.start;
            uint32_t pos = 0, noffs = 0;
            unsigned level = 0;
            if (!index_entry(sub, sub_len, begin, end, tags, tag_count, control_bytes,
                             &pos, &noffs, &level)) continue;
            if (!map_count) continue;
            ctoc_t key = {noffs, NULL, 0};
            const ctoc_t *found = bsearch(&key, map, map_count, sizeof(*map), ctoc_cmp);
            if (!found) continue;
            char label[MOBI_TITLE_MAX + 1];
            size_t used = label_utf8(book->encoding, (const uint8_t *)found->text, found->len,
                                     label, sizeof(label));
            if (!used || pos >= book->text_length) continue;
            size_t slot = list.count;
            anchor_add(&list, pos);
            if (list.count > slot) {
                char *copy = psram(used + 1);
                if (copy) {
                    memcpy(copy, label, used + 1);
                    list.items[slot].title = copy;
                }
            }
        }
        free(sub);
    }
    free(main);
    for (unsigned j = 0; j < 8; ++j) free(sources[j]);
    free(map);
    bool built = ok && list.count >= 2 && chapters_build(book, &list);
    anchors_free(&list);
    return built;
}

/* ---- 章节索引 / Chapter index ---- */

static void chapters_free(mobi_chapter_t *chapters, size_t count) {
    if (!chapters) return;
    for (size_t i = 0; i < count; ++i) free(chapters[i].title);
    free(chapters);
}

/// 把锚点排序去重后变成章节区间；无标题的按序号补名。
/// Turn sorted, de-duplicated anchors into chapter ranges; unlabelled ones get an ordinal.
static bool chapters_build(book_mobi_t *book, anchor_list_t *anchors) {
    if (!anchors->count) return false;
    qsort(anchors->items, anchors->count, sizeof(*anchors->items), anchor_cmp);
    size_t unique = 0;
    for (size_t i = 0; i < anchors->count; ++i) {
        if (unique && anchors->items[unique - 1].offset == anchors->items[i].offset) {
            if (!anchors->items[unique - 1].title && anchors->items[i].title) {
                anchors->items[unique - 1].title = anchors->items[i].title;
            } else {
                free((void *)anchors->items[i].title);
            }
            anchors->items[i].title = NULL;
            continue;
        }
        if (unique != i) {
            anchors->items[unique] = anchors->items[i];
            // 搬运后原槽位不再拥有标题，否则 anchors_free 会二次释放。
            // The source slot no longer owns the title, or anchors_free would free it twice.
            anchors->items[i].title = NULL;
        }
        ++unique;
    }
    // 压缩后尾部是失效副本，只保留前 unique 条的所有权。
    // The tail holds stale copies; only the first `unique` slots own titles now.
    for (size_t i = unique; i < anchors->count; ++i) anchors->items[i].title = NULL;
    anchors->count = unique;
    if (!unique) return false;
    mobi_chapter_t *chapters = psram(unique * sizeof(*chapters));
    if (!chapters) return false;
    memset(chapters, 0, unique * sizeof(*chapters));
    size_t built = 0;
    for (size_t i = 0; i < unique; ++i) {
        uint32_t begin = anchors->items[i].offset;
        uint32_t end = i + 1 < unique ? anchors->items[i + 1].offset : book->text_length;
        const char *label = anchors->items[i].title;
        char ordinal[24];
        if (begin >= end) continue;
        if (!label || !*label) {
            snprintf(ordinal, sizeof(ordinal), "第 %u 节", (unsigned)(built + 1));
            label = ordinal;
        }
        size_t n = strlen(label);
        chapters[built].title = psram(n + 1);
        if (!chapters[built].title) break;
        memcpy(chapters[built].title, label, n + 1);
        chapters[built].begin = begin;
        chapters[built].end = end;
        ++built;
    }
    if (!built) { free(chapters); return false; }
    book->chapters = chapters;
    book->chapter_count = built;
    return true;
}

/// 无目录时按 <mbp:pagebreak> 切分，至少让长书不会整本压成一章。
/// Without a TOC, split on <mbp:pagebreak> so a long book is not a single chapter.
static bool chapters_from_pagebreaks(book_mobi_t *book) {
    anchor_list_t list = {NULL, 0, 0, book->text_length};
    bool ok = false;
    if (text_scan(book, 0, book->text_length, scan_pagebreak, &list) && list.count >= 2) {
        // 书前资料够长才单列一章；短短的扉页并进第一章。/ Keep short front matter inside chapter one.
        uint32_t first = list.items[0].offset;
        for (size_t i = 1; i < list.count; ++i)
            if (list.items[i].offset < first) first = list.items[i].offset;
        if (first > 1024) anchor_add(&list, 0);
        ok = chapters_build(book, &list);
    }
    anchors_free(&list);
    return ok;
}

/// 从 at 向后找最近的标签起点；落在记录边界上时不会把标签劈成两半。
/// Move forward from `at` to the next tag start so a cut never splits a tag.
static uint32_t chunk_boundary(book_mobi_t *book, uint32_t at) {
    size_t rs = book->record_size;
    for (unsigned pass = 0; pass < 3; ++pass) {
        size_t record = (size_t)(at / rs) + pass;
        size_t n = text_record(book, (size_t)book->first_text + record, book->scratch, rs);
        if (!n) break;
        uint32_t base = (uint32_t)(record * rs);
        for (size_t i = at > base ? (size_t)(at - base) : 0; i < n; ++i)
            if (book->scratch[i] == '<') return base + (uint32_t)i;
    }
    return at;
}

/// 既无目录也无分页标记时按固定跨度切分，超长正文也不会压成一章读不出来。
/// With neither a TOC nor page breaks, cut fixed spans so an oversized body still loads.
static bool chapters_fallback(book_mobi_t *book) {
    const uint32_t span = 512u * 1024u;
    size_t count = ((size_t)book->text_length + span - 1) / span;
    if (!count) count = 1;
    if (count > MOBI_CHAPTER_MAX) count = MOBI_CHAPTER_MAX;
    mobi_chapter_t *chapters = psram(count * sizeof(*chapters));
    if (!chapters) return false;
    memset(chapters, 0, count * sizeof(*chapters));
    size_t built = 0;
    uint32_t begin = 0;
    while (built < count && begin < book->text_length) {
        uint32_t end = book->text_length;
        if (built + 1 < count) {
            uint32_t at = (uint32_t)((uint64_t)(built + 1) * span);
            if (at > begin && at < end) end = chunk_boundary(book, at);
        }
        if (end <= begin) end = book->text_length;
        char label[MOBI_TITLE_MAX + 1];
        if (count == 1 && book->book_title && strlen(book->book_title) <= 32)
            snprintf(label, sizeof(label), "%s", book->book_title);
        else if (count == 1)
            snprintf(label, sizeof(label), "正文");
        else
            snprintf(label, sizeof(label), "第 %u 节", (unsigned)(built + 1));
        size_t n = strlen(label);
        chapters[built].title = psram(n + 1);
        if (!chapters[built].title) break;
        memcpy(chapters[built].title, label, n + 1);
        chapters[built].begin = begin;
        chapters[built].end = end;
        ++built;
        if (end >= book->text_length) break;
        begin = end;
    }
    if (!built) { free(chapters); return false; }
    book->chapters = chapters;
    book->chapter_count = built;
    return true;
}

static bool chapters_scan(book_mobi_t *book) {
    // 先读 ncx 编译出的 INDX；这是其它阅读器显示真实章节名的来源。
    // Read the INDX built from the ncx first: that is where other readers get real chapter names.
    if (chapters_from_ncx(book)) return true;
    uint32_t toc = guide_toc_offset(book);
    if (toc && toc < book->text_length) {
        anchor_list_t list = {NULL, 0, 0, book->text_length};
        uint64_t limit = (uint64_t)toc + MOBI_TOC_WINDOW;
        if (limit > book->text_length) limit = book->text_length;
        text_scan(book, toc, (uint32_t)limit, scan_anchor, &list);
        bool ok = list.count >= 2 && chapters_build(book, &list);
        anchors_free(&list);
        if (ok) return true;
    }
    {
        anchor_list_t list = {NULL, 0, 0, book->text_length};
        uint32_t limit = book->text_length < MOBI_FRONT_WINDOW ? book->text_length : MOBI_FRONT_WINDOW;
        text_scan(book, 0, limit, scan_anchor, &list);
        bool ok = list.count >= 1 && chapters_build(book, &list);
        anchors_free(&list);
        if (ok) return true;
    }
    // 目录页也可能在书末且没有 guide：整本扫一遍锚点，再决定要不要退到分页。
    // The TOC page can also sit at the end with no guide: scan the whole text before falling back.
    if (book->text_length <= (4u << 20)) {
        anchor_list_t list = {NULL, 0, 0, book->text_length};
        text_scan(book, 0, book->text_length, scan_anchor, &list);
        bool ok = list.count >= 2 && chapters_build(book, &list);
        anchors_free(&list);
        if (ok) return true;
    }
    if (chapters_from_pagebreaks(book)) return true;
    return chapters_fallback(book);
}

/* ---- HTML 规整 / HTML normalization ---- */

static bool name_is(const char *tag, size_t len, const char *name) {
    return strlen(name) == len && !strncasecmp(tag, name, len);
}

/// MOBI7 正文规整：recindex 改成 src、align 转成内联样式、丢掉 mbp 私有标签。
/// Normalize MOBI7 markup: recindex becomes src, align becomes an inline style, mbp tags are dropped.
static esp_err_t html_normalize(book_mobi_t *book, const uint8_t *src, size_t len,
                                uint8_t **out, size_t *out_len, size_t limit) {
    buf_t sink = {NULL, 0, 0, limit, false};
    if (!buf_reserve(&sink, len / 2 + 256)) return ESP_ERR_NO_MEM;
    bool ok = true;
    for (size_t i = 0; ok && i < len;) {
        if (src[i] == '<') {
            size_t j = i + 1;
            while (j < len && src[j] != '>' && j - i < 2048) ++j;
            if (j >= len || src[j] != '>') {
                // 没有闭合的标签按正文输出，不让它吞掉整章。/ An unterminated tag is emitted as text.
                ok = buf_add(&sink, src + i, len - i);
                break;
            }
            const char *tag = (const char *)src + i + 1;
            size_t tag_len = j - i - 1;
            bool closing = tag_len && tag[0] == '/';
            const char *name = closing ? tag + 1 : tag;
            size_t name_len = 0;
            while (name_len < tag_len && (isalnum((unsigned char)name[name_len]) ||
                                          name[name_len] == ':' || name[name_len] == '-' ||
                                          name[name_len] == '!'))
                ++name_len;
            if (!closing && name_is(name, name_len, "mbp:pagebreak")) {
                i = j + 1;
                continue;
            }
            bool image = !closing && (name_is(name, name_len, "img") || name_is(name, name_len, "image"));
            bool block = !closing && (name_is(name, name_len, "p") || name_is(name, name_len, "div") ||
                                      name_is(name, name_len, "h1") || name_is(name, name_len, "h2") ||
                                      name_is(name, name_len, "h3"));
            const char *extra = NULL;
            size_t extra_len = 0;
            char extra_buf[48];
            if (image) {
                const char *value;
                size_t value_len;
                if (!attr_span(tag, tag_len, "src", &value, &value_len)) {
                    if (attr_span(tag, tag_len, "recindex", &value, &value_len) && value_len <= 12) {
                        // recindex 指向首图记录后的第 N 条；记成 src="rec:N"，图片加载再换算。
                        // recindex points N records past the first image; kept as src="rec:N".
                        int written = snprintf(extra_buf, sizeof(extra_buf), " src=\"rec:%.*s\"",
                                               (int)value_len, value);
                        if (written > 0 && (size_t)written < sizeof(extra_buf)) {
                            extra = extra_buf;
                            extra_len = (size_t)written;
                        }
                    }
                }
            } else if (block) {
                const char *value;
                size_t value_len;
                if (!attr_span(tag, tag_len, "style", &value, &value_len) &&
                    attr_span(tag, tag_len, "align", &value, &value_len) &&
                    (value_len == 6 || value_len == 5 || value_len == 7)) {
                    if (!strncasecmp(value, "center", 6) || !strncasecmp(value, "right", 5) ||
                        !strncasecmp(value, "justify", 7)) {
                        int written = snprintf(extra_buf, sizeof(extra_buf), " style=\"text-align:%.*s\"",
                                               (int)value_len, value);
                        if (written > 0 && (size_t)written < sizeof(extra_buf)) {
                            extra = extra_buf;
                            extra_len = (size_t)written;
                        }
                    }
                }
            }
            if (extra) {
                ok = buf_byte(&sink, '<') && buf_add(&sink, name, name_len) &&
                     buf_add(&sink, extra, extra_len) &&
                     buf_add(&sink, name + name_len, tag_len - name_len) && buf_byte(&sink, '>');
            } else {
                ok = buf_add(&sink, src + i, j - i + 1);
            }
            i = j + 1;
            continue;
        }
        uint8_t c = src[i];
        if (c < 0x80) {
            ok = buf_byte(&sink, (char)c);
            ++i;
            continue;
        }
        if (book->encoding == 65001) {
            size_t width = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
            bool valid = width > 1 && i + width <= len;
            for (size_t k = 1; valid && k < width; ++k)
                if ((src[i + k] & 0xc0) != 0x80) valid = false;
            if (valid) {
                ok = buf_add(&sink, src + i, width);
                i += width;
            } else {
                static const char replacement[3] = {(char)0xef, (char)0xbf, (char)0xbd};
                ok = buf_add(&sink, replacement, 3);
                ++i;
            }
            continue;
        }
        char utf8[4];
        size_t width = utf8_put(utf8, sizeof(utf8), cp1252(c));
        if (!width) { ok = false; break; }
        ok = buf_add(&sink, utf8, width);
        ++i;
    }
    if (!ok || sink.failed) { free(sink.data); return ESP_ERR_NO_MEM; }
    *out = sink.data;
    *out_len = sink.len;
    return ESP_OK;
}

/* ---- 公共接口 / Public interface ---- */

esp_err_t book_mobi_open(const char *path, book_mobi_t **out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!path || !*path || strlen(path) >= MOBI_PATH_MAX) return ESP_ERR_INVALID_ARG;
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    mobi_head_t head;
    uint32_t *records = NULL;
    size_t record_count = 0;
    esp_err_t err = head_parse(file, &head, &records, &record_count);
    if (err != ESP_OK) { fclose(file); return err; }
    book_mobi_t *book = psram(sizeof(*book));
    if (!book) { free(records); fclose(file); return ESP_ERR_NO_MEM; }
    memset(book, 0, sizeof(*book));
    snprintf(book->path, sizeof(book->path), "%s", path);
    book->file = file;
    book->file_size = head.file_size;
    book->records = records;
    book->record_count = record_count;
    book->compression = head.compression;
    book->record_size = head.record_size;
    book->encryption = head.encryption;
    book->text_length = head.text_length;
    book->first_text = head.first_text;
    book->first_image = head.first_image;
    book->encoding = head.encoding;
    book->huff_offset = head.huff_offset;
    book->huff_count = head.huff_count;
    book->ncx_index = head.ncx_index;
    book->scratch = psram(head.record_size);
    if (head.title[0]) {
        size_t n = strlen(head.title);
        book->book_title = psram(n + 1);
        if (book->book_title) memcpy(book->book_title, head.title, n + 1);
    }
    if (head.author[0]) {
        size_t n = strlen(head.author);
        book->book_author = psram(n + 1);
        if (book->book_author) memcpy(book->book_author, head.author, n + 1);
    }
    if (!book->scratch || !chapters_scan(book)) {
        ESP_LOGW(TAG, "unusable MOBI layout: %s", path);
        book_mobi_close(book);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "opened %s compression=%u chapters=%u title=%s", path, (unsigned)book->compression,
             (unsigned)book->chapter_count, book->book_title ? book->book_title : "-");
    *out = book;
    return ESP_OK;
}

void book_mobi_close(book_mobi_t *book) {
    if (!book) return;
    if (book->file) fclose(book->file);
    free(book->records);
    free(book->scratch);
    free(book->book_title);
    free(book->book_author);
    chapters_free(book->chapters, book->chapter_count);
    free(book->dict);
    for (size_t i = 0; i < book->pool_count; ++i) free(book->pool[i]);
    free(book->pool);
    free(book);
}

size_t book_mobi_chapter_count(const book_mobi_t *book) {
    return book ? book->chapter_count : 0;
}

esp_err_t book_mobi_chapter_title(book_mobi_t *book, size_t index, char *buf, size_t cap) {
    if (!book || !buf || !cap) return ESP_ERR_INVALID_ARG;
    if (index >= book->chapter_count) return ESP_ERR_INVALID_ARG;
    size_t n = strlen(book->chapters[index].title);
    if (n >= cap) return ESP_ERR_INVALID_SIZE;
    memcpy(buf, book->chapters[index].title, n + 1);
    return ESP_OK;
}

/// 解压 [begin, end) 区间；只碰覆盖到的记录。/ Inflate [begin, end), touching only covering records.
static esp_err_t text_slice(book_mobi_t *book, uint32_t begin, uint32_t end,
                            uint8_t **data, size_t *len) {
    *data = NULL;
    *len = 0;
    if (!book || begin >= end) return ESP_ERR_INVALID_ARG;
    if (end > book->text_length) end = book->text_length;
    if (begin >= end) return ESP_ERR_INVALID_ARG;
    size_t need = end - begin;
    uint8_t *buffer = psram(need);
    if (!buffer) return ESP_ERR_NO_MEM;
    size_t rs = book->record_size;
    size_t first = begin / rs, last = ((size_t)end - 1) / rs;
    size_t wrote = 0;
    for (size_t r = first; r <= last && wrote < need; ++r) {
        size_t n = text_record(book, (size_t)book->first_text + r, book->scratch, rs);
        if (!n) break;
        uint64_t base = (uint64_t)r * rs;
        size_t skip = begin > base ? (size_t)(begin - base) : 0;
        if (skip >= n) continue;
        size_t take = n - skip;
        if (take > need - wrote) take = need - wrote;
        memcpy(buffer + wrote, book->scratch + skip, take);
        wrote += take;
    }
    if (!wrote) { free(buffer); return ESP_ERR_INVALID_STATE; }
    *data = buffer;
    *len = wrote;
    return ESP_OK;
}

esp_err_t book_mobi_load(book_mobi_t *book, size_t index, html_text_t *out) {
    if (!book || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (index >= book->chapter_count) return ESP_ERR_INVALID_ARG;
    const mobi_chapter_t *chapter = &book->chapters[index];
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    esp_err_t err = text_slice(book, chapter->begin, chapter->end, &raw, &raw_len);
    if (err != ESP_OK) return err;
    size_t limit = raw_len * 3 + 4096;
    if (limit > HTML_TEXT_MAX_BYTES) limit = HTML_TEXT_MAX_BYTES;
    uint8_t *html = NULL;
    size_t html_len = 0;
    err = html_normalize(book, raw, raw_len, &html, &html_len, limit);
    free(raw);
    if (err != ESP_OK) return err;
    err = html_to_blocks((const char *)html, html_len, out);
    free(html);
    return err;
}

uint32_t book_mobi_total_bytes(const book_mobi_t *book) { return book ? book->text_length : 0; }

uint32_t book_mobi_chapter_byte_offset(const book_mobi_t *book, size_t index) {
    if (!book || index >= book->chapter_count) return 0;
    return book->chapters[index].begin;
}

/// src 形如 "rec:00012"（本模块改写）或裸数字。/ src is "rec:00012" as rewritten here, or bare digits.
static bool image_record_index(book_mobi_t *book, const char *src, uint32_t *index) {
    if (!book || !src || !book->first_image) return false;
    if (!strncasecmp(src, "rec:", 4)) src += 4;
    uint32_t value = 0;
    size_t digits = 0;
    for (; src[digits]; ++digits) {
        if (src[digits] < '0' || src[digits] > '9' || digits > 6) return false;
        value = value * 10 + (uint32_t)(src[digits] - '0');
    }
    if (!digits || !value) return false;
    uint64_t record = (uint64_t)book->first_image + value - 1;
    if (record >= book->record_count) return false;
    *index = (uint32_t)record;
    return true;
}

esp_err_t book_mobi_image(book_mobi_t *book, const char *src, uint8_t **data, size_t *size, bool *png) {
    if (!data || !size) return ESP_ERR_INVALID_ARG;
    *data = NULL;
    *size = 0;
    uint32_t record;
    if (!image_record_index(book, src, &record)) return ESP_ERR_NOT_FOUND;
    size_t n = 0;
    uint8_t *bytes = record_bytes(book, record, &n);
    if (!bytes) return ESP_ERR_NOT_FOUND;
    bool is_png = n > 4 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G';
    bool is_jpeg = n > 2 && bytes[0] == 0xff && bytes[1] == 0xd8;
    if (!is_png && !is_jpeg) { free(bytes); return ESP_ERR_NOT_SUPPORTED; }
    if (png) *png = is_png;
    *data = bytes;
    *size = n;
    return ESP_OK;
}

esp_err_t book_mobi_image_dimensions(book_mobi_t *book, const char *src, unsigned *width,
                                     unsigned *height) {
    if (!width || !height) return ESP_ERR_INVALID_ARG;
    uint8_t *data = NULL;
    size_t size = 0;
    bool png = false;
    esp_err_t err = book_mobi_image(book, src, &data, &size, &png);
    if (err != ESP_OK) return err;
    bool ok = book_image_dimensions(data, size, png, width, height);
    free(data);
    return ok ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

/// 打开文件并只解析头部，供封面与元数据使用。/ Open a file and parse the header only.
static esp_err_t head_open(const char *path, FILE **file, mobi_head_t *head,
                           uint32_t **records, size_t *record_count) {
    *file = fopen(path, "rb");
    if (!*file) return ESP_ERR_NOT_FOUND;
    esp_err_t err = head_parse(*file, head, records, record_count);
    if (err != ESP_OK) { fclose(*file); *file = NULL; }
    return err;
}

esp_err_t book_mobi_cover_bounded(const char *path, uint8_t **data, size_t *size,
                                  bool *is_png, size_t budget) {
    if (!data || !size) return ESP_ERR_INVALID_ARG;
    *data = NULL;
    *size = 0;
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    FILE *file = NULL;
    mobi_head_t head;
    uint32_t *records = NULL;
    size_t record_count = 0;
    esp_err_t err = head_open(path, &file, &head, &records, &record_count);
    if (err != ESP_OK) return err;
    mobi_io_t io;
    memset(&io, 0, sizeof(io));
    io.file = file;
    io.file_size = head.file_size;
    io.records = records;
    io.record_count = record_count;
    err = ESP_ERR_NOT_FOUND;
    uint32_t offset = 0;
    if (head.has_cover) offset = head.cover_offset;
    else if (head.has_thumb) offset = head.thumb_offset;
    if (head.first_image && (head.has_cover || head.has_thumb)) {
        uint64_t record = (uint64_t)head.first_image + offset;
        if (record < record_count) {
            size_t n = 0;
            uint8_t *bytes = io_record(&io, (size_t)record, &n);
            if (bytes) {
                bool png = n > 4 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N';
                bool jpeg = n > 2 && bytes[0] == 0xff && bytes[1] == 0xd8;
                if ((png || jpeg) && (!budget || n <= budget)) {
                    if (is_png) *is_png = png;
                    *data = bytes;
                    *size = n;
                    err = ESP_OK;
                } else {
                    free(bytes);
                    err = (png || jpeg) ? ESP_ERR_NO_MEM : ESP_ERR_NOT_FOUND;
                }
            }
        }
    }
    free(records);
    fclose(file);
    return err;
}

esp_err_t book_mobi_cover(const char *path, uint8_t **data, size_t *size, bool *is_png) {
    return book_mobi_cover_bounded(path, data, size, is_png, 0);
}

esp_err_t book_mobi_metadata(const char *path, char *title, size_t title_cap,
                             char *author, size_t author_cap) {
    if (title && title_cap) title[0] = 0;
    if (author && author_cap) author[0] = 0;
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    FILE *file = NULL;
    mobi_head_t head;
    uint32_t *records = NULL;
    size_t record_count = 0;
    esp_err_t err = head_open(path, &file, &head, &records, &record_count);
    free(records);
    if (err == ESP_OK) fclose(file);
    if (err != ESP_OK) return err;
    if (!head.title[0] && !head.author[0]) return ESP_ERR_NOT_FOUND;
    if (title && title_cap) snprintf(title, title_cap, "%s", head.title);
    if (author && author_cap) snprintf(author, author_cap, "%s", head.author);
    return ESP_OK;
}

#if KIIKO_BOARD_METALIO47
#define MOBI_IO_PATH 512

static char s_io_path[MOBI_IO_PATH];
static bool s_io_paused;
static struct stat s_io_stat;

esp_err_t book_mobi_suspend_io(book_mobi_t *book) {
    if (!book || !book->file || s_io_paused) return ESP_OK;
    if (fstat(fileno(book->file), &s_io_stat)) return ESP_FAIL;
    snprintf(s_io_path, sizeof(s_io_path), "%s", book->path);
    if (fclose(book->file)) return ESP_FAIL;
    book->file = NULL;
    s_io_paused = true;
    return ESP_OK;
}

esp_err_t book_mobi_resume_io(book_mobi_t *book) {
    if (!book || !s_io_paused) return ESP_OK;
    FILE *file = fopen(s_io_path, "rb");
    struct stat now;
    if (!file) return ESP_FAIL;
    if (fstat(fileno(file), &now) || now.st_size != s_io_stat.st_size ||
        now.st_mtime != s_io_stat.st_mtime) {
        fclose(file);
        return ESP_FAIL;
    }
    book->file = file;
    book->file_size = (uint64_t)now.st_size;
    s_io_paused = false;
    return ESP_OK;
}
#else
#endif
