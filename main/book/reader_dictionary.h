/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：单任务查询交还；任务仅拥有复制的查询，不借用章节。
 * English: Single-job lookup handoff; the worker owns copied queries, never chapter pointers. */
#pragma once
#include "book_dictionary.h"
/// 重请求先交还旧任务，锁屏/拔卡/离页必须调用取消。/ Drain old work before replacement; cancel before lock, media loss or exit.
bool reader_dictionary_start(const char *query);
bool reader_dictionary_poll(book_dictionary_result_t *result);
void reader_dictionary_cancel(void);
