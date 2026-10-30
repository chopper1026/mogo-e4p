/*
 * 中文：记录真实呈现路由的显示调用，检查区域、波形、错误传播和水波纹节拍恢复。
 * English: Record real presentation calls to check regions, waveforms, error propagation and restored ripple pacing.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reader_present.h"
#include "display_refresh_policy.h"
#include "display.h"
#include "display_reader_waveform.h"
#include "display_normalize.h"
#include "e0470_epaper_waveform.h"

const EpdWaveform E0470_WAVEFORM = {1}, E0470_FULL_WAVEFORM = {2},
    E0470_FOLLOW_WAVEFORM = {3};
static const EpdWaveform text = {4}, page = {5}, image = {6},
    normalize = {7}, normalize_gray = {8};
const EpdWaveform* display_reader_text_waveform(void) { return &text; }
const EpdWaveform* display_reader_page_waveform(void) { return &page; }
const EpdWaveform* display_reader_image_waveform(void) { return &image; }
const EpdWaveform* display_normalize_waveform(bool gray) {
    return gray ? &normalize_gray : &normalize;
}
const EpdWaveform *display_waveform_get(display_waveform_id_t id) {
    switch(id) {
        case DISPLAY_WAVE_DEFAULT: return &E0470_WAVEFORM;
        case DISPLAY_WAVE_FULL: return &E0470_FULL_WAVEFORM;
        case DISPLAY_WAVE_FOLLOW: return &E0470_FOLLOW_WAVEFORM;
        case DISPLAY_WAVE_READER_TEXT: return &text;
        case DISPLAY_WAVE_READER_INK: return &page;
        case DISPLAY_WAVE_READER_IMAGE: return &image;
        case DISPLAY_WAVE_NORMALIZE: return &normalize;
        case DISPLAY_WAVE_NORMALIZE_GRAY: return &normalize_gray;
        default: assert(!"unexpected waveform"); return NULL;
    }
}
display_refresh_context_t display_refresh_context(void) {
    return (display_refresh_context_t){0};
}
enum EpdDrawError update_display_area_diff_padded(EpdiyHighlevelState *s, const EpdWaveform *w,
                                                 enum EpdDrawMode m, EpdRect a, int x, int y) {
    (void)s;(void)w;(void)m;(void)a;(void)x;(void)y;assert(!"unexpected padded output");return EPD_DRAW_INVALID_CROP;
}
enum call_kind { AREA, DIFF, FULL_AREA, WHOLE, WHOLE_DIFF, FULL, FAST, WATER, TICK };
typedef struct {
    enum call_kind kind;
    const EpdWaveform *wave;
    int mode;
    EpdRect area;
    int value;
} call_t;
static call_t calls[16];
static size_t count;
static enum EpdDrawError errors[16];
static size_t outputs;
static int tick;
static uint8_t front[4] = {1, 2, 3, 4}, back[4] = {4, 3, 2, 1};
static EpdiyHighlevelState hl = {.front_fb = front, .back_fb = back};
static const EpdRect none = {0};
static enum EpdDrawError record(EpdiyHighlevelState *state, enum call_kind kind,
                                const EpdWaveform *wave, int mode, EpdRect area) {
    assert(state == &hl && state->front_fb == front && state->back_fb == back);
    assert(count < 16 && outputs < 16);
    calls[count++] = (call_t){kind, wave, mode, area, tick};
    return errors[outputs++];
}
#define AREA_OUTPUT(name, kind) \
enum EpdDrawError name(EpdiyHighlevelState *state, const EpdWaveform *wave, \
                      enum EpdDrawMode mode, EpdRect area) { \
    return record(state, kind, wave, mode, area); \
}
AREA_OUTPUT(update_display_area_with, AREA)
AREA_OUTPUT(update_display_area_diff_with, DIFF)
AREA_OUTPUT(update_display_area_full_with, FULL_AREA)
enum EpdDrawError update_display_with(EpdiyHighlevelState *s, const EpdWaveform *w, enum EpdDrawMode m) {
    return record(s, WHOLE, w, m, none);
}
enum EpdDrawError update_display_mode_diff_with(EpdiyHighlevelState *s, const EpdWaveform *w, enum EpdDrawMode m) {
    return record(s, WHOLE_DIFF, w, m, none);
}
enum EpdDrawError update_display_full(EpdiyHighlevelState *s) { return record(s, FULL, NULL, 0, none); }
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState *s) { return record(s, FAST, NULL, 0, none); }
enum EpdDrawError update_display_water_turn(EpdiyHighlevelState *s, EpdRect a, e0470_turn_dir_t d) {
    return record(s, WATER, NULL, d, a);
}
int e0470_page_turn_tick_us(void) { return tick; }
void e0470_page_turn_set_tick_us(int value) {
    assert(count < 16);
    calls[count++] = (call_t){.kind = TICK, .value = value};
    tick = value;
}
static reader_present_request_t reset(void) {
    count = outputs = 0;
    memset(errors, 0, sizeof(errors));
    tick = 13579;
    return (reader_present_request_t){
        .refresh = READER_PRESENT_AREA, .mode = MODE_GL16, .page_mode = MODE_DU,
        .area = {1, 2, 3, 4}, .body_area = {5, 6, 7, 8}, .footer_area = {9, 10, 11, 12},
        .progress_area = {13, 14, 15, 16}, .tools_area = {17, 18, 19, 20},
        .screen_area = {0, 0, 684, 1216}, .water_dir = E0470_TURN_BTT,
        .reading = true, .reader_body = true, .prior_reader = true, .turn_pending = true,
    };
}
static void expect(size_t i, enum call_kind kind, const EpdWaveform *wave, int mode, EpdRect area) {
    assert(i < count);
    assert(calls[i].kind == kind && calls[i].wave == wave && calls[i].mode == mode);
    assert(memcmp(&calls[i].area, &area, sizeof(area)) == 0);
}
int main(void) {
    reader_present_request_t r = reset();
    reader_present_result_t out = reader_present_output(&hl, &r);
    assert(out.error == 0 && !out.tools_band && !out.footer_updated && count == 1);
    expect(0, DIFF, &text, MODE_GL16, r.area);
    r = reset(); r.fullscreen = true;
    reader_present_output(&hl, &r);
    assert(count == 2); expect(1, DIFF, &E0470_WAVEFORM, MODE_GL16, r.progress_area);
    r = reset(); r.turn_pending = false; r.fullscreen = true;
    reader_present_output(&hl, &r); expect(0, DIFF, &page, MODE_GL16, r.area);
    r = reset(); r.turn_pending = false;
    reader_present_output(&hl, &r); expect(0, AREA, &page, MODE_GL16, r.area);
    r = reset(); r.mode = MODE_DU; r.fullscreen = true;
    reader_present_output(&hl, &r); expect(0, AREA, &page, MODE_DU, r.area);

    // 正文失败仍更新普通页脚，并按位合并错误；全屏进度在失败时跳过。
    // A failed body still updates a normal footer and ORs errors, but skips full-screen progress.
    r = reset(); r.footer_pending = true; r.fullscreen = true;
    errors[0] = EPD_DRAW_EMPTY_LINE_QUEUE; errors[1] = EPD_DRAW_OTHER_ERROR;
    out = reader_present_output(&hl, &r);
    assert(count == 2 && out.footer_updated && out.error == 3);
    expect(1, AREA, &E0470_FOLLOW_WAVEFORM, MODE_DU, r.footer_area);
    r = reset(); r.footer_pending = r.footer_strong = true;
    reader_present_output(&hl, &r); expect(1, DIFF, &E0470_WAVEFORM, MODE_GL16, r.footer_area);

    // 水波纹在成功与失败时都先恢复节拍，再进入页脚。
    // Restore ripple pacing before footer output on both success and failure.
    for (int fail = 0; fail < 2; ++fail) {
        r = reset(); r.water_turn_pending = r.footer_pending = true;
        errors[0] = fail ? EPD_DRAW_EMPTY_LINE_QUEUE : EPD_DRAW_SUCCESS;
        out = reader_present_output(&hl, &r);
        assert(count == 4 && tick == 13579 && out.error == errors[0]);
        assert(calls[0].kind == TICK && calls[0].value == E0470_TURN_FAST_TICK_US);
        expect(1, WATER, NULL, E0470_TURN_BTT, r.area);
        assert(calls[1].value == E0470_TURN_FAST_TICK_US);
        assert(calls[2].kind == TICK && calls[2].value == 13579 && calls[3].value == 13579);
    }

    // 插图灰阶失败短路后续输出；两种效果分别选完整灰阶与插图差分。
    // Image failures short-circuit later outputs; effects select full gray or image differential.
    for (int water = 0; water < 2; ++water) for (int failure = -1; failure < 3; ++failure) {
        r = reset(); r.image_gray_refresh = r.image_page = r.fullscreen = r.footer_pending = true;
        r.water_effect = water;
        if (failure >= 0) errors[failure] = EPD_DRAW_OTHER_ERROR;
        out = reader_present_output(&hl, &r);
        assert(count == (failure < 0 ? 3u : (size_t)failure + 1));
        assert(out.error == (failure < 0 ? 0 : EPD_DRAW_OTHER_ERROR) && !out.footer_updated);
        expect(0, water ? DIFF : AREA, water ? &image : &E0470_FULL_WAVEFORM,
               water ? MODE_GL16 : MODE_GC16, r.body_area);
        if (count > 1) expect(1, DIFF, &E0470_WAVEFORM, MODE_GL16, r.footer_area);
        if (count > 2) expect(2, DIFF, &E0470_WAVEFORM, MODE_GL16, r.progress_area);
    }
    r = reset(); r.image_gray_refresh = r.image_page = r.water_effect = true; r.prior_reader = false;
    reader_present_output(&hl, &r); assert(count == 1); expect(0, WHOLE_DIFF, &image, MODE_GL16, none);
    r.cleanup = true; count = outputs = 0;
    reader_present_output(&hl, &r); expect(0, WHOLE, &E0470_FULL_WAVEFORM, MODE_GC16, none);
    r.cleanup = false; r.refresh = READER_PRESENT_FULL; count = outputs = 0;
    reader_present_output(&hl, &r); expect(0, WHOLE, &E0470_FULL_WAVEFORM, MODE_GC16, none);
    r = reset(); r.cleanup = r.tools_toggle = true; r.refresh = READER_PRESENT_PAGE;
    reader_present_output(&hl, &r); assert(count == 1); expect(0, FULL_AREA, &E0470_FULL_WAVEFORM, MODE_GC16, r.screen_area);
    r.refresh = READER_PRESENT_FULL; count = outputs = 0;
    reader_present_output(&hl, &r); expect(0, FULL, NULL, 0, none);

    r = reset(); r.tools_toggle = true; r.refresh = READER_PRESENT_PAGE;
    out = reader_present_output(&hl, &r);
    assert(count == 1 && out.tools_band); expect(0, DIFF, &page, MODE_GL16, r.tools_area);
    for (int gray = 0; gray < 2; ++gray) {
        r = reset(); r.refresh = READER_PRESENT_PAGE; r.hold_gray = gray;
        r.tools_toggle = r.closing_tools_to_text = r.closing_panel_to_text = true;
        out = reader_present_output(&hl, &r);
        assert(count == 1 && !out.tools_band);
        expect(0, FULL_AREA, gray ? &normalize_gray : &normalize, MODE_GL16, r.screen_area);
    }
    r = reset(); r.refresh = READER_PRESENT_PAGE; r.image_page = true;
    reader_present_output(&hl, &r); expect(0, WHOLE_DIFF, &image, MODE_DU, none);
    r = reset(); r.refresh = READER_PRESENT_PAGE;
    reader_present_output(&hl, &r); expect(0, WHOLE_DIFF, &page, MODE_DU, none);
    r.reading = false; count = outputs = 0;
    reader_present_output(&hl, &r); expect(0, FULL, NULL, 0, none);
    count = outputs = 0; errors[0] = EPD_DRAW_OTHER_ERROR;
    assert(reader_present_output(&hl, &r).error == EPD_DRAW_OTHER_ERROR);
    count = outputs = 0; errors[0] = EPD_DRAW_SUCCESS; r.prior_reader = false;
    reader_present_output(&hl, &r); expect(0, FAST, NULL, 0, none);
    for (int du = 0; du < 2; ++du) {
        r = reset(); r.reading = false; r.feedback = true; r.mode = du ? MODE_DU : MODE_GL16;
        r.water_turn_pending = r.fullscreen = r.footer_pending = true;
        reader_present_output(&hl, &r);
        assert(count == 1 && tick == 13579);
        expect(0, DIFF, du ? &E0470_FOLLOW_WAVEFORM : &E0470_WAVEFORM, r.mode, r.area);
    }
    assert(front[0] == 1 && back[0] == 4);
    puts("reader present routing: PASS");
    return 0;
}
