/* 引导入口，不依赖具体页面。/ Setup intents without page-to-page dependencies. */
#pragma once
#include <app.h>
#include "ui_language.h"
typedef enum {
    APP_SETUP_RESUME,
    APP_SETUP_REPLAY,
    APP_SETUP_LANGUAGE_ONLY,
    APP_SETUP_FONT_ONLY
} app_setup_mode_t;
const app_desc_t* app_setup_start(app_setup_mode_t mode);
