#!/usr/bin/env python3
"""Run production SIM readiness/startup with delayed replies and cancellation.
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

MODEL = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_TIMEOUT = -2, ESP_ERR_INVALID_STATE = -3;
constexpr int pdFALSE = 0, portTICK_PERIOD_MS = 1;
constexpr unsigned kEventStop = 1, kEventHandshakeDone = 2, kHandshakeTimeoutMs = 100;
constexpr int kModemSleepTimeoutS = 30;
static constexpr char kTag[] = "test";
static constexpr uint8_t kHandshakeRequest[] = {1};
static uint32_t now_ms, cancel_at;
static std::atomic<bool> *active_stop;
static uint32_t xTaskGetTickCount() { return now_ms; }
static void vTaskDelay(unsigned ms) { now_ms += ms; }
static unsigned xEventGroupWaitBits(int, unsigned bits, int, int, unsigned ms) {
    now_ms += ms;
    if (cancel_at && now_ms >= cancel_at) *active_stop = true;
    return active_stop->load() ? bits & kEventStop : 0;
}
static void xEventGroupSetBits(int, unsigned) {}
static int uart_set_baudrate(int, int) { return 0; }
#define pdMS_TO_TICKS(ms) (ms)
template<typename... Args> static void log_stub(Args&&...) {}
#define ESP_LOGI(...) log_stub(__VA_ARGS__)
#define ESP_LOGW(...) log_stub(__VA_ARGS__)
#define ESP_LOGE(...) log_stub(__VA_ARGS__)
struct Reply { std::string text; int result = ESP_OK; unsigned latency = 0; };
class UartEthModem {
public:
    enum class SimCardState { Ready, Absent, PinRequired, PukRequired, Locked,
                              Busy, Failure, QueryFailed, Cancelled };
    enum class UartEthModemEvent { Connecting, Connected, Disconnected, ErrorNoSim,
        ErrorRegistrationDenied, ErrorInitFailed, ErrorSimNotReady };
    enum class FrameType { kEthernet };
    struct { int baud_rate = 2000000, uart_num = 2; } config_;
    int detect_baud_rate_ = 2000000, event_group_ = 1;
    struct { int stat = 3; } cell_info_;
    std::atomic<bool> stop_flag_{false}, initialized_{false}, handshake_done_{false};
    std::deque<Reply> replies;
    Reply repeated = {"+CME ERROR: 10\r\n", ESP_FAIL};
    bool cancel_query = false, cancel_registration = false;
    unsigned queries = 0, info_queries = 0, registration_waits = 0, events = 0;
    UartEthModemEvent last_event = UartEthModemEvent::Disconnected;
    std::string detail;
    static SimCardState ParseSimCardState(const std::string&, esp_err_t);
    SimCardState CheckSimCard();
    esp_err_t RunNormalModeInitSequence();
    esp_err_t AtDetect() { return ESP_OK; }
    esp_err_t ConfigureInternalSim() { return ESP_OK; }
    esp_err_t SendAt(const std::string& cmd, std::string& response, unsigned timeout = 1000) {
        response = "OK\r\n";
        if (cmd == "AT+ECNETCFG?") response = "+ECNETCFG: \"nat\",1\r\nOK\r\n";
        if (cmd != "AT+CPIN?") return ESP_OK;
        ++queries;
        Reply reply = repeated;
        if (!replies.empty()) { reply = replies.front(); replies.pop_front(); }
        assert(reply.latency <= timeout);
        now_ms += reply.latency;
        response = reply.text;
        if (cancel_query) stop_flag_ = true;
        return reply.result;
    }
    esp_err_t SendAtWithRetry(const std::string&, std::string&, unsigned, int) { return ESP_OK; }
    void QueryModemInfo() { ++info_queries; }
    bool WaitForRegistration(unsigned) {
        ++registration_waits;
        if (cancel_registration) stop_flag_ = true;
        return false;
    }
    esp_err_t SendFrame(const uint8_t*, size_t, FrameType) { assert(false); return ESP_FAIL; }
    void SetNetworkEvent(UartEthModemEvent event, const std::string& why = "") {
        ++events; last_event = event; detail = why;
    }
};
using SimCardState = UartEthModem::SimCardState;
static void reset(UartEthModem& modem) {
    now_ms = cancel_at = 0; active_stop = &modem.stop_flag_;
}
'''

CASES = r'''
int main() {
    using State = SimCardState;
    using Event = UartEthModem::UartEthModemEvent;
    const auto parse = [&](const char* text, int result = ESP_OK) {
        return UartEthModem::ParseSimCardState(text, result);
    };
    assert(parse("\r\n+CPIN: READY\r\nOK\r\n") == State::Ready);
    assert(parse("+CPIN: READY-unknown\r\n") == State::QueryFailed);
    assert(parse("+CPIN: READY\r\n", ESP_ERR_TIMEOUT) == State::QueryFailed);
    assert(parse("+CPIN: SIM PIN\r\nOK\r\n") == State::PinRequired);
    assert(parse("+CPIN: SIM PUK\r\nOK\r\n") == State::PukRequired);
    assert(parse("+CPIN: PH-NET PIN\r\n") == State::Locked);
    assert(parse("+CME ERROR: 10\r\n", ESP_FAIL) == State::Absent);
    assert(parse("+CME ERROR: 100\r\n", ESP_FAIL) == State::QueryFailed);
    assert(parse("+CME ERROR: 10oops\r\n", ESP_FAIL) == State::QueryFailed);
    assert(parse("+CME ERROR: SIM not inserted\r\n", ESP_FAIL) == State::Absent);
    assert(parse("+CME ERROR: 11\r\n", ESP_FAIL) == State::PinRequired);
    assert(parse("+CME ERROR: 12\r\n", ESP_FAIL) == State::PukRequired);
    assert(parse("+CME ERROR: 13\r\n", ESP_FAIL) == State::Failure);
    assert(parse("+CME ERROR: 14\r\n", ESP_FAIL) == State::Busy);
    assert(parse("OK\r\n") == State::QueryFailed);

    // SIM startup: first query reports absent, then SIM becomes ready.
    UartEthModem delayed;
    reset(delayed);
    delayed.replies = {{"+CME ERROR: 10\r\n", ESP_FAIL},
        {"+CME ERROR: 14\r\n", ESP_FAIL}, {"+CPIN: READY\r\nOK\r\n"}};
    assert(delayed.CheckSimCard() == State::Ready);
    assert(delayed.queries == 3 && now_ms == 2000);
    // Persistent absence is bounded and cannot start registration or DHCP.
    UartEthModem absent;
    reset(absent);
    assert(absent.RunNormalModeInitSequence() == ESP_OK);
    assert(now_ms == 10000 && absent.queries == 10);
    assert(absent.last_event == Event::ErrorNoSim && absent.initialized_);
    assert(!absent.handshake_done_ && !absent.registration_waits && !absent.info_queries);
    // Explicit card errors and incomplete/timed-out queries are never called "no SIM".
    for (Reply reply : {Reply{"+CPIN: SIM PIN\r\nOK\r\n"},
                       Reply{"+CPIN: SIM PUK\r\nOK\r\n"},
                       Reply{"+CME ERROR: 14\r\n", ESP_FAIL},
                       Reply{"+CME ERROR: 13\r\n", ESP_FAIL},
                       Reply{"", ESP_ERR_TIMEOUT, 1000}, Reply{"OK\r\n"}}) {
        UartEthModem modem;
        reset(modem); modem.repeated = reply;
        assert(modem.RunNormalModeInitSequence() == ESP_OK);
        assert(modem.last_event == Event::ErrorSimNotReady && !modem.detail.empty());
        assert(modem.initialized_ && !modem.handshake_done_ && !modem.registration_waits);
        assert(now_ms <= 10000);
    }
    // A later transport failure must not reuse the earlier absent result.
    UartEthModem lost;
    reset(lost);
    lost.replies = {{"+CME ERROR: 10\r\n", ESP_FAIL}};
    lost.repeated = {"", ESP_ERR_TIMEOUT, 1000};
    assert(lost.CheckSimCard() == State::QueryFailed);
    // Cancel before querying, while waiting, and while an AT query completes.
    for (int when : {0, 1, 2}) {
        UartEthModem modem;
        reset(modem);
        if (when == 0) modem.stop_flag_ = true;
        if (when == 1) cancel_at = 1000;
        if (when == 2) modem.cancel_query = true;
        assert(modem.CheckSimCard() == State::Cancelled);
        assert(now_ms <= 1000 && !modem.events);
    }
    UartEthModem cancel_sim;
    reset(cancel_sim); cancel_sim.cancel_query = true;
    assert(cancel_sim.RunNormalModeInitSequence() == ESP_ERR_INVALID_STATE);
    assert(!cancel_sim.events && !cancel_sim.registration_waits);
    UartEthModem cancel_reg;
    reset(cancel_reg);
    cancel_reg.repeated = {"+CPIN: READY\r\nOK\r\n"};
    cancel_reg.cancel_registration = true;
    assert(cancel_reg.RunNormalModeInitSequence() == ESP_ERR_INVALID_STATE);
    assert(cancel_reg.last_event == Event::Connecting && cancel_reg.detail.empty());
    puts("PASS: delayed SIM readiness, bounded absence, PIN/PUK/errors and cancellation");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="cellular-readiness-") as folder:
        work = Path(folder)
        env, compiler = toolchain(work)
        # Normalize only the scoped return type for the existing function extractor.
        # The production method bodies are compiled unchanged.
        extract = work / "modem.cc"
        extract.write_text(MODEM.read_text().replace(
            "UartEthModem::SimCardState UartEthModem::", "SimCardState UartEthModem::"))
        source = MODEL
        for name in ("ParseSimCardState", "CheckSimCard", "RunNormalModeInitSequence"):
            source += function("UartEthModem::" + name, extract) + "\n"
        unit, binary = work / "test.cc", work / "test"
        unit.write_text(source + CASES)
        libraries = ["-lc++"] if sys.platform == "darwin" else ["-lstdc++"]
        subprocess.run(compiler + ["-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                  "-fsanitize=address,undefined", str(unit), "-o", str(binary)]
                       + libraries, cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    main()
