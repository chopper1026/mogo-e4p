# 简约时钟锁屏 / Minimal clock lock

在「设置 → 锁屏样式 → 简约时钟」选择。24小时制，小时与分钟上下对齐，不显示冒号；使用固定 Chakra Petch Bold 数字，字号不跟随系统字号，下方为月日、星期和固件电量图标，不显示年份与电量百分比。中文沿用系统字体。
Select Settings → Lock style → Clock. Fixed Chakra Petch Bold numerals form aligned hour/minute rows without a colon. Month/day, weekday and the existing battery icon follow below, without a year or percentage; Chinese uses the system face.

「仅浅睡」持续更新；「浅睡后关机」更新到所选10/30/60分钟截止点；「彻底关机」保留关机前最后画面。选择时钟不自动更改待机方式。电源键或已开启的拿起唤醒沿用现有行为，已设密码仍须验证。
Light-only standby keeps updating. Timed standby updates until its 10/30/60-minute deadline; full off retains the last face. Selecting the clock preserves the standby choice and existing key/pickup/PIN behavior.

## 时间与刷新 / Time and refresh

- 按北京时间（UTC+8）显示，支持2024–2099年的有效UTC时间。锁屏入口读取PCF8563 RTC，每30秒只读重取锚点，中间用单调计时外推；RTC失败时使用系统时间，两者无效时显示横线及「请先校准时间」。不联网、不校准系统或RTC。
- 浅睡保留既有2秒定时轮询。分钟或电量/充电状态变化时仅重画中心384×672区域，经统一 `DISPLAY_REFRESH_DETAIL` 灰阶出口同步推屏，完成后关闭EPD供电；分钟边界可延迟一个轮询间隔加采样与绘制时间。
- 仅成功呈现才登记分钟身份；失败等待2秒后重试。显示出口仍执行原有供电检查与故障恢复，故障恢复可能整屏刷新。
- 更新留在同一次浅睡等待中，保留原始关机截止点。刷新前仅恢复IOE方向与P14供电并等待80ms稳定，完成后先关闭EPD供电，再恢复IOE睡眠状态；IOE恢复失败不启动屏幕供电，等待2秒后重试。不恢复触摸、前光或网络。绘屏期间临时开启现有电源键快采，睡前等待在途读取结束并保留本轮事件；先处理用户唤醒，再处理到期和下一次更新。

Beijing time comes from read-only RTC anchors, refreshed every thirty monotonic seconds, with the system clock as fallback. Existing two-second sleep polls detect minute/battery changes and update only the central area through the detail refresh policy. Successful presentation alone commits the identity; failures retry after two seconds. Each refresh restores the IOE directions/P14 rail and waits 80 ms before panel power-on, then powers the panel off and parks the IOE again. A failed IOE restore skips panel power-on and retries after two seconds. Updates retain the same shutdown deadline, sleeping touch/frontlight/radios and key input sampled during an awake display scan.

## 密码与持久化 / PIN and persistence

时钟样式不读写TF卡锁屏帧缓存，也不增加原图整帧副本。密码输入期间暂停时钟更新；取消或输入超时后恢复完整白底与当前时间，重新等待唤醒。唤醒进入密码页前重建当前时间的模糊背景，冷启动也重画时钟，避免旧分钟或旧日期回放。错误密码、锁定等待和验证流程保持原有规则。
Clock art bypasses TF frame caches and an extra original-frame copy. Active PIN input pauses updates; cancel/timeout restores the current white clock face. Wake/cold entry rebuilds the current background; PIN verification and retry blocking retain their existing rules.

主样式值为5，沿用原子NVS键 `lock_face = style*2 + calendar_variant`。v40备份标识为 `PICOSETe`，沿用v39尾块布局，只扩展主样式范围；旧版本不接受5，旧备份仍按原规则恢复。密码不加入普通设置备份。
Style 5 uses the existing atomic lock-face key. Backup v40 (`PICOSETe`) keeps the v39 tail layout and expands only the style range; older formats reject 5, and legacy backups retain their restoration rules. PIN credentials stay outside ordinary backups.

## 维护与验证 / Maintenance and verification

数字原始字体、完整SIL OFL许可和SHA-256在 [clock-font](../../../main/assets/clock-font/README.md)。离线生成 `tools/generate_clock_digits.py` 输出十个224px原尺寸4bpp游程掩码；设备只链接生成数据，不加载该TTF或增大字体引擎上限。
The font source, license and digest are retained alongside the assets. Offline generation produces ten native-size masks; firmware embeds only the generated data.

```sh
python3 tools/generate_clock_digits.py --check
python3 tools/run_reader_baseline.py lock_clock lock_clock_ui lock_style lock sleep_power metalio_drivers power_key settings_backup calendar_settings_ui
python3 tools/lock_clock_ui_host_test.py --output build/clock-native
```

宿主测试覆盖时间/日期切换、RTC失败、重试、电量变化、浅睡输入优先级与截止时间、PIN取消/低内存/冷启动、设置持久化与旧备份，以及实际4bpp数字像素和刷新边界。产品构建与宿主图不能证明实际功耗、面板残影、整夜稳定性或硬件时钟精度，这些仍需设备验收。
Host regressions cover model, sleep/input/deadline, PIN, persistence and native raster behavior. Product builds and host images do not establish measured power, ghosting, overnight stability or hardware-clock accuracy; those require device acceptance.

排查锁屏后异常复位时，可在独立诊断构建开启 `CONFIG_APP_LOCK_SLEEP_TRACE`（默认关闭）。它在RTC内存保留最后阶段、睡眠次数、时钟检查次数及锁屏后第一次复位原因，启动和进入主循环后的10/30/60秒输出 `lock_trace: previous_lock`；后续USB复位不会覆盖已记录的第一次原因，下一轮锁屏才重新开始。阶段3表示准备浅睡，8为面板上电，9为扫描，13为正常退出等待，15/16为MPLL校准前后，其余值见 `main/diagnostics/lock_sleep_trace.h`。此选项只增加内存记录和有界重放，不修改看门狗、刷新、外设或睡眠策略；RTC丢失或断电时记录不保证保留。
For reset diagnosis, the default-off `CONFIG_APP_LOCK_SLEEP_TRACE` retains stages and the first post-lock reset reason in RTC RAM, with bounded boot replays for USB reconnection. It changes no power or watchdog policy; power loss can discard the record. Validate the recorder with `python3 tools/run_reader_baseline.py lock_sleep_trace`.
