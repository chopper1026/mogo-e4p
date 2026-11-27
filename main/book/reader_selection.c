/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：选区按原文字节排序，保留组合附加符与变体选择符。
 * English: Source-ordered selection preserving combining marks and variation selectors. */
#include "reader_selection.h"
#include <limits.h>
#include <string.h>

static size_t scalar(const char *text, size_t n, uint32_t *cp) {
    if (!n) return 0;
    const unsigned char *p = (const unsigned char *)text;
    unsigned bytes = p[0] < 0x80 ? 1 : p[0] >= 0xc2 && p[0] <= 0xdf ? 2 :
        p[0] >= 0xe0 && p[0] <= 0xef ? 3 : p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
    if (!bytes || bytes > n || !p[0]) return 0;
    uint32_t value = p[0] & (bytes == 1 ? 0x7f : (1u << (7 - bytes)) - 1);
    for (unsigned i = 1; i < bytes; ++i) {
        if ((p[i] & 0xc0) != 0x80) return 0;
        value = (value << 6) | (p[i] & 0x3f);
    }
    if ((bytes == 2 && value < 0x80) || (bytes == 3 && value < 0x800) ||
        (bytes == 4 && value < 0x10000) || (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) return 0;
    *cp = value;
    return bytes;
}
size_t reader_selection_utf8(const char *text, size_t bytes) {
    if (!text || !bytes) return 0;
    size_t count = 0;
    for (size_t i = 0; i < bytes;) {
        uint32_t cp; size_t n = scalar(text + i, bytes - i, &cp);
        if (!n) return 0;
        i += n; ++count;
    }
    return count;
}
static bool continuation(uint32_t cp) {
    return (cp >= 0x300 && cp <= 0x36f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
        (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0xfe00 && cp <= 0xfe0f) ||
        (cp >= 0xfe20 && cp <= 0xfe2f) || (cp >= 0xe0100 && cp <= 0xe01ef);
}
static bool whitespace(uint32_t cp) {
    return cp <= 0x20 || cp == 0x85 || cp == 0xa0 || cp == 0x1680 ||
        (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 ||
        cp == 0x202f || cp == 0x205f || cp == 0x3000;
}
// 与 build_english_dictionary.py 使用相同的字母、连接符和外缘标点规则。
// Keep the letter, joiner and edge punctuation rules in sync with build_english_dictionary.py.
static bool english_letter(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= 0xc0 && cp <= 0xd6) || (cp >= 0xd8 && cp <= 0xf6) ||
        (cp >= 0xf8 && cp <= 0xff);
}
static bool english_alnum(uint32_t cp) {
    return english_letter(cp) || (cp >= '0' && cp <= '9');
}
static char english_joiner(uint32_t cp) {
    if (cp == '\'' || cp == 0x2018 || cp == 0x2019 || cp == 0x02bc || cp == 0xff07) return '\'';
    if (cp == '-' || cp == 0x2010 || cp == 0x2011 || cp == 0xff0d) return '-';
    return 0;
}
static bool english_edge(uint32_t cp) {
    return whitespace(cp) || english_joiner(cp) ||
        (cp >= 0x21 && cp <= 0x7e && !english_alnum(cp)) || cp == 0xab || cp == 0xbb ||
        (cp >= 0x2012 && cp <= 0x201f) || cp == 0x2026 || cp == 0x3001 || cp == 0x3002 ||
        (cp >= 0x3008 && cp <= 0x3011) || cp == 0xff01 || cp == 0xff0c || cp == 0xff1f;
}
bool reader_selection_english_key(const char *text, char output[READER_SELECTION_BYTES + 1]) {
    output[0] = 0;
    size_t length = text ? strnlen(text, READER_SELECTION_BYTES + 1) : 0;
    if (!length || length > READER_SELECTION_BYTES) return false;
    size_t start = length, end = 0;
    for (size_t at = 0; at < length;) {
        uint32_t cp; size_t n = scalar(text + at, length - at, &cp);
        if (!n) return false;
        if (!english_edge(cp)) {
            if (start == length) start = at;
            end = at + n;
        }
        at += n;
    }
    bool has_letter = false, separated = false, previous_word = false;
    size_t used = 0, count = 0;
    for (size_t at = start; at < end;) {
        uint32_t cp; size_t n = scalar(text + at, end - at, &cp);
        at += n;
        if (whitespace(cp)) {
            separated = true; previous_word = false;
        } else if (english_alnum(cp)) {
            if (separated) { output[used++] = ' '; ++count; }
            ++count;
            separated = false; previous_word = true; has_letter |= english_letter(cp);
            if ((cp >= 'A' && cp <= 'Z') || (cp >= 0xc0 && cp <= 0xde && cp != 0xd7)) cp += 32;
            if (cp < 0x80) output[used++] = (char)cp;
            else { output[used++] = (char)(0xc0 | (cp >> 6)); output[used++] = (char)(0x80 | (cp & 63)); }
        } else {
            char joiner = english_joiner(cp); uint32_t next;
            if (!joiner || !previous_word || !scalar(text + at, end - at, &next) || !english_alnum(next))
                return output[0] = 0, false;
            output[used++] = joiner; ++count; previous_word = false;
        }
    }
    bool valid = has_letter && count <= READER_SELECTION_SCALARS;
    output[valid ? used : 0] = 0;
    return valid;
}
size_t reader_selection_character(const char *text, size_t offset, char output[READER_SELECTION_BYTES + 1]) {
    output[0] = 0;
    if (!text) return 0;
    size_t length = strlen(text);
    if (offset >= length) offset = 0;
    uint32_t cp;
    size_t bytes;
    while (offset < length && (bytes = scalar(text + offset, length - offset, &cp)) && whitespace(cp))
        offset += bytes;
    if (offset >= length || !(bytes = scalar(text + offset, length - offset, &cp))) return 0;
    size_t end = offset + bytes;
    while (end < length && (bytes = scalar(text + end, length - end, &cp)) && continuation(cp)) end += bytes;
    if (end - offset > READER_SELECTION_BYTES) return 0;
    memcpy(output, text + offset, end - offset);
    output[end - offset] = 0;
    return end;
}
static size_t hit(const book_layout_glyph_t *g, size_t count, int x, int y, bool nearest) {
    size_t best = SIZE_MAX; int64_t distance = INT64_MAX;
    for (size_t i = 0; i < count; ++i) {
        int dx = x < g[i].x ? g[i].x - x : x >= g[i].x + g[i].width ? x - (g[i].x + g[i].width - 1) : 0;
        int dy = y < g[i].y ? g[i].y - y : y >= g[i].y + g[i].height ? y - (g[i].y + g[i].height - 1) : 0;
        int64_t d = (int64_t)dx * dx + (int64_t)dy * dy;
        if (d < distance && (nearest || d == 0)) { best = i; distance = d; }
    }
    return best;
}
static bool update(reader_selection_t *s, const char *text, size_t length,
                   const book_layout_glyph_t *g, size_t count) {
    if (!g || s->anchor >= count || s->focus >= count) return false;
    size_t lo = s->anchor < s->focus ? s->anchor : s->focus;
    size_t hi = s->anchor > s->focus ? s->anchor : s->focus;
    uint32_t cp;
    while (lo && g[lo].start < length && scalar(text + g[lo].start, length - g[lo].start, &cp) && continuation(cp)) --lo;
    while (hi + 1 < count && g[hi + 1].start < length &&
           scalar(text + g[hi + 1].start, length - g[hi + 1].start, &cp) && continuation(cp)) ++hi;
    size_t start = g[lo].start, end = (size_t)g[hi].start + g[hi].length;
    if (end > length || start >= end || end - start > READER_SELECTION_BYTES) return false;
    size_t n = reader_selection_utf8(text + start, end - start);
    if (!n || n > READER_SELECTION_SCALARS) return false;
    memcpy(s->query, text + start, end - start); s->query[end - start] = 0;
    if (s->anchor <= s->focus) { s->anchor = lo; s->focus = hi; }
    else { s->anchor = hi; s->focus = lo; }
    return true;
}
static uint32_t glyph_scalar(const char *text, size_t length, const book_layout_glyph_t *g) {
    uint32_t cp;
    return g->start < length && scalar(text + g->start, length - g->start, &cp) ? cp : 0;
}
static bool adjoining(const book_layout_glyph_t *left, const book_layout_glyph_t *right) {
    return (size_t)left->start + left->length == right->start;
}
bool reader_selection_begin(reader_selection_t *s, const char *text, size_t length,
    const book_layout_glyph_t *g, size_t count, size_t page, uint32_t generation, int x, int y) {
    if (!s || !text || !g) return false;
    size_t i = hit(g, count, x, y, false);
    uint32_t cp;
    if (i == SIZE_MAX || g[i].start >= length ||
        !scalar(text + g[i].start, length - g[i].start, &cp) || whitespace(cp)) return false;
    *s = (reader_selection_t){.active = true, .dragging = true, .anchor = i, .focus = i,
        .generation = generation, .page = page};
    if (english_letter(cp)) {
        size_t lo = i, hi = i;
        while (lo && adjoining(&g[lo - 1], &g[lo])) {
            uint32_t prev = glyph_scalar(text, length, &g[lo - 1]);
            if (english_alnum(prev)) --lo;
            else if (lo > 1 && english_joiner(prev) && adjoining(&g[lo - 2], &g[lo - 1]) &&
                     english_alnum(glyph_scalar(text, length, &g[lo - 2]))) lo -= 2;
            else break;
        }
        while (hi + 1 < count && adjoining(&g[hi], &g[hi + 1])) {
            uint32_t next = glyph_scalar(text, length, &g[hi + 1]);
            if (english_alnum(next)) ++hi;
            else if (hi + 2 < count && english_joiner(next) && adjoining(&g[hi + 1], &g[hi + 2]) &&
                     english_alnum(glyph_scalar(text, length, &g[hi + 2]))) hi += 2;
            else break;
        }
        s->anchor = lo; s->focus = hi;
        if (update(s, text, length, g, count)) {
            s->word_first = s->anchor; s->word_last = s->focus; s->extend_word = true;
            return true;
        }
        s->anchor = s->focus = i;
    }
    if (update(s, text, length, g, count)) return true;
    reader_selection_cancel(s); return false;
}
bool reader_selection_move(reader_selection_t *s, const char *text, size_t length,
    const book_layout_glyph_t *g, size_t count, int x, int y) {
    if (!s || !s->active || !text || !g) return false;
    size_t i = hit(g, count, x, y, true);
    if (i == SIZE_MAX) return false;
    size_t anchor = s->anchor, focus = i;
    if (s->extend_word) {
        anchor = i < s->word_first ? s->word_last : s->word_first;
        if (i >= s->word_first && i <= s->word_last) focus = s->word_last;
    }
    if (anchor == s->anchor && focus == s->focus) return false;
    reader_selection_t old = *s; s->anchor = anchor; s->focus = focus;
    if (!update(s, text, length, g, count)) { *s = old; return false; }
    return true;
}
bool reader_selection_range(const reader_selection_t *s, const book_layout_glyph_t *g,
    size_t count, uint32_t *start, uint32_t *end) {
    if (!s || !s->active || !g || s->anchor >= count || s->focus >= count) return false;
    size_t lo = s->anchor < s->focus ? s->anchor : s->focus;
    size_t hi = s->anchor > s->focus ? s->anchor : s->focus;
    *start = g[lo].start; *end = g[hi].start + g[hi].length; return true;
}
void reader_selection_cancel(reader_selection_t *s) { if (s) memset(s, 0, sizeof(*s)); }
