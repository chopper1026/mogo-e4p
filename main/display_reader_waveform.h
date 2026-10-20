/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "epdiy.h"

/// 普通纯文字翻页：白底到中灰的少量回浅脉冲提前，其余迁移保留原表。
/// Plain text turns: advance a few gray-edge settling pulses; retain all other transitions.
const EpdWaveform* display_reader_text_waveform(void);

/// 阅读页整页输出：原厂 GL16 序列，纯白目标沿用主页水波纹的完整擦白列。
/// Full reader pages: vendor GL16 sequences with the main-page water whitening for
/// pure-white targets only.
const EpdWaveform* display_reader_page_waveform(void);

/// 阅读水波纹：原厂 GL16 序列，纯白目标只保留 DU 的擦白次数（黑→白 18 次）。
/// Reader water ripple: vendor GL16 sequences with only the DU erase count for
/// pure-white targets (18 pulses for black→white).
const EpdWaveform* display_reader_water_waveform(void);
