/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

/* Host scheduler tests model the no-modem variant of the unified image. */
static inline bool cellular_service_capable(void) { return false; }
static inline bool cellular_service_pause_for_sleep(void) { return true; }
static inline void cellular_service_resume_after_sleep(void) {}
