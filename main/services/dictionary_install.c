/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：HTTPS 流式写入 TF 临时目录，完整校验后启用，不接触字体和画面。
 * English: Stream HTTPS to a card staging directory and activate after verification, without drawing or fonts. */
#include "dictionary_install.h"
#include "../book/dictionary_package.h"
#include "../ota/ota_online.h"
#include "../time/time_service.h"
#include "weather_service.h"
#include "usb_storage.h"
#include "board_sd.h"
#include "wifi_transfer.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static SemaphoreHandle_t s_mutex,s_done;
static atomic_bool s_cancel;
static bool s_joinable;
static dictionary_install_status_t s_status;
static bool cancelled(void *unused) { (void)unused; return atomic_load(&s_cancel); }
static void message(const char *value) {
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    snprintf(s_status.message,sizeof(s_status.message),"%s",value); ++s_status.revision;
    xSemaphoreGive(s_mutex);
}
void dictionary_install_get_status(dictionary_install_status_t *status) {
    if(!status)return;
    memset(status,0,sizeof(*status));
    if(!s_mutex)return;
    xSemaphoreTake(s_mutex,portMAX_DELAY); *status=s_status; xSemaphoreGive(s_mutex);
}
bool dictionary_install_busy(void) {
    dictionary_install_status_t status; dictionary_install_get_status(&status); return status.busy;
}
void dictionary_install_cancel(void) {
    if(!s_joinable)return;
    atomic_store(&s_cancel,true);
    (void)wifi_transfer_cancel_time_sync();
    xSemaphoreTake(s_done,portMAX_DELAY); s_joinable=false;
}
static bool media(void) {
    board_sd_info_t info;
    int64_t deadline=esp_timer_get_time()+2000000;
    while(!cancelled(NULL)) {
        esp_err_t error=board_sd_get_info(&info);
        if(error==ESP_OK)return info.mounted;
        // 卡健康检查短暂占用探测状态，不代表介质失效。
        // A health probe briefly owns the busy state without invalidating media.
        if(error!=ESP_ERR_NOT_FINISHED || esp_timer_get_time()>=deadline)return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}
static bool online(bool *owned) {
    wifi_transfer_status_t status; wifi_transfer_get_status(&status);
    if(status.mode!=WIFI_TRANSFER_MODE_STA || !status.network_ready) {
        char ssid[33];bool saved=false;
        if(wifi_transfer_get_saved_wifi(ssid,&saved)!=ESP_OK || !saved) { message("请先在 WiFi 设置中保存网络，再点下载安装");return false; }
        wifi_transfer_stop();*owned=true;
        wifi_transfer_cfg_t cfg={.mode=WIFI_TRANSFER_MODE_STA,.network_only=true};
        if(wifi_transfer_start(&cfg)!=ESP_OK)return false;
    }
    int64_t deadline=esp_timer_get_time()+30000000;
    while(!cancelled(NULL) && esp_timer_get_time()<deadline) {
        wifi_transfer_service_poll();wifi_transfer_get_status(&status);
        if(status.mode==WIFI_TRANSFER_MODE_STA && status.network_ready) {
            if(time(NULL)<1704067200) {
                uint32_t utc;
                if(wifi_transfer_sync_time_auto(&utc)!=ESP_OK) { message("网络对时失败，请校准时间后重试");return false; }
                // 主循环从 wifi_transfer 接收对时结果并交还 PMU/NVS。
                // The main loop consumes the sync result and owns PMU/NVS handoff.
            }
            return !cancelled(NULL);
        }
        if(status.state==WIFI_TRANSFER_ERROR)break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    message("WiFi 连接失败，请检查网络后重试");return false;
}
static esp_http_client_handle_t open_http(const char *url,int64_t *length) {
    esp_http_client_config_t config={.url=url,.timeout_ms=2000,.buffer_size=4096,.buffer_size_tx=1024,
        .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client)return NULL;
    esp_http_client_set_header(client,"Accept-Encoding","identity");
    esp_http_client_set_header(client,"Cache-Control","no-store");
    esp_http_client_set_header(client,"User-Agent","mogo-dictionary/1");
    if(!cancelled(NULL) && esp_http_client_open(client,0)==ESP_OK) {
        int64_t until=esp_timer_get_time()+10000000;
        do {
            *length=esp_http_client_fetch_headers(client);
            if(*length!=-ESP_ERR_HTTP_EAGAIN)break;
        }while(!cancelled(NULL) && esp_timer_get_time()<until);
        if(!cancelled(NULL) && esp_http_client_get_status_code(client)==200 && *length>=0)return client;
    }
    esp_http_client_cleanup(client);return NULL;
}
static int read_http(esp_http_client_handle_t client,void *buffer,int capacity,int64_t *idle) {
    while(media() && esp_timer_get_time()-*idle<15000000) {
        int n=esp_http_client_read(client,buffer,capacity);
        if(n>0) {*idle=esp_timer_get_time();return n;}
        if(!n && esp_http_client_is_complete_data_received(client))return 0;
        if(n<0 && n!=-ESP_ERR_HTTP_EAGAIN && errno!=EAGAIN && errno!=EWOULDBLOCK)return -1;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return -1;
}
static bool fetch(const dictionary_package_file_t *file,const char *destination,void *unused) {
    (void)unused;int64_t length=0,idle=esp_timer_get_time();
    esp_http_client_handle_t client=open_http(file->url,&length);
    if(!client)return false;
    bool ok=length==file->size; FILE *output=ok ? fopen(destination,"wb") : NULL;
    uint8_t *buffer=malloc(4096),digest[32];size_t digest_size=0;
    psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT;
    ok=ok && output && buffer && psa_crypto_init()==PSA_SUCCESS && psa_hash_setup(&hash,PSA_ALG_SHA_256)==PSA_SUCCESS;
    uint32_t used=0;
    while(ok && used<file->size) {
        int n=read_http(client,buffer,(int)(file->size-used<4096 ? file->size-used : 4096),&idle);
        if(n<=0 || fwrite(buffer,1,(size_t)n,output)!=(size_t)n || psa_hash_update(&hash,buffer,(size_t)n)!=PSA_SUCCESS) {ok=false;break;}
        used+=(uint32_t)n;
        xSemaphoreTake(s_mutex,portMAX_DELAY);s_status.received+=(uint32_t)n;++s_status.revision;xSemaphoreGive(s_mutex);
    }
    ok=ok && used==file->size && esp_http_client_is_complete_data_received(client) && media() &&
        psa_hash_finish(&hash,digest,sizeof(digest),&digest_size)==PSA_SUCCESS && digest_size==32 && !memcmp(digest,file->sha256,32);
    psa_hash_abort(&hash);
    if(output) {
        if(fflush(output) || fsync(fileno(output)))ok=false;
        if(fclose(output))ok=false;
    }
    free(buffer);esp_http_client_cleanup(client);
    if(!ok)unlink(destination);
    return ok;
}
static void worker(void *unused) {
    (void)unused;bool owned=false,ok=false;
    char *body=calloc(1,8193); dictionary_package_t *package=calloc(1,sizeof(*package));
    message("正在连接 WiFi 并获取词库…");
    if(!body || !package) {message("内存不足，请关闭后重试");goto done;}
    if(!media()) {message("请插入可用的 TF 卡并退出 USB 读卡模式");goto done;}
    if(!online(&owned))goto done;
    int64_t length=0,idle=esp_timer_get_time();
    esp_http_client_handle_t client=open_http(DICTIONARY_PACKAGE_MANIFEST,&length);
    if(!client) {message("官网暂未提供词库或网络不可用，请稍后重试");goto done;}
    size_t used=0;ok=length<=8192;
    while(ok && used<8192) {
        int n=read_http(client,body+used,(int)(8192-used),&idle);
        if(n<0) {ok=false;break;}if(!n)break;used+=(size_t)n;
    }
    ok=ok && esp_http_client_is_complete_data_received(client) && !memchr(body,0,used) && dictionary_package_parse(body,package);
    esp_http_client_cleanup(client);
    if(!ok) {message("词库清单无效或下载中断，请稍后重试");goto done;}
    board_sd_info_t info;
    if(board_sd_refresh_space()!=ESP_OK || board_sd_get_info(&info)!=ESP_OK || !info.mounted || !info.space_valid ||
        info.free_bytes<(uint64_t)package->size+1024*1024) {ok=false;message("TF 卡空间不足或不可写，请检查后重试");goto done;}
    xSemaphoreTake(s_mutex,portMAX_DELAY);s_status.total=package->size;xSemaphoreGive(s_mutex);
    message("正在下载并校验词库，请保持 TF 卡插入…");
    ok=dictionary_package_install("/sdcard/dictionaries",package,fetch,cancelled,NULL);
    message(ok ? "词库已安装" : "安装未完成：网络、校验或 TF 卡写入失败，可重试");
done:
    if(owned)wifi_transfer_stop();
    free(body);free(package);
    xSemaphoreTake(s_mutex,portMAX_DELAY);s_status.busy=false;s_status.installed=ok;++s_status.revision;xSemaphoreGive(s_mutex);
    xSemaphoreGive(s_done);vTaskDelete(NULL);
}
bool dictionary_install_start(void) {
    if(!s_mutex)s_mutex=xSemaphoreCreateMutex();
    if(!s_done)s_done=xSemaphoreCreateBinary();
    if(!s_mutex || !s_done)return false;
    dictionary_install_status_t status;dictionary_install_get_status(&status);
    if(status.busy)return false;
    dictionary_install_cancel();
    if(usb_storage_active() || pico_online_busy())return false;
    app_weather_cancel_join();app_time_service_cancel();
    atomic_store(&s_cancel,false);
    xSemaphoreTake(s_mutex,portMAX_DELAY);s_status=(dictionary_install_status_t){.busy=true,.revision=status.revision+1};xSemaphoreGive(s_mutex);
    if(xTaskCreate(worker,"dictionary_install",8192,NULL,3,NULL)!=pdPASS) {
        xSemaphoreTake(s_mutex,portMAX_DELAY);s_status.busy=false;xSemaphoreGive(s_mutex);return false;
    }
    s_joinable=true;return true;
}
