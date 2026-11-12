/* SPDX-License-Identifier: Apache-2.0
 * Shared bearer and business-route policy vocabulary.
 */
#pragma once

#include <stdbool.h>

typedef enum {
    NETWORK_BEARER_NONE = 0,
    NETWORK_BEARER_WIFI,
    NETWORK_BEARER_CELLULAR,
} network_bearer_t;

typedef enum {
    NETWORK_ROUTE_WIFI = 0,
    NETWORK_ROUTE_CELLULAR,
    NETWORK_ROUTE_AUTO,
} network_route_t;

typedef enum {
    NETWORK_AUTO_WIFI_FIRST = 0,
    NETWORK_AUTO_CELLULAR_FIRST,
} network_auto_priority_t;

typedef enum {
    NETWORK_USE_TIME = 0,
    NETWORK_USE_WEATHER,
    NETWORK_USE_OTA,
} network_use_t;

static inline bool network_route_valid(network_route_t route) {
    return route >= NETWORK_ROUTE_WIFI && route <= NETWORK_ROUTE_AUTO;
}

static inline bool network_auto_priority_valid(network_auto_priority_t priority) {
    return priority == NETWORK_AUTO_WIFI_FIRST || priority == NETWORK_AUTO_CELLULAR_FIRST;
}

const char *network_route_name(network_route_t route);
const char *network_auto_priority_name(network_auto_priority_t priority);
