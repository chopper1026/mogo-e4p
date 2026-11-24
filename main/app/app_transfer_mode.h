#pragma once
#include <app.h>

// The file manager uses this before opening Transfer to enter USB storage directly.
void app_transfer_request_usb_start(void);

// File manager opens the selected transfer mode directly, without the method picker.
void app_transfer_request_wifi_upload(void);
void app_transfer_request_hotspot_start(void);

// Settings uses this before opening Transfer to enter WiFi provisioning directly.
void app_transfer_request_wifi_setup(void);

// 联调构建从文件管理直达 CloudZao；不自动接收。
// Test builds open CloudZao directly from Files without starting a receive job.
void app_transfer_request_cloudzao(void);

/// 配网结束后返回发起请求的引导步骤。/ Return to the requesting setup step after provisioning.
void app_transfer_open_setup_wifi(app_ctx_t *ctx);
/// 只消费本次引导配网确认的成功事件，取消不算成功。/ Consume only a confirmed setup connection, not a cancelled return.
bool app_transfer_take_setup_wifi_success(void);
