# 阅读性能测量 / Reader performance measurement

S31 的阅读诊断由 `CONFIG_READER_PERF` 控制，默认关闭。开启后，每次开书、翻页及字号/边距/行段距重排输出一行 `READER_PERF v=1`。原有刷新策略、分页和文件格式不变。诊断构建会产生计时及串口开销，只能在相同诊断配置间比较。
Reader diagnostics on S31 are controlled by `CONFIG_READER_PERF`, disabled by default. Enabled builds emit one `READER_PERF v=1` record per open, turn or size/margin/spacing reflow. Refresh policy, pagination and formats are unchanged. Timing and serial overhead require comparisons between equivalent diagnostic configurations.

宿主回归入口（保留 ASan/UBSan；结果写入被忽略目录）：
Host regression entry point (retains ASan/UBSan; writes to ignored directories):

```sh
python3 tools/run_reader_baseline.py
python3 tools/run_reader_baseline.py perf prefetch background
```

macOS 默认使用可用的 Command Line Tools 编译器及其 SDK；可以显式设置 `CC` 和 `SDKROOT`。入口先验证 sanitizer 编译与运行，失败即停止；不会静默取消 sanitizer。各组日志与 `results.json` 保留实际退出码。
On macOS the runner defaults to an available Command Line Tools compiler and SDK; explicit `CC` and `SDKROOT` overrides are supported. A sanitizer compile/run probe must succeed; sanitizers are never silently removed. Suite logs and `results.json` retain actual exit codes.

测量配置应复制现有 S31 `sdkconfig` 到独立构建目录，只在诊断副本启用 `CONFIG_READER_PERF=y`，使用 ESP-IDF 6.1 的 `idf.py --preview` 构建。保留当前版本号，不覆盖正式下载文件。设备写入使用已核对的当前分区与恢复方案；默认 `flash` 命令还包含启动器、分区表及 OTA 数据，不能当作仅更新应用的命令。
Copy the existing S31 `sdkconfig` into an isolated build directory, enabling `CONFIG_READER_PERF=y` only in the diagnostic copy, and build with ESP-IDF 6.1 `idf.py --preview`. Keep the current version and published downloads. Device writes require a verified partition/recovery procedure; the default `flash` command also writes the bootloader, partition table and OTA data.

采集与汇总示例（使用 IDF Python 环境中的 pyserial）：
Capture and summary example (uses pyserial from the IDF Python environment):

```sh
python tools/capture_reader_perf.py --port /dev/cu.usbmodem31101 --seconds 60 \
  --output build/reader-baseline/ordinary-warm.log
python3 tools/summarize_reader_perf.py build/reader-baseline/ordinary-warm.log \
  --scenario ordinary-warm --manifest build/reader-baseline/manifest.json \
  --output build/reader-baseline/ordinary-warm.json
```

`manifest.json` 必须记录 `book_sha256`、`font_sha256`、`firmware_sha256` 和 `settings`；设置应包含字号、字重、行段距、边距、背景、全屏/状态栏、阅读刷新模式及周期全刷页数。另记录提交、配置校验值、书籍章节与起始位置、电量/充电和温度条件。不要混合普通/跨章、冷/热缓存、图片、背景、水波纹或重排场景。每个场景每种操作默认至少 20 个成功样本；不足时汇总退出码为 2。固定测试文件仅保留在本地，不加入仓库。
The manifest must contain `book_sha256`, `font_sha256`, `firmware_sha256` and `settings`; settings should include font size/weight, spacing, margins, background, full-screen/status-bar state, reader refresh mode and cleanup interval. Also record commit, config checksum, chapter/start location, battery/charging and temperature conditions. Keep ordinary/cross-chapter, cold/warm, image, background, ripple and reflow scenarios separate. Each operation needs at least 20 successful samples per scenario by default; insufficient samples return exit code 2. Keep test books/fonts local.

| 字段 / Field | 定义 / Meaning |
| --- | --- |
| `complete` | 推屏成功为 1；失败、无推屏、被新操作替换为 0。/ 1 after successful presentation; 0 on failure, no presentation or replacement. |
| `input_us` | 本轮 UI 调度开始至阅读操作入口；不包含触摸硬件/采样队列等待。/ UI iteration start to reader operation entry; excludes hardware/sampler queue delay. |
| `software_us` | 操作入口至首次推屏准备完成，含同步准备/绘制。/ Operation entry to first display scope, including synchronous preparation/painting. |
| `total_us` | 操作入口至推屏、预绘交还及显示错误恢复之后。/ Operation entry through display, prefetch join and display-error recovery. |
| `dispatch_to_present_us` | 汇总时计算 `input_us + total_us`；不是触点到屏幕的物理延迟。/ Summary computes `input_us + total_us`; not physical touch-to-pixel latency. |
| `action/source/read/parse/layout/paint/display/join/lock_us` | 操作、章节分派、读取/ZIP 解压、HTML 解析、排版、画面合成、推屏、预绘交还及绘制锁等待。嵌套阶段与后台字体时间可能重叠，不能相加当独占耗时。/ Action, chapter dispatch, read/ZIP extraction, HTML parse, layout, composition, display, prefetch join and draw-lock waits. Nested phases and background font work overlap; do not add as exclusive slices. |
| `glyph_read_us/raster_us/glyph_hits/glyph_misses` | 复用现有字体测量计数，可能包含呈现期间的下一页预绘。/ Existing font counters may include next-page prefetch during presentation. |
| `heap_local` | 成功取得操作局部堆监控时为 1；其他监控已占用则为 0，不抢占。/ 1 when operation-local heap monitoring is acquired; 0 when already owned elsewhere. |
| `internal/psram_free/min/largest` | 操作结束空闲量、操作内最低空闲量、结束时最大连续块，单位字节。`heap_local=0` 的低水位无效且不进入汇总。/ End free bytes, operation-local low free bytes, and largest end block. Lows are excluded when `heap_local=0`. |
| `ui_stack_free` | IDF UI 任务栈历史最小余量，单位字节，不是本次操作峰值。/ IDF UI task historical minimum stack headroom in bytes, not this operation's peak. |

汇总按操作报告成功/未完成数量，以及成功样本的最近秩 P50/P95。日志缺字段、负值或版本不符会拒绝汇总。零值只代表该样本未进入相应已埋点路径；图片解码、其他重排入口、独立缓存占用与后台任务栈仍需专项测量。宿主测试、固件构建、设备启动与实际屏幕验收是不同证据，不能互相替代。
Summaries report successful/incomplete counts per operation and nearest-rank P50/P95 for successful samples. Missing fields, negative values and incompatible versions are rejected. Zero means the sample did not enter that instrumented path; image decoding, other reflow entries, individual cache occupancy and worker stack headroom still require dedicated measurement. Host tests, firmware builds, device boot and screen acceptance are separate evidence.
