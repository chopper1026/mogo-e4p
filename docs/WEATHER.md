# 组件库与天气 / Components And Weather

## 使用 / Use

首页分为上下两个等高区域，可分别选择「最近阅读」「天气」或「留白」。最近阅读和天气各只能出现一次；选择已在另一区域的组件时，两区自动交换，留白可以重复。通过留白区域的设置图标、长按任一区域，或设置页「阅读背景」下方的「组件库」进入配置。天气去除外框和圆角，铺满所在区域并直接衔接底部导航。组件库、天气设置及城市页底栏均仅保留中间的首页按钮，两侧无隐藏返回热区。点「今日 / 明日」本地切换，不额外请求天气。旧组件、月阅读记录与闭环目标不恢复。

天气底部只保留36像素的刷新图标，不再显示「刷新」文字；图标中心位于区域左侧632像素处，点击范围为64×64像素，避开导航栏的扩展热区；最近阅读右上角的设置图标及点击入口也已移除，天气设置通过长按区域或设置页组件库进入，原底部设置图标及其独立热区移除。

Home has two equal regions, each choosing Recent reading, Weather or Empty. Real widgets are unique: selecting one already in the other region swaps both choices; Empty may repeat. Open configuration using the settings icon in Empty, a hold on either region, or the library below Reading background in Settings. Weather is rectangular, borderless and edge-to-edge, meeting the footer directly. Configuration, weather settings and city footers retain only Home, without hidden side hit areas. Day switching is local and never submits a weather request. Retired widgets, monthly records and goals remain removed.

The weather footer shows only a 36-pixel refresh icon centered at x=632 within the region, with a 64×64-pixel hit area outside the navigation touch margin. The Refresh label and the Reading settings icon/tap target are removed. Open weather settings with a hold or through the component library in Settings; the old footer settings icon and its independent hit area are removed.

「天气设置」提供独立的「自动刷新」开关，默认开启，周期六小时，关闭后仍可手动刷新、搜索和选择城市。仅天气已添加到首页且首页或非城市组件页处于前台时自动检查；移除天气停止自动请求，不中断阅读或为天气唤醒待机设备。错过周期后回到这些页面再补更新。周期以服务器原始预报更新时间为准，重启不重新延长；后台有效期更短、缓存缺失或跨日缺少明日预报时会提前补取。失败至少冷却五分钟，旧数据过期后仍隐藏，不为凑满周期延长有效期。

Weather settings has an independent Automatic refresh toggle, on by default at six-hour intervals. Turning it off preserves manual refresh, search and city selection. Automatic polling requires a placed weather widget and foreground Home or non-city component pages. Removing weather stops automatic requests; weather never interrupts reading or wakes standby. Missed refreshes resume on returning to these pages. Timing uses the original server timestamp across reboots. Earlier expiry, missing cache or a missing tomorrow can trigger an earlier fetch. Failures retain a five-minute minimum cooldown; expired data gains no extra lifetime.

尚无城市时尝试平台 IP 城市估计，每次开机最多自动尝试一次，仅作参考。失败后点城市名称，输入中文、英文、拼音或首字母，点「搜索」并选择带市、省行政区的候选；城市页右上方统一提供一个「自动定位」入口。结果每页五项，显示总数与页码，左右箭头翻页，不再只截取前五条。杭州可查到城市及 13 个区县，共 14 个地点。进入城市页不展开键盘，点输入框才展开，点空白处收起且保留文字及未确认拼音；有效搜索、自动定位及选择候选均可直接执行。城市以官方地点 ID 持久保存，普通刷新不重新定位。支持官方中国 CSV 中的 3577 个地点及其四种 UTC+8 时区，不提供 GPS 或精确定位权限。

With no selected city, approximate platform IP estimation is attempted at most once per boot. Search Chinese, English, Pinyin or initials and select a prefecture/province-qualified result. Five rows per page, total counts and arrow paging expose all matches; Hangzhou has fourteen entries including thirteen districts/counties. The keyboard opens on field taps and dismisses without losing input. The official location ID persists; normal refresh never relocates it. All 3577 official China CSV entries and their four UTC+8 timezones are supported, without GPS permissions.

平台以和风官方公开 CSV 建立持久城市字典，城市搜索和英文转中文只查本站数据库，不访问和风 Geo、不消耗和风次数。IP 定位在平台内匹配标准地点并直接返回中文名、ID 和字典坐标；设备不再另发名称修正查询。字典包括市、区县等官方天气地点，不是完整行政区划名录。

The platform persists the official public CSV as a city dictionary. Search and English-to-Chinese mapping query only that database, without QWeather Geo calls or quota use. IP results are canonicalized there and already include Chinese labels, IDs and dictionary coordinates. Firmware never issues a separate relabel request. The dictionary is a weather-location list, not an exhaustive administrative register.

温度为当日最低/最高摄氏温度，以 `/` 分隔；湿度为白天预报湿度，不是实时传感器读数。日出为地平线上的半轮太阳；日落为半空完整太阳及向地面的箭头。缺失事件显示 `--:--`。全区背景区分太阳、云、雨、雪、雾和雷雨，未知代码保持中性。

Temperatures are daily minimum/maximum °C separated by `/`, and humidity is daytime forecast humidity, not a live sensor. Sunrise uses a half sun at the horizon; sunset uses an airborne sun and downward arrow. Missing events show `--:--`. Region-wide scenes distinguish sun, cloud, rain, snow, fog and thunderstorms; unknown codes stay neutral.

## 平台接口 / Platform Contract

`CONFIG_PICO_COMPONENT_API_ORIGIN` 配置可信 HTTPS 平台源，默认 `https://mogo.mantake.shop`，不带路径或末尾斜杠。仅访问该源的 `/api/v2/components` 及天气 `locations`、`auto-location`、`forecast` GET 接口。无 Cookie、设备注册或供应商密钥，不直连和风，不改变 OTA 下载源。

`CONFIG_PICO_COMPONENT_API_ORIGIN` is a trusted HTTPS origin, defaulting to `https://mogo.mantake.shop`, without a path/trailing slash. Only catalog and weather GET endpoints are used, without cookies, registration, provider credentials or direct QWeather calls. OTA sources are unchanged.

产品配置使用系统可信证书束，不内置本地联调 CA；已移除联调用 USB 控制命令及页面/刷屏性能插桩。独立开发证书、配置和测试包仅保留在忽略目录，不进入源码或已发布下载。

Product builds use the system trust bundle without local development CAs. Local USB control commands and page/display timing probes are removed. Development certificates, configurations and test images stay in ignored directories, outside source and published downloads.

第 1 版结构同时返回两日，太阳事件与期限为 UTC。预报请求仅带 `locationId`，核对响应版本、来源、所选 ID、标准坐标、时区、日期、数值范围和期限；拒绝超长、截断、过深嵌套或坏响应。搜索明确提交后才请求，带 `page`（从零开始）及 `pageSize=5`，校验总数、页码及该页项数；只保留当前页。查询须为2至64个字符，界面输入缓冲为128字节。所有用户共享平台按地点 ID 的默认六小时预报缓存，不按个人坐标分散缓存。

Schema 1 returns both days with UTC events/deadlines. Requests use only `locationId`; responses validate that ID, canonical coordinates, timezone, dates, ranges and deadlines. Explicit search requests a zero-based page with five entries, validating counts and keeping only the current page. Queries require 2-64 characters within 128 bytes. All users share the platform's default six-hour cache per location ID, never per personal coordinate.

平台默认停用，须先配置合法凭据并完成平台测试。官网署名和特殊设备许可仍须向供应商确认，不能把“只在官网署名”视为已获许可。不在卡片中擅自添加来源文案，不生成样例预报冒充服务成功。

Activation requires legal credentials and successful platform checks. Website attribution and special-device permission still require supplier confirmation; website-only attribution is not assumed approved. Device attribution text is not silently added and fixtures never substitute for unavailable weather.

## 网络与生命周期 / Networking And Lifecycle

仅首页或组件页驱动服务，无常驻联网任务、不主动唤醒。已有可用 STA 只借用、不启动或停止；连接中、热点和错误会话不抢占。离线时短连已保存 WiFi，仅关闭自己成功创建的会话。不打开传书服务器、不访问书籍存储、不改 WiFi 凭据。

Only Home/component pages drive weather, without permanent networking or sleep wakeups. Ready STA is borrowed and never started/stopped by weather. Connecting/AP/error sessions are not taken over. Offline requests briefly connect saved WiFi and close only their own session, without upload servers, book access or credential changes.

天气忙时自动校时/OTA避让；开始前取消自动校时，OTA忙时不启动。退出页面、锁屏、关机准备和快捷 WiFi 前取消并等待资源交还，不强杀任务。单次任务预算45秒；HTTPS连接使用异步握手，最长6秒，每20毫秒检查取消；连接后的单次读写等待1.2秒，响应最多8192字节，不跟随重定向。诊断仅记录接口路径（不含查询参数）、阶段、状态和耗时，不记录城市输入或凭据。取消仍可能等待当前 DNS/读写调用；真实延时和峰值内存需设备验证。

Automatic clock/OTA work yields while weather is busy. Weather first cancels automatic sync and never starts during an online update. Exit, lock, shutdown preparation and quick WiFi cancel and await cleanup instead of killing tasks. The session budget is 45 seconds. Asynchronous HTTPS connection establishment permits up to six seconds with cancellation checked every 20 ms; subsequent individual I/O waits remain 1.2 seconds. Responses are capped at 8192 bytes and redirects disabled. Diagnostics include only query-free paths, phases, status and duration, never city input or credentials. Cancellation may still wait for DNS or an in-flight read/write; actual latency/memory peaks need device validation.

自动失败冷却至少5分钟。蓝牙翻页器已连接、低电量未充电、日期无效或内存不足时不联网。用户刷新仍走公共缓存 GET，不调用管理端强刷接口。

Automatic failures cool down at least five minutes. A connected Bluetooth turner, low unplugged battery, invalid clock and allocation failure prevent networking. User refresh uses the public cached GET, never an admin force-refresh endpoint.

## 缓存与绘制 / Cache And Painting

保留服务器原始 `fetchedAt`、`freshUntil`、`serveUntil`，最长六小时，不因读取或失败延长。失败时只展示有效期内缓存并标记；到期隐藏数值。跨午夜按日期匹配，旧“明日”可成为今天，但缺少新的明日就显示缺失，绝不重新命名旧日期。服务器明确停用后撤下数值并尝试删除持久缓存。

Cache retains original server timestamps, with at most six hours of validity and no extension on reads/failures. Only valid cache may survive failures, with labels; expired values disappear. Midnight selection matches actual dates: old tomorrow may become today, but missing new tomorrow stays missing. Explicit server disable clears visible values and attempts persistent cache deletion.

天气开关、城市与预报保存在独立 NVS 命名空间 `kiiko_weather`，仅 UI 任务写入并受 `epd_flash_begin/end` 保护。选择写入失败不伪装保存成功；缓存写入失败不影响当前 RAM 显示。该命名空间**不包含在设置/书籍备份恢复中**。不复用或清除旧组件兼容字段。换城丢弃旧预报；搜索结果在改查询或离页后失效。

Weather preferences/city/forecast use `kiiko_weather` NVS with UI-only EPD-flash-protected writes. Failed preference writes never pretend success; failed cache writes retain current RAM display. **Settings/book backup/restore excludes this namespace.** Retired compatibility fields remain untouched. City changes invalidate forecasts; query changes/exit invalidate transient search results.

首页布局单独保存在 `read_pico/home_regions`，两个区域一次提交，失败时都保持原值。首次没有新设置时，上方为最近阅读，下方继承旧天气显示开关；读取兼容不立即写入。v33 设置备份包含上下组件布局，恢复前校验版本、唯一性和校验值，提交成功才更新当前布局；v1–v32 备份不含布局，恢复时保留当前选择。天气城市、自动刷新偏好和预报缓存仍不随备份迁移。

Home layout uses `read_pico/home_regions`, committing both choices together and retaining both on failure. Without a new setting, reading starts above and the old weather visibility determines the lower region, without a migration write. Version 33 settings backups include the layout, validating schema, uniqueness and checksum before writes and updating live choices only after commit. Versions 1–32 preserve the current layout on restore. Weather city, automatic refresh preference and forecast cache remain outside the backup.

天气偏好schema 3保存地点 ID、行政区信息及自动刷新开关，缓存schema 2保存对应预报。该功能仍处本地开发，不迁移旧开发格式；首次使用新格式需要重新选城。仅在用户修改偏好或保存城市时写入，不在每次轮询落盘。

Preference schema 3 stores official IDs, administrative metadata and the automatic toggle; cache schema 2 stores its forecast. This unreleased feature does not migrate earlier development formats, so select the city again. Writes happen on preference/city changes, never each poll.

`ui_weather_draw()` 纯绘制，无联网、持久化或分配，显式接收684×472区域。首页两区从y=168开始，于y=1112直接接导航；公共导航仅绘制y=1112以下区域，不额外清白或保护上方24px内容。进入页面时准备684×472的4bpp暂存（161424字节PSRAM），离页释放，不另分配整屏缓冲。内存不足则保留清晰白底内容、略去装饰。复用系统字体，不增加内建字库。六种固定背景以1bpp只读资源保存，合计206976字节Flash，不额外占用PSRAM；设备直接绘制原纹理，文字留白按连续笔画合并。首页切日只重画并呈现天气所在区域，不重画页眉或最近阅读。分钟时钟仍独立局部更新，不改变波形或电源策略。

内容刷新截止与固定导航起点统一为y=1112，所有页面普通整页/局部差分直接覆盖y=1088–1111，不再把这24px作为固定区或另留白带。32列物理对齐仍由底层处理，固定导航取真实已显示像素。仅全像素清理向内对齐到y=1088并对余下内容补差分，避免驱动固定导航；导航图标及横条位置不变。

Content differences and fixed navigation share y=1112 across all pages, so ordinary page and area updates directly include y=1088–1111. Only full-pixel cleanup aligns inward to y=1088 and completes the remaining content with differences, preserving fixed navigation pixels and icon positions.

`ui_weather_draw()` only paints into an explicit 684x472 region, without networking, persistence or allocation. Home regions start at y=168 and meet navigation at y=1112. Shared navigation paints only y=1112 and below, without clearing or protecting 24 extra content rows. Entry prepares a 161424-byte region-sized scratch; exit releases it. There is no extra full-screen buffer. Allocation failure retains white-background content. The active font is reused. Six fixed 1bpp backgrounds use 206976 bytes of read-only Flash without extra PSRAM. Firmware stamps original textures and combines contiguous ink halos into spans. Day switching presents only the weather region, leaving the header and reading untouched. The minute clock stays independent; waveforms/power policy are unchanged.

背景原图在 `main/assets/weather/`，资源由 `python3 tools/generate_weather_backgrounds.py` 无损打包；`--check` 校验可复现性，不手改二进制。/ Source masks live in `main/assets/weather/`; losslessly pack them with `python3 tools/generate_weather_backgrounds.py`, or verify with `--check`. Do not hand-edit the binary.

## 验证入口 / Verification

`python3 tools/weather_host_test.py` 或 `python3 tools/run_reader_baseline.py weather` 覆盖真实解析/缓存、取消服务与NVS交接、七类原生背景/双旋转/区域边界/内存降级。固定样本不是定位、供应商接入或实屏证据。产品构建仍须 IDF6.1/S31 `--preview`；部署、供应商实测和刷机分别需要授权。

The weather host suite covers real parsing/cache, cancellable service/NVS handoff and seven native scenes, two rotations, bounds and allocation fallback. Fixtures are not location/provider/optical evidence. Product builds require IDF6.1/S31 `--preview`; deployment, provider tests and flashing require separate authorization.
