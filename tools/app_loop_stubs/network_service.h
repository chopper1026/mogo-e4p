/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "common.h"
#include "wifi_transfer.h"

#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 4
#endif

typedef enum {
    NETWORK_BEARER_NONE = 0,
    NETWORK_BEARER_WIFI,
    NETWORK_BEARER_CELLULAR,
} network_bearer_t;

typedef struct {
    bool cellular_capability_known;
    bool cellular_capable;
    bool cellular_enabled;
    bool cellular_ready;
    int cellular_signal;
    unsigned char cellular_state;
    bool wifi_enabled;
    bool wifi_ready;
    int route;
    int auto_priority;
    network_bearer_t active_bearer;
} network_status_t;

static inline void network_service_init(void) {}
static inline void network_service_poll(void) {}

static inline void network_service_get_status(network_status_t *out) {
    wifi_transfer_status_t wifi = {0};
    if (!out) return;
    wifi_transfer_get_status(&wifi);
    *out = (network_status_t){
        .cellular_capability_known = true,
        .cellular_capable = false,
        .wifi_enabled = wifi.state != WIFI_TRANSFER_STOPPED,
        .wifi_ready = wifi.mode == WIFI_TRANSFER_MODE_STA && wifi.network_ready,
        .active_bearer = wifi.mode == WIFI_TRANSFER_MODE_STA && wifi.network_ready
            ? NETWORK_BEARER_WIFI : NETWORK_BEARER_NONE,
    };
}

static inline esp_err_t network_service_set_enabled(network_bearer_t bearer, bool enabled) {
    if (bearer != NETWORK_BEARER_WIFI) return ESP_ERR_NOT_FOUND;
    if (enabled) {
        char ssid[33] = {0};
        bool configured = false;
        if (wifi_transfer_get_saved_wifi(ssid, &configured) != ESP_OK || !configured)
            return ESP_ERR_NOT_FOUND;
        wifi_transfer_status_t status = {0};
        wifi_transfer_get_status(&status);
        if (status.state == WIFI_TRANSFER_STOPPED) {
            const wifi_transfer_cfg_t cfg = {.mode = WIFI_TRANSFER_MODE_STA, .network_only = true};
            return wifi_transfer_start(&cfg);
        }
        return ESP_OK;
    }
    return wifi_transfer_try_stop_if_idle() ? ESP_OK : ESP_ERR_INVALID_STATE;
}
