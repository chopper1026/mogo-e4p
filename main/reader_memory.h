/*
 * 中文：诊断构建的只读资源快照；调用方先交还预绘，不分配或回收资源。
 * English: Read-only diagnostic resource snapshots after prefetch handoff; never allocate or reclaim resources.
 */
#pragma once
#include "reader_perf.h"
#if CONFIG_READER_PERF
#include <stddef.h>
#include "esp_heap_caps.h"

typedef struct {
    size_t font_source; ///< 自有字体镜像，不含借用镜像 / Owned font images, excluding borrows
    size_t font_tables; ///< 自有映射与轮廓表 / Owned maps and outline tables
    size_t font_blocks; ///< 允许常驻的块缓存与连续读取暂存 / Retained block caches and read scratch
    size_t font_glyphs; ///< 字形条目与位图的分配容量 / Allocated glyph entries and bitmaps
    size_t font_work; ///< 允许常驻的变体工作区 / Retained variation workspace
    size_t font_catalog; ///< 允许常驻的字体目录 / Retained font catalog
    size_t layout; ///< 页表、行缓冲与图片位置表，不含借用章节 / Page, line and image-position buffers, excluding chapter borrows
    size_t chapter; ///< 当前会话接管的章节分配 / Chapter allocations owned by the current session
    size_t prefetch; ///< 预绘帧，不含任务与同步对象 / Prefetch frame, excluding task and synchronization objects
    size_t background; ///< 已解码的唯一背景 / Sole decoded background
    size_t page_images; ///< 当前页图片表与灰阶面 / Current-page image array and grayscale planes
    size_t shelf_ui; ///< 页面接管的书架数据与封面，不含工作队列 / Page-owned shelf data and covers, excluding work queues
    unsigned font_slots; ///< 已就绪字体槽 / Ready font slots
    unsigned prefetch_worker; ///< 预绘任务是否存在 / Whether the prefetch task exists
    unsigned prefetch_stack_free; ///< 预绘任务历史最小剩余栈字节 / Prefetch task historical minimum free stack bytes
} reader_memory_stats_t;

/// 仅查询未释放的自有分配基址；静态数据、内部指针与借用指针不得传入。
/// Query live owned allocation bases only; never pass static data, interior pointers or borrows.
static inline size_t reader_memory_owned(const void *pointer) {
    return pointer ? heap_caps_get_allocated_size((void *)pointer) : 0;
}
/// UI 已排除字体和布局的并发借用；各模块仅覆盖自己负责的字段。
/// UI has excluded concurrent font/layout borrowers; each module replaces only its own fields.
void ttf_font_memory_stats(reader_memory_stats_t *stats);
/// 仅记录布局拥有的四项动态缓冲。/ Record only the four dynamic buffers owned by layout.
void book_layout_memory_stats(reader_memory_stats_t *stats);
/// 后台已交还后记录帧与任务栈；不取得新的锁。/ Record frame and task stack after worker handoff without acquiring locks.
void reader_prefetch_memory_stats(reader_memory_stats_t *stats);
/// 仅记录背景成品缓存，不包含已经交还的解码暂存。/ Record only the final background cache, excluding returned decode scratch.
void ui_reader_background_memory_stats(reader_memory_stats_t *stats);
/// 计时结束后输出独立记录；统计是已列出资源的容量视图，不是全堆分解。
/// Emit a separate record after timing stops; listed allocation capacities do not decompose the whole heap.
void reader_perf_memory(const char *event, const reader_memory_stats_t *stats);
/// 活跃计时期间跳过资源遍历与输出，避免污染该操作的耗时。/ Skip resource walks and output while timing is active.
bool reader_perf_memory_ready(void);
#endif
