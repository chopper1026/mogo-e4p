/* 中文：独立书籍封面壁纸，最近阅读的封面支持完整显示或居中裁剪。
 * English: Standalone wallpaper showing the last-read book cover, contained or center-cropped.
 */
#include "book_lock_cover.h"
#include "book_cover.h"
#include "book_cover_auto.h"
#include "book_metadata.h"
#include "book_progress.h"
#include "book_title.h"
#include "esp_heap_caps.h"
#include "memory_manager.h"
#include "settings.h"
#include "ui_image_dither.h"
#include "ui_kit.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { FACE_W = 684, FACE_H = 1216, FACE_CENTER_Y = 584 };

static EpdRect cover_rect(unsigned w, unsigned h) {
    int fit_w = 576, fit_h = 1000;
    if ((uint64_t)w * fit_h > (uint64_t)h * fit_w) fit_h = (int)((uint64_t)h * fit_w / w);
    else fit_w = (int)((uint64_t)w * fit_h / h);
    if (!fit_w) fit_w = 1;
    if (!fit_h) fit_h = 1;
    return (EpdRect){(FACE_W - fit_w) / 2, FACE_CENTER_Y - fit_h / 2, fit_w, fit_h};
}

static uint8_t *read_embedded(const char *path, unsigned *w, unsigned *h, unsigned max_w, unsigned max_h) {
    const size_t reserve = 1536u * 1024u;
    size_t available = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (available <= reserve) return NULL;
    size_t budget = available - reserve, bytes = 0;
    if (budget > 4u * 1024u * 1024u) budget = 4u * 1024u * 1024u;
    uint8_t *data = NULL, *gray = NULL;
    bool png = false;
    unsigned sw = 0, sh = 0;
    if (book_metadata_cover_bounded(path, &data, &bytes, &png, budget) == ESP_OK &&
        book_image_dimensions(data, bytes, png, &sw, &sh) && sw && sh) {
        *w = sw < max_w ? sw : max_w;
        *h = sh < max_h ? sh : max_h;
        if ((uint64_t)sw * *h > (uint64_t)sh * *w) *h = (unsigned)((uint64_t)sh * *w / sw);
        else *w = (unsigned)((uint64_t)sw * *h / sh);
        if (!*w) *w = 1;
        if (!*h) *h = 1;
        gray = mm_alloc(MM_OWNER_FILE_JOB, MM_POOL_PSRAM, (size_t)*w * *h, 0, reserve);
        if (gray && !book_image_grayscale(data, bytes, png, *w, *h, gray)) {
            mm_free(gray);
            gray = NULL;
        }
    }
    free(data);
    return gray;
}

static uint8_t *load_cover(unsigned *w, unsigned *h, unsigned max_w, unsigned max_h) {
    char path[288];
    struct stat st;
    if (!book_progress_last_path(path, sizeof(path)) || stat(path, &st) || !S_ISREG(st.st_mode)) return NULL;
    const bool embedded = book_metadata_supported(path);
    uint8_t *gray = embedded ? read_embedded(path, w, h, max_w, max_h) : NULL;
    if (!gray) {
        char title[256] = {0}, author[128] = {0}, renamed[sizeof(title)];
        if (embedded) (void)book_metadata_read(path, title, sizeof(title), author, sizeof(author));
        if (book_title_get(path, renamed, sizeof(renamed))) memcpy(title, renamed, strlen(renamed) + 1);
        if (!title[0] && !book_title_from_path(path, title, sizeof(title))) return NULL;
        gray = mm_alloc(MM_OWNER_FILE_JOB, MM_POOL_PSRAM, BOOK_COVER_W * BOOK_COVER_H, 0, 0);
        if (!gray) return NULL;
        if (!book_cover_load_list_gray(path, title, author, gray, false, NULL, w, h)) {
            *w = BOOK_COVER_W;
            *h = BOOK_COVER_H;
            if (!book_auto_cover_render(path, title, author, *w, *h, gray)) {
                mm_free(gray);
                return NULL;
            }
        }
    }
    // 解码或缓存读取期间若源文件被移除或替换，不显示旧封面。
    // Do not paint an old cover if the source disappeared or changed during loading.
    struct stat after;
    if (stat(path, &after) || !S_ISREG(after.st_mode) ||
        st.st_size != after.st_size || st.st_mtime != after.st_mtime) {
        mm_free(gray);
        return NULL;
    }
    return gray;
}

bool book_lock_cover_draw(uint8_t *fb, bool crop) {
    if (!fb) return false;
    unsigned w = 0, h = 0;
    uint8_t *gray = load_cover(&w, &h, crop ? FACE_W : 560, crop ? FACE_H : 1000);
    if (!gray) return false;
    ui_clear_page(fb);
    const EpdRect r = crop ? (EpdRect){0, 0, FACE_W, FACE_H} : cover_rect(w, h);
    const book_crop_t src = crop ? book_cover_crop(w, h, FACE_W, FACE_H) : (book_crop_t){0, 0, w, h};
    const uint8_t *origin = gray + (size_t)src.y * w + src.x;
    const image_gray_axis_t xs = image_gray_axis(src.width, r.width), ys = image_gray_axis(src.height, r.height);
    const uint8_t contrast = app_settings_system_contrast();
    for (int y = 0; y < r.height; ++y) {
        const image_gray_row_t row = image_gray_row(origin, w, ys, y);
        for (int x = 0; x < r.width; ++x) {
            uint8_t tone = ui_image_cover_tone(image_gray_sample(row, xs, x), contrast);
            epd_draw_pixel(r.x+x, r.y+y, ui_image_dither_gray(tone, r.x+x, r.y+y), fb);
        }
    }
    mm_free(gray);
    return true;
}

bool book_lock_cover_preview(uint8_t *pixels, bool crop) {
    if (!pixels) return false;
    unsigned w = 0, h = 0;
    uint8_t *gray = load_cover(&w, &h, BOOK_LOCK_PREVIEW_W, BOOK_LOCK_PREVIEW_H);
    if (!gray) return false;
    const EpdRect r = crop ? (EpdRect){0, 0, FACE_W, FACE_H} : cover_rect(w, h);
    const book_crop_t src = crop ? book_cover_crop(w, h, FACE_W, FACE_H) : (book_crop_t){0, 0, w, h};
    const uint8_t *origin = gray + (size_t)src.y * w + src.x;
    const int x0 = r.x * BOOK_LOCK_PREVIEW_W / FACE_W;
    const int y0 = r.y * BOOK_LOCK_PREVIEW_H / FACE_H;
    int tw = (r.x + r.width) * BOOK_LOCK_PREVIEW_W / FACE_W - x0;
    int th = (r.y + r.height) * BOOK_LOCK_PREVIEW_H / FACE_H - y0;
    if (!tw) tw = 1;
    if (!th) th = 1;
    const image_gray_axis_t xs = image_gray_axis(src.width, tw), ys = image_gray_axis(src.height, th);
    const uint8_t contrast = app_settings_system_contrast();
    memset(pixels, 255, BOOK_LOCK_PREVIEW_BYTES);
    for (int y = 0; y < th; ++y) {
        const image_gray_row_t row = image_gray_row(origin, w, ys, y);
        for (int x = 0; x < tw; ++x) {
            uint8_t tone = ui_image_cover_tone(image_gray_sample(row, xs, x), contrast);
            uint8_t nibble = ui_image_dither_gray(tone, x0+x, y0+y) >> 4;
            unsigned at = (y0+y) * BOOK_LOCK_PREVIEW_W + x0+x;
            unsigned shift = (at & 1u) * 4u;
            pixels[at / 2] = (pixels[at / 2] & ~(15u << shift)) | (nibble << shift);
        }
    }
    mm_free(gray);
    return true;
}
