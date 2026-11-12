#!/usr/bin/env python3
"""Exercise fixed internal SIM startup and fragmented AT responses with sanitizers.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import sys
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/services/cellular/uart_eth_modem.cc"

MODEL = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
constexpr int ESP_ERR_INVALID_RESPONSE = -3, ESP_ERR_TIMEOUT = -4;
constexpr unsigned kEventAtResponse = 1;
static unsigned event_bits;
static void xEventGroupSetBits(int, unsigned bits) { event_bits |= bits; }
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(unsigned ms) { assert(ms == 500); }
struct Reply { std::string command; esp_err_t result; std::string response; };
class UartEthModem {
public:
    std::deque<Reply> replies;
    std::mutex at_response_mutex_;
    int event_group_ = 1;
    bool waiting_for_at_response_ = true, at_collect_until_done_ = false;
    std::string at_command_response_, at_until_marker_;
    void ParseAtResponse(const std::string&) {}
    void HandleAtResponse(const char*, size_t);
    static int ParseSimSlot(const std::string&);
    esp_err_t ConfigureInternalSim();
    esp_err_t SendAt(const std::string& cmd, std::string& response, unsigned timeout) {
        assert(timeout > 0 && !replies.empty());
        Reply next = replies.front(); replies.pop_front();
        if (cmd != next.command) fprintf(stderr, "Expected %s, got %s\n", next.command.c_str(), cmd.c_str());
        assert(cmd == next.command);
        response = next.response;
        return next.result;
    }
    void Feed(const std::string& data) { HandleAtResponse(data.data(), data.size()); }
};
static std::string slot(int value) {
    return "\r\n+ECSIMCFG: \"SimSlot\"," + std::to_string(value) + "\r\n\r\nOK\r\n";
}
'''

CASES = r'''
int main() {
    assert(UartEthModem::ParseSimSlot(slot(0)) == 0);
    assert(UartEthModem::ParseSimSlot("+ECSIMCFG: \"SimSlot\" , 1\r\n") == 1);
    for (const char *bad : {"OK", "\"SimSlot\",10\r\n", "\"SimSlot\",-1\r\n", "\"SimSlot\",0bad\r\n"})
        assert(UartEthModem::ParseSimSlot(bad) == -1);
    // Already internal: query only. A saved external selection is always overridden.
    UartEthModem same;
    same.replies = {{"AT+ECSIMCFG?", ESP_OK, slot(1)}};
    assert(same.ConfigureInternalSim() == ESP_OK && same.replies.empty());
    for (const Reply& initial : {Reply{"AT+ECSIMCFG?", ESP_OK, slot(0)},
                                Reply{"AT+ECSIMCFG?", ESP_ERR_TIMEOUT, ""}}) {
        for (int confirmed : {0, 1}) {
            UartEthModem modem;
            modem.replies = {initial, {"AT+CFUN=0", ESP_OK, "OK"},
                {"AT+ECSIMCFG=SimSlot,1", ESP_OK, "OK"},
                {"AT+CFUN=1", ESP_OK, "OK"}, {"AT+ECSIMCFG?", ESP_OK, slot(confirmed)}};
            assert(modem.ConfigureInternalSim() == (confirmed == 1 ? ESP_OK : ESP_ERR_INVALID_RESPONSE));
            assert(modem.replies.empty());
        }
    }
    // Restore CFUN even after configuration or radio-shutdown failure.
    for (bool shutdown_failed : {false, true}) {
        UartEthModem modem;
        modem.replies = {{"AT+ECSIMCFG?", ESP_OK, slot(0)},
            {"AT+CFUN=0", shutdown_failed ? ESP_ERR_TIMEOUT : ESP_OK, "OK"}};
        if (!shutdown_failed) modem.replies.push_back({"AT+ECSIMCFG=SimSlot,1", ESP_FAIL, "ERROR"});
        modem.replies.push_back({"AT+CFUN=1", ESP_OK, "OK"});
        modem.replies.push_back({"AT+ECSIMCFG?", ESP_OK, slot(1)});
        assert(modem.ConfigureInternalSim() != ESP_OK && modem.replies.empty());
    }
    for (bool radio_failed : {false, true}) {
        UartEthModem modem;
        modem.replies = {{"AT+ECSIMCFG?", ESP_OK, slot(0)},
            {"AT+CFUN=0", ESP_OK, "OK"}, {"AT+ECSIMCFG=SimSlot,1", ESP_OK, "OK"},
            {"AT+CFUN=1", radio_failed ? ESP_ERR_TIMEOUT : ESP_OK, ""},
            {"AT+ECSIMCFG?", ESP_ERR_TIMEOUT, ""}};
        assert(modem.ConfigureInternalSim() == (radio_failed ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_RESPONSE));
        assert(modem.replies.empty());
    }

    // Query data and the final terminator can each cross UART frame boundaries.
    UartEthModem fragmented;
    event_bits = 0;
    fragmented.Feed("\r\n+ECSIMCFG: \"Sim");
    fragmented.Feed("Slot\",0\r\n\r\nO");
    assert(!event_bits);
    fragmented.Feed("K\r\n");
    assert(event_bits && UartEthModem::ParseSimSlot(fragmented.at_command_response_) == 0);
    const auto captured = fragmented.at_command_response_;
    fragmented.Feed("\r\n+CEREG: 2\r\n");
    assert(fragmented.at_command_response_ == captured);
    UartEthModem collecting;
    collecting.at_collect_until_done_ = true;
    collecting.at_until_marker_ = "DONE";
    event_bits = 0;
    collecting.Feed("\r\nOK\r\nDO"); assert(!event_bits);
    collecting.Feed("NE\r\n"); assert(event_bits);
    UartEthModem error;
    error.at_collect_until_done_ = true; error.at_until_marker_ = "DONE";
    event_bits = 0;
    error.Feed("\r\nOK\r\n+CME ER"); assert(!event_bits);
    error.Feed("ROR: 10\r\n"); assert(event_bits && !error.waiting_for_at_response_);
    UartEthModem bounded;
    bounded.Feed(std::string(8193, 'x'));
    assert(!bounded.waiting_for_at_response_ && bounded.at_command_response_.size() < 8192);
    puts("PASS: fixed internal SIM, verified selection, radio recovery and fragmented AT replies");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="cellular-sim-") as folder:
        work = Path(folder)
        env, compiler = toolchain(work)
        source = MODEL
        for name in ("ParseSimSlot", "ConfigureInternalSim", "HandleAtResponse"):
            source += function("UartEthModem::" + name, SOURCE) + "\n"
        unit, binary = work / "test.cc", work / "test"
        unit.write_text(source + CASES)
        libraries = ["-lc++"] if sys.platform == "darwin" else ["-lstdc++"]
        subprocess.run(compiler + ["-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                  "-fsanitize=address,undefined", str(unit), "-o", str(binary)]
                       + libraries, cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    main()
