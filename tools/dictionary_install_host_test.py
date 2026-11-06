#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实下载任务配确定性 HTTP/PSA，验证短流、校验、取消与生命周期。
English: Exercise the download worker with deterministic HTTP/PSA at failure boundaries.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static time_t wall_clock=1800000000;
static time_t download_time(time_t *out){if(out)*out=wall_clock;return wall_clock;}
static bool host_install(const char *, const dictionary_package_t *, dictionary_package_fetch_fn, book_dictionary_cancel_fn, void *);
#define dictionary_package_install host_install
#define time download_time
#include "../main/services/dictionary_install.c"
#undef time
#undef dictionary_package_install
struct FakeHttp { size_t at, size; bool manifest; };
static char body[8193], destination[512];
static unsigned char payload[9000];
static size_t payload_size=sizeof(payload);
static bool done,usb,ota,saved=true,mounted=true,space=true,chunked,bad_hash,short_body,stall,drop_card,cancel_read,fail_task,cancel_sync;
static unsigned clients,starts,stops,installed,weather_cancels,time_cancels,sync_calls;
static unsigned media_busy;
static bool busy_after_read,cancel_busy;
static int64_t clock_us;
static int network_mode=WIFI_TRANSFER_MODE_STA;
static void (*pending)(void *);
static void run_worker(void){ assert(pending);void(*fn)(void*)=pending;pending=NULL;fn(NULL); }
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
SemaphoreHandle_t xSemaphoreCreateBinary(void){return (void*)2;}
int xSemaphoreTake(SemaphoreHandle_t s,uint32_t wait){
 if(s==(void*)1)return pdTRUE;assert(s==(void*)2);if(wait&&pending)run_worker();if(!done)return 0;done=false;return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t s){if(s==(void*)2){assert(!done);done=true;}else assert(s==(void*)1);return pdTRUE;}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned stack,void*a,unsigned p,void*t){
 (void)n;(void)a;(void)p;(void)t;assert(stack==8192&&!pending);if(fail_task)return 0;pending=fn;return pdPASS;
}
void vTaskDelete(void*p){assert(!p);}
void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;}
int64_t esp_timer_get_time(void){return clock_us;}
bool usb_storage_active(void){return usb;}
bool pico_online_busy(void){return ota;}
void app_weather_cancel_join(void){++weather_cancels;}
void app_time_service_cancel(void){++time_cancels;}
esp_err_t board_sd_get_info(board_sd_info_t *i){*i=(board_sd_info_t){.mounted=mounted,.space_valid=space,.free_bytes=1024ull*1024*1024};if(media_busy){--media_busy;if(cancel_busy)atomic_store(&s_cancel,true);return ESP_ERR_NOT_FINISHED;}return ESP_OK;}
esp_err_t board_sd_refresh_space(void){return ESP_OK;}
void wifi_transfer_get_status(wifi_transfer_status_t*s){*s=(wifi_transfer_status_t){.mode=network_mode,.network_ready=network_mode==WIFI_TRANSFER_MODE_STA};}
esp_err_t wifi_transfer_get_saved_wifi(char*s,bool*has){s[0]=0;*has=saved;return ESP_OK;}
void wifi_transfer_stop(void){++stops;network_mode=0;}
esp_err_t wifi_transfer_start(const wifi_transfer_cfg_t*c){assert(c->network_only);++starts;network_mode=c->mode;return ESP_OK;}
void wifi_transfer_service_poll(void){}
esp_err_t wifi_transfer_sync_time_auto(uint32_t*t){++sync_calls;if(cancel_sync){atomic_store(&s_cancel,true);return ESP_FAIL;}*t=1800000000;return ESP_OK;}
bool wifi_transfer_cancel_time_sync(void){return true;}
int esp_crt_bundle_attach(void*p){(void)p;return 0;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*c){
 assert(c->disable_auto_redirect&&c->crt_bundle_attach&&c->timeout_ms==2000);
 assert(!strncmp(c->url,DICTIONARY_PACKAGE_ORIGIN,strlen(DICTIONARY_PACKAGE_ORIGIN)));
 struct FakeHttp*h=calloc(1,sizeof(*h));assert(h);h->manifest=!strcmp(c->url,DICTIONARY_PACKAGE_MANIFEST);h->size=h->manifest?strlen(body):payload_size;++clients;return h;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char*k,const char*v){(void)h;(void)k;(void)v;return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){(void)h;assert(!n);return ESP_OK;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){return h->manifest&&chunked?0:(int64_t)h->size;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return 200;}
int esp_http_client_read(esp_http_client_handle_t h,char*out,int cap){
 if(!h->manifest){if(drop_card)mounted=false;if(cancel_read)atomic_store(&s_cancel,true);if(busy_after_read)media_busy=3;if(stall){errno=EAGAIN;return -ESP_ERR_HTTP_EAGAIN;}}
 size_t limit=h->size-(!h->manifest&&short_body?1:0),n=h->at<limit?limit-h->at:0;if(n>(size_t)cap)n=(size_t)cap;
 memcpy(out,(h->manifest?(const unsigned char*)body:payload)+h->at,n);h->at+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){return h->at==h->size;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){assert(clients);--clients;free(h);return ESP_OK;}
int psa_crypto_init(void){return PSA_SUCCESS;}
int psa_hash_setup(psa_hash_operation_t*h,int alg){assert(alg==PSA_ALG_SHA_256);h->bytes=0;return PSA_SUCCESS;}
int psa_hash_update(psa_hash_operation_t*h,const void*b,size_t n){assert(!memcmp(b,payload+h->bytes,n));h->bytes+=n;return PSA_SUCCESS;}
int psa_hash_finish(psa_hash_operation_t*h,uint8_t*out,size_t n,size_t*used){assert(n==32&&h->bytes==payload_size);memset(out,bad_hash?0:0x42,32);*used=32;return PSA_SUCCESS;}
int psa_hash_abort(psa_hash_operation_t*h){h->bytes=0;return PSA_SUCCESS;}
static bool host_install(const char*root,const dictionary_package_t*p,dictionary_package_fetch_fn fn,book_dictionary_cancel_fn cancel,void*context){
 assert(!strcmp(root,"/sdcard/dictionaries")&&p->size>0);
 for(unsigned i=0;i<DICTIONARY_PACKAGE_FILES;++i)if(cancel(context)||!fn(&p->files[i],destination,context))return false;
 ++installed;return true;
}
static void finish(bool expected){
 run_worker();dictionary_install_status_t status;dictionary_install_get_status(&status);
 assert(!status.busy&&status.installed==expected&&status.message[0]&&!clients);
 dictionary_install_cancel();assert(!pending&&!done&&!dictionary_install_busy());
}
int main(int argc,char**argv){
 assert(argc==3);snprintf(destination,sizeof(destination),"%s",argv[2]);FILE*f=fopen(argv[1],"rb");assert(f);assert(fread(body,1,8192,f));fclose(f);
 for(size_t i=0;i<sizeof(payload);++i)payload[i]=(unsigned char)i;
 usb=true;assert(!dictionary_install_start());usb=false;ota=true;assert(!dictionary_install_start());ota=false;
 fail_task=true;assert(!dictionary_install_start());assert(!dictionary_install_busy());fail_task=false;
 assert(dictionary_install_start());assert(!dictionary_install_start());finish(true);assert(installed==1&&!starts&&!stops);
 chunked=true;assert(dictionary_install_start());finish(true);chunked=false;assert(installed==2);
 bad_hash=true;assert(dictionary_install_start());finish(false);bad_hash=false;
 short_body=true;assert(dictionary_install_start());finish(false);short_body=false;
 payload_size--;assert(dictionary_install_start());finish(false);payload_size++;
 stall=true;assert(dictionary_install_start());int64_t start=clock_us;finish(false);assert(clock_us-start>=15000000);stall=false;
 drop_card=true;assert(dictionary_install_start());finish(false);drop_card=false;mounted=true;
 cancel_read=true;assert(dictionary_install_start());finish(false);cancel_read=false;
 space=false;assert(dictionary_install_start());finish(false);space=true;
 network_mode=0;saved=false;assert(dictionary_install_start());finish(false);saved=true;
 assert(dictionary_install_start());finish(true);assert(starts==1&&stops==2);
 assert(dictionary_install_start());dictionary_install_cancel();assert(!pending&&!done&&!clients&&!dictionary_install_busy());
 wall_clock=0;cancel_sync=true;assert(dictionary_install_start());finish(false);assert(sync_calls==1);cancel_sync=false;
 assert(dictionary_install_start());finish(true);assert(sync_calls==2);
 media_busy=3;assert(dictionary_install_start());finish(true);
 busy_after_read=true;assert(dictionary_install_start());finish(true);busy_after_read=false;
 media_busy=200;assert(dictionary_install_start());int64_t busy_start=clock_us;finish(false);assert(clock_us-busy_start==2000000);media_busy=0;
 media_busy=3;cancel_busy=true;assert(dictionary_install_start());finish(false);cancel_busy=false;media_busy=0;
 assert(installed==6&&weather_cancels&&time_cancels);
 puts("PASS: real download worker; streaming, SHA, short/stalled HTTP, transient/bounded card busy, card loss, cancellation, WiFi ownership and space gates");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    work=Path(folder)
    env, compiler=toolchain(work)
    stubs=work/'stubs'
    shutil.copytree(ROOT/'tools/ota_stubs',stubs)
    with (stubs/'common.h').open('a') as header:
        header.write('\n#define ESP_ERR_NOT_FINISHED 0x10c\nesp_err_t wifi_transfer_sync_time_auto(uint32_t*);\nbool wifi_transfer_cancel_time_sync(void);\n')
    shutil.copyfile(ROOT/'tools/book_layout_stubs/epdiy.h',stubs/'epdiy.h')
    (stubs/'board_sd.h').write_text('#pragma once\n#include "common.h"\ntypedef struct {bool mounted,space_valid;uint64_t free_bytes;} board_sd_info_t;\nesp_err_t board_sd_get_info(board_sd_info_t*);\nesp_err_t board_sd_refresh_space(void);\n')
    # Keep the real service include path and its local headers intact.
    unit=work/'test.c'
    unit.write_text('#include <stdbool.h>\n#include "dictionary_package.h"\n'+UNIT.replace('../main/services/dictionary_install.c',str(ROOT/'main/services/dictionary_install.c')))
    names=['chinese.LICENSE.txt','chinese.manifest.json','chinese.mgdict','licenses/CC-BY-SA-4.0.txt','licenses/OpenCC-Apache-2.0.txt','licenses/OpenCC-NOTICE.txt','licenses/Unicode-3.0.txt','licenses/mapull-MIT.txt']
    import json
    manifest={'schemaVersion':1,'format':1,'packageId':'a'*64,'version':'test','files':[{'name':name,'size':9000,'sha256':'42'*32,'url':'/api/v2/components/dictionary/packages/'+'a'*64+'/files/'+name} for name in names]}
    (work/'manifest.json').write_text(json.dumps(manifest))
    flags=['-std=gnu11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(stubs),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'managed_components/espressif__cjson/cJSON')]
    cjson=work/'json.o'
    subprocess.run(compiler+flags+['-Wno-deprecated-declarations','-c',str(ROOT/'managed_components/espressif__cjson/cJSON/cJSON.c'),'-o',str(cjson)],env=env,check=True)
    # Unused install/path functions live in the same production object; its parser probe is linked normally.
    subprocess.run(compiler+flags+['-I'+str(ROOT/'tools/book_layout_stubs'),'-I'+str(ROOT/'tools/book_source_host_stubs'),str(unit),str(ROOT/'main/book/dictionary_package.c'),str(ROOT/'main/book/book_dictionary.c'),str(ROOT/'main/book/reader_selection.c'),str(cjson),'-lz','-o',str(work/'test')],env=env,check=True)
    subprocess.run([str(work/'test'),str(work/'manifest.json'),str(work/'download.bin')],env=env,check=True,timeout=30)
