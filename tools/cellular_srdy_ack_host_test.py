#!/usr/bin/env python3
"""Verify that only a high SRDY edge releases the transmit ACK wait."""
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
#define IRAM_ATTR
using gpio_num_t = int;
using EventBits_t = unsigned;
using BaseType_t = int;
constexpr BaseType_t pdFALSE = 0;
static constexpr unsigned kEventSrdyHigh = 1u << 7;
static unsigned bits;
static int srdy_level;
struct Gpio { int unused; };
static Gpio GPIO;
static void gpio_ll_intr_disable(Gpio*, int) {}
static int gpio_ll_get_level(Gpio*, int) { return srdy_level; }
static void xQueueSendFromISR(int, void*, BaseType_t*) {}
static void xEventGroupSetBitsFromISR(int, unsigned value, BaseType_t*) { bits |= value; }
static void portYIELD_FROM_ISR() {}
class UartEthModem {
public:
    enum class EventType : uint8_t { None, TxRequest, SrdyLow, SrdyHigh, RxData, Stop };
    struct Event { EventType type; void* rx_buffer; };
    int srdy_pin_isr_ = 3, event_queue_ = 1, event_group_ = 1;
    static void SrdyIsrHandler(void*);
};
'''

CASES = r'''
int main() {
    UartEthModem modem;
    srdy_level = 0; bits = 0;
    UartEthModem::SrdyIsrHandler(&modem);
    assert(bits == 0);
    srdy_level = 1;
    UartEthModem::SrdyIsrHandler(&modem);
    assert(bits == kEventSrdyHigh);
    puts("PASS: SRDY low edges do not acknowledge TX; high edges do");
}
'''

with tempfile.TemporaryDirectory(prefix="cellular-srdy-") as folder:
    work = Path(folder)
    env, compiler = toolchain(work)
    unit = work / "test.cc"
    unit.write_text(MODEL + function("UartEthModem::SrdyIsrHandler", SOURCE) + CASES)
    binary = work / "test"
    subprocess.run(compiler + ["-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                               "-fsanitize=address,undefined", str(unit), "-o", str(binary)] +
                   (["-lc++"] if __import__("sys").platform == "darwin" else ["-lstdc++"]),
                   cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
