#!/usr/bin/env python3
"""Exercise actual modem startup, IP readiness and settings status transitions.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import sys
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
MODEM = ROOT / "main/services/cellular/uart_eth_modem.cc"
SETTINGS = ROOT / "main/apps/app_device_settings.c"

MODEL = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "network_service.h"
#include "cellular_service.h"
using esp_event_base_t = int;
constexpr int IP_EVENT = 1, IP_EVENT_ETH_GOT_IP = 2;
struct ip_event_got_ip_t {
    void *esp_netif;
    struct { struct { unsigned addr; } ip; } ip_info;
};
constexpr unsigned kEventStart = 1, kEventStop = 2, kEventInitTaskDone = 4;
constexpr bool pdTRUE = true;
constexpr unsigned portMAX_DELAY = ~0U;
static unsigned completion_bits;
static unsigned xEventGroupWaitBits(int, unsigned, bool, bool, unsigned) { return kEventStart; }
static void xEventGroupSetBits(int, unsigned bits) { completion_bits |= bits; }
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
class UartEthModem {
public:
    enum class UartEthModemEvent {
        Connecting, Connected, Disconnected, ErrorNoSim,
        ErrorRegistrationDenied, ErrorInitFailed, ErrorSimNotReady
    };
    struct { int stat = 0, act = 0; std::string tac, ci; } cell_info_;
    std::atomic<bool> initialized_{false}, initializing_{true};
    std::atomic<bool> handshake_done_{false}, stop_flag_{false};
    UartEthModemEvent last_event = UartEthModemEvent::Disconnected;
    int event_group_ = 1, ethernet_starts = 0, startup_result = ESP_OK, ethernet_result = ESP_OK;
    int netif_marker = 0;
    void *eth_netif_ = &netif_marker;
    esp_err_t RunNormalModeInitSequence() { return startup_result; }
    esp_err_t InitIotEth() { ++ethernet_starts; return ethernet_result; }
    void SetNetworkEvent(UartEthModemEvent event, const std::string& = "") { last_event = event; }
    void InitTaskRun();
    void ParseAtResponse(const std::string& response);
    static void IpEventHandler(void*, esp_event_base_t, int32_t, void*);
};
enum { SETTINGS_MAIN, SETTINGS_NETWORK, SETTINGS_OTHER };
static int s_page;
static network_status_t live_network, s_network_snapshot;
extern "C" void network_service_get_status(network_status_t *out) { *out = live_network; }
'''

CASES = r'''
int main() {
    using Event = UartEthModem::UartEthModemEvent;
    // SIM failures keep AT access without starting DHCP or overwriting errors.
    for (Event event : {Event::ErrorNoSim, Event::ErrorRegistrationDenied}) {
        UartEthModem modem;
        modem.last_event = event;
        modem.initialized_ = true;
        completion_bits = 0;
        modem.InitTaskRun();
        assert(modem.ethernet_starts == 0 && modem.last_event == event);
        assert(!modem.initializing_ && !modem.stop_flag_);
        assert(completion_bits & kEventInitTaskDone);
    }
    UartEthModem modem;
    modem.handshake_done_ = true;
    modem.InitTaskRun();
    assert(modem.ethernet_starts == 1 && !modem.initializing_);
    UartEthModem failed;
    failed.handshake_done_ = true;
    failed.ethernet_result = ESP_ERR_NO_MEM;
    failed.InitTaskRun();
    assert(failed.stop_flag_ && failed.last_event == Event::ErrorInitFailed);

    // Registered/roaming URCs do not mean an IP address has been obtained.
    modem.initialized_ = true;
    for (const char *reply : {"+CEREG: 2,1", "+CEREG: 5,\"1234\",\"abcd\",7"}) {
        modem.last_event = Event::Connecting;
        modem.ParseAtResponse(reply);
        assert(modem.last_event == Event::Connecting);
    }
    modem.ParseAtResponse("+CEREG: 2,3");
    assert(modem.last_event == Event::ErrorRegistrationDenied);
    modem.ParseAtResponse("+CEREG: 2,2");
    assert(modem.last_event == Event::Connecting);
    UartEthModem terminal;
    terminal.initialized_ = true;
    terminal.initializing_ = false;
    terminal.last_event = Event::ErrorRegistrationDenied;
    terminal.ParseAtResponse("+CEREG: 2");
    assert(terminal.last_event == Event::ErrorRegistrationDenied);
    ip_event_got_ip_t ip = {modem.eth_netif_, {{0x01020304}}};
    UartEthModem::IpEventHandler(&modem, IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip);
    assert(modem.last_event == Event::Connected);
    modem.last_event = Event::Connecting;
    modem.stop_flag_ = true;
    UartEthModem::IpEventHandler(&modem, IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip);
    assert(modem.last_event == Event::Connecting);
    modem.stop_flag_ = false;
    ip.esp_netif = nullptr;
    UartEthModem::IpEventHandler(&modem, IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip);
    assert(modem.last_event == Event::Connecting);
    ip.esp_netif = modem.eth_netif_; ip.ip_info.ip.addr = 0;
    UartEthModem::IpEventHandler(&modem, IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip);
    assert(modem.last_event == Event::Connecting);

    network_status_t status = {};
    status.cellular_state = CELLULAR_SERVICE_NO_REGISTRATION;
    assert(!strcmp(settings_cellular_status(&status), "已关闭"));
    status.cellular_enabled = true;
    assert(!strcmp(settings_cellular_status(&status), "网络注册被拒"));
    status.cellular_state = CELLULAR_SERVICE_NO_SIM;
    assert(!strcmp(settings_cellular_status(&status), "未检测到 SIM 卡"));
    status.cellular_state = CELLULAR_SERVICE_ERROR;
    assert(!strcmp(settings_cellular_status(&status), "连接失败"));
    status.cellular_state = CELLULAR_SERVICE_SIM_NOT_READY;
    assert(!strcmp(settings_cellular_status(&status), "SIM 卡未就绪"));
    strcpy(status.cellular_detail, "SIM 需要 PIN 解锁");
    assert(!strcmp(settings_cellular_status(&status), "SIM 需要 PIN 解锁"));
    status.cellular_state = CELLULAR_SERVICE_STARTING;
    assert(!strcmp(settings_cellular_status(&status), "正在连接"));
    status.cellular_ready = true;
    assert(!strcmp(settings_cellular_status(&status), "已连接"));

    // One redraw per real change; other settings pages stay untouched.
    s_page = SETTINGS_NETWORK;
    assert(!settings_network_poll());
    live_network.cellular_state = CELLULAR_SERVICE_STARTING;
    assert(settings_network_poll() && !settings_network_poll());
    live_network.cellular_state = CELLULAR_SERVICE_NO_REGISTRATION;
    assert(settings_network_poll() && !settings_network_poll());
    strcpy(live_network.cellular_detail, "SIM 查询失败");
    assert(settings_network_poll() && !settings_network_poll());
    s_page = SETTINGS_OTHER;
    live_network.cellular_capable = true;
    assert(!settings_network_poll());
    s_page = SETTINGS_MAIN;
    assert(settings_network_poll() && !settings_network_poll());
    puts("PASS: cellular registration failures, IP readiness and live settings state");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="cellular-state-") as folder:
        work = Path(folder)
        env, compiler = toolchain(work)
        (work / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM -2\n")
        source = MODEL
        for name in ("InitTaskRun", "ParseAtResponse", "IpEventHandler"):
            source += function("UartEthModem::" + name, MODEM) + "\n"
        for name in ("settings_cellular_status", "settings_network_poll"):
            source += function(name, SETTINGS) + "\n"
        unit, binary = work / "test.cc", work / "test"
        unit.write_text(source + CASES)
        libraries = ["-lc++"] if sys.platform == "darwin" else ["-lstdc++"]
        subprocess.run(compiler + ["-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                  "-fsanitize=address,undefined", "-I" + str(work),
                                  "-Imain/services", "-Imain/services/cellular", str(unit),
                                  "-o", str(binary)] + libraries, cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    main()
