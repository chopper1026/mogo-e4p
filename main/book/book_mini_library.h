/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：最多五本书的无存储选取模型；不借用书架页面的目录或过滤状态。
 * English: Storage-free selection of up to five books, without borrowing shelf-page catalogs or filters. */
#pragma once
#include "book_shelf_model.h"

#define BOOK_MINI_LIBRARY_MAX 5
typedef struct {
    char path[BOOK_STORE_PATH_MAX], title[128], author[128];
    uint32_t size, recent;
    uint8_t pct;
    bool has_progress;
} book_mini_item_t;
typedef struct {
    book_mini_item_t random[BOOK_MINI_LIBRARY_MAX], recent[BOOK_MINI_LIBRARY_MAX];
    unsigned random_count, recent_count;
    unsigned random_selected, recent_selected;
} book_mini_library_t;
typedef uint32_t (*book_mini_random_fn)(void *ctx);
/// 输入已去重且可见的目录；保留随机幸存项和焦点，换批尽量避开旧批。/ Accept a distinct visible catalog; preserve surviving random items/focus and maximize novelty on reshuffle.
void book_mini_library_update(book_mini_library_t *model, const book_shelf_entry_t *entries, size_t count,
                              bool reshuffle, book_mini_random_fn random, void *ctx);
/// 失卡时原地去掉卡上条目，不读文件。/ Remove card-backed items in place on media loss without IO.
void book_mini_library_drop_card(book_mini_library_t *model);
