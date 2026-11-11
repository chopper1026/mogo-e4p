/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：USB、文件页与睡眠共用选图、封面和头像交还顺序，保留各任务预算与外围门控。
 * English: Picker/cover/avatar handoff shared by USB, files and sleep, retaining per-job budgets and caller guards.
 */
#include "storage_jobs.h"
#include "book_cover_jobs.h"
#include "ui_avatar.h"
#include "lock_style_images.h"

bool storage_jobs_quiesce(unsigned timeout_ms) {
    return lock_images_quiesce(timeout_ms) && book_cover_jobs_quiesce(timeout_ms) && ui_avatar_quiesce(timeout_ms);
}
