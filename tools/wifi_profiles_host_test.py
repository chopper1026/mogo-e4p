#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""多网络存储与触屏配网的实际实现回归。/ Regress real multi-network storage and provisioning UI.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]


def scan_auth_regression(work, env, compiler, flags):
    source = (ROOT / "components/wifi_transfer/wifi_transfer.c").read_text()
    start = source.index("wifi_transfer_network_t item = {.rssi = ap->rssi")
    end = source.index("scan_offer(out, count, &item);", start)
    # 执行生产扫描分类，避免宿主桩绕过认证白名单。/ Run production classification, not a stubbed allowlist.
    harness = work / "scan_auth.c"
    harness.write_text('''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_transfer_network.h"
enum { WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
       WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_ENTERPRISE, WIFI_AUTH_WPA3_PSK,
       WIFI_AUTH_WPA2_WPA3_PSK, WIFI_AUTH_WAPI_PSK, WIFI_AUTH_OWE,
       WIFI_AUTH_WPA3_ENT_192, WIFI_AUTH_DUMMY_1, WIFI_AUTH_DUMMY_2,
       WIFI_AUTH_DPP, WIFI_AUTH_WPA3_ENTERPRISE, WIFI_AUTH_WPA2_WPA3_ENTERPRISE,
       WIFI_AUTH_WPA_ENTERPRISE, WIFI_AUTH_UNKNOWN, WIFI_AUTH_MAX };
typedef struct { int8_t rssi; int authmode; unsigned char ssid[33]; } wifi_ap_record_t;
static wifi_transfer_network_t classify(const wifi_ap_record_t *ap) {
''' + source[start:end] + '''return item;
}
int main(void) {
    for (int auth = WIFI_AUTH_OPEN; auth <= WIFI_AUTH_MAX; ++auth) {
        wifi_ap_record_t ap = {.rssi = -45, .authmode = auth, .ssid = "legacy-router"};
        wifi_transfer_network_t item = classify(&ap);
        bool expected = auth == WIFI_AUTH_OPEN || auth == WIFI_AUTH_WPA_PSK ||
            auth == WIFI_AUTH_WPA2_PSK || auth == WIFI_AUTH_WPA_WPA2_PSK ||
            auth == WIFI_AUTH_WPA3_PSK || auth == WIFI_AUTH_WPA2_WPA3_PSK;
        assert(item.supported == expected);
        assert(item.requires_password == (auth != WIFI_AUTH_OPEN && auth != WIFI_AUTH_OWE));
        assert(item.authmode == auth && item.rssi == -45 && !strcmp(item.ssid, "legacy-router"));
    }
    puts("transfer scan auth: WPA/WPA2/WPA3 personal allowed; unsupported modes rejected PASS");
}
''')
    binary = work / "scan_auth"
    subprocess.run(compiler + flags + ["-Icomponents/wifi_transfer/include", str(harness),
                   "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)


def main():
    with tempfile.TemporaryDirectory(prefix="mogo-wifi-profiles-") as directory:
        work = Path(directory)
        env, compiler = toolchain(work)
        flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
                 "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        scan_auth_regression(work, env, compiler, flags)
        cjson = ROOT / "managed_components/espressif__cjson/cJSON"
        cases = {
            "storage": (["tools/transfer_wifi_host_test.c", str(cjson / "cJSON.c")], [str(cjson)]),
            "ui": (["tools/transfer_ui_host_test.c", "main/ui/ui_text_edit.c", "main/ui/ui_text_input.c",
                    "main/ui/ui_keyboard.c", "main/ui/ui_ime.c", "components/book_search/book_search.c"],
                   ["tools/transfer_ui_stubs", "components/wifi_transfer/include",
                    "components/book_search/include", "main/ui", "main/services"]),
        }
        for name, (sources, includes) in cases.items():
            binary = work / name
            subprocess.run(compiler + flags + ["-I" + path for path in includes] + sources +
                           ["-lm", "-o", str(binary)], cwd=ROOT, env=env, check=True)
            subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
        subprocess.run([sys.executable, "tools/test_transfer_wifi_forget.py"], cwd=ROOT, env=env, check=True)


if __name__ == "__main__":
    main()
