#!/usr/bin/env python3
"""中文：执行真实FT6336U升级协议、传输适配与驱动的故障注入测试。
English: Exercise the actual FT6336U protocol, transport and driver with injected faults.
"""
import hashlib
from pathlib import Path
import subprocess
import tempfile

from metalio_drivers_host_test import ERRORS, FREERTOS, GPIO, I2C
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/cst836u"
IMAGE = COMPONENT / "firmware/ft6336u_v04_app.bin"


def c_linkage(source):
    prefix = '#ifdef __cplusplus\nextern "C" {\n#endif\n'
    suffix = '\n#ifdef __cplusplus\n}\n#endif\n'
    return prefix + source + suffix


def main():
    assert hashlib.sha256(IMAGE.read_bytes()).hexdigest() == (
        "60a660a4f80bf7aa8000a2c597f83620ea9b815bec1db31be2c8002cce1ee089")
    with tempfile.TemporaryDirectory(prefix="ft6336u-upgrade-") as directory:
        work = Path(directory)
        env, compiler = toolchain(work)
        headers = {
            "esp_err.h": ERRORS,
            "driver/i2c_master.h": c_linkage(I2C),
            "driver/gpio.h": c_linkage(GPIO),
            "freertos/FreeRTOS.h": FREERTOS,
            "freertos/task.h": c_linkage("void vTaskDelay(unsigned);\n"),
            "esp_rom_sys.h": c_linkage("void esp_rom_delay_us(unsigned);\n"),
            "esp_log.h": "#pragma once\nstatic inline void host_log(const char *tag, const char *format, ...) {(void)tag;(void)format;}\n" +
                         "#define ESP_LOGI(...) host_log(__VA_ARGS__)\n#define ESP_LOGW(...) host_log(__VA_ARGS__)\n#define ESP_LOGE(...) host_log(__VA_ARGS__)\n",
            "sdkconfig.h": "#pragma once\n#define CONFIG_IDF_TARGET_ESP32S31 1\n#define CONFIG_TOUCH_FT6336U_UPGRADE_ON_BOOT 1\n#define CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT 100\n",
        }
        for name, content in headers.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        assembly = work / "firmware.S"
        assembly.write_text('.data\n.globl _binary_ft6336u_v04_app_bin_start\n'
                            '_binary_ft6336u_v04_app_bin_start:\n.incbin "' + str(IMAGE) +
                            '"\n.globl _binary_ft6336u_v04_app_bin_end\n_binary_ft6336u_v04_app_bin_end:\n'
                            '#if defined(__linux__)\n.section .note.GNU-stack,"",@progbits\n#endif\n')
        includes = ["-I" + str(work), "-I" + str(COMPONENT),
                    "-I" + str(COMPONENT / "include"),
                    "-I" + str(ROOT / "components/board_i2c/include")]
        flags = ["-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined"]
        driver = work / "driver.o"
        subprocess.run(compiler + flags + includes + ["-std=gnu11", "-c", str(COMPONENT / "cst836u.c"), "-o", str(driver)], env=env, check=True)
        binary = work / "upgrade-test"
        subprocess.run(compiler + ["-x", "c++", "-std=c++17"] + flags + includes +
                       [str(ROOT / "tools/ft6336u_upgrade_host_test.cc"),
                        str(COMPONENT / "ft6336u_upgrade.cpp"), "-x", "none",
                        str(assembly), str(driver), "-lstdc++", "-o", str(binary)], env=env, check=True)
        for case in ("protocol", "upgrade", "current", "newer", "wrongchip", "failure", "lockfail", "resetfail"):
            subprocess.run([str(binary), case, str(IMAGE)], env=env, check=True)


if __name__ == "__main__":
    main()
