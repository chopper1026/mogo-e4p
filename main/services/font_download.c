/* 霞鹜新晰黑（屏显版）只经 Wi-Fi 从国内 CTAN 镜像写 TF，不改用户文件或设置。
 * LXGW Neo XiHei Screen downloads via Wi-Fi from mainland China CTAN mirrors to TF only, never changing user files or settings. */
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
#include "wifi_transfer.h"

#define DIR "/sdcard/fonts"
#define FONT_PATH DIR "/LXGWNeoXiHeiScreen.ttf"
#define PENDING DIR "/.mogo-setup"
#define FONT_NOMINAL_BYTES 7666968u
#define FONT_MIN_BYTES (4u * 1024u * 1024u)
#define FONT_MAX_BYTES (24u * 1024u * 1024u)
#define FONT_HTTP_TIMEOUT_MS 10000

// 国内 CTAN 镜像均经 HTTPS 直出 200，按优先级顺序自动回退；路径不带版本号，
// 因此用长度范围与 TrueType SFNT 结构校验兼容上游小版本更新，避免旧固件失效。
// Mainland China CTAN mirrors serve direct HTTPS 200 with sequential failover; because CTAN paths
// are unversioned, bounded length and TrueType SFNT validation keep shipped firmware working across upstream updates.
static const char* const mirrors[] = {
    "https://mirrors.huaweicloud.com/repository/toolkit/CTAN/fonts/lxgw-fonts/LXGWNeoXiHeiScreen.ttf",
    "https://mirrors.cloud.tencent.com/CTAN/fonts/lxgw-fonts/LXGWNeoXiHeiScreen.ttf",
    "https://mirrors.pku.edu.cn/ctan/fonts/lxgw-fonts/LXGWNeoXiHeiScreen.ttf",
    "https://mirrors.ustc.edu.cn/CTAN/fonts/lxgw-fonts/LXGWNeoXiHeiScreen.ttf",
};
enum { MIRROR_COUNT = sizeof(mirrors) / sizeof(mirrors[0]) };

static atomic_int state;
static atomic_uint percent;
static atomic_bool cancelled;

const char* font_download_path(void) {
    return FONT_PATH;
}
uint32_t font_download_bytes(void) {
    return FONT_NOMINAL_BYTES;
}
font_job_state_t font_download_state(void) {
    return atomic_load(&state);
}
unsigned font_download_percent(void) {
    return atomic_load(&percent);
}

static uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// 核对 TrueType SFNT 头、必选七张表与 head 魔数，拒绝错误页、CFF/OTF 与截断文件。
// Verify TrueType SFNT header, seven required tables and head magic to reject HTML errors, CFF/OTF or truncated files.
static bool validate_ttf_file(FILE* file, uint32_t file_size) {
    if (!file || file_size < FONT_MIN_BYTES || file_size > FONT_MAX_BYTES) return false;
    if (fseek(file, 0, SEEK_SET) != 0) return false;
    uint8_t hdr[12];
    if (fread(hdr, 1, sizeof(hdr), file) != sizeof(hdr)) return false;
    if (read_be32(hdr) != 0x00010000u) return false;
    uint16_t num_tables = read_be16(hdr + 4);
    if (num_tables < 7u || num_tables > 32u) return false;
    uint8_t dir[32u * 16u];
    size_t dir_bytes = (size_t)num_tables * 16u;
    if (fread(dir, 1, dir_bytes, file) != dir_bytes) return false;
    static const char* const required[7] = { "cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp" };
    unsigned found = 0;
    uint32_t head_off = 0, head_len = 0;
    for (uint16_t i = 0; i < num_tables; ++i) {
        const uint8_t* rec = dir + (size_t)i * 16u;
        uint32_t off = read_be32(rec + 8);
        uint32_t len = read_be32(rec + 12);
        if ((uint64_t)off + (uint64_t)len > (uint64_t)file_size) return false;
        for (unsigned k = 0; k < 7u; ++k) {
            if (memcmp(rec, required[k], 4) == 0) {
                if (len == 0) return false;
                found |= 1u << k;
                if (k == 2u) {
                    head_off = off;
                    head_len = len;
                }
            }
        }
    }
    if (found != 0x7fu || head_len < 54u) return false;
    uint8_t magic[4];
    if (fseek(file, (long)(head_off + 12u), SEEK_SET) != 0) return false;
    if (fread(magic, 1, sizeof(magic), file) != sizeof(magic)) return false;
    return read_be32(magic) == 0x5F0F3CF5u;
}

static font_job_state_t download_from_mirror(const char* url, struct ifreq* iface, uint64_t free_bytes) {
    esp_http_client_handle_t http = NULL;
    FILE* file = NULL;
    bool own_temp = false;
    font_job_state_t result = FONT_FAILED;
    atomic_store(&percent, 0);
    esp_http_client_config_t config = { .url = url,
                                        .if_name = iface,
                                        .timeout_ms = FONT_HTTP_TIMEOUT_MS,
                                        .disable_auto_redirect = true,
                                        .crt_bundle_attach = esp_crt_bundle_attach,
                                        .buffer_size = 4096,
                                        .buffer_size_tx = 1024 };
    http = esp_http_client_init(&config);
    if (!http || atomic_load(&cancelled) || esp_http_client_open(http, 0) != ESP_OK ||
        atomic_load(&cancelled))
        goto done;
    int64_t declared = esp_http_client_fetch_headers(http);
    if (atomic_load(&cancelled) || esp_http_client_get_status_code(http) != 200 ||
        declared < (int64_t)FONT_MIN_BYTES || declared > (int64_t)FONT_MAX_BYTES)
        goto done;
    uint32_t expected = (uint32_t)declared;
    if (free_bytes < (uint64_t)expected + 65536u) {
        result = FONT_NO_SPACE;
        goto done;
    }
    // 此工作目录只存本安装器的未提交数据；断电后从头校验。/ Only uncommitted installer data lives here; restart verification after power loss.
    if (mkdir(PENDING, 0775) && errno != EEXIST) goto done;
    file = fopen(PENDING "/font.part", "w+b");
    if (!file) goto done;
    own_temp = true;
    uint32_t total = 0;
    char buf[4096];
    while (total < expected && !atomic_load(&cancelled)) {
        unsigned n = expected - total;
        if (n > sizeof(buf)) n = sizeof(buf);
        int got = esp_http_client_read(http, buf, (int)n);
        if (got <= 0 || atomic_load(&cancelled) || fwrite(buf, 1, (size_t)got, file) != (size_t)got)
            goto done;
        total += (uint32_t)got;
        atomic_store(&percent, (unsigned)((uint64_t)total * 100u / expected));
    }
    if (atomic_load(&cancelled) || total != expected || fflush(file) != 0 ||
        !validate_ttf_file(file, total) || fsync(fileno(file)) != 0)
        goto done;
    int err = fclose(file);
    file = NULL;
    struct stat st;
    if (err || atomic_load(&cancelled) || stat(FONT_PATH, &st) == 0 || errno != ENOENT ||
        rename(PENDING "/font.part", FONT_PATH) != 0)
        goto done;
    own_temp = false;
    result = FONT_READY;
done:
    if (file) fclose(file);
    if (own_temp) unlink(PENDING "/font.part");
    if (http) esp_http_client_cleanup(http);
    return atomic_load(&cancelled) ? FONT_CANCELLED : result;
}

static font_job_state_t receive_font(void) {
    board_sd_info_t card;
    if (board_sd_get_info(&card) != ESP_OK || !card.mounted) return FONT_NO_CARD;
    if (mkdir(DIR, 0775) && errno != EEXIST) return FONT_FAILED;
    struct stat st;
    bool exists = stat(FONT_PATH, &st) == 0;
    if (exists) {
        if (!S_ISREG(st.st_mode) || st.st_size < (off_t)FONT_MIN_BYTES ||
            st.st_size > (off_t)FONT_MAX_BYTES)
            return FONT_CONFLICT;
        FILE* existing = fopen(FONT_PATH, "rb");
        if (!existing) return FONT_FAILED;
        bool valid = validate_ttf_file(existing, (uint32_t)st.st_size);
        fclose(existing);
        return valid ? FONT_READY : FONT_CONFLICT;
    }
    if (errno != ENOENT) return FONT_FAILED;
    if (board_sd_refresh_space() != ESP_OK || board_sd_get_info(&card) != ESP_OK || !card.mounted)
        return FONT_FAILED;
    if (card.free_bytes < (uint64_t)FONT_MIN_BYTES + 65536u) return FONT_NO_SPACE;
    network_lease_t lease = { 0 };
    if (network_service_borrow_ready_wifi(&lease) != ESP_OK) {
        network_status_t net;
        network_service_get_status(&net);
        return net.wifi_ready ? FONT_FAILED : FONT_NEEDS_WIFI;
    }
    struct ifreq iface = { 0 };
    esp_netif_t* sta = wifi_transfer_netif();
    font_job_state_t result = FONT_FAILED;
    if (sta && esp_netif_get_netif_impl_name(sta, iface.ifr_name) == ESP_OK) {
        for (unsigned i = 0; i < MIRROR_COUNT && !atomic_load(&cancelled); ++i) {
            result = download_from_mirror(mirrors[i], &iface, card.free_bytes);
            if (result == FONT_READY || result == FONT_NO_SPACE || result == FONT_CANCELLED) break;
        }
    }
    network_service_release(&lease);
    return atomic_load(&cancelled) ? FONT_CANCELLED : result;
}

static void worker(void* arg) {
    (void)arg;
    font_job_state_t result = receive_font();
    atomic_store(&state, result);
    vTaskDelete(NULL);
}
bool font_download_start(void) {
    if (atomic_load(&state) == FONT_BUSY) return false;
    atomic_store(&cancelled, false);
    atomic_store(&percent, 0);
    atomic_store(&state, FONT_BUSY);
    if (xTaskCreate(worker, "font_install", 8192, NULL, 3, NULL) == pdPASS) return true;
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
