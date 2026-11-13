/* SPDX-License-Identifier: Apache-2.0
 *
 * The service is the only owner of the external business bearer decision.
 * Manual Wi-Fi and cellular enable actions are mutually exclusive: enabling
 * one bearer stops the other when it is idle and selects that bearer for
 * business traffic. Wi-Fi transfer remains the owner of its AP/STA server;
 * this layer only borrows its network-only STA path for time, weather and OTA.
 */
#include "network_service.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "cellular_service.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"
#include "wifi_transfer.h"

static network_route_t s_route = NETWORK_ROUTE_AUTO;
static network_auto_priority_t s_priority = NETWORK_AUTO_WIFI_FIRST;
static atomic_bool s_time_cancel;
static atomic_uint s_cellular_time_pending;
static atomic_bool s_time_active;
static atomic_uint s_wifi_leases;
static atomic_uint s_cellular_leases;
static atomic_bool s_cellular_control;

static bool time_cancelled(void) {
    return atomic_load(&s_time_cancel);
}

static esp_err_t sync_time_cellular(uint32_t *utc_seconds) {
    if (!utc_seconds || time_cancelled()) return ESP_ERR_INVALID_STATE;
    *utc_seconds = 0;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("203.107.6.88", "162.159.200.123", "ntp.aliyun.com"));
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) return err;
    const int64_t deadline = esp_timer_get_time() + 20000000;
    do {
        if (time_cancelled()) { err = ESP_ERR_INVALID_STATE; break; }
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(100));
    } while (err == ESP_ERR_TIMEOUT && esp_timer_get_time() < deadline);
    if (err == ESP_OK && time_cancelled()) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        time_t now = time(NULL);
        if (now >= 1704067200 && now <= 4102444799U) {
            *utc_seconds = (uint32_t)now;
        } else {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    esp_netif_sntp_deinit();
    return err;
}

esp_err_t network_service_sync_time_on_lease(network_lease_t *lease, uint32_t *utc_seconds) {
    if (!lease || !lease->active || !utc_seconds) return ESP_ERR_INVALID_ARG;
    *utc_seconds = 0;
    if (time_cancelled()) return ESP_ERR_INVALID_STATE;
    if (!network_service_wait_ready(lease, 30000)) return ESP_ERR_TIMEOUT;
    if (lease->bearer == NETWORK_BEARER_WIFI)
        return wifi_transfer_sync_time_online(utc_seconds);
    if (lease->bearer == NETWORK_BEARER_CELLULAR)
        return sync_time_cellular(utc_seconds);
    return ESP_ERR_NOT_FOUND;
}

static network_bearer_t resolve_ready_bearer(const network_status_t *status) {
    if (!status) return NETWORK_BEARER_NONE;
    if (status->route == NETWORK_ROUTE_WIFI)
        return status->wifi_ready ? NETWORK_BEARER_WIFI : NETWORK_BEARER_NONE;
    if (status->route == NETWORK_ROUTE_CELLULAR)
        return status->cellular_ready ? NETWORK_BEARER_CELLULAR : NETWORK_BEARER_NONE;
    if (status->auto_priority == NETWORK_AUTO_CELLULAR_FIRST) {
        if (status->cellular_ready) return NETWORK_BEARER_CELLULAR;
        if (status->wifi_ready) return NETWORK_BEARER_WIFI;
    } else {
        if (status->wifi_ready) return NETWORK_BEARER_WIFI;
        if (status->cellular_ready) return NETWORK_BEARER_CELLULAR;
    }
    return NETWORK_BEARER_NONE;
}

static bool saved_wifi(void) {
    char ssid[33] = {0};
    bool configured = false;
    return wifi_transfer_get_saved_wifi(ssid, &configured) == ESP_OK && configured;
}

static esp_err_t start_wifi_for_lease(network_lease_t *lease) {
    wifi_transfer_status_t status;
    wifi_transfer_get_status(&status);
    if (status.mode == WIFI_TRANSFER_MODE_STA && status.network_ready) {
        lease->owns_wifi = false;
        return ESP_OK;
    }
    if (status.state != WIFI_TRANSFER_STOPPED || !saved_wifi()) return ESP_ERR_NOT_FOUND;
    const wifi_transfer_cfg_t cfg = {.mode = WIFI_TRANSFER_MODE_STA, .network_only = true};
    esp_err_t err = wifi_transfer_start(&cfg);
    if (err == ESP_OK) lease->owns_wifi = true;
    return err;
}

static bool start_cellular_for_lease(const network_status_t *status) {
    if (!status || !status->cellular_capable || !status->cellular_enabled ||
        status->cellular_ready) return false;
    // These states are terminal for the current SIM/registration attempt.
    // AUTO must be allowed to fall through to the other bearer immediately.
    if (status->cellular_state == CELLULAR_SERVICE_NO_SIM ||
        status->cellular_state == CELLULAR_SERVICE_SIM_NOT_READY ||
        status->cellular_state == CELLULAR_SERVICE_NO_REGISTRATION ||
        status->cellular_state == CELLULAR_SERVICE_ERROR) return false;
    return cellular_service_set_enabled(true) == ESP_OK;
}

static bool reserve_cellular_lease(void) {
    if (atomic_load(&s_cellular_control)) return false;
    atomic_fetch_add(&s_cellular_leases, 1);
    if (!atomic_load(&s_cellular_control)) return true;
    atomic_fetch_sub(&s_cellular_leases, 1);
    return false;
}

static esp_err_t acquire_bearer(network_bearer_t bearer, network_lease_t *lease) {
    if (!lease) return ESP_ERR_INVALID_ARG;
    *lease = (network_lease_t){0};
    if (bearer == NETWORK_BEARER_CELLULAR && !reserve_cellular_lease())
        return ESP_ERR_INVALID_STATE;
    network_status_t status;
    network_service_get_status(&status);
    if (bearer == NETWORK_BEARER_WIFI) {
        if (status.wifi_ready || start_wifi_for_lease(lease) == ESP_OK) {
            lease->bearer = NETWORK_BEARER_WIFI;
            lease->active = true;
        }
    } else if (bearer == NETWORK_BEARER_CELLULAR) {
        if (status.cellular_ready || start_cellular_for_lease(&status)) {
            lease->bearer = NETWORK_BEARER_CELLULAR;
            lease->active = true;
        }
    }
    if (!lease->active) {
        if (bearer == NETWORK_BEARER_CELLULAR) atomic_fetch_sub(&s_cellular_leases, 1);
        return ESP_ERR_NOT_FOUND;
    }
    if (lease->bearer == NETWORK_BEARER_WIFI) atomic_fetch_add(&s_wifi_leases, 1);
    void *netif = lease->bearer == NETWORK_BEARER_CELLULAR
        ? cellular_service_netif() : wifi_transfer_netif();
    if (netif) (void)esp_netif_set_default_netif((esp_netif_t *)netif);
    return ESP_OK;
}

void network_service_init(void) {
    s_route = (network_route_t)app_settings_network_route();
    s_priority = (network_auto_priority_t)app_settings_network_auto_priority();
    if (!network_route_valid(s_route)) s_route = NETWORK_ROUTE_AUTO;
    if (!network_auto_priority_valid(s_priority)) s_priority = NETWORK_AUTO_WIFI_FIRST;
    atomic_store(&s_time_cancel, false);
    atomic_store(&s_cellular_time_pending, 0);
    atomic_store(&s_time_active, false);
    atomic_store(&s_wifi_leases, 0);
    atomic_store(&s_cellular_leases, 0);
    atomic_store(&s_cellular_control, false);
}

void network_service_poll(void) {
    cellular_service_poll();
    network_status_t status;
    network_service_get_status(&status);
    void *netif = status.active_bearer == NETWORK_BEARER_CELLULAR
        ? cellular_service_netif() : wifi_transfer_netif();
    if (netif) (void)esp_netif_set_default_netif((esp_netif_t *)netif);
}

void network_service_get_status(network_status_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    wifi_transfer_status_t wifi = {0};
    cellular_service_status_t cellular = {0};
    wifi_transfer_get_status(&wifi);
    cellular_service_get_status(&cellular);
    out->cellular_capability_known = cellular.capability_known;
    out->cellular_capable = cellular.capable;
    out->cellular_enabled = cellular.enabled;
    out->cellular_ready = cellular.ready;
    out->cellular_signal = cellular.signal_strength;
    out->cellular_state = (uint8_t)cellular.state;
    memcpy(out->cellular_detail, cellular.detail, sizeof(out->cellular_detail));
    out->cellular_detail[sizeof(out->cellular_detail) - 1] = '\0';
    out->wifi_enabled = wifi.state != WIFI_TRANSFER_STOPPED;
    out->wifi_ready = wifi.mode == WIFI_TRANSFER_MODE_STA && wifi.network_ready;
    out->route = s_route;
    out->auto_priority = s_priority;
    out->active_bearer = resolve_ready_bearer(out);
}

esp_err_t network_service_set_enabled(network_bearer_t bearer, bool enabled) {
    if (bearer != NETWORK_BEARER_WIFI && bearer != NETWORK_BEARER_CELLULAR)
        return ESP_ERR_INVALID_ARG;
    bool idle = false;
    if (!atomic_compare_exchange_strong(&s_cellular_control, &idle, true))
        return ESP_ERR_INVALID_STATE;

    esp_err_t err = ESP_OK;
    if (!enabled) {
        if (bearer == NETWORK_BEARER_CELLULAR) {
            err = atomic_load(&s_cellular_leases) != 0
                ? ESP_ERR_INVALID_STATE : cellular_service_set_enabled(false);
        } else {
            err = atomic_load(&s_wifi_leases) != 0
                ? ESP_ERR_INVALID_STATE
                : (wifi_transfer_try_stop_if_idle() ? ESP_OK : ESP_ERR_INVALID_STATE);
        }
        atomic_store(&s_cellular_control, false);
        return err;
    }

    if (bearer == NETWORK_BEARER_CELLULAR) {
        // A live Wi-Fi business lease cannot be interrupted by a manual 4G switch.
        if (atomic_load(&s_wifi_leases) != 0) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            wifi_transfer_status_t wifi = {0};
            wifi_transfer_get_status(&wifi);
            const bool wifi_was_enabled = wifi.state != WIFI_TRANSFER_STOPPED;
            if (wifi_was_enabled && !wifi_transfer_try_stop_if_idle()) err = ESP_ERR_INVALID_STATE;
            if (err == ESP_OK) err = cellular_service_set_enabled(true);
        }
        if (err == ESP_OK) {
            s_route = NETWORK_ROUTE_CELLULAR;
            app_settings_set_network_route(s_route);
        }
        atomic_store(&s_cellular_control, false);
        return err;
    }

    // A live cellular business lease cannot be interrupted by a manual Wi-Fi switch.
    if (atomic_load(&s_cellular_leases) != 0) err = ESP_ERR_INVALID_STATE;
    wifi_transfer_status_t wifi = {0};
    wifi_transfer_get_status(&wifi);
    cellular_service_status_t cellular = {0};
    cellular_service_get_status(&cellular);
    const bool cellular_was_enabled = cellular.enabled;
    if (err == ESP_OK && !saved_wifi()) err = ESP_ERR_NOT_FOUND;
    if (err == ESP_OK && cellular_was_enabled) err = cellular_service_set_enabled(false);
    if (err == ESP_OK && wifi.state == WIFI_TRANSFER_STOPPED) {
        const wifi_transfer_cfg_t cfg = {.mode = WIFI_TRANSFER_MODE_STA, .network_only = true};
        err = wifi_transfer_start(&cfg);
    } else if (err == ESP_OK && wifi.state != WIFI_TRANSFER_STOPPED) {
        // Wi-Fi is already on; disabling cellular above is enough to enforce exclusivity.
        err = ESP_OK;
    }
    if (err != ESP_OK && cellular_was_enabled) (void)cellular_service_set_enabled(true);
    if (err == ESP_OK) {
        s_route = NETWORK_ROUTE_WIFI;
        app_settings_set_network_route(s_route);
    }
    atomic_store(&s_cellular_control, false);
    return err;
}

esp_err_t network_service_prepare_wifi(void) {
    bool idle = false;
    if (!atomic_compare_exchange_strong(&s_cellular_control, &idle, true))
        return ESP_ERR_INVALID_STATE;
    const esp_err_t err = atomic_load(&s_cellular_leases) != 0
        ? ESP_ERR_INVALID_STATE : cellular_service_set_enabled(false);
    atomic_store(&s_cellular_control, false);
    return err;
}

esp_err_t network_service_set_route(network_route_t route, network_auto_priority_t priority) {
    if (!network_route_valid(route) || !network_auto_priority_valid(priority)) return ESP_ERR_INVALID_ARG;
    cellular_service_status_t cellular = {0};
    cellular_service_get_status(&cellular);
    if (route == NETWORK_ROUTE_CELLULAR && cellular.capability_known && !cellular.capable)
        return ESP_ERR_NOT_SUPPORTED;
    s_route = route;
    s_priority = priority;
    app_settings_set_network_route(route);
    app_settings_set_network_auto_priority(priority);
    return ESP_OK;
}

network_route_t network_service_route(void) { return s_route; }
network_auto_priority_t network_service_auto_priority(void) { return s_priority; }

esp_err_t network_service_acquire(network_use_t use, network_lease_t *lease) {
    (void)use;
    if (!lease) return ESP_ERR_INVALID_ARG;
    *lease = (network_lease_t){0};
    network_status_t status;
    network_service_get_status(&status);
    network_bearer_t first = resolve_ready_bearer(&status);
    if (first == NETWORK_BEARER_NONE) {
        first = status.route == NETWORK_ROUTE_CELLULAR ||
            (status.route == NETWORK_ROUTE_AUTO && status.auto_priority == NETWORK_AUTO_CELLULAR_FIRST)
            ? NETWORK_BEARER_CELLULAR : NETWORK_BEARER_WIFI;
    }
    esp_err_t err = acquire_bearer(first, lease);
    if (err == ESP_OK || status.route != NETWORK_ROUTE_AUTO) return err;
    return acquire_bearer(first == NETWORK_BEARER_WIFI
        ? NETWORK_BEARER_CELLULAR : NETWORK_BEARER_WIFI, lease);
}

bool network_service_wait_ready(network_lease_t *lease, uint32_t timeout_ms) {
    if (!lease || !lease->active) return false;
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (unsigned attempt = 0; attempt < 2 && lease->active; ++attempt) {
        bool failed = false;
        while (esp_timer_get_time() < deadline) {
            network_service_poll();
            network_status_t status;
            network_service_get_status(&status);
            if ((lease->bearer == NETWORK_BEARER_WIFI && status.wifi_ready) ||
                (lease->bearer == NETWORK_BEARER_CELLULAR && status.cellular_ready)) return true;
            if (lease->bearer == NETWORK_BEARER_CELLULAR &&
                (status.cellular_state == CELLULAR_SERVICE_NO_SIM ||
                 status.cellular_state == CELLULAR_SERVICE_SIM_NOT_READY ||
                 status.cellular_state == CELLULAR_SERVICE_NO_REGISTRATION ||
                 status.cellular_state == CELLULAR_SERVICE_ERROR ||
                 (status.cellular_state == CELLULAR_SERVICE_STOPPED &&
                  status.cellular_capability_known))) {
                failed = true;
                break;
            }
            wifi_transfer_status_t wifi = {0};
            wifi_transfer_get_status(&wifi);
            if (lease->bearer == NETWORK_BEARER_WIFI && wifi.state == WIFI_TRANSFER_ERROR) {
                failed = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!failed && esp_timer_get_time() >= deadline) break;
        // AUTO may use the other bearer after a terminal registration or
        // connection error. Forced routes intentionally fail closed.
        if (s_route != NETWORK_ROUTE_AUTO || attempt != 0) break;
        const network_bearer_t fallback = lease->bearer == NETWORK_BEARER_WIFI
            ? NETWORK_BEARER_CELLULAR : NETWORK_BEARER_WIFI;
        network_service_release(lease);
        network_lease_t replacement;
        if (acquire_bearer(fallback, &replacement) != ESP_OK) break;
        *lease = replacement;
    }
    return false;
}

void network_service_release(network_lease_t *lease) {
    if (!lease || !lease->active) return;
    if (lease->owns_wifi) wifi_transfer_stop();
    if (lease->bearer == NETWORK_BEARER_WIFI) atomic_fetch_sub(&s_wifi_leases, 1);
    else if (lease->bearer == NETWORK_BEARER_CELLULAR) atomic_fetch_sub(&s_cellular_leases, 1);
    *lease = (network_lease_t){0};
}

esp_err_t network_service_sync_time(uint32_t *utc_seconds, bool automatic) {
    if (!utc_seconds) return ESP_ERR_INVALID_ARG;
    *utc_seconds = 0;
    if (!automatic) network_service_cancel_time_sync();
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_time_active, &expected, true)) return ESP_ERR_INVALID_STATE;
    atomic_store(&s_time_cancel, false);
    network_lease_t lease;
    esp_err_t err = network_service_acquire(NETWORK_USE_TIME, &lease);
    if (err != ESP_OK) {
        atomic_store(&s_time_active, false);
        return err;
    }
    err = network_service_sync_time_on_lease(&lease, utc_seconds);
    if (err == ESP_OK && lease.bearer == NETWORK_BEARER_CELLULAR)
        atomic_store(&s_cellular_time_pending, *utc_seconds);
    network_service_release(&lease);
    atomic_store(&s_time_active, false);
    return err;
}

void network_service_cancel_time_sync(void) {
    atomic_store(&s_time_cancel, true);
    (void)wifi_transfer_cancel_time_sync();
    const int64_t deadline = esp_timer_get_time() + 2000000;
    while (atomic_load(&s_time_active) && esp_timer_get_time() < deadline)
        vTaskDelay(pdMS_TO_TICKS(10));
}

bool network_service_take_time_sync(uint32_t *utc_seconds) {
    if (!utc_seconds) return false;
    if (wifi_transfer_take_time_sync(utc_seconds)) return true;
    uint32_t pending = atomic_exchange(&s_cellular_time_pending, 0);
    *utc_seconds = pending;
    return pending != 0;
}
