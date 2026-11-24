/*
 * 中文：编译真实交还服务，验证短路顺序与每个任务独立接收完整超时值。
 * English: Compile the real handoff service and verify short-circuit order and full per-job timeouts.
 */
#include "storage_jobs.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static bool shutdown_ok, image_ok, cover_ok, avatar_ok, library_ok;
static unsigned expected_timeout, calls;

static bool font_ok=true;
bool font_download_quiesce(unsigned timeout_ms) {assert(timeout_ms==expected_timeout);return font_ok;}
bool shutdown_wallpaper_quiesce(unsigned timeout_ms) {
    assert(timeout_ms==expected_timeout && calls++==0);return shutdown_ok;
}

bool lock_images_quiesce(unsigned timeout_ms) {
    assert(timeout_ms==expected_timeout && calls++==1);return image_ok;
}

bool book_cover_jobs_quiesce(unsigned timeout_ms) {
    assert(timeout_ms == expected_timeout && calls++ == 2);
    return cover_ok;
}

bool ui_avatar_quiesce(unsigned timeout_ms) {
    assert(timeout_ms == expected_timeout && calls++ == 3);
    return avatar_ok;
}

bool home_library_quiesce(unsigned timeout_ms) {
    assert(timeout_ms == expected_timeout && calls++ == 4);
    return library_ok;
}

int main(void) {
    const unsigned timeouts[] = {0, 1, 2000, UINT_MAX};
    for (unsigned i = 0; i < sizeof(timeouts) / sizeof(timeouts[0]); ++i) {
        expected_timeout = timeouts[i];
        for(unsigned shutdown=0;shutdown<2;++shutdown)
        for(unsigned image=0;image<2;++image)
        for (unsigned cover = 0; cover < 2; ++cover) {
            for (unsigned avatar = 0; avatar < 2; ++avatar)
            for (unsigned library = 0; library < 2; ++library) {
                shutdown_ok=shutdown;image_ok=image;
                cover_ok = cover;
                avatar_ok = avatar;
                library_ok = library;
                calls = 0;
                assert(storage_jobs_quiesce(expected_timeout) == (shutdown_ok && image_ok && cover_ok && avatar_ok && library_ok));
                assert(calls == (!shutdown_ok?1u:!image_ok?2u:!cover_ok?3u:avatar_ok?5u:4u));
            }
        }
    }
    calls=0;font_ok=false;assert(!storage_jobs_quiesce(expected_timeout) && !calls);
    puts("PASS: shutdown art and lock images before cover, avatar and mini-library, short-circuit failure, repeated handoffs and per-job timeout propagation");
}
