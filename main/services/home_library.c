/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：复用书架扫描与隐藏规则，目录只在刷新时短暂持有，静止不扫描或刷屏。
 * English: Reuse shelf scanning and hidden rules, holding catalogs only during refresh and doing no idle scanning or repaint. */
#include "home_library.h"
#include "home_components.h"
#include "book_shelf_scan.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_cover_jobs.h"
#include "board_sd.h"
#include "settings.h"
#include "boot_state.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static book_mini_library_t s_model;
static home_library_cover_t s_covers[BOOK_MINI_LIBRARY_MAX];
static book_mini_item_t s_cover_keys[BOOK_MINI_LIBRARY_MAX];
static time_t s_cover_modified[BOOK_MINI_LIBRARY_MAX];
static bool s_cover_failed[BOOK_MINI_LIBRARY_MAX];
static bool s_active, s_valid, s_pending, s_shuffle, s_need_generation;
static bool s_sd_present, s_sd_mounted, s_fast;
static unsigned s_store_revision, s_progress_revision, s_generation, s_contrast;
static char s_root[128], s_notice[97];

static void free_covers(void) {
    for (unsigned i = 0; i < BOOK_MINI_LIBRARY_MAX; ++i) { free((void *)s_covers[i].pixels); s_covers[i] = (home_library_cover_t){0}; }
    memset(s_cover_failed, 0, sizeof(s_cover_failed)); s_need_generation = true;
}
static unsigned *selected(void) { return app_home_library_recent() ? &s_model.recent_selected : &s_model.random_selected; }
home_library_view_t home_library_view(void) {
    bool recent = app_home_library_recent();
    return (home_library_view_t){.books=recent?s_model.recent:s_model.random, .covers=s_covers,
        .count=recent?s_model.recent_count:s_model.random_count, .selected=*selected(), .recent=recent,
        .loading=s_pending, .notice=s_notice};
}
static bool cover_matches(unsigned cached, const book_mini_item_t *book) {
    const book_mini_item_t *key = &s_cover_keys[cached];
    return key->size == book->size && !strcmp(key->path, book->path) &&
        !strcmp(key->title, book->title) && !strcmp(key->author, book->author);
}
static void reconcile_covers(bool check_files) {
    home_library_view_t view = home_library_view();
    home_library_cover_t kept[BOOK_MINI_LIBRARY_MAX] = {0};
    time_t modified[BOOK_MINI_LIBRARY_MAX] = {0};
    for (unsigned j = 0; j < BOOK_MINI_LIBRARY_MAX; ++j) {
        if (!s_covers[j].pixels) continue;
        bool valid = true;
        if (check_files) {
            struct stat st;
            valid = !stat(s_cover_keys[j].path, &st) && S_ISREG(st.st_mode) &&
                st.st_size == s_cover_keys[j].size && st.st_mtime == s_cover_modified[j];
        }
        for (unsigned i = 0; valid && i < view.count; ++i) {
            if (kept[i].pixels || !cover_matches(j, &view.books[i])) continue;
            kept[i] = s_covers[j]; modified[i] = s_cover_modified[j];
            s_covers[j].pixels = NULL;
            break;
        }
        free((void *)s_covers[j].pixels);
    }
    memcpy(s_covers, kept, sizeof(kept));
    memcpy(s_cover_modified, modified, sizeof(modified));
    for (unsigned i = 0; i < view.count; ++i) if (kept[i].pixels) s_cover_keys[i] = view.books[i];
    memset(s_cover_failed, 0, sizeof(s_cover_failed)); s_need_generation = true;
}
void home_library_enter(void) {
    s_active = true;
    bool fast = app_settings_main_fast_refresh(); unsigned contrast = app_settings_system_contrast();
    if (s_fast != fast || s_contrast != contrast) free_covers();
    s_fast = fast; s_contrast = contrast;
    board_sd_info_t sd = {0}; board_sd_get_info(&sd);
    s_pending = !s_valid || s_store_revision != book_store_revision() || s_progress_revision != book_progress_revision() ||
        s_sd_present != sd.present || s_sd_mounted != sd.mounted || strcmp(s_root, app_settings_books_dir());
    if (s_pending) s_notice[0] = 0;
    reconcile_covers(true);
}
void home_library_leave(void) { s_active = false; s_pending = s_shuffle = false; s_generation = 0; s_need_generation = true; }
void home_library_invalidate(void) { s_valid = false; s_pending = true; }
void home_library_media_lost(void) {
    book_mini_library_drop_card(&s_model); free_covers();
    s_valid = false; s_pending = true; s_shuffle = false; s_generation = 0;
}
bool home_library_recent(bool recent) {
    if (recent == app_home_library_recent()) return true;
    if (!app_home_library_set_recent(recent)) { snprintf(s_notice, sizeof(s_notice), "设置保存失败，请稍后重试"); return false; }
    reconcile_covers(false); s_notice[0] = 0; return true;
}
void home_library_select(unsigned index) { if (index < home_library_view().count) *selected() = index; }
void home_library_shuffle(void) { if (!app_home_library_recent()) { s_shuffle = s_pending = true; s_notice[0] = 0; } }
bool home_library_needs_generation(void) { return s_active && s_need_generation; }
void home_library_cover_generation(unsigned generation) {
    s_generation = generation; s_need_generation = false; memset(s_cover_failed, 0, sizeof(s_cover_failed));
}
void home_library_cover_mode_changed(void) { free_covers(); }
static int ignore_category(const char *path) { (void)path; return 0; }
static bool ignore_favorite(nvs_handle_t handle, const char *path) { (void)handle; (void)path; return false; }
static uint32_t random_value(void *ctx) { (void)ctx; return esp_random(); }

static bool scan(bool (*cancel)(void *ctx), void *ctx) {
    book_shelf_entry_t *entries = NULL; int count = 0; size_t capacity = 0;
    book_shelf_scan_t request = {.catalog={&entries,&count,&capacity}, .query="", .cancel=cancel, .cancel_ctx=ctx,
        .category_add=ignore_category, .favorite_read=ignore_favorite, .hidden_read=book_shelf_hidden_read};
    nvs_handle_t hidden = 0;
    esp_err_t error = nvs_open("rp_shelf", NVS_READONLY, &hidden);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        snprintf(s_notice, sizeof(s_notice), "书架状态读取失败，请稍后重试"); return true;
    }
    board_sd_info_t sd = {0}; board_sd_get_info(&sd);
    bool truncated = false, unreadable = false, skipped = false;
    if (sd.mounted) {
        struct stat legacy;
        if (!strcmp(app_settings_books_dir(), "/sdcard/books") && !stat("/sdcard/book", &legacy) && S_ISDIR(legacy.st_mode))
            book_shelf_scan_dir(&request, "/sdcard/book", false, 0, hidden, &truncated, &unreadable, &skipped);
        book_shelf_scan_dir(&request, app_settings_books_dir(), false, 0, hidden, &truncated, &unreadable, &skipped);
    }
    if (book_store_flash_ready()) book_shelf_scan_dir(&request, "/flash/books", true, 0, hidden, &truncated, &unreadable, &skipped);
    if (hidden) nvs_close(hidden);
    if (cancel && cancel(ctx)) { free(entries); return false; }
    // 历史补入复用书架的路径与尺寸校验，随后排除已卸载来源。/ Backfill with shelf path/size checks, then exclude unmounted sources.
    (void)book_shelf_scan_backfill(&request, &truncated);
    for (int i = 0; i < count; ++i)
        if ((!sd.mounted && !strncmp(entries[i].path, "/sdcard/", 8)) ||
            (!book_store_flash_ready() && !strncmp(entries[i].path, "/flash/", 7))) entries[i].removed = true;
    if (cancel && cancel(ctx)) { free(entries); return false; }
    if (truncated) { free(entries); snprintf(s_notice, sizeof(s_notice), "书目较多，暂不能完整读取"); return true; }
    if (unreadable) { free(entries); snprintf(s_notice, sizeof(s_notice), "书库暂不可用，请稍后重试"); return true; }
    book_mini_library_update(&s_model, entries, (size_t)count, s_shuffle, random_value, NULL);
    free(entries); reconcile_covers(true);
    s_valid = !skipped; s_notice[0] = 0;
    if (skipped) snprintf(s_notice, sizeof(s_notice), "部分书目无法读取");
    s_store_revision = book_store_revision(); s_progress_revision = book_progress_revision();
    s_sd_present = sd.present; s_sd_mounted = sd.mounted;
    snprintf(s_root, sizeof(s_root), "%s", app_settings_books_dir());
    return true;
}

bool home_library_poll(bool (*cancel)(void *ctx), void *ctx) {
    if (!s_active) return false;
    if (s_pending) {
        if (board_sd_get_info(&(board_sd_info_t){0}) == ESP_ERR_NOT_FINISHED) return false;
        bool completed = scan(cancel, ctx);
        if (completed) s_pending = s_shuffle = false;
        return completed;
    }
    bool fast = app_settings_main_fast_refresh(); unsigned contrast = app_settings_system_contrast();
    if (s_fast != fast || s_contrast != contrast) { s_fast = fast; s_contrast = contrast; free_covers(); return true; }
    if (s_need_generation || !s_generation) return false;
    home_library_view_t view = home_library_view();
    for (unsigned n = 0; n < view.count; ++n) {
        unsigned i = (view.selected + n) % view.count;
        if (s_covers[i].pixels || s_cover_failed[i]) continue;
        if (!pico_boot_asset_allowed(view.books[i].path)) { s_cover_failed[i] = true; continue; }
        // 最多五个176×240灰阶结果；低内存时保留文字封面。/ Bound results to five 176x240 gray covers; keep text covers on low memory.
        if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < 512u*1024u) { s_cover_failed[i] = true; continue; }
        book_cover_job_t job = {.list=true, .fast=fast, .natural=true, .width=176, .height=240,
            .stride=fast?22:176, .contrast=contrast, .phase=17};
        snprintf(job.path, sizeof(job.path), "%s", view.books[i].path);
        snprintf(job.title, sizeof(job.title), "%s", view.books[i].title);
        snprintf(job.author, sizeof(job.author), "%s", view.books[i].author);
        if (!book_cover_jobs_submit(s_generation, i+1, &job)) { s_cover_failed[i] = true; continue; }
        book_cover_result_t result = {0};
        int status = book_cover_jobs_take(s_generation, i+1, &result);
        if (!status) return false;
        if (status < 0) { s_cover_failed[i] = true; continue; }
        if (result.size != view.books[i].size) { free(result.pixels); s_cover_failed[i] = true; continue; }
        unsigned width = 176, height = 240;
        if (!result.width || !result.height) { free(result.pixels); s_cover_failed[i] = true; continue; }
        if ((uint64_t)result.width * height > (uint64_t)result.height * width)
            height = (uint64_t)result.height * width / result.width;
        else width = (uint64_t)result.width * height / result.height;
        if (!width) width = 1;
        if (!height) height = 1;
        unsigned stride = fast ? (width + 7) / 8 : width;
        uint8_t *small = heap_caps_calloc(height, stride, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (small) for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
            unsigned sx = (uint64_t)x * result.width / width, sy = (uint64_t)y * result.height / height;
            if (fast) {
                if (result.pixels[sy * ((result.width + 7) / 8) + sx / 8] & (1u << (sx & 7)))
                    small[y * stride + x / 8] |= 1u << (x & 7);
            } else {
                int gray = 128 + ((int)result.pixels[sy * result.width + sx] - 128) * (int)contrast / 100;
                small[y * stride + x] = gray < 0 ? 0 : gray > 255 ? 255 : gray;
            }
        }
        free(result.pixels);
        if (!small) { s_cover_failed[i] = true; continue; }
        s_covers[i] = (home_library_cover_t){small,width,height,fast};
        s_cover_keys[i] = view.books[i]; s_cover_modified[i] = result.modified;
        return true;
    }
    return false;
}
