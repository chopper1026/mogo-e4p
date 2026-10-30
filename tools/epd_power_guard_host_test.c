/* 实际关电任务使用pthread替代RTOS边界，不替换供电策略。/ Replace RTOS boundaries with pthreads while retaining the actual power policy. */
#include "epd_power_guard.h"
#include "epd_board.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct HostSem { pthread_mutex_t lock; pthread_cond_t cond; unsigned value; };
struct HostTask {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned notifications;
    void (*entry)(void*);
    void* arg;
};
struct HostTimer {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool armed, executing, closing;
    int64_t deadline;
    esp_timer_create_args_t args;
};
static _Thread_local TaskHandle_t current_task;
static TaskHandle_t latest_task;
static atomic_bool physical_on, scanning;
static atomic_uint on_calls, off_calls, attempts, failures, releases;
static atomic_int_fast64_t last_off_us, first_attempt_us;
static epd_ctrl_state_t state;

int64_t esp_timer_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void* timer_task(void* arg) {
    esp_timer_handle_t timer = arg;
    pthread_mutex_lock(&timer->lock);
    while (!timer->closing) {
        if (!timer->armed) { pthread_cond_wait(&timer->cond, &timer->lock); continue; }
        int64_t remaining = timer->deadline - esp_timer_get_time();
        if (remaining > 0) {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += remaining / 1000000;
            until.tv_nsec += (remaining % 1000000) * 1000;
            if (until.tv_nsec >= 1000000000) { ++until.tv_sec; until.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&timer->cond, &timer->lock, &until);
            continue;
        }
        timer->armed = false; timer->executing = true;
        pthread_mutex_unlock(&timer->lock);
        timer->args.callback(timer->args.arg);
        pthread_mutex_lock(&timer->lock);
        timer->executing = false; pthread_cond_broadcast(&timer->cond);
    }
    pthread_mutex_unlock(&timer->lock);
    return NULL;
}
int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out) {
    esp_timer_handle_t timer = calloc(1, sizeof(*timer)); assert(timer);
    pthread_mutex_init(&timer->lock, NULL); pthread_cond_init(&timer->cond, NULL);
    timer->args = *args; *out = timer;
    assert(!pthread_create(&timer->thread, NULL, timer_task, timer));
    return ESP_OK;
}
int esp_timer_start_once_at(esp_timer_handle_t timer, uint64_t deadline) {
    pthread_mutex_lock(&timer->lock);
    int result = timer->armed ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (!result && deadline <= (uint64_t)esp_timer_get_time()) result = ESP_ERR_INVALID_ARG;
    if (!result) { timer->deadline = deadline; timer->armed = true; pthread_cond_broadcast(&timer->cond); }
    pthread_mutex_unlock(&timer->lock);
    return result;
}
int esp_timer_stop(esp_timer_handle_t timer) {
    pthread_mutex_lock(&timer->lock);
    int result = timer->armed ? ESP_OK : ESP_ERR_INVALID_STATE;
    timer->armed = false; pthread_cond_broadcast(&timer->cond);
    pthread_mutex_unlock(&timer->lock);
    return result;
}
int esp_timer_stop_blocking(esp_timer_handle_t timer, uint32_t ticks) {
    assert(ticks == portMAX_DELAY);
    pthread_mutex_lock(&timer->lock);
    timer->armed = false; pthread_cond_broadcast(&timer->cond);
    while (timer->executing) pthread_cond_wait(&timer->cond, &timer->lock);
    pthread_mutex_unlock(&timer->lock);
    return ESP_OK;
}
int esp_timer_delete(esp_timer_handle_t timer) {
    pthread_mutex_lock(&timer->lock);
    assert(!timer->armed && !timer->executing);
    timer->closing = true; pthread_cond_broadcast(&timer->cond);
    pthread_mutex_unlock(&timer->lock);
    assert(!pthread_join(timer->thread, NULL));
    pthread_mutex_destroy(&timer->lock); pthread_cond_destroy(&timer->cond); free(timer);
    return ESP_OK;
}
void host_log(const char* tag, const char* fmt, ...) { (void)tag; (void)fmt; }

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
    SemaphoreHandle_t sem = calloc(1, sizeof(*sem)); assert(sem);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&sem->lock, &attr);
    pthread_mutexattr_destroy(&attr);
    pthread_cond_init(&sem->cond, NULL);
    return sem;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xSemaphoreCreateRecursiveMutex(); }
int xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t wait) {
    assert(wait == portMAX_DELAY); return pthread_mutex_lock(&sem->lock) == 0;
}
int xSemaphoreGiveRecursive(SemaphoreHandle_t sem) { return pthread_mutex_unlock(&sem->lock) == 0; }
int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait) {
    assert(wait == portMAX_DELAY);
    pthread_mutex_lock(&sem->lock);
    while (!sem->value) pthread_cond_wait(&sem->cond, &sem->lock);
    sem->value = 0;
    pthread_mutex_unlock(&sem->lock);
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t sem) {
    pthread_mutex_lock(&sem->lock); sem->value = 1;
    pthread_cond_signal(&sem->cond); pthread_mutex_unlock(&sem->lock);
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t sem) {
    pthread_mutex_destroy(&sem->lock); pthread_cond_destroy(&sem->cond); free(sem);
}
static void* start_task(void* arg) {
    current_task = arg;
    current_task->entry(current_task->arg);
    return NULL;
}
BaseType_t xTaskCreate(void (*entry)(void*), const char* name, unsigned stack, void* arg,
                       unsigned priority, TaskHandle_t* result) {
    assert(!strcmp(name, "epd_power") && stack >= 2048 && priority >= 20);
    TaskHandle_t task = calloc(1, sizeof(*task)); assert(task);
    pthread_mutex_init(&task->lock, NULL); pthread_cond_init(&task->cond, NULL);
    task->entry = entry; task->arg = arg;
    *result = latest_task = task;
    assert(pthread_create(&task->thread, NULL, start_task, task) == 0);
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) {
    pthread_mutex_lock(&task->lock); ++task->notifications;
    pthread_cond_signal(&task->cond); pthread_mutex_unlock(&task->lock);
}
unsigned ulTaskNotifyTake(int clear, TickType_t wait) {
    assert(clear && wait == portMAX_DELAY);
    TaskHandle_t task = current_task;
    pthread_mutex_lock(&task->lock);
    if (wait == portMAX_DELAY) {
        while (!task->notifications) pthread_cond_wait(&task->cond, &task->lock);
    } else {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += wait / 1000;
        until.tv_nsec += (long)(wait % 1000) * 1000000;
        if (until.tv_nsec >= 1000000000) { ++until.tv_sec; until.tv_nsec -= 1000000000; }
        while (!task->notifications)
            if (pthread_cond_timedwait(&task->cond, &task->lock, &until)) break;
    }
    unsigned result = task->notifications;
    task->notifications = 0;
    pthread_mutex_unlock(&task->lock);
    return result;
}
void vTaskDelete(TaskHandle_t task) { assert(!task); pthread_exit(NULL); }
void vTaskDelay(TickType_t ticks) { usleep(ticks * 1000); }

static void raw_on(epd_ctrl_state_t* ctrl) {
    assert(ctrl == &state && !scanning && !physical_on); physical_on = true; ++on_calls;
}
static bool raw_off(epd_ctrl_state_t* ctrl) {
    assert(ctrl == &state && !scanning);
    if (atomic_fetch_add(&attempts, 1) == 0) first_attempt_us = esp_timer_get_time();
    if (failures) { --failures; return false; }
    last_off_us = esp_timer_get_time(); physical_on = false; ++off_calls;
    return true;
}
static void raw_release(void) { assert(physical_on && !scanning); ++releases; }
static const EpdBoardDefinition board = {.poweron=raw_on, .poweroff_checked=raw_off,
                                         .release_drive=raw_release};
const EpdBoardDefinition* epd_current_board(void) { return &board; }
epd_ctrl_state_t* epd_ctrl_state(void) { return &state; }
void epd_set_mode(bool mode) { (void)mode; assert(false); }

static void wait_off(unsigned count) {
    int64_t limit = esp_timer_get_time() + 1500000;
    while (off_calls < count && esp_timer_get_time() < limit) usleep(1000);
    assert(off_calls == count && !physical_on);
}
static void join_worker(void) {
    assert(pthread_join(latest_task->thread, NULL) == 0);
    pthread_mutex_destroy(&latest_task->lock); pthread_cond_destroy(&latest_task->cond);
    free(latest_task); latest_task = NULL;
}

static atomic_bool competing_scan;
static void *scan_during_flash(void *arg) {
    (void)arg;
    epd_power_guard_scan_begin();
    competing_scan = true;
    epd_power_guard_scan_end();
    return NULL;
}

int main(void) {
    epd_power_guard_init();
    // 主线程阻塞，任务仍独立关电；重复上电和旧通知不能延长期限。
    // The worker shuts down while the main thread blocks; repeated on calls and stale notices cannot extend the deadline.
    int64_t start = esp_timer_get_time();
    epd_power_guard_on(); usleep(40000); epd_power_guard_on();
    xTaskNotifyGive(latest_task); usleep(300000);
    wait_off(1);
    assert(last_off_us - start >= 100000 && last_off_us - start < 150000);
    assert(on_calls == 1 && releases == 1);
    printf("blocked caller: shutdown after %lld us\n", (long long)(last_off_us-start));

    // 扫描跨过旧期限不准断电；最后一轮结束后重新计时，包含嵌套与过期通知。
    // A scan crossing the old deadline cannot lose power; restart timing after the last scan, including nesting and stale notices.
    epd_power_guard_on(); usleep(70000);
    epd_power_guard_scan_begin(); scanning = true;
    xTaskNotifyGive(latest_task); usleep(180000);
    assert(physical_on && off_calls == 1);
    scanning = false; epd_power_guard_scan_end(); usleep(40000);
    epd_power_guard_scan_begin(); epd_power_guard_scan_begin();
    scanning = true; usleep(140000); assert(off_calls == 1 && physical_on);
    scanning = false; epd_power_guard_scan_end();
    scanning = true; usleep(130000); assert(off_calls == 1 && physical_on);
    scanning = false; start = esp_timer_get_time(); epd_power_guard_scan_end();
    wait_off(2);
    assert(last_off_us - start >= 100000 && last_off_us - start < 150000);

    // 关断命令失败保留带电状态，独立重试而不依赖下一次扫描。
    // Failed shutdown commands retain powered state and retry independently of another scan.
    failures = 2; attempts = 0;
    epd_power_guard_scan_begin(); start = esp_timer_get_time(); epd_power_guard_scan_end();
    wait_off(3);
    assert(attempts == 3 && first_attempt_us - start >= 100000);
    assert(last_off_us - first_attempt_us >= 10000);
    epd_power_guard_on(); failures = 1; attempts = 0;
    epd_power_guard_off(); assert(physical_on);
    wait_off(4); assert(attempts == 2);

    // 失败后立即刷新必须确认关断并重新上电，不能复用“可能带电”的状态。
    // An immediate scan after failure must confirm off and repower instead of reusing uncertain power.
    epd_power_guard_on(); failures = 2; attempts = 0;
    epd_power_guard_off(); assert(physical_on);
    epd_power_guard_scan_begin();
    assert(physical_on && !failures && off_calls == 5 && attempts == 3);
    scanning = true; usleep(1000); scanning = false;
    epd_power_guard_scan_end();
    epd_power_guard_off(); wait_off(6);

    // 显式关电取消旧期限；反复销毁重建，不允许访问已释放的板级资源。
    // Explicit off cancels old deadlines; repeated teardown/reinit must not access released board resources.
    epd_power_guard_on(); epd_power_guard_off(); wait_off(7);
    usleep(130000); assert(off_calls == 7);
    epd_power_guard_deinit(); join_worker();
    for (unsigned i=0; i<10; ++i) {
        epd_power_guard_init(); epd_power_guard_on();
        epd_power_guard_deinit(); join_worker();
    }
    assert(!physical_on && off_calls == 17);
    // 完整显示组不提前上电；嵌套结束只关一次，组内阻塞仍受100ms保护。
    // Display groups do not power early; nested ends shut down once, and blocking within a group retains the 100 ms fallback.
    epd_power_guard_init();
    unsigned before_on = on_calls, before_off = off_calls;
    epd_power_guard_session_begin(); epd_power_guard_session_begin();
    usleep(130000); assert(on_calls == before_on && !physical_on);
    epd_power_guard_scan_begin(); epd_power_guard_scan_end();
    assert(on_calls == before_on + 1 && physical_on);
    epd_power_guard_session_end(); assert(physical_on && off_calls == before_off);
    start = esp_timer_get_time(); epd_power_guard_session_end();
    assert(!physical_on && off_calls == before_off + 1 && last_off_us - start < 20000);
    epd_power_guard_session_begin();
    epd_power_guard_scan_begin(); epd_power_guard_scan_end();
    wait_off(before_off + 2);
    epd_power_guard_session_end(); assert(off_calls == before_off + 2);

    // 写Flash先确认关电；关断失败拒绝写入，写入期间其他扫描必须等待。
    // Flash access confirms off first, rejects failed shutdown and excludes concurrent scans throughout the write.
    epd_power_guard_scan_begin();
    assert(!epd_power_guard_flash_begin() && physical_on);
    epd_power_guard_scan_end(); failures = 1;
    assert(!epd_power_guard_flash_begin() && physical_on);
    assert(!epd_power_guard_scan_allowed());
    wait_off(before_off + 3); assert(epd_power_guard_scan_allowed());
    epd_power_guard_scan_begin(); epd_power_guard_scan_end();
    assert(epd_power_guard_flash_begin() && !physical_on);
    assert(epd_power_guard_flash_begin()); epd_power_guard_flash_end();
    pthread_t competitor;
    assert(!pthread_create(&competitor, NULL, scan_during_flash, NULL));
    usleep(130000); assert(!competing_scan && !physical_on);
    epd_power_guard_flash_end();
    assert(!pthread_join(competitor, NULL) && competing_scan && physical_on);
    epd_power_guard_off();
    epd_power_guard_deinit(); join_worker();
    assert(epd_power_guard_flash_begin()); epd_power_guard_flash_end();
    puts("PASS: actual worker deadlines, blocked caller, active/nested scans, stale notices, retry, explicit off and teardown");
}
