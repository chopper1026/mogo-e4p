/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：阅读同步刷新路由；稳定正文合并栏位，保留分步回退和水波纹节拍恢复。
 * English: Synchronous reader routing joins stable body and chrome output, retaining split fallback and restoring ripple pacing.
 */
#include "reader_present.h"
#include "display.h"
#include "display_refresh.h"

reader_present_result_t reader_present_output(EpdiyHighlevelState *hl,
                                               const reader_present_request_t *request) {
    reader_present_result_t result = {0};
    enum EpdDrawError err;
    bool joined = false;
    bool held = display_present_begin();
    // 含插图的一帧不用背景派生表：那张表把深色对的前导擦白剥掉了，旧字（from=0）落到图片灰
    // 时从黑轨直接进灰尾，落点和纸灰起点不一致，差分会把上一页的字形留在图上。纯文字帧仍走
    // 背景派生表。插图表还保留相等对保持，脏区膨胀带上的未变化像素不会被黑白往返推出一圈细线。
    // Frames holding an illustration skip the background derivative: it strips the dark pairs'
    // leading erase, so old glyphs (from=0) settle on an image gray from the black rail instead
    // of the white rail, land differently from paper-origin pixels and keep their shape. Text
    // frames keep the derivative. The illustration table also holds equal pairs, so the
    // unchanged pixels that dilation brings along are not pushed through a white-black trip
    // that draws a thin outline.
    const unsigned frame_flags = request->image_page ? DISPLAY_REFRESH_IMAGE : DISPLAY_REFRESH_DEFAULT;
    // 横屏仅用普通翻页，不改保存的效果；忽略换向前排队的动画。
    // Landscape uses ordinary turns without changing the saved effect; ignore a queued pre-rotation animation.
    const bool portrait = request->screen_area.width <= request->screen_area.height;
    const bool water_effect = portrait && request->water_effect;
    const bool water_turn = portrait && request->water_turn_pending;
    if (!request->reading && request->prior_reader &&
        (request->refresh == READER_PRESENT_PAGE || request->refresh == READER_PRESENT_FULL)) {
        // 同页退出正文回书架也请求完整48相，清掉阅读文字残影。
        // Request complete 48-phase cleanup for body-to-shelf returns within this page.
        display_request_full_refresh();
        err = update_display_full(hl);
    }
    else if (request->closing_notice && request->reader_body && request->refresh != READER_PRESENT_FULL && !request->cleanup) {
        // 提示覆盖与未覆盖区域的驱动历史不同；收起只做一次整屏归一，插图和背景保留灰阶。
        // Covered and uncovered regions have different drive histories; normalize once on dismissal while holding illustration/background grays.
        err = display_refresh_area(hl, DISPLAY_REFRESH_RESTORE, request->screen_area,
            request->hold_gray || request->image_page ? DISPLAY_REFRESH_HOLD_GRAY : DISPLAY_REFRESH_DEFAULT);
    }
    else if (request->comic_page && request->image_gray_refresh && request->prior_reader &&
             request->refresh != READER_PRESENT_FULL) {
        err = request->turn_pending && !request->cleanup
            ? display_refresh_area(hl, DISPLAY_REFRESH_READER_COMIC, request->body_area, DISPLAY_REFRESH_DEFAULT)
            : display_refresh_area(hl, DISPLAY_REFRESH_READER_CLEANUP, request->screen_area, DISPLAY_REFRESH_DEFAULT);
        joined = true;
        if (err == EPD_DRAW_SUCCESS) {
            result.status_updated = request->status_pending;
            result.footer_updated = request->footer_pending;
        } else {
            // 扫描失败后物理画面可能停在中间灰阶，下次输出须重建完整基准。
            // An interrupted scan can leave intermediate grays. Require a full refresh on the next output.
            display_request_full_refresh();
        }
    }
    else if (request->image_gray_refresh && request->refresh == READER_PRESENT_AREA && !request->cleanup && request->prior_reader) {
        // 灰阶只驱动正文，避免每张插图都把整屏先清白再闪回。
        // Drive the grayscale body from the current frame instead of clearing the whole screen to white.
        err = water_effect
            ? display_refresh_area(hl, DISPLAY_REFRESH_READER_BODY, request->body_area, frame_flags)
            : display_refresh_area(hl, DISPLAY_REFRESH_READER_IMAGE, request->body_area, DISPLAY_REFRESH_DEFAULT);
    }
    else if (request->image_gray_refresh && request->refresh == READER_PRESENT_FULL)
        // 开书首帧虽含插图仍是明确的整屏请求：先把物理屏和 back 基准归到白色，再用完整
        // 48 相灰阶表提交目标。若走图片 AUTO 差分，未变化的首页横线不会进入 dirty map。
        // An illustration on an explicitly full first frame still needs a white-baseline commit:
        // the image AUTO differential leaves unchanged home-page rows out of the dirty map.
        err = update_display_reader_image_gray(hl);
    else if (request->image_gray_refresh) err = water_effect &&
        request->refresh != READER_PRESENT_FULL && !request->cleanup
        // 换章那一帧走整屏差分，表的选择与正文带差分一致。
        // The chapter-change frame diffs the whole screen and takes the same table.
        ? display_refresh_screen(hl, DISPLAY_REFRESH_READER_BODY, frame_flags)
        : display_refresh_screen(hl, DISPLAY_REFRESH_READER_IMAGE, DISPLAY_REFRESH_DEFAULT);
    else if (request->cleanup && request->refresh != READER_PRESENT_FULL)
        // 周期清理使用完整原厂灰阶定稿，保留饱和驱动；一次输出，不叠加擦白或文字补刷。
        // Periodic cleanup retains the complete vendor settling/saturation phases in one output, without erase or text replay.
        err = display_refresh_area(hl, DISPLAY_REFRESH_READER_CLEANUP, request->screen_area, DISPLAY_REFRESH_DEFAULT);
    else if (request->refresh == READER_PRESENT_FULL) err = update_display_full(hl);
    // 工具栏开关只改底部一条带：GL16 差分只驱动带内变化像素，脏区膨胀补推进度条边缘的
    // 白色邻行/邻列，实心进度条擦除后不再留轮廓。收起帧例外：被工具栏盖过的字多走了
    // 一轮擦白推黑，带内差分追不平驱动历史，纯文字页放行到下方的整屏归一分支。
    // A tools toggle changes only the bottom band: a GL16 differential drives the changed
    // pixels inside it, and dirty-mask dilation re-drives the white neighbours around the
    // solid progress bar so erasing it leaves no outline. The closing frame is the
    // exception: the covered glyphs ran one extra erase-then-black cycle that a band
    // differential cannot match, so pure-text pages fall through to the whole-screen
    // normalize branch below.
    else if (request->tools_toggle && request->refresh == READER_PRESENT_PAGE && request->reader_body &&
             request->prior_reader &&
             !(request->closing_tools_to_text)) {
        result.tools_band = true;
        err = display_refresh_area(hl, DISPLAY_REFRESH_READER_BODY, request->tools_area, DISPLAY_REFRESH_DEFAULT);
    }
    else if (request->refresh == READER_PRESENT_AREA && request->feedback) {
        // 抽出与复位只驱动发生变化的像素，避免整张封面被反复压黑。
        // Lift and restore only changed pixels, avoiding a dark pulse over the whole cover.
        err = display_refresh_area(hl, request->mode == MODE_DU ? DISPLAY_REFRESH_FEEDBACK : DISPLAY_REFRESH_DETAIL, request->area, DISPLAY_REFRESH_DEFAULT);
    }
    else if (request->refresh == READER_PRESENT_AREA) {
        // 快档每次显式设置并在推屏后恢复，不能把阅读节拍带入主页水波纹。
        // Explicitly select fast pacing per turn and restore afterward, without leaking reader timing into main-page ripple.
        const int prior_tick = water_turn ? e0470_page_turn_tick_us() : 0;
        if (water_turn) e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
        // 栏位已经画到 front；只在本来就是稳定正文局刷时合成，未开扫时可以回退。
        // Chrome is already in front; join only existing stable body turns with a pre-scan fallback.
        const EpdRect body = request->body_area, area = request->area;
        if (request->reader_body && request->prior_reader && !request->cleanup && !request->image_page &&
            !request->closing_notice && request->mode == MODE_GL16 && (water_turn || request->turn_pending) &&
            (request->status_pending || request->footer_pending) && area.x == body.x && area.y == body.y &&
            area.width == body.width && area.height == body.height) {
            joined = update_display_reader_joined(hl, body,
                request->status_pending ? request->status_area : (EpdRect){0},
                request->footer_pending ? request->footer_area : (EpdRect){0},
                water_turn, request->water_dir, &err);
            if (joined && err == EPD_DRAW_SUCCESS) {
                result.footer_updated = request->footer_pending;
                result.status_updated = request->status_pending;
            }
        }
        if (!joined) {
            // 翻页动画独立于页脚显隐。/ The turn effect is independent of footer visibility.
            err = water_turn
                ? update_display_water_turn(hl, request->area, request->water_dir)
                : request->turn_pending && request->mode == MODE_GL16
                    ? display_refresh_area(hl, DISPLAY_REFRESH_READER_TEXT, request->area, DISPLAY_REFRESH_DEFAULT)
                    : display_refresh_area(hl, DISPLAY_REFRESH_READER_LAYOUT, request->area,
                        request->mode == MODE_DU ? DISPLAY_REFRESH_QUICK : DISPLAY_REFRESH_DEFAULT);
        }
        if (water_turn) e0470_page_turn_set_tick_us(prior_tick);

    }
    // 关闭阅读设置等面板时，被白底盖过的字已经多走了一轮「擦白 → 推黑」，比没被盖过的
    // 字更黑；单次还原追不平。这里整屏再走一遍归一表，全页字芯落在同一墨度。
    // Closing a sheet leaves the text it covered one erase-then-black cycle ahead of the rest,
    // which no single restore can match. One whole-screen normalize pass puts every glyph core
    // on the same ink.
    else if (request->refresh == READER_PRESENT_PAGE && request->reader_body &&
             request->closing_panel_to_text) {
        err = display_refresh_area(hl, DISPLAY_REFRESH_RESTORE, request->screen_area, request->hold_gray ? DISPLAY_REFRESH_HOLD_GRAY : DISPLAY_REFRESH_DEFAULT);
    }
    else err = request->reading
        ? display_refresh_screen(hl, DISPLAY_REFRESH_READER_BODY, frame_flags |
            (request->page_mode == MODE_DU ? DISPLAY_REFRESH_QUICK : DISPLAY_REFRESH_DEFAULT))
        : update_display_fast_page(hl);
    // 仅分步出口在正文成功后继续；合并成功后不得重复推栏位。
    // Only the split path continues after body success; never present joined bars a second time.
    if (!joined && err == EPD_DRAW_SUCCESS && request->refresh == READER_PRESENT_AREA &&
        request->reader_body && request->prior_reader && !request->cleanup && !request->feedback && !request->closing_notice) {
        if (request->footer_pending) {
            // 恢复旧页脚：普通翻页走短 DU，换章与插图帧走默认 GL16 清理。
            // Restore the legacy footer: short DU for ordinary turns, default GL16 for chapters and illustration frames.
            err = display_refresh_area(hl, request->footer_strong || request->image_gray_refresh
                ? DISPLAY_REFRESH_DETAIL : DISPLAY_REFRESH_READER_FOOTER, request->footer_area, DISPLAY_REFRESH_DEFAULT);
            if (err != EPD_DRAW_SUCCESS) goto done;
            result.footer_updated = true;
        }
        if (request->status_pending) {
            err = display_refresh_area(hl, DISPLAY_REFRESH_DETAIL, request->status_area, DISPLAY_REFRESH_DEFAULT);
            if (err != EPD_DRAW_SUCCESS) goto done;
            result.status_updated = true;
        }
    }
done:
    display_present_end(held);
    result.error = err;
    return result;
}
