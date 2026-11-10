/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "time/calendar.h"
/// 固定684×1216的两种日历画面；空日期显示校时提示。/ Two fixed 684×1216 calendar faces; a null date shows a clock prompt.
void ui_calendar_draw(uint8_t *framebuffer, const calendar_date_t *date, bool monthly);
