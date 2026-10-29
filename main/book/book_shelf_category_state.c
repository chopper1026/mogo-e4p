/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从书架页面提取分类、页码与菜单状态，保留有界 PSRAM 分配和目录规则。
 * English: Category, page and menu state extracted from the shelf page, retaining bounded PSRAM allocation and folder rules.
 */
#include "book_shelf_category_state.h"
#include "book_shelf_categories.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct book_shelf_category_entry {
    char path[BOOK_STORE_PATH_MAX];
    int pages[2], count;
    bool available;
};

// 截断必须停在UTF8字符边界。/ Truncation must stop at a UTF8 character boundary.
static void copy_text(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool has_selection(const book_shelf_categories_t *state) {
    return state->entries && state->selected >= 0 && state->selected < state->count;
}

bool book_shelf_categories_init(book_shelf_categories_t *state, const char *root) {
    bool changed = strcmp(state->root, root) != 0;
    if (changed) {
        free(state->entries); state->entries = NULL;
        state->count = state->capacity = state->selected = state->menu_page = 0;
        copy_text(state->root, sizeof(state->root), root);
    }
    if (!state->entries) {
        state->entries = heap_caps_calloc(16, sizeof(*state->entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (state->entries) {
            state->capacity = 16; state->count = 2;
            state->entries[0].available = state->entries[1].available = true;
        }
    }
    return changed;
}

void book_shelf_categories_dispose(book_shelf_categories_t *state) {
    free(state->entries);
    memset(state, 0, sizeof(*state));
}

int book_shelf_categories_add(book_shelf_categories_t *state, const char *path) {
    for (int i = 2; i < state->count; ++i) {
        if (!strcmp(state->entries[i].path, path)) { state->entries[i].available = true; return i; }
    }
    if (state->count == BOOK_SHELF_CATEGORY_LIMIT) return -1;
    if (state->count == state->capacity) {
        int capacity = state->capacity ? state->capacity * 2 : 16;
        if (capacity > BOOK_SHELF_CATEGORY_LIMIT) capacity = BOOK_SHELF_CATEGORY_LIMIT;
        book_shelf_category_entry_t *next = heap_caps_realloc(state->entries, (size_t)capacity * sizeof(*next),
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!next) return -1;
        state->entries = next; state->capacity = capacity;
    }
    int index = state->count++;
    memset(&state->entries[index], 0, sizeof(state->entries[index]));
    copy_text(state->entries[index].path, sizeof(state->entries[index].path), path);
    state->entries[index].available = true;
    return index;
}

void book_shelf_categories_begin_scan(book_shelf_categories_t *state) {
    for (int i = 2; i < state->count; ++i) state->entries[i].available = false;
}

void book_shelf_categories_recount(book_shelf_categories_t *state, const book_shelf_entry_t *entries, int count) {
    if (!state->entries) return;
    for (int i = 0; i < state->count; ++i) state->entries[i].count = 0;
    for (int i = 0; i < count; ++i) {
        if (entries[i].removed) continue;
        ++state->entries[0].count;
        char child[BOOK_STORE_PATH_MAX], path[BOOK_STORE_PATH_MAX];
        if (!book_shelf_child(state->root, entries[i].path, child, sizeof(child))) {
            ++state->entries[1].count; continue;
        }
        int written = snprintf(path, sizeof(path), "%s/%s", state->root, child);
        if (written < 0 || (size_t)written >= sizeof(path)) continue;
        int index = book_shelf_categories_add(state, path);
        if (index >= 2) ++state->entries[index].count;
    }
    if (state->selected >= state->count || !state->entries[state->selected].available) state->selected = 0;
}

book_shelf_query_t book_shelf_categories_query(const book_shelf_categories_t *state, int filter, bool recent_sort) {
    return (book_shelf_query_t){
        .root = state->root,
        .category_path = has_selection(state) && state->selected >= 2 ? state->entries[state->selected].path : NULL,
        .uncategorized = has_selection(state) && state->selected == 1,
        .filter = filter, .recent_sort = recent_sort,
    };
}

const char *book_shelf_categories_name(const book_shelf_categories_t *state, int index) {
    if (index == 1) return "未分类";
    if (!state->entries || index < 2 || index >= state->count) return "全部书籍";
    const char *slash = strrchr(state->entries[index].path, '/');
    return slash ? slash + 1 : state->entries[index].path;
}

int book_shelf_categories_count(const book_shelf_categories_t *state, int index) {
    return state->entries && index >= 0 && index < state->count ? state->entries[index].count : 0;
}

int book_shelf_categories_selected(const book_shelf_categories_t *state) { return state->selected; }

int book_shelf_categories_at(const book_shelf_categories_t *state, int ordinal) {
    for (int i = 0; i < state->count; ++i) {
        if (state->entries[i].available && ordinal-- == 0) return i;
    }
    return -1;
}

int book_shelf_categories_available(const book_shelf_categories_t *state) {
    int count = 0;
    for (int i = 0; i < state->count; ++i) if (state->entries[i].available) ++count;
    return count;
}

void book_shelf_categories_remember(book_shelf_categories_t *state, bool list_layout, int page) {
    if (has_selection(state)) state->entries[state->selected].pages[list_layout] = page;
}

int book_shelf_categories_saved_page(const book_shelf_categories_t *state, bool list_layout) {
    return has_selection(state) ? state->entries[state->selected].pages[list_layout] : 0;
}

int book_shelf_categories_restore(book_shelf_categories_t *state, bool list_layout, int count, int rows) {
    int page = book_shelf_saved_page(book_shelf_categories_saved_page(state, list_layout), count, rows);
    book_shelf_categories_remember(state, list_layout, page);
    return page;
}

bool book_shelf_categories_menu_is_open(const book_shelf_categories_t *state) { return state->menu_open; }

bool book_shelf_categories_menu_open(book_shelf_categories_t *state) {
    if (!state->entries) return false;
    state->menu_open = true;
    int ordinal = 0;
    for (int i = 0; i < state->selected; ++i) if (state->entries[i].available) ++ordinal;
    state->menu_page = ordinal / BOOK_SHELF_CATEGORY_MENU_ROWS;
    return true;
}

void book_shelf_categories_menu_close(book_shelf_categories_t *state) { state->menu_open = false; }
uint8_t book_shelf_categories_menu_page(const book_shelf_categories_t *state) { return state->menu_page; }

bool book_shelf_categories_menu_move(book_shelf_categories_t *state, int direction) {
    int pages = (book_shelf_categories_available(state) + BOOK_SHELF_CATEGORY_MENU_ROWS - 1) / BOOK_SHELF_CATEGORY_MENU_ROWS;
    int next = state->menu_page + direction;
    if (next < 0 || next >= pages) return false;
    state->menu_page = next;
    return true;
}

bool book_shelf_categories_select(book_shelf_categories_t *state, int index) {
    if (!state->entries || index < 0 || index >= state->count || !state->entries[index].available) return false;
    state->selected = index;
    state->menu_open = false;
    return true;
}
