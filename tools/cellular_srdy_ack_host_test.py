#!/usr/bin/env python3
"""Exercise the real TX task and ISR against SRDY busy/ACK pulses."""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/services/cellular/uart_eth_modem.cc"

MODEL = r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define IRAM_ATTR
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) (++errors)
#define ESP_LOGW(...) (++warnings)
#define pdMS_TO_TICKS(ms) (ms)
using gpio_num_t = int;
using EventBits_t = unsigned;
using BaseType_t = int;
using esp_err_t = int;
constexpr BaseType_t pdFALSE = 0, pdTRUE = 1;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = -2, ESP_ERR_TIMEOUT = -3;
constexpr int portMAX_DELAY = -1, kAckTimeoutMs = 100;
constexpr bool kSrdyInterruptForAck = false;
static constexpr unsigned kEventSrdyHigh = 1u << 7;
static constexpr unsigned kEventStop = 1u << 6, kEventActiveState = 1u << 5;
static constexpr unsigned kEventTxTaskDone = 1u << 4;
static unsigned bits, warnings, errors, ack_waits;
static int srdy_level;
enum { GPIO_INTR_LOW_LEVEL, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE, GPIO_INTR_ANYEDGE };
static int intr_type;
static bool intr_enabled, deliver_ack, stop_on_wait;
static int transmit_result;
static int64_t now_us;
struct Gpio { int unused; };
static Gpio GPIO;
static void gpio_ll_intr_disable(Gpio*, int) { intr_enabled = false; }
static int gpio_ll_get_level(Gpio*, int) { return srdy_level; }
static void gpio_intr_disable(int) { intr_enabled = false; }
static void gpio_intr_enable(int) { intr_enabled = true; }
static void gpio_set_intr_type(int, int type) { intr_type = type; }
static void xQueueSendFromISR(int, void*, BaseType_t*) {}
static void xEventGroupSetBitsFromISR(int, unsigned value, BaseType_t*) { bits |= value; }
static void xEventGroupSetBits(int, unsigned value) { bits |= value; }
static void xEventGroupClearBits(int, unsigned value) { bits &= ~value; }
static int xQueueReceive(int, void*, int);
static void xQueueSend(int, const void*, int) {}
static unsigned xEventGroupWaitBits(int, unsigned mask, int clear, int, int timeout) {
    if (mask & kEventSrdyHigh) {
        ++ack_waits;
        if (stop_on_wait) bits |= kEventStop;
    }
    unsigned value = bits & mask;
    if (!value) now_us += (int64_t)timeout * 1000;
    if (clear) bits &= ~value;
    return value;
}
static int64_t esp_timer_get_time() { return now_us; }
static void portYIELD_FROM_ISR() {}
struct FakeUhci { int Transmit(uint8_t*, size_t); };
class UartEthModem {
public:
    enum class EventType : uint8_t { None, TxRequest, SrdyLow, SrdyHigh, RxData, Stop };
    enum class WorkingState { Active, Idle };
    struct Event { EventType type; void* rx_buffer; };
    struct TxCompletion {};
    struct TxFrame { uint8_t *data; size_t length; TxCompletion *completion; };
    struct { int srdy_pin = 3; } config_;
    int srdy_pin_isr_ = 3, event_queue_ = 1, event_group_ = 1, tx_queue_ = 2;
    std::atomic<bool> stop_flag_{false};
    std::atomic<WorkingState> working_state_{WorkingState::Active};
    int64_t last_activity_time_us_ = 0;
    int completion_result = 123;
    FakeUhci uart_uhci_;
    void CompleteTxFrame(TxCompletion*, int result) { completion_result = result; stop_flag_ = true; }
    void ConfigureSrdyInterrupt(bool);
    void TxTaskRun();
    static void SrdyIsrHandler(void*);
};
static UartEthModem *active_modem;
static void drive_srdy(int level) {
    const int previous = srdy_level;
    srdy_level = level;
    if (previous == level || !intr_enabled) return;
    if (intr_type == GPIO_INTR_ANYEDGE ||
        (intr_type == GPIO_INTR_POSEDGE && level) ||
        (intr_type == GPIO_INTR_NEGEDGE && !level))
        UartEthModem::SrdyIsrHandler(active_modem);
}
int FakeUhci::Transmit(uint8_t*, size_t length) {
    assert(length == 4 && intr_enabled && !(bits & kEventSrdyHigh));
    if (transmit_result != ESP_OK) return transmit_result;
    drive_srdy(0);  // Busy must not disable the upcoming ACK edge.
    if (deliver_ack) {
        drive_srdy(1); // A fast ACK arrives before Transmit() returns.
        drive_srdy(0);
    }
    return ESP_OK;
}
static int xQueueReceive(int queue, void *out, int wait) {
    assert(queue == 2 && wait == portMAX_DELAY);
    auto *frame = static_cast<UartEthModem::TxFrame*>(out);
    *frame = {static_cast<uint8_t*>(malloc(4)), 4, nullptr};
    assert(frame->data);
    return pdTRUE;
}
static void reset(UartEthModem *modem) {
    active_modem = modem; bits = warnings = errors = ack_waits = 0;
    srdy_level = 1; intr_enabled = true; intr_type = GPIO_INTR_ANYEDGE;
    deliver_ack = true; stop_on_wait = false; transmit_result = ESP_OK; now_us = 0;
}
'''

CASES = r'''
int main() {
    UartEthModem modem;
    reset(&modem);
    srdy_level = 0; bits = 0;
    UartEthModem::SrdyIsrHandler(&modem);
    assert(bits == 0);
    srdy_level = 1;
    UartEthModem::SrdyIsrHandler(&modem);
    assert(bits == kEventSrdyHigh);
    reset(&modem);
    modem.TxTaskRun();
    assert(ack_waits == 1 && !warnings && !errors && now_us == 0);
    assert(modem.completion_result == ESP_OK && (bits & kEventTxTaskDone));
    assert(intr_enabled && intr_type == GPIO_INTR_ANYEDGE);

    UartEthModem no_ack;
    reset(&no_ack); deliver_ack = false; bits = kEventSrdyHigh;
    no_ack.TxTaskRun();
    assert(ack_waits == 1 && warnings == 1 && now_us == 100000);
    assert(no_ack.completion_result == ESP_OK); // Preserve existing timeout policy.

    UartEthModem failed;
    reset(&failed); transmit_result = ESP_FAIL;
    failed.TxTaskRun();
    assert(!ack_waits && errors == 1 && failed.completion_result == ESP_FAIL);
    assert(intr_enabled && intr_type == GPIO_INTR_ANYEDGE);

    UartEthModem stopped;
    reset(&stopped); stop_on_wait = true;
    stopped.TxTaskRun();
    assert(!warnings && stopped.completion_result == ESP_ERR_INVALID_STATE);
    puts("PASS: real TX arms high ACK edges, ignores busy edges, catches fast ACKs and clears stale ACKs");
}
'''

with tempfile.TemporaryDirectory(prefix="cellular-srdy-") as folder:
    work = Path(folder)
    env, compiler = toolchain(work)
    unit = work / "test.cc"
    unit.write_text(MODEL + function("UartEthModem::ConfigureSrdyInterrupt", SOURCE) +
                    function("UartEthModem::SrdyIsrHandler", SOURCE) +
                    function("UartEthModem::TxTaskRun", SOURCE) + CASES)
    binary = work / "test"
    subprocess.run(compiler + ["-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                               "-fsanitize=address,undefined", str(unit), "-o", str(binary)] +
                   (["-lc++"] if __import__("sys").platform == "darwin" else ["-lstdc++"]),
                   cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
