/* SPDX-License-Identifier: Apache-2.0
 * 中文：4G HTTPS 的有界 DNS 地址选择；会话由单个业务任务独占。
 * English: Bounded DNS endpoint selection for HTTPS over cellular; one worker owns each session.
 */
#pragma once

#include "esp_http_client.h"
#include "network_policy.h"
#include <stdint.h>

typedef struct {
    char hostname[254];
    uint32_t address, preferred_address, failed_address;
    bool addressed;
} network_http_session_t;

// Zero-initialize session and keep it alive until cleanup; TLS retains its hostname pointer.
esp_http_client_handle_t network_http_client_init(const esp_http_client_config_t *config,
    network_bearer_t bearer, network_http_session_t *session, esp_err_t *error);
// Report transport outcome once, before cleanup. Cancellation and business errors need no failure report.
void network_http_note_result(network_http_session_t *session, bool connected);
