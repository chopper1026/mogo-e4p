/* SPDX-License-Identifier: Apache-2.0
 * External-network route and lease service.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "network_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool cellular_capability_known;
    bool cellular_capable;
    bool cellular_enabled;
    bool cellular_ready;
    int cellular_signal;
    uint8_t cellular_state;
    bool wifi_enabled;
    bool wifi_ready;
    network_route_t route;
    network_auto_priority_t auto_priority;
    network_bearer_t active_bearer;
} network_status_t;

typedef struct {
    network_bearer_t bearer;
    bool owns_wifi;
    bool owns_cellular;
    bool active;
} network_lease_t;

void network_service_init(void);
void network_service_poll(void);
void network_service_get_status(network_status_t *out);

/// Toggle one radio without changing the business route preference.
esp_err_t network_service_set_enabled(network_bearer_t bearer, bool enabled);
esp_err_t network_service_set_route(network_route_t route, network_auto_priority_t priority);
network_route_t network_service_route(void);
network_auto_priority_t network_service_auto_priority(void);

/// Acquire the user-selected bearer. Forced routes fail closed; AUTO follows priority.
esp_err_t network_service_acquire(network_use_t use, network_lease_t *lease);
bool network_service_wait_ready(network_lease_t *lease, uint32_t timeout_ms);
void network_service_release(network_lease_t *lease);

/// Synchronize time through the selected external bearer. The automatic form
/// remains cancellable by the existing time-service cancellation path.
esp_err_t network_service_sync_time(uint32_t *utc_seconds, bool automatic);
esp_err_t network_service_sync_time_on_lease(network_lease_t *lease, uint32_t *utc_seconds);
void network_service_cancel_time_sync(void);
/// Consume a successful cellular-side sync on the main loop. Wi-Fi keeps its
/// existing pending-event queue for compatibility.
bool network_service_take_time_sync(uint32_t *utc_seconds);

#ifdef __cplusplus
}
#endif
