#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实异步工作任务的复制、取消、替换与释放回归。
English: Exercise the actual worker's copy, cancellation, replacement and cleanup.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = r'''
#pragma once
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct host_sem *SemaphoreHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreGive(SemaphoreHandle_t sem);
int xSemaphoreTake(SemaphoreHandle_t sem, uint32_t timeout);
void vSemaphoreDelete(SemaphoreHandle_t sem);
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, int priority, void *handle);
void vTaskDelete(void *handle);
'''
UNIT = r'''
#include "freertos/FreeRTOS.h"
#include "reader_dictionary.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct host_sem { pthread_mutex_t lock; pthread_cond_t ready; bool signalled; };
static atomic_int tasks, semaphores, mode, entered;
static bool fail_sem, fail_task;
bool book_dictionary_path(const char *root, char *path, size_t capacity) {
    assert(!strcmp(root, "/sdcard/dictionaries"));
    snprintf(path, capacity, "%s/chinese.mgdict", root); return true;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (fail_sem) return NULL;
    struct host_sem *s = calloc(1, sizeof(*s)); assert(s);
    pthread_mutex_init(&s->lock, NULL); pthread_cond_init(&s->ready, NULL);
    atomic_fetch_add(&semaphores, 1); return s;
}
int xSemaphoreGive(SemaphoreHandle_t s) {
    pthread_mutex_lock(&s->lock); s->signalled = true;
    pthread_cond_signal(&s->ready); pthread_mutex_unlock(&s->lock); return pdTRUE;
}
int xSemaphoreTake(SemaphoreHandle_t s, uint32_t timeout) {
    pthread_mutex_lock(&s->lock);
    while (!s->signalled && timeout) pthread_cond_wait(&s->ready, &s->lock);
    bool ready = s->signalled; s->signalled = false;
    pthread_mutex_unlock(&s->lock); return ready;
}
void vSemaphoreDelete(SemaphoreHandle_t s) {
    pthread_cond_destroy(&s->ready); pthread_mutex_destroy(&s->lock); free(s);
    atomic_fetch_sub(&semaphores, 1);
}
struct start { void (*fn)(void *); void *arg; };
static void *run(void *context) {
    struct start value = *(struct start *)context; free(context); value.fn(value.arg); abort();
}
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, int priority, void *handle) {
    (void)name; (void)priority; (void)handle; assert(stack == 6144);
    if (fail_task) return 0;
    struct start *value = malloc(sizeof(*value)); assert(value);
    *value = (struct start){fn, arg}; pthread_t thread;
    atomic_fetch_add(&tasks, 1);
    assert(!pthread_create(&thread, NULL, run, value)); pthread_detach(thread); return pdPASS;
}
void vTaskDelete(void *handle) { assert(!handle); atomic_fetch_sub(&tasks, 1); pthread_exit(NULL); }
book_dictionary_status_t book_dictionary_lookup(const char *path, const char *query,
    book_dictionary_result_t *result, book_dictionary_cancel_fn cancelled, void *context) {
    assert(!strcmp(path, "/sdcard/dictionaries/chinese.mgdict"));
    atomic_fetch_add(&entered, 1);
    while (atomic_load(&mode) && !cancelled(context)) usleep(100);
    usleep(1000);
    result->status = cancelled(context) ? BOOK_DICTIONARY_CANCELLED : BOOK_DICTIONARY_OK;
    snprintf(result->query, sizeof(result->query), "%s", query);
    snprintf(result->definition, sizeof(result->definition), "释义：%s", query);
    return result->status;
}
static void wait_entered(int target) {
    for (unsigned i = 0; atomic_load(&entered) < target; ++i) { assert(i < 10000); usleep(100); }
}
static void wait_idle(void) {
    for (unsigned i = 0; atomic_load(&tasks); ++i) { assert(i < 10000); usleep(100); }
    assert(!atomic_load(&semaphores));
}
int main(void) {
    book_dictionary_result_t result;
    assert(!reader_dictionary_start(NULL)); assert(!reader_dictionary_start(""));
    char too_long[130]; memset(too_long, 'a', sizeof(too_long)); too_long[129] = 0;
    assert(!reader_dictionary_start(too_long));
    fail_sem = true; assert(!reader_dictionary_start("踽")); fail_sem = false;
    fail_task = true; assert(!reader_dictionary_start("踽")); fail_task = false; wait_idle();
    char borrowed[] = "踽踽独行";
    assert(reader_dictionary_start(borrowed)); memset(borrowed, 'x', sizeof(borrowed) - 1);
    for (unsigned i = 0; !reader_dictionary_poll(&result); ++i) { assert(i < 10000); usleep(100); }
    assert(result.status == BOOK_DICTIONARY_OK && !strcmp(result.query, "踽踽独行"));
    assert(!reader_dictionary_poll(&result)); wait_idle();
    for (unsigned i = 0; i < 100; ++i) {
        atomic_store(&mode, 1); int next = atomic_load(&entered) + 1;
        assert(reader_dictionary_start("旧查询")); wait_entered(next);
        // 新任务先回收旧任务；旧结果不允许流入新查询。/ Replacement drains the old task and drops its result.
        assert(reader_dictionary_start("𠮷")); atomic_store(&mode, 0);
        while (!reader_dictionary_poll(&result)) usleep(100);
        assert(result.status == BOOK_DICTIONARY_OK && !strcmp(result.query, "𠮷")); wait_idle();
        atomic_store(&mode, 1); next = atomic_load(&entered) + 1;
        assert(reader_dictionary_start("拔卡或离页")); wait_entered(next);
        reader_dictionary_cancel(); assert(!reader_dictionary_poll(&result)); wait_idle();
    }
    reader_dictionary_cancel();
    puts("PASS: actual worker, copied query, task/semaphore failures, 100 replacement/cancel cycles, no stale results or live handles");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
    work = Path(folder)
    (work / "freertos").mkdir()
    (work / "freertos/FreeRTOS.h").write_text(HEADER)
    for name in ("task.h", "semphr.h"):
        (work / "freertos" / name).write_text('#include "FreeRTOS.h"\n')
    (work / "test.c").write_text(UNIT)
    subprocess.run(["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-fsanitize=address,undefined", "-I" + str(work), "-I" + str(ROOT / "main/book"),
                    str(work / "test.c"), str(ROOT / "main/book/reader_dictionary.c"),
                    "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True, timeout=30)
