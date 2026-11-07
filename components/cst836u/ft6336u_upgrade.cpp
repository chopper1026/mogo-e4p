/*
 * Metalio FT6336U 开机升级传输适配；原厂874538e，MIT声明见../board_i2c/LICENSE.Metalio。
 * Boot transport adapted from Metalio 874538e; MIT notice: ../board_i2c/LICENSE.Metalio.
 * Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
 * Copyright (c) 2025 Project Contributors
 * 冻结：仅初始化调用、每次开机一轮、整段独占总线；业务与采样线程不调用。
 * Frozen: init only, one run per boot, exclusive bus throughout; never from UI or sampler tasks.
 */
#include "ft6336u_upgrade.h"
#include "ft6336u_upgrade_protocol.h"
#include "board_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const uint8_t firmware_start[] asm("_binary_ft6336u_v04_app_bin_start");
extern const uint8_t firmware_end[] asm("_binary_ft6336u_v04_app_bin_end");

namespace {
constexpr const char* kTag = "touch_upgrade";
struct Transport {
    i2c_master_dev_handle_t device;
    bool Write(const uint8_t* bytes, size_t size) {
        return i2c_master_transmit(device, bytes, size, 500) == ESP_OK;
    }
    bool Read(const uint8_t* command, size_t command_size, uint8_t* output, size_t size) {
        return i2c_master_transmit_receive(device, command, command_size, output, size, 500) == ESP_OK;
    }
    void Delay(int ms) { vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(ms))); }
};
}

esp_err_t ft6336u_upgrade_on_boot(i2c_master_dev_handle_t device) {
    if (!device) return ESP_ERR_INVALID_ARG;
    static bool attempted = false;
    static esp_err_t previous = ESP_FAIL;
    if (attempted) return previous;
    attempted = true;
    previous = board_i2c_lock(1000);
    if (previous != ESP_OK) {
        ESP_LOGE(kTag, "I2C lock failed; controller not written: %s", esp_err_to_name(previous));
        return previous;
    }
    Transport io{device};
    ESP_LOGI(kTag, "boot check LT0470GF0430M_V1.1 target V04; newer versions retained");
    int attempts = 0;
    const auto result = Ft6336uUpgrade::Run(io, firmware_start, firmware_end - firmware_start, &attempts);
    board_i2c_unlock();
    using Ft6336uUpgrade::Result;
    previous = result == Result::Updated || result == Result::Current || result == Result::Newer
        ? ESP_OK : ESP_FAIL;
    if (previous == ESP_OK) {
        const char* status = result == Result::Updated ? "updated and verified V04" :
                             result == Result::Current ? "already V04; skipped" : "newer firmware; retained";
        ESP_LOGI(kTag, "%s; attempts=%d", status, attempts);
    } else {
        ESP_LOGE(kTag, "V04 upgrade failed result=%d attempts=%d; touch init stopped",
                 static_cast<int>(result), attempts);
    }
    return previous;
}
