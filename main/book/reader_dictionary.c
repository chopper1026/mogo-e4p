/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：工作任务不接触字体、画面或阅读位置，主任务接收有界结果。
 * English: The worker never touches fonts, frames or reader position; the UI receives bounded results. */
#include "reader_dictionary.h"
#include "dictionary_package.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

typedef struct {
    atomic_bool cancelled;
    SemaphoreHandle_t done;
    char query[129];
    book_dictionary_result_t result;
} dictionary_job_t;
static dictionary_job_t *s_job;
static bool cancelled(void *context) { return atomic_load(&((dictionary_job_t *)context)->cancelled); }
static void query_task(void *context) {
    dictionary_job_t *job = context;
    char path[192];
    dictionary_package_path("/sdcard/dictionaries", path, sizeof(path));
    book_dictionary_lookup(path, job->query, &job->result, cancelled, job);
    xSemaphoreGive(job->done);
    vTaskDelete(NULL);
}
void reader_dictionary_cancel(void) {
    dictionary_job_t *job = s_job;
    if (!job) return;
    atomic_store(&job->cancelled, true);
    xSemaphoreTake(job->done, portMAX_DELAY);
    vSemaphoreDelete(job->done); free(job); s_job = NULL;
}
bool reader_dictionary_start(const char *query) {
    reader_dictionary_cancel();
    if (!query || !*query || strnlen(query, 129) > 128) return false;
    dictionary_job_t *job = calloc(1, sizeof(*job));
    if (!job) return false;
    atomic_init(&job->cancelled, false);
    job->done = xSemaphoreCreateBinary();
    if (!job->done) { free(job); return false; }
    memcpy(job->query, query, strlen(query) + 1);
    if (xTaskCreate(query_task, "dictionary", 6144, job, 3, NULL) != pdPASS) {
        vSemaphoreDelete(job->done); free(job); return false;
    }
    s_job = job; return true;
}
bool reader_dictionary_poll(book_dictionary_result_t *result) {
    dictionary_job_t *job = s_job;
    if (!job || !result || xSemaphoreTake(job->done, 0) != pdTRUE) return false;
    *result = job->result;
    vSemaphoreDelete(job->done); free(job); s_job = NULL; return true;
}
