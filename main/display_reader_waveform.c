/* SPDX-License-Identifier: Apache-2.0
 * 中文：阅读页翻页的柔和灰边与黑白端点强度，保留抗锯齿目标、相数和周期清残影。
 * English: Soften gray edges and set black/white endpoint strength on reader turns,
 * preserving antialiased targets, phase count and periodic cleanup.
 * 冻结：白底到中灰最多提前两相回浅；to=0 只把裁表砍掉的 5 次推黑补回原厂 18 次，
 * 占用本行前导保持相，相数、帧时序与其他迁移不改。
 * 黑弱白强是实屏问题：旧字擦白 18~24 次而新字推黑曾被裁到 13 次。
 * Frozen: advance at most two settling phases for white-to-midgray; for to=0 restore only
 * the five trim-cut pushes to the vendor's 18 by consuming that row's leading holds, keeping
 * phase count, frame timing and every other transition unchanged. The imbalance is a panel
 * issue: 18-24 erases against 13 black pushes for the new glyphs.
 * 用户 2026-10-19 反馈阅读页数秒后回灰：纯白目标改用水波纹已验证的完整擦白列，
 * 其余灰阶、文字软化与帧时序不变。
 * User report 2026-10-19: reader pages drift gray within seconds; pure-white targets now
 * reuse the verified main-page water whitening column, leaving other grays and timing intact.
 * 用户 2026-10-21 修订：水波纹改回原厂 GL16 表，保留每种灰阶转换的前导擦除；
 * 专用白列表删除，本文件只保留文字软化与整页白列两张阅读表。
 * User revision 2026-10-21: water turns return to the vendor GL16 table with each
 * transition's leading erase; the dedicated white column is gone, leaving only the
 * text-softening and full-page white-column reader tables.
 * 用户 2026-10-22 反馈翻页后正文仍不够黑：裁表只砍黑饱和段，白段一相未砍。三张阅读表
 * 统一把 to=0 补回原厂 18 次连续推黑，改用本行前导保持相，表长与翻页时间不变；
 * 水波纹另用一条同源表，保留原厂前导擦除和白列。
 * User report 2026-10-22: text stays too light after a turn because the trim cut only the
 * black saturation run. All three reader tables now restore the vendor's 18 consecutive
 * black pushes for to=0 by reusing that row's leading holds, leaving table length and turn
 * time unchanged; water keeps its own table with the vendor leading erase and white column.
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

static uint8_t s_water_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_water_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_water_data, NULL};
static const EpdWaveformPhases* s_water_ranges[] = {&s_water_phases};
static const EpdWaveformMode s_water_mode = {5, 1, s_water_ranges};
static const EpdWaveformMode* s_water_modes[] = {&s_water_mode};
static EpdWaveform s_water_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_water_modes};
static bool s_water_ready;

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

// 裁表的 E0470_TRIM_SAT_CUT 只砍 to≠15 的黑饱和段：正文换字（15→0）只剩 13 次推黑，
// 旧字擦白却有 18~24 次。这里把 to=0 补回原厂 18 次连续推黑，占用本行前导保持相，
// 相数、末尾保持与帧时序不变；灰阶边和其他目标不动。
// The trim's E0470_TRIM_SAT_CUT only shortens non-white saturation, leaving 13 black
// pushes for new glyphs against 18-24 erases. Restore the vendor's 18 consecutive pushes
// for to=0 by consuming that row's leading holds; phase count, trailing holds and timing
// stay unchanged, and every other target is untouched.
#define READER_BLACK_PUSHES 18
#define READER_BLACK_TAIL_HOLDS 3

static int cell_action(const uint8_t (*data)[16][4], int phase, int to, int from) {
    return (data[phase][to][from / 4] >> (6 - 2 * (from % 4))) & 3;
}

static void cell_set(uint8_t (*data)[16][4], int phase, int to, int from, int action) {
    const int shift = 6 - 2 * (from % 4);
    data[phase][to][from / 4] =
        (uint8_t)((data[phase][to][from / 4] & ~(3u << shift)) | ((unsigned)action << shift));
}

static void boost_black_column(uint8_t (*data)[16][4], int phases) {
    if (phases <= READER_BLACK_TAIL_HOLDS) return;
    const int limit = phases - 1 - READER_BLACK_TAIL_HOLDS;
    // from=0 是保持黑底，不进入差分，也不额外驱动。
    // from=0 is an unchanged black pixel outside the differential; leave it alone.
    for (int from = 1; from < 16; ++from) {
        int first = -1, black = 0, erase = 0;
        bool ordered = true;
        for (int f = 0; f < phases; ++f) {
            const int action = cell_action(data, f, 0, from);
            if (!action) continue;
            if (first < 0) first = f;
            if (action == 1) ++black;
            else if (action == 2) {
                if (black) ordered = false;
                else ++erase;
            } else ordered = false;
        }
        // 只认「先擦除后推黑」的行，其他形状一律不动。
        // Only [erase then black] rows are rewritten; other shapes stay as they are.
        if (!ordered || !black || black >= READER_BLACK_PUSHES) continue;
        const int span = erase + READER_BLACK_PUSHES;
        int start = first;
        if (start + span - 1 > limit) start = limit - span + 1;
        if (start < 0) continue;
        for (int f = 0; f < phases; ++f) cell_set(data, f, 0, from, 0);
        for (int i = 0; i < erase; ++i) cell_set(data, start + i, 0, from, 2);
        for (int i = 0; i < READER_BLACK_PUSHES; ++i)
            cell_set(data, start + erase + i, 0, from, 1);
    }
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
    boost_black_column(s_data, E0470_GL16_FRAMES);
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
    boost_black_column(s_page_data, E0470_GL16_FRAMES);
    s_page_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_page_ready = true;
    return &s_page_waveform;
}

const EpdWaveform* display_reader_water_waveform(void) {
    if (s_water_ready) return &s_water_waveform;
    const EpdWaveformPhases* base = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
    if (!base || !base->luts || base->phases != E0470_GL16_FRAMES || base->phase_times)
        return &E0470_WAVEFORM;
    // 水波纹保留原厂前导擦除和白列，只在同一张表上补 to=0 的推黑。
    // Water keeps the vendor leading erase and white column; only to=0 gets the extra pushes.
    memcpy(s_water_data, base->luts, sizeof(s_water_data));
    boost_black_column(s_water_data, E0470_GL16_FRAMES);
    s_water_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_water_ready = true;
    return &s_water_waveform;
}
