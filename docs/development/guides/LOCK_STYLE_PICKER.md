# 壁纸 / Wallpaper

设置 → 个性化功能 → 壁纸提供「待机壁纸」「关机壁纸」两个页签；切换页签不修改任何已保存选择。待机壁纸将壁纸、阅读票根、书架拼贴、透明书封、日历、简约时钟和书籍封面放在同一页。上方黑色边框表示正在浏览的分类，勾选表示已启用的样式；下方直接选择图片、票根背景或日历版式，保存成功才更新勾选。壁纸、透明书封和日历分类点击仅展开选项，不替换当前样式。票根分类使用记住的背景，拼贴、时钟和书籍封面点选即启用。

浏览壁纸或透明书封时，标题右侧可选择「裁剪填满」或「完整显示」。裁剪填满按屏幕比例铺满并裁掉超出边缘的部分；完整显示保持图片原比例，缩放到屏幕内并在四周留下当前页面，适合高分辨率或尺寸较大的图片。两种显示方式分别保存，切换图片或重启后仍保留；壁纸旧设置默认使用裁剪填满，透明书封默认完整显示以保持既有行为。

自定义待机和关机壁纸在生成 16 级灰阶前会压深中间调，重点拉开高亮和浅灰之间的层次，并用有序抖动保留相邻灰阶之间的细节；纯黑、纯白和完整显示的白边不变。待机壁纸、缩图和密码背景的旧成品缓存会自动重建。已保存的旧关机壁纸仍能显示，不需要原图；旧缓存只在显示前调色，不改写 TF 卡；对已经过初版调色的成品只补偿浅灰，不重复压深中间调。原图还在时，重新选图可在量化前调色，保留更多浅灰细节。已经是纯白的原图区域无法恢复细节。

Custom standby and shutdown wallpaper deepens midtones and widens the separation of near-white tones before ordered 16-level dithering, keeping pure black, pure white and white fit margins unchanged. Old standby, thumbnail and PIN backdrop caches rebuild automatically. Existing shutdown art still works without its source; validated legacy pixels are toned in memory without rewriting the card. Art rendered with the first curve receives only the added highlight correction, not a second midtone adjustment. Reselect the source, if available, to apply the adjustment before quantization and retain more near-white detail. Detail already clipped to white in the source cannot be recovered.

壁纸与书封每页三张图，点左右箭头或在图片区滑动翻页。点尚未加载的图片会显示「加载后应用」，切页、切分类、再选别图、离页或锁屏取消旧选择。长按图片可放大查看，返回保留分页，不自动启用。坏图、无卡和保存失败保留原选择。系统字号100–200%参与排版，卡片文字受区域大小限制，长文件名截短。

目录沿用 `/sdcard/pictures`、`/sdcard/images`、`/sdcard`，扫描全部 JPG/JPEG/PNG，不设64张固定上限并按文件名排序；目录数组按可用PSRAM动态扩展，内存不足时返回已经成功扫描的项目。透明书封列出 PNG/JPG/JPEG。PNG保留原alpha；JPG/JPEG和不透明PNG按画布四角的主灰度估计纯色背景，再只从四角沿四邻接移除连通区域，因此黑色、灰色和彩色纯底也能透明，封闭的同色主体和只贴边不贴角的白色主体会保留。含真实alpha的PNG跳过颜色键控。由于设备最终显示灰阶，彩色背景按解码后的亮度处理；渐变或复杂纹理不承诺自动抠图。跳过隐藏文件、AppleDouble、目录、空文件、超过50 MiB的文件。透明书封可选择保留完整画布或按屏幕比例居中裁切；缩图棋盘代表透明区域，实际锁屏仍叠加当前页面。显示方式写入NVS，图层缓存按图片、旋转、画布尺寸、显示方式和键控算法版本区分，算法更新后自动重建旧缓存。阅读票根预览使用真实最近阅读数据，优先读取完整封面缓存，缺失时读取列表封面缓存并居中，不在锁屏入口解码EPUB；无记录显示占位。拼贴预览为上次生成且校验有效的整帧快照，不为浏览设置扫描或解码整个书库；首次锁屏前可无预览。日历与时钟预览使用打开时的当前日期/时间，实际锁屏仍按各自现有规则生成。

## 书籍封面 / Book Cover

在待机或关机页签点「书籍封面」即可启用，两页独立保存。显示当前或最后阅读的书的封面，不叠加书名、作者或书摘。点右上「裁剪填满」可按原比例居中裁切铺满屏幕；「完整显示」保留整张封面，为默认选项。取景与自定义图片互不影响。顶部卡片和下方详情共用当前取景的真实缩图；已准备的缩图可跨页签复用，与其他缩图同批定稿。封面不可用时显示「暂无封面」，不借用书架拼贴或系统默认的预览。关机提示仍可独立开关，开启时显示在屏幕底部中央。

Select Book Cover on either tab to enable it independently for standby or shutdown. The current or last-read cover has no title, author or excerpt overlays. Use the top-right controls to center-crop to fill the screen or contain the entire cover (default); both preserve its aspect ratio and are independent of custom image framing. The card and detail panel use the selected framing, reuse prepared thumbnails across tabs and settle with the other thumbnails. Missing covers show their own unavailable state. The optional shutdown notice remains at the bottom center.

EPUB/MOBI 优先读取书内封面；灰阶解码限制为 560×1000（完整显示）或一屏 684×1216（裁剪填满），回退到已有完整比例封面缓存或生成封面（如 TXT）。读取前后核对书源；无最近记录、书源消失或准备失败时，待机显示内建锁屏，关机显示系统默认，不修改选择。PIN 冷启动重新读取封面，不沿用上次的密码背景。第一张真实封面可能需要读卡和解码。

EPUB/MOBI prefer embedded artwork, with grayscale output bounded to 560×1000 (contain) or one 684×1216 panel (crop to fill), then an existing proportional thumbnail or a generated cover (such as TXT). A missing/changed source or preparation failure uses the built-in standby/shutdown face without changing the saved choice. Cold PIN boot rebuilds the cover instead of reusing an old frame. First display may require card I/O and image decoding.

## 独立关机壁纸 / Independent Shutdown Art

「关机壁纸」提供壁纸、阅读票根、书架拼贴、日历、书籍封面和系统默认六种样式，不再提供留白；旧留白配置自动迁移为系统默认。系统默认显示「mogo 已关机」及开机提示。其余样式可开关关机提示，开启时在底部居中显示 36 px 单行提示，避开拼贴右下角书籍数量及其描边。使用与待机壁纸分别保存的选择。票根背景可选纯白或封面背景，日历可选老黄历或万年历，两项均不修改待机页设置。关机时使用最新阅读记录、书架封面或当前日期生成；无可用素材或生成失败时使用系统画面。拼贴预览沿用上次成功生成的快照。
The shutdown tab offers custom wallpaper, reading ticket, shelf collage, calendar, book cover and the built-in face. The optional 36 px single-line bottom-center notice leaves the collage count and outline untouched. Retired white settings migrate to the built-in face. Ticket backgrounds and calendar variants are saved independently of standby art. Shutdown generates current content and falls back to the built-in face if unavailable; collage previews use the last valid snapshot.

自定义壁纸可选裁剪填满或完整显示；完整显示按原比例缩放、四周留白。浏览分类不启用，点选图片且TF缓存和设置保存成功后才应用；票根和日历在下方点选选项后启用，拼贴、书籍封面和系统默认点选即启用。更换图片、翻页、长按预览、切换页签、离页或失卡取消尚未提交的选择；失败保留原选择。
Custom art supports independent crop/contain framing. Browsing does not enable it; completed image preparation and a successful save apply it. Ticket and calendar require an inline option; collage, book cover and the built-in face activate directly. Navigation and media loss cancel unpublished work; failures retain the prior selection.

电源菜单关机和待机方式「彻底关机」直接显示关机画面；仅浅睡继续显示原锁屏，定时浅睡在10/30/60分钟到期后切换关机画面再断电，提前唤醒取消本轮倒计时。时钟在浅睡中更新，关机后使用静态关机图；重启沿用重启画面。关机画面推屏失败不执行断电；断电交接失败时，菜单关机返回可操作原页，锁屏/定时入口恢复受密码保护的锁屏等待，不因超时解锁。
Menu shutdown and immediate-off standby present shutdown art directly. Light sleep retains its lock; timed sleep switches to shutdown art at expiry before powering off, while early wake cancels that countdown. Restart retains its own face. A failed present blocks power-off; failed handoff restores the current page or protected locked waiting, never an unlocked timeout.

关机画面准备好后先物理清白，再从白底以完整48相GC16整屏全像素输出。待机壁纸上屏策略不变。/ Once shutdown art is ready, physically clear the panel, then present the art from white with complete 48-phase GC16 and full-pixel coverage. Standby wallpaper presentation is unchanged.

`services/shutdown_wallpaper` 使用独立可取消的优先级1、12 KiB任务，将所选 JPG/JPEG/PNG准备成684×1216、4bpp标准竖屏画布。解码输出不超过一屏像素，工作内存通过内存管理器分配。只写非活动TF槽 `/sdcard/.readpico/shutdown/0.bin` 或 `1.bin`，完整写入、同步并关闭后才让UI提交单个NVS配置 `read_pico/off_art`；两槽总占用约813 KiB，不写机内图片缓存、不改变分区。失败、取消或NVS提交失败不覆盖活动图片。读取核对尺寸、模式、图片身份、精确长度及像素校验和，损坏、缺失或内存不足时使用系统关机画面。
An independent cancellable worker prepares a canonical portrait frame and writes only the inactive SD slot. The UI publishes one NVS configuration after a complete synced write. Two slots use about 813 KiB on the card, with no internal image writes or partition changes. Exact length, identity, framing and pixel checksums protect reads; absent/corrupt caches or allocation failure use the built-in face.

关机图片在读取或准备新图前检查TF卡是否存在且已挂载；不自动重挂、格式化或回退到机内存储。目录仅在写入时按需创建，读取不创建目录。保存失败按TF卡未就绪、空间不足、目录不可用、内存不足、源图读取/解码、写入/同步或设置提交分别提示，并保留已保存选择；后台记录失败原因用于设备诊断。
Before loading or preparing art, check that the card is present and mounted; never remount, format or redirect images to internal storage. Create cache directories only for writes. Distinguish card readiness/space, directory access, memory, source decoding, write/sync and settings failures while retaining the saved choice; worker warnings support device diagnosis.

自定义图片保存后，重启可从同卡缓存恢复，关机前直接读取成品，不重复解码原图；原图删除不影响已经保存的成品。拔卡、换入没有匹配缓存的卡或缓存损坏时使用系统关机画面，不改已保存选择；恢复可用缓存后继续显示原图。票根与拼贴生成可读取TF上的阅读素材，日历使用本机时间。切换裁剪方式须重新读取原图；原图缺失时保留先前画面及方式。缩图可从TF画布后台恢复，绘图回调不读文件。USB、重挂和睡眠通过 `storage_jobs_quiesce` 先收齐关机图片任务。旧开发版机内缓存不再读取或迁移，更新后重新选图保存到TF卡；旧机内文件不由此服务自动删除。v47设置备份格式不变，包含选择、取景、票根背景、日历版式、提示开关、源路径和缓存身份，不包含像素；恢复后缓存不匹配时回落系统画面，重新选图可重建。
Saved custom art reloads from the same card after reboot and uses its prepared frame at shutdown without decoding the source again; deleting the source does not invalidate saved art. Missing, mismatched or corrupt card caches use the built-in face without changing settings and recover when the valid cache returns. Ticket/collage may read card sources; calendar uses local time. Reframing needs the source, and failure retains prior art and fit. UI painting remains file-free and storage handoffs join the worker. Old development-only internal caches are neither read, migrated nor automatically deleted: reselect once to save on SD. Settings backups omit image pixels; absent/mismatched pixels fall back safely until reselected.

## 加载与缓存

`services/lock_style_images` 独占目录扫描、图片文件读取、缓存与解码工作；一个优先级1、12 KiB栈任务，不调用字库、不写显示缓冲。UI事件只提交请求，`tick`接收结果，`render`只绘制已有像素。每批先探测缓存再解码缺图，当前三张优先，已选分类缩图次之，下一页最多三张最后预读；大预览为单独按需请求。

后台结果不逐张推屏。当前可见文件全部成功或失败、内置样式准备结束后，设置页只返回一次 `APP_REDRAW_FULL`，由主循环按产品均衡配置整屏GC16清刷，与手动刷新共用出口，包含文字、状态栏和底栏。下一页预读不参与等待，也不触发刷新。准备过程继续接收输入；用户主动换分类、翻页或点选仍即时响应。命中全部缓存且内容没有变化时，重新进入只绘制进入页面所需的一帧，不追加加载刷新。

- 缩略图160×284、4bpp，每张22,720字节；内存LRU最多12槽，内置样式另保留最多7张同尺寸小图。普通离页保留已完成小图与目录，重新进入立即绘制，再由后台核对源文件大小和mtime；没有变化不重读像素、不重新解码。目录项按项目增加对应的PSRAM占用，不再受64项目录上限约束。大预览解码342×608、4bpp，显示放大，最多保留一张，离页释放大图和背景工作区。
- 内置样式按字体、字号、对比度及内容身份失效；票根跟随阅读数据代次和日期，日历跟随日期，时钟跟随分钟，拼贴跟随最近成功保存的快照代次。重进时先显示保留的小图，仅重建变动项。文件任务只发布新结果，旧像素由UI取回目录并检查身份后释放，避免绘图借用悬空。
- TF缓存为 `/sdcard/.readpico/thumbnails/000.bin`–`127.bin`，总上限约2.78 MiB及一个临时文件。直接映射冲突会重建，不会取错图。
- 缓存头校验路径、文件大小、mtime、画布尺寸、书封模式、算法版本、精确载荷长度和内容校验和；解码前后重新核对源文件身份。写入先临时文件，完整关闭后替换目标；TF只读或写失败仍显示已解码像素。切换透明书封的完整显示/裁切填满会使用不同缓存项，不会复用错误取景。
- JPEG缩放解码、PNG逐行解码，只输出目标大小；不创建源图大小的像素平面。透明书封的JPG直接写入灰阶图层并使用不透明alpha，不生成中间PNG。PNG读取/行循环、JPEG读取/MCU及渐进式JPEG读取/seek检查取消，不影响其他使用默认解码入口的调用方。
- 每轮请求有独立代次；取消结果不发布。离页与锁屏暂停队列但保留已完成小图；介质丢失、重新就绪和USB交接使缓存失效，即使当前不在选择器也会推进缓存代次。`storage_jobs_quiesce`在USB、重挂、睡眠/关机前等待实际文件句柄交还，超时交回原有失败路径，不强杀任务。

第一次浏览未缓存的大图片仍需要读卡和解码。PNG文件即使输出小图也需要处理压缩行；缓存不能消除第一次读图成本。内存缓存跨页面复用，重启后文件缩图从TF缓存恢复；内置样式重新生成。同路径、同大小且同mtime的外部覆盖无法只凭元数据识别，需删除对应缩略缓存或改变时间/大小。缓存可安全删除，随后自动重建。

## 验证

```sh
python3 tools/run_reader_baseline.py wallpaper_cache shutdown_wallpaper power_dialog lock_cover lock_style_images lock_style_ui image_stream settings_backup lifecycle storage media lock_style sleep_power loop lock lock_clock font_policy
python3 tools/lock_style_ui_host_test.py --output build/lock-style-ui
idf.py --preview build
```

关机回归覆盖独立设置、v47独立生成选项、v46迁移/旧版备份、同卡重启读取、无卡/未挂载/换卡回退、只读/满卡/目录错误、解码中失卡、TF双槽故障保护、解码/写入/同步/内存/任务失败、过时结果取消、坏缓存、关机失败恢复和定时截止点。宿主测试覆盖无IO绘图、跨页面缓存复用且不追加刷新、分批完成只触发一次整屏定稿、不可见预读不刷新、日期/阅读/拼贴身份失效、缓存命中跳过解码、缓存先行、取消/过时代次、损坏和源文件变化、内存/任务失败、有界目录及LRU、透明画布、字号/点击边界、待应用取消和失卡。真实流式测试需要Pillow与libpng（可传 `--png-prefix`）。截图中的书籍、图片、日期和电量可使用确定性样本；宿主耗时不能代替S31+TF卡的首次/缓存翻页时延、触摸响应与墨水屏残影验收。
