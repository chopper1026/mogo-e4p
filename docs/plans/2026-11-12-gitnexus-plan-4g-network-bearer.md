# GitNexus Engineering Plan

> Task: 为 mogo E4P 设计 Wi-Fi/4G 双 SKU、双网络快捷入口和可选联网路径。
> Evidence verified at commit `a75f6c40159344dd7a4475d3936d280ed6d2a7d7`; GitNexus index refreshed this session with `--index-only --pdg`.
> The C/CPP index reported candidate caps and dropped callable/field links; graph output is navigation-only, and load-bearing behavior below is source-weighted.
> Evidence provenance schema 2; global dirty digest and cited-path manifest are included in §11; exact generated plan path is excluded.

## 1. Objective

[inferred] Deliver two explicit firmware SKUs: `mogo E4P Wi-Fi` and `mogo E4P 4G`. The 4G SKU keeps Wi-Fi and adds cellular data for date/time synchronization, weather, and online OTA; the Wi-Fi SKU must never gain 4G capability from a saved setting or a failed runtime probe. The quick layer keeps a Wi-Fi entry on every SKU and adds a separate mobile-network entry on the 4G SKU. Each entry toggles only its own radio on tap and opens that radio's settings on hold. A persisted route policy (`WIFI`, `CELLULAR` or `AUTO`) independently selects the bearer for external services; forced modes do not silently fall back, while `AUTO` may use its configured priority.

[verified] Wi-Fi remains the only local AP/STA transfer path. The change must not make cellular a local file-transfer network.

## 2. Current Behaviour

[verified] `components/board_support/Kconfig:1-32` has only I2C/touch/expander settings, and `components/board_support/board_init.c:27-106` initializes frontlight, EPD, accelerometer, PMU and touch; no modem capability, modem pins or cellular power lifecycle exists.

[verified] `wifi_transfer` owns AP/STA lifecycle, netif/event-loop setup, BLE memory reservation and stop cleanup (`components/wifi_transfer/wifi_transfer.c:2046-2145`). Its public status and time APIs are Wi-Fi-shaped (`components/wifi_transfer/include/wifi_transfer.h:24-127`), and `transfer_policy.h:20-52` only models Wi-Fi time routes.

[verified] Weather directly starts saved Wi-Fi when no ready STA exists, then performs HTTPS (`main/services/weather_service.c:171-228,271-347`). Time sync similarly checks STA readiness and saved Wi-Fi before starting a temporary session (`main/time/time_service.c:82-175`); the file explicitly freezes radio-session ownership inside `wifi_transfer` (`main/time/time_service.c:11-14`).

[verified] Online OTA cancels time, starts saved Wi-Fi when needed, polls Wi-Fi, and reads through the same Wi-Fi service (`main/ota/ota_online.c:128-168,218-238`); automatic OTA has the same STA-only gate (`main/ota/ota_auto.c:85-103`).

[verified] The first quick-layer circle is hard-coded as Wi-Fi for status, tap, hold and drawing (`main/app/app_quick_layer.c:66-72,174-190,275-325`; `main/ui/ui_quick_menu_input.c:195-214,279-284`; `main/ui/ui_quick_menu.c:46,115,239-258`). The top status bar also draws only a Wi-Fi mark (`main/ui/ui_nav.c:230-261`).

## 3. Relevant Architecture

[verified] `main/app/app_loop.c:772-786` is the shared 500 ms scheduler for transfer polling, automatic OTA and background time sync; `main/app/app_loop.c:1120-1125` coordinates Wi-Fi/BLE memory ownership. This is the correct integration point for a bearer coordinator.

[inferred] Use four layers:

1. **SKU capability** in `components/board_support`: compile-time `WIFI` or `4G` variant, modem pin/power description, and immutable `has_cellular` capability. A runtime modem state may say `ABSENT`, `SIM_MISSING`, `REGISTERING` or `READY`, but it must not grant capability to a Wi-Fi build.
2. **Cellular backend** in a new `components/cellular_modem`: isolate the confirmed modem transport (PPP through `esp_modem`, or Ethernet/RNDIS/AT through the confirmed board component), SIM/registration/signal/IMEI/ICCID, AT configuration and power/sleep lifecycle.
3. **`main/services/network_service`**: own independent Wi-Fi/cellular enabled and ready states, the persisted business-route policy, external-network lease arbitration, default route/DNS readiness, cancellation, sleep pause/resume and status snapshots. Keep `wifi_transfer` as the local transfer owner, adapting its STA network-only path behind this service instead of exposing Wi-Fi checks to business code.
4. **Consumers and UI**: weather, time, OTA and the quick layer call the generic service. Settings expose both radio switches and the business route; local transfer explicitly requests Wi-Fi.

[inferred] Keep Wi-Fi and cellular radios independently switchable on the 4G SKU; the two quick entries control them separately. Serialize external-business leases and default-route ownership without making local Wi-Fi use implicitly disable mobile data. Persist a business route policy (`WIFI`, `CELLULAR` or `AUTO`) and an Auto priority. In a forced mode, an unavailable bearer returns an actionable failure instead of silently consuming the other bearer; only `AUTO` may fall back. Local transfer always requests Wi-Fi. If the confirmed hardware cannot run both radios concurrently, the service may pause one radio for that operation, but must restore its prior enabled state without changing the user's route policy.

[inferred] The vendor checkout contains a useful dual-network reference (`DualNetworkBoard`, `Nt26Board`, `UartEthModem`, and network-mode documentation), but its modem model and pins are not current mogo board facts. Treat `/Users/chopper1026/codespace/Metalio-E-INK4-Plus` as a protocol and UX reference only until the actual 4G board is confirmed.

## 4. GitNexus Findings

[graph] The index was rebuilt with `gitnexus analyze --index-only --pdg --skip-agents-md --skip-skills`; it reported 528 flows but capped/ranked only part of the C/CPP callable candidates and dropped field links. Therefore graph results are retained for freshness/navigation, not for unverified C call edges.

[verified] Source inspection identifies the load-bearing hubs: `wifi_transfer_start/stop/status/service_poll`, `weather_worker/http_get`, `app_time_service_poll`, OTA `online/wait_network/read_chunk`, quick-layer `quick_status/quick_toggle_wifi/app_quick_layer_feed_frame`, quick-menu input/draw, and `ui_nav_status`.

[verified] The direct consumers to migrate are weather, time, online OTA, automatic OTA, quick-layer status/toggle, quick-menu rendering/input, top status rendering, settings Wi-Fi hints, and the 500 ms app-loop scheduler. Existing host coverage is present in `tools/app_loop_host_test.c`, `tools/app_quick_layer_host_test.c`, `tools/ui_quick_input_host_test.c`, `tools/weather_service_host_test.c`, `tools/time_sync_host_test.c`, `tools/time_service_host_test.py`, `tools/ota_online_host_test.c`, `tools/ota_auto_host_test.c`, `tools/transfer_host_test.c` and `tools/transfer_wifi_host_test.c`.

## 5. Statement-Level PDG Findings

[graph] No stable symbol-level PDG slice is used as a load-bearing result. The refreshed index warned about C/CPP field-link and callable-candidate limits, and name-based resolution for the central C functions was incomplete. Current-source control and mutation observations below are therefore `[verified]` source evidence, not claimed PDG edges.

[verified] Stop paths cancel or finish in-flight work before deinitializing Wi-Fi/netif (`wifi_transfer_stop`), and OTA cancels time before acquiring its network. [inferred] The new service must preserve this ordering with lease cancellation and an owner token so a quick toggle cannot tear down another task's bearer.

[verified] The scheduler gates background OTA/time on idle, USB, page, BLE and quick-layer state (`main/app/app_loop.c:772-786`). [inferred] `network_service_poll` should be inserted into that same gate, while OTA/transfer lease ownership remains explicit rather than inferred from page state.

## 6. Proposed Changes

### 6.1 SKU and hardware capability

[verified] Extend `components/board_support/Kconfig` and the board component build with a mutually exclusive Wi-Fi/4G SKU. Add new `board_model` APIs returning variant and immutable capabilities; persisted NVS values can select a preference but cannot enable cellular on the Wi-Fi SKU.

[assumed] Add a board-specific cellular power/pin contract only after the 4G schematic is supplied: modem TX/RX or USB, EN/RESET/DTR/RI, SIM detect, rail enable and sleep behavior. Keep all unknown pins out of defaults so the Wi-Fi build never touches them.

### 6.2 Cellular backend

[inferred] Add `components/cellular_modem/` with a narrow backend API: `init`, `start`, `stop`, `poll`, `get_status`, `send_at`, `pause_for_sleep` and `resume_after_sleep`. The backend owns UART/USB, modem power, registration and SIM state; it exposes an IP-ready netif to `network_service` and never reaches into pages or settings.

[assumed] Select PPP/`esp_modem` or Ethernet/RNDIS/AT only after the modem interface is confirmed. Do not copy the vendor NT26/ML307 pin map into this repository by assumption.

### 6.3 Unified network service

[inferred] Add `main/services/network_service.{h,c}` with `network_capabilities_t`, `network_status_t`, bearer enum (`NONE`, `WIFI`, `CELLULAR`), route enum (`WIFI`, `CELLULAR`, `AUTO`), Auto-priority enum, usage enum (`TIME`, `WEATHER`, `OTA`, `LOCAL_TRANSFER`) and lease ownership. Required behavior:

- `network_service_init/poll/get_status` centralize both radio snapshots, route policy and 500 ms progress.
- `network_service_set_enabled(bearer, enabled)` toggles one radio; an upload/OTA lease rejects a conflicting stop. This never changes the business route selection.
- `network_service_set_route_mode(route, priority)` and `network_service_get_route` persist and report the user-selected external route.
- `network_service_acquire/release` resolves the route: forced Wi-Fi/4G fails closed when that bearer is unavailable; `AUTO` tries the configured priority, cancels lower-priority weather/time when OTA arrives, and returns `internet_ready` independently of transport.
- `network_service_pause_for_sleep/resume_after_sleep` tracks each enabled radio and restores it without duplicate netifs or modem tasks.
- Wi-Fi AP/STA/file upload remains in `wifi_transfer`; its network-only STA path is adapted behind the service. Existing public functions become compatibility wrappers during migration. Local Wi-Fi transfer may coexist with cellular external traffic when the board resource contract permits; otherwise the service performs an explicit pause/resume without changing route preference.

### 6.4 Weather, time and OTA

[verified] `weather_worker`, `app_time_service_poll`, OTA `online/wait_network/read_chunk`, and `pico_ota_auto_poll` are the existing Wi-Fi-specific gates. Replace those gates with a route-aware network lease and `network_service_wait_ready` while retaining their current cancellation, timeout, TLS, Date-header and PMU/NVS semantics. A forced Wi-Fi or forced 4G route reports the selected bearer as unavailable; it must not silently switch. `AUTO` follows the persisted priority and records which bearer was used.

[inferred] The HTTP client code need not know whether the default route is Wi-Fi or cellular. Weather still trusts the HTTPS response Date header; SNTP still records success through the time service; OTA still uses the official HTTPS origins and range-read behavior.

[verified] `pico_release_parse` currently requires exact `PICO_OTA_BOARD` and `PICO_OTA_LAYOUT` (`main/ota/ota_release.c:111-134`; constants are in `main/ota/ota_online.h:15-16`). Change the board identity to SKU-specific values such as `METALIO-EINK4P-WIFI` and `METALIO-EINK4P-4G` (or add an equivalently mandatory variant field), and publish matching manifests/assets. A missing or wrong variant must fail before downloading or flashing.

### 6.5 Settings and persistence

[verified] `app_device_settings.c` currently has a Wi-Fi settings page and Wi-Fi-only automatic-time hint (`main/apps/app_device_settings.c:649-663,931-938`); `settings.c` uses append-only, checksummed backup tails and currently maps versions through `f` (`main/settings/settings.c:1692-1701,1831-1840`).

[inferred] Add a network settings page with independent Wi-Fi and mobile-data enabled switches/status. On the 4G SKU show the route selector `Wi-Fi` / `4G` / `自动`, an Auto priority (recommend Wi-Fi-first to avoid unexpected data charges), and cellular SIM/registration/signal/carrier/reconnect/reset details; expose APN only if the hardware requires user editing. Store non-secret radio and route preferences under a network namespace and append a new checked backup tail. Never put SIM PIN in the ordinary settings backup; erase temporary secrets like the existing Wi-Fi backup path does. Wi-Fi SKU hides mobile-data rows and constrains the route selector to Wi-Fi.

### 6.6 Quick layer and status bar

[verified] `UI_ICON_RADIO_TOWER` already exists in `main/assets/ui_icons.h`; no new generated asset is required for the first UI iteration.

[inferred] Keep the existing Wi-Fi semantic action on every SKU and add `UI_QUICK_CELLULAR` plus `UI_QUICK_CELLULAR_SETTINGS` only on the 4G SKU. The Wi-Fi entry uses the Wi-Fi icon; the mobile entry uses `UI_ICON_RADIO_TOWER`. A tap on either entry calls `network_service_set_enabled` for that radio only and never changes the business route. A 500 ms hold opens that radio's settings page. The 4G menu therefore has one additional capability-derived circle; update layout and hit testing without changing the meaning of Bluetooth, frontlight, rotation or haptics.

[inferred] Change `quick_status` and `ui_quick_menu_draw` to consume a generic network snapshot containing independent Wi-Fi/cellular enabled and ready states plus the selected route and current route. Draw both radio states on the 4G SKU and make the selected route legible without implying that an enabled but unready radio is usable. Change `ui_nav_status` to show the available Wi-Fi and/or radio-tower marks beside Bluetooth. Disabled, SIM-missing and registration-error states need distinct text/status feedback so a tap is not mistaken for a successful connection.

### 6.7 Build and integration

[verified] `main/CMakeLists.txt:66-160` explicitly lists main sources, and `components/wifi_transfer/CMakeLists.txt:15-18` owns its dependencies. Add the new service/backend sources and dependencies there; initialize the model and network service from `app_main`, and call `network_service_poll` from the existing app-loop service gate. Add both SKU defaults and ensure a Wi-Fi build does not link or initialize the modem backend.

## 7. Implementation Sequence

1. Confirm the 4G board schematic, modem model/protocol, pins, power budget, SIM behavior, SKU packaging and OTA feed contract. Stop here if any hardware item is unknown.
2. Add SKU Kconfig/build selection and `board_model` capability APIs; build both variants with the modem backend disabled/enabled as appropriate. Add no UI yet.
3. Implement and host-test the cellular backend with mocked AT/IP/SIM/registration transitions; verify sleep, reset and error cleanup.
4. Extract/adapt Wi-Fi STA acquisition from `wifi_transfer` behind `network_service`; preserve AP/upload behavior and BLE memory reservation while keeping cellular independently switchable. Add route-policy, lease arbitration and cancellation tests.
5. Migrate weather, time and OTA to route-aware leases; test forced Wi-Fi, forced 4G and Auto fallback while retaining existing timeouts, cancellation, TLS origin validation, OTA range reads and PMU handoff. Add SKU-aware manifest parsing before any network download.
6. Add NVS/settings/backup migration with independent Wi-Fi/mobile switches, route mode and Auto priority. Validate old backups, invalid fields, secret erasure and Wi-Fi-SKU hiding.
7. Keep the Wi-Fi quick entry, add the 4G mobile-network entry on the 4G SKU, update capability-derived layout and the top status bar; add separate tap/hold/UI raster regressions.
8. Run host tests and `python3 tools/check_architecture.py`, then build both S31 SKU variants with `idf.py --preview build`. Only after the user authorizes hardware work, run the 4G board matrix; do not flash as part of this design.
9. Update OTA/platform manifests and release artifacts only in a separately authorized release task; keep Wi-Fi and 4G binaries, board IDs, sizes and checksums distinct.

## 8. Test Strategy

[verified] Reuse the existing host runners `./tools/run_app_loop_host_tests.sh`, `./tools/run_ota_host_tests.sh`, `./tools/run_time_sync_host_test.sh`, `python3 tools/weather_host_test.py`, `python3 tools/time_service_host_test.py` and `python3 tools/check_architecture.py`.

[inferred] Add a network-service host suite covering: Wi-Fi SKU rejecting cellular settings; 4G SKU independent Wi-Fi/cellular toggles; forced-route no-fallback behavior; Auto priority and fallback; both radios enabled; OTA preempting weather/time; upload/OTA refusing a conflicting toggle; cancellation ownership; sleep pause/resume; and no duplicate netif/modem start.

[inferred] Extend quick-layer/input tests for separate Wi-Fi and mobile entries, independent taps, two long-press settings dispatches, capability-derived hit boxes, movement/multitouch cancellation and unavailable/SIM-missing feedback. Extend weather/time/OTA tests with forced Wi-Fi, forced 4G and Auto routes, no-bearer failures, no silent forced fallback, cancellation and wrong-SKU manifest rejection. Extend transfer tests to prove cellular never starts an AP, local Wi-Fi transfer does not mutate route preference, and any hardware-required radio pause restores the prior enabled state.

[assumed] Hardware acceptance requires two physical boards: Wi-Fi SKU boot/transfer/OTA regression; 4G SKU no-SIM, SIM-present, registration, weak-signal, HTTPS, Date-header time, weather, OTA download/cancel, local Wi-Fi transfer, sleep/wake and modem power-peak checks. A host build cannot establish these physical results.

## 9. Risk and Impact Analysis

- [assumed] **Hardware risk:** exact modem protocol, pins, SIM wiring and rail current are unknown. A wrong GPIO or transport can prevent boot or brown out the board.
- [inferred] **Concurrency risk:** weather/time/OTA and quick toggles currently rely on Wi-Fi ownership side effects; two radios may also compete for default-route, heap and power resources. Lease tokens, explicit route ownership and cancel-before-stop ordering are mandatory.
- [inferred] **OTA risk:** a shared `METALIO-EINK4P` identity can accept the wrong SKU. Separate board identities and platform manifests are required before release.
- [inferred] **Power/memory risk:** cellular attach peaks and TLS/modem buffers compete with BLE and PSRAM/heap; the existing BLE network reservation must be integrated with bearer leases.
- [inferred] **Persistence risk:** old settings backups must remain readable; APN/SIM PIN handling must not expand plaintext secret exposure.
- [inferred] **UX risk:** “enabled”, “registered”, “internet ready” and “selected route” are distinct. Quick-layer feedback must show each radio's state without blocking the display loop, and forced-route failures must explain how to switch.
- [inferred] **Cost risk:** Auto fallback can consume cellular data after Wi-Fi loss; its priority must be visible and forced modes must fail closed to avoid surprise charges.
- [verified] **Scope risk:** the current board, CMake and OTA sources have no cellular implementation; this is an architecture change, not a local Wi-Fi patch.

## 10. Files Expected to Change

| File | Symbols | Reason |
| --- | --- | --- |
| `components/board_support/Kconfig`, `components/board_support/include/board_model.h`, `components/board_support/board_model.c` | new SKU/capability APIs | Build-time board identity and immutable capability |
| `components/board_support/*` board power files | new cellular rail/pin hooks | Safe modem power and sleep, after schematic confirmation |
| `components/cellular_modem/*` | new modem backend | UART/USB, AT, SIM, registration, IP-ready netif |
| `main/services/network_service.{h,c}` | new bearer/lease service | Independent radio state, route policy and one API for external business |
| `components/wifi_transfer/*` | existing lifecycle/time wrappers | Keep local transfer, adapt the Wi-Fi STA backend and preserve independent cellular state |
| `main/services/weather_service.c`, `main/time/time_service.c` | existing workers/poll | Use network leases over Wi-Fi or 4G |
| `main/ota/ota_online.c`, `main/ota/ota_auto.c`, `main/ota/ota_release.c`, `main/ota/ota_online.h` | online/auto/parser/constants | Cellular OTA and SKU-safe manifest validation |
| `main/settings/settings.{h,c}`, `main/apps/app_device_settings.c` | settings pages, backup version | Independent radio switches, route mode/priority, 4G details and migration |
| `main/app/app_quick_layer.c`, `main/ui/ui_quick_menu.{h,c}`, `main/ui/ui_quick_menu_input.c`, `main/ui/ui_nav.c` | network actions/status/draw | Keep Wi-Fi entry, add capability-derived mobile entry, independent tap/hold behavior and dual status icons |
| `main/app_main.c`, `main/app/app_loop.c`, `main/CMakeLists.txt`, component CMake files | init/poll/dependencies | Wire the service and SKU builds |
| `tools/network_service_host_test.c` and existing host tests | new/updated scenarios | Host regression and policy coverage |
| `docs/development/guides/NETWORK.md` (new) | product/maintenance contract | Document SKU, bearer, transfer boundary and failure states |

## 11. Reusable Implementation Context

```json
{
  "implementation_context": {
    "task_summary": "为 mogo E4P 同时支持 Wi-Fi 版与 4G 版建立板型能力、统一网络承载、双网络快捷入口和可选联网路径；4G 版可用 Wi-Fi 或 4G 完成日期对时、天气和 OTA，联网方式由用户设置。",
    "acceptance_criteria": [
      "构建时能明确区分 Wi-Fi SKU 与 4G SKU，Wi-Fi SKU 的持久化设置不能授予 4G 能力。",
      "4G SKU 同时保留 Wi-Fi 与 4G，二者可分别开关；天气、对时和在线 OTA 通过统一服务按 Wi-Fi、4G 或 Auto 路由，业务层不再直接判断 Wi-Fi 状态。",
      "快捷栏在所有 SKU 保留 Wi-Fi 入口；4G SKU 增加独立的移动网络入口。每个入口单击只切换自己的无线能力，长按进入对应设置；单击不改变业务联网方式。",
      "业务联网方式和 Auto 优先级由设置页持久化。强制 Wi-Fi/4G 不可用时明确失败，不静默消耗另一张卡；只有 Auto 按可见优先级回退。",
      "本地 AP/STA 传书仍由 Wi-Fi 负责，4G 不能被误当作本地传书网络，传书不修改用户的外网路由偏好。",
      "错误 SKU 的 OTA 清单和镜像在启动下载前被拒绝；两种 SKU 均有主机测试、构建和真机验收矩阵。"
    ],
    "evidence_provenance": {
      "schema_version": 2,
      "head_commit": "a75f6c40159344dd7a4475d3936d280ed6d2a7d7",
      "generated_plan_path": "docs/plans/2026-11-12-gitnexus-plan-4g-network-bearer.md",
      "global_dirty_digest": {
        "algorithm": "sha256",
        "canonicalization": "gitnexus-evidence-provenance-v2 NUL-framed UTF-8 records",
        "value": "0a9c85780067d9afcd0764f307b60891e3cee927ee11eaeb5ec7826d10fd82cd"
      },
      "cited_path_manifest": [
        {
          "path": "components/board_support/Kconfig",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:9d1906184974c5333bad14acf230a92e866448efb334f2ecf3157824508e3013",
          "index_digest": "sha256:9d1906184974c5333bad14acf230a92e866448efb334f2ecf3157824508e3013",
          "worktree_digest": "sha256:9d1906184974c5333bad14acf230a92e866448efb334f2ecf3157824508e3013",
          "untracked_digest": "absent"
        },
        {
          "path": "components/board_support/board_init.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:2b464a02c795059801efe9846fc60b4ff747f87505a94096c07c85b39845de7f",
          "index_digest": "sha256:2b464a02c795059801efe9846fc60b4ff747f87505a94096c07c85b39845de7f",
          "worktree_digest": "sha256:2b464a02c795059801efe9846fc60b4ff747f87505a94096c07c85b39845de7f",
          "untracked_digest": "absent"
        },
        {
          "path": "components/wifi_transfer/CMakeLists.txt",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:86e135e77f671158abb66a639fddb85ae2cafbf7613c873fc27c7eb1e07807a9",
          "index_digest": "sha256:86e135e77f671158abb66a639fddb85ae2cafbf7613c873fc27c7eb1e07807a9",
          "worktree_digest": "sha256:86e135e77f671158abb66a639fddb85ae2cafbf7613c873fc27c7eb1e07807a9",
          "untracked_digest": "absent"
        },
        {
          "path": "components/wifi_transfer/include/wifi_transfer.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:e0e4dfc8600035f6eabcaa25a438e7223aa8aae4bddadc000433fac06d833bc5",
          "index_digest": "sha256:e0e4dfc8600035f6eabcaa25a438e7223aa8aae4bddadc000433fac06d833bc5",
          "worktree_digest": "sha256:e0e4dfc8600035f6eabcaa25a438e7223aa8aae4bddadc000433fac06d833bc5",
          "untracked_digest": "absent"
        },
        {
          "path": "components/wifi_transfer/transfer_policy.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:70841b75886dbe9132c2aac8ba2151d487443d089d1cc0c0b48cd836d6670623",
          "index_digest": "sha256:70841b75886dbe9132c2aac8ba2151d487443d089d1cc0c0b48cd836d6670623",
          "worktree_digest": "sha256:70841b75886dbe9132c2aac8ba2151d487443d089d1cc0c0b48cd836d6670623",
          "untracked_digest": "absent"
        },
        {
          "path": "components/wifi_transfer/wifi_transfer.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:39f240719cc2872485d846244e3026e71aca8c2dba2a8d99559d2ab3262ba7ab",
          "index_digest": "sha256:39f240719cc2872485d846244e3026e71aca8c2dba2a8d99559d2ab3262ba7ab",
          "worktree_digest": "sha256:39f240719cc2872485d846244e3026e71aca8c2dba2a8d99559d2ab3262ba7ab",
          "untracked_digest": "absent"
        },
        {
          "path": "main/CMakeLists.txt",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:1b5f8c1bf66f1176308687f0b9e0cbeca33f0933658565200d988fba798b3a53",
          "index_digest": "sha256:1b5f8c1bf66f1176308687f0b9e0cbeca33f0933658565200d988fba798b3a53",
          "worktree_digest": "sha256:1b5f8c1bf66f1176308687f0b9e0cbeca33f0933658565200d988fba798b3a53",
          "untracked_digest": "absent"
        },
        {
          "path": "main/app/app_loop.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:13d7e4de2de7a5847c7378967f07282b184c0ba3bb11e040131dc722bf66a720",
          "index_digest": "sha256:13d7e4de2de7a5847c7378967f07282b184c0ba3bb11e040131dc722bf66a720",
          "worktree_digest": "sha256:13d7e4de2de7a5847c7378967f07282b184c0ba3bb11e040131dc722bf66a720",
          "untracked_digest": "absent"
        },
        {
          "path": "main/app/app_quick_layer.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:62c6e9be4fc6878a1a6a398f4992413938796c58112a8f18dc0a3d4ebf0ec769",
          "index_digest": "sha256:62c6e9be4fc6878a1a6a398f4992413938796c58112a8f18dc0a3d4ebf0ec769",
          "worktree_digest": "sha256:62c6e9be4fc6878a1a6a398f4992413938796c58112a8f18dc0a3d4ebf0ec769",
          "untracked_digest": "absent"
        },
        {
          "path": "main/app/app_quick_layer.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:2d0bbe422e3e5140eb2f0f92d9840446e516bf227622a81bec2508550af1a648",
          "index_digest": "sha256:2d0bbe422e3e5140eb2f0f92d9840446e516bf227622a81bec2508550af1a648",
          "worktree_digest": "sha256:2d0bbe422e3e5140eb2f0f92d9840446e516bf227622a81bec2508550af1a648",
          "untracked_digest": "absent"
        },
        {
          "path": "main/app_main.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:235dbd5556da7597e97472b7afe45c0d184ce0a5e0296f043626e4e8eb11fc78",
          "index_digest": "sha256:235dbd5556da7597e97472b7afe45c0d184ce0a5e0296f043626e4e8eb11fc78",
          "worktree_digest": "sha256:235dbd5556da7597e97472b7afe45c0d184ce0a5e0296f043626e4e8eb11fc78",
          "untracked_digest": "absent"
        },
        {
          "path": "main/apps/app_device_settings.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:4e45aee562481e86bb263193fa7a1600900758e91aa6288bea68be3538d5cf96",
          "index_digest": "sha256:4e45aee562481e86bb263193fa7a1600900758e91aa6288bea68be3538d5cf96",
          "worktree_digest": "sha256:4e45aee562481e86bb263193fa7a1600900758e91aa6288bea68be3538d5cf96",
          "untracked_digest": "absent"
        },
        {
          "path": "main/assets/ui_icons.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:34c093ab77771d80bc1e73227bae9029dd14aa343b2d693f548804526c261a62",
          "index_digest": "sha256:34c093ab77771d80bc1e73227bae9029dd14aa343b2d693f548804526c261a62",
          "worktree_digest": "sha256:34c093ab77771d80bc1e73227bae9029dd14aa343b2d693f548804526c261a62",
          "untracked_digest": "absent"
        },
        {
          "path": "main/ota/ota_auto.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:64895f2281d93556c7d7bbbf863ba45979bfd98863a98d62da6cbf3e24668e62",
          "index_digest": "sha256:64895f2281d93556c7d7bbbf863ba45979bfd98863a98d62da6cbf3e24668e62",
          "worktree_digest": "sha256:64895f2281d93556c7d7bbbf863ba45979bfd98863a98d62da6cbf3e24668e62",
          "untracked_digest": "absent"
        },
        {
          "path": "main/ota/ota_online.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:65f2838bb26bd417a10a646738c3eb8df30aaa0f0528a9c82751cd0a7031de1f",
          "index_digest": "sha256:65f2838bb26bd417a10a646738c3eb8df30aaa0f0528a9c82751cd0a7031de1f",
          "worktree_digest": "sha256:65f2838bb26bd417a10a646738c3eb8df30aaa0f0528a9c82751cd0a7031de1f",
          "untracked_digest": "absent"
        },
        {
          "path": "main/ota/ota_online.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:3ae475dbb2bc68333e3d9f3632ecb33727a0334fecf10a6228672b1633171305",
          "index_digest": "sha256:3ae475dbb2bc68333e3d9f3632ecb33727a0334fecf10a6228672b1633171305",
          "worktree_digest": "sha256:3ae475dbb2bc68333e3d9f3632ecb33727a0334fecf10a6228672b1633171305",
          "untracked_digest": "absent"
        },
        {
          "path": "main/ota/ota_release.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:6aeecbca1e5a8eafed0c93f7560979ebd1b911277f01fb5e72c65674a101c0eb",
          "index_digest": "sha256:6aeecbca1e5a8eafed0c93f7560979ebd1b911277f01fb5e72c65674a101c0eb",
          "worktree_digest": "sha256:6aeecbca1e5a8eafed0c93f7560979ebd1b911277f01fb5e72c65674a101c0eb",
          "untracked_digest": "absent"
        },
        {
          "path": "main/services/weather_service.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:29e4da5b3ffb1612bf0ed4796d23146714fd25ce3cb880319d4c5c07cfdf8676",
          "index_digest": "sha256:29e4da5b3ffb1612bf0ed4796d23146714fd25ce3cb880319d4c5c07cfdf8676",
          "worktree_digest": "sha256:29e4da5b3ffb1612bf0ed4796d23146714fd25ce3cb880319d4c5c07cfdf8676",
          "untracked_digest": "absent"
        },
        {
          "path": "main/settings/settings.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:7b4a6e05b9a6df72502b056d5cb4ba299724e1791bb4e72407ebe604c2f75f8c",
          "index_digest": "sha256:7b4a6e05b9a6df72502b056d5cb4ba299724e1791bb4e72407ebe604c2f75f8c",
          "worktree_digest": "sha256:7b4a6e05b9a6df72502b056d5cb4ba299724e1791bb4e72407ebe604c2f75f8c",
          "untracked_digest": "absent"
        },
        {
          "path": "main/settings/settings.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:02ec1eeae8a0c9adad1618a68450eb5ed134b38733ed760608af1d2f74e70338",
          "index_digest": "sha256:02ec1eeae8a0c9adad1618a68450eb5ed134b38733ed760608af1d2f74e70338",
          "worktree_digest": "sha256:02ec1eeae8a0c9adad1618a68450eb5ed134b38733ed760608af1d2f74e70338",
          "untracked_digest": "absent"
        },
        {
          "path": "main/time/time_service.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:863cc9972d2b82c39ae334920756b75d0dfd4affdfc9cfbd4e5d763d9190472d",
          "index_digest": "sha256:863cc9972d2b82c39ae334920756b75d0dfd4affdfc9cfbd4e5d763d9190472d",
          "worktree_digest": "sha256:863cc9972d2b82c39ae334920756b75d0dfd4affdfc9cfbd4e5d763d9190472d",
          "untracked_digest": "absent"
        },
        {
          "path": "main/ui/ui_nav.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:0d6977cabff83b382b49fe3b511b099fe79c21f54e27bf672953da05bf30285e",
          "index_digest": "sha256:0d6977cabff83b382b49fe3b511b099fe79c21f54e27bf672953da05bf30285e",
          "worktree_digest": "sha256:0d6977cabff83b382b49fe3b511b099fe79c21f54e27bf672953da05bf30285e",
          "untracked_digest": "absent"
        },
        {
          "path": "main/ui/ui_quick_menu.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:02c79c429e43c506dd6a76e82da2f068b9110bb4e2232bab4b34c64e11c53920",
          "index_digest": "sha256:02c79c429e43c506dd6a76e82da2f068b9110bb4e2232bab4b34c64e11c53920",
          "worktree_digest": "sha256:02c79c429e43c506dd6a76e82da2f068b9110bb4e2232bab4b34c64e11c53920",
          "untracked_digest": "absent"
        },
        {
          "path": "main/ui/ui_quick_menu.h",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:4fd34bac73b0e008dac835e1b37050b26bd0759b71135128c3224532420876b3",
          "index_digest": "sha256:4fd34bac73b0e008dac835e1b37050b26bd0759b71135128c3224532420876b3",
          "worktree_digest": "sha256:4fd34bac73b0e008dac835e1b37050b26bd0759b71135128c3224532420876b3",
          "untracked_digest": "absent"
        },
        {
          "path": "main/ui/ui_quick_menu_input.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:26fa80ad835d31f152be387c4a34acfb58f123dc3493025f5329a22f71b8d0c6",
          "index_digest": "sha256:26fa80ad835d31f152be387c4a34acfb58f123dc3493025f5329a22f71b8d0c6",
          "worktree_digest": "sha256:26fa80ad835d31f152be387c4a34acfb58f123dc3493025f5329a22f71b8d0c6",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/app_loop_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:07a7607d595e1e23e73775729637b9c76675bb1c9026c7b226fd33ea0169f46c",
          "index_digest": "sha256:07a7607d595e1e23e73775729637b9c76675bb1c9026c7b226fd33ea0169f46c",
          "worktree_digest": "sha256:07a7607d595e1e23e73775729637b9c76675bb1c9026c7b226fd33ea0169f46c",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/app_quick_layer_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:6386e8e96454b37cc4bfadca675f32848397a36affd4b755b7610699d9569ed5",
          "index_digest": "sha256:6386e8e96454b37cc4bfadca675f32848397a36affd4b755b7610699d9569ed5",
          "worktree_digest": "sha256:6386e8e96454b37cc4bfadca675f32848397a36affd4b755b7610699d9569ed5",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/check_architecture.py",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:b433fe16cd39ec5486f35ae7d084a136a77f36a05d2e517caa47ab3eb41d9d42",
          "index_digest": "sha256:b433fe16cd39ec5486f35ae7d084a136a77f36a05d2e517caa47ab3eb41d9d42",
          "worktree_digest": "sha256:b433fe16cd39ec5486f35ae7d084a136a77f36a05d2e517caa47ab3eb41d9d42",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/ota_auto_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:38935e19f3fd8f586b61c8d8b35eaa8918e558bcf8c673c29cf11b4fb3d807c2",
          "index_digest": "sha256:38935e19f3fd8f586b61c8d8b35eaa8918e558bcf8c673c29cf11b4fb3d807c2",
          "worktree_digest": "sha256:38935e19f3fd8f586b61c8d8b35eaa8918e558bcf8c673c29cf11b4fb3d807c2",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/ota_online_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:989986a22108875670806b7949c5e853c8ba9adf3267e4e0bf4ed85f4d72eb98",
          "index_digest": "sha256:989986a22108875670806b7949c5e853c8ba9adf3267e4e0bf4ed85f4d72eb98",
          "worktree_digest": "sha256:989986a22108875670806b7949c5e853c8ba9adf3267e4e0bf4ed85f4d72eb98",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/ota_release_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:d4596534080e7474312ff3a526e83d39ffa54bcf32eaca7b973a283461a77324",
          "index_digest": "sha256:d4596534080e7474312ff3a526e83d39ffa54bcf32eaca7b973a283461a77324",
          "worktree_digest": "sha256:d4596534080e7474312ff3a526e83d39ffa54bcf32eaca7b973a283461a77324",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/run_app_loop_host_tests.sh",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:3e9a0f7106b634d73b90eccc14a876dc932b6a62daa254ffab054fd6376f8f25",
          "index_digest": "sha256:3e9a0f7106b634d73b90eccc14a876dc932b6a62daa254ffab054fd6376f8f25",
          "worktree_digest": "sha256:3e9a0f7106b634d73b90eccc14a876dc932b6a62daa254ffab054fd6376f8f25",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/run_ota_host_tests.sh",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:b25ceb2e63626beaaaabcefa9dc32d63c000f3da4fb2786795300eaf0001a822",
          "index_digest": "sha256:b25ceb2e63626beaaaabcefa9dc32d63c000f3da4fb2786795300eaf0001a822",
          "worktree_digest": "sha256:b25ceb2e63626beaaaabcefa9dc32d63c000f3da4fb2786795300eaf0001a822",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/run_time_sync_host_test.sh",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:c59f16f5f023d78af18a6e63cd5a3f3d46e7877ab89d1941e68db5c8ee808bd8",
          "index_digest": "sha256:c59f16f5f023d78af18a6e63cd5a3f3d46e7877ab89d1941e68db5c8ee808bd8",
          "worktree_digest": "sha256:c59f16f5f023d78af18a6e63cd5a3f3d46e7877ab89d1941e68db5c8ee808bd8",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/time_service_host_test.py",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:0edac8ab053a0cc7bdf20e9e22703385f2169be51df4f69b4a8760225af52842",
          "index_digest": "sha256:0edac8ab053a0cc7bdf20e9e22703385f2169be51df4f69b4a8760225af52842",
          "worktree_digest": "sha256:0edac8ab053a0cc7bdf20e9e22703385f2169be51df4f69b4a8760225af52842",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/time_sync_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:9fc45cf27724f0f686ec9aa89058093f153abb20965c1af87aa6fa2659e60465",
          "index_digest": "sha256:9fc45cf27724f0f686ec9aa89058093f153abb20965c1af87aa6fa2659e60465",
          "worktree_digest": "sha256:9fc45cf27724f0f686ec9aa89058093f153abb20965c1af87aa6fa2659e60465",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/transfer_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:fa2d14d5369e4f4fe3997f7a411e71eaf2c80cd0524a9013c07add3848a0e250",
          "index_digest": "sha256:fa2d14d5369e4f4fe3997f7a411e71eaf2c80cd0524a9013c07add3848a0e250",
          "worktree_digest": "sha256:fa2d14d5369e4f4fe3997f7a411e71eaf2c80cd0524a9013c07add3848a0e250",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/transfer_wifi_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:91c277481673dd17412793e466dfcc7e04b403d763854fc8847b9a985f8b0006",
          "index_digest": "sha256:91c277481673dd17412793e466dfcc7e04b403d763854fc8847b9a985f8b0006",
          "worktree_digest": "sha256:91c277481673dd17412793e466dfcc7e04b403d763854fc8847b9a985f8b0006",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/ui_quick_input_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:55793a570ea6f296cd52cd5a73d117f5843b4144b63db7fae24aa2a1fa0f3f34",
          "index_digest": "sha256:55793a570ea6f296cd52cd5a73d117f5843b4144b63db7fae24aa2a1fa0f3f34",
          "worktree_digest": "sha256:55793a570ea6f296cd52cd5a73d117f5843b4144b63db7fae24aa2a1fa0f3f34",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/weather_host_test.py",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:5b158e444c1d321a3479a37f0828c1d901ac5b5c819ecea8786c9d51bdb6b677",
          "index_digest": "sha256:5b158e444c1d321a3479a37f0828c1d901ac5b5c819ecea8786c9d51bdb6b677",
          "worktree_digest": "sha256:5b158e444c1d321a3479a37f0828c1d901ac5b5c819ecea8786c9d51bdb6b677",
          "untracked_digest": "absent"
        },
        {
          "path": "tools/weather_service_host_test.c",
          "object_kind": {
            "head": "regular",
            "index": "regular",
            "worktree": "regular",
            "untracked": "absent"
          },
          "state": "clean",
          "rename_from": null,
          "rename_to": null,
          "head_digest": "sha256:f2ba6fdb218d46e3db6427ea2d35f4c2a79f0991eebe043af18c2303f2af9942",
          "index_digest": "sha256:f2ba6fdb218d46e3db6427ea2d35f4c2a79f0991eebe043af18c2303f2af9942",
          "worktree_digest": "sha256:f2ba6fdb218d46e3db6427ea2d35f4c2a79f0991eebe043af18c2303f2af9942",
          "untracked_digest": "absent"
        }
      ]
    },
    "primary_symbols": [
      {
        "symbol": "wifi_transfer_start / wifi_transfer_stop",
        "file": "components/wifi_transfer/wifi_transfer.c",
        "lines": "2046-2145",
        "role": "现有 Wi-Fi 生命周期和内存/蓝牙占用边界"
      },
      {
        "symbol": "weather_worker / http_get",
        "file": "main/services/weather_service.c",
        "lines": "171-228,271-347",
        "role": "天气获取的联网入口和临时 Wi-Fi 所有权"
      },
      {
        "symbol": "app_time_service_poll / app_time_service_sync_manual",
        "file": "main/time/time_service.c",
        "lines": "82-175",
        "role": "自动与手动对时的 Wi-Fi 依赖"
      },
      {
        "symbol": "online / wait_network / read_chunk",
        "file": "main/ota/ota_online.c",
        "lines": "128-168,218-238",
        "role": "在线 OTA 网络等待、读取和取消"
      },
      {
        "symbol": "app_quick_layer_feed_frame / quick_toggle_wifi",
        "file": "main/app/app_quick_layer.c",
        "lines": "66-72,174-190,275-325",
        "role": "快捷栏状态、单击切换和长按入口"
      },
      {
        "symbol": "ui_quick_menu_feed / ui_quick_menu_draw",
        "file": "main/ui/ui_quick_menu_input.c; main/ui/ui_quick_menu.c",
        "lines": "195-214,279-284; 46,239-258",
        "role": "快捷栏动作枚举、长按判定和图标绘制"
      },
      {
        "symbol": "ui_nav_status",
        "file": "main/ui/ui_nav.c",
        "lines": "230-261",
        "role": "顶部网络状态图标"
      },
      {
        "symbol": "pico_release_parse",
        "file": "main/ota/ota_release.c",
        "lines": "111-134",
        "role": "OTA board/layout 清单校验"
      }
    ],
    "related_symbols": [
      {
        "symbol": "wifi_transfer_service_poll",
        "relationship": "CALLED_BY",
        "relevance": "主循环每 500ms 驱动现有 Wi-Fi 状态机"
      },
      {
        "symbol": "pico_ota_auto_poll",
        "relationship": "CALLED_BY",
        "relevance": "后台 OTA 准入目前只看 Wi-Fi STA"
      },
      {
        "symbol": "app_weather_cancel_join",
        "relationship": "CALLED_BY",
        "relevance": "快捷栏切换网络前取消天气任务"
      },
      {
        "symbol": "app_settings_backup_save/restore",
        "relationship": "PERSISTS",
        "relevance": "新增网络设置必须追加兼容尾块"
      },
      {
        "symbol": "UI_ICON_RADIO_TOWER",
        "relationship": "AVAILABLE_ASSET",
        "relevance": "已有 4G/蜂窝网络图标，无需立即生成新资源"
      }
    ],
    "execution_path": [
      "启动时加载构建时 board SKU、独立 Wi-Fi/4G 开关和路由设置；能力位决定是否实例化 cellular backend。",
      "network_service 维护两个无线能力的 enabled/ready 状态、业务路由策略和外网 lease；不把“只能一个 radio”作为默认产品行为。",
      "天气、对时、OTA 申请路由感知的网络 lease；强制模式只用用户选定 bearer，Auto 按优先级选择 ready bearer，完成或取消后释放。",
      "快捷栏读取两个 radio 的状态；Wi-Fi SKU 显示 Wi-Fi 入口，4G SKU 额外显示移动网络入口；单击分别开关，长按分别进入设置。",
      "本地传书明确请求 Wi-Fi；睡眠、上传、OTA 和页面切换通过 network_service 统一暂停、恢复和取消，同时保留路由偏好。"
    ],
    "pdg_constraints": [],
    "architectural_patterns": [
      {
        "pattern": "主循环集中轮询后台服务",
        "example_location": "main/app/app_loop.c:772-786",
        "usage_guidance": "network_service_poll 放在现有 500ms 服务门控内，业务任务不得自建永久轮询"
      },
      {
        "pattern": "资源所有权和取消先行",
        "example_location": "main/ota/ota_online.c:128-149; components/wifi_transfer/wifi_transfer.c:2046-2077",
        "usage_guidance": "切换或停止前先取消天气/对时/OTA，再释放对应 lease；独立 radio 的暂停不能改写业务路由偏好"
      },
      {
        "pattern": "设置备份追加独立校验尾块",
        "example_location": "main/settings/settings.c:1692-1701,2050-2105",
        "usage_guidance": "4G 非秘密偏好采用新版本尾块；旧备份按默认值加载，SIM PIN 不进入备份"
      },
      {
        "pattern": "OTA 清单严格绑定 board/layout",
        "example_location": "main/ota/ota_release.c:111-134",
        "usage_guidance": "新增 SKU 绑定必须在下载前完成，不能靠下载后启动失败兜底"
      }
    ],
    "files_to_modify": [
      {
        "file": "components/board_support/Kconfig",
        "symbols": [
          "new CONFIG_MOGO_E4P_VARIANT_* options"
        ],
        "intended_change": "声明 Wi-Fi/4G SKU 与已确认的模组引脚/电源配置"
      },
      {
        "file": "components/board_support/include/board_model.h; components/board_support/board_model.c",
        "symbols": [
          "new board_model_get/board_capabilities"
        ],
        "intended_change": "提供不可由 NVS 伪造的编译时能力与运行时板型查询"
      },
      {
        "file": "components/cellular_modem/*",
        "symbols": [
          "new cellular_modem_* backend"
        ],
        "intended_change": "隔离 UART/PPP 或 Ethernet/RNDIS、SIM、注册、信号和电源生命周期"
      },
      {
        "file": "main/services/network_service.{h,c}",
        "symbols": [
          "new network_service_* API"
        ],
        "intended_change": "统一 Wi-Fi/4G 独立状态、路由策略、lease、取消、睡眠和默认路由"
      },
      {
        "file": "components/wifi_transfer/*",
        "symbols": [
          "wifi_transfer_start/stop/status/time APIs"
        ],
        "intended_change": "保留传书 API，抽出可被 network_service 调用的 Wi-Fi 外网 backend；旧 API 逐步成为兼容层且不改变 4G 开关"
      },
      {
        "file": "main/services/weather_service.c; main/time/time_service.c; main/ota/ota_online.c; main/ota/ota_auto.c",
        "symbols": [
          "weather_worker",
          "app_time_service_poll",
          "online",
          "wait_network",
          "pico_ota_auto_poll"
        ],
        "intended_change": "改为申请统一网络 lease，不直接读取 Wi-Fi 状态"
      },
      {
        "file": "main/app/app_quick_layer.c; main/ui/ui_quick_menu.{h,c}; main/ui/ui_quick_menu_input.c; main/ui/ui_nav.c",
        "symbols": [
          "quick_status",
          "quick_toggle_wifi",
          "ui_quick_menu_feed",
          "ui_quick_menu_draw",
          "ui_nav_status"
        ],
        "intended_change": "保留 Wi-Fi 快捷键，4G SKU 增加独立移动网络键；单击分别开关，长按分别进入设置，显示双网络状态"
      },
      {
        "file": "main/apps/app_device_settings.c; main/settings/settings.{h,c}",
        "symbols": [
          "settings_page_t",
          "app_device_settings_request_*",
          "backup_version"
        ],
        "intended_change": "新增独立 Wi-Fi/4G 开关、业务路由模式/Auto 优先级、4G 设置子页、NVS 字段和兼容备份尾块"
      },
      {
        "file": "main/ota/ota_online.h; main/ota/ota_release.c",
        "symbols": [
          "PICO_OTA_BOARD",
          "PICO_OTA_LAYOUT",
          "pico_release_parse"
        ],
        "intended_change": "将 OTA 清单绑定到 Wi-Fi/4G SKU；平台随后生成两套正式清单和附件"
      },
      {
        "file": "main/app/app_loop.c; main/app_main.c; main/CMakeLists.txt; components/*/CMakeLists.txt",
        "symbols": [
          "app_loop",
          "app_main",
          "idf_component_register"
        ],
        "intended_change": "初始化、轮询、睡眠恢复和依赖接线"
      }
    ],
    "tests": [
      {
        "file": "tools/network_service_host_test.c",
        "scenarios": [
          "Wi-Fi SKU + 持久化 cellular=true → 能力拒绝 4G，业务仍只可走 Wi-Fi",
          "4G SKU + Wi-Fi 与 4G 分别 enabled → 两个 radio 状态独立且可同时 ready",
          "强制 Wi-Fi/强制 4G 不可用 → 明确失败且不静默改用另一 bearer",
          "Auto 按优先级选择 ready bearer，Wi-Fi 失效时才回退 4G",
          "OTA lease 抢占天气/对时 → 取消旧任务并最终释放，网络键不打断占用中的 bearer",
          "睡眠暂停后恢复 → 不产生双重 modem/Wi-Fi 实例并恢复各自 enabled 状态"
        ]
      },
      {
        "file": "tools/app_quick_layer_host_test.c; tools/ui_quick_input_host_test.c",
        "scenarios": [
          "Wi-Fi SKU 只有 Wi-Fi 网络入口，单击只切换 Wi-Fi",
          "4G SKU 同时有 Wi-Fi 与移动网络入口，两个单击互不改变对方状态",
          "Wi-Fi 和移动网络分别长按进入对应设置，且不改变业务路由模式",
          "新增圆键的 capability-derived hit box、移动/多点/取消不误触发长按"
        ]
      },
      {
        "file": "tools/weather_service_host_test.c; tools/time_sync_host_test.c; tools/ota_online_host_test.c; tools/ota_auto_host_test.c",
        "scenarios": [
          "强制 Wi-Fi、强制 4G 和 Auto 路由均可在对应 ready 条件下完成业务",
          "强制 bearer 无 SIM/无 Wi-Fi 时显示可操作失败，不静默回退",
          "Auto 只在首选 bearer 不 ready 时按优先级回退",
          "取消后不释放别人的 lease，4G OTA 校验 board variant，错误镜像在 HTTP 下载前拒绝"
        ]
      },
      {
        "file": "tools/ota_release_host_test.c; tools/transfer_host_test.c; tools/transfer_wifi_host_test.c",
        "scenarios": [
          "旧清单/错误 SKU 被拒绝",
          "4G 模式不能启动本地 AP/上传服务",
          "本地 Wi-Fi 传书不修改业务路由偏好；若资源合同要求暂停 4G，完成后恢复原 enabled 状态"
        ]
      }
    ],
    "verification_commands": [
      "./tools/run_app_loop_host_tests.sh",
      "./tools/run_ota_host_tests.sh",
      "./tools/run_time_sync_host_test.sh",
      "python3 tools/weather_host_test.py",
      "python3 tools/time_service_host_test.py",
      "python3 tools/check_architecture.py",
      "idf.py --preview set-target esp32s31",
      "idf.py --preview build"
    ],
    "risks": [
      "未确认的模组协议和 GPIO/电源脚不能进入实现；错误猜测可能导致 Wi-Fi 板上电或启动异常。",
      "4G 峰值电流、SIM 注册和 PPP/RNDIS 内存占用可能影响 PMU、电池和 BLE；必须实测双 radio 并行和暂停策略。",
      "两个 radio 同时启用时，天气、对时、OTA、快捷键和传书可能竞争默认路由、句柄和内存；必须用 lease 和明确的 route ownership 仲裁。",
      "Auto 回退可能产生用户未预期的 4G 流量；优先级必须可见，强制模式必须 fail closed。",
      "若 OTA 不区分 SKU，Wi-Fi 版可能安装含 4G GPIO/驱动的镜像；manifest 和构建产物必须成对发布。",
      "APN/SIM PIN 等配置的备份泄露风险高于现有 Wi-Fi 备份；SIM PIN 不应进入普通配置备份。"
    ],
    "assumptions": [
      "当前仓库的板型能力以构建时 SKU 为准；待实现前检查 Kconfig/board files 是否与实板一致。",
      "4G 模组最终会提供可被 esp_http_client/esp_sntp 使用的 IP 默认路由；若是 RNDIS/Ethernet 而非 PPP，只替换 cellular backend。",
      "4G SKU 让 Wi-Fi 和 4G 独立开关，业务路由偏好持久化；推荐首启为 Auto 且 Wi-Fi 优先以避免意外流量，最终默认和双 radio 并行资源限制需确认。"
    ],
    "open_questions": [
      "4G 板的确切型号、模组型号（ML307/EC801E/其他）、UART 或 USB/RNDIS 协议是什么？",
      "模组 TX/RX/EN/DTR/RI/RESET、供电/电源使能、SIM 卡座和天线连接分别是哪几个引脚？",
      "4G 版是否与 Wi-Fi 版共用同一固件/同一 OTA，还是接受两个独立构建和下载入口？",
      "Auto 首选是否确定为 Wi-Fi（推荐），以及强制 Wi-Fi/4G 不可用时是否确认只报错并引导用户手动切换，而不做一次性静默回退？",
      "4G 板能否同时保持 Wi-Fi AP/STA 与蜂窝数据，还是必须暂停其中一个 radio；若需暂停，恢复时是否保留原开关和路由偏好？",
      "APN、SIM PIN、运营商配置是否需要用户编辑，哪些字段允许写入 NVS 或备份？"
    ],
    "avoid": [
      "不要用探测 AT 失败来把 Wi-Fi 版动态判成 4G 能力；能力和运行状态必须分离。",
      "不要让天气、对时、OTA 继续直接读取 wifi_transfer_status_t 或 esp_wifi_*。",
      "不要在 4G SKU 隐藏 Wi-Fi 入口，也不要把 Wi-Fi/4G 两个快捷键合并成一个会改变业务路由的隐式切换键。",
      "不要让强制 Wi-Fi/4G 路由静默回退到另一 bearer；Auto 的回退顺序必须持久化并显示。",
      "不要把 4G 当作本地传书 AP；本地传书必须明确走 Wi-Fi。",
      "不要在未确认模组引脚、协议和电源之前刷写或发布 4G 固件。",
      "不要把当前原厂仓库的 NT26/ML307 引脚和协议当成 mogo E4P 已证实硬件事实。"
    ]
  }
}
```

## 12. Assumptions and Open Questions

### Assumptions to verify

- [assumed] The firmware will use separate Wi-Fi and 4G build artifacts. Verify this against the release and OTA packaging decision before coding.
- [assumed] The 4G SKU persists a user-selected route policy and can keep Wi-Fi and cellular independently enabled. The recommended first-boot policy is `AUTO` with Wi-Fi priority to avoid unexpected data charges; verify the final default and concurrent-radio resource limits.
- [assumed] The modem exposes an IP route usable by the existing HTTPS/SNTP clients. Verify PPP versus Ethernet/RNDIS and the required ESP-IDF component.
- [assumed] APN editing is only needed for some operators. Verify whether the carrier profile can be fixed at build time and whether SIM PIN is required.

### Open questions requiring user or hardware input

1. What is the exact 4G development-board model and modem model? Is it ML307, EC801E or another module?
2. What are the modem UART/USB, EN/RESET/DTR/RI, SIM detect, power-enable and rail details?
3. Should Wi-Fi and 4G use separate firmware/OTA feeds and download files, or must one image run on both boards?
4. Should the first-boot Auto priority be Wi-Fi-first (recommended to avoid data charges), and should a forced Wi-Fi/4G route fail closed when unavailable rather than offer one-shot fallback?
5. Can Wi-Fi AP/STA and cellular remain powered together on the 4G board, or must the resource contract pause one radio while preserving its enabled state and route preference?
6. Which network settings are user-editable, and which secrets must never enter the normal TF/NVS backup?

[assumed] Until these answers arrive, no modem driver, GPIO definition, OTA artifact, flash operation or release action is authorized by this design task.

## 13. Definition of Done

- [ ] Both SKU configurations compile for ESP32-S31 with `idf.py --preview build`; Wi-Fi build contains no active cellular initialization.
- [ ] Capability is build-time/board-defined; a Wi-Fi build rejects cellular settings and a 4G build reports modem/SIM/registration/internet states separately.
- [ ] One network-service lease API powers Wi-Fi and 4G date sync, weather and OTA; no migrated business module directly gates on `wifi_transfer_status_t`.
- [ ] Quick layer keeps Wi-Fi on every SKU, adds a separate mobile-network entry on the 4G SKU, toggles each radio independently on tap, opens its own settings on hold, and leaves route selection to the network settings page.
- [ ] Forced Wi-Fi/4G business routes never silently fall back; Auto follows its visible priority and local Wi-Fi transfer does not mutate that preference.
- [ ] Settings and old backups migrate safely; SIM PIN is excluded from normal backups; invalid network data fails closed.
- [ ] OTA manifests reject the other SKU before download and retain official HTTPS origin/layout checks.
- [ ] Host tests, architecture checks, both builds, and the two-board hardware acceptance matrix pass with results recorded separately.
- [ ] No firmware was flashed, published or tagged as part of this design-only task.
