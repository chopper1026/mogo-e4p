# 网络承载 / Network bearers

固件只维护一个 S31 镜像和一套 OTA 身份（`METALIO-EINK4P`）。镜像始终包含 NT26 UART-Ethernet 驱动；启动后通过 UART2 的 TX58/RX59、MRDY60、SRDY3 异步发送 AT 探测。探测到 NT26 即认为具备移动网络能力；未插 SIM、未注册或信号弱仍属于“有 4G 硬件”，只影响联网状态。探测超时或没有模块时，移动网络入口隐藏，Wi-Fi 版不会因持久化设置显示 4G。

Wi-Fi 和移动网络使用互斥开关。快捷栏保留 Wi-Fi 入口；只有运行时确认移动网络能力后才增加“移动网络”入口。单击打开其中一路时，服务会先检查另一条业务租约；没有进行中的业务或传书操作时自动关闭另一条无线，并把业务联网方式选为刚打开的承载。另一条无线正在传书、OTA、天气或对时时，切换会被拒绝，不中断当前操作。蓝牙翻页器已连接、连接中、配对或扫描时，开启 4G、Wi-Fi、扫描 Wi-Fi 和传书会提示先关闭蓝牙，不自动断开；空闲蓝牙临时让位，网络停止并归还资源后按原设置恢复。Wi-Fi 或 4G 持有独占时，快捷栏和蓝牙设置页拒绝从关闭状态开启蓝牙并指明占用的承载；原先开启但临时暂停的蓝牙仍可手动关闭。长按进入网络设置。业务联网方式默认“自动”，默认 Wi-Fi 优先；设置缺失或无效时也恢复此默认值。强制选择的承载不可用时业务失败并提示用户，避免 SIM 套餐到期后静默产生移动流量。

自动启动 Wi-Fi STA 时（快捷栏、Wi-Fi 传书或业务临时联网），先扫描 2.4 GHz：在认证兼容的可见已保存网络中选信号最强者，同强度按保存顺序；没有匹配时不发起连接，也不改保存顺序。手动点选某条网络仍只连接该网络。扫描选网不是在线漫游；已就绪的 STA 仍由业务复用。
Automatic Wi-Fi STA startup (quick controls, Wi-Fi transfer or temporary connectivity) scans 2.4 GHz first. Choose the strongest visible compatible saved network, breaking ties by saved order. Without a match, do not associate or reorder profiles. Explicit selection still targets that profile only. This is startup selection, not live roaming; services reuse a ready STA.

移动网络设置页只提供 4G 开关和业务联网方式，移动网络固定使用内置 SIM 卡，Wi-Fi 开关保留在快捷栏。快捷栏、设置列表和状态栏的移动网络入口统一使用四根递增的圆角信号柱，与创建热点的发射塔图标区分。

S31 的 Wi-Fi 管理帧和 NT26 DMA 共用片内 RAM。产品配置沿用原厂关闭 `ESP_WIFI_IRAM_OPT`、`ESP_WIFI_EXTRA_IRAM_OPT` 和 `ESP_WIFI_RX_IRAM_OPT`；Wi-Fi 会话使用 `WIFI_PS_NONE`，同时关闭无需使用的 `ESP_WIFI_SLP_IRAM_OPT`。保留原收发缓冲数量。量产配置不开放 Wi-Fi 或 4G 与蓝牙翻页器并行运行；PSRAM 余量不能替代驱动所需的片内 DMA 内存。该配置取舍可能影响 Wi-Fi 峰值吞吐。

传书响应逐项检查 JSON 对象、字段和数组的分配结果；失败时释放已构建的 JSON、尚未挂入父对象的子数组及目录条目，返回内存不足，避免半成品响应和异常路径泄漏。

日期对时、天气和联网 OTA 通过 `network_service` 租约使用选定承载。租约存续期间快捷栏不能关闭对应无线；本地传书仍只使用 Wi-Fi。进入浅睡、锁屏或关机前先停止移动模块，唤醒后按用户开关恢复，避免 P14 共享电源轨在模组任务仍运行时被板级睡眠流程切断。

主动刷新天气、点击「通过网络对时」或手动检查和确认下载更新，可按业务联网方式临时连接已保存的 Wi-Fi，或在确认有模组时临时启动关闭的 4G；后台任务不因此获得启动已关闭 4G 的权限。临时会话共用承载、内存及蓝牙准入，不改开关或路由设置；最后一个租约归还后关闭，取消和失败也走相同回收路径。/ Explicit forecast refresh, manual network time sync, manual update checks and confirmed downloads may connect to saved Wi-Fi or start switched-off cellular with a confirmed modem, using the configured route. Background work does not gain permission to start switched-off cellular. Temporary sessions share bearer, memory and Bluetooth admission, preserve switch and route settings, and stop after the last lease, including on cancellation or failure.

手动对时在后台等待无线就绪和 SNTP，设置页仅显示状态并在 UI 任务写入、回读内部时钟；只有完整交接成功才更新「上次对时」。/ Manual time sync waits for connectivity and SNTP in a worker. Settings shows the result and the UI task writes and reads back the internal clock; only successful handoff updates the last-sync record.

已有 STA 正在连接时，业务借用该会话并等待取得 IP，不提前拒绝，也不重启或在结束时关闭用户开启的 Wi-Fi。上次失败的 STA 只有在纯联网、没有其他 Wi-Fi 租约且允许停止时才能重建；热点、文件服务及忙碌会话不自动重启。租约准入与手动无线切换共用控制门，防止恢复过程与新租约交错。自动模式仍按优先级回退，强制路由不跨承载；回退承载不存在时保留原始启动错误。
Business requests borrow an already-connecting STA and wait for IP without rejecting, restarting or later stopping user-enabled Wi-Fi. Rebuild a failed STA only when it is network-only, has no other Wi-Fi leases and can stop safely; never automatically restart AP, file-server or busy sessions. Admission and manual radio changes share a control gate. Auto retains its fallback policy, forced routes never cross bearers, and an absent fallback does not hide the original startup error.

业务租约、手动切换、后台 NT26 探测/重试和睡眠恢复共用承载控制门。更改路由偏好不会启动另一条无线或替换活跃租约；自动回退只在原承载已停止，或原 4G 会话已无数据链路、无租约且可安全交还时启用另一路。Wi-Fi 启动失败并确认清理后不再占用承载。手动切换遇当前启动失败（包括 NT26 没有 AT 应答）时，先停失败承载，再按原路由恢复；恢复遇高峰忙时退避重试，不把显式关停或无 SIM 当作失败。
Leases, manual changes, background NT26 probe/retry and wake share one bearer gate. A route preference change neither starts a second radio nor replaces an active lease. AUTO starts the other radio only after the first has stopped or a terminal, unleased 4G session safely yields its data-less transport. Cleaned-up Wi-Fi failures no longer occupy the bearer. Manual switch rollback stops a failed startup (including NT26 without an AT reply) before restoring the previous route, backs off on peak contention, and does not mistake intentional shutdown or no SIM for startup failure.

传书页面只负责自己启动的会话。离页和拔卡不会关闭仅被观察、并非本页拥有的 STA；若其他业务正借用本页的 STA，则仅关闭文件服务，保留联网直到最后一个租约归还。拔卡禁止旧根上的新文件操作，随后退场文件服务，不切换进行中请求的存储根。
The transfer page owns only the session it started. Page exit and media loss leave STA observed by, but not owned by, the page untouched. If another service borrows a page-owned STA, close only HTTP and retain connectivity until the last lease returns. Media loss rejects new operations on the old root and closes the file server without redirecting in-flight requests to a different root.

联网 OTA 的准入失败提示区分未配网、热点占用、网络切换或忙碌、启动内存不足，以及强制 4G 下的模块检测、SIM 未就绪和注册失败；手动检查不以开关关闭为由要求用户先开启 4G。不自动下载或安装发现的版本。提示仅说明当前软件状态，不代替实际网络、SIM 或射频排查。
OTA admission failures distinguish missing Wi-Fi configuration, AP use, busy/switching radios, startup memory shortage and forced-cellular hardware, SIM and registration states. Manual checks do not ask users to turn on 4G just because its switch is off. Finding an update does not automatically download or install it. Notices describe software state, not a confirmed network, SIM or radio root cause.

仅连接 Wi-Fi 也占用片内 RAM，不代表已启动传书。OTA 内存提示按片内/PSRAM、可用总量/连续块区分，不要求关闭未确认的服务。
A Wi-Fi-only connection also uses internal RAM and does not imply file transfer is running. OTA notices distinguish internal RAM from PSRAM and total capacity from contiguous blocks without asking users to close unconfirmed services.

租约等待取得 IP 后，须成功将该接口设为默认网卡才允许开始 DNS、TCP 或 TLS。无可用承载时不强行切到 Wi-Fi；自动模式回退后，首选无线恢复也不抢占正在使用的租约。产品启用 `CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF`，分别保存各接口的 DHCP DNS，并随默认网卡切换解析服务器；已有本地 `sdkconfig` 须同步该选项。

天气请求按实际租约承载设置等待：Wi-Fi 连接 6 秒、响应空闲 5 秒；4G 连接 20 秒、响应空闲 10 秒。4G 遇到连接失败或响应传输错误时，在同一租约内最多重新建立一次连接；不重试业务错误，不切换承载。两者均保留整个会话 45 秒上限及最长 200 毫秒读取等待，支持离页取消，不因重连或单段响应到达而延长整个会话。

NT26 的 UART TX 由 CPU 写入 FIFO，待发帧和重组缓冲优先使用 PSRAM。ESP32-S31 的 UHCI 接收数据优先使用支持 DMA 的对齐 PSRAM 缓冲，描述符和 ISR 元数据仍在片内；回退时整组使用片内 DMA 缓冲，不混用。DMA 交接按所在内存的缓存行对齐、同步。该布局给蓝牙控制器保留片内 DMA 空间，不改变现有 Wi-Fi 互斥规则。

NT26 UART TX writes the FIFO on the CPU, so queued TX frames and reassembly prefer PSRAM. On ESP32-S31, UHCI RX payloads prefer aligned DMA-accessible PSRAM, while descriptors and ISR metadata stay internal. Fallback uses an entirely internal DMA pool. Cache alignment and synchronization follow the actual payload memory region. This layout leaves more internal DMA heap for the Bluetooth controller without changing Wi-Fi arbitration.

4G 的天气和 OTA 请求通过 `network_http` 从当前 DNS 答案中最多保留两个不同的 IPv4 地址。传输失败后的下一次既有重试选择另一个地址；成功地址仅在仍属于当前 DNS 答案时优先使用，不硬编码服务器 IP。产品启用 `CONFIG_LWIP_DNS_MAX_HOST_IP=2`，已有 `sdkconfig` 也须同步。只调整 TCP 连接目标，HTTP Host（含显式端口）、TLS SNI 和证书域名仍使用原域名，保留证书链校验和禁止自动重定向；Wi-Fi 请求使用原连接路径。每个业务任务独占一份静态会话，必须等 HTTP client 清理完再复用，确保 TLS 持有的域名指针有效；DNS 列表在创建 client 前释放，TLS 连接仍串行，重试次数和天气会话上限不增加。取消和业务/清单校验错误不标记为地址故障。

天气和 OTA 在请求前记录实际承载、默认网卡是否匹配、IP/网关、主备 DNS、时钟及片内/PSRAM 的空闲总量和最大连续块。完整响应记录实际读取字节、完成标志与已连接的服务器地址；失败日志区分连接、响应头、正文、HTTP 状态及 OTA 清单解析，并在清理 HTTP client 前保存 socket errno、TLS 错误和证书验证标志。天气数据拒收进一步区分正文格式、协议、城市匹配、时间字段、每日数据、参考时钟、过期及有效性校验，保留原有严格校验。URL 去除查询参数和 fragment，不记录响应正文；显式取消不记作网络失败。对时或单次刷新成功不能代替反复断开、重连后的 HTTPS 接口验证。

联网 OTA 只从 `https://mogo.mantake.shop/api/v2/ota/devices/mogo-e4p/update.json` 读取清单，连接失败时最多尝试两次，失败或清单无效时直接提示，不再请求 GitHub 回退清单。清单继续使用 `mogo-e4p-dual-5m-v1` 和 `METALIO-EINK4P`，Wi-Fi 版与 4G 版不拆分清单、下载入口或升级路线。

本地排查连接阶段失败时，可开启 `CONFIG_APP_NETWORK_CONNECT_TRACE`，保留 `esp-tls` 的实际解析地址及 TCP/TLS 连接进度；失败后 socket 已在 IDF 内部关闭时，仍能从前置日志核对目标。该开关默认关闭，仅提高 `esp-tls` 的日志等级，不开启 HTTP 请求头、响应正文或 mbedTLS 密钥调试，不修改连接策略或证书校验。诊断结束后应关闭并重新构建。

移动网络固定使用内置 SIM，不提供外置卡或卡槽切换选项。每次启动模组时使用 `AT+ECSIMCFG?` 确认卡槽；若不是内置槽或无法确认，按 `CFUN=0 → ECSIMCFG=SimSlot,1 → CFUN=1` 配置并再次回读，配置失败也尝试恢复射频。旧固件保存的卡槽偏好不再读取，避免先前选择外置卡后无法恢复内置卡。回读确认只代表卡槽配置完成，仍需 SIM 就绪、注册和取得 IP 后才能联网。

AT 查询的数据与最终 `OK` 允许分帧返回；响应缓冲受独立互斥保护并限制大小，保留 `ECSIMCFG`、`CPIN`、`CEREG` 等查询的前置数据，避免只拿到最后一帧 `OK` 而误判卡槽或注册状态。

每次启动后，SIM 就绪检测最多等待 10 秒，期间的无卡、卡忙及查询失败允许重试，不因第一次失败立即判定无卡。只有最后一次查询明确返回无卡才显示“未检测到 SIM 卡”；PIN/PUK、其他锁定、卡忙、读取故障及查询失败分别提示，保留 AT 通道。不自动提交 PIN/PUK，也不自动重启设备。关闭开关或休眠取消初始化时不报告注册超时。

已连接的 NT26 复用初始化任务在后台查询信号，界面和承载轮询只读取缓存；重试与停止仍由原有生命周期管理。/ A connected NT26 reuses its init task for background signal queries; UI and bearer polling use the cache, with retries and stop still owned by the existing lifecycle.

NT26 驱动在注册 SRDY 引脚中断前安装共享 GPIO 中断服务，兼容加速度计先启动的情况；停止模组时只移除自己的引脚处理器。产品日志使用 USB Serial/JTAG 控制台，GPIO58/59 专用于 NT26，避免默认 UART0 控制台占用同一组引脚。已有本地 `sdkconfig` 在更新默认配置后也须同步这两项控制台选择。

NT26 每帧发送前先清除旧 ACK，再启用 SRDY 上升沿中断；发送完成后等待模组的高电平确认脉冲。低电平忙信号不能释放发送等待，也不能在等待确认时关闭上升沿检测。普通空闲状态继续检测双边沿，保留唤醒与休眠状态转换。

NT26 接收回调只有在事件成功入队后才把 DMA 缓冲所有权交给主任务；队列满时直接在 ISR 归还该缓冲，不分配内存。描述符错误导致暂停时，在任务上下文确认 CPU 持有的缓冲全部归还后恢复 DMA；中断中不执行重挂和 FIFO 清理。模组启动的对象、事件组或任务分配失败时安全返回，停止时由唯一所有者清理资源。

移动网络设置随后台状态变化自动更新，区分正在连接、未检测到 SIM 卡、注册被拒、连接失败和已连接；信号未知不显示为零。只有 UART 数据握手完成并取得本接口 IP 地址后才判定已连接，单纯注册到运营商不代表天气或 OTA 已可用。未插卡或注册失败时保留 AT 查询通道，不启动 DHCP，也不把失败改回连接中；修正卡状态后可关闭再开启移动网络重试。自动联网模式在终止错误后尝试另一种可用承载，强制 Wi-Fi 或 4G 时保留用户选择并报错。
