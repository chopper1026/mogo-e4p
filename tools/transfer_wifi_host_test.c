/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 配网解析与连接时限的真实策略回归。/ Regression of actual provisioning parsing and connection deadlines.
 */
#include <assert.h>
#include <stdio.h>
#include "../components/wifi_transfer/transfer_policy.h"

static int64_t body_time;
static int body_reads, body_cancel_at, body_step = 4000, body_failure;
static bool body_stopped(void *ctx) { (void)ctx; return body_cancel_at && body_reads >= body_cancel_at; }
static int64_t body_now(void *ctx) { (void)ctx; return body_time; }
static int body_receive(void *ctx, char *out, size_t cap) {
    (void)ctx; assert(cap); *out = 'a'; ++body_reads; body_time += body_step;
    return body_failure ? body_failure : 1;
}
static void test_body_deadline(void) {
    char body[257] = {0};
    body_time = body_reads = 0; body_cancel_at = 1;
    assert(!receive_credentials_body(body, 1, body_receive, body_stopped, body_now, NULL));
    assert(body_reads == 1);
    body_time = body_reads = body_cancel_at = 0;
    assert(!receive_credentials_body(body, 256, body_receive, body_stopped, body_now, NULL));
    assert(body_reads == 4);
    body_time = body_reads = 0;
    assert(receive_credentials_body(body, 2, body_receive, body_stopped, body_now, NULL));
    assert(body_reads == 2);
    body_time = body_reads = 0; body_cancel_at = -1;
    assert(!receive_credentials_body(body, 2, body_receive, body_stopped, body_now, NULL));
    assert(body_reads == 0);
    body_cancel_at = 0; body_step = 15000;
    assert(!receive_credentials_body(body, 1, body_receive, body_stopped, body_now, NULL));
    body_time = body_reads = 0; body_step = 1; body_failure = -1;
    assert(!receive_credentials_body(body, 2, body_receive, body_stopped, body_now, NULL));
    body_time = body_reads = 0; body_failure = 3;
    assert(!receive_credentials_body(body, 2, body_receive, body_stopped, body_now, NULL));
    body_failure = 0;
}

// 故障注入只替换NVS边界；生产读写函数原样执行。/ Inject faults only at NVS boundaries; run production storage helpers unchanged.
#define WIFI_TRANSFER_NVS_TEST
typedef int esp_err_t;
typedef unsigned nvs_handle_t;
enum { ESP_OK, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG, ESP_FAIL,
       ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND };
enum { NVS_READONLY, NVS_READWRITE };
static transfer_credentials_t committed, staged;
static bool present;
static bool legacy_present, pending_legacy, profiles_present, pending_profiles;
static unsigned char committed_profiles[1024], staged_profiles[1024];
static size_t committed_size, staged_size;
static int fail_open, fail_write, fail_commit, fail_erase, handles;
static esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name, "rp_wifi")); (void)mode;
    if (fail_open) return ESP_FAIL;
    ++handles; *handle = 1; staged = committed;
    pending_legacy = legacy_present; pending_profiles = profiles_present;
    staged_size = committed_size; memcpy(staged_profiles, committed_profiles, sizeof(staged_profiles));
    return ESP_OK;
}
static esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    assert(handle == 1);
    if (!strcmp(key, "profiles")) {
        if (!profiles_present) return ESP_ERR_NVS_NOT_FOUND;
        assert(*size >= committed_size); *size = committed_size;
        memcpy(out, committed_profiles, committed_size); return ESP_OK;
    }
    assert(!strcmp(key, "credentials"));
    if (!legacy_present) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= sizeof(committed)); *size = sizeof(committed); memcpy(out, &committed, *size); return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *in, size_t size) {
    assert(handle == 1);
    if (fail_write) return ESP_FAIL;
    if (!strcmp(key, "credentials")) {
        assert(size == sizeof(staged)); memcpy(&staged, in, size); pending_legacy = true;
    } else {
        assert(!strcmp(key, "profiles") && size <= sizeof(staged_profiles));
        memcpy(staged_profiles, in, size); staged_size = size; pending_profiles = true;
    }
    return ESP_OK;
}
static esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    assert(handle == 1);
    if (fail_write || fail_erase) return ESP_FAIL;
    bool *slot = !strcmp(key, "credentials") ? &pending_legacy : &pending_profiles;
    assert(slot == &pending_legacy || !strcmp(key, "profiles"));
    if (!*slot) return ESP_ERR_NVS_NOT_FOUND;
    *slot = false; return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == 1);
    if (fail_commit) return ESP_FAIL;
    committed = staged; legacy_present = pending_legacy;
    present = pending_profiles ? staged_profiles[1] != 0 : legacy_present;
    profiles_present = pending_profiles; committed_size = staged_size;
    memcpy(committed_profiles, staged_profiles, sizeof(committed_profiles)); return ESP_OK;
}
static void nvs_close(nvs_handle_t handle) {
    assert(handle == 1); --handles; clear_secret(&staged, sizeof(staged));
    clear_secret(staged_profiles, sizeof(staged_profiles));
}
// 记录影子分区的操作与字节数，不与实际存储共享状态。/ Record shadow operations and sizes without touching live keys.
#define NVS_TYPE_BLOB 0x42
typedef struct { const char *written, *erased; size_t bytes; } restore_nvs_t;
static esp_err_t restore_nvs_write(restore_nvs_t *image, const char *ns, const char *key, int type, const void *data, size_t len) {
    assert(!strcmp(ns, "rp_wifi") && type == NVS_TYPE_BLOB && data);
    image->written = key; image->bytes = len; return ESP_OK;
}
static esp_err_t restore_nvs_erase(restore_nvs_t *image, const char *ns, const char *key) {
    assert(!strcmp(ns, "rp_wifi")); image->erased = key; return ESP_OK;
}
#include "../components/wifi_transfer/transfer_credentials_store.h"

static void test_profiles(void) {
    profiles_present = false; legacy_present = true;
    assert(credentials_from_text("legacy", "12345678", &committed));
    transfer_profiles_t profiles;
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 1 && !profiles_present);
    assert(!strcmp(profiles.items[0].ssid, "legacy"));
    transfer_credentials_t next;
    assert(credentials_from_text("second", "abcdefgh", &next));
    fail_commit = 1;
    assert(store_credentials(&next) == ESP_FAIL && !profiles_present && legacy_present && !handles);
    fail_commit = 0;
    assert(store_credentials(&next) == ESP_OK && profiles_present && !legacy_present);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 2);
    assert(!strcmp(profiles.items[0].ssid, "second") && !strcmp(profiles.items[1].ssid, "legacy"));
    assert(credentials_from_text("legacy", "newpassword", &next));
    assert(store_credentials(&next) == ESP_OK);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 2);
    assert(!strcmp(profiles.items[0].password, "newpassword") && !strcmp(profiles.items[1].password, "abcdefgh"));
    for (size_t i = 2; i < WIFI_TRANSFER_SAVED_MAX; ++i) {
        char name[33]; snprintf(name, sizeof(name), "profile-%u", (unsigned)i);
        assert(credentials_from_text(name, "12345678", &next));
        assert(store_credentials(&next) == ESP_OK);
    }
    assert(credentials_from_text("overflow", "12345678", &next));
    assert(store_credentials(&next) == ESP_ERR_NO_MEM);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == WIFI_TRANSFER_SAVED_MAX);
    assert(profile_index(&profiles, "legacy") >= 0 && profile_index(&profiles, "overflow") < 0);
    transfer_profiles_t restored = {.version = 2, .count = 1, .items = {next}};
    assert(!merge_backup_profiles(&restored, &profiles));
    assert(load_profiles(&profiles) == ESP_OK && profile_index(&profiles, "overflow") < 0);
    restored = (transfer_profiles_t){.version = 2, .count = 1};
    assert(credentials_from_text("legacy", "backup-pass", &restored.items[0]));
    assert(merge_backup_profiles(&restored, &profiles));
    assert(restored.count == profiles.count && !strcmp(restored.items[0].password, "backup-pass"));
    assert(!strcmp(restored.items[1].ssid, profiles.items[0].ssid));
    clear_secret(&restored, sizeof(restored));
    fail_commit = 1;
    assert(forget_credentials("legacy", &next) == ESP_FAIL && !next.version && !handles);
    fail_commit = 0;
    assert(forget_credentials("legacy", &next) == ESP_OK && next.version == 1);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == WIFI_TRANSFER_SAVED_MAX - 1);
    assert(profile_index(&profiles, "legacy") < 0 && profile_index(&profiles, "second") >= 0);
    assert(forget_credentials("missing", &next) == ESP_ERR_NOT_FOUND && !next.version);
    assert(forget_credentials(NULL, &next) == ESP_OK);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == WIFI_TRANSFER_SAVED_MAX - 2);
    profiles.items[1] = profiles.items[0];
    assert(!profiles_valid(&profiles));
    profiles.count = WIFI_TRANSFER_SAVED_MAX + 1; assert(!profiles_valid(&profiles));
    // 老 v2 空表仍优先于遗留单条；损坏的权威键不回退或被读取覆盖。/ An old empty v2 table wins over legacy; corrupt authoritative data must not fall back.
    transfer_profiles_t empty = {.version = 2};
    memcpy(committed_profiles, &empty, sizeof(empty)); committed_size = sizeof(empty); profiles_present = true;
    legacy_present = true;
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 0);
    committed_profiles[0] = 99;
    assert(load_profiles(&profiles) == ESP_ERR_INVALID_STATE && profiles.count == 0);
    assert(store_credentials(NULL) == ESP_OK && !handles && !profiles_present && !legacy_present);

    // 旧版把单条也写成八槽；只读不迁移，保存空间不足时原凭据仍在。
    // Old releases wrote one network as a full table. Reads do not migrate and failed compact writes retain it.
    transfer_profiles_t one = {.version = 2, .count = 1};
    assert(credentials_from_text("first", "12345678", &one.items[0]));
    memcpy(committed_profiles, &one, sizeof(one)); committed_size = sizeof(one); profiles_present = true;
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 1 && !legacy_present && profiles_present);
    fail_write = 1;
    assert(store_profiles(&profiles) == ESP_FAIL && !legacy_present && profiles_present);
    assert(load_profiles(&profiles) == ESP_OK && !strcmp(profiles.items[0].password, "12345678"));
    fail_write = 0;
    // 2→1 两步提交中断时旧表仍有效；1→2 时已提交的新表优先。/ Interrupt cleanup in both directions and retry against the authoritative table.
    assert(credentials_from_text("first", "12345678", &next));
    assert(store_credentials(&next) == ESP_OK && legacy_present && !profiles_present);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 1);
    restore_nvs_t dry = {0};
    assert(persist_profiles(&profiles, &dry) == ESP_OK && dry.bytes == sizeof(next) &&
           !strcmp(dry.written, "credentials") && !strcmp(dry.erased, "profiles"));
    fail_erase = 1;
    assert(credentials_from_text("second", "abcdefgh", &next));
    assert(store_credentials(&next) == ESP_FAIL && legacy_present && profiles_present);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 2 && !strcmp(profiles.items[0].ssid, "second"));
    assert(persist_profiles(&profiles, &dry) == ESP_OK && dry.bytes == sizeof(profiles) &&
           !strcmp(dry.written, "profiles") && !strcmp(dry.erased, "credentials"));
    assert(forget_credentials("second", &next) == ESP_FAIL && !next.version);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 2);
    fail_erase = 0;
    assert(forget_credentials("second", &next) == ESP_OK && !strcmp(next.ssid, "first") && legacy_present && !profiles_present);
    assert(forget_credentials("first", &next) == ESP_OK && !legacy_present && !profiles_present);
    clear_secret(&profiles, sizeof(profiles)); clear_secret(&next, sizeof(next));
}

static bool parse(const char *json, transfer_credentials_t *c) { return parse_credentials(json, strlen(json), c); }
int main(void) {
    test_body_deadline();
    test_profiles();
    bool stopping = false, uploading = false;
    assert(admission_begin(stopping, &uploading));
    assert(!admission_stop(&stopping, uploading) && !stopping);
    uploading = false;
    assert(admission_stop(&stopping, uploading) && stopping);
    assert(!admission_begin(stopping, &uploading) && !uploading);
    stopping = false;
    assert(admission_stop(&stopping, uploading));
    assert(!admission_begin(stopping, &uploading));
    transfer_credentials_t c;
    assert(credentials_from_text("device", "12345678", &c));
    assert(!credentials_from_text("device", "short", &c) && c.version == 0);
    assert(!credentials_from_text("", "12345678", &c));
    assert(!credentials_from_text("123456789012345678901234567890123", "12345678", &c));
    wifi_transfer_network_t networks[WIFI_TRANSFER_SCAN_MAX] = {0};
    size_t count = 0;
    wifi_transfer_network_t ap = {.ssid = "same", .rssi = -70, .requires_password = true, .supported = true};
    scan_offer(networks, &count, &ap); ap.rssi = -40; scan_offer(networks, &count, &ap);
    assert(count == 1 && networks[0].rssi == -40);
    ap.ssid[0] = 0; scan_offer(networks, &count, &ap); assert(count == 1);
    for (int i = 0; i < 25; ++i) {
        snprintf(ap.ssid, sizeof(ap.ssid), "network-%02d", i); ap.rssi = (int8_t)(-100 + i);
        scan_offer(networks, &count, &ap);
    }
    assert(count == WIFI_TRANSFER_SCAN_MAX && networks[0].rssi == -40);
    for (size_t i = 1; i < count; ++i) assert(networks[i - 1].rssi >= networks[i].rssi);
    strcpy(ap.ssid, "network-24"); ap.rssi = -20; ap.requires_password = false;
    scan_offer(networks, &count, &ap);
    assert(count == WIFI_TRANSFER_SCAN_MAX && networks[0].rssi == -20 && !networks[0].requires_password);
    assert(parse("{\"ssid\":\"家庭 WiFi\",\"password\":\"12345678\"}", &c));
    assert(!strcmp(c.ssid, "家庭 WiFi"));
    assert(parse("{\"ssid\":\"open\",\"password\":\"\"}", &c));
    assert(store_credentials(&c) == ESP_OK && present && !handles);
    transfer_credentials_t old;
    assert(load_credentials(&old) == ESP_OK && !strcmp(old.ssid, "open"));
    strcpy(c.ssid, "replacement"); fail_write = 1;
    assert(store_credentials(&c) == ESP_FAIL && !handles);
    assert(load_credentials(&old) == ESP_OK && !strcmp(old.ssid, "open"));
    fail_write = 0; fail_commit = 1;
    assert(store_credentials(&c) == ESP_FAIL && !handles);
    assert(load_credentials(&old) == ESP_OK && !strcmp(old.ssid, "open"));
    assert(store_credentials(NULL) == ESP_FAIL && present && !handles);
    fail_commit = 0; fail_open = 1;
    assert(store_credentials(&c) == ESP_FAIL && !handles);
    assert(load_credentials(&old) == ESP_FAIL && old.version == 0);
    fail_open = 0;
    assert(store_credentials(NULL) == ESP_OK && !present && !handles);
    assert(load_credentials(&old) == ESP_OK && old.version == 0);
    assert(store_credentials(NULL) == ESP_OK && !handles);
    const char *bad[] = {"{}", "[]", "null", "{\"ssid\":\"\",\"password\":\"12345678\"}",
        "{\"ssid\":\"home\",\"password\":12345678}", "{\"ssid\":\"home\",\"password\":\"short\"}",
        "{\"ssid\":\"home\",\"password\":\"12345678\"}x", "{\"ssid\":\"home\\u0000x\",\"password\":\"12345678\"}",
        "{\"ssid\":\"home\",\"ssid\":\"evil\",\"password\":\"12345678\"}",
        "{\"ssid\":\"home\",\"password\":\"12345678\",\"unknown\":1}"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) assert(!parse(bad[i], &c));
    assert(!parse("{\"ssid\":[],\"password\":\"12345678\"}", &c));
    assert(parse("{\"ssid\":\"home[{}]\",\"password\":\"12345678\"}", &c));
    const char embedded[] = "{\"ssid\":\"home\",\"password\":\"12345678\"}\0junk";
    assert(!parse_credentials(embedded, sizeof(embedded) - 1, &c));
    memset(&c, 0, sizeof(c)); c.version = 1; memset(c.ssid, 'a', 32); memset(c.password, 'a', 64);
    assert(credentials_valid(&c)); c.password[0] = 'g'; assert(!credentials_valid(&c));
    c.password[0] = 'a'; c.ssid[32] = 'b'; assert(!credentials_valid(&c));
    clear_secret(&c, sizeof(c)); for (size_t i = 0; i < sizeof(c); ++i) assert(((unsigned char *)&c)[i] == 0);
    transfer_connection_t net;
    connection_begin(&net, 100);
    assert(connection_poll(&net, 100) == 1 && net.attempts == 1);
    assert(connection_poll(&net, 29999) == 0);
    assert(connection_poll(&net, 30100) == -1 && net.failed);
    assert(connection_poll(&net, 40000) == 0);
    connection_begin(&net, 0);
    for (int i = 0; i < 5; ++i) {
        assert(connection_poll(&net, i * 1000) == 1);
        connection_lost(&net, i * 1000);
    }
    assert(connection_poll(&net, 5000) == -1);
    connection_begin(&net, 0); net.online = true;
    assert(connection_poll(&net, 100000) == 0);
    connection_lost(&net, 100000);
    assert(connection_poll(&net, 100999) == 0);
    assert(connection_poll(&net, 101000) == 1 && net.attempts == 1);
    assert(time_route(true, true, true, false) == TRANSFER_TIME_ONLINE);
    assert(time_route(true, true, false, false) == TRANSFER_TIME_UNAVAILABLE);
    assert(time_route(true, false, true, false) == TRANSFER_TIME_UNAVAILABLE);
    assert(time_route(false, true, false, true) == TRANSFER_TIME_TEMPORARY);
    assert(time_route(false, true, false, false) == TRANSFER_TIME_UNAVAILABLE);
    puts("transfer WiFi policy tests passed");
}
