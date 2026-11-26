/* 中文：滑动列表借用显示缓冲绘制可见窗口，按上屏实线定点补白。
 * English: Paint a scrolling viewport in borrowed display storage and reinforce white at committed rules.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "display_refresh.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t left;  ///< 被占用的最左像素。/ First occupied pixel.
    uint16_t end;   ///< 右侧开区间端点；零表示空。/ Exclusive right edge; zero denotes an empty span.
} display_rule_span_t;

/// 仅发布绘制窗口内的目标像素；同步借用差分缓冲，不扫描、不分配。/ Paint only the target viewport using difference scratch; no scan or allocation.
bool display_list_paint(EpdiyHighlevelState *hl, EpdRect viewport, void (*paint)(uint8_t *));
/// 将真实旧帧中贯穿 [left,end) 的黑线并入每行跨度（调用方提供 viewport.height 项）。/ Merge committed black runs spanning [left,end) into one span per viewport row.
bool display_list_capture_rules(const EpdiyHighlevelState *hl, EpdRect viewport, int left, int end,
                                display_rule_span_t *rows);
/// 仅对跨度内前后帧均白的像素做一次物理白推，不改目标或基准。/ Pulse only stable whites in the spans once, without changing either framebuffer.
enum EpdDrawError display_list_clean_rules(EpdiyHighlevelState *hl, EpdRect viewport, const display_rule_span_t *rows);

#ifdef __cplusplus
}
#endif
