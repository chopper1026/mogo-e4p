/* SPDX-License-Identifier: Apache-2.0 */
#include "network_policy.h"

const char *network_route_name(network_route_t route) {
    switch (route) {
        case NETWORK_ROUTE_WIFI: return "Wi-Fi";
        case NETWORK_ROUTE_CELLULAR: return "4G";
        case NETWORK_ROUTE_AUTO: return "自动";
        default: return "未知";
    }
}

const char *network_auto_priority_name(network_auto_priority_t priority) {
    return priority == NETWORK_AUTO_CELLULAR_FIRST ? "4G 优先" : "Wi-Fi 优先";
}
