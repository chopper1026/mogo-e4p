/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：以生成的 MOBI 样本验证压缩、目录、封面、图片与边界。
 * English: Exercise compression, navigation, cover, images and bounds with generated MOBI files.
 * 冻结：仅供主机验证。/ Frozen: Host validation only.
 */
#include "book_mobi.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 字体引擎的宿主替身：书内字体在这里一律拒绝。/ Host stand-in for the font engine: always declines.
int ttf_font_open_mem(unsigned char *data, size_t len, const char *label) {
    (void)len; (void)label;
    free(data);
    return -1;
}
void ttf_font_close_embedded(void) {}

static const char *const kTitles[] = {
    "第一章 河边清晨", "第二章 雨后小路", "第三章 山间书屋", "第四章 灯下归途"};

static bool contains(const char *haystack, size_t hay_len, const char *needle) {
    size_t n = strlen(needle);
    if (!n || n > hay_len) return false;
    for (size_t i = 0; i + n <= hay_len; ++i)
        if (!memcmp(haystack + i, needle, n)) return true;
    return false;
}

static void check_book(const char *path, bool labelled) {
    book_mobi_t *book = NULL;
    assert(book_mobi_open(path, &book) == ESP_OK);
    assert(book_mobi_chapter_count(book) == 4);
    assert(book_mobi_total_bytes(book) > 4096);
    uint32_t previous = 0;
    for (size_t i = 0; i < 4; ++i) {
        char title[128];
        assert(book_mobi_chapter_title(book, i, title, sizeof(title)) == ESP_OK);
        if (labelled) assert(strcmp(title, kTitles[i]) == 0);
        else assert(strncmp(title, "第 ", 3) == 0);
        uint32_t offset = book_mobi_chapter_byte_offset(book, i);
        assert(i == 0 || offset > previous);
        previous = offset;
        html_text_t text = {0};
        assert(book_mobi_load(book, i, &text) == ESP_OK);
        assert(text.utf8 && text.len > 4096 && text.count > 8);
        assert(text.utf8[text.len] == 0);
        char marker[32];
        snprintf(marker, sizeof(marker), "记录 %zu-001", i + 1);
        assert(contains(text.utf8, text.len, marker));
        assert(contains(text.utf8, text.len, labelled ? kTitles[i] : "记录"));
        // 插图只放在第三章，其余章节不该冒出图片条目。
        // Only chapter three carries art; the others must not sprout image entries.
        assert((i == 2) == (text.image_count == 1));
        if (i == 2) {
            unsigned width = 0, height = 0;
            assert(book_mobi_image_dimensions(book, text.images[0], &width, &height) == ESP_OK);
            assert(width == 1 && height == 1);
            uint8_t *data = NULL;
            size_t size = 0;
            bool png = true;
            assert(book_mobi_image(book, text.images[0], &data, &size, &png) == ESP_OK);
            assert(!png && size > 2 && data[0] == 0xff && data[1] == 0xd8);
            free(data);
        }
        html_text_free(&text);
    }
    // 越界章节与未知图片路径必须干净失败。/ Out-of-range chapters and unknown art must fail cleanly.
    html_text_t overflow = {0};
    assert(book_mobi_load(book, 4, &overflow) == ESP_ERR_INVALID_ARG);
    assert(!overflow.utf8 && !overflow.count);
    uint8_t *data = NULL;
    size_t size = 0;
    bool png = false;
    assert(book_mobi_image(book, "rec:99", &data, &size, &png) != ESP_OK);
    assert(!data && !size);
    char title[8];
    assert(book_mobi_chapter_title(book, 0, title, sizeof(title)) == ESP_ERR_INVALID_SIZE);
    book_mobi_close(book);
}

int main(int argc, char **argv) {
    assert(argc >= 2);
    uint8_t *cover = NULL;
    size_t cover_size = 0;
    bool png = false;
    assert(book_mobi_cover(argv[1], &cover, &cover_size, &png) == ESP_OK);
    assert(!png && cover_size > 2 && cover[0] == 0xff && cover[1] == 0xd8);
    free(cover);
    assert(book_mobi_cover_bounded(argv[1], &cover, &cover_size, &png, 16) == ESP_ERR_NO_MEM);
    assert(!cover && !cover_size);
    char title[128], author[128];
    assert(book_mobi_metadata(argv[1], title, sizeof(title), author, sizeof(author)) == ESP_OK);
    assert(strcmp(title, "河边记事") == 0);
    assert(strcmp(author, "Read Pico fixtures") == 0);
    for (int i = 1; i < argc; ++i) {
        bool labelled = strstr(argv[i], "无目录") == NULL;
        check_book(argv[i], labelled);
    }
    // 非 MOBI 文件与缺失文件不能伪装成可读。/ Non-MOBI and missing files must not pass as readable.
    book_mobi_t *rejected = NULL;
    assert(book_mobi_open("/tmp/definitely-missing.mobi", &rejected) == ESP_ERR_NOT_FOUND);
    assert(rejected == NULL);
    assert(book_mobi_metadata("/tmp/definitely-missing.mobi", title, sizeof(title), author,
                              sizeof(author)) == ESP_ERR_NOT_FOUND);
    puts("book mobi host tests passed");
    return 0;
}
