/* SPDX-License-Identifier: Apache-2.0
 * Runtime NT26 capability probe and cellular bearer adapter.
 *
 * The Wi-Fi and 4G boards use one firmware image. The fixed UART pins are
 * only a transport contract; capability is granted after the modem answers
 * AT and remains separate from SIM and registration state.
 */

#include "cellular_service.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "board_hardware.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "settings.h"
#include "uart_eth_modem.h"

namespace {

constexpr char kTag[] = "cellular_service";
constexpr int64_t kProbeTimeoutUs = 15000000;
constexpr int64_t kSignalPollIntervalUs = 10000000;
constexpr gpio_num_t kTx = (gpio_num_t)BOARD_CELLULAR_UART_TX_GPIO;
constexpr gpio_num_t kRx = (gpio_num_t)BOARD_CELLULAR_UART_RX_GPIO;
constexpr gpio_num_t kMrdy = (gpio_num_t)BOARD_CELLULAR_MRDY_GPIO;
constexpr gpio_num_t kSrdy = (gpio_num_t)BOARD_CELLULAR_SRDY_GPIO;

struct Service {
    std::mutex mutex;
    std::recursive_mutex lifecycle;
    std::shared_ptr<UartEthModem> modem;
    cellular_service_status_t status = {};
    bool user_enabled = false;
    bool probe_active = false;
    bool stop_after_probe = false;
    bool paused = false;
    int64_t probe_deadline_us = 0;
    int64_t next_signal_poll_us = 0;
};

Service s_service;

void set_detail(cellular_service_status_t *status, const std::string &detail) {
    if (!status) return;
    std::snprintf(status->detail, sizeof(status->detail), "%s", detail.c_str());
}

void on_modem_event(UartEthModem::UartEthModemEvent event, const std::string &detail) {
    std::lock_guard<std::mutex> lock(s_service.mutex);
    const bool module_replied = event != UartEthModem::UartEthModemEvent::ErrorInitFailed ||
        detail.find("Modem not detected") == std::string::npos;
    switch (event) {
        case UartEthModem::UartEthModemEvent::Connecting:
            s_service.status.capability_known = true;
            s_service.status.capable = true;
            s_service.status.state = CELLULAR_SERVICE_STARTING;
            s_service.status.ready = false;
            s_service.probe_active = false;
            s_service.stop_after_probe = !s_service.user_enabled;
            break;
        case UartEthModem::UartEthModemEvent::Connected:
            s_service.status.capability_known = true;
            s_service.status.capable = true;
            s_service.status.state = CELLULAR_SERVICE_READY;
            s_service.status.ready = true;
            s_service.probe_active = false;
            s_service.stop_after_probe = !s_service.user_enabled;
            break;
        case UartEthModem::UartEthModemEvent::ErrorNoSim:
        case UartEthModem::UartEthModemEvent::ErrorSimNotReady:
            s_service.status.capability_known = true;
            s_service.status.capable = true;
            s_service.status.state = event == UartEthModem::UartEthModemEvent::ErrorNoSim
                ? CELLULAR_SERVICE_NO_SIM : CELLULAR_SERVICE_SIM_NOT_READY;
            s_service.status.ready = false;
            s_service.probe_active = false;
            s_service.stop_after_probe = !s_service.user_enabled;
            break;
        case UartEthModem::UartEthModemEvent::ErrorRegistrationDenied:
            s_service.status.capability_known = true;
            s_service.status.capable = true;
            s_service.status.state = CELLULAR_SERVICE_NO_REGISTRATION;
            s_service.status.ready = false;
            s_service.probe_active = false;
            s_service.stop_after_probe = !s_service.user_enabled;
            break;
        case UartEthModem::UartEthModemEvent::ErrorInitFailed:
            s_service.status.capability_known = true;
            s_service.status.capable = module_replied;
            s_service.status.state = module_replied ? CELLULAR_SERVICE_ERROR : CELLULAR_SERVICE_STOPPED;
            s_service.status.ready = false;
            s_service.probe_active = false;
            s_service.stop_after_probe = true;
            break;
        case UartEthModem::UartEthModemEvent::Disconnected:
        default:
            s_service.status.state = s_service.status.enabled
                ? CELLULAR_SERVICE_STARTING : CELLULAR_SERVICE_STOPPED;
            s_service.status.ready = false;
            break;
    }
    set_detail(&s_service.status, detail);
}

esp_err_t ensure_netif_runtime(void) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    return ESP_OK;
}

std::shared_ptr<UartEthModem> make_modem() {
    UartEthModem::Config config = {
        .uart_num = UART_NUM_2,
        .baud_rate = 2000000,
        .tx_pin = kTx,
        .rx_pin = kRx,
        .mrdy_pin = kMrdy,
        .srdy_pin = kSrdy,
    };
    auto modem = std::make_shared<UartEthModem>(config);
    modem->SetNetworkEventCallback(on_modem_event);
    return modem;
}

esp_err_t start_modem(bool enabled, bool probing) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(s_service.lifecycle);
    {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        if (s_service.modem) return ESP_OK;
    }
    esp_err_t err = ensure_netif_runtime();
    if (err != ESP_OK) return err;
    if (board_ioe_wake() != ESP_OK) return ESP_ERR_INVALID_STATE;

    auto modem = make_modem();
    {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        // A concurrent wake/lease may have won the start race after the first
        // check; let the temporary object clean itself up before returning.
        if (s_service.modem) return ESP_OK;
        s_service.modem = modem;
        s_service.status.enabled = enabled;
        s_service.status.ready = false;
        s_service.status.state = probing ? CELLULAR_SERVICE_PROBING : CELLULAR_SERVICE_STARTING;
        s_service.status.signal_strength = 99;
        s_service.status.detail[0] = 0;
        s_service.probe_active = probing;
        s_service.stop_after_probe = false;
        s_service.probe_deadline_us = esp_timer_get_time() + kProbeTimeoutUs;
    }

    err = modem->Start();
    if (err == ESP_OK) return ESP_OK;
    (void)modem->Stop();
    std::lock_guard<std::mutex> lock(s_service.mutex);
    if (s_service.modem == modem) s_service.modem.reset();
    s_service.status.capability_known = true;
    s_service.status.capable = false;
    s_service.status.enabled = false;
    s_service.status.ready = false;
    s_service.status.state = CELLULAR_SERVICE_ERROR;
    s_service.probe_active = false;
    s_service.stop_after_probe = false;
    set_detail(&s_service.status, "移动网络驱动启动失败");
    return err;
}

bool stop_modem(bool preserve_status) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(s_service.lifecycle);
    std::shared_ptr<UartEthModem> modem;
    {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        modem = s_service.modem;
        s_service.modem.reset();
        // `enabled` is the user's bearer preference, while `modem` is the
        // live transport. Sleep/probe handoff can stop the latter without
        // making a saved-on radio look permanently disabled.
        s_service.status.enabled = preserve_status ? s_service.user_enabled : false;
        s_service.status.ready = false;
        if (!preserve_status) s_service.status.state = CELLULAR_SERVICE_STOPPED;
        s_service.status.signal_strength = 99;
        s_service.probe_active = false;
        s_service.stop_after_probe = false;
        s_service.probe_deadline_us = 0;
        s_service.next_signal_poll_us = 0;
    }
    const bool stopped = !modem || modem->Stop() == ESP_OK;
    if (!stopped) {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        if (!s_service.modem) s_service.modem = modem;
        s_service.status.enabled = preserve_status ? s_service.user_enabled : false;
        s_service.status.ready = false;
    }
    return stopped;
}

}  // namespace

extern "C" void cellular_service_init(void) {
    const bool enabled = app_settings_cellular_enabled();
    {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        s_service.status = (cellular_service_status_t){
            .capability_known = false,
            .capable = false,
            .enabled = enabled,
            .ready = false,
            .state = CELLULAR_SERVICE_PROBING,
            .signal_strength = 99,
            .detail = "正在检测移动网络",
        };
        s_service.user_enabled = enabled;
        s_service.paused = false;
    }
    // Probe both variants once. A Wi-Fi board fails closed and the UI keeps
    // the mobile entry hidden; a 4G board with no SIM still proves capability.
    if (start_modem(enabled, true) != ESP_OK)
        ESP_LOGW(kTag, "runtime cellular probe could not start");
}

extern "C" esp_err_t cellular_service_set_enabled(bool enabled) {
    cellular_service_status_t status;
    std::lock_guard<std::recursive_mutex> lifecycle_lock(s_service.lifecycle);
    cellular_service_get_status(&status);
    if (enabled) {
        if (!status.capability_known) return ESP_ERR_INVALID_STATE;
        if (!status.capable) return ESP_ERR_NOT_SUPPORTED;
        bool already_running = false;
        {
            std::lock_guard<std::mutex> lock(s_service.mutex);
            s_service.user_enabled = true;
            s_service.stop_after_probe = false;
            if (s_service.modem) {
                s_service.status.enabled = true;
                if (s_service.status.state == CELLULAR_SERVICE_STOPPED)
                    s_service.status.state = CELLULAR_SERVICE_STARTING;
                already_running = true;
            }
        }
        if (already_running) {
            app_settings_set_cellular_enabled(true);
            return ESP_OK;
        }
        esp_err_t err = start_modem(true, false);
        if (err == ESP_OK) app_settings_set_cellular_enabled(true);
        return err;
    }
    {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        s_service.user_enabled = false;
    }
    const bool stopped = stop_modem(false);
    if (stopped) app_settings_set_cellular_enabled(false);
    return stopped ? ESP_OK : ESP_FAIL;
}

extern "C" bool cellular_service_pause_for_sleep(void) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(s_service.lifecycle);
    bool has_modem;
    {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        if (s_service.paused) return true;
        s_service.paused = true;
        has_modem = s_service.modem != nullptr;
    }
    const bool stopped = !has_modem || stop_modem(true);
    if (!stopped) {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        s_service.paused = false;
    }
    return stopped;
}

extern "C" void cellular_service_resume_after_sleep(void) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(s_service.lifecycle);
    bool paused = false;
    bool enabled = false;
    bool probing = false;
    {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        paused = s_service.paused;
        s_service.paused = false;
        enabled = s_service.user_enabled;
        probing = !s_service.status.capability_known;
    }
    if (paused && (enabled || probing)) (void)start_modem(enabled, probing);
}

extern "C" void cellular_service_poll(void) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(s_service.lifecycle);
    std::shared_ptr<UartEthModem> modem;
    bool stop_pending = false;
    bool probe_timed_out = false;
    bool sample_signal = false;
    {
        std::lock_guard<std::mutex> lock(s_service.mutex);
        modem = s_service.modem;
        stop_pending = s_service.stop_after_probe;
        probe_timed_out = s_service.probe_active && esp_timer_get_time() >= s_service.probe_deadline_us;
        const int64_t now = esp_timer_get_time();
        sample_signal = modem && modem->IsInitialized() && now >= s_service.next_signal_poll_us;
        if (sample_signal) s_service.next_signal_poll_us = now + kSignalPollIntervalUs;
    }
    if (sample_signal) {
        const int signal = modem->GetSignalStrength();
        std::lock_guard<std::mutex> lock(s_service.mutex);
        s_service.status.signal_strength = signal;
    }
    if (probe_timed_out) {
        ESP_LOGW(kTag, "runtime cellular probe timed out");
        (void)stop_modem(false);
        std::lock_guard<std::mutex> lock(s_service.mutex);
        s_service.status.capability_known = true;
        s_service.status.capable = false;
        s_service.status.state = CELLULAR_SERVICE_STOPPED;
        set_detail(&s_service.status, "未检测到移动网络模块");
    } else if (stop_pending) {
        (void)stop_modem(true);
    }
}

extern "C" void cellular_service_get_status(cellular_service_status_t *out) {
    if (!out) return;
    std::lock_guard<std::mutex> lock(s_service.mutex);
    *out = s_service.status;
}

extern "C" bool cellular_service_capable(void) {
    std::lock_guard<std::mutex> lock(s_service.mutex);
    return s_service.status.capable;
}

extern "C" bool cellular_service_enabled(void) {
    std::lock_guard<std::mutex> lock(s_service.mutex);
    return s_service.status.enabled;
}

extern "C" bool cellular_service_ready(void) {
    std::lock_guard<std::mutex> lock(s_service.mutex);
    return s_service.status.ready;
}

extern "C" void *cellular_service_netif(void) {
    std::lock_guard<std::mutex> lock(s_service.mutex);
    return s_service.modem ? reinterpret_cast<void *>(s_service.modem->GetNetif()) : nullptr;
}
