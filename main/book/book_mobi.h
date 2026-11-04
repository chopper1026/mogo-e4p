/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：独立 MOBI/PRC 书源，按 PalmDOC 文本记录提供章节与文本块。
 * English: Independent MOBI/PRC source exposing chapters and text blocks from PalmDOC text records.
 *
 * 冻结：进度使用解压后正文字节偏移，不是 PDB 记录物理偏移；只读，不解析 DRM，不写文件。
 * Frozen: Progress uses decompressed text offsets, not physical PDB record offsets; read-only,
 * no DRM handling, never writes files.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "html_text.h"

typedef struct book_mobi book_mobi_t;

/// 打开 MOBI；失败时 out 置空，成功后由 close 释放。/ Open MOBI; clear out on failure, close owns cleanup.
esp_err_t book_mobi_open(const char *path, book_mobi_t **out);
/// 关闭句柄并释放索引，可传 NULL。/ Close the handle and free the index; NULL is allowed.
void book_mobi_close(book_mobi_t *book);
/// 返回章节数，上限 2048。/ Return the chapter count, at most 2048.
size_t book_mobi_chapter_count(const book_mobi_t *book);
/// 复制 UTF-8 标题，容量不足报错而不截断。/ Copy a UTF-8 title; report insufficient capacity instead of truncating.
esp_err_t book_mobi_chapter_title(book_mobi_t *book, size_t index, char *buf, size_t cap);
/// 解压本章并按 HTML 解析成文本与块表；调用方 html_text_free。
/// Decompress this chapter and parse it into text and blocks; the caller uses html_text_free.
esp_err_t book_mobi_load(book_mobi_t *book, size_t index, html_text_t *out);
/// 解压后正文总字节数，等于 PalmDOC 头的 text length。/ Total decompressed text bytes, the PalmDOC text length.
uint32_t book_mobi_total_bytes(const book_mobi_t *book);
/// 该章在解压正文中的起始偏移。/ Start offset of this chapter inside the decompressed text.
uint32_t book_mobi_chapter_byte_offset(const book_mobi_t *book, size_t index);
/// 读取 recindex 指向的 JPEG/PNG 记录；<img recindex> 已被改写成 src="rec:N"。
/// Read a JPEG/PNG record by recindex; <img recindex> has already been rewritten to src="rec:N".
esp_err_t book_mobi_image(book_mobi_t *book, const char *src, uint8_t **data, size_t *size, bool *png);
esp_err_t book_mobi_image_dimensions(book_mobi_t *book, const char *src, unsigned *width, unsigned *height);
/// 从 EXTH 封面声明读取 JPEG/PNG 原始数据；调用方 free。/ Read the EXTH-declared cover bytes; caller frees.
esp_err_t book_mobi_cover(const char *path, uint8_t **data, size_t *size, bool *is_png);
/// 在分配封面前检查调用方的内存预算。/ Check the caller's cover budget before allocating.
esp_err_t book_mobi_cover_bounded(const char *path, uint8_t **data, size_t *size,
                                  bool *is_png, size_t budget);
/// 只读 EXTH/PDB 头部的书名和作者；供书架显示。/ Read only the EXTH/PDB title and creator for the shelf.
esp_err_t book_mobi_metadata(const char *path, char *title, size_t title_cap, char *author, size_t author_cap);

#if KIIKO_BOARD_METALIO47
/// 浅睡仅暂停只读句柄，不释放章节索引。/ Pause the read handle without discarding the chapter index.
esp_err_t book_mobi_suspend_io(book_mobi_t *book);
esp_err_t book_mobi_resume_io(book_mobi_t *book);
#else
#endif
