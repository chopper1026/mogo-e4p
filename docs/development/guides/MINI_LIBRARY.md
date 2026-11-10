# 迷你书库 / Mini library

首页组件库可把「迷你书库」放在上方或下方等高区域。最多一个实例，最多展示五本书；选择另一区域已有的组件时交换位置。它与「最近阅读」续读组件可以同时使用。
The Home component library can place one mini library in either equal region, showing at most five books. Selecting an existing component swaps regions. The original resume component can coexist with it.

## 使用 / Interaction

- **随机**：从可见书架无放回抽取最多五本。「换一批」先合拢书匣，再挑选并展开；优先取上一批之外的书，数量不足才复用旧书。
- **最近阅读**：只选存在有效进度的书，按持久阅读顺序从近到远排列；收藏不影响顺序，不用未读书或文件修改时间补足数量。
- 点侧边封面、下方编号或左右划动，选中的书转到中间；再点当前封面打开原书。下方编号再次点选仅保持选择。书名使用整行宽度，作者/进度另起一行；随机模式右侧以刷新图标配合「换一批」。
- 长按区域进入组件配置；底部导航始终可用于离开首页。扫描时也允许导航取消。
- 空书架与无阅读记录分别显示空态。源读取失败会保留上次完整书匣并提示；源截断或目录内存不足时不使用部分扫描结果替换书匣。

Random selection samples without replacement and maximizes novelty on manual redeals. Recent mode uses only valid progress records in persistent reading order, independent of favorites and file modification times. Tap a side cover/index or swipe to select; tap the selected cover to open the original book. Give the title a full-width line and author/progress their own row, with an icon and redeal label on the right in random mode. Holding the region opens configuration. Empty and failed-source states are explicit; incomplete scans preserve the previous complete batch.

## 数据与生命周期 / Data and lifecycle

书源与书架共用 `book_shelf_scan` 的递归目录、支持格式、隐藏记录和历史补入规则。只使用已挂载的来源：首页不触发内置卷首次格式化。书目完整快照只在扫描期间持有，之后仅保留随机与最近各五条。移出书架、改书名、进度与书源版本变化在返回首页时更新，失卡立即剔除卡上条目；隐藏项不会被历史重新补入。
Sources reuse shelf traversal, supported formats, hidden records and history backfill, using only mounted stores without triggering first-use formatting. The full catalog exists only during scans; retain ten result records afterward. Source/title/visibility/progress changes refresh on Home entry. Media loss immediately removes card-backed entries and history cannot reintroduce hidden books.

随机批次和各模式的当前选择在同一次运行的切页、阅读返回和模式切换之间保留；源更新只替换失效项、补齐空位及更新元数据。重启后重新抽取，不把随机批次写入NVS。组件布局和模式使用现有 `read_pico/home_regions` 一次事务保存，并随设置备份恢复。
Keep the random batch and focus across navigation, reading returns and mode switches during one run. Reconcile source changes in place, replacing only missing members. Reboot resamples without persisting a batch. Layout and mode share one atomic `read_pico/home_regions` transaction and participate in settings backups.

旧布局继续使用原来的 `0x1?` 两位编码。含迷你书库或最近模式时用v2编码：最高位置位；bits0–2和3–5是上下组件；bit6为最近模式。同类非空组件不重复，留白可重复。仅接受规范的42个字节值；旧备份仍能恢复，当前备份版本v39及尾块长度不变。新迷你布局不保证被旧固件识别。
Legacy layouts keep their two-bit `0x1?` encoding. V2 sets bit7, uses bits0–2/3–5 for the slots and bit6 for recent mode. Only 42 canonical values are accepted, with unique nonempty widgets. Legacy backups remain readable, retaining v39 and existing tail lengths. Older firmware may not recognize a mini-library layout.

## 绘制与资源 / Drawing and resources

`book_mini_library` 负责无IO选书；`home_library` 持有快照与封面、执行可取消扫描；`ui_mini_library` 只绘制和命中；首页控制模式、开书、动画和呈现。绘制不分配、不读文件、不改设置。逆向定点旋转与命中使用相同几何，先命中最上层封面。
The model selects without IO, the service owns snapshots/covers and cancellable scans, and the UI only draws/hit-tests. Home owns actions, animation and presentation. Paint performs no allocation, file IO or persistence, sharing inverse fixed-point geometry with topmost-first hit testing.

首页统一拥有封面队列代次，续读使用槽0，迷你书库使用槽1–5。结果交回后由服务拥有，最多五张176×240灰阶或对应1位点阵。离页取消队列任务并保留有效封面，返回首页检查文件后直接复用；阅读进度更新、最近阅读重排及切模式时，按路径、尺寸和书名/作者匹配保留重叠书目的像素。文件修改时间、大小或元数据变化只清理对应旧图，换批清理不再展示的封面；格式/对比度变化及失卡清理缓存。低内存、解码或分配失败使用纸页占位及下方真实书名，保持可选可读。
Home owns the shared cover generation: resume uses slot0 and the library uses slots1–5. Keep at most five 176×240 gray/1bpp images across page exits, cancelling pending jobs. Validate files on return and reuse matching path, size, title and author identities across progress updates, recency reordering and mode changes. Invalidate affected images for file modification/size/metadata changes, discard departing batch members, and clear caches on format/contrast changes or media loss. Low memory or decoding failures retain usable placeholders and real titles.

选书采用3个等距位置帧（进度85、170、256），合拢和展开各2帧；每次成功呈现后才推进，失败重试当前帧。调度间隔下限40ms，实际节奏取决于同步推屏耗时。新的选书操作立即从当前选中书开始切换，不排队等待旧动画；触摸保持期间暂停动画轮询，抬手后继续。封面按目标扇列的层叠顺序从两侧向中间绘制，绘制与静止命中共用几何。
Selection uses three equally spaced positions (progress 85, 170 and 256); closing/opening use two frames each. Advance only after successful presentation and retry a failed frame. The minimum scheduling interval is 40ms and synchronous panel time determines actual cadence. A new selection starts immediately from the currently selected book without queuing behind old motion. Pause animation polling while a touch is held and resume after release. Paint covers from the sides toward the selected center using the target deck's layer order and shared drawing/hit geometry.

每帧完整绘制组件，动态封面使用黑白并提交 `MOTION`，书名等信息随当前选择更新。最终灰阶帧以 `CLEANUP` 对整个组件执行完整GC16清残影，包含模式下划线、编号、书名/作者及底部操作；定稿时组件会短暂闪动。底部文字和操作位于导航全像素清理安全边界之上，区域末24行留白。静止没有循环动画或周期目录扫描。锁屏、离页和失卡取消动画。设备的实际动画速度、触摸及残影效果仍须实屏验收，宿主测试和构建不能替代。
Redraw the full widget each frame, using monochrome covers for `MOTION` and updating book details with the current selection. Follow motion with full-waveform GC16 cleanup across the whole widget, including all labels, indicators and actions; this briefly flashes the region. Keep changing ink above the navigation cleanup boundary, leaving the final 24 rows blank. There is no idle animation or periodic catalog scan. Lock, exit and media loss cancel motion. Actual panel speed, touch and ghosting require hardware acceptance.

## 验证入口 / Verification

```sh
python3 tools/run_reader_baseline.py mini_library home_library mini_library_ui dashboard_home home_recent home_cover_cache reading_origin settings_backup weather management routing loop
python3 tools/mini_library_ui_host_test.py --output build/mini-library-preview
idf.py --preview -B build/mini-library build
```

原生4bpp预览与sanitizer检查覆盖两区域、0–5本、全部焦点、封面格式、动画与命中；模型另覆盖10000本、换批避重、焦点/元数据保持和失卡。设备验收另行进行。
Native 4bpp previews and sanitizer checks cover both regions, 0–5 books, every focus, cover formats, motion and hit tests. The model additionally covers 10000 books, novelty, focus/metadata retention and media loss. Hardware acceptance remains separate.
