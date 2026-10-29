# 开发工具入口 / Development tools

所有命令从仓库根目录执行；生成物放入被忽略的 build 目录。现有脚本路径继续保留，方便 CI 与本地流程复用。
Run commands from the repository root and keep outputs in ignored build directories. Existing script paths remain stable for CI and local workflows.

| 类别 / Category | 入口 / Entry | 用途 / Purpose |
| --- | --- | --- |
| 目录与依赖 / Structure | `python3 tools/check_architecture.py` | 检查模块依赖、构建清单和测试入口。/ Check module boundaries, build registration and test entry points. |
| 完整阅读回归 / Reader regression | `python3 tools/run_reader_baseline.py` | 统一执行阅读、字体、显示、存储、OTA 与应用循环回归，保留 ASan/UBSan。/ Run reader/font/display/storage/OTA/app-loop suites with ASan/UBSan. |
| 指定领域 / Selected suites | `python3 tools/run_reader_baseline.py session chapter open turn reflow` | 只执行指定领域；完整列表见 `--help`。/ Run selected domains; see --help for all suites. |
| 书架分类 / Shelf categories | `python3 tools/run_reader_baseline.py shelf management navigation` | 链接真实分类状态，检查目录、两种布局页码、菜单手势、分配失败及页面衔接。/ Link real category state to check folders, both layout pages, menu gestures, allocation failures and page integration. |
| 书架封面 / Shelf covers | `python3 tools/run_reader_baseline.py shelf_covers` | 运行真实封面状态、工作队列和页面绘制，检查取消、整组显示、推屏失败、内存上限与缓存复用。/ Run real cover state, worker and page painting; check cancellation, grouped reveal, failed presentation, memory bounds and cache reuse. |
| 阅读设置绘制 / Reader settings view | `python3 tools/run_reader_baseline.py settings_view controls` | 链接真实绘制模块，对照原指令摘要并检查字号适配、命中边界与系统字形。/ Link the real view; compare legacy drawing digests and check scaled bounds, hit edges and UI glyphs. |
| 图片文件 / Image files | `python3 tools/test_image_files.py` | 实际 JPEG/PNG 流式解码、损坏和大小边界；需要 Pillow、libpng 与 JPEG 依赖源码。/ Real streaming JPEG/PNG, corruption and size bounds; requires Pillow, libpng and JPEG sources. |
| 题头 / Heading | `python3 tools/reader_title_style_host_test.py` | 四种样式、字号、折行、默认画面像素一致性。/ Four styles, sizes, wrapping and historical-default pixel equality. |
| 字体上下文 / Font context | `python3 tools/lock_font_host_test.py` | 系统/阅读字体借用与睡眠恢复。/ System/reader font borrowing and wake restoration. |
| 资源生成 / Asset generation | `gen_builtin_font.py`、`compress_builtin_images.py`、`gen_ui_icons.py` | 更新对应源资源后再生成，不手改生成表；具体参数见脚本帮助。/ Regenerate after source changes instead of hand-editing tables; see each script's help. |
| 性能采集 / Performance | `capture_reader_perf.py`、`summarize_reader_perf.py`、`summarize_reader_memory.py` | 使用方式与结论边界见 docs/READER_PERFORMANCE.md。/ See the performance guide for usage and interpretation limits. |
| OTA 与包校验 / OTA and bundles | `prepare_platform_submission.py`、`verify_flash_bundle.py` | 准备草稿或校验包；不会代替管理员发布审核。/ Prepare drafts or verify bundles, without replacing administrator publication review. |

新增宿主回归应接入统一入口；公共测试替身按所属领域放入 *_stubs。重命名或迁移脚本时同步 CI、调用方和文档，避免复制一份旧实现维持兼容。
Register new host regressions in the shared runner and group doubles under the matching *_stubs directory. Update CI, callers and docs when moving scripts instead of duplicating old implementations.
