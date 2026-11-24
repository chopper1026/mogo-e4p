/* 真正的分块收取、文件操作和 SHA-256；只替换卡、网络、任务平台接口。/ Real streamed file I/O and SHA-256 with card, network and task adapters replaced. */
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
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#define HASH_CTX CC_SHA256_CTX
#define HASH_INIT CC_SHA256_Init
#define HASH_UPDATE CC_SHA256_Update
#define HASH_FINISH CC_SHA256_Final
#else
#include <openssl/sha.h>
#define HASH_CTX SHA256_CTX
#define HASH_INIT SHA256_Init
#define HASH_UPDATE SHA256_Update
#define HASH_FINISH SHA256_Final
#endif
#define ESP_OK 0
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
#define PSA_HASH_OPERATION_INIT { 0 }
typedef HASH_CTX psa_hash_operation_t;
static int psa_crypto_init(void) {
    return 0;
}
static int psa_hash_setup(HASH_CTX* c, int alg) {
    assert(alg == 1);
    return !HASH_INIT(c);
}
static int psa_hash_update(HASH_CTX* c, uint8_t* buf, size_t n) {
    return !HASH_UPDATE(c, buf, n);
}
static int psa_hash_finish(HASH_CTX* c, uint8_t* out, size_t cap, size_t* n) {
    assert(cap == 32);
    *n = 32;
    return !HASH_FINISH(out, c);
}
static void psa_hash_abort(HASH_CTX* c) {
    (void)c;
}
typedef struct {
    bool mounted;
    uint64_t free_bytes;
} board_sd_info_t;
static board_sd_info_t card = { true, UINT32_MAX };
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
typedef FILE* esp_http_client_handle_t;
static void esp_crt_bundle_attach(void) {}
static const char* payload = "payload";
static long declared = 3;
static int status = 200, fault;
static FILE* esp_http_client_init(const esp_http_client_config_t* cfg) {
    assert(
        !strcmp(cfg->if_name->ifr_name, "sta") && cfg->disable_auto_redirect &&
        cfg->crt_bundle_attach);
    assert(cfg->timeout_ms == 10000);
    ++clients;
    ++requests;
    FILE* f = fopen(payload, "rb");
    assert(f);
    return f;
}
static int esp_http_client_open(FILE* f, int size);
static long esp_http_client_fetch_headers(FILE* f);
static int esp_http_client_get_status_code(FILE* f) {
    (void)f;
    return status;
}
static int esp_http_client_read(FILE* f, char* buf, int n);
static void esp_http_client_cleanup(FILE* f) {
    assert(!fclose(f));
    --clients;
}
#include "receive.inc"
static int esp_http_client_open(FILE* f, int size) {
    assert(f && !size);
    if (fault == 3) atomic_store(&cancelled, true);
    return 0;
}
static long esp_http_client_fetch_headers(FILE* f) {
    assert(f && !atomic_load(&cancelled));
    return declared;
}
static int esp_http_client_read(FILE* f, char* buf, int n) {
    if (fault == 1) return -1;
    if (fault == 2) atomic_store(&cancelled, true);
    return fread(buf, 1, n, f);
}
static void write_file(const char* p, const char* s) {
    FILE* f = fopen(p, "wb");
    assert(f && fwrite(s, 1, strlen(s), f) == strlen(s) && !fclose(f));
}
static void run(const pack_t* p, font_job_state_t expected) {
    atomic_store(&cancelled, false);
    assert(receive_pack(p) == expected);
    assert(!leases && !clients && access(PENDING "/font.part", F_OK) != 0);
}
int main(int argc, char** argv) {
    assert(!strcmp(packs[0].url, "https://mogo.mantake.shop/files/fonts/1.530/LXGWWenKai-Medium.ttf"));
    assert(!strcmp(packs[1].url, "https://mogo.mantake.shop/files/fonts/1.530/LXGWWenKaiTC-Medium.ttf"));
    pack_t demo = { "https://example.invalid/fixture", DIR "/target.ttf",
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 3 };
    write_file(payload, "abc");
    card.mounted = false;
    run(&demo, FONT_NO_CARD);
    card.mounted = true;
    card.free_bytes = 3;
    run(&demo, FONT_NO_SPACE);
    card.free_bytes = UINT32_MAX;
    wifi = false;
    run(&demo, FONT_NEEDS_WIFI);
    wifi = true;
    assert(!requests);
    declared = 2;
    run(&demo, FONT_FAILED);
    declared = 3;
    status = 404;
    run(&demo, FONT_FAILED);
    status = 200;
    fault = 1;
    run(&demo, FONT_FAILED);
    fault = 2;
    atomic_store(&percent, 0);
    run(&demo, FONT_CANCELLED);
    assert(!font_download_percent());
    fault = 3;
    run(&demo, FONT_CANCELLED);
    fault = 0;
    write_file(payload, "x");
    run(&demo, FONT_FAILED);
    write_file(payload, "bad");
    run(&demo, FONT_FAILED);
    assert(access(demo.path, F_OK));
    write_file(payload, "abc");
    run(&demo, FONT_READY);
    int before = requests;
    wifi = false;
    run(&demo, FONT_READY);
    assert(before == requests);
    write_file(demo.path, "bad");
    run(&demo, FONT_CONFLICT);
    char b[4] = { 0 };
    FILE* f = fopen(demo.path, "rb");
    assert(f && fread(b, 1, 3, f) == 3 && !strcmp(b, "bad") && !fclose(f));
    assert(argc == 1 || argc == 3);
    for (int i = 1; i < argc; i++) {
        const pack_t* p = &packs[i - 1];
        payload = argv[i];
        declared = p->bytes;
        wifi = true;
        run(p, FONT_READY);
        wifi = false;
        before = requests;
        run(p, FONT_READY);
        assert(requests == before);
    }
    puts(
        "PASS: Wi-Fi binding, no card, low space, cancelled/disconnected/short/corrupt responses, "
        "no overwrite, offline reuse and resource cleanup");
}
