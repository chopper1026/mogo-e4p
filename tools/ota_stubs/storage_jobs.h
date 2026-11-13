/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>

static inline bool storage_jobs_quiesce(unsigned timeout_ms) {
    (void)timeout_ms;
    return true;
}
