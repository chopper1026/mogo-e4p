# 显示刷新契约 / Display refresh contract

页面和控件提交“刷新意图 + 区域 + 画面属性”，显示层决定波形、驱动模式和像素覆盖。绘制、输入、页面状态和计数仍归原模块；接口同步执行，不排队、不安排延迟补刷。原有整页导航、强刷、图片清白和错相翻页保留语义出口。
Pages and controls submit intent, bounds and frame attributes. Display selects waveforms, drive modes and pixel coverage. Original modules retain painting, input, page state and counters. Calls execute synchronously without queues or deferred passes. Semantic exits remain for navigation, full refresh, image whitening and staggered turns.

## 分层 / Ownership

| 文件 / File | 职责 / Responsibility |
| --- | --- |
| `main/display/display_refresh.h` | 页面唯一的通用局部/整屏意图接口。/ Public area/screen intent interface. |
| `display_refresh_policy.c` | 纯解析：意图和属性 → 波形编号、模式、覆盖、脏区膨胀。/ Pure intent/attribute → waveform ID, mode, coverage and dilation. |
| `display_refresh_context.c` | 每次同步读取现有主页与背景设置，不缓存状态。/ Read existing main/background settings for each call without caching. |
| `display_refresh.c` | 解析后只调用一次既有物理出口，原样返回错误。/ Resolve and invoke exactly one existing physical exit, returning its error. |
| `display_waveform_catalog.c` | 24 个稳定编号、来源、主要基底、派生说明和用途；惰性调用原构造器。/ 24 stable IDs with sources, primary bases, recipes and roles; resolve original builders lazily. |
| `display.c` | 物理执行、导航多段输出、清屏计数、扫描档、OTA 互斥、供电准入与失败恢复。/ Physical execution, staged navigation, counters, scan profiles, OTA exclusion, power admission and recovery. |
| `display_static_black.c` | 复用差分缓冲同帧合成局刷和区外纯黑补偿，不改源波形。/ Merge the requested update and outside-black reinforcement in the existing difference buffer without modifying source waveforms. |
| `display_main_waveform.c` / `display_reader_waveform.c` / `display_normalize.c` | 原有派生构造与分配失败回退，不属于页面接口。/ Existing derivation builders and allocation-failure fallbacks, outside the page API. |
| `components/e0470_epaper_waveform/` | 原始表、基础裁剪及基础短表；生成/供应商表不手改。/ Source tables, base trims and short tables; do not hand-edit generated/vendor data. |

`display_refresh_area()` 的面积由调用方给出，策略不扩大矩形；有膨胀的意图交给原出口扩大 dirty masks。`display_refresh_screen()` 支持 AUTO/DIFF 且无膨胀的策略；不支持的组合在构造波形前拒绝。整屏范围不代表全像素驱动。
Callers own the rectangle passed to `display_refresh_area()`. Policy does not enlarge it; padded intents let the existing exit dilate dirty masks. `display_refresh_screen()` accepts unpadded AUTO/DIFF policies, rejecting unsupported combinations before waveform construction. Screen bounds do not imply full-pixel drive.

## 意图矩阵 / Intent matrix

AUTO 保留旧出口行为：区域 GL16 会全像素驱动，DU 按原差分；整屏走原 high-level 路径。DIFF 通常只驱动变化像素（静态纯黑补偿见下文）；FULL 强制区域全像素。`QUICK` 仅表示已选短刷新状态，不能让调用方自由指定波形。
AUTO retains the old exit behavior: area GL16 drives all pixels, DU uses the original differential, and screen requests use the original high-level path. DIFF normally drives changed pixels (see the static-black exception below); FULL forces all pixels in the region. QUICK conveys an already-selected short-refresh state without allowing arbitrary waveforms.

| 意图 / Intent | 波形 / Waveform | 模式 / Mode | 覆盖 / Coverage |
| --- | --- | --- | --- |
| DETAIL | default | GL16 | DIFF |
| LAYOUT | default | GL16 | AUTO |
| SETTLE / OVERLAY | default | GL16 | FULL |
| FEEDBACK | follow | DU | DIFF |
| MOTION | follow | DU | AUTO |
| READER_FOOTER | follow | DU | AUTO |
| DYNAMIC | default | APP_DYNAMIC_REFRESH_MODE | AUTO |
| MAIN_CLOCK | 主页快刷 main-bw，否则 main-gray / fast main-bw, otherwise main-gray | DU / GL16 | DIFF |
| MAIN_DETAIL | 普通 main-normal，否则 main-gray / ordinary main-normal, otherwise main-gray | GL16 | DIFF |
| MAIN_SCROLL | 普通 main-normal，否则 default / ordinary main-normal, otherwise default | GL16 | DIFF，1 行 + 1 列字节膨胀 / 1 row + 1 column byte padding |
| READER_TEXT | reader-text；有背景用 reader-background / reader-background when configured | GL16 | DIFF；有背景保留 AUTO 回退 / AUTO fallback for backgrounds |
| READER_BODY | IMAGE 优先 reader-image；否则背景 reader-background / 无背景 reader-ink / IMAGE wins, otherwise background reader-background or reader-ink | QUICK DU，否则 GL16 / QUICK DU, otherwise GL16 | DIFF |
| READER_LAYOUT | 背景 reader-background，否则 reader-ink / background reader-background, otherwise reader-ink | QUICK DU，否则 GL16 / QUICK DU, otherwise GL16 | AUTO |
| READER_IMAGE | full | GC16 | AUTO |
| READER_COMIC | reader-comic | GC16 | 正文 FULL、栏位 DIFF，同帧 / FULL body and DIFF chrome in one scan |
| READER_CLEANUP | full | GC16 | FULL |
| RESTORE | HOLD_GRAY 为 normalize-gray，否则 normalize / HOLD_GRAY normalize-gray, otherwise normalize | GL16 | FULL |
| CAPSULE | default | DU | DIFF，1 行 + 1 列字节膨胀 / 1 row + 1 column byte padding |

仅 READER_BODY 接受 QUICK/IMAGE，READER_LAYOUT 接受 QUICK，RESTORE 接受 HOLD_GRAY；其余意图只接受 DEFAULT。无效意图、无效标志或整屏组合返回 `EPD_DRAW_INVALID_CROP`，缺表返回 `EPD_DRAW_NO_PHASES_AVAILABLE`；都不扫描。
Only READER_BODY accepts QUICK/IMAGE, READER_LAYOUT accepts QUICK, and RESTORE accepts HOLD_GRAY. Other intents accept DEFAULT only. Invalid intents, flags or screen combinations return EPD_DRAW_INVALID_CROP; missing tables return EPD_DRAW_NO_PHASES_AVAILABLE. Neither scans.

### 设置主列表滚动 / Settings root list scrolling

主列表越过纵向触摸容差后按起点位移跟手，合并移动中的采样，松手不续滑。绘制借用已有差分缓冲，只将列表窗口复制到目标帧；题头、底栏与子页翻页不变。每步沿用 MAIN_SCROLL 的灰阶差分，不因滚动次数强刷。
After vertical touch slop, the root tracks displacement from contact, coalesces moves and stops on release. Painting borrows difference scratch and publishes only the list viewport. Header, navigation and selector paging remain unchanged. Motion retains MAIN_SCROLL gray differences without count-based wipes.

松手后可丢弃地补白至多一次：从已呈现的 back 识别旧分隔线与卡片横边，新旧帧均为白的候选像素才接受一相白推，其余保持。不会伪造参考帧、加入惯性或恢复全屏；再触摸、离页或锁屏取消未执行的补白。物理残影仍需真机对照。
Release permits at most one optional white pulse. Detect old horizontal rules and card edges from committed pixels; only candidates white in both buffers receive drive. Other pixels hold. Never fabricate a baseline or add inertia or a full-screen settle. A new contact, page change or lock cancels pending cleanup. Optical results require panel comparison.

实屏 A/B 使用同起点、同手势、同刷新模式；仅对照构建开启 `CONFIG_SETTINGS_SCROLL_SKIP_CLEANUP`，产品默认关闭。/ Use matched starts, gestures and refresh modes; enable `CONFIG_SETTINGS_SCROLL_SKIP_CLEANUP` only in the comparison build (off by default).

## 阅读状态与输出顺序 / Reader status and output order

解锁恢复与退出阅读到主页面（包括同页返回书架）统一使用完整48相 `E0470_FULL_WAVEFORM`、GC16及整屏全像素输出，包含固定底栏和未变化像素。定时关机后启动恢复的首张目标页也使用这一策略。阅读返回按状态转场识别，与触发该动作的按键绑定无关。`display_request_full_refresh()` 只在目标输出成功后消费请求，失败重试保留48相；输出后恢复默认波形。锁屏出图成功时保留真实前后帧，不增加预清白；供电或输出故障仍按真实白底重建。日常阅读翻页、主页切页及未请求转场的手动全刷保持既有策略。软件验证不代表实屏残影改善。
Unlocks and reader-to-main returns (including an in-app shelf return) use complete 48-phase `E0470_FULL_WAVEFORM`, GC16 and whole-screen full-pixel coverage, including fixed navigation and unchanged pixels. The first resumed frame after timed power-off uses the same policy. Reader returns are identified by the view transition, independently of the key binding that triggers the action. `display_request_full_refresh()` is consumed only on successful target output; retries retain 48 phases and output restores the default waveform. Successful lock output retains real frame references without an extra pre-clear; power/output faults retain physical-white reconstruction. Ordinary reader turns, main transitions and manual full refreshes without a transition request retain their policies. Software validation does not establish optical improvement.

阅读正文静置时，分钟、电量及充电变化不单独触发顶部刷新；下一次正文翻页或整页重绘显示当前状态。窗口化页眉与系统状态栏共用80/160像素顶部带，全屏隐藏状态栏时不刷新顶部；主页面和快捷栏仍按原规则更新状态。
Minute, battery and charging changes do not independently refresh a resting reader. The next body turn or whole-page redraw shows current status. Windowed headers and system status share the 80/160-pixel top band; full screen with a hidden status bar has no top output. Main pages and the quick layer retain their status rules.

稳定文字页的局部翻页在开始扫描前准备正文、变化后的顶部和可见页脚；能合并时一次推屏，不在正文后再刷顶底栏。未开扫且无法合并时才走分步路径：先正文，再页脚，最后更新有变化的顶部；普通页脚仍用 8 相短 DU，顶部及换章/插图的较强页脚使用 37 相 DETAIL GL16。只有成功输出后才确认顶部状态；中途扫描失败不以差分回放，沿用真实基准恢复。插图、手动/周期清理、工具带与关闭面板归一仍走原出口。

Stable partial text turns prepare body, changed status and visible footer before scan. A joined turn presents them together with no trailing chrome scan. If joining is unavailable before scanning, the split order is body, footer, then changed status. Ordinary split footers retain eight-phase short DU; split status and stronger chapter/image footers use 37-phase DETAIL GL16. Acknowledge status only after success; never replay a differential after interrupted output. Illustration, manual/periodic cleanup, tools bands, and panel-close normalization keep their existing exits.

## 局刷同帧静态纯黑补偿 / Same-scan static-black reinforcement

`DETAIL`、`MAIN_CLOCK` 和 `MAIN_DETAIL` 在最终选择 GL16 差分、区域裁剪后且确有变化时计数。每第 4 次尝试补偿，整屏 GC16 成功后重新计数；无变化不计。DU、跟手/按压反馈、显式扩边的动效、整页与清理仍走原路径。策略与页面无关，不要求调用方重画静止区域。

`DETAIL`, `MAIN_CLOCK`, and `MAIN_DETAIL` count when the effective mode is differential GL16 and the final cropped area really changes. Try reinforcement every fourth eligible update; a successful full-screen GC16 restarts the count. No-ops do not count. DU, motion/press feedback, explicitly padded motion, whole-page updates, and cleaning keep their existing paths. The policy is page-independent and does not require callers to repaint resting regions.

补偿轮在同一次扫描中保留区域内变化像素的原波形；区外只根据已提交的 `back_fb` 识别 `0x0` 纯黑，复用当前 GL16 白→黑的最后四次推黑相，不推白、不增加相位。其他未变化像素保持，白底、中灰、抗锯齿灰边不补偿；固定导航也仅允许已显示的纯黑受驱动，不能改写内容。补偿轮不使用常规脏掩码膨胀，成功后仅回写请求区域（保留边界邻接半字节），不提前提交区外待绘图。没有区外纯黑、波形不适用或临时 LUT 分配失败则使用原差分。扫描失败不回写，走既有整屏恢复机制。

The merged scan retains the original transitions for changed pixels in the area. Outside it, only committed `back_fb` pixels at gray level `0x0` reuse the final four white-to-black pushes from the effective GL16 table, with no whitening or added phases. Other unchanged pixels hold, including whites, midgrays and antialiased edges. Fixed navigation may only receive drive on already-visible black; its content cannot change. The merged scan does not dilate dirty masks; on success it commits only the requested area, including exact nibble edges, and does not leak staged outside targets. No outside black, unsupported waveforms or temporary LUT allocation failure fall back to the original differential. Failed scans do not commit; they use existing full-recovery handling.

相数不增加不代表耗时无变化：合并轮会多做整屏差分编码，实际前导相跳过也取决于像素迁移。4 次 / 4 相是起始参数，需用实屏比较静态纯黑墨度、保持中的灰边、黑→白残影和局刷耗时。

No extra phases does not guarantee unchanged latency: merged updates encode a full-screen difference, and leading-phase skips depend on the transitions in it. The four-update/four-pulse values are starting points; verify static black density, held gray edges, black-to-white ghosting and partial-update latency on panel.

### 阅读翻页顶底栏保墨 / Reader turn chrome reinforcement

稳定正文的普通 GL16 和水波纹翻页将有变化的栏位并入正文的扫描窗口，在原相位预算内擦旧字、写新字；未变化的纯黑仍可同帧补四相，不等通用局刷计数。正文外纯白仅在新旧帧以该点为中心的 5×5 邻域都为白时，借最后一个黑→白拍同向补一次；这样文字、图标、抗锯齿灰边和周围两像素都不会被补白。其他不变灰阶保持；未请求更新的栏位不提交新内容。静置时不产生保墨请求。

Ordinary GL16 and ripple turns of stable text publish changed chrome in the body scan window. Old and new glyphs fit the existing phase budget; unchanged pure black can still receive four reinforcement pulses without waiting for generic partial-update counts. Drive a white pixel outside the body once on the last black-to-white tick only if its 5×5 neighborhood is white in both frames. This excludes glyphs, icons, antialiased edges and their two-pixel margin. Hold other unchanged chrome levels; never publish staged content outside requested regions. Reinforcement never schedules an idle scan.

普通翻页使用当前正文表响应栏位差分；带背景时正文保留原迁移，包括白→白。水波纹正文保留 8 带/37 相的揭页顺序，栏位在同一轮的前 37 拍走独立、未错相的 LUT。已变化和静态纯黑分开编码，未启动的正文条带保持；整体仍是 44 拍、11090 µs 目标节拍。扫描成功才提交准确范围。宿主验证相数和范围，不保证实际耗时与光学表现；实机须检查跨章和跨分钟的擦除、墨度以及连翻白底和稳定性。

Ordinary turns use the current body table for chrome differences; with a background the body retains original transitions, including white-to-white. Ripple retains the eight-band/37-phase body reveal, and chrome uses an independent, unstaggered LUT within the first 37 ticks. Changed pixels and static black are encoded separately, and unstarted body bands hold; the full turn still takes 44 ticks at the 11090 µs target. Commit exact regions only on success. Host tests establish phase counts and bounds, not physical latency or optical behavior; verify chapter/minute erasure, ink density, background whiteness and consecutive-turn stability on panel.

已解码的整页 EPUB 漫画普通翻页时，正文使用完整 GC16，页眉、状态栏和页脚只驱动变化像素，同一次扫描更新。正文取当前布局的全宽阅读区域，四个方向均按实际边界处理。GC16 原表中 2→0 与 3→0 的驱动逐相相同。`reader-comic` 将这两种真实转换合用一个编码，用空出的编码保持栏位中的静止像素。正文所有 256 种转换均保留原驱动与时序。首次构造校验两种转换并分配 3 KiB PSRAM。原表变化导致校验失败、分配失败或三栏全隐藏时，使用原表完整扫描。周期清理和手动全刷保留整屏 GC16。扫描失败时保留旧帧基准并请求完整刷新。
On ordinary full-page EPUB comic turns, the current body receives full GC16 and only changed pixels in the status, header and footer bars are driven in the same scan. The body follows the current full-width layout in all four orientations. Full GC16 has identical pulses for 2→0 and 3→0 in every phase. The `reader-comic` table shares one code between these real transitions and uses the spare code to hold stationary chrome. All 256 body transitions retain their original drive and timing. First use checks the equivalence and allocates 3 KiB of PSRAM. A failed equivalence check or allocation, or a body occupying the whole screen, uses the original full table. Periodic and manual cleanup retain whole-screen GC16. Scan failure preserves the previous frame baseline and requests a full refresh.

## 界面三色 / UI palette

界面底色和控件目标统一为白、黑及固定灰（`UI_GRAY_LIGHT=0x90`，4 位灰阶第 9 档），不随系统对比度漂移。卡片和导航以白底为主，文字使用黑色或明确的反白；固定灰用于进度底槽、未启用控件及活动状态。边框使用黑色，底栏分隔线使用固定灰0x90。共享底栏只有四枚图标与选中横条，高104像素，图标垂直居中；内容扫描止于1088，2像素0x90灰分隔线位于1112，保留24行白色保护带。
UI surfaces and controls target white, black and one fixed gray (`UI_GRAY_LIGHT=0x90`, level 9 in the 4-bit framebuffer), independent of system contrast. Cards and navigation favor white, text uses black or explicit white, and fixed gray marks tracks, disabled controls and active states. Frames are black and the footer divider uses fixed gray 0x90. The shared footer contains four icons and a selection marker in 104 pixels with vertically centered icons; content scans end at 1088, and a two-pixel 0x90 gray divider at 1112 retains a 24-row white guard.

三色约束发生在控件绘制入口，不对整帧量化。封面、图片、头像、时钟图像、阅读正文背景及字体和图标抗锯齿保留原路径；书架亚克力挡板保留原来的模糊、灰阶与透明合成，快刷模式原有的点阵处理继续生效。需要保留灰阶的绘制使用原始接口，不能经过控件调色板。
Apply the palette at control drawing entry points rather than quantizing the finished frame. Covers, artwork, avatars, clock artwork, reading backgrounds and font/icon antialiasing retain their existing paths. Acrylic shelf guards retain blur, tones and translucency, including the existing fast-mode dither. Drawing that must retain gray uses raw interfaces rather than the control palette.

快捷栏使用不透明白底；前光胶囊为黑底白填充，图标位于固定白色圆盘内。拖动和松手走 CAPSULE 的完整 DU 差分（复用 default，20相），脏掩模外扩1行和1列字节，不追加松手定稿。展开覆盖新旧层高的并集；收起重画底页后，通过 RESTORE 整屏全像素归一。归一表复用既有37相，对最终黑色像素统一18相擦白及18相推黑；只有当前显示阅读背景正文时才使用 HOLD_GRAY 保护未变化的图片灰阶，其他页面完整归一。退出阅读到主页面，包括同页退回书架，使用整屏GC16。刷新失败保留可靠全刷恢复。三色与软件验证不保证光学无残影，须实屏验证累计拖动、开合、图片与背景正文。
The quick layer uses opaque white; frontlight capsules use black tracks, white fills and icons on fixed white disks. Dragging and release use CAPSULE complete DU differences (the existing 20-phase default table), with one row and one column byte of dirty-mask padding and no release settle pass. Opening covers the union of old/new heights. Closing repaints the underlay and uses RESTORE for whole-screen full-pixel normalization. The existing 37-phase table gives final black pixels the same 18 erase and 18 black pushes; only a visible reading body with a background requests HOLD_GRAY to protect unchanged picture grays, while other pages use full normalization. Leaving the reader for a main page, including the shelf in the same app, uses whole-screen GC16. Failed output retains reliable full recovery. Neither the palette nor software checks prove optical ghost removal; verify repeated drags, open/close, images and background reading on the panel.

## 波形目录与依赖 / Catalog and dependencies

| 组 / Group | 编号对应的诊断名 / Diagnostic names |
| --- | --- |
| 基础 / Base (9) | default, full, gray8, follow, text-base, capsule-drag, capsule-white, capsule-gray, apply |
| 主页 / Main (7) | main-gray, shelf-exit, main-water, main-bw, main-normal, shelf-gl16, shelf-gc16 |
| 阅读 / Reader (6) | reader-text, reader-ink, reader-water, reader-background, reader-image, reader-comic |
| 归一 / Normalize (2) | normalize, normalize-gray |

gray8、capsule-white、capsule-gray 是保留的备用表；apply 是错相逐带执行壳，设置主列表定点补白也复用其单相 DU。目录不因为无当前产品调用而删除表，也不合并光学目的不同的派生表。
gray8, capsule-white and capsule-gray are reserved; apply is the staggered-band execution shell; settings rule cleanup also uses its one-phase DU. The catalog neither removes unused product alternatives nor merges derivatives with different optical purposes.

`parent` 是首要基底，并非完整依赖图。`reader-background` 和 `reader-image` 都从无背景的 `reader-water` 裁出。为应对背景翻页的残影反馈，背景表黑→白连续推白 21 拍，灰→白恢复从主页水波纹借来的单向序列，保留 29 相及末尾 3 相保持；独立插图表继续使用无背景 `reader-water` 白列。黑白闪与擦白余量须实屏确认。归一以 `reader-ink` 为基底。目录 getter 不隐式读取背景设置，原低层兼容 getter 保留设置和回退规则。
The parent field identifies the primary base, not every dependency. Both `reader-background` and `reader-image` are trimmed from the plain `reader-water` table. In response to background-turn ghosting reports, the background table uses 21 consecutive white pushes from black and restores the one-way gray-to-white sequence from main-page water, retaining 29 phases and three trailing holds. The separate illustration table keeps the plain `reader-water` white column. Flashing and whitening margin require panel validation. Normalize builds on `reader-ink`. Catalog getters do not read background settings implicitly; compatibility getters retain their settings and fallbacks.

描述查询不分配、不初始化。get/inspect 可能触发原有惰性构造；分配失败或缺模式时保留原构造器的回退。inspect 统计实际返回表的相数、相等灰度对驱动和黑白端点推动次数，不能证明拿到了描述中期望的派生表，也不能据此推断光学效果。未被选用的阅读/归一表不再因页面预选指针而提前构造，分配时机可能晚于旧实现。
Descriptor queries neither allocate nor initialize. get/inspect may trigger existing lazy builders and preserve their allocation/missing-mode fallbacks. inspect reports the resolved table's phases, equal-gray activity and endpoint pushes; it does not prove the intended derivative was constructed or establish optical quality. Unselected reader/normalize tables no longer build merely because a page preselected a pointer, so allocation can occur later than before.

当前基础波形只有一个温度档，显示出口固定传入 25°C，目录检查与原 getter 一样读取第一个档；它没有新增温度传感器或温度补偿。未来引入多温度档必须同时修改选择与检查契约。
Current base waveforms have one temperature range. Display passes a fixed 25°C, and catalog inspection reads the first range like the existing getter. This adds neither sensing nor compensation. Multiple ranges require coordinated selection and inspection changes.

## 清理周期与扫描 / Cleanup and scan timing

`display_refresh_config.h` 集中既有阈值：系统整页 40 次、主页内容 12 次、非普通子页每 3 次全像素 GL16；阅读缺省 15 页，仍支持原来的 0/5/10/15/30 设置。局部按钮/弹窗不累计系统整页清理；正文计数、手动清理和成功提交规则仍在原模块。
display_refresh_config.h centralizes existing thresholds: 40 system pages, 12 main-content updates, and full-pixel GL16 every third nonordinary subpage. Reader defaults to 15 turns and retains 0/5/10/15/30 settings. Local controls/dialogs do not advance the system-page cadence. Original modules retain reader counters, manual cleanup and successful-commit rules.

FOLLOW DU 与胶囊拖动选择 FAST 扫描档，其余保留 FULL。FAST/FULL 是时序配置名，不能直接推断物理帧时长。S31 默认 20 MHz、8-bit 总线下，两档当前均求解为 16 μs/行、11,008 μs/相；这是板级公式结果，不是实测刷新耗时。错相翻页还要计入带数、错相节拍、计算与供电开销。
FOLLOW DU and capsule drag select FAST; other outputs retain FULL. These are timing profile names, not latency guarantees. With the current S31 20 MHz 8-bit bus, both solve to 16 microseconds per line and 11,008 per phase. These are timing calculations, not measured refresh latency. Staggered turns also include band offsets, ticks, computation and power overhead.

OTA/display 递归互斥、供电检查、成功后提交 back buffer、失败后重建真实基准、欠载降频恢复继续由原物理出口负责。新策略不绕过这些出口，不提前确认一帧成功。
Existing physical exits retain recursive OTA/display exclusion, power checks, success-only back-buffer commits, baseline reconstruction after failures and underrun frequency fallback. Policy never bypasses these exits or confirms success early.

横屏阅读固定使用普通翻页，丢弃换向前等待输出的水波纹请求；保留普通正文、阅读背景、插图以及周期/手动全刷出口。效果设置不写回，竖屏阅读重新使用所选效果；主页水波纹与板级扫描时钟策略不变。
Landscape reading always uses ordinary turns and discards any queued pre-rotation ripple. Ordinary text, reading backgrounds, illustrations and periodic/manual cleanups keep their existing paths. Do not rewrite the preference: portrait uses the selected effect again, and main-page ripple and board clock policy are unchanged.

## 维护与验证 / Maintenance and verification

新增场景先增加意图/属性和固定期望测试，再迁移调用方。新增波形登记来源、用途、基底、构造器回退与实屏依据。产品层禁止直接引用表、派生 getter 或带波形参数的原始出口，也禁止包含内部策略/目录头；工厂 VCOM 路径保留原有低层例外。
Add an intent/attribute and fixed expected tests before migrating a new scene. Register each waveform's source, purpose, base, fallback and panel evidence. Product layers must not reference tables, derivative getters, raw waveform-taking exits or internal policy/catalog headers. Factory VCOM calibration retains its original low-level exception.

```sh
python3 tools/run_reader_baseline.py refresh present quick loop lock navigation display ripple architecture
python3 tools/run_reader_baseline.py
idf.py --preview build
```

refresh 专项覆盖 24 表身份与实际驱动统计、1,280 组策略输入、唯一出口、面积/膨胀、错误透传、无效请求和页面呈现边界。结构检查包含拒绝反例。测试中的目录/硬件替身与真实策略分开，阅读/快捷栏原有输出断言继续有效。
The refresh suite covers 24 table identities/statistics, 1,280 policy inputs, one-exit dispatch, bounds/padding, errors, rejection and page presentation boundaries. Structural checks include rejecting fixtures. Catalog/hardware doubles remain separate from real policy, retaining existing reader/quick-layer output assertions.

波形合并或改参数前，用同一设备、温度、VCOM、电源和固定图案，对比黑白文字互换、16 灰阶、背景正文、插图、时钟、输入候选、快捷栏开合。记录残影、白底回灰、字芯黑度、闪烁及实测延迟，包含多次累积和睡眠/失败恢复。宿主与编译结果不能代替这一步。
Before merging or retuning waveforms, use the same device, temperature, VCOM, power and fixed patterns to compare black/white text reversals, 16 grays, background text, illustrations, clocks, input candidates and overlay open/close. Record ghosting, white drift, glyph darkness, flashing and measured latency, including repeated accumulation and sleep/failure recovery. Host tests and compilation do not replace this step.

## 高压生命周期 / High-voltage lifecycle

高压生命周期由 epdiy 底层统一管理。产品显示出口只在真实扫描前上电；帧缓冲绘制、图片解码、波形准备、无变化返回均不提前上电。完整显示组通过 `display_present_begin/end()` 配对，最外层结束即主动关电；阅读正文、页脚和顶部属于同一组，内部子输出不逐段关电。显示组可以嵌套，但不会持供电锁等待准备工作，也不会延长空闲期限。
The epdiy layer owns the HV lifecycle. Product display paths power on only before physical scans; framebuffer drawing, image decoding, waveform preparation and unchanged updates do not power on early. Pair complete display groups with `display_present_begin/end()`; the outermost end immediately requests shutdown. Reader body, footer and header share one group without power cycling at each sub-output. Groups may nest but neither hold the power mutex during preparation nor extend idle deadlines.

独立的微秒绝对定时器唤醒高优先级任务，在最后一次物理扫描结束后100ms发起空闲关断，作为显示组内停顿或直接使用底层输出时的兜底。普通更新、清屏盲推、开机、锁屏和出厂测试共用这条路径；应用循环、图片解码、书籍加载和OTA显示锁不参与计时。独立显式上电而没有扫描也有100ms期限；重复上电请求、无变化刷新、失败返回和过期通知不延后已有期限。
An absolute microsecond timer wakes an independent high-priority task to request shutdown 100 ms after the last physical scan, covering pauses inside groups and direct low-level output. Normal updates, blind clearing pulses, boot, lock screens and factory tests share this fallback. The application loop, image decoding, book loading and OTA display lock do not own the deadline. Explicit standalone power-on without a scan also expires after 100 ms; repeated on requests, unchanged updates, failed draws and stale notifications do not extend an existing deadline.

实际扫描与关断共用专用递归锁，扫描进行期间不截断波形。下一次扫描在锁内重新确认供电；扫描结束立即通过GPIO撤MODE/XOE，再开始空闲计时。关断命令失败时保留“可能带电”状态，5ms后重试；成功仅表示扩展口接受了关断写入。关断未确认时开始新刷新，须先确认关闭并重新完整上电，不复用不确定供电。马达与供电位使用原子状态，获得I2C事务锁后才形成OUT0快照，防止排队的马达写入恢复过期供电位。
Physical scanning and shutdown share a dedicated recursive mutex so active waveforms are never cut off. Each new scan ensures power under that lock; completion immediately lowers MODE/XOE through GPIO and starts idle timing. An unconfirmed shutdown retains the possibly-powered state and retries after 5 ms. Success means the expander accepted the shutdown write. A new scan after unconfirmed shutdown must confirm off and fully repower, rather than reuse uncertain power. Motor and power bits use atomic state, and OUT0 is sampled only after taking the I2C transaction lock so queued motor writes cannot restore old power bits.

板级关断顺序为：GPIO拉低XOE/MODE，等待至少12µs，再发布并写入VCOM/PWRUP关闭位，等待原有2ms，最后关闭WAKEUP。供电关闭位在12µs等待后才可被马达等共享扩展口写入者看到，避免并发写入提前断轨。VCOM参数和波形数据不由该机制修改。
Board shutdown lowers XOE/MODE through GPIO, waits at least 12 µs, publishes and writes VCOM/PWRUP off, waits the existing 2 ms, then disables WAKEUP. Shared expander writers such as the motor see the off bits only after the 12 µs wait, preventing concurrent writes from disabling rails early. This mechanism does not alter VCOM parameters or waveform data.

设置模块写事务及联网/TF OTA在Flash写入前调用 `epd_flash_begin()`：确认关电后持供电锁直到 `epd_flash_end()`，期间新扫描等待。确认失败则禁止写入并返回失败；仅成功的begin需要同任务配对end。锁顺序固定为显示/OTA锁→供电锁→I2C，不得持供电锁再请求显示锁。初始化前允许调用，但屏幕初始化/销毁必须与写入串行；保护不覆盖所有其他模块的NVS或系统内部Flash写入。
Settings write transactions and online/card OTA call `epd_flash_begin()` before flash access: confirm shutdown and hold the power mutex until `epd_flash_end()`, blocking new scans meanwhile. Failed confirmation forbids writing and returns failure; only a successful begin is paired with end on the same task. Lock order is display/OTA -> power -> I2C; never request the display lock while holding the power mutex. Calls before display initialization are allowed, but initialization/teardown must serialize with writes. This protection does not cover every other module's NVS or internal system flash writes.

100ms是空闲关断的软件期限，不能等同于实际高压已在100.000ms降到零。RTOS调度、共享I2C占用、总线故障和未接入保护的Flash操作仍可能造成延迟；诊断开关 DISPLAY_POWER_DIAG 记录期限、处理开始、完成和延迟，显示组提前关电时延迟可以为负值，均不能替代引脚或电压测量。若需要故障条件下也有严格电气上限，现有I2C扩展口控制路径需要独立硬件关断支持。
The 100 ms idle deadline is a software limit, not proof that physical voltage reaches zero at exactly 100.000 ms. RTOS scheduling, shared I2C contention, bus faults and unguarded flash operations can still delay it. DISPLAY_POWER_DIAG records deadlines, processing start, completion and lateness; early group shutdown can produce negative lateness. Logs do not replace pin or voltage measurements. A strict electrical limit under faults requires independent hardware shutdown support beyond the present I2C expander path.

## 显示与触摸坐标 / Display and touch coordinates

显示使用面板默认居中位置，不做软件平移。屏内触点只随阅读方向旋转，屏外三键坐标不变。旧固件的本机偏移不参与启动、解锁或备份恢复；清理规则见[持久化契约](../guides/PERSISTENCE.md)。启动仍在供电守卫内建立白底，再呈现开机图；不得将旧锁屏图当作差分基准。
The display uses the centered panel coordinates without software translation. Only on-panel contacts rotate with reading orientation; physical keys stay fixed. Legacy offsets never affect boot, unlock or restore; see the [persistence contract](../guides/PERSISTENCE.md). Boot still establishes a white baseline under the power guard before presenting the splash, never using cached lock art as a differential baseline.
