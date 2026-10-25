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
 * 用户 2026-10-25 续定：背景页用一张更深裁的派生表（擦除/白黑饱和再收，to=0 增黑保留），
 * 两个 accessor 在设置了阅读背景时自动切换；参数按残影与文字黑度上机回调。
 * User follow-up of 2026-10-25: background pages use a deeper-trimmed derivative (erase and
 * both saturation runs trimmed again, to=0 boost kept); the two accessors switch to it
 * automatically while a reading background is set; tune the numbers on hardware against
 * ghosting and text blackness.
 * 用户 2026-10-26：背景页翻页旧字转白擦不净（二次裁剪只剩 15 拍白推），白列改回主页
 * 水波纹已验证的 24 拍，其余迁移与相数不变，翻页时长不变。
 * User report 2026-10-26: erased text stayed pale on background pages because the second trim
 * left only 15 white pushes. The to=15 row now carries the main water table's verified 24
 * pushes; every other transition and the phase count stay put, so turns keep their timing.
 */
#include "display_reader_waveform.h"
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
#include "e0470_waveform_trim.h"
#include "settings.h"
#include "esp_log.h"
#include <stdbool.h>
#include <string.h>

// 背景页二次裁剪参数：在 37 相水波纹表上再收余量，目标 28 相上下。
// Second-pass trim for background pages: shave more slack off the 37-phase water table,
// aiming for roughly 28 phases.
#define BG_TRIM_ERASE_MAX 7
#define BG_TRIM_SAT_CUT 4
#define BG_TRIM_WHITE_SAT_CUT 3
#define BG_TRIM_HOLD 3

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

// 背景页派生表：相数运行时定，结构与上面三张同构。
// Background derivative: the phase count lands at runtime; the structure mirrors the three above.
static uint8_t s_bg_data[E0470_GL16_FRAMES][16][4];
static EpdWaveformPhases s_bg_phases;
static const EpdWaveformPhases* s_bg_ranges[1];
static const EpdWaveformMode s_bg_mode = {5, 1, s_bg_ranges};
static const EpdWaveformMode* s_bg_modes[] = {&s_bg_mode};
static EpdWaveform s_bg_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_bg_modes};
static bool s_bg_ready;

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
    e0470_boost_black_column(s_data, E0470_GL16_FRAMES);
    s_waveform.temp_intervals = E0470_TEXTTURN_WAVEFORM.temp_intervals;
    s_ready = true;
    return &s_waveform;
}

const EpdWaveform* display_reader_ink_waveform(void) {
    // 与背景设置无关的全页表：归一表拿它做基底，整屏 pass 的字芯墨度不能随背景开关漂移。
    // Background-independent page table: the normalize table builds on it so the whole-screen
    // pass lands on the same glyph ink whether or not a reading background is set.
    if (s_page_ready) return &s_page_waveform;
    const EpdWaveformPhases* base = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
    if (!base || !base->luts || base->phases != E0470_GL16_FRAMES || base->phase_times)
        return &E0470_WAVEFORM;
    memcpy(s_page_data, base->luts, sizeof(s_page_data));
    copy_white_column(s_page_data);
    e0470_boost_black_column(s_page_data, E0470_GL16_FRAMES);
    s_page_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_page_ready = true;
    return &s_page_waveform;
}

const EpdWaveform* display_reader_page_waveform(void) {
    // 背景页统一走派生表：深色图形上的饱和往返更短，黑闪与扫描时长一起下降。
    // Background pages always take the derivative: shorter saturation runs over dark
    // graphics cut both the black flashing and the scan time.
    if (app_settings_book_background_path()[0]) return display_reader_bg_waveform();
    return display_reader_ink_waveform();
}

// 无背景的水波纹原表：构建逻辑保持不变，供 accessor 与背景派生表作基底。
// The no-background water table: the build stays as-is, serving the accessor and as the
// base of the background derivative.
static const EpdWaveform* water_table(void) {
    if (s_water_ready) return &s_water_waveform;
    const EpdWaveformPhases* base = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
    if (!base || !base->luts || base->phases != E0470_GL16_FRAMES || base->phase_times)
        return &E0470_WAVEFORM;
    // 水波纹保留原厂前导擦除和白列，只在同一张表上补 to=0 的推黑。
    // Water keeps the vendor leading erase and white column; only to=0 gets the extra pushes.
    memcpy(s_water_data, base->luts, sizeof(s_water_data));
    e0470_boost_black_column(s_water_data, E0470_GL16_FRAMES);
    s_water_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_water_ready = true;
    return &s_water_waveform;
}

// 深色对上限：from 与 to 都不超过该值视为「深底上的文字迁移」，剥离前导擦白。
// Dark-pair ceiling: a pair with both from and to at or below it counts as text moving
// over dark art and gets its leading white erase stripped.
#define BG_DARK_MAX 7

// 表字节布局与裁表工具一致：data[frame][to][from/4]，每字节高位起 4 个 2bit 动作。
// Same byte layout as the trim tool: data[frame][to][from/4], four 2-bit actions MSB-first.
static int bg_get(int f, int to, int from) {
    return (s_bg_data[f][to][from / 4] >> (6 - 2 * (from % 4))) & 3;
}
static void bg_set(int f, int to, int from, int v) {
    const int shift = 6 - 2 * (from % 4);
    s_bg_data[f][to][from / 4] =
        (uint8_t)((s_bg_data[f][to][from / 4] & ~(3u << shift)) | ((unsigned)v << shift));
}

// 原厂对深色对的骨架是「先冲白轨擦除、再压黑轨饱和、灰尾落位」——深底图片上的
// 文字足迹每次翻页都整段白→黑往返，这就是黑闪。对角迁移（白→黑）没有擦除、直接
// 单调推：把深色对的前导擦白整段剥掉，只留「黑饱和 + 灰尾」右对齐，迁移变成与
// 文字落黑同向的变暗脉冲，不再有对比频闪；灰阶终点仍由原厂灰尾保证。
// The vendor skeleton for dark pairs is "erase to the white rail, saturate to the black
// rail, settle on the gray tail" -- every text footprint over dark art makes that full
// white-to-black round trip each turn, which is the black flashing. Diagonal moves
// (white-to-black) carry no erase and push monotonically: strip the leading white erase
// from dark pairs and keep only [black saturation + gray tail] right-aligned, turning the
// move into a darkening pulse in the same direction the text lands, with no contrast
// strobe; the vendor gray tail still ends on the exact target.
static void bg_strip_dark_erase(int frames) {
    uint8_t kept[E0470_GL16_FRAMES];
    for (int to = 0; to <= BG_DARK_MAX; ++to) {
        for (int from = 0; from <= BG_DARK_MAX; ++from) {
            if (from == to) continue;
            int a = 0;
            while (a < frames && !bg_get(a, to, from)) ++a;
            if (a >= frames || bg_get(a, to, from) != 2) continue;  // 无前导擦白则不动。/ Skip without a leading white run.
            int z = frames;
            while (z > a && !bg_get(z - 1, to, from)) --z;
            int e = a;
            while (e < z && bg_get(e, to, from) == 2) ++e;
            int len = z - e, start = frames - BG_TRIM_HOLD - len;
            if (start < a) {  // 剥离后仍超窗则再截头，保住结尾保持相。/ Still too long: cut more head, keep the trailing holds.
                start = a;
                len = frames - BG_TRIM_HOLD - start;
                e = z - len;
                if (len <= 0) continue;
            }
            for (int i = 0; i < len; ++i) kept[i] = (uint8_t)bg_get(e + i, to, from);
            for (int f = 0; f < frames; ++f) bg_set(f, to, from, 0);
            for (int i = 0; i < len; ++i) bg_set(start + i, to, from, kept[i]);
        }
    }
}

// 白列补强：基座拿的是原厂 18 拍白列，二次裁剪又削掉 3 拍，旧字转白只剩 15 拍，擦不净
// 就留下淡灰字形。主页水波纹的 to=15 行是实屏验证过的 24 拍；按原形状（含中间保持）
// 右对齐搬进来，只有短表放不下时才压掉中间保持相。
// White-column boost: the base carries the vendor's 18 pushes and the second trim shaves
// three more, leaving 15 to erase text to white -- too few, so glyphs stay pale gray. The
// main water table's to=15 row is the panel-verified 24 pushes; move it in right-aligned
// with its interior holds, compressing them only when the shorter table cannot take it.
static void bg_boost_white_column(int frames) {
    const EpdWaveformPhases* water = e0470_waveform_phases(display_main_water_waveform(), MODE_GL16);
    if (!water || !water->luts || water->phases != E0470_GL16_FRAMES || water->phase_times) return;
    const int limit = frames - BG_TRIM_HOLD;
    if (limit <= 0) return;
    for (int from = 0; from < 15; ++from) {
        int first = -1, last = -1;
        for (int f = 0; f < water->phases; ++f) {
            if (!e0470_phase_action(water, f, 15, from)) continue;
            if (first < 0) first = f;
            last = f;
        }
        if (first < 0) continue;
        for (int f = 0; f < frames; ++f) bg_set(f, 15, from, 0);
        const int span = last - first + 1;
        if (span <= limit) {
            const int start = limit - span;
            for (int i = 0; i < span; ++i)
                bg_set(start + i, 15, from, e0470_phase_action(water, first + i, 15, from));
            continue;
        }
        // 只留非零动作并右对齐；仍放不下就丢掉最早的白推，保留最靠近落位的部分。
        // Keep only the non-zero actions, right-aligned; if still too long, drop the earliest
        // pushes and keep the ones closest to the settle.
        uint8_t seq[E0470_GL16_FRAMES];
        int n = 0;
        for (int f = first; f <= last; ++f) {
            const int action = e0470_phase_action(water, f, 15, from);
            if (action) seq[n++] = (uint8_t)action;
        }
        const int skip = n > limit ? n - limit : 0;
        const int start = limit - (n - skip);
        for (int i = skip; i < n; ++i) bg_set(start + i - skip, 15, from, seq[i]);
    }
}

const EpdWaveform* display_reader_bg_waveform(void) {
    if (s_bg_ready) return &s_bg_waveform;
    const EpdWaveformPhases* base = e0470_waveform_phases(water_table(), MODE_GL16);
    if (!base || !base->luts || base->phases != E0470_GL16_FRAMES || base->phase_times)
        return water_table();
    // 再裁一轮擦除与白/黑饱和；to=0 仍补 18 连推，文字黑度不减。
    // A second trim of erase and both saturation runs; to=0 keeps the 18-push boost so
    // text blackness does not regress.
    const e0470_trim_t trim = {BG_TRIM_ERASE_MAX, BG_TRIM_SAT_CUT, BG_TRIM_WHITE_SAT_CUT, BG_TRIM_HOLD};
    const int frames = e0470_waveform_trim(base, &trim, s_bg_data);
    if (!frames) return water_table();
    e0470_boost_black_column(s_bg_data, frames);
    bg_strip_dark_erase(frames);
    // 黑闪主体在相等对：原厂表里除纯白外的 (g,g) 都带完整擦白+推黑，而条带整带标脏，
    // 深色图形的每个未变化像素都被推一遍白黑往返。差分路径从不驱动相等对，这里同样置保持。
    // The bulk of the flashing lives on equal pairs: every vendor (g,g) except pure white
    // carries a full white-then-black run, and the ripple dirties whole bands, so every
    // unchanged pixel of a dark graphic is driven through that excursion. The ordinary
    // differential never drives equal pairs; hold here the same way.
    for (int g = 0; g < 15; ++g)
        for (int f = 0; f < frames; ++f) bg_set(f, g, g, 0);
    bg_boost_white_column(frames);
    s_bg_phases = (EpdWaveformPhases){.phases = frames, .luts = (const uint8_t*)s_bg_data, .phase_times = NULL};
    s_bg_ranges[0] = &s_bg_phases;
    s_bg_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_bg_ready = true;
    ESP_LOGI("reader_wf", "background table frames=%d", frames);
    return &s_bg_waveform;
}

const EpdWaveform* display_reader_water_waveform(void) {
    return app_settings_book_background_path()[0] ? display_reader_bg_waveform() : water_table();
}
