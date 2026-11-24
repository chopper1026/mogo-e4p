/* 真正的分块收取、镜像回退、文件操作和 TrueType SFNT 校验；只替换卡、网络、任务平台接口。
 * Real streamed file I/O, mirror failover and TrueType SFNT validation with card, network and task adapters replaced. */
#include <assert.h>
#include <errno.h>
#include <net/if.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "font_download.h"

#define ESP_OK 0

typedef struct {
    bool mounted;
    uint64_t free_bytes;
} board_sd_info_t;
static board_sd_info_t card = { true, UINT64_C(1) << 30 };
static int board_sd_get_info(board_sd_info_t* out) {
    *out = card;
    return 0;
}
static int board_sd_refresh_space(void) {
    return 0;
}
typedef struct {
    bool active;
} network_lease_t;
typedef struct {
    bool wifi_ready;
} network_status_t;
static bool wifi = true;
static int leases, clients, requests;
static int network_service_borrow_ready_wifi(network_lease_t* l) {
    if (!wifi) return -1;
    l->active = true;
    ++leases;
    return 0;
}
static void network_service_release(network_lease_t* l) {
    if (l->active) --leases;
}
static void network_service_get_status(network_status_t* s) {
    s->wifi_ready = wifi;
}
typedef int esp_netif_t;
static esp_netif_t* wifi_transfer_netif(void) {
    static int sta;
    return &sta;
}
static int esp_netif_get_netif_impl_name(esp_netif_t* sta, char* out) {
    assert(sta == wifi_transfer_netif());
    strcpy(out, "sta");
    return 0;
}
typedef struct {
    const char* url;
    struct ifreq* if_name;
    int timeout_ms, buffer_size, buffer_size_tx;
    bool disable_auto_redirect;
    void (*crt_bundle_attach)(void);
} esp_http_client_config_t;
typedef struct {
    FILE* file;
    int status;
    int64_t declared;
    int fault;
} mock_http_t;
typedef mock_http_t* esp_http_client_handle_t;
static void esp_crt_bundle_attach(void) {}

typedef struct {
    const char* payload;
    int64_t declared;
    int status;
    int fault;
} mirror_plan_t;
static mirror_plan_t plans[4];
static int plan_count;
static const char* seen_urls[8];

static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* cfg) {
    assert(!strcmp(cfg->if_name->ifr_name, "sta") && cfg->disable_auto_redirect &&
           cfg->crt_bundle_attach);
    assert(cfg->timeout_ms == 10000);
    int idx = requests < plan_count ? requests : plan_count - 1;
    if (requests < 8) seen_urls[requests] = cfg->url;
    ++clients;
    ++requests;
    mock_http_t* h = calloc(1, sizeof(*h));
    assert(h);
    h->status = plans[idx].status;
    h->declared = plans[idx].declared;
    h->fault = plans[idx].fault;
    h->file = fopen(plans[idx].payload, "rb");
    assert(h->file);
    return h;
}
static int esp_http_client_open(esp_http_client_handle_t h, int size);
static int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h);
static int esp_http_client_get_status_code(esp_http_client_handle_t h) {
    return h->status;
}
static int esp_http_client_read(esp_http_client_handle_t h, char* buf, int n);
static void esp_http_client_cleanup(esp_http_client_handle_t h) {
    assert(!fclose(h->file));
    free(h);
    --clients;
}

#include "receive.inc"

static int esp_http_client_open(esp_http_client_handle_t h, int size) {
    assert(h && !size);
    if (h->fault == 3) atomic_store(&cancelled, true);
    if (h->fault == 4) return -1;
    return 0;
}
static int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) {
    assert(h && !atomic_load(&cancelled));
    return h->declared;
}
static int esp_http_client_read(esp_http_client_handle_t h, char* buf, int n) {
    if (h->fault == 1) return -1;
    if (h->fault == 2) atomic_store(&cancelled, true);
    return (int)fread(buf, 1, (size_t)n, h->file);
}

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static void write_ttf_fixture(
    const char* path, uint32_t size, uint32_t sfnt_tag, uint32_t head_magic, bool bad_bounds) {
    uint8_t buf[512] = { 0 };
    put_be32(buf, sfnt_tag);
    put_be16(buf + 4, 7);
    static const char* const tags[7] = { "cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp" };
    for (unsigned i = 0; i < 7; ++i) {
        uint8_t* rec = buf + 12 + i * 16;
        memcpy(rec, tags[i], 4);
        uint32_t off = (i == 2) ? 256u : 320u + i * 16u;
        uint32_t len = (i == 2) ? 54u : (bad_bounds && i == 1 ? size : 16u);
        put_be32(rec + 8, off);
        put_be32(rec + 12, len);
    }
    put_be32(buf + 256 + 12, head_magic);
    FILE* f = fopen(path, "wb");
    assert(f && fwrite(buf, 1, sizeof(buf), f) == sizeof(buf));
    if (size > sizeof(buf)) {
        assert(fseek(f, (long)(size - 1u), SEEK_SET) == 0 && fputc(0, f) != EOF);
    }
    assert(!fclose(f));
}
static void write_raw(const char* path, const char* s) {
    FILE* f = fopen(path, "wb");
    assert(f && fwrite(s, 1, strlen(s), f) == strlen(s) && !fclose(f));
}
static void set_uniform_plan(const char* payload, int64_t declared, int status, int fault) {
    plan_count = 1;
    plans[0] = (mirror_plan_t){ payload, declared, status, fault };
}
static void run(font_job_state_t expected) {
    requests = 0;
    memset(seen_urls, 0, sizeof(seen_urls));
    atomic_store(&cancelled, false);
    assert(receive_font() == expected);
    assert(!leases && !clients && access(PENDING "/font.part", F_OK) != 0);
}

int main(int argc, char** argv) {
    assert(MIRROR_COUNT == 4);
    assert(!strcmp(
        mirrors[0],
        "https://mirrors.huaweicloud.com/repository/toolkit/CTAN/fonts/lxgw-fonts/LXGWNeoXiHeiScreen.ttf"));
    assert(!strcmp(
        mirrors[1],
        "https://mirrors.cloud.tencent.com/CTAN/fonts/lxgw-fonts/LXGWNeoXiHeiScreen.ttf"));
    assert(!strcmp(
        mirrors[2],
        "https://mirrors.pku.edu.cn/ctan/fonts/lxgw-fonts/LXGWNeoXiHeiScreen.ttf"));
    assert(!strcmp(
        mirrors[3],
        "https://mirrors.ustc.edu.cn/CTAN/fonts/lxgw-fonts/LXGWNeoXiHeiScreen.ttf"));
    assert(!strcmp(font_download_path(), "fonts/LXGWNeoXiHeiScreen.ttf"));
    assert(font_download_bytes() == FONT_NOMINAL_BYTES);

    write_ttf_fixture("valid.ttf", FONT_MIN_BYTES, 0x00010000u, 0x5F0F3CF5u, false);
    write_ttf_fixture("otf.ttf", FONT_MIN_BYTES, 0x4F54544Fu, 0x5F0F3CF5u, false);
    write_ttf_fixture("badmagic.ttf", FONT_MIN_BYTES, 0x00010000u, 0x12345678u, false);
    write_ttf_fixture("badbounds.ttf", FONT_MIN_BYTES, 0x00010000u, 0x5F0F3CF5u, true);
    write_raw("short.bin", "short");

    set_uniform_plan("valid.ttf", FONT_MIN_BYTES, 200, 0);
    card.mounted = false;
    run(FONT_NO_CARD);
    assert(!requests);
    card.mounted = true;
    card.free_bytes = FONT_MIN_BYTES;
    run(FONT_NO_SPACE);
    assert(!requests);
    card.free_bytes = UINT64_C(1) << 30;
    wifi = false;
    run(FONT_NEEDS_WIFI);
    assert(!requests);
    wifi = true;

    // 响应声明体积超过剩余空间时直接中止，不再尝试后续镜像。
    // Stop immediately on FONT_NO_SPACE without retrying remaining mirrors.
    card.free_bytes = FONT_MIN_BYTES + 131072u;
    set_uniform_plan("valid.ttf", FONT_MIN_BYTES + 1048576u, 200, 0);
    run(FONT_NO_SPACE);
    assert(requests == 1);
    card.free_bytes = UINT64_C(1) << 30;

    // 所有镜像返回异常大小、错误状态、OTF/魔数错/表越界/截断均应失败且不留文件。
    set_uniform_plan("valid.ttf", FONT_MIN_BYTES - 1u, 200, 0);
    run(FONT_FAILED);
    assert(requests == MIRROR_COUNT);
    set_uniform_plan("valid.ttf", FONT_MAX_BYTES + 1u, 200, 0);
    run(FONT_FAILED);
    set_uniform_plan("valid.ttf", FONT_MIN_BYTES, 404, 0);
    run(FONT_FAILED);
    set_uniform_plan("otf.ttf", FONT_MIN_BYTES, 200, 0);
    run(FONT_FAILED);
    set_uniform_plan("badmagic.ttf", FONT_MIN_BYTES, 200, 0);
    run(FONT_FAILED);
    set_uniform_plan("badbounds.ttf", FONT_MIN_BYTES, 200, 0);
    run(FONT_FAILED);
    set_uniform_plan("short.bin", FONT_MIN_BYTES, 200, 0);
    run(FONT_FAILED);
    set_uniform_plan("valid.ttf", FONT_MIN_BYTES, 200, 1);
    run(FONT_FAILED);
    assert(access(FONT_PATH, F_OK) != 0);

    // 取消下载立即中止，不再切到下一个镜像。
    set_uniform_plan("valid.ttf", FONT_MIN_BYTES, 200, 2);
    atomic_store(&percent, 0);
    run(FONT_CANCELLED);
    assert(requests == 1 && !font_download_percent());
    set_uniform_plan("valid.ttf", FONT_MIN_BYTES, 200, 3);
    run(FONT_CANCELLED);
    assert(requests == 1);

    // 镜像顺序回退：前 3 个分别 404 / 连接失败 / 错格式，第 4 个成功。
    plan_count = 4;
    plans[0] = (mirror_plan_t){ "valid.ttf", FONT_MIN_BYTES, 404, 0 };
    plans[1] = (mirror_plan_t){ "valid.ttf", FONT_MIN_BYTES, 200, 4 };
    plans[2] = (mirror_plan_t){ "otf.ttf", FONT_MIN_BYTES, 200, 0 };
    plans[3] = (mirror_plan_t){ "valid.ttf", FONT_MIN_BYTES, 200, 0 };
    run(FONT_READY);
    assert(requests == 4);
    for (int i = 0; i < 4; ++i) assert(!strcmp(seen_urls[i], mirrors[i]));
    assert(font_download_percent() == 100);

    // 已存在合法 TTF 时离线复用，不发起任何网络请求。
    wifi = false;
    run(FONT_READY);
    assert(!requests);

    // 同名文件不是合法 TrueType 时报冲突且绝不覆盖。
    write_raw(FONT_PATH, "bad");
    run(FONT_CONFLICT);
    char b[4] = { 0 };
    FILE* f = fopen(FONT_PATH, "rb");
    assert(f && fread(b, 1, 3, f) == 3 && !strcmp(b, "bad") && !fclose(f));

    // 即使体积够大但非 TrueType（如 OTF）同样报冲突。
    assert(!unlink(FONT_PATH));
    write_ttf_fixture(FONT_PATH, FONT_MIN_BYTES, 0x4F54544Fu, 0x5F0F3CF5u, false);
    run(FONT_CONFLICT);
    assert(!unlink(FONT_PATH));

    // 可选传入真实 LXGWNeoXiHeiScreen.ttf 验证流式落盘与离线复用。
    assert(argc == 1 || argc == 2);
    if (argc == 2) {
        struct stat st;
        assert(stat(argv[1], &st) == 0);
        set_uniform_plan(argv[1], st.st_size, 200, 0);
        wifi = true;
        run(FONT_READY);
        assert(requests == 1);
        wifi = false;
        run(FONT_READY);
        assert(!requests);
    }
    puts(
        "PASS: CTAN mirror failover, Wi-Fi binding, no card, low space, cancelled/disconnected/"
        "short/non-TTF rejection, no overwrite, offline reuse and resource cleanup");
}
