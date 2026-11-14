/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_http_client.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
void network_diagnostics_snapshot(const char *owner, int bearer);
void network_diagnostics_http_failure(const char *owner, esp_http_client_handle_t client,
                                      const char *phase, long result, int bearer);
void network_diagnostics_http_response(const char *owner, esp_http_client_handle_t client, size_t received);
void network_diagnostics_data_failure(const char *owner, const char *phase, const char *reason,
                                      size_t received, int64_t reference, int bearer);
#else
static inline void network_diagnostics_snapshot(const char *owner, int bearer) {
    (void)owner; (void)bearer;
}
static inline void network_diagnostics_http_failure(const char *owner,
        esp_http_client_handle_t client, const char *phase, long result, int bearer) {
    (void)owner; (void)client; (void)phase; (void)result; (void)bearer;
}
static inline void network_diagnostics_http_response(const char *owner,
        esp_http_client_handle_t client, size_t received) {
    (void)owner; (void)client; (void)received;
}
static inline void network_diagnostics_data_failure(const char *owner, const char *phase,
        const char *reason, size_t received, int64_t reference, int bearer) {
    (void)owner; (void)phase; (void)reason; (void)received; (void)reference; (void)bearer;
}
#endif

#ifdef __cplusplus
}
#endif
