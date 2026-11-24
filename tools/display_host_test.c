/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 真实 display.c 欠载恢复回归；仅模拟硬件与高层 framebuffer 边界。
 * Regression for real display.c underrun recovery, mocking hardware and high-level framebuffer boundaries only.
 * 冻结：目标画面不得丢失；白色基准只作用于后缓冲。
 * Frozen: Preserve the requested picture; the white baseline affects only the back buffer.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display.h"
#include "display_reader_waveform.h"
#include "display_static_black.h"
#include "app_config.h"
#include "settings.h"
#include "e0470_epaper_waveform.h"
app_main_refresh_mode_t app_settings_main_refresh_mode(void) {return APP_MAIN_REFRESH_NORMAL;}
// 所有真实推屏和欠载恢复都必须持有 OTA 互斥锁，包括递归兜底。
// Every physical scan and underrun recovery must hold OTA exclusion, including recursive fallback.
static unsigned ota_display_depth;
bool pico_online_display_begin(void) { ++ota_display_depth; return true; }
void pico_online_display_end(bool held) { assert(held && ota_display_depth); --ota_display_depth; }
void e0470_page_turn_release(void) {}
static int turn_tick_us = 21000;
void e0470_page_turn_set_tick_us(int us) { turn_tick_us = us; }
int e0470_page_turn_tick_us(void) { return turn_tick_us; }
bool app_settings_main_fast_refresh(void) {return false;}
bool frontlight_on(void) {return false;}
int epd_width(void) {return 1216;}
int epd_height(void) {return 684;}
static enum EpdRotation rotation = EPD_ROT_INVERTED_PORTRAIT;
enum EpdRotation epd_get_rotation(void) {return rotation;}
void* heap_caps_aligned_alloc(size_t a,size_t b,unsigned c) {(void)a;(void)b;(void)c;assert(false);return NULL;}
void heap_caps_free(void* p) {assert(p);free(p);}
const EpdWaveform* display_main_gray_waveform(void) {return &E0470_WAVEFORM;}
const EpdWaveform* display_main_shelf_exit_waveform(void) {return &E0470_WAVEFORM;}
// 本组不注册灰阶内容带；若误入分区呈现，立即失败。/ This suite registers no gray bands; fail on an unexpected regional presentation.
const EpdWaveform* display_main_shelf_waveform(enum EpdDrawMode mode) {
    (void)mode; assert(false); return NULL;
}
enum EpdDrawError e0470_update_regions(EpdiyHighlevelState* hl, EpdRect area,
    EpdRect strong_area, const EpdWaveform* soft_waveform,
    const EpdWaveform* strong_waveform, enum EpdDrawMode strong_mode) {
    (void)hl; (void)area; (void)strong_area; (void)soft_waveform;
    (void)strong_waveform; (void)strong_mode;
    assert(false); return EPD_DRAW_OTHER_ERROR;
}
static const EpdWaveform soft_gray = {.num_modes=4};
static const EpdWaveform test_gl16;
static bool use_join_wave, bg_join_wave;
const char* app_settings_book_background_path(void) { return bg_join_wave ? "test" : ""; }
const EpdWaveform* display_reader_bg_waveform(void) { return &test_gl16; }
static const EpdWaveform reader_soft = {.num_modes=5};
static const EpdWaveform reader_water = {.num_modes=6};
const EpdWaveform* display_reader_text_waveform(void) {return use_join_wave ? &test_gl16 : &reader_soft;}
const EpdWaveform* display_reader_water_waveform(void) {return &reader_water;}
const EpdWaveform* display_main_water_waveform(void) {return &soft_gray;}
const EpdWaveform* display_main_normal_waveform(void) { return display_main_water_waveform(); }
const EpdWaveform* display_main_bw_waveform(void) {return &E0470_WAVEFORM;}
const EpdWaveform* display_main_acrylic_waveform(void) {return &E0470_WAVEFORM;}
const EpdWaveform* display_main_acrylic_clean_waveform(void) {return &E0470_WAVEFORM;}
int epd_rotated_display_width(void) { return rotation & 1 ? 684 : 1216; }
int epd_rotated_display_height(void) { return rotation & 1 ? 1216 : 684; }

#define FB_BYTES 128
const EpdWaveform E0470_WAVEFORM = {.num_modes=0}, E0470_FOLLOW_WAVEFORM = {.num_modes=1}, E0470_GRAY8_WAVEFORM = {.num_modes=2}, E0470_FULL_WAVEFORM = {.num_modes=3},
    E0470_CAPSULE_DRAG_WAVEFORM = {.num_modes=4}, E0470_CAPSULE_WHITE_WAVEFORM = {.num_modes=5},
    E0470_CAPSULE_GRAY_WAVEFORM = {.num_modes=6}, E0470_APPLY_WAVEFORM = {.num_modes=7};
static uint8_t target[FB_BYTES], presented[FB_BYTES];
static int clocks, powerons, clears, draws, full_draws, safe_clock, prefill, last_mode, last_scan;
static int clears_at_draw;
static bool white_baseline, correct_target_at_draw;
static int scan_at_draw, prefill_at_draw;
static enum EpdDrawError next_draw_error;
static unsigned draw_errors_remaining;
static unsigned blind_pushes, repair_delays, poweroffs, clear_cycles;
static bool checking_repair;
static bool drive_active, no_change;
static bool power_failure;
static unsigned drive_releases, release_failures;
static int64_t now_us = 1000000;
static const EpdWaveform *waveform_at_draw;
static enum EpdDrawError water_result;
static int water_calls;
static EpdRect last_area;
static const EpdWaveform comic_wave = {.num_modes=1};
static const uint8_t *comic_front, *comic_back;
static EpdRect comic_body;
static unsigned comic_scans;
static bool check_static_black, check_reader_black, check_reader_background, check_joined, full_size_fb;
static unsigned static_black_scans, forgotten_waveforms;
static uint8_t test_gl16_lut[8][16][4];
static const EpdWaveformPhases test_gl16_phases = {8, (const uint8_t*)test_gl16_lut, NULL};
static const EpdWaveformPhases *test_gl16_ranges[] = {&test_gl16_phases};
static const EpdWaveformMode test_gl16_mode = {MODE_GL16, 1, test_gl16_ranges};
static const EpdWaveformMode *test_gl16_modes[] = {&test_gl16_mode};
static const EpdWaveformTempInterval test_temperatures[] = {{0, 50}};
static const EpdWaveform test_gl16 = {.num_modes=1, .num_temp_ranges=1,
    .mode_data=test_gl16_modes, .temp_intervals=test_temperatures};
static void physical_scan_power(void);

static unsigned phase_action(const EpdWaveformPhases *p, int f, unsigned transition) {
    return (p->luts[f * 64 + (transition >> 2)] >> (6 - 2 * (transition & 3))) & 3;
}

void epd_clear_phase_luts(void) {}
void epd_leading_skip_discard(void) {}
void epd_leading_skip_forget_waveform(void) { ++forgotten_waveforms; }
enum EpdDrawError epd_draw_base(EpdRect area, const uint8_t *data, EpdRect crop,
    enum EpdDrawMode mode, int temperature, const bool *lines,
    const uint8_t *columns, const EpdWaveform *waveform) {
    assert(ota_display_depth && !lines && !columns && temperature == 25);
    if (waveform == &comic_wave) {
        assert(mode == (MODE_PACKING_1PPB_DIFFERENCE | MODE_GC16) && last_scan == BOARD_EPD_SCAN_FULL);
        assert(area.width == 1216 && area.height == 684 && !memcmp(&area, &crop, sizeof(area)));
        physical_scan_power();
        ++comic_scans;
        for (int y = 0; y < area.height; ++y) for (int x = 0; x < area.width; ++x) {
            int logical_y = rotation == EPD_ROT_PORTRAIT ? area.width - 1 - x :
                rotation == EPD_ROT_INVERTED_PORTRAIT ? x :
                rotation == EPD_ROT_INVERTED_LANDSCAPE ? area.height - 1 - y : y;
            size_t i = (size_t)y * area.width + x;
            unsigned old = (comic_back[i / 2] >> (4 * (x & 1))) & 15;
            unsigned target_gray = (comic_front[i / 2] >> (4 * (x & 1))) & 15;
            unsigned pair = (target_gray << 4) | old;
            bool outside = logical_y < comic_body.y || logical_y >= comic_body.y + comic_body.height;
            unsigned expected = outside && old == target_gray ? DISPLAY_COMIC_HOLD :
                pair == DISPLAY_COMIC_HOLD ? DISPLAY_COMIC_EQUIVALENT : pair;
            assert(data[i] == expected);
        }
        enum EpdDrawError err = next_draw_error;
        next_draw_error = EPD_DRAW_SUCCESS;
        return err;
    }
    assert(mode == (MODE_PACKING_1PPB_DIFFERENCE | MODE_GL16));
    if (check_static_black || check_reader_black || check_reader_background || check_joined) {
        physical_scan_power();
        assert(area.width == 1216 && area.height == 684 && !memcmp(&area, &crop, sizeof(area)));
        const EpdWaveformPhases *phase = e0470_waveform_phases(waveform, MODE_GL16);
        assert(phase && phase->phases == test_gl16_phases.phases);
        const bool reader = check_reader_black || check_reader_background || check_joined;
        for (int f = 0; f < phase->phases; ++f) {
            for (unsigned code = 0; code < 256; ++code) {
                const unsigned expected = code == 0xff && !(check_reader_background || bg_join_wave) ? 0 :
                    code == 0 ? (unsigned)(f >= 3 && f <= 6) : code == 0xdd && reader ? (f == 5 ? 2 : 0) :
                    phase_action(&test_gl16_phases, f, code);
                assert(phase_action(phase, f, code) == expected);
            }
        }
        const size_t row = 1216;
        if (reader) {
            const unsigned hold = check_reader_background || bg_join_wave ? 0xee : 0xff;
            assert(data[4] == hold && data[6] == hold && data[8] == (check_joined ? 0x0f : hold) && data[10] == hold);
            assert(data[11] == 0xdd && data[20] == hold && data[23] == 0xdd && data[28] == hold);
        }
        if (check_joined) {
            const unsigned hold = bg_join_wave ? 0xee : 0xff;
            assert(data[0] == 0 && data[1] == 0xf0 && data[2] == 0x06 && data[3] == 0x60);
            assert(data[80] == hold && data[81] == 0x5f && data[84] == 0xff);
            assert(data[1214] == 0 && data[1215] == hold);
            assert(data[row + 1214] == hold && data[row + 1215] == 0x0f);
        } else if (check_reader_background) {
            assert(data[0] == 0 && data[1] == 0xee && data[2] == 0xee && data[3] == 0xee);
            assert(data[80] == 0xee && data[81] == 0x65);
            assert(data[82] == 0xee && data[83] == 0x77 && data[84] == 0xff && data[86] == 0xee);
            assert(data[1214] == 0 && data[1215] == 0xee);
        } else if (check_reader_black) {
            assert(data[0] == 0 && data[1] == 0xff && data[2] == 0xff && data[3] == 0xff);
            assert(data[80] == 0xff && (data[81] == 0x5f || data[81] == 0x65));
            assert(data[1214] == 0 && data[1215] == 0xff);
        } else {
            assert(data[0] == 0 && data[1] == 0xff && data[2] == 0xff);
            assert(data[row + 2] == 0 && data[row + 3] == 0x56 && data[row + 4] == 0xff);
            assert(data[row + 8] == 0xa9 && data[row + 9] == 0);
        }
        ++static_black_scans;
        enum EpdDrawError error = next_draw_error;
        next_draw_error = EPD_DRAW_SUCCESS;
        return error;
    }
    assert(false);
    return EPD_DRAW_OTHER_ERROR;
}

enum EpdDrawError e0470_page_turn(EpdiyHighlevelState* hl,EpdRect area,e0470_turn_dir_t dir) {
    (void)hl;(void)area;(void)dir;
    assert(false);return EPD_DRAW_OTHER_ERROR;
}
enum EpdDrawError e0470_page_turn_reader(EpdiyHighlevelState* hl,EpdRect area,
    e0470_turn_dir_t dir,const EpdWaveform* waveform,unsigned pushes) {
    assert(ota_display_depth);
    assert(hl&&area.width>0&&dir==E0470_TURN_RTL);
    assert(waveform==&reader_water&&pushes==4);
    ++water_calls;assert(last_scan==BOARD_EPD_SCAN_FAST);
    return water_result;
}

enum EpdDrawError e0470_page_turn_reader_joined(EpdiyHighlevelState* hl, EpdRect body,
    e0470_turn_dir_t dir, const EpdWaveform* wave, unsigned pushes, const EpdRect chrome[2], bool* scanned) {
    (void)hl; (void)body; (void)dir; (void)wave; (void)pushes; (void)chrome; (void)scanned;
    assert(false); return EPD_DRAW_OTHER_ERROR;
}

enum EpdDrawError e0470_page_turn_with_waveform_compact(EpdiyHighlevelState* hl, EpdRect area,
    e0470_turn_dir_t dir, const EpdWaveform* waveform, unsigned bands) {
    (void)hl; (void)area; (void)dir; (void)waveform; (void)bands;
    assert(false);
    return EPD_DRAW_OTHER_ERROR;
}

void board_epd_set_pclk(int mhz) { ++clocks; safe_clock = mhz; }
void board_epd_use_scan(board_epd_scan_t scan) { last_scan = scan; }
void epd_lcd_set_prefill_lines(int lines) { prefill = lines; }
void epd_poweron(void) { ++powerons; drive_active = false; }
bool board_rails_on(void) { return !power_failure; }
bool epd_power_scan_allowed(void) { return board_rails_on(); }
static unsigned power_session_depth;
static bool actual_power;
void epd_power_session_begin(void) { ++power_session_depth; }
void epd_power_session_end(void) { assert(power_session_depth); if (!--power_session_depth) actual_power = false; }
static void physical_scan_power(void) { assert(power_session_depth); if (!actual_power) { epd_poweron(); actual_power = true; } }

bool board_epd_release_drive(void) {
    assert(ota_display_depth);
    ++drive_releases;
    if (release_failures) { --release_failures; return false; }
    drive_active = false;
    return true;
}
void epd_poweroff(void) { if (checking_repair) assert(ota_display_depth); ++poweroffs; actual_power = false; drive_active = false; }
EpdRect epd_full_screen(void) { return (EpdRect){0,0,1216,684}; }
void vTaskDelay(uint32_t ticks) {
    assert(checking_repair && ota_display_depth);
    assert((repair_delays == 0 && ticks == 400) || (repair_delays > 0 && ticks == 1));
    ++repair_delays;
}
void epd_push_pixels(EpdRect area, short time, int color) {
    physical_scan_power();
    assert(checking_repair && ota_display_depth && poweroffs && powerons);
    assert(area.x == 0 && area.y == 0 && area.width == 1216 && area.height == 684 && time == 15);
    const unsigned phase = blind_pushes % 26;
    assert(color == (phase < 10 ? 0 : phase < 23 ? 1 : 2));
    ++blind_pushes;
}
void epd_clear(void) { physical_scan_power(); assert(ota_display_depth); assert(powerons > 0); ++clears; }
void epd_clear_area_cycles(EpdRect area, int cycles, int cycle_time) {
    physical_scan_power();
    assert(ota_display_depth && area.x == 0 && area.y == 0 && area.width == 1216 && area.height == 684);
    assert((cycles == 1 || cycles == 3) && cycle_time == 15);
    ++clears;
    clear_cycles += (unsigned)cycles;
}
int64_t esp_timer_get_time(void) { return now_us; }

// 与 highlevel.c:307 相同：该 API 清前缓冲，而不是参考后缓冲。
// Match highlevel.c:307: this API clears the front buffer, not the reference back buffer.
void epd_hl_set_all_white(EpdiyHighlevelState* hl) { memset(hl->front_fb, 255, FB_BYTES); }
void epd_hl_waveform(EpdiyHighlevelState* hl, const EpdWaveform* waveform) { hl->waveform = waveform; }
static enum EpdDrawError draw(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, bool full) {
    assert(ota_display_depth);
    assert((mode == MODE_DU || mode == MODE_GL16 || mode == MODE_GC16) && temperature == 25);
    if (no_change && !full) {
        assert(!memcmp(hl->front_fb, hl->back_fb, FB_BYTES));
        return EPD_DRAW_SUCCESS;
    }
    physical_scan_power();
    ++draws;
    clears_at_draw = clears;
    full_draws += full;
    last_mode = mode;
    waveform_at_draw = hl->waveform;
    scan_at_draw = last_scan; prefill_at_draw = prefill;
    white_baseline = true;
    for (size_t i = 0; i < FB_BYTES; ++i) if (hl->back_fb[i] != 255) white_baseline = false;
    correct_target_at_draw = memcmp(hl->front_fb, target, FB_BYTES) == 0;
    memcpy(presented, hl->front_fb, FB_BYTES);
    enum EpdDrawError err = next_draw_error;
    if (draw_errors_remaining > 1) --draw_errors_remaining;
    else { draw_errors_remaining = 0; next_draw_error = EPD_DRAW_SUCCESS; }
    if (err == EPD_DRAW_SUCCESS) memcpy(hl->back_fb, hl->front_fb,
        full_size_fb ? (size_t)epd_width() * epd_height() / 2 : FB_BYTES);
    return err;
}
enum EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature) {
    return draw(hl, mode, temperature, false);
}
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature) {
    return draw(hl, mode, temperature, true);
}
enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, EpdRect area) {
    (void)area;
    return draw(hl, mode, temperature, false);
}
enum EpdDrawError epd_hl_update_area_full(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, EpdRect area) {
    last_area = area;
    return draw(hl, mode, temperature, true);
}
// 测试桩不模拟脏区膨胀，按普通差分处理。
// The stub does not model dirty-mask dilation and treats both as ordinary differentials.
enum EpdDrawError epd_hl_update_area_padded(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, EpdRect area, int pad_lines, int pad_col_bytes) {
    (void)pad_lines; (void)pad_col_bytes;
    if (hl->waveform == &test_gl16) {
        assert(ota_display_depth && mode == MODE_GL16 && temperature == 25);
        EpdRect native;
        if (display_static_black_changed(hl, area, &native)) {
            physical_scan_power(); ++draws;
            const size_t off = 1216 / 2 + 1;
            hl->back_fb[off] = (hl->back_fb[off] & 0x0f) | (hl->front_fb[off] & 0xf0);
        }
        return EPD_DRAW_SUCCESS;
    }
    return epd_hl_update_area(hl, mode, temperature, area);
}
enum EpdDrawError epd_hl_update_screen_padded(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, int pad_lines, int pad_col_bytes) {
    (void)pad_lines; (void)pad_col_bytes;
    return draw(hl, mode, temperature, false);
}
// 与 highlevel.c:142 相同：白后缓冲后，强制整屏推目标前缓冲。
// Match highlevel.c:142: whiten the back buffer, then force a full update from the target front buffer.
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature) {
    memset(hl->back_fb, 255, full_size_fb ? (size_t)epd_width() * epd_height() / 2 : FB_BYTES);
    return epd_hl_update_screen_full(hl, mode, temperature);
}

static enum EpdDrawError power_fault_present(EpdiyHighlevelState *hl, unsigned path) {
    EpdRect area = {0, 160, 16, 16};
    switch (path) {
        case 0: return update_display_mode(hl, MODE_GL16);
        case 1: return update_display_mode_diff(hl, MODE_GL16);
        case 2: return update_display_mode_diff_with(hl, &E0470_WAVEFORM, MODE_GL16);
        case 3: return update_display_fast_page(hl);
        case 4: return update_display_from_white_with(hl, &E0470_WAVEFORM, MODE_GC16);
        case 5: return update_display_from_white(hl);
        case 6: return update_display_white(hl);
        case 7: return update_display_full(hl);
        case 8: return update_display_image_gray(hl);
        case 9: return update_display_with(hl, &E0470_WAVEFORM, MODE_GL16);
        case 10: return update_display_area_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
        case 11: return update_display_area_diff_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
        case 12: return update_display_area_diff_padded(hl, &E0470_WAVEFORM, MODE_DU, area, 1, 1);
        case 13: return update_display_area_full_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
        case 14: return update_display_click_feedback(hl, area);
        case 15: return update_display_water_turn(hl, area, E0470_TURN_RTL);
        default: return display_panel_repair(hl);
    }
}

static void test_static_black(void) {
    const int width = 1216, height = 684;
    const size_t pixels = (size_t)width * height, stride = width / 2;
    uint8_t *front = malloc(pixels / 2), *back = malloc(pixels / 2), *diff = malloc(pixels);
    assert(front && back && diff);
    full_size_fb = true;
    EpdiyHighlevelState hl = {.front_fb=front, .back_fb=back, .difference_fb=diff};
    memset(back, 0xff, pixels / 2);
    back[0] = 0x80;
    back[stride + 1] = 0x60;
    back[stride + 2] = 0xf0;
    back[stride + 4] = 0x09;
    memcpy(front, back, pixels / 2);
    assert(update_display_full(&hl) == EPD_DRAW_SUCCESS);
    const EpdRect area = {680, 3, 3, 6};
    EpdRect native;
    assert(!display_static_black_changed(&hl, area, &native));
    assert(native.x == 3 && native.y == 1 && native.width == 6 && native.height == 3);
    memset(test_gl16_lut, 0x55, sizeof(test_gl16_lut));
    test_gl16_lut[0][0][0] = 0x95;
    test_gl16_lut[0][15][3] = 0x56;
    test_gl16_lut[0][0][3] = 0x54;
    test_gl16_lut[7][0][3] = 0x54;
    test_gl16_lut[5][15][0] = 0x95;
    for (int i = 0; i < 3; ++i) {
        front[stride + 1] ^= 0x10;
        assert(update_display_area_stable_diff(&hl, &test_gl16, MODE_GL16, area) == EPD_DRAW_SUCCESS);
    }
    // 未变化或其他出口不消费补偿；区外尚未提交的目标也不算变化。
    // No-ops and unrelated routes do not consume the pending correction; outside pending targets do not count as changes.
    assert(update_display_area_stable_diff(&hl, &test_gl16, MODE_GL16, area) == EPD_DRAW_SUCCESS);
    front[0] = 0xff; front[1] = 0;
    front[stride + 1] = 0x5f; front[stride + 4] = 0xfa;
    back[stride + 1] = 0x60;
    check_static_black = true;
    const int normal_draws = draws;
    assert(update_display_area_stable_diff(&hl, &test_gl16, MODE_GL16, area) == EPD_DRAW_SUCCESS);
    assert(static_black_scans == 1 && draws == normal_draws);
    assert(back[0] == 0x80 && back[1] == 0xff);
    assert(back[stride + 1] == 0x50 && back[stride + 4] == 0x0a);
    // 复位后需要再来四次；让失败经过公共出口，并验证下一次请求转为整屏恢复。
    // Reset requires four more changes; fail through the public exit and require full recovery on the next request.
    check_static_black = false;
    for (int i = 0; i < 3; ++i) {
        front[stride + 1] ^= 0x10;
        assert(update_display_area_stable_diff(&hl, &test_gl16, MODE_GL16, area) == EPD_DRAW_SUCCESS);
    }
    assert(static_black_scans == 1);
    back[stride + 1] = 0x60; back[stride + 4] = 0x09;
    front[stride + 1] = 0x5f;
    next_draw_error = EPD_DRAW_OTHER_ERROR;
    check_static_black = true;
    assert(update_display_area_stable_diff(&hl, &test_gl16, MODE_GL16, area) == EPD_DRAW_OTHER_ERROR);
    assert(static_black_scans == 2 && forgotten_waveforms == 2);
    assert(back[0] == 0x80 && back[1] == 0xff && back[stride + 1] == 0x60 && back[stride + 4] == 0x09);
    check_static_black = false;
    const int before_recovery = full_draws;
    assert(update_display_area_stable_diff(&hl, &test_gl16, MODE_GL16, area) == EPD_DRAW_SUCCESS);
    assert(full_draws == before_recovery + 1 && !memcmp(front, back, pixels / 2));
    // 连续正文翻页立即补偿；区外新黑与待擦黑都保持，且不消耗通用局刷计数。
    // Consecutive body turns reinforce immediately; hold newly staged and soon-erased outside targets.
    memset(back, 0xff, pixels / 2); memset(front, 0xff, pixels / 2);
    const EpdRect body = {0, 81, height, width - 156};
    back[0] = 0x00; front[0] = 0xf0;
    back[1] = 0x06; front[1] = 0x60;
    front[3] = back[3] = 0xfd; front[4] = 0xf0;
    back[2 * stride + 10] = 0xfd; front[2 * stride + 14] = 0xfd;
    front[stride - 1] = back[stride - 1] = 0xf0;
    check_reader_black = true;
    front[40] = 0x5f;
    assert(update_display_area_reader_turn(&hl, &test_gl16, MODE_GL16, body, false) == EPD_DRAW_SUCCESS);
    check_reader_black = false;
    check_reader_background = true;
    for (int f = 0; f < test_gl16_phases.phases; ++f) {
        test_gl16_lut[f][0][0] &= 0x3f;
        test_gl16_lut[f][14][3] &= 0xf3;
        test_gl16_lut[f][13][3] &= 0xcf;
    }
    // 有背景仍按原表驱动正文 FF、77 等色；正文黑不借用专门给栏位补黑的 00。
    // Keep equal body pairs such as FF and 77 on the original background table; black body pixels must not use chrome 00.
    front[40] = 0x6f;
    front[41] = back[41] = 0x70;
    front[43] = back[43] = 0xfd;
    assert(update_display_area_reader_turn(&hl, &test_gl16, MODE_GL16, body, true) == EPD_DRAW_SUCCESS);
    assert(static_black_scans == 4 && back[0] == 0x00 && back[1] == 0x06 && back[40] == 0x6f);
    check_reader_background = false;
    test_gl16_lut[0][0][0] |= 0x40;
    const int prior_full = full_draws;
    front[40] = 0x5f;
    assert(update_display_area_reader_turn(&hl, &test_gl16, MODE_GL16, body, true) == EPD_DRAW_SUCCESS);
    assert(static_black_scans == 4 && full_draws == prior_full + 1);
    // 一次普通 GL16 提交正文和请求的栏位；无关目标和失败帧均不回写。
    // One GL16 scan commits body and requested bars; ignore unrelated targets and preserve the failed baseline.
    check_joined = use_join_wave = true;
    test_gl16_lut[0][0][0] &= 0x3f;
    const EpdRect top = {0, 0, height, 80}, footer = {1, 1141, height - 2, 75};
    for (int i = 0; i < 3; ++i) {
        memset(back, 0xff, pixels / 2); memset(front, 0xff, pixels / 2);
        bg_join_wave = i > 0;
        back[0] = 0x00; front[0] = 0xf0;
        back[1] = 0x06; front[1] = 0x60;
        front[3] = back[3] = 0xfd; front[4] = 0xf0;
        back[2 * stride + 10] = 0xfd; front[2 * stride + 14] = 0xfd;
        front[40] = 0x50;
        back[stride - 1] = 0xf0; front[stride - 1] = 0x60;
        front[2 * stride - 1] = 0x0f;
        if (i == 2) next_draw_error = EPD_DRAW_OTHER_ERROR;
        const unsigned before = static_black_scans;
        enum EpdDrawError result = EPD_DRAW_INVALID_CROP;
        assert(update_display_reader_joined(&hl, body, top, footer, false, E0470_TURN_RTL, &result));
        assert(static_black_scans == before + 1);
        assert(result == (i == 2 ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS));
        assert(back[0] == (i == 2 ? 0 : 0xf0) && back[1] == (i == 2 ? 0x06 : 0x60));
        assert(back[40] == (i == 2 ? 0xff : 0x5f) && back[stride - 1] == 0xf0);
        assert(back[2 * stride - 1] == (i == 2 ? 0xff : 0x0f));
    }
    check_joined = bg_join_wave = use_join_wave = false;
    assert(update_display_full(&hl) == EPD_DRAW_SUCCESS);
    full_size_fb = false;
    free(diff); free(back); free(front);
}

static void test_comic_regions(void) {
    size_t bytes = (size_t)epd_width() * epd_height() / 2;
    uint8_t *front = malloc(bytes), *back = malloc(bytes), *original = malloc(bytes), *diff = malloc(2 * bytes);
    assert(front && back && original && diff);
    for (size_t i = 0; i < 2 * bytes; i += 2) {
        unsigned a = i & 255, b = (i + 1) & 255;
        original[i / 2] = (a & 15) | (b << 4);
        front[i / 2] = (a >> 4) | (b & 0xf0);
    }
    EpdiyHighlevelState hl = {.front_fb=front, .back_fb=back, .difference_fb=diff};
    comic_front = front; comic_back = original; full_size_fb = true;
    for (int rot = 0; rot < 4; ++rot) for (int bars = 0; bars < 4; ++bars) {
        rotation = (enum EpdRotation)rot;
        int width = epd_rotated_display_width(), height = epd_rotated_display_height();
        int top = bars & 1 ? 83 : 0, footer = bars & 2 ? 71 : 0;
        comic_body = (EpdRect){0, top, width, height - top - footer};
        for (int fail = 0; fail < 2; ++fail) {
            memcpy(back, original, bytes);
            enum EpdDrawError expected = fail ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS;
            next_draw_error = expected;
            unsigned scans = comic_scans;
            int full = full_draws;
            assert(update_display_reader_comic(&hl, &comic_wave, comic_body) == expected);
            assert(comic_scans == scans + (bars != 0) && full_draws == full + (bars == 0));
            if (!bars) assert(waveform_at_draw == &E0470_FULL_WAVEFORM && last_mode == MODE_GC16);
            assert(!memcmp(back, fail ? original : front, bytes));
        }
    }
    memcpy(back, original, bytes);
    int full = full_draws;
    assert(update_display_reader_comic(&hl, &E0470_FULL_WAVEFORM, comic_body) == EPD_DRAW_SUCCESS);
    assert(full_draws == full + 1 && !memcmp(back, front, bytes));
    full_size_fb = false;
    free(diff); free(original); free(back); free(front);
    puts("comic: one GC16 scan, all rotation and bar combinations, exact odd edges, error baseline and original-table fallback");
}

int main(void) {
    uint8_t front[FB_BYTES], back[FB_BYTES];
    for (size_t i = 0; i < FB_BYTES; ++i) target[i] = (uint8_t)(i * 37U + 3U);
    memcpy(front, target, FB_BYTES);
    memset(back, 0x55, FB_BYTES);
    EpdiyHighlevelState hl = {.front_fb = front, .back_fb = back, .waveform = &E0470_WAVEFORM};
    // 局部控件刷新不推进整页清残影计数；第 40 次整页差分才升级为 GC16。
    // Local control updates do not advance cleanup; only the 40th whole-page differential promotes to GC16.
    clears = draws = full_draws = last_mode = 0;
    for (int i = 0; i < 80; ++i)
        assert(update_display_area_with(&hl, &E0470_WAVEFORM, MODE_GL16, (EpdRect){0, 0, 16, 16}) == EPD_DRAW_SUCCESS);
    assert(last_mode == MODE_GL16);
    // 反复局部差分不强驱动未变像素，也不累积整页 GC16 计数。
    // Repeated local differentials neither force unchanged pixels nor advance whole-page GC16 cleanup.
    int locals_full = full_draws;
    for (int i = 0; i < 200; ++i)
        assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                            (EpdRect){0, 160, 684, 936}) == EPD_DRAW_SUCCESS);
    assert(full_draws == locals_full && last_mode == MODE_GL16);
    // 词栏停顿整理必须驱动未变化的白底；局部 GL16 不清整屏、不升级 GC16。
    // Idle word-strip settling drives unchanged white pixels without screen clears or GC16 promotion.
    for (int i = 0; i < 80; ++i) {
        int before_full = full_draws, before_draws = draws;
        assert(update_display_area_full_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                            (EpdRect){24, 612, 636, 114}) == EPD_DRAW_SUCCESS);
        assert(full_draws == before_full + 1 && draws == before_draws + 1 && !clears);
        assert(last_area.x == 24 && last_area.y == 612 && last_area.width == 636 && last_area.height == 114);
        assert(last_mode == MODE_GL16 && waveform_at_draw == &E0470_WAVEFORM && correct_target_at_draw);
    }
    for (int i = 0; i < APP_UI_GC16_EVERY - 1; ++i) {
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS);
        assert(last_mode == MODE_GL16);
    }
    assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS);
    assert(last_mode == MODE_GC16);

    // 普通子页面一次输出柔和灰阶，第三次不驱动整页，达到原阈值仍用原表GC16。
    // Ordinary subpages use one soft-gray difference without third-page full output, retaining original-table GC16 at the threshold.
    for (int i = 0; i < APP_UI_FAST_GC16_EVERY - 1; ++i) {
        int prior_full = full_draws;
        assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
        assert(last_mode == MODE_GL16 && last_scan == BOARD_EPD_SCAN_FULL);
        assert(waveform_at_draw == &soft_gray && hl.waveform == &E0470_WAVEFORM);
        assert(full_draws == prior_full);
    }
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    assert(last_mode == MODE_GC16 && waveform_at_draw == &E0470_WAVEFORM && last_scan == BOARD_EPD_SCAN_FULL && full_draws > 0);

    // Standalone image pages retain three optical cleanup cycles and the full grayscale waveform.
    memcpy(front, target, FB_BYTES);
    memset(back, 0x55, FB_BYTES);
    clears = draws = clear_cycles = 0;
    assert(update_display_image_gray(&hl) == EPD_DRAW_SUCCESS);
    assert(clears == 1 && clear_cycles == 3 && draws == 1 && white_baseline && correct_target_at_draw);
    assert(last_mode == MODE_GC16 && waveform_at_draw == &E0470_FULL_WAVEFORM &&
           hl.waveform == &E0470_WAVEFORM);

    // Reader entry image frames use one cleanup cycle, while the standalone image page retains three.
    memcpy(front, target, FB_BYTES);
    memset(back, 0x55, FB_BYTES);
    clears = draws = clear_cycles = 0;
    assert(update_display_reader_image_gray(&hl) == EPD_DRAW_SUCCESS);
    assert(clears == 1 && clear_cycles == 1 && draws == 1 && white_baseline && correct_target_at_draw);
    assert(last_mode == MODE_GC16 && waveform_at_draw == &E0470_FULL_WAVEFORM &&
           hl.waveform == &E0470_WAVEFORM);

    // 关机清白先于48相出图，失败仍保留完整重建请求。/ Shutdown clears before the 48-phase draw and retains full recovery on failure.
    for (unsigned fail = 0; fail < 2; ++fail) {
        memcpy(front, target, FB_BYTES);
        memset(back, 0x55, FB_BYTES);
        clears = draws = clear_cycles = 0;
        const int prior_full = full_draws;
        const enum EpdDrawError expected = fail ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS;
        next_draw_error = expected;
        assert(update_display_full_clean(&hl) == expected);
        assert(clears == 1 && clears_at_draw == 1 && draws == 1 && full_draws == prior_full + 1);
        assert(white_baseline && correct_target_at_draw && !memcmp(front, target, FB_BYTES));
        assert(last_mode == MODE_GC16 && waveform_at_draw == &E0470_FULL_WAVEFORM);
        assert(hl.waveform == &E0470_WAVEFORM && !ota_display_depth);
        assert(update_display_full(&hl) == EPD_DRAW_SUCCESS);
        assert(clears == (fail ? 2 : 1) && !memcmp(back, target, FB_BYTES));
        assert(waveform_at_draw == (fail ? &E0470_FULL_WAVEFORM : &E0470_WAVEFORM));
    }

    // 撤回跟手滚动后，解锁的下一帧仍须全刷一次，随后恢复局部差分。
    // Removing follow scrolling must retain exactly one full update on the next wake frame, then resume local differences.
    int before_wake_full = full_draws;
    display_request_full_refresh();
    assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                        (EpdRect){0,160,684,928}) == EPD_DRAW_SUCCESS);
    assert(full_draws == before_wake_full + 1 && last_mode == MODE_GC16);
    assert(waveform_at_draw == &E0470_FULL_WAVEFORM && hl.waveform == &E0470_WAVEFORM);
    assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                        (EpdRect){0,160,684,928}) == EPD_DRAW_SUCCESS);
    assert(full_draws == before_wake_full + 1 && last_mode == MODE_GL16);
    display_request_full_refresh();
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    assert(full_draws == before_wake_full + 2 && last_mode == MODE_GC16);
    assert(waveform_at_draw == &E0470_FULL_WAVEFORM && hl.waveform == &E0470_WAVEFORM);
    // 扫描失败保留48相请求，供电恢复重建也不能降回36相。
    // Failed scans retain 48-phase requests, including full reconstruction after power recovery.
    display_request_full_refresh();
    next_draw_error = EPD_DRAW_OTHER_ERROR;
    assert(update_display_full(&hl) == EPD_DRAW_OTHER_ERROR);
    assert(waveform_at_draw == &E0470_FULL_WAVEFORM && hl.waveform == &E0470_WAVEFORM);
    int before_retry_clears = clears;
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    assert(clears == before_retry_clears + 1 && white_baseline);
    assert(waveform_at_draw == &E0470_FULL_WAVEFORM && hl.waveform == &E0470_WAVEFORM);
    display_request_full_refresh(); power_failure = true;
    assert(update_display_fast_page(&hl) == DISPLAY_DRAW_POWER_NOT_READY);
    now_us += 1001000; power_failure = false;
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    assert(waveform_at_draw == &E0470_FULL_WAVEFORM && hl.waveform == &E0470_WAVEFORM);
    assert(update_display_full(&hl) == EPD_DRAW_SUCCESS);
    assert(waveform_at_draw == &E0470_WAVEFORM);
    display_request_full_refresh();
    next_draw_error = EPD_DRAW_EMPTY_LINE_QUEUE;
    assert(update_display_full(&hl) == EPD_DRAW_EMPTY_LINE_QUEUE);
    guard_draw_result(&hl, EPD_DRAW_EMPTY_LINE_QUEUE);
    assert(waveform_at_draw == &E0470_FULL_WAVEFORM && hl.waveform == &E0470_WAVEFORM);
    assert(update_display_full(&hl) == EPD_DRAW_SUCCESS && waveform_at_draw == &E0470_WAVEFORM);
    // 页面自定义出口同样服从一次48相请求；成功时无预清白，失败时保持重试。
    // Custom page exits also honor one 48-phase request, without pre-clearing on success and retaining failed retries.
    for (unsigned path = 0; path < 16; ++path) {
        memcpy(front, target, FB_BYTES); memset(back, 0x55, FB_BYTES);
        int before_draws = draws, before_clears = clears, before_full = full_draws;
        display_request_full_refresh();
        assert(power_fault_present(&hl, path) == EPD_DRAW_SUCCESS);
        assert(draws == before_draws + 1 && full_draws == before_full + 1 && clears == before_clears);
        assert(last_mode == MODE_GC16 && waveform_at_draw == &E0470_FULL_WAVEFORM);
        assert(hl.waveform == &E0470_WAVEFORM && !memcmp(front, back, FB_BYTES));
        assert(update_display_full(&hl) == EPD_DRAW_SUCCESS && waveform_at_draw == &E0470_WAVEFORM);
    }

    clocks = powerons = clears = draws = full_draws = last_mode = 0;
    guard_draw_result(&hl, EPD_DRAW_SUCCESS);
    guard_draw_result(&hl, EPD_DRAW_OTHER_ERROR);
    assert(!clocks && !clears && !draws && !memcmp(front, target, FB_BYTES));
    for (int bulk = 0; bulk < 2; ++bulk) {
        memcpy(front, target, FB_BYTES);
        memset(back, 0x55, FB_BYTES);
        clocks = powerons = clears = draws = full_draws = last_mode = 0;
        display_set_bulk_io(bulk != 0);
        guard_draw_result(&hl, EPD_DRAW_EMPTY_LINE_QUEUE | EPD_DRAW_OTHER_ERROR);
        if (memcmp(front, target, FB_BYTES)) {
            fputs("FAIL: underrun recovery erased target front_fb (white screen regression)\n", stderr);
            return 1;
        }
        assert(correct_target_at_draw && !memcmp(presented, target, FB_BYTES));
        assert(white_baseline && full_draws == 1 && draws == 1);
        assert(!memcmp(back, target, FB_BYTES));
        assert(clocks == 1 && safe_clock == DISPLAY_PCLK_SAFE_MHZ && display_pclk_mhz() == DISPLAY_PCLK_SAFE_MHZ);
        assert(powerons == 1 && clears == 1 && prefill == (bulk ? 127 : 32));
    }
    // 缺波形直接沿用 GL16；扫描中断则从白底重建整屏目标，不把半途像素当作已完成。
    // Missing phases use GL16; an interrupted scan rebuilds the target from white.
    display_set_bulk_io(false);
    memcpy(front, target, FB_BYTES);
    memset(back, 0x55, FB_BYTES);
    water_calls = draws = clears = 0;
    water_result = EPD_DRAW_SUCCESS;
    assert(update_display_water_turn(&hl,(EpdRect){0,0,16,16},E0470_TURN_RTL)==EPD_DRAW_SUCCESS);
    assert(water_calls==1&&!draws&&!clears&&last_scan==BOARD_EPD_SCAN_FULL);
    assert(turn_tick_us==21000&&!ota_display_depth);
    water_calls=0;
    water_result = EPD_DRAW_NO_PHASES_AVAILABLE;
    assert(update_display_water_turn(&hl, (EpdRect){0, 0, 16, 16}, E0470_TURN_RTL) == EPD_DRAW_SUCCESS);
    assert(water_calls == 1 && draws == 1 && !clears && last_mode == MODE_GL16);
    memset(back, 0x55, FB_BYTES);
    water_result = EPD_DRAW_OTHER_ERROR;
    assert(update_display_water_turn(&hl, (EpdRect){0, 0, 16, 16}, E0470_TURN_RTL) == EPD_DRAW_SUCCESS);
    assert(water_calls == 2 && clears == 1 && white_baseline && correct_target_at_draw);
    assert(last_mode == MODE_GC16 && !memcmp(back, target, FB_BYTES));
    memcpy(front,target,FB_BYTES);memset(back,0x55,FB_BYTES);draws=clears=full_draws=0;
    // 阅读全刷与 rc90 一致：一轮原表 GC16，不先 epd_clear，也不追加白底文字输出。
    // Reader full refresh matches rc90: one original-table GC16 output, without epd_clear or a white-baseline text pass.
    assert(update_display_full(&hl)==EPD_DRAW_SUCCESS);
    assert(clears==0&&draws==1&&full_draws==1&&last_mode==MODE_GC16&&!white_baseline&&correct_target_at_draw);
    assert(waveform_at_draw==&E0470_WAVEFORM);
    assert(!memcmp(front,target,FB_BYTES)&&!memcmp(back,target,FB_BYTES));
    // 手动修复保留目标页和底栏，只做一次最终整屏输出；成功后局部更新不再被迫全刷。
    // Manual repair preserves the target and navigation with one final full output, then allows local updates again.
    checking_repair = true;
    for (int failure = 0; failure < 3; ++failure) {
        memcpy(front, target, FB_BYTES); memset(back, 0x55, FB_BYTES);
        blind_pushes = repair_delays = poweroffs = 0;
        draws = full_draws = clears = 0;
        next_draw_error = failure ? EPD_DRAW_EMPTY_LINE_QUEUE : EPD_DRAW_SUCCESS;
        draw_errors_remaining = failure;
        display_main_transition_arm(&hl, true, true);
        display_request_full_refresh();
        enum EpdDrawError repaired = display_panel_repair(&hl);
        assert(blind_pushes == 6 * 26 && repair_delays == 7 && poweroffs == 1);
        assert(!memcmp(front, target, FB_BYTES) && hl.waveform == &E0470_WAVEFORM);
        assert(draws == (failure ? 2 : 1) && full_draws == draws);
        assert(correct_target_at_draw && white_baseline && !ota_display_depth);
        assert((repaired == EPD_DRAW_SUCCESS) == (failure != 2));
        int prior = full_draws;
        assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                            (EpdRect){0,160,16,16}) == EPD_DRAW_SUCCESS);
        assert(full_draws == prior + (failure == 2));
    }
    unsigned prior_pushes = blind_pushes;
    assert(display_panel_repair(NULL) == EPD_DRAW_INVALID_CROP && blind_pushes == prior_pushes);
    checking_repair = false;
    assert(!ota_display_depth);
    puts("display recovery: front retained, locked blind panel repair, single final frame, retry/failure recovery and bulk prefill passed");
    // 每个输出出口都必须拒绝未就绪的供电；连续输入不重复等待，恢复失败继续要求整屏重建。
    // Every output must reject unready power; queued input cannot repeat the wait, and failed recovery retains the full-screen rebuild request.
    no_change = false;
    for (unsigned path = 0; path <= 16; ++path) {
        now_us += 2000000;
        for (unsigned i = 0; i < FB_BYTES; ++i) target[i] = (uint8_t)(i + path * 13);
        memcpy(front, target, FB_BYTES); memset(back, 0x5a, FB_BYTES);
        power_failure = true;
        checking_repair = path == 16;
        if (checking_repair) repair_delays = 0;
        int before_draws = draws, before_clears = clears, before_water = water_calls;
        unsigned before_blind = blind_pushes;
        assert(power_fault_present(&hl, path) == DISPLAY_DRAW_POWER_NOT_READY);
        checking_repair = false;
        assert(draws == before_draws && clears == before_clears && water_calls == before_water);
        assert(blind_pushes == before_blind && !drive_active && !ota_display_depth);
        for (unsigned i = 0; i < FB_BYTES; ++i) assert(back[i] == 0x5a);
        // white 出口的目标本来就是白；其他出口必须保留调用方的完整目标。
        // White output intentionally requests white; every other output preserves the caller's complete target.
        if (path == 6) memset(target, 0xff, FB_BYTES);
        assert(!memcmp(front, target, FB_BYTES));
        int attempts = powerons;
        for (unsigned n = 0; n < 20; ++n)
            assert(update_display_mode_diff(&hl, MODE_GL16) == DISPLAY_DRAW_POWER_NOT_READY);
        guard_draw_result(&hl, DISPLAY_DRAW_POWER_NOT_READY);
        guard_draw_result(&hl, EPD_DRAW_EMPTY_LINE_QUEUE);
        assert(powerons == attempts && draws == before_draws && clears == before_clears);
        now_us += 1001000; power_failure = false;
        next_draw_error = EPD_DRAW_OTHER_ERROR;
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_OTHER_ERROR);
        assert(clears == before_clears + 1 && draws == before_draws + 1 && white_baseline);
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS);
        assert(clears == before_clears + 2 && draws == before_draws + 2 && correct_target_at_draw);
        assert(!memcmp(front, target, FB_BYTES) && !memcmp(back, target, FB_BYTES) && !ota_display_depth);
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS);
        assert(clears == before_clears + 2);
    }
    puts("power recovery: all 17 outputs reject missing rails without committing back; queued retries are bounded and recovery preserves the target until full output succeeds");
    {
        int before = clears, before_draws = draws;
        assert(update_display_boot_frame(&hl) == EPD_DRAW_SUCCESS);
        assert(clears == before + 1 && draws == before_draws + 1 && white_baseline);
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS && clears == before + 1);
    }
    int before = clears, before_draws = draws;
    power_failure = true;
    assert(update_display_boot_frame(&hl) == DISPLAY_DRAW_POWER_NOT_READY);
    assert(clears == before && draws == before_draws);
    now_us += 1001000;
    power_failure = false;
    assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS && clears == before + 1);
    puts("boot: one clear for a centered start; power rejection defers the baseline rebuild");
    test_static_black();
    test_comic_regions();
}

// Continuous state is exercised separately with native-size buffers.

void epd_fill_rect(EpdRect area, uint8_t color, uint8_t *fb) {
    (void)area; (void)color; (void)fb; assert(false);
}
