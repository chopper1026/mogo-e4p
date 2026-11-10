/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：首页迷你书库的书目快照、刷新生命周期与有界封面所有权。
 * English: Home mini-library snapshots, refresh lifecycle and bounded cover ownership. */
#pragma once
#include "book_mini_library.h"

typedef struct {
    const uint8_t *pixels;
    unsigned width, height;
    bool fast;
} home_library_cover_t;
typedef struct {
    const book_mini_item_t *books;
    const home_library_cover_t *covers;
    unsigned count, selected;
    bool recent, loading;
    const char *notice;
} home_library_view_t;

void home_library_enter(void);
/// 离页停任务，保留最多五张有效封面；返回时检查文件并按书籍身份复用。/ Stop work on exit, keeping up to five covers for identity-checked reuse on return.
void home_library_leave(void);
void home_library_media_lost(void);
void home_library_invalidate(void);
/// 只在UI事件任务调用；扫描借用取消回调，绘制不调用。/ UI event task only; scans borrow a cancellation hook, never called by painting.
bool home_library_poll(bool (*cancel)(void *ctx), void *ctx);
home_library_view_t home_library_view(void);
bool home_library_recent(bool recent);
void home_library_shuffle(void);
void home_library_select(unsigned selected);
/// 页面统一拥有队列代次；变更书目/格式后请求新代次，已取回像素继续归本服务。/ The page owns the queue generation; request a new one on catalog/format changes, retaining taken pixels here.
bool home_library_needs_generation(void);
void home_library_cover_generation(unsigned generation);
void home_library_cover_mode_changed(void);
