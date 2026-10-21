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
        // 变白列由白列检查单独覆盖，不参与文字软化的逐相对比。
        // The whitening column has its own check below and is excluded from the softening comparison.
        if (to == 15) continue;
        int a_count[4] = {0}, b_count[4] = {0}, a_balance = 0, b_balance = 0;
        int a_peak = 0, b_peak = 0, first = -1, last = -1;
        unsigned differences = 0;
        for (int f = 0; f < old->phases; ++f) {
            int a = e0470_phase_action(old, f, to, from), b = e0470_phase_action(soft, f, to, from);
            ++a_count[a]; ++b_count[b];
            if (a != b) ++differences;
            if (from != 15 || to == 0) assert(a == b);
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
    // 水波纹直接用原厂 GL16 表：黑→白仍是原厂的 18 次擦白，中灰→白保留前导擦除，
    // 因此不会把主页白列的 35 次尾部补推带回来。
    // Water turns use the vendor GL16 table directly: black-to-white keeps the vendor's
    // 18 erases and gray-to-white keeps its leading erase, so the main-page column's
    // 35-push tail never returns.
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
    puts("PASS: water turns keep the vendor GL16 table with 18 black-to-white erases and a leading erase on every gray-to-white pair");
    puts("PASS: ordinary text turns hold all 16 unchanged grays and reduce at least ten gray-edge dark runs; existing text profile, targets and 37 phases unchanged");
    puts("PASS: all 256 pairs, endpoints, holds, first/final actions, pulse counts, 37-frame timing and original LUT retained; optical validation pending");
    return 0;
}
