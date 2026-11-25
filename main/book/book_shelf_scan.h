/*
 * 中文：书架目录扫描、历史补入和删除重试合并；同步借用目录，不绘制或导航。
 * English: Shelf directory scanning, history backfill and deletion-retry merging; borrow the catalog synchronously without painting or navigation.
 */
#pragma once
#include "book_shelf_model.h"
#include "nvs.h"

typedef struct {
    book_shelf_entry_t **entries; ///< 页面持有的条目数组，增长时更新 / Page-owned array, updated when grown
    int *count; ///< 已用条目数 / Used entry count
    size_t *capacity; ///< 已分配条目容量 / Allocated entry capacity
} book_shelf_catalog_t;

typedef struct {
    book_shelf_catalog_t catalog; ///< 同步借用；页面负责最终释放 / Synchronous borrow; the page performs final release
    const char *query; ///< 本次搜索文本 / Search text for this call
    bool (*cancel)(void *ctx); ///< 检查是否取消；NULL表示不检查 / Cooperative cancellation; NULL disables checks
    void *cancel_ctx; ///< 取消回调上下文 / Cancellation context
    int (*category_add)(const char *path); ///< 可选：登记书根一级目录 / Optional: register first-level book-root folders
    bool (*favorite_read)(nvs_handle_t handle, const char *path); ///< 借用已打开的收藏句柄 / Borrow an open favorites handle
    bool (*hidden_read)(nvs_handle_t handle, const char *path); ///< 借用已打开的隐藏记录句柄 / Borrow an open hidden-record handle
    nvs_handle_t progress; ///< 已尝试打开的只读进度句柄，扫描借用 / Borrowed read-only progress handle for this scan
    bool progress_checked; ///< 已完成一次进度命名空间打开尝试 / Progress namespace open was attempted once
    struct book_shelf_cache *cache; ///< 稳定条目缓存；按路径、大小和修改时间校验 / Static-entry cache, validated by path, size and mtime
    bool metadata; ///< 需要显示作者时才读缓存 / Consult metadata caches only for views displaying authors
} book_shelf_scan_t;

typedef struct book_shelf_cache book_shelf_cache_t;

/// 打开按 TF 卡身份隔离的稳定条目缓存；缓存损坏时返回空缓存并继续扫描。
/// Open the cache isolated by TF identity; corrupt data becomes an empty cache and scanning continues.
book_shelf_cache_t *book_shelf_cache_open(const char *identity);
/// 用路径、大小和修改时间查找稳定标题/作者；不匹配时返回 false。
/// Find stable title/author by path, size and mtime; return false on a mismatch.
bool book_shelf_cache_lookup(const book_shelf_cache_t *cache, const char *path, uint32_t size,
                             int64_t modified, char *name, size_t name_cap,
                             char *author, size_t author_cap);
/// 完整扫描成功后原子保存当前条目的稳定字段；失败不影响本次书架结果。
/// Atomically save stable fields after a complete scan; failures do not affect this shelf result.
bool book_shelf_cache_save(book_shelf_cache_t *cache, const book_shelf_entry_t *entries, int count);
/// 释放缓存句柄与其外部内存。
/// Release the cache handle and external memory.
void book_shelf_cache_close(book_shelf_cache_t *cache);

struct book_shelf_retry;
/// 路径匹配才视为移出书架；首页组件与书架共用隐藏规则。/ Hide only exact path matches; shared by Home components and the shelf.
bool book_shelf_hidden_read(nvs_handle_t handle, const char *path);
/// 保留原PSRAM增长策略；失败保持数组和容量不变。/ Keep the original PSRAM growth policy; failure preserves array and capacity.
bool book_shelf_catalog_reserve(const book_shelf_catalog_t *catalog);
/// 核对缓存作者或明确署名；非缓存模式允许读取一本书的头部，不读正文。
/// Resolve a cached creator or explicit byline; non-cache mode may read one book header, never body text.
void book_shelf_resolve_author(book_shelf_entry_t *item, bool cache_only);
/// 使用有界PSRAM目录栈；停止或失败前关闭本次打开的所有目录。
/// Use a bounded PSRAM directory stack; close all directories opened by this call on stop or failure.
void book_shelf_scan_dir(const book_shelf_scan_t *scan, const char *root, bool is_flash,
                         nvs_handle_t favorites, nvs_handle_t hidden,
                         bool *truncated, bool *unreadable, bool *skipped);
/// 按原顺序补入尺寸匹配的历史；返回是否新增，保留原枚举失败处理。
/// Backfill size-matched history in the original order; report additions and retain original enumeration-error handling.
bool book_shelf_scan_backfill(const book_shelf_scan_t *scan, bool *truncated);
/// 同路径重现仍以旧删除清理记录覆盖；不转移重试记录所有权。
/// Old deletion-cleanup records override reappearing paths; retry ownership never transfers.
void book_shelf_scan_retries(const book_shelf_scan_t *scan, const struct book_shelf_retry *retries,
                             bool *truncated);
