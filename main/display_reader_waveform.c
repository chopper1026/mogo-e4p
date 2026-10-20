/* SPDX-License-Identifier: Apache-2.0
 * 中文：普通文字翻页的柔和灰边，保留抗锯齿目标、驱动计数和周期清残影。
 * English: Soften gray-edge transients on ordinary text turns, preserving antialiased targets,
 * pulse counts and periodic cleanup.
 * 冻结：仅白底到中灰提前最多两相回浅；黑/白端点、其他迁移、原表及帧时序不改。
 * 相同驱动计数不保证光学灰度相同；此调序需实屏对比深浅与残影。
 * Frozen: Advance at most two settling phases only for white-to-midgray pairs. Keep endpoints,
 * all other pairs, original tables and frame timing. Equal pulse counts do not guarantee equal
 * optical grays; validate this ordering on the panel against tone and ghosting.
 * 用户 2026-10-19 反馈阅读页数秒后回灰：纯白目标改用水波纹已验证的完整擦白列，
 * 其余灰阶、文字软化与帧时序不变。
 * User report 2026-10-19: reader pages drift gray within seconds; pure-white targets now
 * reuse the verified main-page water whitening column, leaving other grays and timing intact.
 */
#include "display_reader_waveform.h"
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
#include <stdbool.h>
#include <string.h>

static uint8_t s_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_data, NULL};
static const EpdWaveformPhases* s_ranges[] = {&s_phases};
static const EpdWaveformMode s_mode = {5, 1, s_ranges};
static const EpdWaveformMode* s_modes[] = {&s_mode};
static EpdWaveform s_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_modes};
static bool s_ready;

static uint8_t s_page_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_page_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_page_data, NULL};
static const EpdWaveformPhases* s_page_ranges[] = {&s_page_phases};
static const EpdWaveformMode s_page_mode = {5, 1, s_page_ranges};
static const EpdWaveformMode* s_page_modes[] = {&s_page_mode};
static EpdWaveform s_page_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_page_modes};
static bool s_page_ready;

// E4P 原厂 GL16 的白饱和不足：大片旧文字转白后会在数秒内回灰。主页水波纹已验证
// 把全部剩余相用作同向擦白可以消除；这里只替换 to=15 一列，其余迁移和帧时序不动。
// The E4P vendor GL16 under-saturates white: large text-to-white areas drift gray
// within seconds. The main-page water table already uses every remaining phase as a
// white push; copy only its to=15 column, leaving all other pairs and timing alone.
static void copy_white_column(uint8_t (*dst)[16][4]) {
    const EpdWaveformPhases* water = e0470_waveform_phases(display_main_water_waveform(), MODE_GL16);
    if (!water || !water->luts || water->phases != E0470_GL16_FRAMES || water->phase_times) return;
    for (int f = 0; f < E0470_GL16_FRAMES; ++f)
        memcpy(dst[f][15], water->luts + ((size_t)f * 16 + 15) * 4, 4);
}

static void set_action(int phase, int to, int action) {
    // from=15 占该字节最低两位；其余三对不变。/ from=15 occupies only the lowest two bits.
    s_data[phase][to][3] = (uint8_t)((s_data[phase][to][3] & ~3u) | (unsigned)action);
}

const EpdWaveform* display_reader_text_waveform(void) {
    if (s_ready) return &s_waveform;
    const EpdWaveformPhases* original = e0470_waveform_phases(&E0470_TEXTTURN_WAVEFORM, MODE_GL16);
    if (!original || !original->luts || original->phases != E0470_GL16_FRAMES || original->phase_times)
        return &E0470_TEXTTURN_WAVEFORM;
    memcpy(s_data, original->luts, sizeof(s_data));
    for (int to = 1; to < 15; ++to) {
        int first_white = original->phases, last_black = -1;
        for (int f = 0; f < original->phases; ++f) {
            int action = e0470_phase_action(original, f, to, 15);
            if (action == 2 && first_white == original->phases) first_white = f;
            if (action == 1) last_black = f;
        }
        int prefix_black = 0, tail_white = 0;
        for (int f = 0; f < first_white; ++f)
            prefix_black += e0470_phase_action(original, f, to, 15) == 1;
        for (int f = last_black + 1; f < original->phases; ++f)
            tail_white += e0470_phase_action(original, f, to, 15) == 2;
        int moves = tail_white >= 4 && prefix_black >= 6 ? 2 :
                    tail_white >= 2 && prefix_black >= 4 ? 1 : 0;
        if (!moves) continue;
        // 交换动作而非删相；保留起点、末尾回浅及所有保持相，不增加第二次推屏。
        // Swap pulses instead of dropping phases; retain the start, final settling and holds.
        int rank = 0, moved = 0, white = last_black + 1;
        int target_rank = prefix_black - (moves == 2 ? 5 : 3);
        for (int f = 0; f < first_white && moved < moves; ++f) {
            if (e0470_phase_action(original, f, to, 15) != 1) continue;
            if (rank++ != target_rank) continue;
            while (white < original->phases && e0470_phase_action(original, white, to, 15) != 2) ++white;
            if (white == original->phases) break;
            set_action(f, to, 2); set_action(white++, to, 1);
            ++moved; target_rank += 2;
        }
    }
    copy_white_column(s_data);
    s_waveform.temp_intervals = E0470_TEXTTURN_WAVEFORM.temp_intervals;
    s_ready = true;
    return &s_waveform;
}

const EpdWaveform* display_reader_page_waveform(void) {
    if (s_page_ready) return &s_page_waveform;
    const EpdWaveformPhases* base = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
    if (!base || !base->luts || base->phases != E0470_GL16_FRAMES || base->phase_times)
        return &E0470_WAVEFORM;
    memcpy(s_page_data, base->luts, sizeof(s_page_data));
    copy_white_column(s_page_data);
    s_page_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_page_ready = true;
    return &s_page_waveform;
}
