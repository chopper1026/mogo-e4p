/*
 * 中文：提供 TF 卡上的阅读书签原子文件存储；不负责界面或书籍排版。
 * English: Provide atomic TF-card bookmark files without owning UI or book layout.
 *
 * 冻结：仅为 /sdcard 书籍工作；内置存储书籍继续由调用方选择其他持久化方式。
 * Frozen: Operate only for /sdcard books; callers choose another store for internal books.
 */
#pragma once

#include "book_bookmark_format.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/// 读取指定书籍的 TF 卡书签；调用方负责 free 返回记录。/ Load card bookmarks; caller frees the returned record.
esp_err_t book_bookmark_store_load(const char *path, uint32_t file_size,
                                   reader_bookmarks_unbounded_t **out);
/// 原子保存指定书籍的 TF 卡书签。/ Atomically save card bookmarks for a book.
esp_err_t book_bookmark_store_save(const reader_bookmarks_unbounded_t *marks);
/// 删除指定书籍的 TF 卡书签文件；不存在也算成功。/ Remove a card bookmark file; absence is success.
esp_err_t book_bookmark_store_remove(const char *path);
/// 随书籍改名或移动书签文件，并更新记录中的路径。/ Relocate and rebase a card bookmark file.
esp_err_t book_bookmark_store_relocate(const char *old_path, const char *new_path,
                                       uint32_t file_size);

/// 枚举已提交记录（含中断换名后保留的旧版）。/ Visit committed records including interrupted replacements.
typedef esp_err_t (*book_bookmark_visit_fn)(const reader_bookmarks_unbounded_t *, size_t, void *);
esp_err_t book_bookmark_store_visit(book_bookmark_visit_fn visit, void *context, char *bad_slot, size_t cap);
/// 恢复时先写独立目录，校验成功后整体替换。/ Stage a separate restore directory and publish it only after validation.
esp_err_t book_bookmark_restore_begin(void);
esp_err_t book_bookmark_restore_put(const reader_bookmarks_unbounded_t *marks);
esp_err_t book_bookmark_restore_finish(void);
