/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际阅读波形的驱动边界；光学灰度和残影仍需实屏验证。
 * English: Validate actual reader pulse boundaries; optical tone and ghosting need panel tests.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"

int main(void) {
    e0470_waveform_init();
    const EpdWaveformPhases* old = e0470_waveform_phases(&E0470_TEXTTURN_WAVEFORM, MODE_GL16);
    uint8_t saved[E0470_GL16_FRAMES * 16 * 4];
    assert(old && old->phases == E0470_GL16_FRAMES && !old->phase_times);
    memcpy(saved, old->luts, sizeof(saved));
    const EpdWaveform* wave = display_reader_text_waveform();
    const EpdWaveformPhases* soft = e0470_waveform_phases(wave, MODE_GL16);
    assert(wave != &E0470_TEXTTURN_WAVEFORM && display_reader_text_waveform() == wave);
    assert(wave->temp_intervals == E0470_TEXTTURN_WAVEFORM.temp_intervals);
    assert(soft && soft->phases == old->phases && soft->phase_times == old->phase_times);
    unsigned changed = 0;
    for (int from = 0; from < 16; ++from) for (int to = 0; to < 16; ++to) {
        // 变白列与黑列由各自的检查覆盖，不参与文字软化的逐相对比。
        // The whitening and black columns have their own checks and stay out of the softening comparison.
        if (to == 15 || to == 0) continue;
        int a_count[4] = {0}, b_count[4] = {0}, a_balance = 0, b_balance = 0;
        int a_peak = 0, b_peak = 0, first = -1, last = -1;
        unsigned differences = 0;
        for (int f = 0; f < old->phases; ++f) {
            int a = e0470_phase_action(old, f, to, from), b = e0470_phase_action(soft, f, to, from);
            ++a_count[a]; ++b_count[b];
            if (a != b) ++differences;
            if (from != 15) assert(a == b);
            if (a == 0 || a == 3) assert(b == a);
            if (a) {if (first < 0) first = f; last = f;}
            a_balance += a == 1 ? 1 : a == 2 ? -1 : 0;
            b_balance += b == 1 ? 1 : b == 2 ? -1 : 0;
            if (a_balance > a_peak) a_peak = a_balance;
            if (b_balance > b_peak) b_peak = b_balance;
        }
        assert(!memcmp(a_count, b_count, sizeof(a_count)));
        if (first >= 0) assert(e0470_phase_action(old, first, to, from) == e0470_phase_action(soft, first, to, from));
        if (last >= 0) assert(e0470_phase_action(old, last, to, from) == e0470_phase_action(soft, last, to, from));
        if (differences) {
            assert(from == 15 && to > 0 && to < 15 && differences <= 4);
            assert(b_peak < a_peak && a_peak - b_peak <= 2);
            ++changed;
            printf("15->%d net black pulse peak %d -> %d; phases ", to, a_peak, b_peak);
            for (int f = 0; f < soft->phases; ++f) putchar(".BW?"[e0470_phase_action(soft, f, to, from)]);
            putchar('\n');
        }
    }
    // 普通文字表沿用主页白列：DU 的 18 次擦白 + 6 相尾部补推，白到白保持。
    // The ordinary text table keeps the main-page column: 18 DU erases plus the six
    // tail pushes, holding white-to-white.
    unsigned text_erases = 0;
    for (int f = 0; f < soft->phases; ++f) {
        text_erases += e0470_phase_action(soft, f, 15, 0) == 2;
        assert(e0470_phase_action(soft, f, 15, 15) == 0);
    }
    assert(text_erases == 24);
    assert(changed > 0 && !memcmp(saved, old->luts, sizeof(saved)));
    // 黑列补推：裁表砍掉的 5 次推黑补回原厂 18 次，占用前导保持相，相数与末尾保持不变。
    // Black column: restore the five trim-cut pushes to the vendor's 18 from leading holds,
    // keeping the phase count and the trailing holds.
    const EpdWaveformPhases* full = e0470_waveform_phases(&E0470_FULL_WAVEFORM, MODE_GL16);
    assert(full && full->luts);
    for (int from = 1; from < 16; ++from) {
        int want = 0, got = 0, last = -1, first_black = -1, erase_after_black = 0;
        for (int f = 0; f < full->phases; ++f)
            want += e0470_phase_action(full, f, 0, from) == 1;
        for (int f = 0; f < soft->phases; ++f) {
            const int action = e0470_phase_action(soft, f, 0, from);
            got += action == 1;
            if (action == 1) { if (first_black < 0) first_black = f; last = f; }
            else if (action == 2 && first_black >= 0) ++erase_after_black;
        }
        assert(want == 18 && got == want);
        assert(first_black >= 0 && last <= (int)soft->phases - 4);
        assert(!erase_after_black);
    }
    // 普通纯文字翻页复用这套已验证文字序列：相同灰阶全保持，不能改字形目标或原厂水波纹表。
    // Ordinary text turns share these proven text phases: equal grays hold, retaining glyph targets and the vendor ripple table.
    const EpdWaveformPhases* raw=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);
    unsigned held=0,edge_runs=0;
    for(int to=0;to<16;++to) {
        int old_run=0,new_run=0,old_max=0,new_max=0,old_same=0;
        int raw_counts[4]={0},soft_counts[4]={0};
        for(int f=0;f<raw->phases;++f) {
            int a=e0470_phase_action(raw,f,to,15),b=e0470_phase_action(soft,f,to,15);
            ++raw_counts[a];++soft_counts[b];
            old_run=a==1?old_run+1:0;new_run=b==1?new_run+1:0;
            if(old_run>old_max)old_max=old_run;
            if(new_run>new_max)new_max=new_run;
            old_same+=e0470_phase_action(raw,f,to,to)!=0;
            assert(e0470_phase_action(soft,f,to,to)==0);
        }
        held+=old_same>0;
        if(to>0&&to<15) {
            assert(!memcmp(raw_counts,soft_counts,sizeof(raw_counts)));
            assert(new_max<=old_max);edge_runs+=new_max<old_max;
        }
    }
    assert(held==16&&edge_runs>=10);
    // 水波纹：白列仍是原厂（黑→白 18 次擦白、中灰→白保留前导擦除），
    // 只把 to=0 的黑列补到 18 次，37 相与 8 带 44 拍不变。
    // Water keeps the vendor white column (18 black-to-white erases and a leading erase
    // on gray-to-white) and only restores the 18 pushes for to=0; 37 phases and the
    // eight-band 44-tick turn stay unchanged.
    unsigned erases = 0;
    for (int f = 0; f < raw->phases; ++f)
        erases += e0470_phase_action(raw, f, 15, 0) == 2;
    assert(erases == 18);
    for (int from = 1; from < 15; ++from) {
        int first_white = -1, first_black = -1;
        for (int f = 0; f < raw->phases; ++f) {
            const int action = e0470_phase_action(raw, f, 15, from);
            if (action == 2 && first_white < 0) first_white = f;
            if (action == 1 && first_black < 0) first_black = f;
        }
        assert(first_black >= 0 && first_white > 0 && first_black < first_white);
    }
    const EpdWaveform* water = display_reader_water_waveform();
    const EpdWaveformPhases* ripple = e0470_waveform_phases(water, MODE_GL16);
    assert(water != &E0470_WAVEFORM && display_reader_water_waveform() == water);
    assert(water->temp_intervals == E0470_WAVEFORM.temp_intervals);
    assert(ripple && ripple->luts && ripple->phases == raw->phases && !ripple->phase_times);
    assert(ripple->phases == E0470_GL16_FRAMES);
    for (int f = 0; f < raw->phases; ++f) {
        // 白列逐字节保持原厂，不引入主页的白列尾部补推。
        // The white column stays byte-identical to the vendor table.
        assert(!memcmp(ripple->luts + (size_t)f * 16 * 4 + 15 * 4,
                       raw->luts + (size_t)f * 16 * 4 + 15 * 4, 4));
    }
    for (int to = 1; to < 16; ++to)
        for (int from = 0; from < 16; ++from)
            for (int f = 0; f < raw->phases; ++f)
                assert(e0470_phase_action(ripple, f, to, from) ==
                       e0470_phase_action(raw, f, to, from));
    for (int from = 1; from < 16; ++from) {
        int got = 0;
        for (int f = 0; f < ripple->phases; ++f)
            got += e0470_phase_action(ripple, f, 0, from) == 1;
        assert(got == 18);
    }
    // 整页阅读表同样补黑，白列仍是 24 次擦白。
    // The full-page reader table gets the same black pushes and keeps 24 erases.
    const EpdWaveform* page = display_reader_page_waveform();
    const EpdWaveformPhases* full_page = e0470_waveform_phases(page, MODE_GL16);
    assert(full_page && full_page->phases == E0470_GL16_FRAMES);
    unsigned page_erases = 0;
    for (int f = 0; f < full_page->phases; ++f)
        page_erases += e0470_phase_action(full_page, f, 15, 0) == 2;
    assert(page_erases == 24);
    for (int from = 1; from < 16; ++from) {
        int black = 0;
        for (int f = 0; f < full_page->phases; ++f)
            black += e0470_phase_action(full_page, f, 0, from) == 1;
        assert(black == 18);
    }
    puts("PASS: water turns restore 18 black pushes for to=0 while keeping the vendor white column, 37 phases and the eight-band tick count");
    puts("PASS: ordinary text turns hold all 16 unchanged grays, reduce at least ten gray-edge dark runs and keep the 37 phases");
    puts("PASS: all 256 pairs, holds, first/final actions, phase count and original LUT retained except the to=0 black column; optical validation pending");
    return 0;
}
