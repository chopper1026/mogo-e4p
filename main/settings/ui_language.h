/* 系统语言与本机引导进度。/ System language and device-local setup progress. */
#pragma once
#include <stdbool.h>

typedef enum { UI_LANG_CN, UI_LANG_TW, UI_LANG_EN, UI_LANG_COUNT } ui_language_t;
// 这些数值已写入 NVS，不是显示顺序；新步骤只能追加。/ Persisted NVS codes, not display order; append new steps.
typedef enum {
    SETUP_LANGUAGE = 0, SETUP_FONT = 1, SETUP_HOME = 2, SETUP_DONE = 3,
    SETUP_WIFI = 4, SETUP_STEP_COUNT
} setup_step_t;
/// 读取已提交的 UI 语言，不访问存储。/ Read the committed UI language without I/O.
ui_language_t ui_language(void);
const char* ui_locale_tag(void);
const char* ui_language_name(ui_language_t language);
setup_step_t app_setup_step(void);
/// 同一事务保存语言与下一步。/ Commit the language and next step together.
bool app_setup_save(ui_language_t language, setup_step_t step);
