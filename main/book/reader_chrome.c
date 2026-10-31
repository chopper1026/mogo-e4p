/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：页眉页脚三段槽位的合成、排布与绘制。左格贴左、右格贴右、中格居中，
 * 相邻格保持固定间距；重叠时长文本（书名/章节名）逐字截短补省略号，
 * 全是短项时中格让位。页脚整行共用一种字体面，页眉书名沿用题头字体。
 * English: composition, layout and painting for the header/footer slots. The left slot hugs the
 * left edge, the right slot the right edge and the center slot sits centred with a fixed gap
 * between neighbours; on overlap long text (title/chapter) trims character by character with an
 * ellipsis, and when only short items remain the center slot yields. The footer row shares one
 * face; the header title keeps the heading face.
 */
#include "reader_chrome.h"

#include <stdio.h>
#include <string.h>

#include "epdiy.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_nav.h"

/* ---- 几何与常量 / Geometry and constants ---- */
#define CHROME_LEFT_X 36
#define CHROME_RIGHT_X 648
#define CHROME_SLOT_GAP 24
// 电池图标占位与 ui_nav_battery_draw 的 40px 机身加 2px 余量一致，右对齐时仍落 606。
// The icon box matches ui_nav_battery_draw's 40 px body plus 2 px slack, so right alignment keeps x=606.
#define CHROME_ICON_W 42
#define CHROME_HEADER_TEXT_PX 28 ///< 页眉非书名项字号，与状态栏一致 / Header size for non-title items, matching the status bar
#define CHROME_HEADER_TITLE_PX 32 ///< 页眉书名字号，沿用历史页眉 / Header title size, per the historical header
#define CHROME_SLOT_TEXT_MAX 64

// 页眉分隔线、页脚进度条与行基准由调用方持有；本文件只负责三格内容。
// The header rule, footer bar and row origin stay with the caller; this file owns the three slots.

/* ---- 槽位文本合成 / Slot text composition ---- */
// 复制并按 UTF-8 边界截断，避免半字符。/ Copy with a UTF-8 safe cut, never splitting a glyph.
static void chrome_copy(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void cut_last_utf8(char *text) {
    size_t n = strlen(text) - 1;
    while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
    text[n] = 0;
}
static void slot_text(const reader_chrome_data_t *d, app_reader_slot_item_t item,
                      char *buf, size_t cap) {
    buf[0] = 0;
    switch (item) {
    case APP_READER_SLOT_TITLE:
        chrome_copy(buf, cap, d->title ? d->title : "");
        break;
    case APP_READER_SLOT_CHAPTER:
        chrome_copy(buf, cap, d->chapter ? d->chapter : "");
        break;
    case APP_READER_SLOT_PAGE:
        snprintf(buf, cap, "%u/%u", d->page + 1, d->page_count);
        break;
    case APP_READER_SLOT_PAGE_PROGRESS:
        snprintf(buf, cap, "%u/%u · 全书 %u%%", d->page + 1, d->page_count, d->percent);
        break;
    case APP_READER_SLOT_PERCENT:
        snprintf(buf, cap, "%u%%", d->percent);
        break;
    case APP_READER_SLOT_CLOCK:
        chrome_copy(buf, cap, d->clock ? d->clock : "");
        break;
    case APP_READER_SLOT_BATTERY:
        if (d->battery < 0) chrome_copy(buf, cap, "--%");
        else snprintf(buf, cap, "%u%%", (unsigned)d->battery);
        break;
    case APP_READER_SLOT_DATE:
        chrome_copy(buf, cap, d->date ? d->date : "");
        break;
    default:
        break; // 不显示与电量图标在这里都没有文本。/ Blank and the battery icon carry no text here.
    }
}

/* ---- 字体面与测量 / Faces and metrics ---- */
// 内建字库缺《》【】等常用标点，按每行覆盖情况换字体会让同一行页脚在两种字号间跳。
// One face for the whole footer: the card face when one is loaded, otherwise the builtin
// supplement. The builtin lacks common punctuation such as 《》【】, and choosing the face per
// line made the same footer jump between two apparent sizes.
static bool chrome_face_ready(void) {
    return ttf_font_ready() && !ttf_font_is_builtin();
}
static int chrome_face_width(int px, const char *text) {
    if (!chrome_face_ready()) return ui_text_fixed_width_px(px, text);
    int keep = ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    int width = ttf_text_width_px(px, text);
    ttf_font_select(keep);
    return width;
}
// 按页脚实际字体量宽再绘制：内建字宽比阅读字体窄三成，混量会压到相邻槽位。
// Measure and paint with the footer's real face: the builtin advances are about 30% narrower than
// the card face, so mixing the two would let slots collide.
static void chrome_face_draw(uint8_t *fb, int x, int y_top, int px, const char *text,
                             enum EpdFontFlags align) {
    if (!chrome_face_ready()) {
        ui_text_fixed(fb, x, y_top, px, text, align, false);
        return;
    }
    int keep = ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    ttf_draw_text_px(fb, x, y_top + ttf_ascender_px(px), px, text, align, UI_INK_BLACK, UI_INK_WHITE);
    ttf_font_select(keep);
}
// 槽位量宽：页眉书名走题头字体并给出实际字号，其余页眉项固定字号，页脚走页脚字体。
// Slot metrics: the header title uses the heading face and reports its resolved size, other header
// items use the fixed size, and the footer uses the footer face.
static int slot_measure(app_reader_slot_item_t item, const char *text, int px, bool header,
                        int *used_px) {
    *used_px = px;
    if (item == APP_READER_SLOT_ICON_BATTERY) return CHROME_ICON_W;
    if (!text[0]) return 0;
    if (header && item == APP_READER_SLOT_TITLE)
        return ui_text_title_measure(px, text, text, used_px);
    if (header) return ui_text_fixed_width_px(px, text);
    return chrome_face_width(px, text);
}

/* ---- 排布与截短 / Layout and trimming ---- */
typedef struct {
    app_reader_slot_item_t item;
    char text[CHROME_SLOT_TEXT_MAX];
    int width; ///< 量宽结果，图标为固定占位 / Measured width, fixed box for the icon
    int px;    ///< 实际绘制字号 / Resolved type size
    bool drawn;
} chrome_slot_t;
// 截短到配额：先逐字去掉尾部，截短过就补「…」，连省略号都放不下则继续截。
// Trim to a quota: drop tail characters one by one, append an ellipsis when trimmed, and keep
// cutting when even the ellipsis does not fit.
static void slot_trim(chrome_slot_t *slot, bool header, int quota) {
    if (quota <= 0) {
        slot->text[0] = 0;
        slot->width = 0;
        return;
    }
    const size_t original = strlen(slot->text);
    int used = slot->px, dummy;
    while (slot->text[0] && slot_measure(slot->item, slot->text, slot->px, header, &used) > quota)
        cut_last_utf8(slot->text);
    slot->px = used;
    if (strlen(slot->text) < original && slot->text[0]) {
        const int ellipsis = slot_measure(slot->item, "…", slot->px, header, &dummy);
        while (slot->text[0] &&
               slot_measure(slot->item, slot->text, slot->px, header, &used) + ellipsis > quota)
            cut_last_utf8(slot->text);
        // 缓冲内追加省略号；空间不足时保持无省略号的截短结果。
        // Append the ellipsis within the buffer; keep the bare trim when it would not fit.
        if (slot->text[0] && strlen(slot->text) + 3 < sizeof(slot->text))
            strcat(slot->text, "…");
    }
    slot->width = slot->text[0] ? slot_measure(slot->item, slot->text, slot->px, header, &slot->px) : 0;
}
// 确定性排布：重叠时长文本逐字让位，没有长文本可截时中格整体让位。
// Deterministic layout: long text yields character by character on overlap; with only short items
// left, the center slot yields as a whole.
static void layout_slots(const reader_chrome_data_t *d, bool header, int base_px,
                         int left_x, int right_x, chrome_slot_t slots[3]) {
    const int center_x = (left_x + right_x) / 2;
    for (int i = 0; i < 3; ++i) {
        slots[i].item = app_settings_reader_chrome_item(header ? 0 : 1, (unsigned)i);
        slots[i].drawn = slots[i].item != APP_READER_SLOT_BLANK;
        slots[i].px = base_px;
        slots[i].text[0] = 0;
        slots[i].width = 0;
        if (!slots[i].drawn) continue;
        slot_text(d, slots[i].item, slots[i].text, sizeof(slots[i].text));
        slots[i].width = slot_measure(slots[i].item, slots[i].text, base_px, header, &slots[i].px);
    }
    for (int pass = 0; pass < 6; ++pass) {
        const int left_end = left_x + slots[0].width;
        const int center_start = center_x - slots[1].width / 2;
        const int center_end = center_start + slots[1].width;
        const int right_start = right_x - slots[2].width;
        const bool clash_left = slots[0].drawn && slots[1].drawn &&
            left_end + CHROME_SLOT_GAP > center_start;
        const bool clash_right = slots[1].drawn && slots[2].drawn &&
            center_end + CHROME_SLOT_GAP > right_start;
        if (!clash_left && !clash_right) break;
        int victim = -1, widest = 0;
        for (int i = 0; i < 3; ++i) {
            const bool involved = (clash_left && i <= 1) || (clash_right && i >= 1);
            if (!involved) continue;
            if (slots[i].item != APP_READER_SLOT_TITLE &&
                slots[i].item != APP_READER_SLOT_CHAPTER) continue;
            if (slots[i].width > widest) {
                widest = slots[i].width;
                victim = i;
            }
        }
        if (victim < 0) {
            // 页码/时间/日期/电量这类短项互挤：中格整体让位，左右保持贴边。
            // Short items such as page, clock, date or battery never trim: the center slot yields
            // whole while the sides keep their edges.
            slots[1].drawn = false;
            slots[1].width = 0;
            break;
        }
        int quota;
        if (victim == 0)
            quota = (slots[1].drawn ? center_start
                                    : slots[2].drawn ? right_start - CHROME_SLOT_GAP : right_x) -
                    CHROME_SLOT_GAP - left_x;
        else if (victim == 2)
            quota = right_x - (slots[1].drawn ? center_end + CHROME_SLOT_GAP
                                              : slots[0].drawn ? left_end + CHROME_SLOT_GAP : left_x);
        else {
            // 中格锚定在中心：可用宽度是两侧较窄一侧的两倍。
            // The center slot anchors mid-row: its room is twice the narrower side.
            const int lo = slots[0].drawn ? left_end + CHROME_SLOT_GAP : left_x;
            const int hi = slots[2].drawn ? right_start - CHROME_SLOT_GAP : right_x;
            const int left_room = center_x - lo, right_room = hi - center_x;
            quota = 2 * (left_room < right_room ? left_room : right_room);
        }
        slot_trim(&slots[victim], header, quota);
    }
}

/* ---- 绘制 / Painting ---- */
// 三格共用：i=0 左对齐左界，i=1 居中，i=2 右对齐右界；图标按同一边距落位。
// Shared placement: slot 0 left-aligns, slot 1 centres and slot 2 right-aligns; icons follow the
// same margins.
static void slot_place(unsigned i, int left_x, int right_x,
                       int *x, enum EpdFontFlags *align, int *icon_left) {
    const int center_x = (left_x + right_x) / 2;
    if (i == 0) {
        *x = left_x;
        *align = EPD_DRAW_ALIGN_LEFT;
        *icon_left = *x;
    } else if (i == 1) {
        *x = center_x;
        *align = EPD_DRAW_ALIGN_CENTER;
        *icon_left = *x - CHROME_ICON_W / 2;
    } else {
        *x = right_x;
        *align = EPD_DRAW_ALIGN_RIGHT;
        *icon_left = *x - CHROME_ICON_W;
    }
}
const char *reader_chrome_item_label(app_reader_slot_item_t item) {
    static const char *const labels[APP_READER_SLOT_COUNT] = {
        [APP_READER_SLOT_BLANK] = "不显示",
        [APP_READER_SLOT_TITLE] = "书名",
        [APP_READER_SLOT_CHAPTER] = "章节名",
        [APP_READER_SLOT_PAGE] = "本章页码",
        [APP_READER_SLOT_PAGE_PROGRESS] = "页码·全书进度",
        [APP_READER_SLOT_PERCENT] = "全书进度",
        [APP_READER_SLOT_CLOCK] = "时间",
        [APP_READER_SLOT_BATTERY] = "电量",
        [APP_READER_SLOT_DATE] = "日期星期",
        [APP_READER_SLOT_ICON_BATTERY] = "电量图标",
    };
    return labels[item >= 0 && item < APP_READER_SLOT_COUNT ? item : APP_READER_SLOT_BLANK];
}
void reader_chrome_draw_header(uint8_t *fb, const reader_chrome_data_t *d, int cy) {
    chrome_slot_t slots[3];
    layout_slots(d, true, CHROME_HEADER_TEXT_PX, CHROME_LEFT_X, CHROME_RIGHT_X, slots);
    for (unsigned i = 0; i < 3; ++i) {
        if (!slots[i].drawn) continue;
        int x, icon_left;
        enum EpdFontFlags align;
        slot_place(i, CHROME_LEFT_X, CHROME_RIGHT_X, &x, &align, &icon_left);
        if (slots[i].item == APP_READER_SLOT_ICON_BATTERY) {
            ui_nav_battery_draw(fb, icon_left, cy);
            continue;
        }
        if (!slots[i].text[0]) continue;
        // 书名与时间共用同一视觉中心线；字体切换不改变对齐。
        // Title and clock share one visual centerline across font changes.
        if (slots[i].item == APP_READER_SLOT_TITLE)
            ui_text_title_vc(fb, x, cy, slots[i].px, slots[i].text, slots[i].text, align);
        else
            ui_text_fixed_vc(fb, x, cy, slots[i].px, slots[i].text, align, false);
    }
}
void reader_chrome_draw_footer_row(uint8_t *fb, const reader_chrome_data_t *d,
                                   int top, int px, int left_x, int right_x) {
    chrome_slot_t slots[3];
    layout_slots(d, false, px, left_x, right_x, slots);
    const int cy = top + px / 2;
    for (unsigned i = 0; i < 3; ++i) {
        if (!slots[i].drawn) continue;
        int x, icon_left;
        enum EpdFontFlags align;
        slot_place(i, left_x, right_x, &x, &align, &icon_left);
        if (slots[i].item == APP_READER_SLOT_ICON_BATTERY) {
            ui_nav_battery_draw(fb, icon_left, cy);
            continue;
        }
        if (!slots[i].text[0]) continue;
        chrome_face_draw(fb, x, top, slots[i].px, slots[i].text, align);
    }
}
uint32_t reader_chrome_header_fingerprint(const reader_chrome_data_t *d) {
    // 指纹覆盖三格的配置项、合成文本与可见性；电量图标随电量与充电状态变化，
    // 单独并入。文本未变的翻页不会推顶部差分带。
    // The fingerprint covers each slot's item, composed text and visibility; the battery icon also
    // moves with charge and the charging flag, folded in separately. Turns that compose identical
    // texts skip the top-band differential.
    chrome_slot_t slots[3];
    layout_slots(d, true, CHROME_HEADER_TEXT_PX, CHROME_LEFT_X, CHROME_RIGHT_X, slots);
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 3; ++i) {
        hash = (hash ^ (uint32_t)slots[i].item) * 16777619u;
        hash = (hash ^ (slots[i].drawn ? 1u : 0u)) * 16777619u;
        for (const char *p = slots[i].text; *p; ++p) hash = (hash ^ (uint8_t)*p) * 16777619u;
    }
    hash = (hash ^ (uint32_t)(d->battery + 1)) * 16777619u;
    hash = (hash ^ (d->charging ? 1u : 0u)) * 16777619u;
    return hash;
}
