/*
 * 中文：便签本书架的纸张、行布局和文字裁切，不读取书籍或封面。
 * English: Notepad paper, row layout, and text fitting; does not read books or artwork.
 */
#include "ui_shelf_notepad.h"
#include "ui_locale.h"
#include "assets/shelf_notepad_roll.h"
#include "ui_image_dither.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PAPER_X = 36, PAPER_W = 612, ROW_H = 68, ROW_FIRST = 306, SELECT_FIRST = 312,
       TITLE_X = 52, TITLE_GAP = 18, AUTHOR_X = 390, PROGRESS_X = 628, AUTHOR_END = 543, ROLL_Y = 174,
       PAPER_TOP = ROLL_Y + NOTEPAD_ROLL_H, SELECT_PAPER_TOP = 272, PAPER_CUT = 2 };

EpdRect ui_notepad_row(int row, bool selecting) {
    return (EpdRect){PAPER_X, (selecting ? SELECT_FIRST : ROW_FIRST) + row * ROW_H, PAPER_W, ROW_H};
}

static void paint_roll(uint8_t *fb, EpdRect box, bool fast) {
    // 高光保白，加深卷身以及背光侧。/ Keep highlights white while deepening the curl face and shaded side.
    static const uint8_t tone[] = {0, 0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15};
    for (int y = 0; y < box.height; ++y) {
        int src_y = y * NOTEPAD_ROLL_H / box.height;
        int at = 0;
        for (unsigned j = notepad_roll_rows[src_y]; j < notepad_roll_rows[src_y + 1]; ++j) {
            const uint16_t span = notepad_roll_spans[j];
            const int next = at + (span >> 4);
            const uint8_t gray = tone[span & 15u] * 17u;
            const int x0 = at * box.width / NOTEPAD_ROLL_W, x1 = next * box.width / NOTEPAD_ROLL_W;
            if (x1 > x0 && gray != UI_GRAY_WHITE) {
                if (fast) {
                    // 仅卷边图像做点阵，避免快刷二值化抹掉光影。/ Dither the curl so fast-mode binarization retains its shading.
                    for (int x = box.x + x0; x < box.x + x1; ++x)
                        if (ui_image_dither_bw(gray, x, box.y + y) == UI_GRAY_BLACK)
                            epd_draw_pixel(x, box.y + y, UI_GRAY_BLACK, fb);
                } else ui_fill_rect_fast((EpdRect){box.x + x0, box.y + y, x1 - x0, 1}, gray, fb);
            }
            at = next;
        }
    }
}

static void paper_outline(uint8_t *fb, EpdRect box, int layers, bool fast) {
    const uint8_t edge = fast ? 0x50 : 0x60, stack = fast ? UI_GRAY_WHITE : 0xb0;
    for (int i = layers; i >= 0; --i) {
        const EpdRect r = {box.x + i * 2, box.y + i, box.width, box.height};
        const int left_cut = i ? 1 : PAPER_CUT;
        ui_fill_rect_fast(r, i ? stack : UI_GRAY_WHITE, fb);
        ui_fill_rect_fast((EpdRect){r.x, r.y, left_cut, r.height}, edge, fb);
        ui_fill_rect_fast((EpdRect){r.x + r.width - 1, r.y, 1, r.height}, edge, fb);
        ui_hairline(fb, r.y + r.height - 1, r.x, r.width, edge);
        if (!i) ui_hairline(fb, r.y + r.height - 2, r.x, r.width, edge);
    }
}

void ui_notepad_paper(uint8_t *fb, bool selecting, bool fast) {
    const int y = selecting ? SELECT_PAPER_TOP : PAPER_TOP;
    const int end = ui_notepad_row(UI_NOTEPAD_ROWS, selecting).y;
    paper_outline(fb, (EpdRect){PAPER_X, y, PAPER_W, end - y + 4}, selecting ? 0 : 2, fast);
    if (!selecting)
        paint_roll(fb, (EpdRect){PAPER_X - 12, ROLL_Y, NOTEPAD_ROLL_W, NOTEPAD_ROLL_H}, fast);
    ui_notepad_line(fb, -1, selecting, false, false, fast, ui_tr("书名"), ui_tr("作者"), 0);
}

static void cell_text(uint8_t *fb, int x, int cy, int size, int width, const char *value, uint8_t ink) {
    char text[256];
    // 先保留完整 UTF-8 字符，再为省略号腾出位置。/ Preserve complete UTF-8 characters and reserve room for the ellipsis.
    size_t len = strlen(value);
    const bool clipped = len >= sizeof(text) - sizeof("…");
    if (clipped) {
        len = sizeof(text) - sizeof("…");
        while (len && ((unsigned char)value[len] & 0xc0) == 0x80) --len;
    }
    memcpy(text, value, len);
    text[len] = '\0';
    if (clipped || ui_text_fixed_width_px(size, text) > width) {
        do {
            memcpy(text + len, "…", sizeof("…"));
            if (ui_text_fixed_width_px(size, text) <= width || !len) break;
            do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
        } while (true);
    }
    ui_text_fixed_ink_vc(fb, x, cy, size, text, EPD_DRAW_ALIGN_LEFT, ink);
}

void ui_notepad_line(uint8_t *fb, int row, bool selecting, bool selected, bool pressed, bool fast,
                     const char *title, const char *author, unsigned progress) {
    const bool header = row < 0;
    EpdRect r = ui_notepad_row(header ? 0 : row, selecting);
    if (header) {
        const int top = selecting ? SELECT_PAPER_TOP : PAPER_TOP;
        r.height = r.y - top;
        r.y = top;
    }
    const int cy = r.y + r.height / 2;
    if (!header) ui_fill_rect_fast((EpdRect){r.x + PAPER_CUT, r.y, r.width - 2 * PAPER_CUT, r.height},
                                   pressed && !fast ? 0xd0 : UI_GRAY_WHITE, fb);
    if (!header && pressed && fast)
        ui_fill_rect_fast((EpdRect){r.x + 3, r.y + 18, 4, r.height - 36}, UI_GRAY_BLACK, fb);
    if (header || row < UI_NOTEPAD_ROWS - 1) {
        const int y = r.y + r.height - 1;
        for (int x = r.x + 11; x + 9 < r.x + r.width - 11; x += 12) {
            ui_hairline(fb, y, x, 9, fast ? 0x50 : header ? 0x40 : 0x70);
            if (header) ui_hairline(fb, y - 1, x, 9, fast ? 0x50 : 0x90);
        }
    }
    int title_size = ui_text_effective_px(header ? 21 : 28);
    int meta_size = ui_text_effective_px(header ? 21 : 22);
    if (title_size > (header ? 29 : 37)) title_size = header ? 29 : 37;
    if (meta_size > 30) meta_size = 30;
    const int title_x = selecting ? 94 : TITLE_X;
    const uint8_t ink = header ? 0x40 : UI_GRAY_BLACK;
    cell_text(fb, title_x, cy, title_size, AUTHOR_X - title_x - TITLE_GAP, title, ink);
    cell_text(fb, AUTHOR_X, cy, meta_size, AUTHOR_END - AUTHOR_X, author, 0x40);
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", progress > 100 ? 100 : progress);
    const char *progress_text = header ? "进度" : pct;
    int progress_size = meta_size;
    while (progress_size > 12 && ui_text_fixed_width_px(progress_size, progress_text) > PROGRESS_X - AUTHOR_END - 8)
        --progress_size;
    ui_text_fixed_ink_vc(fb, PROGRESS_X, cy, progress_size, progress_text,
                         EPD_DRAW_ALIGN_RIGHT, header ? 0x40 : UI_GRAY_BLACK);
    if (selecting && !header) {
        ui_draw_control_frame(fb, (EpdRect){48, cy - 15, 30, 30}, 4, UI_GRAY_BLACK);
        if (selected) ui_draw_icon(fb, 63, cy, 25, UI_ICON_CHECK, UI_GRAY_BLACK);
    }
}

EpdRect ui_notepad_title_ink(const uint8_t *fb, int row) {
    if (!fb || row < 0 || row >= UI_NOTEPAD_ROWS) return (EpdRect){0};
    const EpdRect r = ui_notepad_row(row, false);
    const int end = AUTHOR_X - TITLE_GAP + 4;
    int left = end, right = -1, top = r.y + r.height, bottom = r.y;
    // 按实际可见的字形定位书名中心，不用字宽猜测省略号和侧承。
    // Center on the painted glyphs instead of guessing the ellipsis and side bearings from text advances.
    for (int y = r.y + 2; y < r.y + r.height - 2; ++y) {
        for (int x = TITLE_X - 4; x < end; ++x) {
            if (ui_read_pixel(fb, x, y) == (UI_GRAY_WHITE >> 4)) continue;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
    }
    return left < end ? (EpdRect){left, top, right - left + 1, bottom - top + 1} : (EpdRect){0};
}

typedef struct { int x, y; } ink_point_t;
typedef struct {
    uint8_t *fb;
    EpdRect clip;
    int left, top, right, bottom;
} ink_stroke_t;

static void ink_stamp(ink_stroke_t *s, ink_point_t p, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy > radius * radius) continue;
        const int x = p.x + dx, y = p.y + dy;
        if (x < s->clip.x || x >= s->clip.x + s->clip.width || y < s->clip.y || y >= s->clip.y + s->clip.height)
            continue;
        // 只在未写字的纸底添墨，不覆盖已有字形。/ Add ink only on bare paper; leave existing glyph pixels intact.
        if (ui_read_pixel(s->fb, x, y) != (UI_GRAY_WHITE >> 4)) continue;
        epd_draw_pixel(x, y, UI_GRAY_BLACK, s->fb);
        if (x < s->left) s->left = x;
        if (x > s->right) s->right = x;
        if (y < s->top) s->top = y;
        if (y > s->bottom) s->bottom = y;
    }
}

static void ink_curve(ink_stroke_t *s, const ink_point_t p[4], int radius) {
    enum { STEPS = 16, DIV = STEPS * STEPS * STEPS };
    ink_point_t from = p[0];
    ink_stamp(s, from, radius);
    for (int i = 1; i <= STEPS; ++i) {
        const int t = STEPS - i;
        const int a = t * t * t, b = 3 * t * t * i, c = 3 * t * i * i, d = i * i * i;
        const ink_point_t to = {(a*p[0].x + b*p[1].x + c*p[2].x + d*p[3].x + DIV/2) / DIV,
                                (a*p[0].y + b*p[1].y + c*p[2].y + d*p[3].y + DIV/2) / DIV};
        const int dx = to.x - from.x, dy = to.y - from.y;
        const int w = abs(dx), h = abs(dy), n = w > h ? w : h;
        for (int j = 1; j <= n; ++j) ink_stamp(s, (ink_point_t){from.x + dx*j/n, from.y + dy*j/n}, radius);
        from = to;
    }
}

EpdRect ui_notepad_ink_step(uint8_t *fb, int row, EpdRect title, unsigned step) {
    if (!fb || row < 0 || row >= UI_NOTEPAD_ROWS || title.width <= 0 || title.height <= 0 ||
        step >= UI_NOTEPAD_INK_STEPS) return (EpdRect){0};
    const EpdRect r = ui_notepad_row(row, false);
    const int min_x = r.x + 3, max_x = AUTHOR_X - 5, min_y = r.y + 3, max_y = r.y + r.height - 4;
    int radius_y = title.height / 2 + 9;
    if (radius_y < 20) radius_y = 20;
    if (radius_y > 26) radius_y = 26;
    const int radius_x = 2 * radius_y;
    int cx = title.x + title.width / 2, cy = title.y + title.height / 2;
    if (cx < min_x + radius_x + 1) cx = min_x + radius_x + 1;
    if (cx > max_x - radius_x - 1) cx = max_x - radius_x - 1;
    if (cy < min_y + radius_y + 2) cy = min_y + radius_y + 2;
    if (cy > max_y - radius_y - 2) cy = max_y - radius_y - 2;
    // 只把笔迹横向拉宽一倍，保留原高度与笔尖。/ Stretch the path horizontally only, retaining its height and pen tip.
    static const ink_point_t curves[][4] = {
        {{13,-14}, {4,-25}, {-20,-22}, {-20,-1}},
        {{-20,-1}, {-21,13}, {-10,21}, {2,20}},
        {{2,20}, {23,19}, {26,-8}, {14,-16}},
        {{14,-16}, {11,-18}, {8,-17}, {6,-14}},
    };
    static const unsigned stops[] = {0, 1, 2, 4};
    ink_stroke_t stroke = {.fb = fb, .clip = {min_x, min_y, max_x - min_x + 1, max_y - min_y + 1},
                           .left = max_x, .top = max_y, .right = -1, .bottom = -1};
    for (unsigned i = stops[step]; i < stops[step + 1]; ++i) {
        ink_point_t placed[4];
        for (unsigned j = 0; j < 4; ++j) {
            placed[j] = (ink_point_t){cx + curves[i][j].x * radius_x / 20,
                                      cy + curves[i][j].y * radius_y / 20};
        }
        ink_curve(&stroke, placed, 1);
    }
    if (stroke.right < 0) return (EpdRect){0};
    return (EpdRect){stroke.left, stroke.top, stroke.right - stroke.left + 1, stroke.bottom - stroke.top + 1};
}

void ui_notepad_thumbnail(uint8_t *fb, EpdRect bounds) {
    const int h = bounds.width * NOTEPAD_ROLL_H / NOTEPAD_ROLL_W;
    paper_outline(fb, (EpdRect){bounds.x + 4, bounds.y + h, bounds.width - 8, bounds.height - h - 2}, 1, false);
    paint_roll(fb, (EpdRect){bounds.x, bounds.y, bounds.width, h}, false);
    const int step = (bounds.height - h - 8) / 5;
    for (int i = 0; i < 5; ++i) {
        const int y = bounds.y + h + 4 + i * step;
        ui_hairline(fb, y, bounds.x + 15, bounds.width * 41 / 100 - i * 5, 0x30);
        ui_hairline(fb, y, bounds.x + bounds.width * 58 / 100, bounds.width * 17 / 100, 0x60);
        ui_hairline(fb, y, bounds.x + bounds.width * 82 / 100, bounds.width * 10 / 100, 0x40);
    }
}
