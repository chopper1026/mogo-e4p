#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 仅硬复位后、首次采样前调用；每次开机最多运行一轮，不从唤醒调用。
/// Run after reset before the first sampler; at most one run per boot, never on wake.
esp_err_t ft6336u_upgrade_on_boot(i2c_master_dev_handle_t device);

#ifdef __cplusplus
}
#endif
