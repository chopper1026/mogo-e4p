/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：纯视图；文字来自已完成查询，不读词库或改变阅读状态。
 * English: Pure view using completed query data, without dictionary I/O or reader mutations. */
#include "ui_locale.h"
#include "ui_reader_dictionary.h"
#include "ui_kit.h"
#include "ttf_font.h"
#include <stdio.h>
#include <string.h>

enum { DICTIONARY_PAGE_LINES = 6 };

static void invert_logical_rect(uint8_t *fb, EpdRect logical) {
    EpdRect rect = ui_rotate_rect_to_fb(logical);
    size_t stride = (size_t)epd_width() / 2;
    for (int y = rect.y; y < rect.y + rect.height; ++y)
        for (int x = rect.x; x < rect.x + rect.width; ++x)
            fb[(size_t)y * stride + x / 2] ^= x & 1 ? 0xf0 : 0x0f;
}
EpdRect ui_reader_dictionary_selection(uint8_t *fb, const reader_selection_t *s,
    const book_layout_glyph_t *g, size_t count) {
    uint32_t first, last;
    if (!fb || !reader_selection_range(s, g, count, &first, &last)) return (EpdRect){0};
    const int width = epd_rotated_display_width(), height = epd_rotated_display_height();
    EpdRect box = {width, height, 0, 0}; int right = 0, bottom = 0;
    size_t first_glyph = count, last_glyph = count;
    for (size_t i = 0; i < count; ++i) if (g[i].start >= first && g[i].start < last) {
        if (first_glyph == count) first_glyph = i;
        last_glyph = i;
        if (g[i].x < box.x) box.x = g[i].x;
        if (g[i].y < box.y) box.y = g[i].y;
        if (g[i].x + g[i].width > right) right = g[i].x + g[i].width;
        if (g[i].y + g[i].height > bottom) bottom = g[i].y + g[i].height;
    }
    if (first_glyph == count) return (EpdRect){0};
    const book_layout_glyph_t *ends[] = {&g[first_glyph], &g[last_glyph]};
    int handle_x[] = {ends[0]->x, ends[1]->x + ends[1]->width};
    int handle_y[] = {ends[0]->y + ends[0]->height + 9, ends[1]->y + ends[1]->height + 9};
    for (int i = 0; i < 2; ++i) {
        if (handle_x[i] - 7 < box.x) box.x = handle_x[i] - 7;
        if (handle_y[i] + 8 > bottom) bottom = handle_y[i] + 8;
        if (handle_x[i] + 8 > right) right = handle_x[i] + 8;
    }
    if (box.x < 0) box.x = 0;
    if (box.y < 0) box.y = 0;
    if (right > width) right = width;
    if (bottom > height) bottom = height;
    for (int y = box.y; y < bottom; ++y) {
        unsigned char covered[1216] = {0};
        for (size_t i = 0; i < count; ++i) if (g[i].start >= first && g[i].start < last &&
            y >= g[i].y && y < g[i].y + g[i].height) {
            int from = g[i].x, to = from + g[i].width;
            if (from < 0) from = 0;
            if (to > width) to = width;
            if (to > from) memset(covered + from, 1, (size_t)(to - from));
        }
        for (int i = 0; i < 2; ++i) for (int x = handle_x[i] - 7; x <= handle_x[i] + 7; ++x)
            if (x >= 0 && x < width &&
                (x - handle_x[i]) * (x - handle_x[i]) + (y - handle_y[i]) * (y - handle_y[i]) <= 49)
                covered[x] = 1;
        for (int x = box.x; x < right;) {
            if (!covered[x]) { ++x; continue; }
            int start = x++;
            while (x < right && covered[x]) ++x;
            invert_logical_rect(fb, (EpdRect){start, y, x - start, 1});
        }
    }
    box.width = right - box.x; box.height = bottom - box.y;
    return box;
}
static void text(uint8_t *fb, int x, int y, int px, const char *value) {
    ttf_draw_text_px(fb, x, y + ttf_ascender_px(px), px, ui_tr(value), EPD_DRAW_ALIGN_LEFT, 0, 15);
}
void ui_reader_dictionary_notice(uint8_t *fb) {
    int previous = ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    ui_fill_rect_fast((EpdRect){208,150,268,68}, UI_GRAY_WHITE, fb);
    text(fb, 218, 158, 22, "本页无法选字");
    text(fb, 218, 187, 22, "请调整字号");
    ttf_font_select(previous);
}
static size_t wrap(const char *value, size_t offset, char line[512], int width, int px, bool ui_face) {
    size_t n = strlen(value), at = offset, used = 0;
    while (at < n && used + 4 < 512) {
        if (value[at] == '\n') { ++at; break; }
        unsigned char c = (unsigned char)value[at];
        size_t bytes = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (bytes > n - at) break;
        memcpy(line + used, value + at, bytes); line[used + bytes] = 0;
        if ((ui_face ? ui_text_fixed_width_px(px, line) : ttf_text_width_px(px, line)) > width && used) { line[used] = 0; break; }
        used += bytes; at += bytes;
    }
    line[used] = 0; return at;
}
// 分段正文保留所有读音、对应词头和例句，无整条拼接分配。/ Segmented content retains every reading, alias and example without concatenation allocations.
static size_t content_segments(const book_dictionary_result_t *r, const char *segments[10]) {
    size_t n = 0;
    if (r->reading[0]) { segments[n++] = r->kind == BOOK_DICTIONARY_CHINESE ? ui_tr("读音：") : "IPA: "; segments[n++] = r->reading; segments[n++] = "\n"; }
    if (r->variant[0]) { segments[n++] = r->kind == BOOK_DICTIONARY_CHINESE ? ui_tr("对应词头：") : "See: "; segments[n++] = r->variant; segments[n++] = "\n"; }
    segments[n++] = r->definition[0] ? r->definition : ui_tr(r->kind == BOOK_DICTIONARY_EN_EN ? "词库未提供英文释义" : "词库未提供中文释义");
    if (r->example[0]) { segments[n++] = r->kind == BOOK_DICTIONARY_CHINESE ? ui_tr("\n例句：") : "\nExample: "; segments[n++] = r->example; }
    return n;
}
size_t ui_reader_dictionary_content_size(const book_dictionary_result_t *r) {
    const char *segments[10]; size_t count = content_segments(r, segments), total = 0;
    for (size_t i = 0; i < count; ++i) total += strlen(segments[i]);
    return total;
}
static unsigned char content_byte(const char **segments, size_t count, size_t offset) {
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(segments[i]);
        if (offset < length) return (unsigned char)segments[i][offset];
        offset -= length;
    }
    return 0;
}
static size_t content_line(const book_dictionary_result_t *r, size_t offset, char line[512]) {
    const char *segments[10]; size_t count = content_segments(r, segments), used = 0;
    unsigned char c;
    size_t word_offset = 0, word_end = 0;
    while ((c = content_byte(segments, count, offset)) && used + 4 < 512) {
        if (c == '\n') { ++offset; break; }
        size_t bytes = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        for (size_t i = 0; i < bytes; ++i) line[used + i] = (char)content_byte(segments, count, offset + i);
        line[used + bytes] = 0;
        if (r->kind != BOOK_DICTIONARY_CHINESE && c == ' ' && used && line[used - 1] != ' ') {
            word_end = used; word_offset = offset + 1;
        }
        if (ttf_text_width_px(32, line) > 588 && used) {
            if (word_end) {
                used = word_end; offset = word_offset;
                while (content_byte(segments, count, offset) == ' ') ++offset;
            }
            break;
        }
        used += bytes; offset += bytes;
    }
    line[used] = 0;
    return offset;
}
size_t ui_reader_dictionary_next_page(const book_dictionary_result_t *r, size_t offset) {
    if (r->status != BOOK_DICTIONARY_OK) return 0;
    // 0 号槽在阅读上下文中就是用户阅读字体；不走 ui_text 的内置字库回退。/ Slot zero holds the chosen reader face here; bypass ui_text's built-in fallback.
    int previous = ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    char line[512]; size_t length = ui_reader_dictionary_content_size(r);
    const size_t start = offset;
    for (int i = 0; i < DICTIONARY_PAGE_LINES && offset < length; ++i) offset = content_line(r, offset, line);
    ttf_font_select(previous);
    if (offset <= start || offset >= length) return 0;
    // 尾部空白不单独占一页。/ Trailing whitespace alone does not need a page.
    const char *segments[10]; size_t count = content_segments(r, segments);
    for (size_t i = offset; i < length; ++i)
        if (!strchr(" \t\r\n", content_byte(segments, count, i))) return offset;
    return 0;
}
ui_dictionary_action_t ui_reader_dictionary_action(const ui_reader_dictionary_t *v, int x, int y) {
    if (!v->open) return UI_DICTIONARY_NONE;
    if (y < v->top || y >= v->top + 584 || (x >= 540 && y < v->top + 82)) return UI_DICTIONARY_CLOSE;
    if (y < v->top + 472 || y >= v->top + 536) return UI_DICTIONARY_NONE;
    bool english = v->kind != BOOK_DICTIONARY_CHINESE;
    if (x < (english ? 196 : 228)) return v->offset ? UI_DICTIONARY_PREV : UI_DICTIONARY_NONE;
    if (x >= (english ? 488 : 456)) return v->next_offset ? UI_DICTIONARY_NEXT : UI_DICTIONARY_NONE;
    if (english) return x < 342 ? UI_DICTIONARY_EN_ZH : UI_DICTIONARY_EN_EN;
    return v->can_character ? UI_DICTIONARY_CHARACTER : UI_DICTIONARY_NONE;
}
// 只查释义所需的汉字，不把换行或 IPA 缺字误报为英汉字体不支持。
// Check the Han characters the gloss needs, not newlines or missing IPA.
static bool missing_han(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned n = *p < 0x80 ? 1 : *p < 0xe0 ? 2 : *p < 0xf0 ? 3 : 4;
        uint32_t cp = *p & (n == 1 ? 0x7f : (1u << (7 - n)) - 1);
        for (unsigned i = 1; i < n; ++i) cp = (cp << 6) | (p[i] & 63);
        if ((cp >= 0x3400 && cp <= 0x9fff) || (cp >= 0xf900 && cp <= 0xfaff) ||
            (cp >= 0x20000 && cp <= 0x323af)) {
            char glyph[5] = {0}; memcpy(glyph, p, n);
            if (!ttf_font_has_text(glyph)) return true;
        }
        p += n;
    }
    return false;
}
static void ui_copy(uint8_t *fb, int x, int y, int px, const char *copy) {
    ui_text_fixed(fb, x, y, px, copy, EPD_DRAW_ALIGN_LEFT, false);
}
static void mode_tab(uint8_t *fb, int x, int top, const char *label, bool active) {
    if (active) ui_fill_rect_fast((EpdRect){x - 63, top + 476, 126, 48}, UI_GRAY_BLACK, fb);
    ui_text_fixed(fb, x, top + 483, 28, label, EPD_DRAW_ALIGN_CENTER, active);
}
void ui_reader_dictionary_draw(uint8_t *fb, const ui_reader_dictionary_t *v,
    const book_dictionary_result_t *r, const char *query) {
    if (!v->open) return;
    int previous = ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    int top = v->top;
    bool english = v->kind != BOOK_DICTIONARY_CHINESE;
    ui_fill_rect_fast((EpdRect){0,top,UI_LOCK_WIDTH,584},UI_GRAY_WHITE,fb);
    ui_draw_separator(fb,top,0,UI_LOCK_WIDTH,UI_GRAY_BLACK);
    char title[320]; snprintf(title,sizeof(title),"%s",query);
    if (english) for (char *p = title; *p; ++p) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
    bool title_missing = !ttf_font_has_text(english ? title : query);
    while (ttf_text_width_px(42,title)>490 && strlen(title)>3) { size_t n=strlen(title)-1;while(n&&((unsigned char)title[n]&0xc0)==0x80)--n;title[n]=0; }
    text(fb,40,top+22,42,title);
    if (english) ui_copy(fb, 562, top + 28, 26, "关闭");
    else text(fb,570,top+28,28,"关闭");
    char codes[192]="";
    if (title_missing) {
        const unsigned char *q=(const unsigned char *)query;size_t n=0;
        while(*q && n+12<sizeof(codes)) { uint32_t cp;unsigned k=*q<0x80?1:*q<0xe0?2:*q<0xf0?3:4;cp=*q& (k==1?0x7f:(1u<<(7-k))-1);for(unsigned j=1;j<k;++j)cp=(cp<<6)|(q[j]&63);n+=(size_t)snprintf(codes+n,sizeof(codes)-n,"U+%04X ",(unsigned)cp);q+=k; }
        if (english) ui_copy(fb,40,top+80,24,codes);
        else text(fb,40,top+80,24,codes);
    } else if (r->reading[0]) {
        char line[512];(void)wrap(r->reading,0,line,588,30,false);text(fb,40,top+80,30,line);
    } else if (english) ui_copy(fb,40,top+80,28,v->loading?"正在查询…":"词库未提供读音");
    else text(fb,40,top+80,28,v->loading?"正在查询…":"词库未提供读音");
    const char *message=NULL;
    if(v->loading)message="正在读取离线词库…";
    else switch(r->status) {
        case BOOK_DICTIONARY_OK:break;
        case BOOK_DICTIONARY_MISSING:message=english?"未安装此英文词库\n请到官网说明书下载英文词库\nmogo.mantake.shop":"未安装词库\n请到官网说明书下载词库\nmogo.mantake.shop\n解压后将 dictionaries 文件夹\n复制到 TF 卡根目录";break;
        case BOOK_DICTIONARY_NOT_FOUND:message=english?"此词库未收录该英文词或短语":"未收录这个词语，可点“逐字”查询单字";break;
        case BOOK_DICTIONARY_UNSUPPORTED:message=english?"英文词库版本不支持\n请到官网说明书重新下载\nmogo.mantake.shop":"词库版本不支持\n请到官网说明书下载词库\nmogo.mantake.shop\n解压后将 dictionaries 文件夹\n复制到 TF 卡根目录";break;
        case BOOK_DICTIONARY_CORRUPT:message=english?"英文词库损坏\n请到官网说明书重新下载\nmogo.mantake.shop":"词库损坏\n请到官网说明书下载词库\nmogo.mantake.shop\n解压后将 dictionaries 文件夹\n复制到 TF 卡根目录";break;
        case BOOK_DICTIONARY_IO:message="TF 卡读取失败，请检查存储卡";break;
        case BOOK_DICTIONARY_CANCELLED:message="查询已取消";break;
        case BOOK_DICTIONARY_BUSY:message="查询资源不足，请关闭后重试";break;
    }
    message=ui_tr(message);
    size_t offset=message?0:v->offset;
    char line[512];
    size_t length = message ? strlen(message) : ui_reader_dictionary_content_size(r);
    for (int i=0; i<DICTIONARY_PAGE_LINES && offset<length; ++i) {
        offset = message ? wrap(message,offset,line,588,32,english) : content_line(r,offset,line);
        if (english && message) ui_copy(fb,40,top+136+i*48,32,line);
        else text(fb,40,top+136+i*48,32,line);
    }
    if (english) {
        if (!v->loading && v->kind == BOOK_DICTIONARY_EN_ZH && r->status == BOOK_DICTIONARY_OK && missing_han(r->definition))
            ui_copy(fb,40,top+433,22,"阅读字体缺少汉字，请换用含汉字的字体");
        else if (title_missing) ui_copy(fb,40,top+433,22,"当前字体缺字，可更换 TF 卡字体");
        if (v->offset) ui_text_fixed(fb, 98, top + 483, 28, "上一页", EPD_DRAW_ALIGN_CENTER, false);
        if (v->next_offset) ui_text_fixed(fb, 586, top + 483, 28, "下一页", EPD_DRAW_ALIGN_CENTER, false);
        mode_tab(fb,269,top,"英汉",v->kind == BOOK_DICTIONARY_EN_ZH);
        mode_tab(fb,415,top,"英英",v->kind == BOOK_DICTIONARY_EN_EN);
        (void)wrap(r->source[0]?r->source:ui_tr("离线英文词典"),0,line,588,22,true);
        ui_copy(fb,40,top+550,22,line);
    } else {
        if (title_missing) text(fb,40,top+438,22,"当前字体缺字，可更换 TF 卡字体");
        if (v->offset) text(fb,50,top+490,28,"上一页");
        if (v->can_character) text(fb,300,top+490,28,"逐字");
        if (v->next_offset) text(fb,520,top+490,28,"下一页");
        (void)wrap(r->source[0]?r->source:ui_tr("离线中文词典"),0,line,588,22,false);text(fb,40,top+550,22,line);
    }
    ttf_font_select(previous);
}
