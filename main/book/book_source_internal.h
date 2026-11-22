/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书源编码和章节解析，不依赖界面。
 * English: Source encoding and chapter parsing without UI dependencies.
 * 冻结：保留原文件偏移，无效编码替换，不写文件。
 * Frozen: Preserve original byte offsets, replace invalid encoding, never write files.
 */
#pragma once
#include "book_source.h"
#include <stdbool.h>
#include <stdio.h>
#define BOOK_CHAPTER_MAX 2048
#define BOOK_TXT_NAV_MAX 4096
#define BOOK_CHAPTER_BYTES_MAX (96 * 1024)
typedef struct {
    uint32_t offset; ///< 原文件偏移 / Original file offset
    char title[128]; ///< UTF-8 标题，长标题按字符边界截断 / UTF-8 title, truncated at a character boundary
} book_entry_t;
typedef struct {
    uint32_t part; ///< 存量排版分段 / Existing layout segment
    uint32_t text_offset; ///< 分段内的 UTF-8 字节偏移 / UTF-8 byte offset within the segment
    char title[128]; ///< 目录标题 / TOC title
} book_txt_nav_t;
typedef struct {
    FILE *file; ///< 文件句柄 / File handle
    book_entry_t *entries; ///< PSRAM 目录 / PSRAM index
    size_t count; ///< 排版分段数 / Layout segment count
    book_txt_nav_t *nav; ///< 仅 Part 回退时构建的逻辑目录 / Logical TOC built only for Part fallback
    size_t nav_count, nav_capacity; ///< 逻辑目录条数及容量 / Logical TOC count and capacity
    uint32_t total; ///< 文件长度 / File length
    bool gbk; ///< GBK 编码 / GBK encoding
    uint32_t bom; ///< BOM 字节数 / BOM size
} book_txt_t;
/// 建立 TXT 目录。/ Build the TXT index.
esp_err_t book_txt_open(book_txt_t *book, const char *path);
/// 加载一章并转为 UTF-8。/ Load a chapter as UTF-8.
esp_err_t book_txt_load(book_txt_t *book, size_t i, char **utf8, size_t *len);
