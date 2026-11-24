/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：复用书架扫描与隐藏规则，目录只在刷新时短暂持有，静止不扫描或刷屏。
 * English: Reuse shelf scanning and hidden rules, holding catalogs only during refresh and doing no idle scanning or repaint. */
#include "../ui/ui_locale.h"
#include "home_library.h"
#include "home_library_jobs.h"
#include "home_components.h"
#include "book_shelf_scan.h"
#include "book_metadata.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_cover_jobs.h"
#include "board_sd.h"
#include "settings.h"
#include "boot_state.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static book_mini_library_t s_model;
static home_library_cover_t s_covers[BOOK_MINI_LIBRARY_MAX];
static book_mini_item_t s_cover_keys[BOOK_MINI_LIBRARY_MAX];
static time_t s_cover_modified[BOOK_MINI_LIBRARY_MAX];
static bool s_cover_failed[BOOK_MINI_LIBRARY_MAX];
typedef enum { LIBRARY_NONE, LIBRARY_CHECK, LIBRARY_PROGRESS, LIBRARY_FULL } library_work_t;
typedef struct { bool valid; time_t modified; } library_file_t;
typedef struct {
    atomic_bool cancelled;
    library_work_t work;
    bool shuffle, updated, complete, present, mounted, flash;
    unsigned store_revision, progress_revision;
    char root[128], identity[40];
    book_mini_library_t model;
    library_file_t files[2][BOOK_MINI_LIBRARY_MAX];
    const char *notice;
} library_job_t;
static library_job_t *s_job;
static atomic_bool s_running;
static library_work_t s_work;
static bool s_active, s_valid, s_shuffle, s_need_generation;
static bool s_sd_present, s_sd_mounted, s_flash, s_fast;
static unsigned s_store_revision, s_progress_revision, s_generation, s_contrast;
static char s_root[128], s_identity[40], s_notice[97];

static void free_covers(void) {
    for (unsigned i = 0; i < BOOK_MINI_LIBRARY_MAX; ++i) { free((void *)s_covers[i].pixels); s_covers[i] = (home_library_cover_t){0}; }
    memset(s_cover_failed, 0, sizeof(s_cover_failed)); s_need_generation = true;
}
static unsigned *selected(void) { return app_home_library_recent() ? &s_model.recent_selected : &s_model.random_selected; }
home_library_view_t home_library_view(void) {
    bool recent = app_home_library_recent();
    return (home_library_view_t){.books=recent?s_model.recent:s_model.random, .covers=s_covers,
        .count=recent?s_model.recent_count:s_model.random_count, .selected=*selected(), .recent=recent,
        .loading=s_work == LIBRARY_FULL, .notice=s_notice};
}
static bool cover_matches(unsigned cached, const book_mini_item_t *book) {
    const book_mini_item_t *key = &s_cover_keys[cached];
    return key->size == book->size && !strcmp(key->path, book->path) &&
        !strcmp(key->title, book->title) && !strcmp(key->author, book->author);
}
static bool reconcile_covers(const library_file_t *files) {
    home_library_view_t view = home_library_view();
    home_library_cover_t kept[BOOK_MINI_LIBRARY_MAX] = {0};
    time_t modified[BOOK_MINI_LIBRARY_MAX] = {0};
    bool changed = false;
    for (unsigned j = 0; j < BOOK_MINI_LIBRARY_MAX; ++j) {
        if (!s_covers[j].pixels) continue;
        for (unsigned i = 0; i < view.count; ++i) {
            if (kept[i].pixels || !cover_matches(j, &view.books[i]) ||
                (files && (!files[i].valid || files[i].modified != s_cover_modified[j]))) continue;
            changed |= i != j;
            kept[i] = s_covers[j]; modified[i] = s_cover_modified[j];
            s_covers[j].pixels = NULL;
            break;
        }
        changed |= s_covers[j].pixels != NULL;
        free((void *)s_covers[j].pixels);
    }
    memcpy(s_covers, kept, sizeof(kept));
    memcpy(s_cover_modified, modified, sizeof(modified));
    for (unsigned i = 0; i < view.count; ++i) if (kept[i].pixels) s_cover_keys[i] = view.books[i];
    if (changed) s_need_generation = true;
    return changed;
}
static void cancel_scan(void) {
    if (!s_job) return;
    atomic_store(&s_job->cancelled, true);
    if (!atomic_load(&s_running)) { free(s_job); s_job = NULL; }
}

static void queue_work(library_work_t work) {
    cancel_scan();
    s_work = work;
    if (work >= LIBRARY_PROGRESS) s_notice[0] = 0;
}

static bool same_media(const board_sd_info_t *sd) {
    return sd->present == s_sd_present && sd->mounted == s_sd_mounted && !strcmp(sd->identity, s_identity);
}

static library_work_t refresh_work(const board_sd_info_t *sd) {
    if (!s_valid || s_store_revision != book_store_revision() || !same_media(sd) ||
        s_flash != book_store_flash_ready() || strcmp(s_root, app_settings_books_dir()))
        return LIBRARY_FULL;
    return s_progress_revision != book_progress_revision() ? LIBRARY_PROGRESS : LIBRARY_CHECK;
}

void home_library_enter(void) {
    s_active = true;
    bool fast = app_settings_main_fast_refresh(); unsigned contrast = app_settings_system_contrast();
    if (s_fast != fast || s_contrast != contrast) free_covers();
    s_fast = fast; s_contrast = contrast;
    board_sd_info_t sd = {0}; (void)board_sd_get_info(&sd);
    if (strcmp(sd.identity, s_identity) && s_sd_mounted) {
        book_mini_library_drop_card(&s_model); free_covers();
    }
    queue_work(refresh_work(&sd));
}
void home_library_leave(void) { s_active = false; s_shuffle = false; s_generation = 0; s_need_generation = true; queue_work(LIBRARY_NONE); }
void home_library_pause(void) { cancel_scan(); s_shuffle = false; }
void home_library_invalidate(void) { s_valid = false; queue_work(LIBRARY_FULL); }
void home_library_media_lost(void) {
    book_mini_library_drop_card(&s_model); free_covers();
    s_shuffle = false; s_generation = 0; home_library_invalidate();
}
bool home_library_quiesce(unsigned timeout_ms) {
    cancel_scan();
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (atomic_load(&s_running)) {
        if (esp_timer_get_time() >= deadline) return false;
        vTaskDelay(1);
    }
    cancel_scan();
    return true;
}
bool home_library_recent(bool recent) {
    if (recent == app_home_library_recent()) return true;
    if (!app_home_library_set_recent(recent)) {
        snprintf(s_notice, sizeof(s_notice), "%s",ui_tr("设置保存失败，请稍后重试")); return false;
    }
    (void)reconcile_covers(NULL); s_need_generation = true; s_notice[0] = 0; return true;
}
void home_library_select(unsigned index) { if (index < home_library_view().count) *selected() = index; }
void home_library_shuffle(void) {
    if (!app_home_library_recent()) { s_shuffle = true; queue_work(LIBRARY_FULL); }
}
bool home_library_needs_generation(void) { return s_active && s_need_generation; }
void home_library_cover_generation(unsigned generation) {
    s_generation = generation; s_need_generation = false; memset(s_cover_failed, 0, sizeof(s_cover_failed));
}
void home_library_cover_mode_changed(void) { free_covers(); }
static bool ignore_favorite(nvs_handle_t handle, const char *path) { (void)handle; (void)path; return false; }
static uint32_t random_value(void *ctx) { (void)ctx; return esp_random(); }
static bool scan_cancelled(void *ctx) { return atomic_load(&((library_job_t *)ctx)->cancelled); }

static void scan(library_job_t *job) {
    book_shelf_entry_t *entries = NULL; int count = 0; size_t capacity = 0;
    book_shelf_scan_t request = {.catalog={&entries,&count,&capacity}, .query="", .cancel=scan_cancelled, .cancel_ctx=job,
        .favorite_read=ignore_favorite, .hidden_read=book_shelf_hidden_read};
    nvs_handle_t hidden = 0, progress = 0;
    esp_err_t error = nvs_open("rp_shelf", NVS_READONLY, &hidden);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        job->notice = "书架状态读取失败，请稍后重试"; return;
    }
    error = nvs_open("rp_books", NVS_READONLY, &progress);
    request.progress = progress;
    request.progress_checked = error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND;
    bool truncated = false, unreadable = false, skipped = false;
    if (job->work == LIBRARY_FULL) {
        if (job->mounted && !scan_cancelled(job)) {
            struct stat legacy;
            if (!strcmp(job->root, "/sdcard/books") && !stat("/sdcard/book", &legacy) && S_ISDIR(legacy.st_mode))
                book_shelf_scan_dir(&request, "/sdcard/book", false, 0, hidden, &truncated, &unreadable, &skipped);
            if (!scan_cancelled(job))
                book_shelf_scan_dir(&request, job->root, false, 0, hidden, &truncated, &unreadable, &skipped);
        }
        if (job->flash && !scan_cancelled(job))
            book_shelf_scan_dir(&request, "/flash/books", true, 0, hidden, &truncated, &unreadable, &skipped);
    }
    if (!scan_cancelled(job) && !truncated && !unreadable) {
        (void)book_shelf_scan_backfill(&request, &truncated);
        for (int i = 0; i < count; ++i)
            if ((!job->mounted && !strncmp(entries[i].path, "/sdcard/", 8)) ||
                (!job->flash && !strncmp(entries[i].path, "/flash/", 7))) entries[i].removed = true;
    }
    if (!scan_cancelled(job)) {
        if (truncated) job->notice = "书目较多，暂不能完整读取";
        else if (unreadable) job->notice = "书库暂不可用，请稍后重试";
        else {
            if (job->work == LIBRARY_FULL) {
                book_mini_library_update(&job->model, entries, (size_t)count, job->shuffle, random_value, NULL);
            } else {
                book_mini_library_update_recent(&job->model, entries, (size_t)count);
                for (unsigned i = 0; i < job->model.random_count && !scan_cancelled(job); ++i) {
                    book_mini_item_t *item = &job->model.random[i];
                    book_progress_t p = {0};
                    item->has_progress = request.progress_checked ? book_progress_load_handle((uintptr_t)progress, item->path, item->size, &p) :
                        book_progress_load(item->path, item->size, &p);
                    item->pct = item->has_progress ? p.pct : 0;
                    item->recent = item->has_progress ? p.last_open_s : 0;
                }
            }
            job->updated = true;
            job->complete = !skipped;
            if (skipped) job->notice = "部分书目无法读取";
        }
    }
    if (hidden) nvs_close(hidden);
    if (progress) nvs_close(progress);
    free(entries);
}

static void scan_worker(void *ctx) {
    library_job_t *job = ctx;
    if (job->work >= LIBRARY_PROGRESS && !scan_cancelled(job)) scan(job);
    if (job->work == LIBRARY_CHECK || job->updated) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            book_mini_item_t *items = mode ? job->model.recent : job->model.random;
            unsigned count = mode ? job->model.recent_count : job->model.random_count;
            for (unsigned i = 0; i < count && !scan_cancelled(job); ++i) {
                book_mini_item_t *item = &items[i];
                struct stat st;
                library_file_t *file = &job->files[mode][i];
                file->valid = !stat(item->path, &st) && S_ISREG(st.st_mode) && st.st_size == item->size;
                if (!file->valid) continue;
                file->modified = st.st_mtime;
                // 只查入选书目的逐书缓存，不读取书架正在维护的整表文件。
                // Read per-book metadata only for selected entries, not the shelf's mutable catalog file.
                if (!scan_cancelled(job) && job->updated && (mode || job->work == LIBRARY_FULL) &&
                    book_metadata_supported(item->path) && pico_boot_asset_allowed(item->path)) {
                    char title[sizeof(item->title)], author[sizeof(item->author)];
                    if (book_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
                        snprintf(item->author, sizeof(item->author), "%s", author);
                }
            }
        }
    }
    // 最后一项操作发布完成状态；UI 可立即接管任务对象，worker 不再访问它。
    // Publish completion last; the UI can take the job immediately and the worker no longer touches it.
    atomic_store(&s_running, false);
    vTaskDelete(NULL);
}

static unsigned keep_focus(const book_mini_item_t *old, unsigned old_count, unsigned selected,
                           const book_mini_item_t *items, unsigned count) {
    if (selected < old_count)
        for (unsigned i = 0; i < count; ++i) if (!strcmp(old[selected].path, items[i].path)) return i;
    return 0;
}

static bool collect_scan(const board_sd_info_t *sd) {
    library_job_t *job = s_job;
    s_job = NULL;
    bool repaint = false;
    if (!scan_cancelled(job)) {
        if (job->store_revision != book_store_revision() || job->progress_revision != book_progress_revision() ||
            job->present != sd->present || job->mounted != sd->mounted || strcmp(job->identity, sd->identity) ||
            job->flash != book_store_flash_ready() || strcmp(job->root, app_settings_books_dir())) {
            s_work = s_shuffle ? LIBRARY_FULL : refresh_work(sd);
        } else {
            s_work = LIBRARY_NONE;
            s_shuffle = false;
            if (job->updated) {
                job->model.random_selected = job->shuffle ? 0 : keep_focus(s_model.random, s_model.random_count,
                    s_model.random_selected, job->model.random, job->model.random_count);
                job->model.recent_selected = keep_focus(s_model.recent, s_model.recent_count,
                    s_model.recent_selected, job->model.recent, job->model.recent_count);
                s_model = job->model;
                s_valid = job->complete;
                s_store_revision = job->store_revision; s_progress_revision = job->progress_revision;
                s_sd_present = job->present; s_sd_mounted = job->mounted; s_flash = job->flash;
                strcpy(s_root, job->root); strcpy(s_identity, job->identity);
                s_need_generation = true;
            }
            repaint = job->work >= LIBRARY_PROGRESS;
            if (job->work == LIBRARY_CHECK || job->updated)
                repaint |= reconcile_covers(job->files[app_home_library_recent()]);
            if (repaint) snprintf(s_notice, sizeof(s_notice), "%s", job->notice ? job->notice : "");
        }
    }
    free(job);
    return repaint;
}

static bool poll_scan(void) {
    if (atomic_load(&s_running)) return false;
    board_sd_info_t sd = {0};
    const esp_err_t sd_error = board_sd_get_info(&sd);
    if (sd_error == ESP_ERR_NOT_FINISHED) return false;
    if (s_job) return collect_scan(&sd);
    library_work_t needed = refresh_work(&sd);
    if (needed > s_work) s_work = needed;
    library_job_t *job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (job) {
        job->model = s_model;
        job->work = s_work; job->shuffle = s_shuffle;
        job->present = sd.present; job->mounted = sd.mounted; job->flash = book_store_flash_ready();
        job->store_revision = book_store_revision(); job->progress_revision = book_progress_revision();
        snprintf(job->root, sizeof(job->root), "%s", app_settings_books_dir());
        snprintf(job->identity, sizeof(job->identity), "%s", sd.identity);
        atomic_init(&job->cancelled, false);
        s_job = job;
        atomic_store(&s_running, true);
        if (xTaskCreate(scan_worker, "home_books", 8192, job, tskIDLE_PRIORITY, NULL) == pdPASS) return false;
        atomic_store(&s_running, false);
        free(job); s_job = NULL;
    }
    const bool repaint = s_work >= LIBRARY_PROGRESS;
    s_work = LIBRARY_NONE; s_shuffle = false;
    if (repaint) snprintf(s_notice, sizeof(s_notice), "%s",ui_tr("书目较多，暂不能完整读取"));
    return repaint;
}

bool home_library_poll(void) {
    if (!s_active) return false;
    if (s_work != LIBRARY_NONE || s_job) {
        bool repaint = poll_scan();
        if (repaint || s_work >= LIBRARY_PROGRESS || s_job) return repaint;
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
