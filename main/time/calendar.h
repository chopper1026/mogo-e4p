/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/// 离线公农历与民俗数据，公历支持2000–2099年。/ Offline solar/lunar and folk data for 2000–2099.
typedef struct {
    int year, month, day, weekday, days, first_weekday;
    int lunar_year, lunar_month, lunar_day;
    int year_cycle, month_cycle, day_cycle;
    const char *yi, *ji, *wealth, *clash, *sha;
    /// 日粒度民俗方位、神煞与建除，沿用固定源的默认流派。/ Civil-day folk directions, spirits and officer using the pinned source's default school.
    const char *joy, *fortune, *auspicious, *inauspicious, *officer, *guardian, *guardian_type;
} calendar_date_t;
/// 输入北京时间的公历日期；非法日期返回false并清空输出。/ Accept a Beijing civil date; clear output and return false for invalid dates.
bool calendar_date(int year, int month, int day, calendar_date_t *out);
/// UTC毫秒转北京时间；未校准的产品时钟（2024年前）不显示假日期。/ Convert UTC milliseconds to Beijing time; reject uncalibrated product clocks before 2024.
bool calendar_from_utc(int64_t milliseconds, calendar_date_t *out);
/// 格式化农历日期或月历格短标签，节气优先。/ Format lunar date or short grid label, preferring solar terms.
void calendar_lunar_text(const calendar_date_t *date, bool short_label, char *out, unsigned capacity);
/// 干支和当月两节气。/ Stem/branch names and the month's two solar terms.
const char *calendar_cycle_name(int cycle);
const char *calendar_term(int year, int month, int which, int *day);
const char *calendar_month_name(int month);
