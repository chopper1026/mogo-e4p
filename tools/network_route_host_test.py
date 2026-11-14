#!/usr/bin/env python3
"""Run real bearer acquisition, fallback and lease release against radio models.
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
enum { WIFI_TRANSFER_STOPPED, WIFI_TRANSFER_STARTING, WIFI_TRANSFER_READY, WIFI_TRANSFER_ERROR };
enum { WIFI_TRANSFER_MODE_STA = 1 };
typedef struct { int state, mode; bool network_ready; } wifi_transfer_status_t;
typedef struct { int mode; bool network_only; } wifi_transfer_cfg_t;
static network_route_t s_route;
static network_auto_priority_t s_priority;
static atomic_uint s_wifi_leases, s_cellular_leases;
static atomic_bool s_cellular_control;
static cellular_service_status_t cellular;
static wifi_transfer_status_t wifi;
static bool configured, deny_registration, fail_wifi, cell_recovers;
static bool missing_wifi_netif, missing_cell_netif, fail_default;
static unsigned default_calls, cell_status_reads, ready_on_cell_read;
static unsigned wifi_starts, wifi_stops, cell_starts, cell_stops;
static int64_t now_us;
static int wifi_marker, cell_marker;
static void *default_netif;
static int64_t esp_timer_get_time(void) { return now_us; }
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(unsigned ms) { now_us += (int64_t)ms * 1000; }
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
    ++wifi_starts; wifi.mode = cfg->mode;
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
static bool wifi_transfer_try_stop_if_idle(void) { wifi_transfer_stop(); return true; }
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
    default_calls = cell_status_reads = ready_on_cell_read = 0;
    wifi_starts = wifi_stops = cell_starts = cell_stops = 0; default_netif = NULL;
}
'''

CASES = r'''
int main(void) {
    network_lease_t lease;
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


def main():
    with tempfile.TemporaryDirectory(prefix="network-route-") as folder:
        work = Path(folder)
        env, compiler = toolchain(work)
        (work / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n"
            "#define ESP_ERR_NOT_FOUND -1\n#define ESP_ERR_INVALID_ARG -2\n#define ESP_ERR_INVALID_STATE -3\n")
        unit = MODEL
        for name in ("resolve_ready_bearer", "saved_wifi", "start_wifi_for_lease",
                     "start_cellular_for_lease", "reserve_cellular_lease", "select_bearer_netif",
                     "acquire_bearer", "network_service_poll",
                     "network_service_get_status", "network_service_acquire",
                     "network_service_wait_ready", "network_service_release", "network_service_set_enabled"):
            unit += function(name, SOURCE) + "\n"
        source, binary = work / "test.c", work / "test"
        source.write_text(unit + CASES)
        subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
                                  "-fsanitize=address,undefined", "-I" + str(work),
                                  "-Imain/services", "-Imain/services/cellular", str(source),
                                  "-o", str(binary)], cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    main()
