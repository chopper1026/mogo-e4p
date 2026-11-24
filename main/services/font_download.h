/* 文楷安装任务；工作线程只取得文件，UI 线程启用字体。/ WenKai download worker; only the UI thread activates a face. */
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
bool font_download_start(bool traditional);
font_job_state_t font_download_state(void);
unsigned font_download_percent(void);
const char* font_download_path(bool traditional);
uint32_t font_download_bytes(bool traditional);
bool font_download_quiesce(unsigned timeout_ms);
