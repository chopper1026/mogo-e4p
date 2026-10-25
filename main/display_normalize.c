/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：由阅读整页表派生一张归一表，只把 to=0 列改成固定的「擦白 18 相 + 推黑 18 相」。
 * 全像素推屏一次后，页面上每个字芯的最后 36 相完全相同，墨度因此一致。
 *
 * English: Derive a normalization table from the reader page table, rewriting only the to=0
 * column to a fixed 18 erase phases followed by 18 black phases. After one full-pixel pass,
 * every glyph core on the page shares the same last 36 phases and therefore the same ink.
 *
 * 冻结：不改白列与灰阶行；相数、帧时序与其他迁移保持阅读整页表。
 * Frozen: leave the white column and gray rows alone; phase count, frame timing and every
 * other transition stay as the reader page table.
 */
#include "display_normalize.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include <stdbool.h>
#include <string.h>

// 与 to=0 补强同一组实屏验证过的拍数：白列擦白、原厂 18 次连续推黑。
// The same panel-validated runs as the to=0 boost: the white column's erase and 18 black pushes.
#define NORMALIZE_ERASE 18
#define NORMALIZE_BLACK 18

static uint8_t s_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_data, NULL};
static const EpdWaveformPhases* s_ranges[] = {&s_phases};
static const EpdWaveformMode s_mode = {5, 1, s_ranges};
static const EpdWaveformMode* s_modes[] = {&s_mode};
static EpdWaveform s_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_modes};
static bool s_ready;

// from 占该字节两位：0 号位是最高两位。/ from occupies two bits, from=0 is the top pair.
static void set_action(int phase, int to, int from, int action) {
    uint8_t *cell = &s_data[phase][to][from / 4];
    const int shift = 6 - 2 * (from % 4);
    *cell = (uint8_t)((*cell & ~(3u << shift)) | ((unsigned)action << shift));
}

const EpdWaveform* display_normalize_waveform(void) {
    if (s_ready) return &s_waveform;
    const EpdWaveform* page = display_reader_page_waveform();
    const EpdWaveformPhases* base = e0470_waveform_phases(page, MODE_GL16);
    if (!base || !base->luts || base->phases != E0470_GL16_FRAMES || base->phase_times ||
        NORMALIZE_ERASE + NORMALIZE_BLACK > E0470_GL16_FRAMES)
        return page;
    memcpy(s_data, base->luts, sizeof(s_data));
    for (int from = 0; from < 16; ++from) {
        for (int f = 0; f < E0470_GL16_FRAMES; ++f) set_action(f, 0, from, 0);
        for (int f = 0; f < NORMALIZE_ERASE; ++f) set_action(f, 0, from, 2);
        for (int f = NORMALIZE_ERASE; f < NORMALIZE_ERASE + NORMALIZE_BLACK; ++f)
            set_action(f, 0, from, 1);
    }
    s_waveform.temp_intervals = page->temp_intervals;
    s_ready = true;
    return &s_waveform;
}
