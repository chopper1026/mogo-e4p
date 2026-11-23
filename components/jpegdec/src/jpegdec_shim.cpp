/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：给 C 代码用的最小包装。jpeg.inl 里的 JPEG_* 函数族被
 * `__MACH__ / __LINUX__ / __MCUXPRESSO / _WIN64` 保护，ESP_PLATFORM 不在其中，
 * 所以 ESP-IDF 上只有 C++ 的 JPEGDEC 类可用；这里用 extern "C" 把它包出来。
 *
 * English: A minimal wrapper for C callers. The JPEG_* functions in jpeg.inl sit behind
 * __MACH__ / __LINUX__ / __MCUXPRESSO / _WIN64 and ESP_PLATFORM is not among them, so under
 * ESP-IDF only the C++ JPEGDEC class exists; this exposes it with C linkage.
 */
#include <new>
#include <stdio.h>
#include <stdint.h>

#include "JPEGDEC.h"

extern "C" {

// 调用方按目标选择 1、2、4 或 8 倍降采样；渐进式只支持 1/8 DC。
// The caller selects a 1, 2, 4 or 8 reduction; progressive frames support only the 1/8 DC plane.
int jpegdec_gray_memory(const uint8_t *data, int size, unsigned divisor, JPEG_DRAW_CALLBACK *draw, void *user) {
    if (!data || size <= 0 || !draw) return 0;
    JPEGDEC *jpeg = new (std::nothrow) JPEGDEC();
    if (!jpeg) return 0;
    int ok = 0;
    if (jpeg->openRAM(const_cast<uint8_t *>(data), size, draw)) {
        const int scale = divisor == 8 ? JPEG_SCALE_EIGHTH : divisor == 4 ? JPEG_SCALE_QUARTER :
                          divisor == 2 ? JPEG_SCALE_HALF : 0;
        jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
        jpeg->setUserPointer(user);
        ok = jpeg->decode(0, 0, scale) != 0;
    }
    jpeg->close();
    delete jpeg;
    return ok;
}


typedef struct { FILE *file; bool (*cancelled)(void *); void *context; } cancellable_file_t;
static int32_t file_read(JPEGFILE *file, uint8_t *buf, int32_t size) {
    if (!file || !file->fHandle || size < 0) return 0;
    cancellable_file_t *input = (cancellable_file_t *)file->fHandle;
    if (input->cancelled && input->cancelled(input->context)) return 0;
    int32_t got = (int32_t)fread(buf, 1, (size_t)size, input->file);
    file->iPos += got;
    return got;
}
static int32_t file_seek(JPEGFILE *file, int32_t pos) {
    if (!file || !file->fHandle || pos < 0 || pos > file->iSize) return -1;
    cancellable_file_t *input = (cancellable_file_t *)file->fHandle;
    if ((input->cancelled && input->cancelled(input->context)) || fseek(input->file,pos,SEEK_SET)) return -1;
    file->iPos = pos;
    return pos;
}
// C 调用方持有文件及目标平面；JPEGDEC 保持 MCU 级缓冲，不读入整个文件。
// The C caller owns the file and destination plane; JPEGDEC only buffers MCUs.
int jpegdec_gray_file_cancel(FILE *file, int size, unsigned source_w, unsigned source_h,
                     unsigned out_w, unsigned out_h, bool progressive,
                     JPEG_DRAW_CALLBACK *draw, void *user, unsigned *scaled_w, unsigned *scaled_h,
                     bool (*cancelled)(void *), void *context) {
    if (!file || size <= 0 || !source_w || !source_h || !draw) return 0;
    unsigned divisor = 1;
    if (progressive) divisor = 8;
    else while (divisor < 8 && source_w / (divisor * 2) >= out_w && source_h / (divisor * 2) >= out_h) divisor *= 2;
    *scaled_w = (source_w + divisor - 1) / divisor;
    *scaled_h = (source_h + divisor - 1) / divisor;
    JPEGDEC *jpeg = new (std::nothrow) JPEGDEC();
    if (!jpeg) return 0;
    int ok = 0;
    cancellable_file_t input = {file,cancelled,context};
    if (jpeg->open(&input, size, nullptr, file_read, file_seek, draw)) {
        jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
        jpeg->setUserPointer(user);
        int option = divisor == 8 ? JPEG_SCALE_EIGHTH : divisor == 4 ? JPEG_SCALE_QUARTER : divisor == 2 ? JPEG_SCALE_HALF : 0;
        ok = jpeg->decode(0, 0, option) != 0;
    }
    jpeg->close();
    delete jpeg;
    return ok;
}

}  // extern "C"
