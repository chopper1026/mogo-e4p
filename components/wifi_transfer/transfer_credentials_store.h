/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 多网络共用一个版本化 blob，兼容旧单网络；失败不更新公开快照。
 * One versioned blob holds multiple networks with legacy compatibility; failures leave snapshots unchanged.
 */
#pragma once
#ifndef WIFI_TRANSFER_NVS_TEST
#include "nvs.h"
#include "restore_nvs.h"
#endif
#include "transfer_policy.h"

typedef struct {
    uint8_t version;
    uint8_t count;
    transfer_credentials_t items[WIFI_TRANSFER_SAVED_MAX];
} transfer_profiles_t;

static bool profiles_valid(const transfer_profiles_t *profiles) {
    if (profiles->version != 2 || profiles->count > WIFI_TRANSFER_SAVED_MAX) return false;
    for (size_t i = 0; i < profiles->count; ++i) {
        if (!credentials_valid(&profiles->items[i])) return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(profiles->items[i].ssid, profiles->items[j].ssid)) return false;
    }
    return true;
}

static esp_err_t load_profiles(transfer_profiles_t *out) {
    memset(out, 0, sizeof(*out));
    out->version = 2;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_wifi", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t size = sizeof(*out);
    err = nvs_get_blob(handle, "profiles", out, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // 空的新格式也是权威记录；仅缺键时读取旧格式，避免遗忘后复活旧网络。
        // An empty new record is authoritative; only a missing key may fall back to legacy data.
        transfer_credentials_t legacy = {0};
        size = sizeof(legacy);
        err = nvs_get_blob(handle, "credentials", &legacy, &size);
        if (err == ESP_OK && (size != sizeof(legacy) || !credentials_valid(&legacy)))
            err = ESP_ERR_INVALID_STATE;
        memset(out, 0, sizeof(*out));
        out->version = 2;
        if (err == ESP_OK) { out->items[0] = legacy; out->count = 1; }
        clear_secret(&legacy, sizeof(legacy));
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        size = sizeof(*out);
    }
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(*out) || !profiles_valid(out)) {
        clear_secret(out, sizeof(*out));
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    return ESP_OK;
}

// 单条复用旧凭据，多条沿用 v2；两键同时存在时 profiles 始终优先。先提交新副本，再删旧键。
// Reuse legacy credentials for one network and v2 for multiple; profiles always wins if both exist. Commit before cleanup.
static esp_err_t erase_profile_key(restore_nvs_t *image, nvs_handle_t h, const char *key) {
    if (image) return restore_nvs_erase(image, "rp_wifi", key);
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    return err == ESP_OK ? nvs_commit(h) : err;
}
static esp_err_t persist_profiles(const transfer_profiles_t *next, restore_nvs_t *image) {
    if (!profiles_valid(next)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h = 0;
    esp_err_t err = image ? ESP_OK : nvs_open("rp_wifi", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (next->count) {
        bool single = next->count == 1;
        const char *key = single ? "credentials" : "profiles";
        const void *data = single ? (const void *)&next->items[0] : (const void *)next;
        size_t size = single ? sizeof(next->items[0]) : sizeof(*next);
        err = image ? restore_nvs_write(image, "rp_wifi", key, NVS_TYPE_BLOB, data, size)
                    : nvs_set_blob(h, key, data, size);
        if (!image && err == ESP_OK) err = nvs_commit(h);
        if (err == ESP_OK) err = erase_profile_key(image, h, single ? "profiles" : "credentials");
    } else {
        // 先删回退键再删权威键，避免断电后复活旧网络；无需保留八槽空表。/ Remove fallback first so interruptions cannot resurrect it; no empty table needed.
        err = erase_profile_key(image, h, "credentials");
        if (err == ESP_OK) err = erase_profile_key(image, h, "profiles");
    }
    if (!image) nvs_close(h);
    return err;
}
static esp_err_t store_profiles(const transfer_profiles_t *next) {
    return persist_profiles(next, NULL);
}

static esp_err_t load_credentials(transfer_credentials_t *out) {
    memset(out, 0, sizeof(*out));
    transfer_profiles_t profiles;
    esp_err_t err = load_profiles(&profiles);
    if (err == ESP_OK && profiles.count) *out = profiles.items[0];
    clear_secret(&profiles, sizeof(profiles));
    return err;
}

static int profile_index(const transfer_profiles_t *profiles, const char *ssid) {
    for (size_t i = 0; i < profiles->count; ++i)
        if (!strcmp(profiles->items[i].ssid, ssid)) return (int)i;
    return -1;
}

// 构造有序并集，不静默丢弃本机网络。/ Construct an ordered union without silently dropping local networks.
static bool merge_backup_profiles(transfer_profiles_t *preferred, const transfer_profiles_t *local) {
    for (size_t i = 0; i < local->count; ++i) {
        if (profile_index(preferred, local->items[i].ssid) >= 0) continue;
        if (preferred->count == WIFI_TRANSFER_SAVED_MAX) return false;
        preferred->items[preferred->count++] = local->items[i];
    }
    return true;
}

static esp_err_t store_credentials(const transfer_credentials_t *next) {
    if (next && !credentials_valid(next)) return ESP_ERR_INVALID_ARG;
    transfer_profiles_t profiles = {.version = 2};
    esp_err_t err = next ? load_profiles(&profiles) : ESP_OK;
    if (err == ESP_OK && next) {
        int index = profile_index(&profiles, next->ssid);
        if (index < 0 && profiles.count == WIFI_TRANSFER_SAVED_MAX) err = ESP_ERR_NO_MEM;
        else {
            if (index < 0) index = profiles.count++;
            memmove(&profiles.items[1], &profiles.items[0], (size_t)index * sizeof(*next));
            profiles.items[0] = *next;
        }
    }
    if (err == ESP_OK) err = store_profiles(&profiles);
    clear_secret(&profiles, sizeof(profiles));
    return err;
}

static esp_err_t forget_credentials(const char *ssid, transfer_credentials_t *preferred) {
    memset(preferred, 0, sizeof(*preferred));
    transfer_profiles_t profiles;
    esp_err_t err = load_profiles(&profiles);
    if (err == ESP_OK) {
        int index = ssid ? profile_index(&profiles, ssid) : profiles.count ? 0 : -1;
        if (index < 0 && ssid) err = ESP_ERR_NOT_FOUND;
        else if (index >= 0) {
            --profiles.count;
            memmove(&profiles.items[index], &profiles.items[index + 1],
                    (profiles.count - (size_t)index) * sizeof(profiles.items[0]));
            clear_secret(&profiles.items[profiles.count], sizeof(profiles.items[0]));
        }
        if (err == ESP_OK) err = store_profiles(&profiles);
        if (err == ESP_OK && profiles.count) *preferred = profiles.items[0];
    }
    clear_secret(&profiles, sizeof(profiles));
    return err;
}
