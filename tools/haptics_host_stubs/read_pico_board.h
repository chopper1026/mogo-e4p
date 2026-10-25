/* SPDX-License-Identifier: Apache-2.0 */
/* 宿主替身：马达板级接口。/ Host shim for the board-level motor API. */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

bool read_pico_has_motor(void);
esp_err_t read_pico_motor(bool on);
