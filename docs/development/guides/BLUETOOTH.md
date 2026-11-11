# 蓝牙翻页器设置 / Bluetooth page-turner settings

入口为「设置 → 蓝牙翻页器」，或长按快捷栏的蓝牙圆钮。页面显示连接状态、最多四台已配对设备，以及上一页／下一页的按键学习。关闭蓝牙保留配对和自定义按键；WiFi 占用无线资源时显示暂停。
Open Settings → Bluetooth page turner, or hold the Bluetooth quick control. The page shows connection status, up to four bonded peers and Previous/Next learning. Switching Bluetooth off preserves pairings and bindings; WiFi use displays a paused state.

「添加设备」持续搜索有广播名的 BLE 设备，最多显示24台，每页六行，箭头翻页；未广播名称的地址回落项不显示，列表出现不代表兼容性已经验证。已连接或正在连接时先确认断开，链路结束后才起扫。返回、导航离页和锁屏停止扫描。
Add device continuously discovers named BLE peers, up to 24 in six-row pages. Address-only entries are hidden and discovery does not certify compatibility. An existing or pending connection requires confirmation before disconnecting; discovery waits for teardown. Back, navigation and lock stop discovery.

点已配对设备进入详情，按地址区分同名设备。遗忘需确认，清除该设备的配对记录、自定义按键和解码提示缓存；遗忘其他设备不主动断开当前连接。连接切换交给蓝牙组件的异步队列处理。
Tap a bonded peer for details; addresses distinguish identical names. Confirming Forget clears that peer's stored pairing, bindings and decode hints without disconnecting an unrelated live peer. The BLE component queues asynchronous peer switching.

连接后，在动作行点「学习」并按翻页器上的键，识别成功后保存；已有绑定可「重学」。学习前清空旧报告并设定静止帧基准，一次只学习一个动作；取消、离页、锁屏、断连或换设备均结束本次学习。保存失败显示失败提示，不把失败标为已学习。内置翻页映射与阅读器的消费规则不变。
Learn an action by tapping its button then pressing the remote; existing bindings can be relearned. Clear stale reports and establish the idle baseline before waiting for one action. Cancel, exit, lock, disconnect or a peer change ends learning. Failed saves are reported without marking an action learned. Built-in mappings and reader consumption remain unchanged.

`main/apps/settings_bluetooth.c` 持有子视图、布局、可见设备快照和交互；`app_device_settings.c` 持有页面契约及来源返回。绘图不消费事件、不写设置；绘图与命中共用几何和快照，屏幕触摸期间延后采样，扫描结果最多每秒更新一次，同档信号强度变化不触发重绘。`ble_page_turner` 继续持有协议、配对与绑定存储，`app_loop` 继续协调协议栈与 WiFi 生命周期。未新增设置键、备份字段或固件版本。
The Bluetooth page helper owns subviews, geometry, visible snapshots and interactions; Device Settings owns the page contract and return origin. Rendering is pure and shares geometry/snapshots with hit testing. Sampling waits while the screen is touched, discovery refreshes at most once a second and RSSI changes within a displayed band do not repaint. The BLE component retains protocol/pairing/binding ownership; the app loop coordinates stack and WiFi lifecycles. No settings keys, backup fields or version changes are introduced.

宿主验证：`python3 tools/run_reader_baseline.py ble ble_recent bluetooth_ui routing quick haptics_shortcut lifecycle refresh`。导出生产代码 host 图：`python3 tools/bluetooth_ui_host_test.py --output build/bluetooth-ui-preview`。设备服务和状态栏为固定样本；真实内置字库与4bpp绘图覆盖18种状态、100%／120%／200%字号。射频连接、按键触感、屏幕残影和实机返回仍需设备验收。
Host validation uses the commands above. Preview exports run production drawing with fixed device-service/status samples, real builtin fonts and 4bpp pixels across 18 states at 100/120/200% scale. Radio connectivity, physical controls, panel ghosting and device navigation require hardware acceptance.
