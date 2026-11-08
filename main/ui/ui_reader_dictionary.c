/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：纯视图；文字来自已完成查询，不读词库或改变阅读状态。
 * English: Pure view using completed query data, without dictionary I/O or reader mutations. */
#include "ui_reader_dictionary.h"
#include "ui_kit.h"
#include "ttf_font.h"
#include <stdio.h>
#include <string.h>

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
    ttf_draw_text_px(fb, x, y + ttf_ascender_px(px), px, value, EPD_DRAW_ALIGN_LEFT, 0, 15);
}
void ui_reader_dictionary_notice(uint8_t *fb) {
    int previous = ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    ui_fill_rect_fast((EpdRect){208,150,268,68}, UI_GRAY_WHITE, fb);
    text(fb, 218, 158, 22, "本页无法选字");
    text(fb, 218, 187, 22, "请调整字号");
    ttf_font_select(previous);
}
static size_t wrap(const char *value, size_t offset, char line[512], int width, int px) {
    size_t n = strlen(value), at = offset, used = 0;
    while (at < n && used + 4 < 512) {
        if (value[at] == '\n') { ++at; break; }
        unsigned char c = (unsigned char)value[at];
        size_t bytes = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (bytes > n - at) break;
        memcpy(line + used, value + at, bytes); line[used + bytes] = 0;
        if (ttf_text_width_px(px, line) > width && used) { line[used] = 0; break; }
        used += bytes; at += bytes;
    }
    line[used] = 0; return at;
}
// 分段正文保留所有读音、对应词头和例句，无整条拼接分配。/ Segmented content retains every reading, alias and example without concatenation allocations.
static size_t content_segments(const book_dictionary_result_t *r, const char *segments[10]) {
    size_t n = 0;
    if (r->reading[0]) { segments[n++] = "读音："; segments[n++] = r->reading; segments[n++] = "\n"; }
    if (r->variant[0]) { segments[n++] = "对应词头："; segments[n++] = r->variant; segments[n++] = "\n"; }
    segments[n++] = r->definition[0] ? r->definition : "词库未提供中文释义";
    if (r->example[0]) { segments[n++] = "\n例句："; segments[n++] = r->example; }
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
    while ((c = content_byte(segments, count, offset)) && used + 4 < 512) {
        if (c == '\n') { ++offset; break; }
        size_t bytes = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        for (size_t i = 0; i < bytes; ++i) line[used + i] = (char)content_byte(segments, count, offset + i);
        line[used + bytes] = 0;
        if (ttf_text_width_px(32, line) > 588 && used) { line[used] = 0; break; }
        used += bytes; offset += bytes;
    }
    line[used] = 0;
    return offset;
}
size_t ui_reader_dictionary_page_end(const book_dictionary_result_t *r, size_t offset) {
    // 0 号槽在阅读上下文中就是用户阅读字体；不走 ui_text 的内置字库回退。/ Slot zero holds the chosen reader face here; bypass ui_text's built-in fallback.
    int previous = ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    char line[512]; size_t length = ui_reader_dictionary_content_size(r);
    for (int i = 0; i < 6 && offset < length; ++i) offset = content_line(r, offset, line);
    ttf_font_select(previous);
    return offset;
}
int ui_reader_dictionary_action(const ui_reader_dictionary_t *v, int x, int y) {
    if (!v->open) return -1;
    if (y < v->top || y >= v->top + 584) return 0;
    if (x >= 540 && y < v->top + 82) return 0;
    if (y >= v->top + 472 && y < v->top + 536) {
        if (!v->can_browse) return -1;
        return x < 228 ? 1 : x >= 456 ? 2 : 3;
    }
    return -1;
}
void ui_reader_dictionary_draw(uint8_t *fb, const ui_reader_dictionary_t *v,
    const book_dictionary_result_t *r, const char *query) {
    if (!v->open) return;
    int previous = ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    int top = v->top;
    ui_fill_rect_fast((EpdRect){0,top,UI_LOCK_WIDTH,584},UI_GRAY_WHITE,fb);
    ui_draw_separator(fb,top,0,UI_LOCK_WIDTH,UI_GRAY_BLACK);
    char title[320]; snprintf(title,sizeof(title),"%s",query);
    while (ttf_text_width_px(42,title)>490 && strlen(title)>3) { size_t n=strlen(title)-1;while(n&&((unsigned char)title[n]&0xc0)==0x80)--n;title[n]=0; }
    text(fb,40,top+22,42,title);text(fb,570,top+28,28,"关闭");
    char codes[192]="";
    if (!ttf_font_has_text(query)) {
        const unsigned char *q=(const unsigned char *)query;size_t n=0;
        while(*q && n+12<sizeof(codes)) { uint32_t cp;unsigned k=*q<0x80?1:*q<0xe0?2:*q<0xf0?3:4;cp=*q& (k==1?0x7f:(1u<<(7-k))-1);for(unsigned j=1;j<k;++j)cp=(cp<<6)|(q[j]&63);n+=(size_t)snprintf(codes+n,sizeof(codes)-n,"U+%04X ",(unsigned)cp);q+=k; }
        text(fb,40,top+80,24,codes);
    } else if (r->reading[0]) {
        char line[512];(void)wrap(r->reading,0,line,588,30);text(fb,40,top+80,30,line);
    } else text(fb,40,top+80,28,v->loading?"正在查询…":"词库未提供读音");
    const char *message=NULL;
    if(v->loading)message="正在读取离线词库…";
    else switch(r->status) {
        case BOOK_DICTIONARY_OK:break;
        case BOOK_DICTIONARY_MISSING:message="未安装词库\n请到官网说明书下载词库\nmogo.mantake.shop\n解压后将 dictionaries 文件夹\n复制到 TF 卡根目录";break;
        case BOOK_DICTIONARY_NOT_FOUND:message="未收录这个词语，可点“逐字”查询单字";break;
        case BOOK_DICTIONARY_UNSUPPORTED:message="词库版本不支持\n请到官网说明书下载词库\nmogo.mantake.shop\n解压后将 dictionaries 文件夹\n复制到 TF 卡根目录";break;
        case BOOK_DICTIONARY_CORRUPT:message="词库损坏\n请到官网说明书下载词库\nmogo.mantake.shop\n解压后将 dictionaries 文件夹\n复制到 TF 卡根目录";break;
        case BOOK_DICTIONARY_IO:message="TF 卡读取失败，请检查存储卡";break;
        case BOOK_DICTIONARY_CANCELLED:message="查询已取消";break;
        case BOOK_DICTIONARY_BUSY:message="查询资源不足，请关闭后重试";break;
    }
    size_t offset=message?0:v->offset;
    char line[512];
    size_t length = message ? strlen(message) : ui_reader_dictionary_content_size(r);
    for (int i=0; i<6 && offset<length; ++i) {
        offset = message ? wrap(message,offset,line,588,32) : content_line(r,offset,line);
        text(fb,40,top+136+i*48,32,line);
    }
    if (!ttf_font_has_text(query)) text(fb,40,top+438,22,"当前字体缺字，可更换 TF 卡字体");
    if (v->can_browse) {text(fb,50,top+490,28,"上一屏");text(fb,300,top+490,28,"逐字");text(fb,520,top+490,28,"下一屏");}
    (void)wrap(r->source[0]?r->source:"离线中文词典",0,line,588,22);text(fb,40,top+550,22,line);
    ttf_font_select(previous);
}
