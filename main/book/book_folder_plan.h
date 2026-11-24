/* 中文：书架文件操作的只读预览和执行边界。/ English: Read-only previews and execution boundaries for shelf file operations. */
#pragma once
#include "book_category_ops.h"
#include "book_collection.h"

typedef enum {
    FOLDER_CREATE,
    FOLDER_REUSE,
    FOLDER_MOVE,
    FOLDER_REMOVE,
    FOLDER_KEEP,
    FOLDER_SKIP,
    FOLDER_CONFLICT
} book_folder_action_t;
typedef struct {
    book_folder_action_t action;
    char from[BOOK_STORE_PATH_MAX], to[BOOK_STORE_PATH_MAX];
    uint32_t size, target_hash;
    int64_t modified;
    esp_err_t error;
} book_folder_item_t;
typedef struct {
    char root[BOOK_STORE_PATH_MAX];
    book_folder_item_t* items;
    size_t count, capacity, moves, completed;
    bool changed;
    esp_err_t error;
    book_path_state_extra_fn check;
} book_folder_plan_t;
/// 首次调用前零初始化。/ Zero-initialize before first use.
/// 计划拥有路径副本，取消时无写入。/ The plan owns path copies; cancellation performs no writes.
void book_folder_begin(book_folder_plan_t* plan, const char* root, book_path_state_extra_fn check);
void book_folder_clear(book_folder_plan_t* plan);
/// 计划新建目录，迁移可复用原名完全相同的目录。/ Plan a mkdir, allowing exact-name reuse during migration.
void book_folder_directory(book_folder_plan_t* plan, const char* name, bool reuse);
void book_folder_add_move(
    book_folder_plan_t* plan, const char* source, const char* directory, bool migration);
/// 非破坏性枚举目录，不忽略隐藏项。/ Inspect every directory entry, including hidden ones, without mutating it.
void book_folder_delete(book_folder_plan_t* plan, const char* source);
/// 只解释旧索引明确指定目标的书。/ Interpret only explicitly assigned legacy paths.
void book_folder_migrate(book_folder_plan_t* plan, const book_collection_t* legacy);
bool book_folder_same_plan(const book_folder_plan_t* a, const book_folder_plan_t* b);
/// 调用者独占介质；每项写入前再次核实，首个失败即停。/ Caller owns the medium; recheck each mutation and stop on first failure.
esp_err_t book_folder_execute(book_folder_plan_t* plan, book_path_state_extra_fn relocate);
