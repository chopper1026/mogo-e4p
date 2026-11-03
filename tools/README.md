# 开发工具入口 / Development tools

所有命令从仓库根目录执行；生成物放入被忽略的 build 目录。现有脚本路径继续保留，方便 CI 与本地流程复用。
Run commands from the repository root and keep outputs in ignored build directories. Existing script paths remain stable for CI and local workflows.

| 类别 / Category | 入口 / Entry | 用途 / Purpose |
| --- | --- | --- |
| 天气 / Weather | `python3 tools/weather_host_test.py` | 真实解析、缓存、代理任务/取消、NVS交接及原生绘图边界；不联网。/ Real parsing, cache, proxy jobs/cancellation, NVS handoff and native raster bounds, without networking. |
| 天气背景 / Weather masks | `python3 tools/generate_weather_backgrounds.py` | 无损打包已确认的六张1bpp背景；`--check` 核对源图与资源一致。/ Losslessly pack six approved 1bpp backgrounds; `--check` verifies source/asset consistency. |
| 目录与依赖 / Structure | `python3 tools/check_architecture.py` | 检查模块依赖、构建清单和测试入口。/ Check module boundaries, build registration and test entry points. |
| 完整阅读回归 / Reader regression | `python3 tools/run_reader_baseline.py` | 统一执行阅读、字体、显示、存储、OTA 与应用循环回归，保留 ASan/UBSan。/ Run reader/font/display/storage/OTA/app-loop suites with ASan/UBSan. |
| 指定领域 / Selected suites | `python3 tools/run_reader_baseline.py session chapter open turn reflow` | 只执行指定领域；完整列表见 `--help`。/ Run selected domains; see --help for all suites. |
| 传书网页 / Transfer web | `python3 tools/run_reader_baseline.py transfer_web` | Node.js 18+ 执行实际内嵌脚本，验证目录与分页、串行上传/覆盖/断连、壁纸裁剪及签名；不连接设备。/ Node.js 18+ executes the embedded script to check directories, paging, serial uploads, overwrite/disconnection, wallpaper cropping and signatures without hardware. |
| 书架分类 / Shelf categories | `python3 tools/run_reader_baseline.py shelf management navigation` | 链接真实分类状态，检查目录、两种布局页码、菜单手势、分配失败及页面衔接。/ Link real category state to check folders, both layout pages, menu gestures, allocation failures and page integration. |
| 书架封面 / Shelf covers | `python3 tools/run_reader_baseline.py shelf_covers` | 运行真实封面状态、工作队列和页面绘制，检查取消、整组显示、推屏失败、内存上限与缓存复用。/ Run real cover state, worker and page painting; check cancellation, grouped reveal, failed presentation, memory bounds and cache reuse. |
| 阅读设置绘制 / Reader settings view | `python3 tools/run_reader_baseline.py settings_view controls` | 链接真实绘制模块，对照原指令摘要并检查字号适配、命中边界与系统字形。/ Link the real view; compare legacy drawing digests and check scaled bounds, hit edges and UI glyphs. |
| 刷新策略 / Refresh policy | `python3 tools/run_reader_baseline.py refresh present quick navigation` | 统一意图矩阵、23 表目录、调用出口及页面边界；维护契约见 docs/DISPLAY_REFRESH.md。/ Central policy matrix, 23-table catalog, dispatch and page boundaries; see the refresh contract. |
| 图片文件 / Image files | `python3 tools/test_image_files.py` | 实际 JPEG/PNG 流式解码、损坏和大小边界；需要 Pillow、libpng 与 JPEG 依赖源码。/ Real streaming JPEG/PNG, corruption and size bounds; requires Pillow, libpng and JPEG sources. |
| 题头 / Heading | `python3 tools/reader_title_style_host_test.py` | 四种样式、字号、折行、默认画面像素一致性。/ Four styles, sizes, wrapping and historical-default pixel equality. |
| 字体上下文 / Font context | `python3 tools/lock_font_host_test.py` | 系统/阅读字体借用与睡眠恢复。/ System/reader font borrowing and wake restoration. |
| 字体列表 / Font lists | `python3 tools/run_reader_baseline.py font_catalog settings_font` | 大目录扩容、内存不足重试及阅读/系统字体分页选择。/ Large catalogs, allocation-failure retry and reader/system font selection across pages. |
| 蓝牙翻页 / Bluetooth turns | `python3 tools/run_reader_baseline.py ble ble_recent` | 学习保存、锁屏重连后的首按、重复报告及最近设备。/ Learning persistence, first presses after lock reconnection, repeated reports and recent peers. |
| 当前板与电源 / Current board and power | `python3 tools/run_reader_baseline.py metalio_drivers board power_gate battery battery_filter media storage` | 执行 Metalio 板级、电量采样与滤波、无卡座 CD 的存储检测、睡眠恢复及关机交接；不再读取旧 S3/CW32 后端。/ Exercise Metalio board support, battery polling/filtering, card detection without a CD pin, sleep restoration and power-off handoff without the old S3/CW32 backends. |
| 定时待机关机 / Timed standby shutdown | `python3 tools/run_reader_baseline.py metalio_drivers sleep_power settings_backup lock` | 虚拟时钟执行完整 10/30/60 分钟等待，检查提前唤醒、卡片命中、保存恢复、关机失败回退与密码保护；不能替代真机定时和断电验收。/ Run full 10/30/60-minute waits on a virtual clock; check early wake, card hit bounds, persistence, failed-off recovery and PIN protection, not physical timing or power-off acceptance. |
| 资源生成 / Asset generation | `gen_builtin_font.py`、`compress_builtin_images.py`、`gen_ui_icons.py` | 更新对应源资源后再生成，不手改生成表；具体参数见脚本帮助。/ Regenerate after source changes instead of hand-editing tables; see each script's help. |
| 性能采集 / Performance | `capture_reader_perf.py`、`summarize_reader_perf.py`、`summarize_reader_memory.py` | 使用方式与结论边界见 docs/READER_PERFORMANCE.md。/ See the performance guide for usage and interpretation limits. |
| OTA 与包校验 / OTA and bundles | `prepare_platform_submission.py`、`verify_flash_bundle.py` | 准备草稿或校验包；不会代替管理员发布审核。/ Prepare drafts or verify bundles, without replacing administrator publication review. |
| 自动 OTA / Automatic OTA | `python3 tools/run_reader_baseline.py ota_auto ota_auto_ui loop lifecycle routing` | 验证 12 小时边界、跨重启间隔、时钟调整、联网守卫、取消交接、入口与确认返回；不连接网络或刷写设备。/ Check twelve-hour boundaries, reboot cadence, clock changes, network guards, cancellation, entry and offer return without networking or flashing hardware. |

新增宿主回归应接入统一入口；公共测试替身按所属领域放入 *_stubs。重命名或迁移脚本时同步 CI、调用方和文档，避免复制一份旧实现维持兼容。
Register new host regressions in the shared runner and group doubles under the matching *_stubs directory. Update CI, callers and docs when moving scripts instead of duplicating old implementations.
