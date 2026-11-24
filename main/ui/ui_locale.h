/* 仅本地化系统文案，不修改用户文本。/ Localize system copy, never user content. */
#pragma once
#ifdef ESP_PLATFORM
#include "../settings/ui_language.h"
const char *ui_tr(const char *literal) __attribute__((format_arg(1)));
const char *ui_tr_for(ui_language_t language, const char *literal);
#else
#define ui_tr(s) (s)
#endif
