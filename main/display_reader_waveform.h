/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "epdiy.h"

/// 普通纯文字翻页：白底到中灰的少量回浅脉冲提前，to=0 补回原厂 18 次推黑。
/// Plain text turns: advance a few gray-edge settling pulses and restore the vendor's
/// 18 black pushes for to=0.
const EpdWaveform* display_reader_text_waveform(void);

/// 阅读页整页输出：原厂 GL16 序列，纯白目标沿用主页水波纹的完整擦白列，
/// to=0 同样补回 18 次推黑。
/// Full reader pages: vendor GL16 sequences with the main-page water whitening for
/// pure-white targets and the same 18 black pushes for to=0.
const EpdWaveform* display_reader_page_waveform(void);

/// 水波纹翻页：原厂 GL16 表加 to=0 的 18 次推黑，保留每种迁移的前导擦除与白列；
/// 37 相不变，错相 8 带的拍数与节拍也不变。设置阅读背景时自动换用背景派生表。
/// Water turns: the vendor GL16 table plus 18 black pushes for to=0, keeping each
/// transition's leading erase and the white column; the 37 phases and the staggered
/// eight-band tick count are unchanged. Switches to the background derivative while a
/// reading background is set.
const EpdWaveform* display_reader_water_waveform(void);

/// 背景页派生表：在水波纹表上再裁擦除与白/黑饱和，to=0 增黑保留；相数更少，
/// 深色图形上的黑闪更短。page 与 water accessor 在背景开启时都会返回这张表。
/// Background derivative: the water table trimmed once more on erase and both saturation
/// runs while keeping the to=0 boost; fewer phases and shorter black flashing over dark
/// graphics. Both page and water accessors return it while a background is set.
const EpdWaveform* display_reader_bg_waveform(void);
