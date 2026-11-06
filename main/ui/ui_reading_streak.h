/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：首页打卡题头纯绘制接口。/ English: Pure drawing interface for the Home streak header.
 */
#pragma once
#include "epdiy.h"
#include "reading_journal_model.h"

/// 仅清理与呈现题头，避开时钟、头像和组件区。/ Header-only clearing/presentation, excluding clock, avatar and widget regions.
EpdRect ui_reading_streak_area(void);
/// 借用调用方快照，不读文件、分配或保存。/ Borrow a caller snapshot without IO, allocation or persistence.
void ui_reading_streak_draw(uint8_t *fb, const reading_streak_view_t *view);
