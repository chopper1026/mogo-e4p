#!/usr/bin/env python3
"""中文：天气的真实模型与可取消服务回归，不联网。
English: Real weather model and cancellable service regressions without networking.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from run_reader_baseline import toolchain
from network_http_host_test import build_objects

ROOT = Path(__file__).resolve().parents[1]
service_source = (ROOT / "main/services/weather_service.c").read_text()
assert '"request start origin=' not in service_source
assert '"request finish phase=' not in service_source
with tempfile.TemporaryDirectory(prefix="weather-host-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined"]
    includes = ["-Imain/services", "-Itools", "-Imanaged_components/espressif__cjson/cJSON"]
    cjson = work / "cjson.o"
    subprocess.run(compiler + flags + includes + ["-Wno-deprecated-declarations", "-c",
        "managed_components/espressif__cjson/cJSON/cJSON.c", "-o", str(cjson)], cwd=ROOT, env=env, check=True)
    binary = work / "model"
    subprocess.run(compiler + flags + includes + ["tools/weather_model_host_test.c",
        "main/services/weather_model.c", str(cjson), "-lm", "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)
    shutil.copytree(ROOT / "tools/ota_stubs", work / "stubs")
    stubs = work / "stubs"
    (stubs / "nvs.h").write_text((ROOT / "tools/book_storage_stubs/nvs.h").read_text() +
        "\n#define ESP_ERR_NVS_NOT_FOUND 0x1102\n")
    (stubs / "device_power.h").write_text("""#pragma once
#include <stdbool.h>
typedef struct {int battery;} pmu_snapshot_t;
const pmu_snapshot_t *device_power_get(void);
int pmu_battery_percent(const pmu_snapshot_t *);
bool pmu_battery_charging(const pmu_snapshot_t *);
""")
    (stubs / "ble_page_turner.h").write_text("#pragma once\n#include <stdbool.h>\nbool ble_pt_connected(void);\n")
    with (stubs / "wifi_transfer.h").open("a") as output:
        output.write("\n#define WIFI_TRANSFER_STOPPED 0\n#define WIFI_TRANSFER_READY 2\n")
    (stubs / "network_service.h").write_text("""#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 2
#endif
#include "network_policy.h"
typedef struct { int bearer; bool owns_wifi, owns_cellular, active; } network_lease_t;
esp_err_t network_service_acquire(network_use_t use, network_lease_t *lease);
bool network_service_wait_ready(network_lease_t *lease, uint32_t timeout_ms);
void network_service_release(network_lease_t *lease);
""")
    network_stub = work / "network_service_stub.c"
    network_stub.write_text("""#include \"network_service.h\"
#include \"wifi_transfer.h\"
extern bool weather_test_cellular;
esp_err_t network_service_acquire(network_use_t use, network_lease_t *lease) {
    (void)use;
    if (!lease) return ESP_ERR_INVALID_ARG;
    *lease = (network_lease_t){0};
    if (weather_test_cellular) {
        lease->bearer = NETWORK_BEARER_CELLULAR; lease->active = true; return ESP_OK;
    }
    wifi_transfer_status_t status = {0};
    wifi_transfer_get_status(&status);
    if (status.mode == WIFI_TRANSFER_MODE_STA && status.network_ready) {
        lease->bearer = NETWORK_BEARER_WIFI; lease->active = true; return ESP_OK;
    }
    if (status.state != WIFI_TRANSFER_STOPPED) return ESP_ERR_NOT_FOUND;
    char ssid[33] = {0}; bool configured = false;
    if (wifi_transfer_get_saved_wifi(ssid, &configured) != ESP_OK || !configured)
        return ESP_ERR_NOT_FOUND;
    const wifi_transfer_cfg_t cfg = {.mode = WIFI_TRANSFER_MODE_STA, .network_only = true};
    if (wifi_transfer_start(&cfg) != ESP_OK) return ESP_ERR_NOT_FOUND;
    lease->bearer = NETWORK_BEARER_WIFI; lease->owns_wifi = true; lease->active = true; return ESP_OK;
}
bool network_service_wait_ready(network_lease_t *lease, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!lease || !lease->active) return false;
    wifi_transfer_status_t status = {0}; wifi_transfer_get_status(&status);
    return lease->bearer == NETWORK_BEARER_CELLULAR ||
        (lease->bearer == NETWORK_BEARER_WIFI && status.network_ready);
}
void network_service_release(network_lease_t *lease) {
    if (!lease || !lease->active) return;
    if (lease->owns_wifi) wifi_transfer_stop();
    *lease = (network_lease_t){0};
}
""")
    with (stubs / "esp_http_client.h").open("a") as output:
        output.write("\nesp_err_t esp_http_client_close(esp_http_client_handle_t);\n")
    binary = work / "service"
    http_objects = build_objects(work, env, compiler, flags)
    subprocess.run(compiler + flags + ["-I" + str(stubs)] + includes + ["-Imain/time", "-Imain/ota",
        "tools/weather_service_host_test.c", str(network_stub), "main/services/weather_model.c", str(cjson),
        *http_objects, "-lm", "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)
    subprocess.run([os.sys.executable, str(ROOT / "tools/weather_raster_host_test.py")], cwd=ROOT, env=env, check=True)
    subprocess.run([os.sys.executable, str(ROOT / "tools/weather_navigation_host_test.py")], cwd=ROOT, env=env, check=True)
    subprocess.run([os.sys.executable, str(ROOT / "tools/home_components_host_test.py")], cwd=ROOT, env=env, check=True)

# 共享系统回归保留原行为；这里单独核对新增交还和调度守卫的真实入口。
# Existing system regressions retain their behavior; verify new handoff/scheduling entries separately.
from home_cover_cache_host_test import function
for name, path, following in (
    ("quick_toggle_wifi", "main/app/app_quick_layer.c", "network_service_get_status"),
    ("app_lifecycle_pause_for_lock", "main/app/app_lifecycle.c", "wifi_transfer_pause_for_sleep"),
    ("app_lifecycle_prepare_poweroff", "main/app/app_lifecycle.c", "wifi_transfer_pause_for_sleep"),
):
    body = function(name, ROOT / path)
    assert body.index("app_weather_cancel_join") < body.index(following)
loop = (ROOT / "main/app/app_loop.c").read_text()
assert "pico_ota_auto_poll(!app_weather_busy()" in loop
assert "app_time_service_poll(!app_weather_busy()" in loop
for page in ("main/apps/app_dashboard.c", "main/apps/app_components.c"):
    assert ".on_before_lock=" in (ROOT / page).read_text()
print("PASS: weather scheduling and handoff entries for quick WiFi, lock, shutdown and both pages")
