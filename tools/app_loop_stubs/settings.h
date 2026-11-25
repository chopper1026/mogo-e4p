/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"
#include "app/app_quick_items.h"
const app_quick_layout_t *app_settings_quick_layout(void);

bool app_settings_ble_turner(void);
void app_settings_set_ble_turner(bool enabled);
bool app_settings_gestures(void);
unsigned app_settings_gesture_width(void);
int app_settings_gesture_edge_px(void);
bool app_settings_touch_haptics(void);
void app_settings_set_touch_haptics(bool enabled);

bool app_settings_frontlight_on(void);
void app_settings_set_frontlight_on(bool on);
uint8_t app_settings_frontlight_brightness(void);
void app_settings_set_frontlight_brightness(uint8_t percent);
uint8_t app_settings_frontlight_cct(void);
void app_settings_set_frontlight_cct(uint8_t percent);

uint8_t app_settings_auto_lock_minutes(void);

uint8_t app_settings_system_contrast(void);
