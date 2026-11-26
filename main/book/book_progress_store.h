// 内部 FAT 分桶进度；文件名不含用户路径，备份复用原 RPB 字节。/ Bucketed internal progress with path-free filenames and portable RPB bytes.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "book_path.h"
#include "esp_err.h"

#define PROGRESS_HEADER 24
#define PROGRESS_CAP (PROGRESS_HEADER + BOOK_STORE_PATH_MAX)
void progress_key(const char* path, char key[11]);
bool progress_record_valid(const char* key, const uint8_t* data, size_t len);
/// 调用者先挂载内部卷；未找到与 I/O 或坏数据严格区分。/ Caller mounts first; distinguish absence from I/O and corruption.
esp_err_t progress_store_get(const char* path, uint8_t out[PROGRESS_CAP], size_t* len);
/// data 为空时删除，只修改同路径记录。/ A null value removes only the matching owner.
esp_err_t progress_store_put(const char* path, const uint8_t* data, size_t len);
typedef esp_err_t (*progress_store_visitor)(
    const char* key, const uint8_t* data, size_t len, void* ctx);
esp_err_t progress_store_visit(progress_store_visitor fn, void* ctx, bool skip_damaged);
/// 暂存完整替换历史；发布前不影响读取，重复键禁止。/ Stage a replacement without affecting reads; reject duplicate keys.
esp_err_t progress_stage_begin(void);
esp_err_t progress_stage_put(const uint8_t* data, size_t len);
void progress_stage_discard(void);
esp_err_t progress_stage_finish(void);
