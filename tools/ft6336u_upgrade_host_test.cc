// 中文：用真实固件和有状态芯片替身验证擦写、退出及开机/唤醒边界。
// English: Stateful controller model with the real image checks writes, exits and init/wake boundaries.
#include "ft6336u_upgrade_protocol.h"
#include "ft6336u_upgrade.h"
#include "cst836u.h"
#include "board_i2c.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using Ft6336uUpgrade::Result;
static std::vector<uint8_t> image;
struct Controller {
    uint8_t version = 3, chip = 0x64, chip2 = 0x26, variant = 2;
    bool app_valid = true, boot = false, wrong_boot = false, erase_stall = false;
    bool packet_stall = false, first_packet_stall = false, first_ecc_bad = false;
    bool ecc_bad = false, version_bad = false, reset_fail = false, lock_fail = false;
    bool write_fail = false, read_fail = false, require_lock = false;
    unsigned reads = 0, writes = 0, erases = 0, packets = 0, resets = 0, boot_reads = 0;
    unsigned status_reads = 0, lock_calls = 0, unlock_calls = 0, hard_resets = 0;
    unsigned normal_writes = 0, removed = 0, written = 0;
    uint16_t status = 0;
    unsigned lock_depth = 0;
    std::vector<uint8_t> flash = std::vector<uint8_t>(Ft6336uUpgrade::kAppSize, 0xff);
    bool Write(const uint8_t* p, size_t n) {
        assert(n && (!require_lock || lock_depth == 1));
        ++writes;
        if (write_fail && p[0] != 0x07) return false;
        switch (p[0]) {
        case 0xbc: assert(n == 2 && (p[1] == 0xaa || p[1] == 0x55)); break;
        case 0x55: assert(n == 2 && p[1] == 0xaa); boot = true; break;
        case 0x61:
            assert(n == 1 && boot && !wrong_boot);
            ++erases; written = 0; status = 0xb002;
            std::fill(flash.begin(), flash.end(), 0xff);
            break;
        case 0xbf: {
            assert(boot && erases && n >= 6);
            const size_t offset = (size_t(p[1]) << 16) | (size_t(p[2]) << 8) | p[3];
            const size_t length = (size_t(p[4]) << 8) | p[5];
            assert(offset == written && length == std::min<size_t>(32, flash.size() - offset));
            assert(n == length + 6 && offset + length <= flash.size());
            assert(std::memcmp(p + 6, image.data() + offset, length) == 0);
            std::copy(p + 6, p + n, flash.begin() + offset);
            written += length; ++packets; status = uint16_t(0xb003 + offset / 32);
            break;
        }
        case 0x07:
            assert(n == 1); ++resets;
            if (reset_fail) return false;
            boot = false;
            // 首包失败也伪报V04，检查第二次确实整片重写。
            // Report V04 even after a partial first write to challenge retry skipping.
            if (erases) { app_valid = true; version = version_bad ? 3 : 4; }
            break;
        default: assert(false);
        }
        return true;
    }
    bool Read(const uint8_t* p, size_t command_size, uint8_t* out, size_t n) {
        assert(!require_lock || lock_depth == 1);
        ++reads; std::memset(out, 0, n);
        if (read_fail) return false;
        if (command_size == 1 && n == 1) {
            switch (p[0]) {
            case 0xa3: out[0] = app_valid && !boot ? chip : 0xff; return true;
            case 0x9f: out[0] = app_valid && !boot ? chip2 : 0xff; return true;
            case 0xa0: out[0] = app_valid && !boot ? variant : 0xff; return true;
            case 0xa6: out[0] = app_valid && !boot ? version : 0xff; return true;
            case 0xcc:
                assert(written == flash.size() && flash == std::vector<uint8_t>(image.begin(), image.begin() + flash.size()));
                for (uint8_t b : flash) out[0] ^= b;
                if (ecc_bad || (first_ecc_bad && erases == 1)) out[0] ^= 1;
                return true;
            }
        }
        assert(command_size == 4 && n == 2 && p[1] == 0 && p[2] == 0 && p[3] == 0);
        if (p[0] == 0x90) {
            ++boot_reads; assert(boot); out[0] = wrong_boot ? 0 : 0x79; out[1] = 0x1c;
        } else {
            assert(p[0] == 0x6a && boot); ++status_reads;
            uint16_t answer = status;
            if ((erase_stall && status == 0xb002) ||
                ((packet_stall || (first_packet_stall && erases == 1)) && status >= 0xb003)) answer = 0;
            out[0] = uint8_t(answer >> 8); out[1] = uint8_t(answer);
        }
        return true;
    }
    void Delay(int ms) { assert(ms > 0); }
};
static Controller controller;

static void protocol_tests() {
    assert(Ft6336uUpgrade::Validate(image.data(), image.size()));
    assert(!Ft6336uUpgrade::Validate(nullptr, image.size()));
    assert(!Ft6336uUpgrade::Validate(image.data(), 1));
    for (size_t index : {size_t(0), size_t(0x100), size_t(0x102), size_t(0x10a), image.size() - 1}) {
        auto corrupt = image; corrupt[index] ^= 1; Controller io; int attempts = 0;
        assert(Ft6336uUpgrade::Run(io, corrupt.data(), corrupt.size(), &attempts) == Result::InvalidImage);
        assert(attempts == 1 && io.reads == 0 && io.writes == 0);
    }
    for (unsigned version : {3u, 4u, 5u}) {
        Controller io; io.version = uint8_t(version); int attempts = 0;
        const auto result = Ft6336uUpgrade::Run(io, image.data(), image.size(), &attempts);
        assert(attempts == 1);
        if (version == 3) {
            assert(result == Result::Updated && io.erases == 1 && io.packets == 1472 && io.written == 47096);
            assert(io.version == 4 && io.resets == 1 && !io.boot);
        } else {
            assert(result == (version == 4 ? Result::Current : Result::Newer) && !io.writes);
        }
    }
    for (int field = 0; field < 3; ++field) {
        Controller io;
        (field == 0 ? io.chip : field == 1 ? io.chip2 : io.variant) ^= 1;
        assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::WrongChip && io.writes == 0);
    }
    { Controller io; io.app_valid = false;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::Updated);
      assert(io.resets == 2 && io.erases == 1); }
    { Controller io; io.wrong_boot = true; int attempts = 0;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size(), &attempts) == Result::BootIdError);
      assert(attempts == 2 && io.boot_reads == 60 && io.erases == 0 && io.resets == 2 && !io.boot); }
    { Controller io; io.erase_stall = true;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::EraseError);
      assert(io.erases == 2 && io.status_reads == 100 && io.packets == 0 && io.resets == 2); }
    { Controller io; io.packet_stall = true;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::PacketError);
      assert(io.erases == 2 && io.packets == 2 && io.status_reads == 202 && io.resets == 2); }
    { Controller io; io.first_packet_stall = true; int attempts = 0;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size(), &attempts) == Result::Updated);
      assert(attempts == 2 && io.erases == 2 && io.packets == 1473 && io.written == 47096); }
    { Controller io; io.first_ecc_bad = true; int attempts = 0;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size(), &attempts) == Result::Updated);
      assert(attempts == 2 && io.erases == 2 && io.packets == 2944); }
    { Controller io; io.ecc_bad = true;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::EccError && io.erases == 2); }
    { Controller io; io.version_bad = true;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::VersionError && io.erases == 2); }
    { Controller io; io.write_fail = true;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::IoError && io.erases == 0 && io.resets == 2); }
    { Controller io; io.reset_fail = true;
      // 首轮未退出Boot，第二轮的复位入口也失败，不得再擦除。
      // Failed boot exit also blocks the retry's reset entry, preventing another erase.
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::IoError && io.erases == 1 && io.resets == 3); }
    { Controller io; io.read_fail = true;
      assert(Ft6336uUpgrade::Run(io, image.data(), image.size()) == Result::BootIdError && io.erases == 0 && io.resets == 4); }
    std::cout << "PASS protocol: real image, versions, identity, recovery, bounded failures, forced complete retry\n";
}

extern "C" {
int i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t* bytes, size_t n, int timeout) {
    assert(dev && timeout == 500); return controller.Write(bytes, n) ? ESP_OK : ESP_FAIL;
}
int i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t* command, size_t command_size,
                               uint8_t* output, size_t n, int timeout) {
    assert(dev && timeout == 500); return controller.Read(command, command_size, output, n) ? ESP_OK : ESP_FAIL;
}
esp_err_t board_i2c_lock(int timeout) {
    assert(timeout == 1000 && !controller.lock_depth); ++controller.lock_calls;
    if (controller.lock_fail) return ESP_ERR_TIMEOUT;
    ++controller.lock_depth; return ESP_OK;
}
void board_i2c_unlock() { assert(controller.lock_depth == 1); --controller.lock_depth; ++controller.unlock_calls; }
esp_err_t board_i2c_add_device(i2c_master_bus_handle_t bus, uint8_t address, i2c_master_dev_handle_t* out) {
    assert(bus && address == 0x38); *out = reinterpret_cast<i2c_master_dev_handle_t>(&controller); return ESP_OK;
}
int i2c_master_bus_rm_device(i2c_master_dev_handle_t dev) { assert(dev); ++controller.removed; return ESP_OK; }
esp_err_t board_i2c_transmit(i2c_master_dev_handle_t dev, const uint8_t* bytes, size_t n, int timeout) {
    assert(dev && !controller.lock_depth && controller.hard_resets && timeout == 100);
    assert(n == 2 && bytes[0] == 0xa4 && bytes[1] == 0);
    ++controller.normal_writes; return ESP_OK;
}
esp_err_t board_i2c_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* out, size_t n, int timeout) {
    assert(dev && !controller.lock_depth && controller.normal_writes && timeout == 100);
    if (reg == 0x02) { assert(n == 13); std::memset(out, 0, n); return ESP_OK; }
    assert(n == 1 && (reg == 0xa3 || reg == 0xa6));
    *out = reg == 0xa3 ? controller.chip : controller.version; return ESP_OK;
}
int gpio_config(const gpio_config_t*) { return ESP_OK; }
int gpio_set_level(int, int) { return ESP_OK; }
int gpio_get_level(int) { return 1; }
void vTaskDelay(unsigned ticks) { assert(ticks >= 1); }
void esp_rom_delay_us(unsigned) {}
}
static esp_err_t hardware_reset() {
    assert(!controller.lock_depth); ++controller.hard_resets;
    return controller.reset_fail ? ESP_FAIL : ESP_OK;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    std::ifstream input(argv[2], std::ios::binary);
    image = {std::istreambuf_iterator<char>(input), {}};
    const std::string mode = argv[1];
    if (mode == "protocol") { protocol_tests(); return 0; }
    controller.require_lock = true;
    if (mode == "current") controller.version = 4;
    if (mode == "newer") controller.version = 5;
    if (mode == "wrongchip") controller.variant = 3;
    if (mode == "failure") controller.wrong_boot = true;
    if (mode == "lockfail") controller.lock_fail = true;
    if (mode == "resetfail") controller.reset_fail = true;
    cst836u_config_t config{};
    config.int_gpio = GPIO_NUM_NC; config.rst_gpio = GPIO_NUM_NC; config.reset_fn = hardware_reset;
    cst836u_handle_t handle = nullptr;
    auto bus = reinterpret_cast<i2c_master_bus_handle_t>(1);
    const bool success = mode == "upgrade" || mode == "current" || mode == "newer";
    assert(ft6336u_upgrade_on_boot(nullptr) == ESP_ERR_INVALID_ARG);
    assert((cst836u_init(bus, &config, &handle) == ESP_OK) == success);
    assert(bool(handle) == success && controller.lock_depth == 0);
    assert(controller.lock_calls == (mode == "resetfail" ? 0u : 1u));
    assert(controller.unlock_calls == (mode == "resetfail" || mode == "lockfail" ? 0u : 1u));
    if (success) {
        assert(controller.erases == (mode == "upgrade" ? 1u : 0u));
        cst836u_info_t info{}; assert(cst836u_get_info(handle, &info) == ESP_OK);
        assert(info.fw == (mode == "newer" ? 5 : 4));
        const auto raw_reads = controller.reads, raw_writes = controller.writes;
        for (unsigned wake = 0; wake < 3; ++wake) {
            assert(cst836u_set_mode(handle, CST836U_MODE_DEEPSLEEP) == ESP_OK);
            assert(cst836u_wake(handle) == ESP_OK);
        }
        cst836u_touch_t touch{}; assert(cst836u_read(handle, &touch) == ESP_OK && !touch.touched);
        assert(controller.lock_calls == 1 && controller.reads == raw_reads && controller.writes == raw_writes);
        assert(cst836u_deinit(handle) == ESP_OK);
        handle = nullptr;
        assert(cst836u_init(bus, &config, &handle) == ESP_OK);
        assert(controller.lock_calls == 1 && controller.reads == raw_reads && controller.writes == raw_writes);
        assert(cst836u_deinit(handle) == ESP_OK);
    } else {
        assert(controller.normal_writes == 0 && controller.removed == 1);
        if (mode != "resetfail") {
            const auto writes = controller.writes;
            assert(cst836u_init(bus, &config, &handle) != ESP_OK && !handle);
            assert(controller.lock_calls == 1 && controller.writes == writes && controller.removed == 2);
        }
    }
    std::cout << "PASS driver/transport " << mode << ": init ordering, exclusive bus, once per boot, no wake upgrades\n";
}
