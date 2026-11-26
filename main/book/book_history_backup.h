/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
struct restore_nvs;
#include <stdbool.h>
#include <stdio.h>
#include "esp_err.h"

// 阅读资料按记录流附在设置备份后，避免把大量书籍进度装入内存。
// Append reading data as a stream so a large library does not require a large allocation.
esp_err_t book_history_backup_write(FILE *file, char *bad_slot, size_t cap);
bool book_history_backup_validate(FILE *file);
/// 非空 image 只演练，不发布卡书签或修改阅读记录。/ A non-null image rehearses without publishing bookmarks or changing live history.
esp_err_t book_history_backup_restore(FILE *file, struct restore_nvs *image);
