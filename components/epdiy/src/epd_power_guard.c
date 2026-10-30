/*
 * 独立高优先级任务负责100ms空闲关电。仅真实扫描持有供电锁，耗时绘制不续期。
 * A dedicated high-priority task owns the 100 ms idle shutdown. Only physical scans hold the power lock;
 * slow framebuffer preparation does not renew the deadline.
 */
#include "epd_power_guard.h"
#include "epd_board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define IDLE_US INT64_C(100000)
#define RETRY_US INT64_C(5000)
static SemaphoreHandle_t power_lock, stopped;
static TaskHandle_t worker;
static esp_timer_handle_t alarm_timer;
static bool powered, uncertain, stopping;
static unsigned scan_depth, session_depth, flash_depth;
static int64_t idle_deadline_us, wake_deadline_us;

static void lock(void) {
    if (xSemaphoreTakeRecursive(power_lock, portMAX_DELAY) != pdTRUE) abort();
}
static void unlock(void) { xSemaphoreGiveRecursive(power_lock); }

static void release_drive(void) {
    const EpdBoardDefinition* board = epd_current_board();
    if (board->release_drive) board->release_drive();
    else epd_set_mode(false);
}

static void alarm_callback(void* arg) {
    (void)arg;
    xTaskNotifyGive(worker);
}

static void schedule_alarm(int64_t deadline_us) {
    esp_err_t result = esp_timer_stop(alarm_timer);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) abort();
    if (!deadline_us) return;
    if (deadline_us > esp_timer_get_time()) {
        result = esp_timer_start_once_at(alarm_timer, deadline_us);
        if (result == ESP_OK) return;
        if (result != ESP_ERR_INVALID_ARG || deadline_us > esp_timer_get_time()) abort();
    }
    // 绝对期限已过就直接唤醒，不再等待系统节拍累计。
    // Wake immediately when the absolute deadline has passed instead of accumulating RTOS ticks.
    xTaskNotifyGive(worker);
}

static void arm(int64_t completed_us) {
    idle_deadline_us = completed_us + IDLE_US;
    wake_deadline_us = idle_deadline_us;
    schedule_alarm(wake_deadline_us);
}

static bool power_off(void);

static void power_on(void) {
    if (powered && !uncertain) return;
    // 未确认关断不能当作有效供电；先反复确认关断，再执行完整上电，禁止无效供电下扫描。
    // Unconfirmed shutdown is not ready power; confirm off before a full power-on rather than scanning with uncertain rails.
    while (uncertain) {
        if (power_off()) uncertain = powered = false;
        else vTaskDelay(pdMS_TO_TICKS(5));
    }
    // 失败也视作可能带电；板级失败策略不得继续扫描。
    // Treat failure as possibly powered; the board failure policy must prevent scanning.
    powered = true;
    epd_current_board()->poweron(epd_ctrl_state());
}

static bool power_off(void) {
    const EpdBoardDefinition* board = epd_current_board();
    if (board->poweroff_checked) return board->poweroff_checked(epd_ctrl_state());
    board->poweroff(epd_ctrl_state());
    return true;
}

static void shut_down(void) {
    const int64_t started_us = esp_timer_get_time();
    const int64_t deadline_us = idle_deadline_us;
    const bool ok = power_off();
    const int64_t completed_us = esp_timer_get_time();
    powered = !ok;
    uncertain = !ok;
    wake_deadline_us = ok ? 0 : completed_us + RETRY_US;
    if (ok) idle_deadline_us = 0;
    schedule_alarm(wake_deadline_us);
    // 诊断不参与期限计算；失败必须保留可能带电状态并重试。
    // Diagnostics do not determine deadlines; failures retain possible power and schedule a retry.
#if CONFIG_DISPLAY_POWER_DIAG
    ESP_LOGI("epd_power", "HV_IDLE deadline_us=%lld start_us=%lld done_us=%lld late_us=%lld ok=%d",
             (long long)deadline_us, (long long)started_us, (long long)completed_us,
             (long long)(deadline_us ? started_us - deadline_us : 0), ok);
#else
    (void)started_us;
    (void)deadline_us;
#endif
    if (!ok) ESP_LOGE("epd_power", "HV shutdown unconfirmed; retrying");
}

static void power_task(void* arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        lock();
        const int64_t now_us = esp_timer_get_time();
        if (wake_deadline_us && now_us >= wake_deadline_us) shut_down();
        else schedule_alarm(wake_deadline_us);
        if (stopping && !powered) {
            // 回调只通知任务，不持供电锁；先等回调退出再释放任务句柄。
            // Callbacks only notify and never take the power lock; join callbacks before releasing the task handle.
            if (esp_timer_stop_blocking(alarm_timer, portMAX_DELAY) != ESP_OK) abort();
            unlock();
            xSemaphoreGive(stopped);
            vTaskDelete(NULL);
            return;
        }
        unlock();
    }
}

void epd_power_guard_init(void) {
    assert(!power_lock && !worker);
    power_lock = xSemaphoreCreateRecursiveMutex();
    stopped = xSemaphoreCreateBinary();
    if (!power_lock || !stopped) abort();
    powered = uncertain = stopping = false;
    scan_depth = session_depth = flash_depth = 0;
    idle_deadline_us = wake_deadline_us = 0;
    const esp_timer_create_args_t args = {
        .callback = alarm_callback,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "epd_idle",
    };
    if (esp_timer_create(&args, &alarm_timer) != ESP_OK) abort();
    // 定时回调只发通知，I2C由独立任务执行；不持产品/OTA锁。分配失败禁止启用面板。
    // Timer callbacks only notify; a dedicated task performs I2C without the product/OTA lock. Allocation failure prevents panel use.
    BaseType_t result = xTaskCreate(power_task, "epd_power", 3072, NULL,
                                    configMAX_PRIORITIES - 2, &worker);
    if (result != pdPASS) abort();
}

void epd_power_guard_on(void) {
    lock();
    assert(!stopping && !flash_depth);
    if (!powered || uncertain) {
        power_on();
        release_drive();
        arm(esp_timer_get_time());
    }
    unlock();
}

void epd_power_guard_off(void) {
    lock();
    assert(scan_depth == 0);
    if (powered) shut_down();
    xTaskNotifyGive(worker);
    unlock();
}

void epd_power_guard_scan_begin(void) {
    lock();
    assert(!stopping && !flash_depth);
    ++scan_depth;
    idle_deadline_us = wake_deadline_us = 0;
    schedule_alarm(0);
    power_on();
}

void epd_power_guard_scan_end(void) {
    const int64_t completed_us = esp_timer_get_time();
    assert(scan_depth > 0);
    if (--scan_depth == 0) {
        release_drive();
        arm(completed_us);
    }
    unlock();
}

bool epd_power_guard_scan_allowed(void) {
    lock();
    const bool allowed = !stopping && !uncertain && !flash_depth;
    unlock();
    return allowed;
}

void epd_power_guard_session_begin(void) {
    lock();
    assert(!stopping && !flash_depth);
    ++session_depth;
    unlock();
}

void epd_power_guard_session_end(void) {
    lock();
    assert(session_depth && !scan_depth);
    if (!--session_depth && powered) shut_down();
    unlock();
}

bool epd_power_guard_flash_begin(void) {
    // 启动早期尚未初始化屏幕时没有高压资源；调用方须保证初始化/销毁不与写入并发。
    // Before display initialization there are no HV resources; callers serialize initialization/teardown with writes.
    if (!power_lock) return true;
    lock();
    if (stopping || scan_depth) { unlock(); return false; }
    if (powered) shut_down();
    if (powered) { unlock(); return false; }
    ++flash_depth;
    // 实际Flash操作期间持锁，防止其他任务在确认关电后重新开始扫描。
    // Hold the lock through the actual flash operation so another task cannot restart scanning after confirmed off.
    return true;
}

void epd_power_guard_flash_end(void) {
    if (!power_lock) return;
    assert(flash_depth);
    --flash_depth;
    unlock();
}

void epd_power_guard_deinit(void) {
    lock();
    assert(!scan_depth && !session_depth && !flash_depth);
    stopping = true;
    if (powered) shut_down();
    xTaskNotifyGive(worker);
    unlock();
    // 关电未确认时保持板级资源可用，独立任务继续重试。
    // Keep board resources alive while the independent worker retries an unconfirmed shutdown.
    xSemaphoreTake(stopped, portMAX_DELAY);
    if (esp_timer_delete(alarm_timer) != ESP_OK) abort();
    alarm_timer = NULL;
    vSemaphoreDelete(stopped);
    vSemaphoreDelete(power_lock);
    stopped = power_lock = NULL;
    worker = NULL;
}
