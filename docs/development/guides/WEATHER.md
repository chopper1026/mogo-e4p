# 组件库与天气 / Components And Weather

## 使用 / Use

首页分为上下两个等高区域，可分别选择「最近阅读」「天气」「阅读手账」或「留白」。非留白组件各只能出现一次；选择已在另一区域的组件时，两区自动交换，留白可以重复。通过留白区域的设置图标、长按任一区域，或设置页「阅读背景」下方的「组件库」进入配置。天气去除外框和圆角，铺满所在区域并直接衔接底部导航。组件库、天气设置及城市页底栏均仅保留中间的首页按钮，两侧无隐藏返回热区。点「今日 / 明日」本地切换，不额外请求天气。旧组件、旧月阅读记录与闭环目标不恢复；新增手账的独立采集、日历和存储边界见[阅读手账](READING_JOURNAL.md)。

天气底部只保留36像素的刷新图标，不再显示「刷新」文字；图标中心位于区域左侧632像素处，点击范围为64×64像素，避开导航栏的扩展热区；最近阅读右上角的设置图标及点击入口也已移除，天气设置通过长按区域或设置页组件库进入，原底部设置图标及其独立热区移除。

Home has two equal regions, each choosing Reading, Weather, Reading journal or Empty. Real widgets are unique: selecting one already in the other region swaps both choices; Empty may repeat. Open configuration using the settings icon in Empty, a hold on either region, or the library below Reading background in Settings. Weather is rectangular, borderless and edge-to-edge, meeting the footer directly. Configuration, weather settings and city footers retain only Home, without hidden side hit areas. Day switching is local and never submits a weather request. Retired widgets, old monthly records and goals remain removed; the journal guide defines the new independent recording/calendar/storage contract.

The weather footer shows only a 36-pixel refresh icon centered at x=632 within the region, with a 64×64-pixel hit area outside the navigation touch margin. The Refresh label and the Reading settings icon/tap target are removed. Open weather settings with a hold or through the component library in Settings; the old footer settings icon and its independent hit area are removed.

「天气设置」提供独立的「自动刷新」开关，默认开启，正常周期为十小时，关闭后仍可手动刷新、搜索和选择城市。天气须已添加到首页。首页在首帧呈现后且没有新输入 1.5 秒再检查，组件页在非选城界面可检查；已有城市时，设置或文件等非天气主页面空闲 10 秒后也可借用已就绪的联网承载，静默更新缓存，不重绘当前页面。阅读、书架、传书、升级、USB 存储和系统覆盖层避让，锁屏或待机不为天气唤醒。

Weather settings has an independent, on-by-default Automatic refresh toggle, with a normal interval of ten hours. Turning it off preserves manual refresh, search and city selection. The widget must be on Home. Home waits 1.5 seconds after presentation and input; non-city component views may also check. With a selected city, another eligible root page (such as Settings or Files) can borrow a ready link after ten seconds of idle time and update the cache without repainting. Reading, bookshelf, transfers, updates, USB storage and system overlays take precedence. Weather never wakes a locked or sleeping device.

周期从平台原始取数时间起算，重启不延长。服务端提供的有效期更短、缓存缺失或跨日缺少明日预报时会提前补取，不擅自延长有效期。正常十小时周期要求平台天气新鲜期和缓存上限配置为 600 分钟和 10 小时；旧平台或较短配置继续按实际有效期工作。实际请求失败至少冷却五分钟；对时、升级或内存准入繁忙可短暂延期。首页有有效预报时，自动请求不另刷「正在更新」。

Timing starts from the original upstream fetch and survives reboots. Earlier server expiry, missing cache or missing tomorrow can trigger earlier refresh; firmware never extends validity. A full ten-hour cadence requires platform freshness and expiry set to 600 minutes and 10 hours. Older servers and shorter policies still use their actual validity. Actual failures cool down for at least five minutes; busy time sync, updates or memory admission can defer briefly. Home does not repaint a loading state when an automatic request starts with a valid forecast.

尚无城市时尝试平台 IP 城市估计，每次开机最多自动尝试一次，仅作参考。失败后点城市名称，输入中文、英文、拼音或首字母，点「搜索」并选择带市、省行政区的候选；城市页右上方统一提供一个「自动定位」入口。结果每页八项，城市名36px、行政区24px分两行显示，每行84px且整行可点，列表利用输入框至底部翻页区之间的空间；底部左右箭头的点击范围为72×64px，显示总数与页码。杭州可查到城市及 13 个区县，共 14 个地点。进入城市页不展开键盘，点输入框才展开，点空白处收起且保留文字及未确认拼音；展开键盘时暂不显示或响应结果与翻页，收起后恢复。有效搜索和自动定位会收起键盘。城市以官方地点 ID 持久保存，普通刷新不重新定位。支持官方中国 CSV 中的 3577 个地点及其四种 UTC+8 时区，不提供 GPS 或精确定位权限。

With no selected city, approximate platform IP estimation is attempted at most once per boot. Search Chinese, English, Pinyin or initials and select a prefecture/province-qualified result. Eight 84-pixel rows fill the area between the field and bottom pager, each with a 36-pixel name, 24-pixel administrative subline and full-row hit area. Paging arrows use 72x64-pixel hit areas beside totals/page numbers. Hangzhou has fourteen entries including thirteen districts/counties. The keyboard opens on field taps and dismisses without losing input. Results and paging are hidden and inactive while it is open; valid search/location requests dismiss it. The official location ID persists; normal refresh never relocates it. All 3577 official China CSV entries and their four UTC+8 timezones are supported, without GPS permissions.

共用键盘的键间背景、辅助键和首个候选均使用白底黑字，首选候选以双线框区分，候选字号32px。按键按下仍有局部黑白反馈。候选栏累计变化三次并停手400ms后，沿现有局部全像素GL16出口整理词栏；城市页保留这一刷新意图，不降为差分刷新。连续输入、按住及多点触摸期间不整理，不增加整屏清理或修改波形、电源策略。实际清晰度和累计残影仍需实屏验收。

Shared keys, utility buttons and the first candidate use white paper/black text; a double outline marks the first candidate, with 32-pixel candidate text. Press feedback remains local and binary. Three candidate-strip changes followed by a 400ms touch-free pause use the existing full-pixel local GL16 settle, which the city page must not downgrade to differences. Continuous input, held keys and multitouch postpone cleanup, without whole-screen cleanup or waveform/power-policy changes. Optical clarity and accumulated residue still require panel acceptance.

平台以和风官方公开 CSV 建立持久城市字典，城市搜索和英文转中文只查本站数据库，不访问和风 Geo、不消耗和风次数。IP 定位在平台内匹配标准地点并直接返回中文名、ID 和字典坐标；设备不再另发名称修正查询。字典包括市、区县等官方天气地点，不是完整行政区划名录。

The platform persists the official public CSV as a city dictionary. Search and English-to-Chinese mapping query only that database, without QWeather Geo calls or quota use. IP results are canonicalized there and already include Chinese labels, IDs and dictionary coordinates. Firmware never issues a separate relabel request. The dictionary is a weather-location list, not an exhaustive administrative register.

温度为当日最低/最高摄氏温度，以 `/` 分隔；湿度为白天预报湿度，不是实时传感器读数。日出为地平线上的半轮太阳；日落为半空完整太阳及向地面的箭头。缺失事件显示 `--:--`。全区背景区分晴、多云、阴、雨、雪、雨雪混合、雾霾、沙尘和雷雨；雾和霾共用背景，天气文字保留各自原描述。热、冷和未知代码保持中性，不推断天空状况。

Temperatures are daily minimum/maximum °C separated by `/`, and humidity is daytime forecast humidity, not a live sensor. Sunrise uses a half sun at the horizon; sunset uses an airborne sun and downward arrow. Missing events show `--:--`. Region-wide scenes distinguish sun, cloud, overcast, rain, snow, mixed rain/snow, fog/haze, dust and thunderstorms. Fog and Haze share one background while retaining their original condition labels. Hot, cold and unknown codes stay neutral without inferring sky conditions.

按和风[天气现象](https://dev.qweather.com/docs/api/weather/weather-conditions/)映射：`104` 为阴，`404–406` 为雨雪混合，`503/504/507/508` 为沙尘；雾 `500/501/509/510/514/515` 和霾 `502/511–513` 共用雾景。多云、小雨、雪仍沿用原背景，不按雨雪强度另外新增图片。

Following QWeather's [condition list](https://dev.qweather.com/en/docs/api/weather/weather-conditions/), `104` selects Overcast, `404–406` mixed rain/snow and `503/504/507/508` Dust. Fog `500/501/509/510/514/515` and Haze `502/511–513` share the fog image. Cloud, Rain and Snow retain their images, without extra images for precipitation intensity.

雾霾以林梢被低雾带遮挡表现低能见度，不使用全区平行斜线或独立霾图。雷阵雨使用独立云雨构图，闪电主干从云底贯穿到组件最下沿，允许自然分叉，以不规则转折、宽窄变化和内侧亮芯表现放电通道。背景仍沿用固定灰度的1bpp资源，不改变刷新策略；host图不等同于实屏效果。

Fog and Haze use treetops obscured by low mist banks, without region-wide parallel slashes or a separate Haze asset. Thunderstorms have a separate cloud/rain composition and a discharge trunk running from the cloud base to the widget's bottom edge, with natural side channels, irregular turns, varied width and inner bright cores. Backgrounds retain fixed-gray 1bpp masks and existing refresh policy; host previews are not panel acceptance.

## 平台接口 / Platform Contract

`CONFIG_PICO_COMPONENT_API_ORIGIN` 配置可信 HTTPS 平台源，默认 `https://mogo.mantake.shop`，不带路径或末尾斜杠。仅访问该源的 `/api/v2/components` 及天气 `locations`、`auto-location`、`forecast` GET 接口。无 Cookie、设备注册或供应商密钥，不直连和风，不改变 OTA 下载源。

`CONFIG_PICO_COMPONENT_API_ORIGIN` is a trusted HTTPS origin, defaulting to `https://mogo.mantake.shop`, without a path/trailing slash. Only catalog and weather GET endpoints are used, without cookies, registration, provider credentials or direct QWeather calls. OTA sources are unchanged.

产品配置使用系统可信证书束，不内置本地联调 CA；已移除联调用 USB 控制命令、页面/刷屏性能插桩及天气逐请求测时日志。独立开发证书、配置和测试包仅保留在忽略目录，不进入源码或已发布下载。

Product builds use the system trust bundle without local development CAs. Local USB control commands, page/display timing probes and per-request weather timing logs are removed. Development certificates, configurations and test images stay in ignored directories, outside source and published downloads.

第 1 版结构同时返回两日，太阳事件与期限为 UTC。预报请求携带 `locationId` 与客户端有效期上限 `maxAgeHours=10`，核对响应版本、来源、所选 ID、标准坐标、时区、日期、数值范围和期限；拒绝超长、截断、过深嵌套或坏响应。搜索明确提交后才请求，带 `page`（从零开始）及 `pageSize=20`，校验总数、页码及该批项数。界面仍每页八项，已取批次暂存RAM，同一查询翻到已缓存结果不启动网络任务、不重连当前网络；不足20条的结果一次取齐。更多结果首次翻到缺失批次时才补取，回翻不重复请求，不在首搜时阻塞下载整个城市字典。总数上限4000，按需分配批次；改查询、重新搜索、选城或离页释放缓存，后台旧查询结果不回填。查询须为2至64个字符，界面输入缓冲为128字节。所有用户共享平台按地点 ID 的同一份预报缓存，不按个人坐标分散缓存。

Schema 1 returns both days with UTC events/deadlines. Forecast requests use `locationId` and `maxAgeHours=10`; responses validate that ID, canonical coordinates, timezone, dates, ranges and deadlines. Explicit search requests validated 20-item batches while the UI keeps eight rows per page. Query-scoped RAM batches serve cached pages without a worker or network reconnect; up to 20 matches arrive together. Larger searches fetch only missing batches when needed, retain them for return navigation and never download the entire dictionary before showing results. At most 4000 results are supported with allocation on demand. Query edits, explicit resubmission, city selection and exit release batches; stale workers cannot restore them. Queries require 2-64 characters within 128 bytes. All users share the platform cache per location ID, never per personal coordinate.

天气校验、今日/明日选择及自动刷新共用 `app_weather_now()`：完整且校验通过的可信 HTTPS 预报响应提供标准 GMT `Date` 时间头，以收到该头时的单调计时推进天气专用 UTC，不写系统时钟或 RTC。正文下载及 UI 交接等待均计入经过时间，后续旧响应不能倒退已建立的参照。目录、搜索、定位、失败或取消的响应不能建立参照；缺失、格式错误或重复的 `Date` 不可信，改用已有天气参照，尚无参照时仍按设备时钟严格校验。保留超过参照时间120秒的未来预报拒收、原始期限及最长十小时限制，不用 `fetchedAt` 冒充当前时间。

Validation, day selection and automatic refresh share `app_weather_now()`. A complete, validated forecast from the trusted HTTPS origin supplies a standard GMT `Date` header, advanced using monotonic time since header receipt without setting the system clock or RTC. Body download and UI handoff delays count, and later old responses cannot move an established reference backward. Catalog, search, location, failed and cancelled responses cannot establish it. Missing, malformed or duplicate `Date` falls back to the existing weather reference, or strict device-clock validation if none exists. Forecasts over 120 seconds into the reference's future remain rejected, with original deadlines and the ten-hour limit intact; `fetchedAt` never substitutes for current time.

地点 ID 为 1 至 16 位大写字母或数字的字符串，不限定九位数字；官方 `F4F1`、`CA3` 等地点可正常选择、请求和保存。平台仍须核对 ID 存在于官方字典中。

Location IDs are strings of 1-16 uppercase ASCII letters or digits, not necessarily nine digits. Official IDs such as `F4F1` and `CA3` can be selected, requested and persisted. The platform still requires dictionary membership.

平台默认停用，须先配置合法凭据并完成平台测试。官网署名和特殊设备许可仍须向供应商确认，不能把“只在官网署名”视为已获许可。不在卡片中擅自添加来源文案，不生成样例预报冒充服务成功。

Activation requires legal credentials and successful platform checks. Website attribution and special-device permission still require supplier confirmation; website-only attribution is not assumed approved. Device attribution text is not silently added and fixtures never substitute for unavailable weather.

## 网络与生命周期 / Networking And Lifecycle

首页和组件页保留原有刷新入口；非天气主页面的静默刷新只借用当前路由就绪的纯联网 Wi-Fi 或 4G，不能启动无线、等待连接或在失败后切换承载。切页只通知取消，无常驻联网任务、不主动唤醒。天气通过统一网络服务租用用户选择的 Wi-Fi、4G 或自动承载。主动请求预报时，即使两个开关都关闭，也可以临时连接已保存的 Wi-Fi，或在确认有移动网络模组时临时启动 4G。自动路由可在首选不可用时按现有规则回退；固定路由不跨承载。临时 4G 在最后一个租约释放后关闭，不改变开关或路由设置；后台自动刷新不会打开关闭的 4G。已有连接只借用，热点或传书不抢占，不访问书籍存储、不改 Wi-Fi 凭据。

Home and Components retain their page-driven refresh. Quiet refresh off Home only borrows ready network-only Wi-Fi or 4G on the selected route; it never starts radios, waits for a connection or falls back to another bearer. Navigation only signals cancellation. No permanent networking worker or standby wake is added. An explicit forecast request can temporarily connect to saved Wi-Fi or start a confirmed cellular modem even if both switches are off. Auto follows the existing fallback rules; forced routes never cross bearers. Temporary cellular stops after the last lease without changing the switch or route settings; background refresh cannot start cellular while it is off. Weather borrows existing connections, does not take over AP or transfer sessions, and never accesses book storage or changes Wi-Fi credentials.

天气和自动对时/自动 OTA 彼此避让；不在页面启动路径取消并等待对时。任务的片内栈和 PSRAM 响应缓冲通过分池准入，响应缓冲计入网络账本；在等待联网前归还高峰名额。离页只通知取消，旧结果不写回；锁屏和关机前仍等待资源交还，不强杀任务。联网就绪等待在轮询间检查取消。单次任务预算45秒；HTTPS连接使用异步握手，按实际承载设置时限：Wi-Fi最长6秒，4G最长20秒，每20毫秒检查取消；响应头等待与正文无进度时限在Wi-Fi为5秒、4G为10秒，收到正文会重置无进度计时但不延长会话总预算。4G连接失败或响应传输错误时，关闭旧连接后等待200毫秒，在同一租约内最多重连一次，所有尝试共用原会话截止时间；业务错误、解析失败、截断或取消不重试，Wi-Fi仍只尝试一次。单次读取等待最多200毫秒，正文每次最多512字节，暂时性EAGAIN在窗口内继续等待。响应最多8192字节，不跟随重定向。取消仍可能等待当前 DNS/读写调用；真实延时和峰值内存需设备验证。

Weather and automatic clock/OTA work yield to each other. The task's internal stack and PSRAM response use pool admission, and the body is tracked as network memory. The startup slot is released before awaiting a bearer. Navigation signals cancellation and discards stale results; lock and shutdown still await handoff rather than killing workers. Bearer readiness checks cancellation between polls. The session budget remains 45 seconds. Asynchronous HTTPS connection establishment permits up to six seconds on Wi-Fi and twenty seconds on 4G, based on the actual lease, with cancellation checked every 20 ms. Header and body inactivity windows are five seconds on Wi-Fi and ten seconds on 4G; body progress resets only the inactivity window without extending the overall budget. Cellular connection or response transport failures allow one fresh connection after closing the old one and waiting 200 ms, on the same lease and within the original session deadline. Business errors, parse failures, incomplete EOF and cancellation are not retried; Wi-Fi still makes one attempt. Individual reads wait at most 200 ms with body chunks capped at 512 bytes. Temporary EAGAIN continues within the window. Responses are capped at 8192 bytes and redirects disabled. Cancellation may still wait for DNS or an in-flight read/write; actual latency/memory peaks need device validation.

自动失败冷却至少 5 分钟。翻页器已连接时不执行自动刷新；手动刷新提示先关闭蓝牙。天气不为联网断开翻页器，恢复联网后仍按用户选择的路由请求。天气任务与 Idle 同优先级，软件 TLS 大整数运算期间给 Idle 留出运行机会，不调整看门狗。未就绪或资源不足时保留仍有效的缓存。低电量未充电或日期无效时仍不联网。用户刷新走公共缓存 GET，不调用管理端强刷接口。

Automatic failures cool down for at least five minutes. With a connected page-turner, automatic refresh stays off and a manual refresh asks the user to turn off Bluetooth. Weather does not disconnect the turner and uses the configured route after connectivity is restored. The worker shares idle priority during software TLS computations without changing watchdog policy. Unavailable links or resources do not discard valid cached data. Low unplugged battery and an invalid clock still block networking. Manual refresh uses the public cached GET, not an admin endpoint.

天气参照只在本次开机的 RAM 中保留，重启后先以设备时间校验持久缓存，取得下一次正常预报才重新建立。系统日期无效时仍需先校时；HTTPS 证书验证依赖系统时钟，本机制不替代 SNTP 或修复无效系统日期。

The weather reference stays in RAM for the current boot. After reboot, persistent cache is checked against device time until a new valid forecast establishes a reference. An invalid system date still requires clock synchronization: HTTPS certificate validation depends on system time, so this mechanism does not replace SNTP or repair invalid system dates.

## 缓存与绘制 / Cache And Painting

保留服务器原始 `fetchedAt`、`freshUntil`、`serveUntil`，最长十小时，不因读取或失败延长。失败时只展示有效期内缓存并标记；到期隐藏数值。跨午夜按日期匹配，旧“明日”可成为今天，但缺少新的明日就显示缺失，绝不重新命名旧日期。服务器明确停用后撤下数值并尝试删除持久缓存。

Cache retains original server timestamps, with at most ten hours of validity and no extension on reads/failures. Only valid cache may survive failures, with labels; expired values disappear. Midnight selection matches actual dates: old tomorrow may become today, but missing new tomorrow stays missing. Explicit server disable clears visible values and attempts persistent cache deletion.

天气开关、城市与预报保存在独立 NVS 命名空间 `kiiko_weather`，写入受 `epd_flash_begin/end` 保护。已选城市的自动预报缓存由后台任务写入，并在取得保护后再次检查取消；已经开始的写入结束后才释放保护。城市、偏好和首次自动定位的保存语义不变。选择写入失败不伪装保存成功；缓存写入失败不影响当前 RAM 显示。v48 设置备份只导出城市和自动刷新，不复制整个命名空间。不复用或清除旧组件兼容字段。换城丢弃旧预报；搜索结果在改查询或离页后失效。

Weather preferences/city/forecast use `kiiko_weather` NVS with EPD-flash-protected writes. The worker persists automatic forecasts for an already selected city and rechecks cancellation under the guard. An in-flight write completes before releasing the guard. City, preference and first auto-location saves retain their original semantics. Failed preference writes never pretend success; failed cache writes retain current RAM display. Settings backup v48 exports the city and automatic-refresh preference, not the entire namespace. Retired compatibility fields remain untouched. City changes invalidate forecasts; query changes/exit invalidate transient search results.

首页布局单独保存在 `read_pico/home_regions`，两个区域一次提交，失败时都保持原值。首次没有新设置时，上方为最近阅读，下方继承旧天气显示开关；读取兼容不立即写入。v33 设置备份包含上下组件布局，恢复前校验版本、唯一性和校验值，提交成功才更新当前布局；v1–v32 备份不含布局，恢复时保留当前选择。v48 起恢复城市与自动刷新；旧备份保留现值；预报和搜索结果不会从备份导入。

Home layout uses `read_pico/home_regions`, committing both choices together and retaining both on failure. Without a new setting, reading starts above and the old weather visibility determines the lower region, without a migration write. Version 33 settings backups include the layout, validating schema, uniqueness and checksum before writes and updating live choices only after commit. Versions 1–32 preserve the current layout on restore. Version 48 restores city and automatic refresh; older backups preserve current values. Forecasts and search results are not imported.

天气偏好schema 3保存地点 ID、行政区信息及自动刷新开关，缓存schema 2保存对应预报。该功能仍处本地开发，不迁移旧开发格式；首次使用新格式需要重新选城。仅在偏好、城市或缓存变化时写入，不在每次轮询落盘。

Preference schema 3 stores official IDs, administrative metadata and the automatic toggle; cache schema 2 stores its forecast. This unreleased feature does not migrate earlier development formats, so select the city again. Writes happen on preference, city or cache changes, never each poll.

`ui_weather_draw()` 纯绘制，无联网、持久化或常驻分配，显式接收684×472区域。首页两区从y=168开始，于y=1112直接接导航；公共导航仅绘制y=1112以下区域，不额外清白或保护上方24px内容。进入页面时准备684×472的4bpp暂存（161424字节PSRAM），离页释放，不另分配整屏缓冲。内存不足则保留清晰白底内容、略去装饰。复用系统字体，不增加内建字库。九种固定背景保留1bpp源数据，构建时按场景无损 zlib-9 压缩为23202字节Flash（含几何与场景偏移表），雾霾共图。绘制使用77字节栈上行缓冲与临时PSRAM解压状态（绘完立即释放），不增加常驻缓存；设备逐行还原原纹理，文字留白按连续笔画合并。首页切日只重画并呈现天气所在区域，不重画页眉或最近阅读。分钟时钟仍独立局部更新，不改变波形或电源策略。

内容刷新截止与固定导航起点统一为y=1112，所有页面普通整页/局部差分直接覆盖y=1088–1111，不再把这24px作为固定区或另留白带。32列物理对齐仍由底层处理，固定导航取真实已显示像素。仅全像素清理向内对齐到y=1088并对余下内容补差分，避免驱动固定导航；导航图标及横条位置不变。

Content differences and fixed navigation share y=1112 across all pages, so ordinary page and area updates directly include y=1088–1111. Only full-pixel cleanup aligns inward to y=1088 and completes the remaining content with differences, preserving fixed navigation pixels and icon positions.

`ui_weather_draw()` only paints into an explicit 684x472 region, without networking, persistence or resident allocation. Home regions start at y=168 and meet navigation at y=1112. Shared navigation paints only y=1112 and below, without clearing or protecting 24 extra content rows. Entry prepares a 161424-byte region-sized scratch; exit releases it. There is no extra full-screen buffer. Allocation failure retains white-background content. The active font is reused. Nine fixed 1bpp backgrounds are losslessly zlib-9 packed per scene at build time into 23202 bytes of Flash, including geometry and scene offsets, with a shared Fog/Haze image. Rendering uses a 77-byte stack row buffer and temporary PSRAM inflate state freed on exit, without adding a persistent cache. Firmware reconstructs the original textures and combines contiguous ink halos into spans. Day switching presents only the weather region, leaving the header and reading untouched. The minute clock stays independent; waveforms/power policy are unchanged.

背景原图在 `main/assets/weather/`，资源由 `python3 tools/generate_weather_backgrounds.py` 无损打包；`--check` 校验可复现性，不手改二进制。/ Source masks live in `main/assets/weather/`; losslessly pack them with `python3 tools/generate_weather_backgrounds.py`, or verify with `--check`. Do not hand-edit the binary.

构建由 `tools/compress_builtin_images.py` 将原始位图转换为 `weather_backgrounds.wbr`。WBZ1 头含宽/高/场景数/行字节数及场景边界；各场景独立 zlib-9 压缩并流式逐行解压，解码检查输入、行输出及流尾校验边界。`weather_raster_host_test.py` 对所有场景逐行及整帧比对源像素，覆盖截断、损坏流、无效头及临时解压零泄漏。/ The build converts raw masks to `weather_backgrounds.wbr` with `tools/compress_builtin_images.py`. WBZ1 stores geometry, scene count, row stride and scene boundaries with independent per-scene zlib-9 streams decoded row by row with input, output and trailer bounds checks. Raster tests compare all rows and full frames with source pixels and cover truncation, corrupt streams, invalid headers and zero-leak cleanup.

## 验证入口 / Verification

`python3 tools/weather_host_test.py` 或 `python3 tools/run_reader_baseline.py weather` 覆盖真实解析/缓存、取消服务与NVS交接、九种背景及中性画面/双旋转/区域边界/内存降级，并逐像素验证四个霾代码与雾景共图。固定样本不是定位、供应商接入或实屏证据。产品构建仍须 IDF6.1/S31 `--preview`；部署、供应商实测和刷机分别需要授权。

The weather host suite covers real parsing/cache, cancellable service/NVS handoff, nine backgrounds plus neutral, two rotations, bounds and allocation fallback, and pixel-identical rendering for all four Haze codes versus Fog. Fixtures are not location/provider/optical evidence. Product builds require IDF6.1/S31 `--preview`; deployment, provider tests and flashing require separate authorization.

时间回归还覆盖 GMT 时间头与两分钟边界、设备时钟偏快/偏慢、系统时间跳变、正文及交接延迟、旧参照不倒退、原始期限到期、跨日缺明日及重启回退；首页与天气设置页均验证传入天气专用时间。/ Clock regressions cover GMT headers, the two-minute boundary, skew in both directions, system-clock jumps, body/handoff delays, nondecreasing references, original expiry, missing tomorrow at midnight and reboot fallback; Home and weather settings both verify the weather-specific draw time.

`python3 tools/weather_raster_host_test.py --output-dir build/weather-preview` 在回归通过后导出十类天气（雾霾共图）及中性画面的今日/明日共22张原生组件图、五张对照图和PNG压缩包，使用当前固件绘制与内置字体，不访问网络。

After regressions pass, `python3 tools/weather_raster_host_test.py --output-dir build/weather-preview` exports 22 native widget images (ten conditions with shared Fog/Haze, plus neutral, Today/Tomorrow), five comparison sheets and a PNG archive using current firmware drawing and the built-in font, without networking.
