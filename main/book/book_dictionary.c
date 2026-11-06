/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：校验定长索引与独立压缩词条，所有偏移和长度先检查再读。
 * English: Validate fixed index rows and independent compressed entries before reading offsets. */
#include "book_dictionary.h"
#include "reader_selection.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t u32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static unsigned u16(const unsigned char *p) { return p[0] | (unsigned)p[1]<<8; }
static bool read_at(FILE *f, uint32_t off, void *p, size_t n) { return !fseek(f, (long)off, SEEK_SET) && fread(p, 1, n, f) == n; }
static bool range(uint32_t off, uint32_t n, uint32_t end) { return off <= end && n <= end - off; }
book_dictionary_status_t book_dictionary_lookup(const char *path, const char *query,
    book_dictionary_result_t *r, book_dictionary_cancel_fn cancelled, void *context) {
    if (!r) return BOOK_DICTIONARY_CORRUPT;
    memset(r, 0, sizeof(*r)); r->status = BOOK_DICTIONARY_CORRUPT;
    size_t qn = query ? strnlen(query, 129) : 0;
    if (!path || !qn || qn > 128 || !reader_selection_utf8(query, qn)) return r->status;
    memcpy(r->query, query, qn + 1);
    if (cancelled && cancelled(context)) return r->status = BOOK_DICTIONARY_CANCELLED;
    FILE *f = fopen(path, "rb");
    if (!f) return r->status = errno == ENOENT ? BOOK_DICTIONARY_MISSING : BOOK_DICTIONARY_IO;
    unsigned char header[64], row[20]; unsigned char *compressed = NULL, *raw = NULL;
    if (fseek(f, 0, SEEK_END)) { r->status = BOOK_DICTIONARY_IO; goto done; }
    long length = ftell(f);
    if (length < 64 || length > INT32_MAX || !read_at(f, 0, header, 64)) goto done;
    if (memcmp(header, "MGDICT\r\n", 8)) goto done;
    if (u32(header + 8) != 1) { r->status = BOOK_DICTIONARY_UNSUPPORTED; goto done; }
    uint32_t total = u32(header+12), count = u32(header+16), index = u32(header+20), keys = u32(header+24),
        data = u32(header+28), meta = u32(header+32), meta_n = u32(header+36);
    if (total != (uint32_t)length || !count || count > 1000000 || index != 64 ||
        count > (total - 64)/20 || keys != index + count*20 || keys > data || data > meta ||
        !range(meta, meta_n, total) || meta_n == 0 || meta_n >= sizeof(r->source) || meta + meta_n != total ||
        u32(header+60) != (uint32_t)crc32(0, header, 60)) goto done;
    if (!read_at(f, meta, r->source, meta_n) || !reader_selection_utf8(r->source, meta_n) ||
        u32(header+40) != (uint32_t)crc32(0, (const unsigned char *)r->source, meta_n)) goto done;
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        if (cancelled && cancelled(context)) { r->status = BOOK_DICTIONARY_CANCELLED; goto done; }
        uint32_t mid = lo + (hi - lo)/2;
        if (!read_at(f, index + mid*20, row, 20)) { r->status = BOOK_DICTIONARY_IO; goto done; }
        if (u16(row+18)) goto done;
        unsigned kn = u16(row+16); uint32_t ko = u32(row);
        if (!kn || kn > 128 || ko < keys || !range(ko, kn, data)) goto done;
        char key[129] = {0};
        if (!read_at(f, ko, key, kn)) { r->status = BOOK_DICTIONARY_IO; goto done; }
        if (!reader_selection_utf8(key, kn)) goto done;
        int order = strcmp(query, key);
        if (order > 0) { lo = mid+1; continue; }
        if (order < 0) { hi = mid; continue; }
        uint32_t off = u32(row+4); unsigned cn = u16(row+8), rn = u16(row+10);
        if (!cn || cn > BOOK_DICTIONARY_RAW_MAX || rn < 8 || rn > BOOK_DICTIONARY_RAW_MAX ||
            off < data || !range(off, cn, meta)) goto done;
        compressed = malloc(cn); raw = malloc(rn);
        if (!compressed || !raw) { r->status = BOOK_DICTIONARY_BUSY; goto done; }
        if (!read_at(f, off, compressed, cn)) { r->status = BOOK_DICTIONARY_IO; goto done; }
        uLongf decoded = rn;
        if (uncompress(raw, &decoded, compressed, cn) != Z_OK || decoded != rn ||
            (uint32_t)crc32(0, raw, rn) != u32(row+12)) goto done;
        char *fields[] = {r->reading, r->definition, r->example, r->variant};
        size_t caps[] = {sizeof(r->reading),sizeof(r->definition),sizeof(r->example),sizeof(r->variant)};
        size_t at = 8;
        for (unsigned i = 0; i < 4; ++i) {
            unsigned n = u16(raw + i*2);
            if (n >= caps[i] || n > rn-at || (n && !reader_selection_utf8((char *)raw+at, n))) goto done;
            memcpy(fields[i], raw+at, n); fields[i][n] = 0; at += n;
        }
        if (at != rn) goto done;
        r->status = BOOK_DICTIONARY_OK; goto done;
    }
    r->status = BOOK_DICTIONARY_NOT_FOUND;
done:
    free(raw); free(compressed); fclose(f);
    if (cancelled && cancelled(context)) r->status = BOOK_DICTIONARY_CANCELLED;
    return r->status;
}
