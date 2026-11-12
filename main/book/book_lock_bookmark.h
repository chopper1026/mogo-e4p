/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：当前页面上的透明PNG/JPG书封；输入framebuffer即底图，不加载书籍。
 * English: Transparent PNG/JPG cover over the current framebuffer; never load a book for its background.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
bool book_lock_bookmark_draw(uint8_t *fb,const char *png);
/// 设置页预览：可命中缓存但不回写 / Settings preview: may hit the cache but never rewrites it
bool book_lock_bookmark_preview(uint8_t *fb,const char *png);
/// 选图确认后预建图层缓存，第一次锁屏不再解码 / Prebuild the layer cache after a pick so the first lock never decodes
void book_lock_bookmark_prepare(const char *png);
/// 最近一次绘制是否命中TF卡图层缓存 / Whether the last draw hit the SD-card layer cache
bool book_lock_bookmark_cached(void);
void book_lock_bookmark_preview_paint(uint8_t *fb,const uint8_t *canvas,EpdRect area);

/// 选图用的小画布，保持完整PNG/JPG比例、alpha与去白规则；仅文件/像素，不调用字库。
/// Picker thumbnail preserving canvas ratio, alpha and white removal; file/pixel work only.
#include "image_decode.h"
bool book_lock_bookmark_thumbnail(const char *path,unsigned width,unsigned height,uint8_t *gray,
                                  image_cancel_fn cancelled,void *context);
