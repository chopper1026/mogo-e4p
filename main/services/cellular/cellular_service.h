/* SPDX-License-Identifier: Apache-2.0
 * Runtime NT26 capability and cellular bearer service.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CELLULAR_SERVICE_DISABLED = 0,
    CELLULAR_SERVICE_STOPPED,
    CELLULAR_SERVICE_PROBING,
    CELLULAR_SERVICE_STARTING,
    CELLULAR_SERVICE_READY,
    CELLULAR_SERVICE_NO_SIM,
    CELLULAR_SERVICE_NO_REGISTRATION,
    CELLULAR_SERVICE_ERROR,
    CELLULAR_SERVICE_SIM_NOT_READY,
} cellular_service_state_t;

typedef struct {
    /// A probe has completed; false means the unified image is still probing.
    bool capability_known;
    /// The NT26 hardware answered its AT probe. SIM/registration are runtime state.
    bool capable;
    bool enabled;
    bool ready;
    cellular_service_state_t state;
    int signal_strength;
    char detail[96];
} cellular_service_status_t;

void cellular_service_init(void);
void cellular_service_poll(void);
esp_err_t cellular_service_set_enabled(bool enabled);
/// Stop the modem before board_ioe_sleep without changing the user's setting.
bool cellular_service_pause_for_sleep(void);
/// Restore the modem/probe after board_ioe_wake; keeps the user's setting.
void cellular_service_resume_after_sleep(void);
void cellular_service_get_status(cellular_service_status_t *out);
bool cellular_service_capable(void);
bool cellular_service_enabled(void);
bool cellular_service_ready(void);

/// Return the modem-created Ethernet netif while the service is running.
/// The pointer is owned by the service and becomes invalid after it is stopped.
void *cellular_service_netif(void);

#ifdef __cplusplus
}
#endif
