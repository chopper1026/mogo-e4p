#!/usr/bin/env python3
"""Execute the real modem GPIO initialization against a shared IRQ model.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/services/cellular/uart_eth_modem.cc"

MODEL = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_STATE = -1, ESP_ERR_NO_MEM = -2;
constexpr int GPIO_MODE_OUTPUT = 1, GPIO_MODE_INPUT = 2;
constexpr int GPIO_PULLUP_DISABLE = 0, GPIO_PULLDOWN_DISABLE = 0;
constexpr int GPIO_INTR_DISABLE = 0, GPIO_INTR_LOW_LEVEL = 1;
struct gpio_config_t { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; };
static bool service_installed, handler_installed, interrupt_enabled;
static int install_error, handler_error, install_calls, handler_calls;
static esp_err_t gpio_config(const gpio_config_t*) { return ESP_OK; }
static void gpio_set_level(int pin, int value) { assert(pin == 60 && value == 1); }
static void gpio_sleep_sel_dis(int pin) { assert(pin == 60); }
static esp_err_t gpio_wakeup_enable(int pin, int mode) {
    assert(pin == 3 && mode == GPIO_INTR_LOW_LEVEL); return ESP_OK;
}
static esp_err_t gpio_install_isr_service(int flags) {
    assert(flags == 0); ++install_calls;
    if (install_error) return install_error;
    if (service_installed) return ESP_ERR_INVALID_STATE;
    service_installed = true; return ESP_OK;
}
static esp_err_t gpio_isr_handler_add(int pin, void (*callback)(void*), void* context) {
    assert(pin == 3 && callback && context); ++handler_calls;
    if (!service_installed) return ESP_ERR_INVALID_STATE;
    if (handler_error) return handler_error;
    handler_installed = true; return ESP_OK;
}
#define ESP_RETURN_ON_ERROR(expression, ...) do { \
    const int error = (expression); if (error != ESP_OK) return error; \
} while (0)
class UartEthModem {
public:
    esp_err_t InitGpio();
    int srdy_pin_isr_ = -1;
private:
    struct { int mrdy_pin = 60, srdy_pin = 3; } config_;
    static constexpr bool kSrdyInterruptForWakeup = true;
    static void SrdyIsrHandler(void*) {}
    void ConfigureSrdyInterrupt(bool wakeup) {
        assert(wakeup && handler_installed); interrupt_enabled = true;
    }
};
'''

CASES = r'''
static void reset(bool installed = false) {
    service_installed = installed;
    handler_installed = interrupt_enabled = false;
    install_error = handler_error = install_calls = handler_calls = 0;
}
int main() {
    UartEthModem modem;
    // Cold boot: no other driver has installed the shared service yet.
    reset();
    assert(modem.InitGpio() == ESP_OK);
    assert(service_installed && handler_installed && interrupt_enabled);
    assert(install_calls == 1 && handler_calls == 1 && modem.srdy_pin_isr_ == 3);
    // The accelerometer may have installed it before the modem starts.
    reset(true);
    assert(modem.InitGpio() == ESP_OK);
    assert(service_installed && handler_installed && interrupt_enabled);
    // Infrastructure errors must stop startup before enabling the pin IRQ.
    reset(); install_error = ESP_ERR_NO_MEM;
    assert(modem.InitGpio() == ESP_ERR_NO_MEM);
    assert(handler_calls == 0 && !interrupt_enabled);
    reset(); handler_error = ESP_ERR_NO_MEM;
    assert(modem.InitGpio() == ESP_ERR_NO_MEM);
    assert(service_installed && !handler_installed && !interrupt_enabled);
    puts("PASS: cellular GPIO cold boot, shared ISR reuse and allocation failures");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="cellular-startup-") as folder:
        work = Path(folder)
        env, compiler = toolchain(work)
        source, binary = work / "test.cc", work / "test"
        source.write_text(MODEL + function("UartEthModem::InitGpio", SOURCE) + CASES)
        subprocess.run(compiler + ["-x", "c++", "-std=c++17", "-Wall", "-Wextra",
                                  "-Werror", "-fsanitize=address,undefined",
                                  str(source), "-o", str(binary)], env=env, check=True)
        subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    main()
