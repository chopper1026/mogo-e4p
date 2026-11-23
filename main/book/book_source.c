/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT、EPUB 与 MOBI 单实例书源分派，不依赖界面。
 * English: Singleton TXT, EPUB and MOBI source dispatch without UI dependencies.
 * 冻结：TXT 保留源字节；EPUB 采用累计 spine HTML 字节；MOBI6 固定 v1 原始 HTML 分段；不改原书。
 * Frozen: TXT keeps source bytes; EPUB uses cumulative spine HTML; MOBI6 fixes v1 raw HTML segments; never modify originals.
 * 中文：新增原生 MOBI6 书源，不改变 TXT/EPUB 的既有进度语义。
 * English: Native MOBI6 support preserves existing TXT/EPUB progress semantics.
 */
#include "../diagnostics/reader_perf.h"
#include "../diagnostics/reader_memory.h"
#include "book_source_internal.h"
#include "book_epub.h"
#include "book_mobi.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static book_txt_t s_book;
static book_epub_t *s_epub;
static book_mobi_t *s_mobi;
size_t book_open_min_psram_block(const char *path) {
    const char *ext = path ? strrchr(path, '.') : NULL;
    return ext && !strcasecmp(ext, ".txt") ? sizeof(book_entry_t) * BOOK_CHAPTER_MAX : 0;
}

esp_err_t book_open(const char *path) {
    book_close();
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) return book_epub_open(path, &s_epub);
    if (ext && !strcasecmp(ext, ".mobi")) return book_mobi_open(path, &s_mobi);
    if (!ext || strcasecmp(ext, ".txt")) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err = book_txt_open(&s_book, path);
    if (err != ESP_OK) book_close();
    return err;
}
void book_close(void) {
    book_epub_close(s_epub); s_epub = NULL;
    book_mobi_close(s_mobi); s_mobi = NULL;
    if (s_book.file) fclose(s_book.file);
    free(s_book.entries); free(s_book.nav); memset(&s_book, 0, sizeof(s_book));
}
void book_set_embedded_fonts(bool on) {
    if (s_epub) book_epub_set_fonts_enabled(s_epub, on);
}
size_t book_chapter_count(void) { return s_mobi ? book_mobi_chapter_count(s_mobi) : s_epub ? book_epub_chapter_count(s_epub) : s_book.count; }
size_t book_navigation_count(void) {
    return s_mobi ? book_mobi_navigation_count(s_mobi) : s_epub ? book_epub_navigation_count(s_epub) :
        s_book.nav_count ? s_book.nav_count : s_book.count;
}
size_t book_navigation_chapter(size_t position) {
    if (s_mobi) return book_mobi_navigation_chapter(s_mobi, position);
    if (s_epub) return book_epub_navigation_chapter(s_epub, position);
    if (s_book.nav_count) return position < s_book.nav_count ? s_book.nav[position].part : SIZE_MAX;
    return position < s_book.count ? position : SIZE_MAX;
}
esp_err_t book_navigation_title(size_t position, char *buf, size_t cap) {
    if (s_mobi) return book_mobi_navigation_title(s_mobi, position, buf, cap);
    if (s_epub) return book_epub_navigation_title(s_epub, position, buf, cap);
    if (!s_book.nav_count) return book_chapter_title(position, buf, cap);
    if (!buf || !cap || position >= s_book.nav_count) return ESP_ERR_INVALID_ARG;
    const char *title = s_book.nav[position].title;
    size_t n = strlen(title);
    if (n >= cap) { buf[0] = 0; return ESP_ERR_INVALID_SIZE; }
    memcpy(buf, title, n + 1); return ESP_OK;
}
const char *book_navigation_anchor(size_t position) {
    return s_epub ? book_epub_navigation_anchor(s_epub, position) : NULL;
}
size_t book_navigation_source_offset(size_t position) {
    if (s_mobi) return book_mobi_navigation_source_offset(s_mobi, position);
    if (s_epub) return book_epub_navigation_source_offset(s_epub, position);
    return position < s_book.nav_count ? s_book.nav[position].text_offset : SIZE_MAX;
}
bool book_navigation_txt_position(size_t chapter, size_t text_offset, size_t *position) {
    if (!s_book.nav_count) return false;
    size_t lo = 0, hi = s_book.nav_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const book_txt_nav_t *e = &s_book.nav[mid];
        if (e->part < chapter || (e->part == chapter && e->text_offset <= text_offset)) lo = mid + 1;
        else hi = mid;
    }
    *position = lo ? lo - 1 : SIZE_MAX;
    return true;
}
esp_err_t book_chapter_title(size_t i, char *buf, size_t cap) {
    if (s_mobi) return book_mobi_chapter_title(s_mobi, i, buf, cap);
    if (s_epub) return book_epub_chapter_title(s_epub, i, buf, cap);
    if (!buf || !cap || i >= s_book.count) return ESP_ERR_INVALID_ARG;
    size_t n = strlen(s_book.entries[i].title);
    if (n >= cap) { buf[0] = 0; return ESP_ERR_INVALID_SIZE; }
    memcpy(buf, s_book.entries[i].title, n + 1); return ESP_OK;
}
esp_err_t book_chapter_source_name(size_t i, char *buf, size_t cap) {
    if (buf && cap) buf[0] = 0;
    if (s_mobi) return book_mobi_chapter_name(s_mobi, i, buf, cap);
    if (!s_epub) return ESP_ERR_NOT_SUPPORTED;
    return book_epub_chapter_name(s_epub, i, buf, cap);
}
esp_err_t book_chapter_load(size_t i, char **utf8, size_t *len) {
    if (!utf8 || !len) return ESP_ERR_INVALID_ARG;
    *utf8 = NULL; *len = 0;
    if (s_epub || s_mobi) {
        html_text_t text = {0};
        esp_err_t err = s_mobi ? book_mobi_load_target(s_mobi, i, NULL, SIZE_MAX, NULL, &text) : book_epub_load(s_epub, i, &text);
        if (err != ESP_OK) return err;
        *utf8 = text.utf8; *len = text.len; text.utf8 = NULL;
        html_text_free(&text); return ESP_OK;
    }
    if (!s_book.file || i >= s_book.count) return ESP_ERR_INVALID_ARG;
    return book_txt_load(&s_book, i, utf8, len);
}
esp_err_t book_chapter_load_blocks(size_t i, html_text_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (s_mobi) return book_mobi_load_target(s_mobi, i, NULL, SIZE_MAX, NULL, out);
    if (s_epub) return book_epub_load(s_epub, i, out);
    return book_chapter_load(i, &out->utf8, &out->len);
}
esp_err_t book_chapter_load_blocks_anchor(size_t i, const char *anchor,
                                           size_t *anchor_offset, html_text_t *out) {
    return book_chapter_load_blocks_target(i, anchor, SIZE_MAX, anchor_offset, out);
}
esp_err_t book_chapter_load_blocks_target(size_t i, const char *anchor,
                                           size_t source_offset, size_t *text_offset,
                                           html_text_t *out) {
#if CONFIG_READER_PERF
    READER_PERF_SCOPE(READER_PERF_SOURCE);
#endif
    if (text_offset) *text_offset = 0;
    if (s_mobi) return book_mobi_load_target(s_mobi, i, anchor, source_offset, text_offset, out);
    if (s_epub) return book_epub_load_target(s_epub, i, anchor, source_offset, text_offset, out);
    esp_err_t err = book_chapter_load_blocks(i, out);
    // TXT 目录目标已是 UTF-8 字节偏移；不把它当成原文件字节再次转码。
    // TXT navigation targets already use UTF-8 offsets; do not interpret them as source bytes.
    if (err == ESP_OK && s_book.nav_count && source_offset != SIZE_MAX) {
        if (source_offset >= out->len || ((unsigned char)out->utf8[source_offset] & 0xc0) == 0x80) {
            html_text_free(out); return ESP_ERR_INVALID_SIZE;
        }
        if (text_offset) *text_offset = source_offset;
    }
    return err;
}
uint32_t book_total_bytes(void) { return s_mobi ? book_mobi_total_bytes(s_mobi) : s_epub ? book_epub_total_bytes(s_epub) : s_book.total; }
uint32_t book_chapter_byte_offset(size_t i) {
    if (s_mobi) return book_mobi_chapter_byte_offset(s_mobi, i);
    return s_epub ? book_epub_chapter_byte_offset(s_epub, i) : i < s_book.count ? s_book.entries[i].offset : 0;
}
bool book_is_epub_comic(void) { return book_epub_is_comic(s_epub); }
book_kind_t book_kind(void) { return s_mobi ? BOOK_KIND_MOBI : s_epub ? BOOK_KIND_EPUB : BOOK_KIND_TXT; }
esp_err_t book_chapter_image(size_t chapter, const char *src, uint8_t **data, size_t *size, bool *png) {
    if (s_mobi) return book_mobi_image(s_mobi, chapter, src, data, size, png);
    return s_epub ? book_epub_image(s_epub, chapter, src, data, size, png) : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t book_chapter_image_dimensions(size_t chapter, const char *src, unsigned *width, unsigned *height) {
    if (s_mobi) return book_mobi_image_dimensions(s_mobi, chapter, src, width, height);
    return s_epub ? book_epub_image_dimensions(s_epub, chapter, src, width, height) : ESP_ERR_NOT_SUPPORTED;
}

#if CONFIG_READER_PERF && CONFIG_READER_MEMORY
void book_source_memory_stats(reader_source_memory_stats_t *stats) {
    *stats = (reader_source_memory_stats_t){0};
    stats->txt_index = reader_memory_owned(s_book.entries) + reader_memory_owned(s_book.nav);
    stats->open_files = s_book.file != NULL;
    book_epub_memory_stats(s_epub, stats);
    book_mobi_memory_stats(s_mobi, stats);
}
#endif
