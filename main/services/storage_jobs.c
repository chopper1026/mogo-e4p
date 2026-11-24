/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：USB、文件页与睡眠共用关机图片、选图、封面、头像和书库交还顺序，保留各任务预算与外围门控。
 * English: Shutdown-art/picker/cover/avatar/library handoff shared by USB, files and sleep, retaining per-job budgets and caller guards.
 */
#include "storage_jobs.h"
#include "font_download.h"
#include "cloudzao_service.h"
#include "book_cover_jobs.h"
#include "home_library_jobs.h"
#include "ui_avatar.h"
#include "lock_style_images.h"
#include "shutdown_wallpaper.h"

bool storage_jobs_quiesce(unsigned timeout_ms) {
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    if (!cloudzao_quiesce(timeout_ms)) return false;
#endif
    return font_download_quiesce(timeout_ms) && shutdown_wallpaper_quiesce(timeout_ms) && lock_images_quiesce(timeout_ms) &&
        book_cover_jobs_quiesce(timeout_ms) && ui_avatar_quiesce(timeout_ms) &&
        home_library_quiesce(timeout_ms);
}
