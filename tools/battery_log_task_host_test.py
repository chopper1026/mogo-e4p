#!/usr/bin/env python3
"""中文：执行真实日志任务和交接 API，覆盖异步落盘、失败计数和在途写入门控。
English: Execute the real logger task and handoff APIs for asynchronous output, failure counters and in-flight write gating.
"""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT / "components/device_power/device_power_metalio.c"
UNIT = r'''
#include "device_power.h"
#include "battery_log.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef pthread_mutex_t portMUX_TYPE;
typedef pthread_mutex_t StaticSemaphore_t;
typedef pthread_mutex_t *SemaphoreHandle_t;
typedef void *TaskHandle_t;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) pthread_mutex_lock(p)
#define portEXIT_CRITICAL(p) pthread_mutex_unlock(p)
#define portMAX_DELAY (~0u)
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define BATT_LOG_PATH "/test/batt.log"
#define ESP_LOGI(tag, ...) test_log(__VA_ARGS__)
#define ESP_LOGW(tag, ...) test_log(__VA_ARGS__)
static batt_log_state_t s_batt_log;
static portMUX_TYPE s_batt_log_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_batt_io_storage;
static SemaphoreHandle_t s_batt_io_mutex;
static bool s_batt_io_available, s_batt_log_busy, s_batt_log_pending;
static TaskHandle_t s_batt_log_task;
static uint32_t s_batt_io_errors, s_batt_file_missed, s_batt_file_seq, s_batt_retry_request;
static uint64_t s_batt_boot_id = 123;
static char s_batt_metadata[1024]="fw=1.0.1 elf=test";
static int8_t s_batt_ic_soc;
static int64_t s_batt_ic_us;
static pthread_mutex_t notify_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notify_cond=PTHREAD_COND_INITIALIZER;
static unsigned notifications;
static bool stop;
static pthread_mutex_t output_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t output_cond=PTHREAD_COND_INITIALIZER;
static bool block_output, entered, fail_output, last_header;
static atomic_uint appends, serial_rows, metadata_rows, checkpoints;
static char last_metadata[1536];
static uint32_t written_seq[256];
static unsigned written_count;
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
static int nvs_fault;
static size_t stored_size=sizeof(batt_log_checkpoint_t);
static unsigned nvs_opens,nvs_closes;
static batt_log_checkpoint_t stored_checkpoint={.version=BATT_LOG_CHECKPOINT_VERSION,.active=1,.chain=99,
    .boot=100,.session=1,.seq=5,.wall_sec=1793415600,.covered_us=5000000,.out_ma_us=500000000};
static int nvs_open(const char *name,int mode,nvs_handle_t *h) {
    assert(!strcmp(name,"battlog") && (mode==0 || mode==1));++nvs_opens;*h=1;
    return nvs_fault==1?ESP_FAIL:ESP_OK;
}
static int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) {
    assert(h==1 && !strcmp(key,"checkpoint") && *size==sizeof(stored_checkpoint));
    memcpy(out,&stored_checkpoint,sizeof(stored_checkpoint));*size=stored_size;
    return nvs_fault==2?ESP_FAIL:ESP_OK;
}
static int nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size) {
    assert(h==1 && !strcmp(key,"checkpoint") && size==sizeof(stored_checkpoint));
    if(nvs_fault==3)return ESP_FAIL;
    memcpy(&stored_checkpoint,in,size);return ESP_OK;
}
static int nvs_commit(nvs_handle_t h) {
    assert(h==1);if(nvs_fault==4)return ESP_FAIL;atomic_fetch_add(&checkpoints,1);return ESP_OK;
}
static void nvs_close(nvs_handle_t h){assert(h==1);++nvs_closes;}

static int64_t esp_timer_get_time(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static void vTaskDelay(unsigned ticks) { usleep(ticks*1000); }
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p) {
    assert(!pthread_mutex_init(p,NULL));return p;
}
static int xSemaphoreTake(SemaphoreHandle_t p,unsigned ticks) {
    if(ticks==portMAX_DELAY){assert(!pthread_mutex_lock(p));return pdTRUE;}
    int64_t deadline=esp_timer_get_time()+(int64_t)ticks*1000;
    do {if(!pthread_mutex_trylock(p))return pdTRUE;usleep(1000);}
    while(esp_timer_get_time()<deadline);
    return 0;
}
static int xSemaphoreGive(SemaphoreHandle_t p) { assert(!pthread_mutex_unlock(p));return pdTRUE; }
static void xTaskNotifyGive(TaskHandle_t task) {
    assert(task);pthread_mutex_lock(&notify_lock);++notifications;
    pthread_cond_signal(&notify_cond);pthread_mutex_unlock(&notify_lock);
}
static unsigned ulTaskNotifyTake(int clear,unsigned ticks) {
    assert(clear==pdTRUE && ticks==portMAX_DELAY);
    pthread_mutex_lock(&notify_lock);
    while(!notifications && !stop)pthread_cond_wait(&notify_cond,&notify_lock);
    unsigned result=notifications;notifications=0;bool end=stop;
    pthread_mutex_unlock(&notify_lock);
    if(end)pthread_exit(NULL);
    return result;
}
static void test_log(const char *fmt,...) {
    if(strstr(fmt,"batt-meta:"))atomic_fetch_add(&metadata_rows,1);
    else if(strstr(fmt,"batt:"))atomic_fetch_add(&serial_rows,1);
}
static bool test_append(batt_log_file_t *file,const char *path,const char *backup,
    long limit,uint32_t session,const char *metadata,const char *line) {
    assert(!strcmp(path,"/test/batt.log") && !strcmp(backup,"/test/batt.log.1"));
    assert(limit==BATT_LOG_FILE_LIMIT && session && strstr(metadata,"elf=test") && strstr(line,"fmt=3"));
    pthread_mutex_lock(&output_lock);entered=true;pthread_cond_broadcast(&output_cond);
    while(block_output)pthread_cond_wait(&output_cond,&output_lock);
    last_header=file->header;
    bool ok=!fail_output;file->header=!ok;
    if(ok){assert(written_count<256);written_seq[written_count++]=(uint32_t)strtoul(strstr(line," seq=")+5,NULL,10);}
    snprintf(last_metadata,sizeof(last_metadata),"%s",metadata);
    atomic_fetch_add(&appends,1);pthread_mutex_unlock(&output_lock);
    return ok;
}
'''
for name in ("batt_log_wall", "batt_log_notify", "batt_log_io_mutex",
             "device_power_batt_log_storage", "device_power_set_batt_log",
             "device_power_batt_log_event", "device_power_batt_log_flush", "batt_log_session_metadata",
             "batt_log_checkpoint_load", "batt_log_checkpoint_save", "batt_log_undervoltage"):
    UNIT += function(name, BACKEND) + "\n"
UNIT += "#define batt_log_append test_append\n" + function("batt_log_task", BACKEND)
UNIT += r'''
#undef batt_log_append
static void *worker(void *arg) { batt_log_task(arg);return NULL; }
int main(void) {
    batt_log_checkpoint_t cp=stored_checkpoint, out;
    assert(batt_log_checkpoint_load(&out));
    for(int f=1;f<=2;f++){nvs_fault=f;assert(!batt_log_checkpoint_load(&out));}
    nvs_fault=0;stored_size--;assert(!batt_log_checkpoint_load(&out));stored_size++;
    stored_checkpoint.version++;assert(!batt_log_checkpoint_load(&out));stored_checkpoint=cp;
    for(int f=1;f<=4;f++){if(f==2)continue;nvs_fault=f;assert(!batt_log_checkpoint_save(&cp));}
    nvs_fault=0;assert(batt_log_checkpoint_save(&cp));
    cp.active=2;unsigned opens=nvs_opens;assert(!batt_log_checkpoint_save(&cp) && opens==nvs_opens);
    cp.active=1;assert(nvs_closes<nvs_opens);
    pthread_t task;s_batt_log_task=(void *)1;
    assert(!pthread_create(&task,NULL,worker,NULL));
    device_power_set_batt_log(true);
    assert(!device_power_batt_log_flush(20));
    assert(!atomic_load(&appends) && atomic_load(&serial_rows)==1 && !s_batt_file_missed);
    assert(device_power_batt_log_storage(true,100));
    assert(device_power_batt_log_flush(2000));
    assert(atomic_load(&appends)==1 && atomic_load(&metadata_rows)==1);
    assert(s_batt_ic_soc==-1 && s_batt_ic_us==-1);
    assert(strstr(last_metadata,"chain=0000000000000063 resume=1") && strstr(last_metadata,"prev_seq=5"));
    assert(written_count==1 && written_seq[0]==1 && s_batt_file_seq==1);
    pthread_mutex_lock(&output_lock);block_output=true;entered=false;pthread_mutex_unlock(&output_lock);
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_RESTART);
    pthread_mutex_lock(&output_lock);
    while(!entered)pthread_cond_wait(&output_cond,&output_lock);
    pthread_mutex_unlock(&output_lock);
    assert(!device_power_batt_log_storage(false,10));
    assert(!device_power_batt_log_flush(10));
    pthread_mutex_lock(&output_lock);block_output=false;pthread_cond_broadcast(&output_cond);pthread_mutex_unlock(&output_lock);
    assert(device_power_batt_log_flush(2000));
    unsigned before=atomic_load(&appends);
    pthread_mutex_lock(&output_lock);block_output=true;entered=false;pthread_mutex_unlock(&output_lock);
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_RESTART);
    pthread_mutex_lock(&output_lock);
    while(!entered)pthread_cond_wait(&output_cond,&output_lock);
    pthread_mutex_unlock(&output_lock);
    int64_t trigger=esp_timer_get_time();
    batt_log_undervoltage(trigger,3090,true,-88,0);
    assert(esp_timer_get_time()-trigger<100000); // protection capture cannot wait for blocked TF
    portENTER_CRITICAL(&s_batt_log_mux);
    batt_log_record_t uv=s_batt_log.queue[(s_batt_log.head+s_batt_log.count-1)%BATT_LOG_QUEUE_SIZE];
    portEXIT_CRITICAL(&s_batt_log_mux);
    assert((uv.events&BATT_LOG_UNDERVOLTAGE) && uv.sample.raw_mv==3090 && uv.sample.ma==-88);
    assert(uv.sample.up_us>=trigger && uv.sample.up_us-trigger<100000 && !uv.sample.have_filter);
    pthread_mutex_lock(&output_lock);block_output=false;pthread_cond_broadcast(&output_cond);pthread_mutex_unlock(&output_lock);
    assert(device_power_batt_log_flush(2000));before=atomic_load(&appends);
    assert(device_power_batt_log_storage(false,100));
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_LOCK);
    assert(!device_power_batt_log_flush(20));
    assert(atomic_load(&appends)==before && !s_batt_file_missed && !s_batt_io_errors);
    unsigned serial_before=atomic_load(&serial_rows), checkpoint_before=atomic_load(&checkpoints);
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_OFF);
    assert(!device_power_batt_log_flush(20));
    assert(atomic_load(&serial_rows)==serial_before+1 && atomic_load(&checkpoints)>checkpoint_before);
    assert(device_power_batt_log_storage(true,100));
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_WAKE);
    assert(device_power_batt_log_flush(2000));
    pthread_mutex_lock(&output_lock);fail_output=true;pthread_mutex_unlock(&output_lock);
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_OFF);
    assert(!device_power_batt_log_flush(20));assert(s_batt_io_errors>=1 && !s_batt_file_missed);
    unsigned failures=s_batt_io_errors;
    usleep(50000);assert(s_batt_io_errors==failures); // no autonomous retry timer
    pthread_mutex_lock(&output_lock);fail_output=false;pthread_mutex_unlock(&output_lock);
    device_power_set_batt_log(false);assert(device_power_batt_log_flush(2000));
    assert(written_count==s_batt_file_seq);
    for(unsigned i=0;i<written_count;i++)assert(written_seq[i]==i+1);
    before=atomic_load(&appends);
    device_power_set_batt_log(false);device_power_batt_log_event(DEVICE_POWER_BATT_LOG_OFF);
    assert(device_power_batt_log_flush(2000) && atomic_load(&appends)==before);
    device_power_set_batt_log(true);assert(device_power_batt_log_flush(2000));
    assert(atomic_load(&metadata_rows)==2 && s_batt_log.session==2);
    device_power_set_batt_log(false);assert(device_power_batt_log_flush(2000));
    assert(strstr(last_metadata,"resume=0") && !strstr(last_metadata,"chain=0000000000000063"));
    assert(device_power_batt_log_storage(false,100));
    device_power_set_batt_log(true);assert(!device_power_batt_log_flush(20));
    for(unsigned i=0;i<20;i++){
        device_power_batt_log_event(DEVICE_POWER_BATT_LOG_LOCK);
        assert(!device_power_batt_log_flush(5));
    }
    assert(s_batt_file_missed>0);
    assert(device_power_batt_log_storage(true,100));
    usleep(50000);
    assert(!device_power_batt_log_flush(50)); // loss is never reported as a complete flush
    assert(!s_batt_log_pending && !s_batt_log.count);
    assert(stored_checkpoint.session==3 && stored_checkpoint.chain!=99);
    pthread_mutex_lock(&notify_lock);stop=true;pthread_cond_signal(&notify_cond);pthread_mutex_unlock(&notify_lock);
    assert(!pthread_join(task,NULL));
    puts("PASS: actual logger task, session metadata, async handoff, in-flight I/O/flush timeout, TF ownership, serial fallback, failure counters and disabled silence");
}
'''

with tempfile.TemporaryDirectory(prefix="battery-log-task-") as folder:
    work = Path(folder)
    env, compiler = toolchain(work)
    (work / "driver").mkdir()
    (work / "driver/i2c_master.h").write_text("#pragma once\ntypedef void *i2c_master_bus_handle_t;\n")
    (work / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n")
    source, binary = work / "test.c", work / "test"
    source.write_text(UNIT)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-fsanitize=address,undefined", "-I" + str(work),
        "-I" + str(ROOT / "components/device_power/include"),
        "-I" + str(ROOT / "components/device_power"), str(source),
        str(ROOT / "components/device_power/battery_log.c"), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary)], cwd=work, env=env, check=True, timeout=20)
