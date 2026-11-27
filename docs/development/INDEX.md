# 项目开发总入口 / Development Index

本页只维护导航、资料权威和覆盖范围，不逐次抄写任务摘要。当前能力与验证状态见[功能全景](FEATURES.md)，按版本的用户变化见[CHANGELOG](../CHANGELOG.md)。
This page owns navigation, authority and scope, not per-task summaries. Use the feature inventory for current capabilities and evidence, and the changelog for versioned user changes.

## 权威入口 / Authoritative Entries

| 资料 / Document | 适用范围 / Scope |
| --- | --- |
| [README](../../README.md)、[上手指南](../ONBOARDING.md) | README 是产品、安装和硬件入口；上手指南是固定本机环境、构建与宿主回归命令的权威。移植评估归历史资料。/ README owns product, installation and hardware entry points; onboarding owns the fixed environment, build and host commands. Port assessments are historical. |
| [AGENTS](../../AGENTS.md)、[贡献指南](../../CONTRIBUTING.md) | 产品硬约束、页面契约、许可和操作授权边界。/ Product constraints, page contracts, licenses and authorization boundaries. |
| [中文提交规范](../COMMIT_CONVENTION.md) | 未来提交唯一格式依据；保留原路径与旧 Git 历史。/ Sole format authority for future commits, retaining its path and existing history. |
| [架构](../ARCHITECTURE.md)、[功能全景](FEATURES.md) | 稳定依赖与所有权，以及有入口依据的产品能力。/ Stable dependencies/ownership and entry-backed capabilities. |
| [持久化与备份](guides/PERSISTENCE.md) | 数据位置、支持导出/恢复的字段、全擦除和布局迁移风险；安装前核对。/ Storage, backup coverage and destructive-operation risks. |
| [正式发布](guides/RELEASE.md)、[说明书维护](guides/MANUAL.md) | 同源交付契约与保留的手册输入/生成前提；不代表发布或部署。/ Release delivery rules and manual source/generation requirements. |
| [交接协议](../HANDOFF.md) | 只定义共享协议；当前机器账本仍为被忽略的 `docs/HANDOFF.local.md`。/ Shared protocol only; the live machine ledger remains ignored. |
| [分类索引](CATALOG.md)、[工具入口](../../tools/README.md) | 自动生成的资料导航与可复跑工具，不证明运行结果。/ Generated navigation and repeatable tooling, not execution evidence. |

## 现行技术资料 / Technical References

- [统一内存管理](designs/MEMORY_MANAGEMENT.md)：快捷栏与实时管理页、分池准入、部分记账、压力清理及资源所有权。/ Quick controls, live memory page, pool admission, partial accounting, pressure cleanup and resource ownership.
- [蓝牙翻页器设置](guides/BLUETOOTH.md)：状态卡、分页扫描、设备管理、行内学习与生命周期。/ Status cards, paged discovery, peer management, inline learning and lifecycle.
- [显示刷新契约](designs/DISPLAY_REFRESH.md)：显示意图、波形出口与失败恢复。/ Refresh intents, waveform exits and failure recovery.
- [简约时钟锁屏](guides/LOCK_CLOCK.md)：极简24小时制、浅睡分钟刷新、RTC与密码/定时关机边界。/ Minimal clock, light-sleep minute updates, RTC and PIN/shutdown boundaries.
- [日历锁屏](guides/CALENDAR.md)：两款样式、北京时间、离线数据、v38备份和生成许可。/ Two faces, Beijing time, offline data, v38 backup and asset generation.
- [壁纸](guides/LOCK_STYLE_PICKER.md)：待机和关机壁纸、书籍封面、异步图片加载、TF双槽及取消边界。/ Standby and shutdown art, book covers, asynchronous images, SD dual slots and cancellation boundaries.
- [CloudZao 传输](guides/CLOUDZAO_TRANSFER.md)：直连收件、发版接入配置与 TF 交还。/ Direct reception, release configuration and TF handoff.
- [组件与天气](guides/WEATHER.md)：首页布局、选城、网络生命周期与缓存。/ Home layout, city selection, networking and cache.
- [迷你书库](guides/MINI_LIBRARY.md)：扇页选书、随机/最近模式、封面缓存与动画清理。/ Fan-deck selection, random/recent modes, cover caching and motion cleanup.
- [阅读手账](guides/READING_JOURNAL.md)：当月逐日统计、日历、六图轮换与本机存储边界。/ Monthly daily records, calendar, six-scene rotation and local persistence limits.
- [阅读性能测量](guides/READER_PERFORMANCE.md)：诊断开关、资源归属和样本解释边界。/ Diagnostic flags, resource ownership and measurement limits.
- [阅读字典](guides/READER_DICTIONARY.md)：选字交互、阅读字体、TF 词库安装、来源许可、格式和取消边界。/ Selection, reader fonts, card packages, licensing, format and cancellation.
- [阅读方向](guides/READER_ORIENTATION.md)：快捷栏自动旋转与锁定、四向重排、输入及系统竖屏边界。/ Quick auto-rotation/lock, four-way reflow, input and system portrait boundaries.
- [MOBI 阅读](guides/MOBI.md)：格式边界、目录与进度坐标、缓存和资源限制，以及解码组件许可。/ Format scope, navigation/progress coordinates, caches, resource limits and decoder licensing.
- [平台 OTA 操作](guides/PLATFORM_OTA.md)、[OTA 日志要求](../ota-release-notes/README.md)：构建提交草稿与管理员发布不同。/ Draft submission is distinct from administrator publication.
- [板级入口](../../components/board_support/README.md)、[电源组件](../../components/device_power/README.md)、[离线检索](../../components/book_search/README.md)：模块旁文档保留原位。电源组件的 `docs/` 是旧 CW32 协议参考，不是当前板级事实。/ Keep component documentation in place; the power component's docs are legacy CW32 references, not current board specifications.

## 历史与固定路径 / History And Fixed Paths

- [移植评估](history/porting/%E7%A7%BB%E6%A4%8D%E9%80%82%E9%85%8D%E6%96%B9%E6%A1%88.md)保留 2026-10-19 的候选方案和旧路径，不是当前硬件或删除清单。/ The port assessment retains its dated proposals and old paths, not current hardware or a deletion list.
- [rc68](history/releases/RELEASE_NOTES_0.3.3-rc68.md)、[rc69](history/releases/RELEASE_NOTES_0.3.3-rc69.md)记录旧 S3 产品当时的发布事实，不作为 E4P 设备验收。/ Legacy S3 release notes are historical, not E4P acceptance.
- [旧官网说明书维护说明](history/WEBSITE-MANUAL.md)描述旧站点与 `flash/` 输出；当前 checkout 没有该目录，不能据此认定网站已部署。生成器和两份说明书 JSON 仍保留原位。/ The old website guide describes the former site and absent `flash/` outputs; it does not establish deployment. Keep manual generators and their JSON inputs in place.
- `docs/ota-release-notes/`、`docs/user-manual.zh-CN.json`、`docs/user-manual-detailed.zh-CN.json` 是构建或工具输入，不随资料分类搬迁。根入口、许可证、模块 README、固件图片和生成数据也保持原路径。/ Preserve build/tool inputs, root entries, licenses, component READMEs and runtime/generated assets.
- 历史文档采用先备份后迁移；完成状态以 `.tidy.json` 的 `legacy_docs.status` 为准。确认新结构不代表文件已经移动。/ Back up before migration; the config records its completion state, not the selection alone.

## 过程资料与闭环 / Local Work And Checks

- 提交策略为 `adapt`，复用现有中文规范；过程可见性为 `local`。普通计划、草稿、审查、图片和验收证据留在 `docs/local/tidy/`，不新增共享逐任务账本。/ Adapt the existing Chinese convention and keep ordinary work records local.
- 本机记录在 `docs/local/tidy/changes/`，本机导航由 `sync` 生成于 `docs/local/tidy/INDEX.md`；这些路径不作为共享文档的链接目标。/ Local records/navigation are generated locally, not shared-link dependencies.
- GitNexus 已按项目配置停用；源码和测试为分析依据，无实际 post-commit hook，不要求安装 CLI、运行 impact/detect_changes 或刷新图谱。保留历史状态，不改其他项目或全局工具。/ GitNexus is disabled; source/tests remain authoritative, with no active local post-commit hook or graph requirement.
- 提交前核对实际 diff、清理本任务调试残留、维护相关长期说明与功能状态、运行 `sync` 和 `check`；仅暂存本任务内容后再检查 `check --staged`。本地记录仍需人工确认。/ Review the real diff, clean task-owned debug leftovers and maintain documentation before separate workspace/index checks; local records still require manual review.
- 不自动提交、推送、安装 hook、发布、上传文档或操作硬件。/ No automatic commits, pushes, hook installation, publication, document uploads or hardware operations.

## 覆盖与排除 / Coverage And Exclusions

共享分类覆盖 `docs/`、工具 README、已登记的自有板级/驱动/检索 README 和旧电源协议附件；不移动这些模块文档。完整盘点可运行 `inventory-docs`，候选包含配置和运行时素材，不能直接当作待迁移文档。
The shared catalog covers docs, the tool guide, selected first-party component READMEs and legacy power references without moving module docs. Inventory candidates also include configuration/runtime assets and are not an automatic move list.

忽略目录、本机交接、夜间试验、构建产物、供应商源码和工具自己的 `.claude/`、`.zcode/`、`.playwright-mcp/` 不纳入共享过程资料。第三方许可由 [LICENSE](../../LICENSE) 和[第三方声明](../../THIRD_PARTY_NOTICES.md)负责；目录分类不替代许可核验。
Ignored/local work, night trials, builds, vendor sources and tool working directories are excluded from shared process material. Root licensing and third-party notices remain authoritative; classification is not license verification.

文档检查覆盖根 README 三语言、AGENTS/贡献说明、现行 docs Markdown、模块 README、项目刷机说明和两份手册 JSON；排除历史资料、本机过程、旧计划与按版本存档的 CHANGELOG。引用检查不请求外部 URL，也不验证手册生成或产品行为。suite 注册来自 Python runner，需静态核对 `SUITES`，不套用 JSON registry 合约。
Document checks cover maintained entry points, guides, module READMEs, the project flashing skill and both manual inputs. History, local work, old plans and the versioned changelog are excluded. External routes, generation and product behavior need separate evidence; runner suites use Python registration.
