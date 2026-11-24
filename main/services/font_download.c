/* 固定版本文楷只经 Wi-Fi 写 TF，不改用户文件或设置。/ Pinned WenKai downloads use Wi-Fi and TF only, never changing user files or settings. */
#include "font_download.h"
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "board_sd.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_service.h"
#include "psa/crypto.h"
#include "wifi_transfer.h"

#define DIR "/sdcard/fonts"
#define URL "https://mogo.mantake.shop/files/fonts/1.530/"
#define PENDING DIR "/.mogo-setup"
typedef struct {
    const char *url, *path, *sha;
    uint32_t bytes;
} pack_t;
static const pack_t packs[] = {
    { URL "LXGWWenKai-Medium.ttf", DIR "/LXGWWenKai-1.530-Medium.ttf",
      "f4b8a34d1f838fe411540cb7c97b1c384ac9738a876c4104500bc42508b2312a", 25800076 },
    { URL "LXGWWenKaiTC-Medium.ttf", DIR "/LXGWWenKaiTC-1.530-Medium.ttf",
      "649efd8516f42d36eacc05480d5dbd2713fb28fbd2bbee8ea76fc40875045c49", 15122300 }
};
static atomic_int state;
static atomic_uint percent;
static atomic_bool cancelled;
const char* font_download_path(bool tc) {
    return packs[tc].path;
}
uint32_t font_download_bytes(bool tc) {
    return packs[tc].bytes;
}
font_job_state_t font_download_state(void) {
    return atomic_load(&state);
}
unsigned font_download_percent(void) {
    return atomic_load(&percent);
}

static font_job_state_t receive_pack(const pack_t* p) {
    board_sd_info_t card;
    if (board_sd_get_info(&card) != ESP_OK || !card.mounted) return FONT_NO_CARD;
    if (mkdir(DIR, 0775) && errno != EEXIST) return FONT_FAILED;
    struct stat st;
    bool exists = stat(p->path, &st) == 0;
    if (exists && (!S_ISREG(st.st_mode) || st.st_size != p->bytes)) return FONT_CONFLICT;
    if (!exists && errno != ENOENT) return FONT_FAILED;
    network_lease_t lease = { 0 };
    esp_http_client_handle_t http = NULL;
    struct ifreq iface = { 0 };
    FILE* file = NULL;
    bool own_temp = false;
    font_job_state_t result = FONT_FAILED;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if (psa_crypto_init() != PSA_SUCCESS || psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS)
        goto done;
    if (exists) {
        file = fopen(p->path, "rb");
    } else {
        if (board_sd_refresh_space() != ESP_OK || board_sd_get_info(&card) != ESP_OK ||
            !card.mounted)
            goto done;
        if (card.free_bytes < p->bytes + 65536u) {
            result = FONT_NO_SPACE;
            goto done;
        }
        if (network_service_borrow_ready_wifi(&lease) != ESP_OK) {
            network_status_t net;
            network_service_get_status(&net);
            result = net.wifi_ready ? FONT_FAILED : FONT_NEEDS_WIFI;
            goto done;
        }
        esp_netif_t* sta = wifi_transfer_netif();
        if (!sta || esp_netif_get_netif_impl_name(sta, iface.ifr_name) != ESP_OK) goto done;
        esp_http_client_config_t config = { .url = p->url,
                                            .if_name = &iface,
                                            .timeout_ms = 10000,
                                            .disable_auto_redirect = true,
                                            .crt_bundle_attach = esp_crt_bundle_attach,
                                            .buffer_size = 4096,
                                            .buffer_size_tx = 1024 };
        http = esp_http_client_init(&config);
        if (!http || atomic_load(&cancelled) || esp_http_client_open(http, 0) != ESP_OK ||
            atomic_load(&cancelled) || esp_http_client_fetch_headers(http) != (int64_t)p->bytes ||
            atomic_load(&cancelled) || esp_http_client_get_status_code(http) != 200)
            goto done;
        // 此工作目录只存本安装器的未提交数据；断电后从头校验。/ Only uncommitted installer data lives here; restart verification after power loss.
        if (mkdir(PENDING, 0775) && errno != EEXIST) goto done;
        file = fopen(PENDING "/font.part", "wb");
        own_temp = file != NULL;
    }
    if (!file) goto done;
    uint32_t total = 0;
    char buf[3072];
    while (total < p->bytes && !atomic_load(&cancelled)) {
        unsigned n = p->bytes - total;
        if (n > sizeof(buf)) n = sizeof(buf);
        int got = exists ? (int)fread(buf, 1, n, file) : esp_http_client_read(http, buf, n);
        if (got <= 0 || atomic_load(&cancelled) ||
            (!exists && fwrite(buf, 1, got, file) != (size_t)got) ||
            psa_hash_update(&hash, (uint8_t*)buf, got) != PSA_SUCCESS)
            goto done;
        total += got;
        atomic_store(&percent, (uint64_t)total * 100 / p->bytes);
    }
    uint8_t digest[32];
    size_t len = 0;
    char hex[65];
    if (atomic_load(&cancelled) || total != p->bytes ||
        psa_hash_finish(&hash, digest, sizeof(digest), &len) != PSA_SUCCESS || len != 32)
        goto done;
    for (unsigned i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    if (strcmp(hex, p->sha)) {
        result = exists ? FONT_CONFLICT : FONT_FAILED;
        goto done;
    }
    if (!exists) {
        if (fflush(file) || fsync(fileno(file))) goto done;
        int err = fclose(file);
        file = NULL;
        if (err || atomic_load(&cancelled) || !stat(p->path, &st) || errno != ENOENT ||
            rename(PENDING "/font.part", p->path))
            goto done;
        own_temp = false;
    }
    result = FONT_READY;
done:
    if (file) fclose(file);
    if (own_temp) unlink(PENDING "/font.part");
    if (http) esp_http_client_cleanup(http);
    network_service_release(&lease);
    psa_hash_abort(&hash);
    return atomic_load(&cancelled) ? FONT_CANCELLED : result;
}
static void worker(void* arg) {
    font_job_state_t result = receive_pack(arg);
    atomic_store(&state, result);
    vTaskDelete(NULL);
}
bool font_download_start(bool tc) {
    if (atomic_load(&state) == FONT_BUSY) return false;
    atomic_store(&cancelled, false);
    atomic_store(&percent, 0);
    atomic_store(&state, FONT_BUSY);
    if (xTaskCreate(worker, "font_install", 8192, (void*)&packs[tc], 3, NULL) == pdPASS)
        return true;
    atomic_store(&state, FONT_FAILED);
    return false;
}
bool font_download_quiesce(unsigned timeout_ms) {
    if (atomic_load(&state) != FONT_BUSY) return true;
    atomic_store(&cancelled, true);
    TickType_t start = xTaskGetTickCount();
    while (atomic_load(&state) == FONT_BUSY &&
           xTaskGetTickCount() - start < pdMS_TO_TICKS(timeout_ms))
        vTaskDelay(1);
    return atomic_load(&state) != FONT_BUSY;
}
