/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 书目录与 FAT 容量查询；调用方负责先显示首次初始化提示。
 * Book directories and FAT capacity queries; callers show initialization feedback first.
 *
 * 冻结：不格式化 SD；内部 FAT 承载用户进度，只允许书库初始化或已确认且通过校验的配置恢复
 * 初始化全为擦除态的逻辑卷；存在数据或读错时禁止格式化。
 * Frozen: never format SD. Only library setup or a confirmed, validated settings restore may
 * initialize a fully erased internal logical volume. Never format on existing data or a read error.
 * 决策变更：整擦后可直接恢复配置，无需先进入书架。
 * Decision change: restore after a full erase no longer depends on opening the shelf first.
 * 冻结：有卡但挂载失效时不把上传静默切换到内置存储；拔卡不复用旧容量。
 * Frozen: present but invalid media must not silently redirect uploads to flash; never reuse removed-card capacity.
 * 冻结：用户确认后仅删已挂载根内的直接普通书籍；文件与进度部分失败必须返回，不通知 UI 版本。
 * Frozen: after user confirmation, delete only direct regular books in mounted roots; report partial failures without notifying UI revision.
 * 冻结：删除前清除同名普通覆盖备份；仅有备份时先恢复，失败保留可恢复的原书。
 * Frozen: clear a matching regular replacement backup before deletion; restore backup-only books first and preserve recovery on failure.
 */
#include "book_store.h"
#include "book_progress.h"
#include "settings.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "board_sd.h"
#include "wear_levelling.h"

static const char* TAG = "book_store";
static pthread_mutex_t mount_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic wl_handle_t s_wl = WL_INVALID_HANDLE;
static unsigned s_revision;
static bool s_roots_degraded;

void book_store_notify_changed(void) { ++s_revision; }
unsigned book_store_revision(void) { return s_revision; }
bool book_store_roots_degraded(void) { return s_roots_degraded; }

static esp_err_t ensure_dir(const char* path) {
    if (mkdir(path, 0777) == 0) return ESP_OK;
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return ESP_OK;
    }
    ESP_LOGW(TAG, "book directory unavailable: %s (%d)", path, errno);
    return ESP_FAIL;
}

static bool sd_ready(void) {
    board_sd_info_t info = {0};
    board_sd_get_info(&info);
    return info.present && info.mounted;
}

static bool erased_volume(wl_handle_t h) {
    uint8_t bytes[512];
    size_t size = wl_size(h);
    if (!size || size % sizeof(bytes)) return false;
    for (size_t off=0; off<size; off+=sizeof(bytes)) {
        if (wl_read(h,off,bytes,sizeof(bytes))!=ESP_OK) return false;
        for (size_t i=0; i<sizeof(bytes); ++i) if (bytes[i]!=0xff) return false;
    }
    return true;
}
static esp_err_t mount_flash(bool initialize_blank) {
    pthread_mutex_lock(&mount_lock);
    esp_err_t err=ESP_OK;
    if (!book_store_flash_ready()) {
        esp_vfs_fat_mount_config_t config = {.format_if_mount_failed=false, .max_files=4, .allocation_unit_size=4096};
        for (;;) {
            wl_handle_t handle=WL_INVALID_HANDLE;
            err=esp_vfs_fat_spiflash_mount_rw_wl("/flash","storage",&config,&handle);
            if (err==ESP_OK) { s_wl=handle; break; }
            if (handle==WL_INVALID_HANDLE) break;
            // 失败后先检查已取得的逻辑卷，再释放 WL；有任何数据或读错均不重试格式化。
            // Check the acquired logical volume before releasing WL; any data or read error forbids formatting.
            bool blank=initialize_blank && !config.format_if_mount_failed && erased_volume(handle);
            esp_err_t cleanup=wl_unmount(handle);
            if (cleanup!=ESP_OK) { ESP_LOGW(TAG,"failed mount cleanup: %s",esp_err_to_name(cleanup)); break; }
            if (!blank) break;
            config.format_if_mount_failed=true;
        }
    }
    pthread_mutex_unlock(&mount_lock);
    return err;
}

static esp_err_t flash_root(book_store_root_t* out) {
    if (!book_store_flash_ready()) {
        esp_err_t err = mount_flash(true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "flash mount: %s", esp_err_to_name(err));
            return err;
        }
    }
    esp_err_t err = ensure_dir("/flash/books");
    if (err == ESP_OK) *out = (book_store_root_t){.path = "/flash/books", .is_flash = true};
    return err;
}

esp_err_t book_store_roots(book_store_root_t out[BOOK_STORE_ROOT_MAX], int* n) {
    if (out == NULL || n == NULL) return ESP_ERR_INVALID_ARG;
    *n = 0;
    s_roots_degraded = false;
    memset(out, 0, sizeof(*out) * BOOK_STORE_ROOT_MAX);
    if (sd_ready()) {
        // Also show books already copied to a singular `book` directory.
        struct stat legacy;
        if (!strcmp(app_settings_books_dir(), "/sdcard/books") && stat("/sdcard/book", &legacy) == 0 && S_ISDIR(legacy.st_mode))
            out[(*n)++] = (book_store_root_t){.path = "/sdcard/book", .is_flash = false};
        if (ensure_dir(app_settings_books_dir()) == ESP_OK)
            snprintf(out[(*n)++].path, sizeof(out[0].path), "%s", app_settings_books_dir());
        else s_roots_degraded = true;
    } else {
        board_sd_info_t info = {0};
        board_sd_get_info(&info);
        if (info.present) s_roots_degraded = true;
    }
    esp_err_t err = flash_root(&out[*n]);
    if (err == ESP_OK) ++*n;
    else s_roots_degraded = true;
    return *n > 0 ? ESP_OK : err;
}

esp_err_t book_store_upload_root(book_store_root_t* out) {
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!sd_ready()) {
        board_sd_info_t info = {0};
        board_sd_get_info(&info);
        // 有卡但挂载失效时让用户显式重挂，不能悄悄改写内置目录。
        // Present but invalid media requires an explicit remount, not silent flash redirection.
        if (info.present) return ESP_ERR_INVALID_STATE;
        return flash_root(out);
    }
    esp_err_t err = ensure_dir(app_settings_books_dir());
    if (err == ESP_OK) snprintf(out->path, sizeof(out->path), "%s", app_settings_books_dir());
    return err;
}

size_t book_store_file_limit(const book_store_root_t* root) {
    if (root == NULL) return 0;
    return root->is_flash ? BOOK_STORE_FLASH_FILE_MAX : SIZE_MAX;
}

uint64_t book_store_free_bytes(const book_store_root_t* root) {
    if (root == NULL) return 0;
    if (!root->is_flash && !sd_ready()) return 0;
    uint64_t total = 0, free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(root->is_flash ? "/flash" : "/sdcard", &total, &free_bytes);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FAT capacity: %s", esp_err_to_name(err));
        return 0;
    }
    return free_bytes;
}

bool book_store_flash_ready(void) {
    return s_wl != WL_INVALID_HANDLE;
}

esp_err_t book_store_prepare_data(void) {
    if (book_store_flash_ready()) return ESP_OK;
    return mount_flash(false);
}

esp_err_t book_store_prepare_restore_data(void) {
    return mount_flash(true);
}

esp_err_t book_store_delete(const char* path, bool* removed) {
    if (!removed) return ESP_ERR_INVALID_ARG;
    *removed = false;
    if (!path || strnlen(path, BOOK_STORE_PATH_MAX) >= BOOK_STORE_PATH_MAX) return ESP_ERR_INVALID_ARG;
    const char* root;
    const char* name;
    size_t selected_len = strlen(app_settings_books_dir());
    if (!strncmp(path, app_settings_books_dir(), selected_len) && path[selected_len] == '/') {
        root = app_settings_books_dir(); name = path + selected_len + 1;
        if (!sd_ready()) return ESP_ERR_INVALID_STATE;
    } else if (!strcmp(app_settings_books_dir(), "/sdcard/books") && !strncmp(path, "/sdcard/book/", 13)) {
        root = "/sdcard/book"; name = path + 13;
        if (!sd_ready()) return ESP_ERR_INVALID_STATE;
    } else if (!strncmp(path, "/flash/books/", 13)) {
        root = "/flash/books"; name = path + 13;
        if (!book_store_flash_ready()) return ESP_ERR_INVALID_STATE;
    } else return ESP_ERR_INVALID_ARG;
    size_t len = strlen(name);
    if (!len || len > 255) return ESP_ERR_INVALID_ARG;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p) {
        if (*p < 32 || *p == 127 || *p == '/' || *p == '\\' || *p == ':') return ESP_ERR_INVALID_ARG;
    }
    const char* ext = strrchr(name, '.');
    if (!ext || (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub") && strcasecmp(ext, ".mobi"))) return ESP_ERR_INVALID_ARG;
    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return ESP_ERR_INVALID_STATE;
    // FAT 没有符号链接；主机测试用 lstat，确保不会跟随主机链接删其他文件。
    // FAT has no symlinks; host builds use lstat to reject host links rather than follow them.
#ifdef ESP_PLATFORM
    int result = stat(path, &st);
#else
    int result = lstat(path, &st);
#endif
    bool exists = result == 0;
    if (!exists && errno != ENOENT) return ESP_FAIL;
    if (exists && !S_ISREG(st.st_mode)) return ESP_ERR_INVALID_ARG;
    // 显式删除也处理覆盖备份，避免下次传书启动恢复已删图书。
    // Explicit deletion handles replacement backups so transfer startup cannot resurrect deleted books.
    const char suffix[] = ".rename-backup";
    if (len + sizeof(suffix) - 1 <= 255) {
        char backup[BOOK_STORE_PATH_MAX + sizeof(suffix)];
        snprintf(backup, sizeof(backup), "%s%s", path, suffix);
#ifdef ESP_PLATFORM
        result = stat(backup, &st);
#else
        result = lstat(backup, &st);
#endif
        if (result != 0) {
            if (errno != ENOENT) return ESP_FAIL;
        } else {
            if (!S_ISREG(st.st_mode)) return ESP_ERR_INVALID_ARG;
            if (exists) {
                if (unlink(backup) != 0 && errno != ENOENT) return ESP_FAIL;
            } else {
                // 未完成覆盖先恢复旧书；后续删除失败时仍保留可见原书。
                // Restore an interrupted replacement first, keeping the old book visible if deletion fails.
                if (rename(backup, path) != 0) return ESP_FAIL;
                exists = true;
            }
        }
    }
    if (exists) {
        if (unlink(path) != 0 && errno != ENOENT) return ESP_FAIL;
    }
    *removed = true;
    return book_progress_forget(path);
}
