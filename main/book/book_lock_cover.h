#pragma once
#include <stdbool.h>
#include <stdint.h>

/// 从最后阅读记录准备独立的封面画面；缺少可读书源时由调用方回退。
/// Prepare an independent face from the last-read book; the caller handles missing sources.
bool book_lock_cover_draw(uint8_t *fb, bool crop);

enum { BOOK_LOCK_PREVIEW_W = 160, BOOK_LOCK_PREVIEW_H = 284,
       BOOK_LOCK_PREVIEW_BYTES = BOOK_LOCK_PREVIEW_W * BOOK_LOCK_PREVIEW_H / 2 };

/// 与正式画面同源的逻辑竖屏缩图，低半字节在前；调用方提供 BOOK_LOCK_PREVIEW_BYTES 字节。
/// Same-source portrait preview, packed low nibble first into a caller-owned BOOK_LOCK_PREVIEW_BYTES buffer.
bool book_lock_cover_preview(uint8_t *pixels, bool crop);
