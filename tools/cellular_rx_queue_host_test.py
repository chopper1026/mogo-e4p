#!/usr/bin/env python3
"""Verify that a full modem event queue immediately returns a DMA RX buffer."""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/services/cellular/uart_eth_modem.cc"

MODEL = r'''
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#define IRAM_ATTR
using BaseType_t = int;
constexpr BaseType_t pdFALSE = 0, pdTRUE = 1;
using QueueHandle_t = int;
struct UartUhci {
    struct RxBuffer {};
    struct RxEventData { RxBuffer* buffer; size_t recv_size; };
    unsigned returned = 0;
    void ReturnBufferFromISR(RxBuffer*) { ++returned; }
    void RecoverOverflowIfReady() {}
};
static BaseType_t queue_result;
static BaseType_t xQueueSendFromISR(QueueHandle_t, const void*, BaseType_t*) {
    return queue_result;
}
class UartEthModem {
public:
    enum class EventType : uint8_t { None, TxRequest, SrdyLow, SrdyHigh, RxData, Stop };
    struct Event { EventType type; UartUhci::RxBuffer* rx_buffer; };
    QueueHandle_t event_queue_ = 1;
    UartUhci uart_uhci_;
    std::atomic<uint32_t> rx_event_drops_{0};
    static bool UhciRxCallbackStatic(const UartUhci::RxEventData&, void*);
};
'''

CASES = r'''
int main() {
    UartEthModem modem;
    UartUhci::RxBuffer buffer;
    UartUhci::RxEventData data{&buffer, 1600};
    queue_result = pdTRUE;
    assert(!UartEthModem::UhciRxCallbackStatic(data, &modem));
    assert(modem.uart_uhci_.returned == 0 && modem.rx_event_drops_.load() == 0);
    queue_result = pdFALSE;
    assert(!UartEthModem::UhciRxCallbackStatic(data, &modem));
    assert(modem.uart_uhci_.returned == 1 && modem.rx_event_drops_.load() == 1);
    puts("PASS: full RX event queue returns DMA buffer");
}
'''

with tempfile.TemporaryDirectory(prefix="cellular-rx-queue-") as folder:
    work = Path(folder)
    env, compiler = toolchain(work)
    unit = work / "test.cc"
    unit.write_text(MODEL + function("UartEthModem::UhciRxCallbackStatic", SOURCE) + CASES)
    binary = work / "test"
    subprocess.run(compiler + ["-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                               "-fsanitize=address,undefined", str(unit), "-o", str(binary)] +
                   (["-lc++"] if __import__("sys").platform == "darwin" else ["-lstdc++"]),
                   cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
