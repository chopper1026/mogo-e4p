# AGENTS.md

面向 AI agent 的仓库入口（人类可读）。目标硬件为 **Metalio E-INK4 Plus（ESP32-S31）**；旧 S3 / CW32 / SY7636A 仅为上游历史背景，勿切回 S3。

- **原厂参考**：本地路径 `~/codespace/Metalio-E-INK4-Plus`（“原厂”均指此仓），含 SC7A20H 驱动与 AOI1 运动中断等硬件基础，不含晃动翻页等功能实现；硬件问题可对照其板级目录排查。

## 导航入口

| 资料 | 职责 |
| --- | --- |
| [README.md](README.md) | 产品概述、硬件表、引脚与使用说明 |
| [docs/ONBOARDING.md](docs/ONBOARDING.md) | 固定构建环境、宿主回归、工具链硬约束 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 目录职责、依赖边界、页面按键路由与术语表 |
| [docs/HANDOFF.md](docs/HANDOFF.md) | 任务交接协议（本机进行中账本为被忽略的 `docs/HANDOFF.local.md`） |
| [docs/development/INDEX.md](docs/development/INDEX.md) | 开发资料总入口、契约索引与功能全景导航 |

## 硬约束

- **`冻结 / Frozen:` 决策**：文件头中的 `冻结 / Frozen:` 段落是产品决策而非建议；改行为前必须先改写该段并说明决策变更原因。
- **品牌与服务边界**：固件与项目品牌统一小写 `mogo`（默认设备名、热点前缀、界面与传书网页），仅迁移历史默认设备名、保留用户自定义名称；热点口令 `mogo1234`；网页设备称「墨水屏」、保持纯白底色。天气与 OTA 官方域名为 `mogo.mantake.shop`，联网 OTA 仅读官方服务器清单（无 GitHub 回退源）。源码与新版本发布使用当前 `origin`（现为 `chopper1026/mogo-e4p`）；保留已发布下载/OTA 地址、备份文件名、内部协议标识与历史许可/版权声明。
- **硬件与电源红线**：
  - S31 面板 VCOM 标定存于 ESP NVS `panel/vcom`，由 `device_power_metalio.c` 启动读取；仅出厂标定页（`main/factory/vcom_setup.c`）可写入，未标定时拦住开机，产品页只读。
  - 旧 SY7636A 路径禁写 VCOM / VLDO / 放电 / 延时 / VCOMCTL，电源页只读（不替代 S31 正常上电应用已保存 VCOM 的流程）。
  - S31 使用 flash 80 MHz、Octal PSRAM 200 MHz；旧 S3 的 Zbit HPM 覆盖不参与 S31 构建。
  - 现用 `board_*`、`cst836u_*`、`fca9555_*` 包含当前板兼容接口，不得仅凭旧芯片名删除。
- **禁改文件与文案**：不因“整理注释”修改 `components/epdiy/**`；不手改 `main/font/fallback.h`、`main/font/stb_truetype.h`、`components/e0470_epaper_waveform/waveforms/*.h`；除非任务明确要求，不改 UI 字面量或日志字符串。
- **夜间阅读模式**：已撤回、不作为正式功能上线，N00–N21 试验已归档停止（历史记录留存于本地 `docs/local/reader-night-mode-experiments.md`）；未经重新明确授权，不得恢复实验功能、继续试验或刷机。

## 构建、测试、刷机与发布

- **构建与本机环境**：使用 ESP-IDF v6.1，`esp32s31` 为 preview 目标，构建必须带 `--preview`（`sdkconfig.defaults` 为产品配置，`sdkconfig.ci` 仅叠加警告；CI 不替代真机验证）。本机构建与宿主回归必须先遵循 [本机环境硬约束](docs/ONBOARDING.md#本机环境硬约束--local-environment-hard-rules)：固定 IDF v6.1 / Python 3.13 路径禁裸跑 `idf.py`；macOS 禁用系统 clang/cc/gcc，使用 CLT、限时 sanitizer 探针与 `tools/run_reader_baseline.py`，无输出超 30 秒即停止并留日志，禁全局关闭 sanitizer；环境检查失败不得跑全量回归、冷构建或刷机。
- **本地刷机**：优先使用本仓 [mogo-e4p-flash](.agents/skills/mogo-e4p-flash/SKILL.md) 与 `python3 tools/flash_fast.py flash --yes`（默认及上限 180 秒，末尾 10 秒用于安全清理与恢复；使用官方 S31 RAM stub，不回退 ROM 读取、不硬编码活动槽、不自动退回整机擦写、不刷旧包；skill 仅放本仓 `.agents/skills/`，不安装全局）。开发构建阶段可先 `prepare --build-dir` 登记构建。每次刷机、超时准备或整机恢复均须用户明确授权，设计或测试工具不代表刷机授权。
- **本地版本与产物**：日常本地测试沿用当前正式发布的固件版本号，用日期和说明区分，不另增 rc 编号；本地包放忽略构建目录，严禁覆盖 `flash/` 中已发布的固定下载文件。联网 OTA 不同版本专项测试须先获明确授权。
- **正式发布**：完整遵循 [正式发布契约](docs/development/guides/RELEASE.md)——正式标签前先提交准确的中文 OTA 日志（≤383 字节），核对当前 `origin`，以同源产物同步 GitHub Release、官网刷机、TF 下载与 OTA 草稿，遵守不可覆盖已发布附件与管理员审核边界。调用治理不授予发布权限。
- **Git 提交**：遵循 [中文提交规范](docs/COMMIT_CONVENTION.md)——标题 `分类（范围）：中文摘要`，正文按分类顺序从 `1.` 连续编号、每项一事，末尾如实写验证结果；agent 提交末尾注明 `代理：<名称>`；不改写历史提交。

## 架构与页面契约

- **目录与依赖**：以 [目录职责](docs/ARCHITECTURE.md#目录职责--ownership) 为准。`main/` 根目录仅留启动入口与构建配置；页面互不直接依赖；公共图像模块（`main/image/`）不依赖书籍、页面或字库。
- **页面注册**：产品页清单以 `main/CMakeLists.txt` 与 [app_registry.c](main/app/app_registry.c) 为准；新增页面须同时登记两者，出厂 VCOM 与自检由 `main/factory/` 与启动入口调用。
- **`app_desc_t` 核心红线**：完整定义见 [main/app/app.h](main/app/app.h)，主循环推屏见 [app_loop.c](main/app/app_loop.c)，回调与三键/全局快捷键路由契约见 [页面回调与按键路由](docs/ARCHITECTURE.md#页面回调与按键路由--page-callbacks-and-key-routing)。
  - `render()` 必须纯绘制，严禁 I2C 写、蜂鸣、睡眠或改设置。
  - `normalize_hold_gray()`、`screen_touch_disabled()` 等纯查询须按当前可见画面判断，不可用全局设置替代。
  - 锁屏前保存状态使用 `on_before_lock()`；失卡清理用 `on_media_lost()`（不画屏、不重挂载）。

## 文档交付、许可与代码注释

- **文档交付边界**：计划、检查点、过程审查、验收结果与调试日志留在 `docs/local/` 或忽略构建目录，不入库；长期有效行为与限制写入对应功能文档；用户可见功能变化仅在 `docs/CHANGELOG.md` 按版本轻量记录，不另建逐任务变更账本；未完交接按 [docs/HANDOFF.md](docs/HANDOFF.md) 维护。
- **授权声明**：适用边界以 [LICENSE](LICENSE) 与 [CONTRIBUTING.md](CONTRIBUTING.md) 为准。新增原创内容仅在权利人明确同意时标注 `LicenseRef-Kiikoread-Personal-Study-1.0` 并保留真实版权人；不撤销 rc93 及以前授权，不覆盖第三方权利；不得将下方 Apache 文件头示例机械用于新原创文件，也不得批量替换既有文件头。
- **源码注释规范**：全仓源码注释保持**中英双语**；纯整理注释时不改逻辑；术语以 [术语表](docs/ARCHITECTURE.md#术语表--glossary) 为准，注释中不重复展开。
  - 头文件公开 API / struct 成员用 `///`；`.c` 内部用 `//`（禁用 `/** */`）；`enum` 尾注 `///< 中文 / English`；大 `.c` 用 `/* ---- 区块名 / Section ---- */` 分区。
  - 单行 `// 中文。/ English.`；多行中文在前、英文紧跟。
  - 既有 Apache 文件头参考格式（新原创文件按上方授权规则选择 SPDX）：
    ```c
    /*
     * SPDX-FileCopyrightText: 2026 mindreset
     * SPDX-License-Identifier: Apache-2.0
     *
     * 中文：这个文件负责什么、边界在哪。
     * English: what this file owns and where it stops.
     *
     * 冻结：不得做的事。
     * Frozen: things this file must not do.
     */
    ```

<!-- tidy:start -->
## 项目开发治理

先读取 `.tidy.json` 与 `docs/development/INDEX.md`。提交信息遵守 `docs/COMMIT_CONVENTION.md`。
使用 `$tidy` 执行提交前文档/架构/功能检查；GitNexus 是否参与由项目配置决定。
tidy 更新后使用 `$tidy-rebuild` 评估并对齐现有治理；制定或维护自动化发版流程使用 `$tidy-release`。
本次记录放 `docs/local/tidy/changes/`；过程材料遵守已确认的可见性，不取消既有忽略边界。
普通计划、设计草稿、图片、过程决策和验收摘要放 `docs/local/tidy/` 对应分类；工具固定路径优先，稳定共享文档另按项目政策维护。
GitNexus 计划保留 `docs/plans/*gitnexus-plan*.md` 原路径与正文，tidy 只建索引，不搬迁、改名、复制或用符号链接替代。
本地过程策略与 `gitnexus-work` 的计划入库要求冲突时，先确认可见性与当前提交授权；不强制入库或擅自改变忽略规则。
提交前按规范归类全部实际变化并维护统一入口；仅暂存本任务内容。
现行操作说明与源码/配置对齐；变动涉及多语言、手册或备份边界时同步核对其权威入口。功能实现与验证证据分开维护，局部结果不追认为全量验收。
已配置 document_checks 时，check 同时核对文档引用；语义、测试执行与设备验收仍须各自证据。
每次提交前检查并清理本任务的临时日志、断点、测试入口及调试桩；保留正式日志/回归测试，清理后验证并运行 check --staged。
GitNexus 已停用：tidy 仍执行文档、架构、功能和暂存快照检查，不运行 GitNexus；需要时按项目决定重新启用。
不因治理流程获得 push、历史改写、发布或硬件操作授权。现有规则冲突先询问用户。
<!-- tidy:end -->
