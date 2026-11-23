/* SPDX-License-Identifier: Apache-2.0
 * 中文：公共图像探测、缩放与灰阶解码；不访问书籍缓存、页面或字库。
 * English: Shared image probing, scaling and grayscale decoding; no book caches, pages or fonts.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// 按填满目标、居中裁剪计算的源图窗口；不拉伸。/ Source window for aspect fill and centered cropping, without stretching.
typedef struct {
    unsigned x, y, width, height; ///< 源窗口坐标与尺寸 / Source window coordinates and dimensions
} book_crop_t;

/// 面板范围内的灰阶封面绘制；中心对齐、双线性、无额外位图。
/// Center-aligned bilinear drawing for panel-sized cover windows; no extra bitmap.
typedef struct { int32_t start, step, last; } image_gray_axis_t;
typedef struct { const uint8_t *top, *bottom; unsigned frac; } image_gray_row_t;

static inline image_gray_axis_t image_gray_axis(unsigned source, unsigned target) {
    const int32_t step = (int32_t)((source << 16) / target);
    return (image_gray_axis_t){ (step - 65536) / 2, step, (int32_t)((source - 1) << 16) };
}

static inline unsigned image_gray_coord(image_gray_axis_t axis, unsigned at) {
    const int32_t q = axis.start + (int32_t)at * axis.step;
    return q < 0 ? 0u : (unsigned)(q > axis.last ? axis.last : q);
}

static inline image_gray_row_t image_gray_row(const uint8_t *pixels, unsigned stride,
                                                image_gray_axis_t ys, unsigned y) {
    const unsigned q = image_gray_coord(ys, y);
    const uint8_t *top = pixels + (size_t)(q >> 16) * stride;
    return (image_gray_row_t){top, q < (unsigned)ys.last ? top + stride : top, (q >> 8) & 255u};
}

static inline uint8_t image_gray_sample(image_gray_row_t row, image_gray_axis_t xs, unsigned x) {
    const unsigned q = image_gray_coord(xs, x), a = q >> 16;
    const unsigned fx = (q >> 8) & 255u;
    if (!fx && !row.frac) return row.top[a];
    const unsigned b = q < (unsigned)xs.last ? a + 1 : a;
    const unsigned u = row.top[a] * (256u - fx) + row.top[b] * fx;
    const unsigned v = row.bottom[a] * (256u - fx) + row.bottom[b] * fx;
    return (uint8_t)((u * (256u - row.frac) + v * row.frac + 32768u) >> 16);
}

/// 兼容既有调用名；仅计算窗口，不读取文件。/ Retain the existing API name; calculate the crop without file I/O.
book_crop_t book_cover_crop(unsigned src_width, unsigned src_height,
                            unsigned dst_width, unsigned dst_height);
/// JPEG/PNG/静态 WebP 解码为每像素一字节灰度；WebP 按签名识别，png 仅标记 PNG。
/// Decode JPEG/PNG/static WebP to byte grayscale, bounded to one panel; WebP is signature-detected.
bool book_image_grayscale(const uint8_t *data, size_t size, bool png,
                          unsigned out_width, unsigned out_height, uint8_t *out);
/// 漫画基线 JPEG 的软件回退直接解目标所需的亮度平面。/ Decode a target-sized luma plane on the comic baseline JPEG software path.
bool image_comic_grayscale(const uint8_t *data, size_t size, bool png,
                           unsigned out_width, unsigned out_height, uint8_t *out);
/// 在解码后的整页漫画上校正抬高的黑位，缩图时增强暗边。原地处理，每张位图仅调用一次。
/// Correct lifted ink and strengthen reduced dark edges in place, once per decoded comic bitmap.
void image_enhance_comic(uint8_t *gray, unsigned width, unsigned height, bool downscaled);
/// 读取图像头尺寸；不分配像素。/ Read header dimensions without pixel allocations.
bool book_image_dimensions(const uint8_t *data, size_t size, bool png,
                           unsigned *width, unsigned *height);
/// JPEG 帧头同时支持基线与渐进式。/ Probe both baseline and progressive JPEG frame headers.
bool book_jpeg_frame(const uint8_t *data, size_t size, uint8_t *sof, unsigned *width, unsigned *height);

/// 文件流输入上限为 50 MiB，输出仍限制为一屏。/ Stream inputs up to 50 MiB, with output bounded to one panel.
#define BOOK_IMAGE_FILE_MAX (50u * 1024u * 1024u)
/// 仅读取文件头，不解码像素。/ Probe file headers without decoding pixels.
bool book_image_file_dimensions(const char *path, bool png, unsigned *width, unsigned *height);
/// 文件流解码为调用方持有的灰阶缓冲。/ Decode a file stream into caller-owned grayscale storage.
bool book_image_file_grayscale(const char *path, bool png, unsigned width, unsigned height, uint8_t *out);
/// PNG 预乘灰度和 alpha，各由调用方提供 width*height 字节。/ Premultiplied PNG gray and alpha, each in caller-owned width*height bytes.
bool book_png_file_layer(const char *path,unsigned width,unsigned height,uint8_t *gray,uint8_t *alpha);

/// 可取消的文件解码；回调只查询当前请求是否已过期，不绘屏、不访问字库。
/// Cancellable file decoding; the callback only checks request lifetime.
typedef bool (*image_cancel_fn)(void *context);
bool book_image_file_grayscale_cancel(const char *path, bool png, unsigned width, unsigned height,
                                      uint8_t *out, image_cancel_fn cancelled, void *context);
bool book_png_file_layer_cancel(const char *path, unsigned width, unsigned height,
                                 uint8_t *gray, uint8_t *alpha, image_cancel_fn cancelled, void *context);
