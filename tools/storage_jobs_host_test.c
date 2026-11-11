/*
 * 中文：编译真实交还服务，验证短路顺序与每个任务独立接收完整超时值。
 * English: Compile the real handoff service and verify short-circuit order and full per-job timeouts.
 */
#include "storage_jobs.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static bool image_ok, cover_ok, avatar_ok;
static unsigned expected_timeout, calls;

bool lock_images_quiesce(unsigned timeout_ms) {
    assert(timeout_ms==expected_timeout && calls++==0);return image_ok;
}

bool book_cover_jobs_quiesce(unsigned timeout_ms) {
    assert(timeout_ms == expected_timeout && calls++ == 1);
    return cover_ok;
}

bool ui_avatar_quiesce(unsigned timeout_ms) {
    assert(timeout_ms == expected_timeout && calls++ == 2);
    return avatar_ok;
}

int main(void) {
    const unsigned timeouts[] = {0, 1, 2000, UINT_MAX};
    for (unsigned i = 0; i < sizeof(timeouts) / sizeof(timeouts[0]); ++i) {
        expected_timeout = timeouts[i];
        for(unsigned image=0;image<2;++image)
        for (unsigned cover = 0; cover < 2; ++cover) {
            for (unsigned avatar = 0; avatar < 2; ++avatar) {
                image_ok=image;
                cover_ok = cover;
                avatar_ok = avatar;
                calls = 0;
                assert(storage_jobs_quiesce(expected_timeout) == (image_ok && cover_ok && avatar_ok));
                assert(calls == (!image_ok?1u:cover_ok?3u:2u));
            }
        }
    }
    puts("PASS: lock images before cover and avatar, short-circuit failure, repeated handoffs and per-job timeout propagation");
}
