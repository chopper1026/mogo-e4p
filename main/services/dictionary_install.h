/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：官方词库下载任务，取消后等待释放网络与卡句柄。
 * English: Official dictionary downloads; cancellation joins network and card handles. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool busy, installed;
    uint32_t received, total, revision;
    char message[160];
} dictionary_install_status_t;
bool dictionary_install_start(void);
bool dictionary_install_busy(void);
void dictionary_install_get_status(dictionary_install_status_t *status);
void dictionary_install_cancel(void);
