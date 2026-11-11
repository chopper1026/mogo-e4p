/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LOCK_IMAGES_MAX 64
#define LOCK_IMAGE_REQUESTS 8
#define LOCK_THUMB_WIDTH 160
#define LOCK_THUMB_HEIGHT 284
#define LOCK_THUMB_BYTES (LOCK_THUMB_WIDTH * LOCK_THUMB_HEIGHT / 2)
typedef struct { char path[288], name[96]; uint64_t key; } lock_image_item_t;
typedef enum { LOCK_IMAGE_PENDING, LOCK_IMAGE_READY, LOCK_IMAGE_FAILED } lock_image_state_t;
typedef struct { const char *path; bool bookmark, large; } lock_image_request_t;
/// UI线程调用；扫描、读缓存、解码和落卡均由单个低优先级任务完成。
/// UI-thread API; one low-priority worker owns scanning, cache I/O and decoding.
void lock_images_open(void);
/// 离页取消任务，保留已完成的小图；大图立即释放。/ Cancel work but retain ready thumbnails; release large previews.
void lock_images_pause(void);
/// 介质失效或USB交接时释放缓存并推进身份代次。/ Purge on media loss/USB handoff and advance cache identity.
void lock_images_close(void);
unsigned lock_images_epoch(void);
/// 取走目录结果的所有权；调用方free。NULL且finished=false表示仍在扫描。
/// Transfer ownership of the catalogue (caller frees); unfinished NULL means scanning.
lock_image_item_t *lock_images_catalogue(int *count, bool *finished);
/// 按顺序优先处理；当前页在前、预读在后。更新请求取消不再需要的解码。
/// Ordered priorities: visible images first, prefetch last; retargeting cancels stale decoding.
void lock_images_request(const lock_image_request_t *requests, int count);
/// 返回稳定只读4bpp像素；UI下次request/catalogue/pause/close可能释放，绘图不进行IO。
/// Read-only pixels survive until the UI next requests/takes a catalogue/pauses/closes; painting performs no I/O.
const uint8_t *lock_images_pixels(const char *path, bool bookmark, bool large, lock_image_state_t *state);
unsigned lock_images_revision(void);
/// 取消并等待文件句柄实际交还；不能强杀仍持有TF句柄的任务。
/// Cancel and wait for actual file-handle return; never delete a live file worker.
bool lock_images_quiesce(unsigned timeout_ms);
