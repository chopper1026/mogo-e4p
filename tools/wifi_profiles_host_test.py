#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""多网络存储与触屏配网的实际实现回归。/ Regress real multi-network storage and provisioning UI.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]


def station_scan_regression(work, env, compiler, flags):
    wifi = ROOT / "components/wifi_transfer/wifi_transfer.c"
    source = wifi.read_text()
    station = "\n".join(function(name, wifi) for name in (
        "supported_station_auth", "best_saved_ap", "scan_saved_station", "configure_station"))
    start = source.index("wifi_transfer_network_t item = {.rssi = ap->rssi")
    end = source.index("scan_offer(out, count, &item);", start)
    # 执行生产扫描、选网与认证分类。/ Exercise the production scan, profile selection and auth classification.
    harness = work / "scan_auth.c"
    harness.write_text('''#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_transfer_network.h"
enum { WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
       WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_ENTERPRISE, WIFI_AUTH_WPA3_PSK,
       WIFI_AUTH_WPA2_WPA3_PSK, WIFI_AUTH_WAPI_PSK, WIFI_AUTH_OWE,
       WIFI_AUTH_WPA3_ENT_192, WIFI_AUTH_DUMMY_1, WIFI_AUTH_DUMMY_2,
       WIFI_AUTH_DPP, WIFI_AUTH_WPA3_ENTERPRISE, WIFI_AUTH_WPA2_WPA3_ENTERPRISE,
       WIFI_AUTH_WPA_ENTERPRISE, WIFI_AUTH_UNKNOWN, WIFI_AUTH_MAX };
typedef int wifi_auth_mode_t;
typedef struct { int8_t rssi; int authmode; unsigned char ssid[33]; } wifi_ap_record_t;
typedef struct { char ssid[33], password[65]; } transfer_credentials_t;
typedef struct { size_t count; transfer_credentials_t items[WIFI_TRANSFER_SAVED_MAX]; } transfer_profiles_t;
typedef struct {
    struct { unsigned char ssid[32], password[64]; struct { int authmode; } threshold;
        int scan_method, sort_method, sae_pwe_h2e; struct { bool capable; } pmf_cfg;
    } sta;
} wifi_config_t;
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_NOT_FOUND, ESP_ERR_NO_MEM, ESP_ERR_WIFI_STATE, ESP_ERR_WIFI_CONN };
enum { WIFI_IF_STA, WIFI_ALL_CHANNEL_SCAN, WIFI_CONNECT_AP_BY_SIGNAL, WPA3_SAE_PWE_BOTH };
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdMS_TO_TICKS(ms) (ms)
static wifi_ap_record_t aps[32];
static uint16_t ap_count;
static bool list_live, fail_calloc;
static unsigned scans, clears, stops, configs;
static int scan_result, count_result, records_result;
static wifi_config_t configured;
static void *heap_caps_calloc(size_t n, size_t size, int caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) && n == ap_count);
    return fail_calloc ? NULL : calloc(n, size);
}
static void heap_caps_free(void *p) { free(p); }
static void clear_secret(void *p, size_t size) { memset(p, 0, size); }
static void vTaskDelay(unsigned ticks) { assert(ticks == 180); }
static int esp_wifi_scan_start(const void *cfg, bool block) {
    assert(!cfg && block && !list_live);
    ++scans; list_live = true;
    int result = scan_result; scan_result = ESP_OK;
    return result;
}
static int esp_wifi_scan_stop(void) { ++stops; return ESP_OK; }
static int esp_wifi_clear_ap_list(void) { ++clears; list_live = false; return ESP_OK; }
static int esp_wifi_scan_get_ap_num(uint16_t *count) {
    assert(list_live); *count = ap_count; return count_result;
}
static int esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records) {
    assert(list_live && *count == ap_count);
    if (records_result != ESP_OK) return records_result;
    memcpy(records, aps, ap_count * sizeof(*aps)); list_live = false; return ESP_OK;
}
static int esp_wifi_set_config(int interface, wifi_config_t *cfg) {
    assert(interface == WIFI_IF_STA && !list_live);
    ++configs; configured = *cfg; return ESP_OK;
}
''' + station + '''
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
    const transfer_profiles_t profiles = {.count = 3, .items = {
        {"home", "home-pass"}, {"0123456789abcdefghijklmnopqrstuv", "work-pass"}, {"guest", ""}
    }};
    const transfer_credentials_t *selected = NULL;
    // 已保存公司网在 UI 上限之外；同名开放网、企业网和未保存的强信号均不能胜出。
    // The saved office AP is beyond the UI limit; same-name open/enterprise APs and stronger unknown APs cannot win.
    ap_count = 20;
    for (uint16_t i = 0; i < ap_count; ++i) aps[i] = (wifi_ap_record_t){-20, WIFI_AUTH_OPEN, "other"};
    aps[0] = (wifi_ap_record_t){-12, WIFI_AUTH_OPEN, "home"};
    aps[1] = (wifi_ap_record_t){-10, WIFI_AUTH_ENTERPRISE, "home"};
    aps[17] = (wifi_ap_record_t){-83, WIFI_AUTH_WPA2_PSK, "0123456789abcdefghijklmnopqrstuv"};
    aps[18] = (wifi_ap_record_t){-69, WIFI_AUTH_WPA3_PSK, "0123456789abcdefghijklmnopqrstuv"};
    aps[19] = (wifi_ap_record_t){-72, WIFI_AUTH_OPEN, "guest"};
    assert(configure_station(&profiles, false, &selected) == ESP_OK);
    assert(selected == &profiles.items[1] && scans == 1 && configs == 1 && !list_live);
    assert(!memcmp(configured.sta.ssid, profiles.items[1].ssid, sizeof(configured.sta.ssid)));
    assert(!strcmp((char *)configured.sta.password, "work-pass"));
    aps[3] = (wifi_ap_record_t){-69, WIFI_AUTH_WPA2_PSK, "home"};
    assert(configure_station(&profiles, false, &selected) == ESP_OK && selected == &profiles.items[0]);
    aps[19].rssi = -55;
    assert(configure_station(&profiles, false, &selected) == ESP_OK && selected == &profiles.items[2]);
    assert(configured.sta.threshold.authmode == WIFI_AUTH_OPEN && !configured.sta.password[0]);
    assert(configure_station(&profiles, true, &selected) == ESP_OK && selected == &profiles.items[0]);
    assert(scans == 3 && configs == 4);
    ap_count = 3;
    assert(configure_station(&profiles, false, NULL) == ESP_ERR_NOT_FOUND && configs == 4 && !list_live);
    ap_count = 0;
    assert(configure_station(&profiles, false, NULL) == ESP_ERR_NOT_FOUND && clears == 1 && !list_live);
    count_result = ESP_FAIL;
    assert(configure_station(&profiles, false, NULL) == ESP_FAIL && clears == 2 && !list_live);
    count_result = ESP_OK;
    ap_count = 20; fail_calloc = true;
    assert(configure_station(&profiles, false, NULL) == ESP_ERR_NO_MEM && clears == 3 && !list_live);
    fail_calloc = false; records_result = ESP_FAIL;
    assert(configure_station(&profiles, false, NULL) == ESP_FAIL && clears == 4 && !list_live);
    records_result = ESP_OK; scan_result = ESP_FAIL;
    assert(configure_station(&profiles, false, NULL) == ESP_FAIL && clears == 5 && stops == 1 && !list_live);
    scan_result = ESP_ERR_WIFI_STATE;
    assert(configure_station(&profiles, false, &selected) == ESP_OK && selected == &profiles.items[2]);
    assert(clears == 6 && stops == 2 && configs == 5 && !list_live);
    assert(configure_station(&(transfer_profiles_t){0}, false, NULL) == ESP_ERR_NOT_FOUND && scans == 11);
    puts("transfer scan: auth allowlist, roaming startup selection, manual override and cleanup PASS");
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
        station_scan_regression(work, env, compiler, flags)
        cjson = ROOT / "managed_components/espressif__cjson/cJSON"
        cases = {
            "storage": (["tools/transfer_wifi_host_test.c", str(cjson / "cJSON.c")], [str(cjson)]),
            "ui": (["tools/transfer_ui_host_test.c", "main/ui/ui_text_edit.c", "main/ui/ui_text_input.c",
                    "main/ui/ui_keyboard.c", "main/ui/ui_ime.c", "components/book_search/book_search.c"],
                   ["tools/transfer_ui_stubs", "components/wifi_transfer/include",
                    "components/book_search/include", "main", "main/ui", "main/services", "main/app"]),
        }
        for name, (sources, includes) in cases.items():
            binary = work / name
            subprocess.run(compiler + flags + ["-I" + path for path in includes] + sources +
                           ["-lm", "-o", str(binary)], cwd=ROOT, env=env, check=True)
            subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
        sources, includes = cases["ui"]
        binary = work / "ui-cloudzao"
        subprocess.run(compiler + flags + ["-DCONFIG_MOGO_CLOUDZAO_TRANSFER=1"] +
                       ["-I" + path for path in includes] + sources + ["-lm", "-o", str(binary)],
                       cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
        subprocess.run([sys.executable, "tools/test_transfer_wifi_forget.py"], cwd=ROOT, env=env, check=True)


if __name__ == "__main__":
    main()
