/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：有界的离线词库查询，无字体、UI 或网络依赖。
 * English: Bounded offline lookup without font, UI or network dependencies. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BOOK_DICTIONARY_RAW_MAX 8192u
#define BOOK_DICTIONARY_QUERY_MAX 128u

typedef enum { BOOK_DICTIONARY_OK, BOOK_DICTIONARY_MISSING, BOOK_DICTIONARY_NOT_FOUND,
    BOOK_DICTIONARY_UNSUPPORTED, BOOK_DICTIONARY_CORRUPT, BOOK_DICTIONARY_IO,
    BOOK_DICTIONARY_CANCELLED, BOOK_DICTIONARY_BUSY } book_dictionary_status_t;
typedef struct {
    book_dictionary_status_t status;
    char query[129], reading[512], definition[5632], example[1024], variant[129], source[160];
} book_dictionary_result_t;
typedef bool (*book_dictionary_cancel_fn)(void *context);
/// 手工文件优先，兼容旧安装目录；缺失时仍返回标准手工路径。
/// Prefer the manual file, retaining legacy generations; return the manual path when absent.
bool book_dictionary_path(const char *root, char *path, size_t capacity);
/// 每次独立只读打开并关闭；取消回调仅观察调用方令牌。/ Open and close read-only per lookup; cancellation observes caller tokens only.
book_dictionary_status_t book_dictionary_lookup(const char *path, const char *query,
    book_dictionary_result_t *result, book_dictionary_cancel_fn cancelled, void *context);
