#!/usr/bin/env python3
"""Compile the real cellular service against a controllable modem/task model.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/services/cellular/cellular_service.cc"

MODEL = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include "cellular_service.h"
using gpio_num_t = int;
constexpr int BOARD_CELLULAR_UART_TX_GPIO = 58, BOARD_CELLULAR_UART_RX_GPIO = 59;
constexpr int BOARD_CELLULAR_MRDY_GPIO = 60, BOARD_CELLULAR_SRDY_GPIO = 3, UART_NUM_2 = 2;
static bool setting_enabled;
static int64_t now_us;
static int next_start_result, next_stop_result;
static unsigned creations, starts, stops;
static int64_t esp_timer_get_time() { return now_us; }
static int esp_netif_init() { return ESP_OK; }
static int esp_event_loop_create_default() { return ESP_OK; }
static int board_ioe_wake() { return ESP_OK; }
static bool app_settings_cellular_enabled() { return setting_enabled; }
static void app_settings_set_cellular_enabled(bool enabled) { setting_enabled = enabled; }
static bool app_settings_cellular_capability_known() { return false; }
static bool app_settings_cellular_capable() { return false; }
static void app_settings_set_cellular_capability(bool capable) { (void)capable; }
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
class UartEthModem {
public:
    enum class UartEthModemEvent {
        Connecting, Connected, Disconnected, ErrorNoSim, ErrorRegistrationDenied, ErrorInitFailed,
        ErrorSimNotReady
    };
    struct Config {
        int uart_num, baud_rate, tx_pin, rx_pin, mrdy_pin, srdy_pin;
    };
    bool initialized = false;
    std::function<void(UartEthModemEvent, const std::string&)> callback;
    explicit UartEthModem(const Config& cfg) {
        assert(cfg.uart_num == 2 && cfg.baud_rate == 2000000); ++creations;
    }
    void SetNetworkEventCallback(decltype(callback) cb) { callback = cb; }
    int Start() { ++starts; return next_start_result; }
    int Stop() { ++stops; initialized = false; return next_stop_result; }
    bool IsInitialized() const { return initialized; }
    int GetSignalStrength() { return 18; }
    void *GetNetif() { return nullptr; }
};
'''

CASES = r'''
int main() {
    using Event = UartEthModem::UartEthModemEvent;
    cellular_service_status_t status;
    cellular_service_init();
    assert(creations == 1 && starts == 1);
    assert(cellular_service_set_enabled(true) == ESP_ERR_INVALID_STATE);
    s_service.modem->callback(Event::ErrorNoSim, "");
    cellular_service_get_status(&status);
    assert(status.capability_known && status.capable && !status.enabled && !status.ready);
    cellular_service_poll();
    assert(!s_service.modem && stops == 1 && !setting_enabled);
    assert(cellular_service_set_enabled(true) == ESP_OK);
    assert(creations == 2 && starts == 2 && setting_enabled);
    assert(cellular_service_set_enabled(true) == ESP_OK && starts == 2);
    s_service.modem->initialized = true;
    s_service.modem->callback(Event::ErrorSimNotReady, "SIM 需要 PIN 解锁");
    cellular_service_poll();
    cellular_service_get_status(&status);
    assert(status.state == CELLULAR_SERVICE_SIM_NOT_READY && !status.ready);
    assert(!strcmp(status.detail, "SIM 需要 PIN 解锁") && status.signal_strength == 18);
    s_service.modem->callback(Event::ErrorRegistrationDenied, "");
    cellular_service_get_status(&status);
    assert(status.state == CELLULAR_SERVICE_NO_REGISTRATION && !status.ready);
    s_service.modem->callback(Event::Connected, "");
    assert(cellular_service_ready() && cellular_service_enabled());
    next_stop_result = ESP_FAIL;
    assert(!cellular_service_pause_for_sleep() && s_service.modem && !s_service.paused);
    next_stop_result = ESP_OK;
    assert(cellular_service_pause_for_sleep() && !s_service.modem && setting_enabled);
    cellular_service_get_status(&status);
    assert(status.enabled && !status.ready);
    assert(cellular_service_pause_for_sleep());
    cellular_service_resume_after_sleep();
    assert(creations == 3 && starts == 3 && s_service.modem && setting_enabled);
    cellular_service_resume_after_sleep();
    assert(starts == 3);
    assert(cellular_service_set_enabled(false) == ESP_OK && !setting_enabled);
    assert(cellular_service_pause_for_sleep());
    cellular_service_resume_after_sleep();
    assert(starts == 3 && !s_service.modem);

    next_start_result = ESP_ERR_NO_MEM;
    assert(cellular_service_set_enabled(true) == ESP_ERR_NO_MEM);
    cellular_service_get_status(&status);
    assert(status.state == CELLULAR_SERVICE_ERROR && !status.ready && !s_service.modem);
    next_start_result = ESP_OK;
    assert(cellular_service_set_enabled(false) == ESP_OK);
    cellular_service_init();
    now_us += 15000000;
    cellular_service_poll();
    cellular_service_get_status(&status);
    assert(status.capability_known && !status.capable && !s_service.modem);
    assert(cellular_service_set_enabled(true) == ESP_ERR_NOT_SUPPORTED);
    // AT detection failure does not expose mobile controls on a Wi-Fi-only board.
    cellular_service_init();
    s_service.modem->callback(Event::ErrorInitFailed, "Modem not detected (AT no response)");
    cellular_service_poll();
    assert(!cellular_service_capable() && !s_service.modem);
    puts("PASS: cellular capability, radio lifecycle, error states and sleep restoration");
}

'''


def main():
    with tempfile.TemporaryDirectory(prefix="cellular-service-") as folder:
        work = Path(folder)
        env, compiler = toolchain(work)
        (work / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_ARG -2\n#define ESP_ERR_INVALID_STATE -3\n"
            "#define ESP_ERR_NO_MEM -4\n#define ESP_ERR_NOT_SUPPORTED -5\n")
        # Only platform includes are substituted; the entire service body is compiled.
        service = re.sub(r'^#include[^\n]*\n', '', SOURCE.read_text(), flags=re.MULTILINE)
        unit, binary = work / "test.cc", work / "test"
        unit.write_text(MODEL + service + CASES)
        libraries = ["-lc++"] if sys.platform == "darwin" else ["-lstdc++"]
        subprocess.run(compiler + ["-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                  "-fsanitize=address,undefined", "-I" + str(work),
                                  "-Imain/services/cellular", str(unit), "-o", str(binary)]
                       + libraries, cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    main()
