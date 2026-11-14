#!/usr/bin/env python3
"""运行真实租约准入与 OTA 设备分支，使用无线模型而不访问设备。
Run real bearer admission and the device OTA branch against offline radio models.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/services/network_service.c"

MODEL = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "network_service.h"
#include "cellular_service.h"
typedef void esp_netif_t;
enum { WIFI_TRANSFER_STOPPED, WIFI_TRANSFER_STARTING, WIFI_TRANSFER_READY,
       WIFI_TRANSFER_UPLOADING, WIFI_TRANSFER_ERROR };
enum { WIFI_TRANSFER_MODE_AP, WIFI_TRANSFER_MODE_STA };
typedef struct { int state, mode; bool network_ready, network_only; } wifi_transfer_status_t;
typedef struct { int mode; bool network_only; } wifi_transfer_cfg_t;
static network_route_t s_route;
static network_auto_priority_t s_priority;
static atomic_uint s_wifi_leases, s_cellular_leases;
static atomic_bool s_cellular_control;
static cellular_service_status_t cellular;
static wifi_transfer_status_t wifi;
static bool configured, deny_registration, fail_wifi, cell_recovers;
static bool missing_wifi_netif, missing_cell_netif, fail_default;
static bool deny_wifi_stop, attempt_during_start;
static esp_err_t nested_acquire_error, nested_disable_error;
static int wifi_start_error;
static int64_t wifi_ready_at;
static unsigned default_calls, cell_status_reads, ready_on_cell_read;
static unsigned wifi_starts, wifi_stops, cell_starts, cell_stops;
static int64_t now_us;
static int wifi_marker, cell_marker;
static void *default_netif;
static int64_t esp_timer_get_time(void) { return now_us; }
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(unsigned ms) {
    now_us += (int64_t)ms * 1000;
    if (wifi_ready_at && now_us >= wifi_ready_at) {
        wifi.network_ready = true; wifi.state = WIFI_TRANSFER_READY;
    }
}
static int esp_netif_set_default_netif(void *netif) {
    ++default_calls;
    if (fail_default) return ESP_ERR_INVALID_STATE;
    default_netif = netif; return ESP_OK;
}
static void wifi_transfer_get_status(wifi_transfer_status_t *out) { *out = wifi; }
static int wifi_transfer_get_saved_wifi(char *ssid, bool *saved) {
    strcpy(ssid, configured ? "saved-network" : ""); *saved = configured; return ESP_OK;
}
static int wifi_transfer_start(const wifi_transfer_cfg_t *cfg) {
    assert(cfg->mode == WIFI_TRANSFER_MODE_STA && cfg->network_only);
    ++wifi_starts; wifi.mode = cfg->mode; wifi.network_only = cfg->network_only;
    if (attempt_during_start) {
        network_lease_t other;
        nested_acquire_error = network_service_acquire(NETWORK_USE_WEATHER, &other);
        nested_disable_error = network_service_set_enabled(NETWORK_BEARER_WIFI, false);
        assert(!other.active);
    }
    if (wifi_start_error) { wifi.state = WIFI_TRANSFER_ERROR; return wifi_start_error; }
    wifi.network_ready = !fail_wifi;
    wifi.state = fail_wifi ? WIFI_TRANSFER_STARTING : WIFI_TRANSFER_READY;
    return ESP_OK;
}
static void wifi_transfer_stop(void) { ++wifi_stops; wifi = (wifi_transfer_status_t){0}; }
static void *wifi_transfer_netif(void) { return missing_wifi_netif ? NULL : &wifi_marker; }
void *cellular_service_netif(void) { return missing_cell_netif ? NULL : &cell_marker; }
void cellular_service_get_status(cellular_service_status_t *out) {
    if (++cell_status_reads == ready_on_cell_read) cellular.ready = true;
    *out = cellular;
}
static void app_settings_set_network_route(network_route_t route) { (void)route; }
esp_err_t cellular_service_set_enabled(bool enabled) {
    if (enabled) ++cell_starts; else ++cell_stops;
    cellular.enabled = enabled; return ESP_OK;
}
static bool wifi_transfer_try_stop_if_idle(void) {
    if (deny_wifi_stop) return false;
    wifi_transfer_stop(); return true;
}
void cellular_service_poll(void) {
    if (deny_registration) { cellular.state = CELLULAR_SERVICE_NO_REGISTRATION; cellular.ready = false; }
    if (fail_wifi && wifi.state == WIFI_TRANSFER_STARTING) wifi.state = WIFI_TRANSFER_ERROR;
    if (cell_recovers && cell_starts) { cellular.state = CELLULAR_SERVICE_READY; cellular.ready = true; }
}
static void reset(network_route_t route, network_auto_priority_t priority) {
    s_route = route; s_priority = priority; now_us = 0;
    atomic_store(&s_wifi_leases, 0); atomic_store(&s_cellular_leases, 0);
    atomic_store(&s_cellular_control, false);
    cellular = (cellular_service_status_t){.capability_known=true, .capable=true,
        .enabled=true, .state=CELLULAR_SERVICE_STARTING};
    wifi = (wifi_transfer_status_t){0};
    configured = true; deny_registration = fail_wifi = cell_recovers = false;
    missing_wifi_netif = missing_cell_netif = fail_default = false;
    deny_wifi_stop = attempt_during_start = false; wifi_start_error = 0; wifi_ready_at = 0;
    nested_acquire_error = nested_disable_error = ESP_OK;
    default_calls = cell_status_reads = ready_on_cell_read = 0;
    wifi_starts = wifi_stops = cell_starts = cell_stops = 0; default_netif = NULL;
}
'''

CASES = r'''
int main(void) {
    network_lease_t lease;
    // 已启动的 STA 连接可被借用，取得 IP 前不重启或误报不可用。
    // Borrow a connecting STA without restarting it or rejecting it before IP acquisition.
    for (int automatic = 0; automatic < 2; ++automatic) {
        for (int use = NETWORK_USE_TIME; use <= NETWORK_USE_OTA; ++use) {
            reset(automatic ? NETWORK_ROUTE_AUTO : NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
            cellular.enabled = false;
            wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_STARTING,
                .mode=WIFI_TRANSFER_MODE_STA, .network_only=true};
            wifi_ready_at = 200000;
            assert(network_service_acquire((network_use_t)use, &lease) == ESP_OK);
            assert(lease.active && !lease.owns_wifi && !wifi_starts);
            assert(network_service_wait_ready(&lease, 30000) && now_us == 200000);
            network_service_release(&lease);
            assert(!wifi_stops && !atomic_load(&s_wifi_leases));
        }
    }
    // 只有空闲纯联网会话可从 ERROR 重建；一次申请只启动一次。
    // Rebuild only an idle network-only ERROR session, starting at most once per acquisition.
    reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
    wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_ERROR,
        .mode=WIFI_TRANSFER_MODE_STA, .network_only=true};
    assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_OK);
    assert(lease.owns_wifi && wifi_starts == 1 && wifi_stops == 1);
    assert(network_service_wait_ready(&lease, 30000));
    network_service_release(&lease);
    assert(wifi_stops == 2 && !atomic_load(&s_wifi_leases));
    reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
    wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_ERROR,
        .mode=WIFI_TRANSFER_MODE_STA, .network_only=true};
    assert(network_service_set_enabled(NETWORK_BEARER_WIFI, true) == ESP_OK);
    assert(wifi_starts == 1 && wifi_stops == 1 && wifi.network_ready);
    for (int protected_session = 0; protected_session < 3; ++protected_session) {
        reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
        wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_ERROR,
            .mode=WIFI_TRANSFER_MODE_STA, .network_only=true};
        if (protected_session == 0) wifi.network_only = false;
        if (protected_session == 1) deny_wifi_stop = true;
        if (protected_session == 2) atomic_store(&s_wifi_leases, 1);
        assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_ERR_INVALID_STATE);
        assert(!lease.active && !wifi_starts && !wifi_stops);
    }
    reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
    wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_STARTING, .mode=0};
    assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_ERR_INVALID_STATE);
    assert(!wifi_starts && !wifi_stops);
    reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
    wifi_start_error = ESP_ERR_NO_MEM;
    assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_ERR_NO_MEM);
    assert(!lease.active && wifi_starts == 1 && !atomic_load(&s_wifi_leases));
    reset(NETWORK_ROUTE_AUTO, NETWORK_AUTO_WIFI_FIRST);
    cellular.enabled = false; wifi_start_error = ESP_ERR_NO_MEM;
    assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_ERR_NO_MEM);
    assert(!lease.active && wifi_starts == 1 && !atomic_load(&s_wifi_leases));
    reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
    atomic_store(&s_cellular_control, true);
    assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_ERR_INVALID_STATE);
    assert(!lease.active && !wifi_starts && !atomic_load(&s_wifi_leases));
    // 启动或恢复发布租约前，重入申请和关无线均须被同一控制门挡住。
    // The same gate rejects reentrant acquire/shutdown until startup publishes its lease.
    reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST); attempt_during_start = true;
    assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_OK);
    assert(nested_acquire_error == ESP_ERR_INVALID_STATE && nested_disable_error == ESP_ERR_INVALID_STATE);
    assert(wifi_starts == 1 && !wifi_stops && atomic_load(&s_wifi_leases) == 1);
    assert(!atomic_load(&s_cellular_control));
    network_service_release(&lease);
    assert(wifi_stops == 1 && !atomic_load(&s_wifi_leases));
    // No ready bearer must leave an existing route alone, including at acquire.
    reset(NETWORK_ROUTE_CELLULAR, NETWORK_AUTO_WIFI_FIRST);
    default_netif = &wifi_marker;
    network_service_poll();
    assert(default_calls == 0 && default_netif == &wifi_marker);
    assert(network_service_acquire(NETWORK_USE_WEATHER, &lease) == ESP_OK);
    assert(default_calls == 0 && default_netif == &wifi_marker);
    // IP may arrive between poll's snapshot and wait_ready's second snapshot.
    cell_status_reads = 0; ready_on_cell_read = 2;
    assert(network_service_wait_ready(&lease, 300));
    assert(now_us == 0 && default_netif == &cell_marker && default_calls == 1);
    network_service_release(&lease);
    // A ready flag alone is insufficient without a usable/default interface.
    for (int missing = 0; missing < 2; ++missing) {
        reset(NETWORK_ROUTE_CELLULAR, NETWORK_AUTO_WIFI_FIRST);
        cellular.ready = true; missing_cell_netif = missing; fail_default = !missing;
        assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_OK);
        assert(!network_service_wait_ready(&lease, 300));
        assert(!default_netif && now_us == 300000);
        network_service_release(&lease);
    }
    reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
    missing_wifi_netif = true;
    assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_OK);
    assert(!network_service_wait_ready(&lease, 300) && !default_netif);
    network_service_release(&lease);
    // Capability probes never rewrite Auto or a user's explicit route.
    for (int route = NETWORK_ROUTE_WIFI; route <= NETWORK_ROUTE_AUTO; ++route) {
        reset((network_route_t)route, NETWORK_AUTO_WIFI_FIRST);
        cellular.capable = false;
        network_service_poll();
        network_status_t status;
        network_service_get_status(&status);
        assert(status.route == (network_route_t)route);
        const esp_err_t result = network_service_acquire(NETWORK_USE_WEATHER, &lease);
        if (route == NETWORK_ROUTE_CELLULAR) {
            assert(result == ESP_ERR_NOT_FOUND && !lease.active && !wifi_starts);
        } else {
            assert(result == ESP_OK && lease.bearer == NETWORK_BEARER_WIFI);
            network_service_release(&lease);
        }
        cellular.capable = true;
        network_service_poll();
        network_service_get_status(&status);
        assert(status.route == (network_route_t)route);
    }
    // The automatic route immediately falls back after a registration denial.
    reset(NETWORK_ROUTE_AUTO, NETWORK_AUTO_CELLULAR_FIRST);
    assert(network_service_acquire(NETWORK_USE_WEATHER, &lease) == ESP_OK);
    assert(lease.bearer == NETWORK_BEARER_CELLULAR);
    deny_registration = true;
    assert(network_service_wait_ready(&lease, 30000));
    assert(lease.bearer == NETWORK_BEARER_WIFI && lease.owns_wifi);
    assert(wifi_starts == 1 && default_netif == &wifi_marker);
    assert(atomic_load(&s_cellular_leases) == 0 && atomic_load(&s_wifi_leases) == 1);
    // Recovery of the preferred 4G bearer cannot steal the fallback Wi-Fi lease.
    deny_registration = false; cellular.ready = true;
    network_service_poll();
    assert(default_netif == &wifi_marker);
    network_service_release(&lease);
    assert(wifi_stops == 1 && !lease.active && cellular.enabled);
    assert(atomic_load(&s_wifi_leases) == 0);
    network_service_poll();
    assert(default_netif == &cell_marker);

    // An explicitly selected 4G route reports failure and never starts Wi-Fi.
    reset(NETWORK_ROUTE_CELLULAR, NETWORK_AUTO_CELLULAR_FIRST);
    assert(network_service_acquire(NETWORK_USE_WEATHER, &lease) == ESP_OK);
    deny_registration = true;
    assert(!network_service_wait_ready(&lease, 30000));
    assert(!wifi_starts && lease.bearer == NETWORK_BEARER_CELLULAR);
    network_service_release(&lease);
    assert(atomic_load(&s_cellular_leases) == 0);
    for (int state = CELLULAR_SERVICE_NO_SIM; state <= CELLULAR_SERVICE_SIM_NOT_READY; ++state) {
        reset(NETWORK_ROUTE_CELLULAR, NETWORK_AUTO_WIFI_FIRST);
        cellular.state = state;
        assert(network_service_acquire(NETWORK_USE_WEATHER, &lease) == ESP_ERR_NOT_FOUND);
        assert(!lease.active && !wifi_starts && !cell_starts);
    }
    reset(NETWORK_ROUTE_AUTO, NETWORK_AUTO_CELLULAR_FIRST);
    cellular.state = CELLULAR_SERVICE_SIM_NOT_READY;
    strcpy(cellular.detail, "SIM 需要 PIN 解锁");
    network_status_t status;
    network_service_get_status(&status);
    assert(!strcmp(status.cellular_detail, cellular.detail));
    assert(network_service_acquire(NETWORK_USE_WEATHER, &lease) == ESP_OK);
    assert(lease.bearer == NETWORK_BEARER_WIFI && !cell_starts);
    network_service_release(&lease);

    // Wi-Fi failures can fall back to enabled 4G under the automatic route.
    reset(NETWORK_ROUTE_AUTO, NETWORK_AUTO_WIFI_FIRST);
    fail_wifi = cell_recovers = true;
    assert(network_service_acquire(NETWORK_USE_WEATHER, &lease) == ESP_OK);
    assert(network_service_wait_ready(&lease, 30000));
    assert(lease.bearer == NETWORK_BEARER_CELLULAR && default_netif == &cell_marker);
    assert(wifi_stops == 1 && cell_starts == 1);
    // Likewise, a recovered preferred Wi-Fi bearer waits for the 4G lease to end.
    fail_wifi = false;
    wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_READY,
        .mode=WIFI_TRANSFER_MODE_STA, .network_ready=true};
    network_service_poll();
    assert(default_netif == &cell_marker);
    network_service_release(&lease);
    assert(!atomic_load(&s_wifi_leases) && !atomic_load(&s_cellular_leases));
    network_service_poll();
    assert(default_netif == &wifi_marker);

    // Forced Wi-Fi never silently borrows a connected cellular bearer.
    reset(NETWORK_ROUTE_WIFI, NETWORK_AUTO_WIFI_FIRST);
    configured = false; cellular.ready = true;
    assert(network_service_acquire(NETWORK_USE_WEATHER, &lease) == ESP_ERR_NOT_FOUND);
    assert(!lease.active && !cell_starts);
    // Active business leases protect the radio against shutdown.
    for (int use = NETWORK_USE_TIME; use <= NETWORK_USE_OTA; ++use) {
        reset(NETWORK_ROUTE_CELLULAR, NETWORK_AUTO_WIFI_FIRST);
        cellular.ready = true;
        assert(network_service_acquire((network_use_t)use, &lease) == ESP_OK);
        assert(network_service_set_enabled(NETWORK_BEARER_CELLULAR, false) == ESP_ERR_INVALID_STATE);
        assert(!cell_stops && !atomic_load(&s_cellular_control));
        network_service_release(&lease);
        assert(network_service_set_enabled(NETWORK_BEARER_CELLULAR, false) == ESP_OK && cell_stops == 1);
        assert(!cellular.enabled && !atomic_load(&s_cellular_leases));
    }
    // The control gate also blocks a new lease while the radio is being changed.
    reset(NETWORK_ROUTE_CELLULAR, NETWORK_AUTO_WIFI_FIRST);
    atomic_store(&s_cellular_control, true);
    assert(network_service_set_enabled(NETWORK_BEARER_CELLULAR, false) == ESP_ERR_INVALID_STATE);
    assert(network_service_acquire(NETWORK_USE_OTA, &lease) == ESP_ERR_INVALID_STATE);
    assert(!atomic_load(&s_cellular_leases) && !cell_stops);
    puts("PASS: bearer fallback, readiness races, route failures, lease route stability and radio exclusion");
}
'''


OTA_MODEL = r'''
#define ESP_PLATFORM 1
#include <time.h>
#include "ota_online.h"
static network_lease_t s_network_lease;
static bool s_network_lease_active;
static atomic_bool s_cancel;
static pico_update_status_t update;
static unsigned snapshots, warnings, sync_calls, time_notes;
static int snapshot_bearer;
static esp_err_t sync_error;
static time_t clock_utc;
static time_t ota_test_time(time_t *out) {
    if (out) *out = clock_utc;
    return clock_utc;
}
#define time ota_test_time
#define ESP_LOGW(tag, format, error) ((void)(tag), (void)(format), (void)(error), ++warnings)
static const char *esp_err_to_name(esp_err_t error) { (void)error; return "test-error"; }
static void state(pico_update_state_t value, const char *message) {
    assert(message && strlen(message) < sizeof(update.message));
    update.state = value;
    strcpy(update.message, message);
}
static void network_diagnostics_snapshot(const char *owner, int bearer) {
    assert(!strcmp(owner, "ota")); ++snapshots; snapshot_bearer = bearer;
}
esp_err_t network_service_sync_time_on_lease(network_lease_t *lease, uint32_t *utc) {
    assert(lease->active); ++sync_calls; *utc = 1704067200;
    return sync_error;
}
static void app_time_service_note_success(uint32_t utc) {
    assert(utc == 1704067200); ++time_notes;
}
static void reset_ota(network_route_t route) {
    reset(route, NETWORK_AUTO_WIFI_FIRST);
    cellular.enabled = false;
    s_network_lease = (network_lease_t){0}; s_network_lease_active = false;
    atomic_store(&s_cancel, false);
    update = (pico_update_status_t){.state=PICO_UPDATE_CHECKING};
    snapshots = warnings = sync_calls = time_notes = 0;
    snapshot_bearer = NETWORK_BEARER_NONE;
    sync_error = ESP_OK; clock_utc = 1704067200;
}
static void release_ota(void) {
    if (s_network_lease_active) network_service_release(&s_network_lease);
    s_network_lease_active = false;
    assert(!atomic_load(&s_wifi_leases) && !atomic_load(&s_cellular_leases));
}
'''

OTA_CASES = r'''
int main(void) {
    bool owned = false;
    // 编译真实 ESP_PLATFORM 分支，连通 OTA 与租约准入、等待、释放流程。
    // Compile the actual ESP_PLATFORM branch to integrate OTA admission, readiness and cleanup.
    for (int automatic = 0; automatic < 2; ++automatic) {
        reset_ota(automatic ? NETWORK_ROUTE_AUTO : NETWORK_ROUTE_WIFI);
        wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_STARTING,
            .mode=WIFI_TRANSFER_MODE_STA, .network_only=true};
        wifi_ready_at = 200000;
        assert(online(&owned));
        assert(now_us == 200000 && !wifi_starts && !wifi_stops && !owned);
        assert(s_network_lease_active && !s_network_lease.owns_wifi);
        assert(snapshots == 1 && snapshot_bearer == NETWORK_BEARER_WIFI && !warnings);
        release_ota(); assert(!wifi_stops && wifi.network_ready);
    }
    reset_ota(NETWORK_ROUTE_WIFI);
    wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_ERROR,
        .mode=WIFI_TRANSFER_MODE_STA, .network_only=true};
    assert(online(&owned) && wifi_starts == 1 && wifi_stops == 1);
    assert(s_network_lease.owns_wifi);
    release_ota(); assert(wifi_stops == 2);

    // 热点、文件服务、正在执行的文件操作和其他租约不会被错误重连打断。
    // Failed-session recovery must not interrupt AP, file-server, active file work or another lease.
    for (int protected_session = 0; protected_session < 4; ++protected_session) {
        reset_ota(NETWORK_ROUTE_WIFI);
        wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_ERROR,
            .mode=WIFI_TRANSFER_MODE_STA, .network_only=true};
        if (protected_session == 0) wifi.mode = WIFI_TRANSFER_MODE_AP;
        if (protected_session == 1) wifi.network_only = false;
        if (protected_session == 2) deny_wifi_stop = true;
        if (protected_session == 3) atomic_store(&s_wifi_leases, 1);
        assert(!online(&owned));
        assert(update.state == PICO_UPDATE_FAILED && !s_network_lease_active);
        assert(!wifi_starts && !wifi_stops && warnings == 1 && snapshots == 1);
        assert(strstr(update.message, protected_session == 0 ? "热点" : "正在使用或切换"));
        assert(atomic_load(&s_wifi_leases) == (protected_session == 3 ? 1u : 0u));
    }
    reset_ota(NETWORK_ROUTE_WIFI); configured = false;
    assert(!online(&owned) && strstr(update.message, "尚未保存Wi-Fi"));
    assert(!wifi_starts && !s_network_lease_active);
    for (int automatic = 0; automatic < 2; ++automatic) {
        reset_ota(automatic ? NETWORK_ROUTE_AUTO : NETWORK_ROUTE_WIFI);
        wifi_start_error = ESP_ERR_NO_MEM;
        assert(!online(&owned) && strstr(update.message, "内存不足"));
        assert(wifi_starts == 1 && !s_network_lease_active && !atomic_load(&s_wifi_leases));
    }

    struct { int state; const char *message; } failures[] = {
        {CELLULAR_SERVICE_NO_SIM, "未检测到SIM卡"},
        {CELLULAR_SERVICE_SIM_NOT_READY, "SIM卡未就绪"},
        {CELLULAR_SERVICE_NO_REGISTRATION, "移动网络注册失败"},
        {CELLULAR_SERVICE_ERROR, "移动网络连接失败"},
    };
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        reset_ota(NETWORK_ROUTE_CELLULAR); cellular.enabled = true;
        cellular.state = failures[i].state;
        assert(!online(&owned) && strstr(update.message, failures[i].message));
        assert(update.state == PICO_UPDATE_FAILED && !s_network_lease_active);
        assert(!wifi_starts && !cell_starts && warnings == 1 && snapshots == 1);
    }
    reset_ota(NETWORK_ROUTE_CELLULAR);
    assert(!online(&owned) && strstr(update.message, "先开启移动网络"));
    reset_ota(NETWORK_ROUTE_CELLULAR); cellular.capable = false;
    assert(!online(&owned) && strstr(update.message, "未检测到4G模块"));
    reset_ota(NETWORK_ROUTE_CELLULAR);
    cellular.capability_known = cellular.capable = false;
    assert(!online(&owned) && strstr(update.message, "正在检测移动网络"));
    assert(!wifi_starts);

    reset_ota(NETWORK_ROUTE_WIFI);
    wifi = (wifi_transfer_status_t){.state=WIFI_TRANSFER_STARTING,
        .mode=WIFI_TRANSFER_MODE_STA, .network_only=true};
    assert(!online(&owned) && now_us == 30000000);
    assert(update.state == PICO_UPDATE_FAILED && strstr(update.message, "连接超时"));
    assert(!s_network_lease_active && !s_network_lease.active && !wifi_stops);
    assert(!atomic_load(&s_wifi_leases));
    reset_ota(NETWORK_ROUTE_CELLULAR); cellular.enabled = true; deny_registration = true;
    assert(!online(&owned) && strstr(update.message, "移动网络注册失败"));
    assert(!s_network_lease_active && !s_network_lease.active && !atomic_load(&s_cellular_leases));
    assert(!wifi_starts);

    reset_ota(NETWORK_ROUTE_WIFI); clock_utc = 0;
    assert(online(&owned) && sync_calls == 1 && time_notes == 1);
    release_ota();
    reset_ota(NETWORK_ROUTE_WIFI); clock_utc = 0; sync_error = ESP_ERR_TIMEOUT;
    assert(!online(&owned) && strstr(update.message, "网络对时失败"));
    assert(sync_calls == 1 && !time_notes);
    release_ota(); assert(wifi_stops == 1);
    reset_ota(NETWORK_ROUTE_WIFI); atomic_store(&s_cancel, true);
    assert(!online(&owned));
    release_ota(); assert(wifi_stops == 1);
    puts("PASS: device OTA admission, Wi-Fi retry, protected sessions, actionable failures and lease cleanup");
}
'''


def main(cases=CASES, extra=""):
    with tempfile.TemporaryDirectory(prefix="network-route-") as folder:
        work = Path(folder)
        env, compiler = toolchain(work)
        (work / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n"
            "#define ESP_ERR_NOT_FOUND -1\n#define ESP_ERR_INVALID_ARG -2\n#define ESP_ERR_INVALID_STATE -3\n"
            "#define ESP_ERR_NO_MEM -4\n#define ESP_ERR_TIMEOUT -5\n")
        unit = MODEL
        for name in ("resolve_ready_bearer", "saved_wifi", "start_wifi_for_lease",
                     "start_cellular_for_lease", "select_bearer_netif",
                     "acquire_bearer", "network_service_poll",
                     "network_service_get_status", "network_service_acquire",
                     "network_service_wait_ready", "network_service_release", "network_service_set_enabled"):
            unit += function(name, SOURCE) + "\n"
        source, binary = work / "test.c", work / "test"
        source.write_text(unit + extra + cases)
        subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
                                  "-fsanitize=address,undefined", "-I" + str(work),
                                  "-Imain/services", "-Imain/services/cellular", "-Imain/ota", str(source),
                                  "-o", str(binary)], cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    main()
    ota_source = ROOT / "main/ota/ota_online.c"
    ota_extra = OTA_MODEL
    for symbol in ("ota_bearer", "network_unavailable_notice", "online"):
        ota_extra += function(symbol, ota_source) + "\n"
    main(OTA_CASES, ota_extra)
