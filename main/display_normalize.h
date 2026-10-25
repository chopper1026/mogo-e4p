/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：遮罩与面板收起后的归一表。被白底或半透明遮罩盖过的字已经多走过一轮
 * 「擦白 → 推黑」，比没被盖过的字更饱和；单次还原无法追平，只能让整页字芯
 * 再统一走一遍同样的序列。
 *
 * English: Normalization table for closing a panel or the quick layer. Text the white sheet
 * or the shade covered has already run one extra erase-then-black cycle and is more
 * saturated than the rest; a single restore cannot match it, so every glyph core on the page
 * runs the same cycle once more.
 *
 * 冻结：只改写 to=0 列；白列、灰阶行、相数与帧时序沿用阅读整页表（其灰阶行与原厂表
 * 逐相相同，所以首页、书架等页面的灰卡不受影响）。
 * Frozen: rewrite only the to=0 column; keep the reader page table's white column, gray
 * rows, phase count and frame timing (its gray rows match the vendor table phase for phase,
 * so gray cards on the home, shelf and settings pages are unaffected).
 */
#pragma once
#include "epdiy.h"

/// 归一表：任意起点的字芯都是「擦白 18 相 + 推黑 18 相」，其余目标同阅读整页表。
/// / Normalize table: every glyph core runs 18 erase phases then 18 black phases from any
/// start; all other targets match the reader page table.
const EpdWaveform* display_normalize_waveform(void);
