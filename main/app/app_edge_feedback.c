/* 中文：在抬手返回时从起滑边缘拉出一段浅弧，内侧折角朝向屏幕中央，不跟手刷新、不保存整页。
 * English: Pull a shallow, ink-outlined curve with an inward chevron from the origin edge on committed Back, without tracking scans or full-page storage.
 */
#include "app_edge_feedback.h"
#include "display.h"
#include "display_refresh.h"
#include "ui_kit.h"
#include <stdlib.h>

// 图形尺寸独立于触摸热区；局刷宽度向上取整到 8 像素。/ Visual dimensions are independent of the hit zone; round the refresh width up to eight pixels.
enum {
    HALF_H = 68, EDGE_H = 2 * HALF_H + 1,
    OPEN = 29, EXTENDED = 42, EDGE_W = (EXTENDED + 7) / 8 * 8,
    OPEN_WING = 7, EXTENDED_WING = 9, RIM = 3
};

// 用互补的平方权重让两端贴边、弧顶舒展，同时保持上下对称。/ Complementary squared weights taper the roots and ease into a broad, symmetric crest.
static int reach_at(int row, int depth) {
    const int d = abs(row - HALF_H), t = HALF_H - d;
    const int weight = t * t, scale = weight + d * d;
    return (depth * weight + scale / 2) / scale;
}

static void draw_tab(uint8_t *fb, EpdRect area, bool right, int depth) {
    const int wing = depth == OPEN ? OPEN_WING : EXTENDED_WING, tip = (depth + wing - 1) / 2;
    for (int row = 0; row < EDGE_H; ++row) {
        const int reach = reach_at(row, depth), dy = abs(row - HALF_H);
        if (!reach) continue;
        // 陡坡多包一行的墨线，斜段不会比瓣顶细。/ Include the neighboring row on the slope to keep the outline optically even.
        const int ink_from = reach_at(row < HALF_H ? row - 1 : row + 1, depth) - RIM;
        for (int col = 0; col < reach; ++col) {
            // 白色纸面衬出墨线；不画沿整个屏边的直线，窄尾直接接入边框。
            // White fill isolates the ink; the narrow tails meet the bezel without a vertical rail.
            bool outline = col >= ink_from;
            const int offset = col + dy - tip;
            bool chevron = (dy <= wing && offset >= -2 && offset <= 1) ||
                           (dy == wing + 1 && (offset == 0 || offset == 1));
            int x = right ? area.width - 1 - col : col;
            epd_draw_pixel(area.x + x, area.y + row, outline || chevron ? 0x00 : 0xff, fb);
        }
    }
}

void app_edge_feedback_show(app_ctx_t *ctx, bool right, int origin_y) {
    int w = epd_rotated_display_width(), h = epd_rotated_display_height();
    if (!ctx->fb || !ctx->hl || w < EDGE_W || h < EDGE_H) return;
    int y = origin_y - HALF_H;
    EpdRect area = {right ? w - EDGE_W : 0, y < 0 ? 0 : y > h - EDGE_H ? h - EDGE_H : y, EDGE_W, EDGE_H};
    bool held = display_present_begin();
    if (display_local_feedback_ready()) {
        // 只存展开轮廓内的像素对。半瓣内互补行宽之和为 EXTENDED；每行再留一字节给取整。
        // Save pairs only within the extended contour. Complementary half-lobe widths sum to EXTENDED; allow one byte per row for rounding.
        uint8_t saved[(HALF_H + 1) * (EXTENDED / 2) + EDGE_H];
        unsigned n = 0;
        for (int row = 0; row < EDGE_H; ++row) {
            int reach = reach_at(row, EXTENDED);
            for (int col = 0; col < reach; col += 2) {
                int x = right ? w - 1 - col : col, step = right ? -1 : 1;
                saved[n++] = ui_read_pixel(ctx->fb, x, area.y + row) |
                             (ui_read_pixel(ctx->fb, x + step, area.y + row) << 4);
            }
        }
        draw_tab(ctx->fb, area, right, OPEN);
        enum EpdDrawError result = display_refresh_area(ctx->hl, DISPLAY_REFRESH_FEEDBACK, area, DISPLAY_REFRESH_DEFAULT);
        if (result == EPD_DRAW_SUCCESS) {
            draw_tab(ctx->fb, area, right, EXTENDED);
            result = display_refresh_area(ctx->hl, DISPLAY_REFRESH_FEEDBACK, area, DISPLAY_REFRESH_DEFAULT);
        }
        n = 0;
        for (int row = 0; row < EDGE_H; ++row) {
            int reach = reach_at(row, EXTENDED);
            for (int col = 0; col < reach; col += 2) {
                int x = right ? w - 1 - col : col, step = right ? -1 : 1;
                uint8_t pair = saved[n++];
                epd_draw_pixel(x, area.y + row, (uint8_t)(pair << 4), ctx->fb);
                epd_draw_pixel(x + step, area.y + row, pair & 0xf0, ctx->fb);
            }
        }
        // 按原灰阶撤掉箭头；失败时让导航目标修复基准，不重放图标。/ Restore original grays; on failure repair at navigation, never replay the badge.
        if (result == EPD_DRAW_SUCCESS)
            result = display_refresh_area(ctx->hl, DISPLAY_REFRESH_DETAIL, area, DISPLAY_REFRESH_DEFAULT);
        if (result != EPD_DRAW_SUCCESS) {
            guard_draw_result(ctx->hl, result);
            display_request_full_refresh();
        }
    }
    display_present_end(held);
}
