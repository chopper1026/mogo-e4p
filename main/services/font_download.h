/* 霞鹜新晰黑安装任务；工作线程只取得并校验 TTF，UI 线程启用字体。/ LXGW Neo XiHei installer worker; only the UI thread activates a face. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    FONT_IDLE,
    FONT_BUSY,
    FONT_READY,
    FONT_NEEDS_WIFI,
    FONT_NO_CARD,
    FONT_NO_SPACE,
    FONT_CONFLICT,
    FONT_FAILED,
    FONT_CANCELLED
} font_job_state_t;
bool font_download_start(void);
font_job_state_t font_download_state(void);
unsigned font_download_percent(void);
const char* font_download_path(void);
uint32_t font_download_bytes(void);
bool font_download_quiesce(unsigned timeout_ms);
