/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：随机无放回补位、最近阅读排序与焦点保持；只持有十个结果快照。
 * English: Sampling without replacement, recent ordering and focus retention, owning only ten result snapshots. */
#include "book_mini_library.h"
#include <stdio.h>
#include <string.h>

static void copy_text(char *out, size_t cap, const char *text) {
    size_t n = strlen(text);
    if (n >= cap) { n = cap - 1; while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n; }
    memcpy(out, text, n); out[n] = 0;
}
static void copy_item(book_mini_item_t *out, const book_shelf_entry_t *entry) {
    memset(out, 0, sizeof(*out));
    copy_text(out->path, sizeof(out->path), entry->path);
    copy_text(out->title, sizeof(out->title), entry->name);
    copy_text(out->author, sizeof(out->author), entry->author);
    out->size = entry->size; out->recent = entry->recent;
    out->has_progress = entry->has_progress; out->pct = entry->pct > 100 ? 100 : entry->pct;
}
static bool contains(const book_mini_item_t *items, unsigned count, const char *path) {
    for (unsigned i = 0; i < count; ++i) if (!strcmp(items[i].path, path)) return true;
    return false;
}
static uint32_t below(uint32_t bound, book_mini_random_fn random, void *ctx) {
    uint32_t threshold = (uint32_t)(-bound) % bound, value;
    do { value = random(ctx); } while (value < threshold);
    return value % bound;
}
static bool usable(const book_shelf_entry_t *entry) { return entry->path[0] && !entry->removed; }
static void sample(book_mini_item_t *out, unsigned *count, const book_shelf_entry_t *entries, size_t size,
                   const book_mini_item_t *exclude, unsigned excluded, bool only_excluded,
                   book_mini_random_fn random, void *ctx) {
    const unsigned start = *count, room = BOOK_MINI_LIBRARY_MAX - start;
    uint32_t seen = 0;
    if (!room) return;
    for (size_t i = 0; i < size; ++i) {
        const book_shelf_entry_t *entry = &entries[i];
        if (!usable(entry) || contains(out, start, entry->path) ||
            contains(exclude, excluded, entry->path) != only_excluded) continue;
        ++seen;
        unsigned slot = seen <= room ? seen - 1 : below(seen, random, ctx);
        if (slot < room) copy_item(&out[start + slot], entry);
    }
    *count += seen < room ? seen : room;
}
static unsigned focus(const book_mini_item_t *items, unsigned count, const char *path) {
    for (unsigned i = 0; i < count; ++i) if (!strcmp(items[i].path, path)) return i;
    return 0;
}
void book_mini_library_update(book_mini_library_t *model, const book_shelf_entry_t *entries, size_t count,
                              bool reshuffle, book_mini_random_fn random, void *ctx) {
    char random_focus[BOOK_STORE_PATH_MAX] = "", recent_focus[BOOK_STORE_PATH_MAX] = "";
    if (model->random_selected < model->random_count) strcpy(random_focus, model->random[model->random_selected].path);
    if (model->recent_selected < model->recent_count) strcpy(recent_focus, model->recent[model->recent_selected].path);
    book_mini_item_t old[BOOK_MINI_LIBRARY_MAX];
    unsigned old_count = model->random_count;
    memcpy(old, model->random, sizeof(old));
    model->random_count = model->recent_count = 0;
    if (!reshuffle) {
        for (unsigned i = 0; i < old_count; ++i) for (size_t j = 0; j < count; ++j)
            if (usable(&entries[j]) && !strcmp(old[i].path, entries[j].path)) {
                copy_item(&model->random[model->random_count++], &entries[j]); break;
            }
    }
    sample(model->random, &model->random_count, entries, count, old, old_count, false, random, ctx);
    sample(model->random, &model->random_count, entries, count, old, old_count, true, random, ctx);
    if (reshuffle) for (unsigned i = model->random_count; i > 1; --i) {
        unsigned j = below(i, random, ctx);
        book_mini_item_t item = model->random[i - 1]; model->random[i - 1] = model->random[j]; model->random[j] = item;
    }
    for (size_t i = 0; i < count; ++i) {
        const book_shelf_entry_t *entry = &entries[i];
        if (!usable(entry) || !entry->has_progress) continue;
        unsigned at = 0;
        while (at < model->recent_count && (model->recent[at].recent > entry->recent ||
              (model->recent[at].recent == entry->recent && strcmp(model->recent[at].path, entry->path) < 0))) ++at;
        if (at >= BOOK_MINI_LIBRARY_MAX) continue;
        if (model->recent_count < BOOK_MINI_LIBRARY_MAX) ++model->recent_count;
        for (unsigned j = model->recent_count - 1; j > at; --j) model->recent[j] = model->recent[j - 1];
        copy_item(&model->recent[at], entry);
    }
    model->random_selected = reshuffle ? 0 : focus(model->random, model->random_count, random_focus);
    model->recent_selected = focus(model->recent, model->recent_count, recent_focus);
}
static void drop_card(book_mini_item_t *items, unsigned *count, unsigned *selected) {
    char path[BOOK_STORE_PATH_MAX] = "";
    if (*selected < *count) strcpy(path, items[*selected].path);
    unsigned kept = 0;
    for (unsigned i = 0; i < *count; ++i) if (strncmp(items[i].path, "/sdcard/", 8)) items[kept++] = items[i];
    *count = kept; *selected = focus(items, kept, path);
}
void book_mini_library_drop_card(book_mini_library_t *model) {
    drop_card(model->random, &model->random_count, &model->random_selected);
    drop_card(model->recent, &model->recent_count, &model->recent_selected);
}
