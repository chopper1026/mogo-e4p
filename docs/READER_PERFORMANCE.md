# 阅读性能测量 / Reader performance measurement

S31 的阅读诊断由 `CONFIG_READER_PERF` 控制，默认关闭。开启后，每次开书、翻页及字号/边距/行段距重排输出一行 `READER_PERF v=1`。原有刷新策略、分页和文件格式不变。诊断构建会产生计时及串口开销，只能在相同诊断配置间比较。
Reader diagnostics on S31 are controlled by `CONFIG_READER_PERF`, disabled by default. Enabled builds emit one `READER_PERF v=1` record per open, turn or size/margin/spacing reflow. Refresh policy, pagination and formats are unchanged. Timing and serial overhead require comparisons between equivalent diagnostic configurations.

宿主回归入口（保留 ASan/UBSan；结果写入被忽略目录）：
Host regression entry point (retains ASan/UBSan; writes to ignored directories):

```sh
python3 tools/run_reader_baseline.py
python3 tools/run_reader_baseline.py perf prefetch background
python3 tools/run_reader_baseline.py shelf management
python3 tools/run_reader_baseline.py session images book
python3 tools/run_reader_baseline.py controls
python3 tools/run_reader_baseline.py present
```

`display_contract` 在禁止硬件头的条件下单独编译 C/C++ 页面契约，检查不透明显示状态与真实后端类型及布局兼容，并覆盖区域回调在零尺寸、负坐标和整数边界上的无损转换。显示状态仍由装配和显示服务持有；页面只借用，区域裁剪继续由原显示出口处理。
`display_contract` compiles C/C++ page contracts with hardware headers forbidden, checks opaque/native state identity and layout, and verifies lossless callback rectangles at zero sizes, negative coordinates and integer boundaries. Assembly and display services retain state ownership; pages borrow it, and existing display exits retain clipping.

`font_source` 编译真实字体字节源，验证文件游标、短读/读错/EOF、内存边界、整表映射预算与回收。每个字体槽持有自己的文件或内存镜像、glyf/gvar 映射；上层仍在预绘交还后按原顺序清字形、映射、块缓存及字节源。`font_faces` 同时检查真实字体的像素、字宽、文件句柄及重复嵌入字体槽进出；块工作区沿用原有保留策略，不把其常驻占用计作泄漏。
`font_source` compiles the real byte-source module to check cursors, short/error/EOF reads, memory bounds, map budgets and reclamation. Each slot owns its file or memory image and glyf/gvar maps; after prefetch handoff, callers retain the original glyph/map/block/source cleanup order. `font_faces` also checks real-font pixels, advances, descriptors and repeated embedded-slot cycles; retained block workspaces keep their existing policy and are counted as resident resources.

`platform_input` 编译真实 S31 输入适配器与驱动类型，检查完整双点载荷、原始字节、读错及早退快照，并检查页面契约不引入触摸/加速度计驱动头。页面只借用 `app_touch_t` 快照；慢速加载导航通过同步读取回调轮询，设备句柄由主循环和 S31 适配器持有。采样队列、去抖及锁屏顺序不变。`pinpad` 与 `lock` 分别验证密码键盘和包含真实适配器的密码锁循环。
`platform_input` compiles the real S31 adapter and driver types to check complete two-contact payloads, raw bytes, error/early-return snapshots, and a page contract free of touch/accelerometer driver headers. Pages borrow `app_touch_t` snapshots; navigation during long loads polls through a synchronous callback while the loop and S31 adapter retain device handles. Sampler queues, debounce and lock ordering are unchanged. `pinpad` and `lock` cover the PIN pad and authentication loop with the real adapter.

`shelf` 与 `management` 编译真实的书架数据模块，分别覆盖目录/分页记忆及搜索、排序、选择和管理失败重试；保留页面适配层的回归断言。
`shelf` and `management` compile the actual shelf data module, covering folder/page memory and search, ordering, selection and management retries while retaining page-adapter regression assertions.

`session` 使用真实 HTML 解析/释放代码并跟踪分配，检查书源排他与章节交接后回收；`images` 覆盖章节加载及排版失败回退；`book` 还验证会话与真实 TXT/EPUB 文件的联动。这些宿主检查不能替代设备上的预绘交还和资源峰值测量。
`session` tracks allocations through real HTML parsing/cleanup to verify exclusive sources and chapter reclamation; `images` covers chapter-load and layout rollback; `book` also integrates sessions with real TXT/EPUB files. These host checks do not replace device prefetch handoff or peak-resource measurements.

当前书籍的路径、书名、章节标题和文件大小属于 `reader_session.book`，由 UI 在原有开书及章节提交位置更新。关闭书源后会话清空路径和书名；失败候选不覆盖旧章信息。进度和书签仍使用原有路径、文件大小、章节及字节偏移，持久结构与 NVS 键不变。
The path, book/chapter titles and file size belong to `reader_session.book`, updated by the UI at the existing open/chapter commit points. After source close, the session clears path and book title; failed candidates preserve prior chapter metadata. Progress and bookmarks retain their path, file size, chapter and byte-offset fields, persisted layouts and NVS keys.

`reader_present` 同步借用已画好的真实前后帧，接收区域、模式、效果及当前画面状态，调用现有显示出口。页面仍负责绘制、预绘启动与交还、欠载恢复和状态提交；显示服务继续负责波形执行及 OTA 擦写互斥。`present` 宿主组验证区域与波形选择、插图失败短路、正文与页脚错误合并、工具栏归一，以及水波纹成功/失败后的节拍恢复。
`reader_present` synchronously borrows completed real front/back frames and routes regions, modes, effects and frame state through existing display exits. The page retains painting, prefetch launch/handoff, underrun recovery and state commits; display services retain waveform execution and OTA flash exclusion. The `present` host suite verifies region/waveform selection, image failure short-circuits, body/footer error combination, tools normalization and restored ripple pacing after success or failure.

布局仍使用单实例引擎，由拥有书源的会话统一构建和释放。构建入口绑定同一章节的文本、块、字体段和内链；失败候选由页面释放并恢复旧布局，成功候选再转交会话。关闭或释放章节时，先解除布局和尺寸回调的借用，再回收章节。调用方必须先等预绘交还，并串行化字体与布局访问。`book` 使用真实排版引擎重复 100 次，比较原入口与会话入口的分页偏移和绘制文本，并覆盖跨会话拒绝、排版失败回滚、成功换章和重复退出。
Layout retains its singleton engine, built and released by the source-owning session. Builds bind text, blocks, runs and links from one chapter; the page frees failed candidates and restores the previous layout, then transfers successful candidates to the session. Closing or releasing a chapter first detaches layout and dimension-callback borrows. Callers must await prefetch handoff and serialize font/layout access. `book` repeats 100 cycles with the real layout engine, comparing legacy/session page offsets and rendered text, rejecting other sessions and checking failed-build rollback, successful chapter replacement and repeated exit.

`controls` 覆盖全屏/窗口化翻页、跨章边界、字号与间距重排、正文点击、缓存接管后的时钟及缩进长按。当前章和当前页属于 `reader_session.position`；解析候选失败不会替换位置，成功加载与重排仍由 UI 按原顺序提交。进度文件继续使用章节和字节偏移，布局页码不作为持久位置。
`controls` covers full-screen/windowed turns, chapter boundaries, size/spacing reflow, body taps, cached-page clocks and indent holds. The current chapter and page belong to `reader_session.position`; failed parse candidates never replace position, and the UI commits successful loads and reflows in the existing order. Progress files retain chapter and byte offsets rather than layout page numbers.

`prefetch` 编译完整的预绘模块，使用真实线程和受控行边界验证推屏期间借用、停止交还、部分页续画、代次失效、内存/任务创建失败及重复释放。页面负责输入优先级和插图排除；预绘模块在 `end` 返回前借用稳定的字体、章节与布局，调用方随后才能替换资源。
`prefetch` compiles the complete prefetch module with real threads and controlled row boundaries, checking presentation-time borrowing, stop/handoff, partial-page continuation, generation invalidation, allocation/task failures and repeated release. The page owns input priority and illustration exclusions; fonts, chapter and layout remain stable until `end` returns, before callers replace resources.

`storage` 编译真实的 `storage_jobs_quiesce`，覆盖封面先于头像、失败短路和每项独立超时；同时运行 S31 USB 四线总线/模拟开关、退出失败重试、文件页格式化/重挂门控、主机下电恢复，以及真实封面与头像工作队列测试。USB、文件页与睡眠共用这一交还入口；阅读预绘、传输、OTA 和介质状态仍由原调用边界负责。
`storage` compiles the real `storage_jobs_quiesce`, checking cover-before-avatar ordering, failure short-circuiting and independent per-job timeouts. It also runs S31 USB four-bit bus/switch and teardown-retry checks, file-page format/remount guards, host power-down recovery, and real cover/avatar worker tests. USB, files and sleep share this handoff; reader prefetch, transfers, OTA and media state remain owned by their existing caller boundaries.

`clock` 编译真实时间服务，覆盖手动联网成功后的内部时钟写入与回读、部分成功错误和成功登记；PMU 与 NVS 操作仍只在 UI 调度任务执行，无线会话仍由传输组件持有。`time_policy` 保留六小时节奏和重试预算检查，`ota` 覆盖升级清单、下载、取消及显示互斥；这些宿主检查不代表联网或实机校准已通过。
`clock` compiles the real time service, covering internal-clock write/readback after manual network success, partial-success errors and success bookkeeping. PMU and NVS operations remain on the UI scheduling task and radio sessions remain owned by the transfer component. `time_policy` retains six-hour cadence and retry-budget checks; `ota` covers manifests, downloads, cancellation and display exclusion. These host checks do not establish network or device calibration acceptance.

macOS 默认使用可用的 Command Line Tools 编译器及其 SDK；可以显式设置 `CC` 和 `SDKROOT`。入口先验证 sanitizer 编译与运行，失败即停止；不会静默取消 sanitizer。各组日志与 `results.json` 保留实际退出码。
On macOS the runner defaults to an available Command Line Tools compiler and SDK; explicit `CC` and `SDKROOT` overrides are supported. A sanitizer compile/run probe must succeed; sanitizers are never silently removed. Suite logs and `results.json` retain actual exit codes.

`python3 tools/gen_book_fixtures.py` 在 `build/book-fixtures/books/` 生成原创测试书。`河边记事_图文.epub` 每章交替包含 60 段短文和 60 张灰阶 PNG（384×128，十六阶灰阶带及黑白棋盘格），用于补充正文图片混排场景。生成器固定 ZIP 元数据并在 manifest 记录校验值；书源宿主回归验证实际解析后的图片数量、尺寸和资源读取。传入设备后，仍须独立采集并确认显示效果。
`python3 tools/gen_book_fixtures.py` generates original books in `build/book-fixtures/books/`. `河边记事_图文.epub` alternates 60 short paragraphs with 60 grayscale PNGs per chapter (384×128, sixteen gray bands and a black/white checkerboard). Fixed ZIP metadata and manifest checksums support repeatable mixed-page measurements. Source host tests verify parsed image counts, dimensions and resource reads. Device capture and visual acceptance remain separate checks.

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

采集器不发送串口数据，并在打开前禁用 DTR/RTS；但 S31 的 USB 串口打开或重连仍可能触发主机复位。必须检查日志中的启动原因，把重启后的开书与稳定翻页分组；USB 共享退出后的日志不能直接当作纯存储交接记录。
The collector sends no serial data and disables DTR/RTS before opening, but opening or reconnecting the S31 USB console can still reset the host. Inspect boot reasons and separate post-reset opens from stable turns; logs after USB sharing cannot by themselves isolate storage teardown behavior.

采集器默认在断线时停止并返回非零状态，保留已写日志，不自动重新打开串口。USB 共享切换测试应先结束采集，退出共享后再手动开始新的采集文件。只有明确需要跨断线采集时才使用 `--reconnect`；该选项保留重开串口可能干扰设备的限制。`capture` 宿主组使用串口替身覆盖断线停止、显式重连、超时、手动中断与原始字节保存。
By default the collector stops with a nonzero status on disconnect, preserving recorded data without reopening the port. End capture before USB-sharing transition tests and manually start a new log after leaving sharing. Use `--reconnect` only when capture across disconnects is required; reopening can still disturb the device. The `capture` host suite uses serial doubles to cover stop-on-disconnect, explicit reconnection, timeout, interruption and raw-byte preservation.

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

## 资源容量与退出回收 / Resource capacities and exit reclamation

额外开启 `CONFIG_READER_MEMORY`（依赖 `CONFIG_READER_PERF`，默认关闭）才会输出独立的 `READER_MEMORY v=1`。`present` 在推屏、预绘交还及操作计时结束后采样；`book_closed` 在关闭书源并恢复系统字体后采样；`reader_exit` 在退出阅读应用并释放预绘任务后采样。仍有活跃操作计时时跳过资源遍历与输出。`id` 对应最近一次操作代次，多个生命周期记录可能共用同一个 `id`，不能把它当作唯一记录编号。
An additional `CONFIG_READER_MEMORY` flag (depending on `CONFIG_READER_PERF`, disabled by default) emits separate `READER_MEMORY v=1` records. `present` samples after output, prefetch handoff and operation timing finish; `book_closed` follows source close and system-font activation; `reader_exit` follows reader exit and prefetch-task release. Active timing skips resource walks and logging. The `id` refers to the latest operation generation; lifecycle records may share it, so it is not a unique record identifier.

各容量字段读取未释放的自有分配基址，由分配器报告容量；不等于有效内容长度，不含静态数组、借用镜像或布局借用的章节。字形按条目和位图各计一次，共享预算不再累加。`font_blocks`、`font_work`、`font_catalog` 沿用原有常驻策略，关闭书籍后允许非零。`prefetch` 仅含帧，任务存在状态和历史最小剩余栈分别由 `prefetch_worker`、`prefetch_stack_free` 报告；任务控制块与同步对象未纳入容量。栈字段单位为 ESP-IDF 的字节，表示任务生命周期低水位，不是单次操作峰值。
Capacity fields query live owned allocation bases and report allocator capacities rather than content lengths. Static arrays, borrowed images and chapter data borrowed by layout are excluded. Glyph entries and bitmaps count once; the shared budget is not added again. `font_blocks`, `font_work` and `font_catalog` retain their existing resident policy and may remain nonzero after closing a book. `prefetch` includes only its frame; `prefetch_worker` and `prefetch_stack_free` separately report task presence and historical minimum free stack. Task control blocks and synchronization objects are excluded. Stack values use ESP-IDF bytes and reflect task-lifetime lows rather than operation-local peaks.

固定页序的旧新性能对照只开启 `CONFIG_READER_PERF`，关闭 `CONFIG_READER_MEMORY`，将串口日志配置保持一致。资源遍历虽在计时外执行，仍可能改变后续操作的缓存和调度状态，不能把资源诊断轮与纯计时轮混作同一配置。
For fixed-page old/new comparisons, enable only `CONFIG_READER_PERF`, disable `CONFIG_READER_MEMORY`, and keep serial logging settings equivalent. Although resource walks execute outside timing, they may change caches and scheduling for subsequent operations; resource and timing-only runs are different configurations.

此视图覆盖字体、布局、当前会话章节、预绘帧、背景、页内灰阶以及页面已接管的书架数据和封面。尚未按拥有者分解 EPUB 书源内部目录/缓存、排队中的封面结果、系统服务与显示后端分配，不应把已列字段之和解释为整个堆。操作期间峰值仍看 `READER_PERF` 的本地内存低水位；快照只说明采样点的存活容量。
This view covers fonts, layout, the current session chapter, prefetch frame, background, page grayscale, and shelf data/covers already owned by the page. EPUB source directories/caches, queued cover results, system services and display-backend allocations are not decomposed by owner. The listed fields do not total the whole heap. Operation-local heap lows in `READER_PERF` still measure peaks; snapshots show live capacity only at sample points.

```sh
python3 tools/run_reader_baseline.py memory perf font_faces prefetch
python3 tools/summarize_reader_memory.py serial.log --output build/reader-memory.json
```

重复进出应比较相同书籍、字体、位置及背景下的 `reader_exit` 与常驻缓存范围，并结合完整堆空闲量、最大连续块、任务退出后的调度时机判断；汇总工具仅报告首末值和范围，不自动宣称泄漏或已回收全部资源。首次启动后开书与同一位置再次打开需分开采集，并记录重启、字体加载、设置与文件散列；重开书不会主动清除字体缓存，不能称为完全冷缓存。
For repeated entry/exit, compare `reader_exit` and retained-cache ranges with the same book, font, position and background, alongside heap free/largest blocks and scheduling after task deletion. The summary reports first/last values and ranges without claiming leaks or complete reclamation. Capture first open after boot separately from reopening the same position, recording restart, font loading, settings and file hashes. Reopening does not forcibly clear font caches and is not a fully cold-cache run.
