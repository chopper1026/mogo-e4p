# 日历锁屏 / Calendar lock faces

从 **设置 → 锁屏样式 → 日历** 选择「老黄历」或「万年历」，点按立即保存并启用日历锁屏。进入选择页或返回不改变当前样式。系统字号100–200%可用；锁屏本身固定为684×1216的白底黑字版式，文字借用系统字体，数字使用内置衬线位图。
Choose either face under Settings → Lock style → Calendar. Tapping saves and activates it; entering/back does not change the selection. Settings support 100–200% text, while faces use a fixed 684×1216 black-on-white layout, system text and serif numeral bitmaps.

老黄历使用双线边框、回纹及云纹，采用单日版式，不显示月历网格。大日期与农历干支下方，宜忌以圆印左右分组，短列表下配梅枝；财神、喜神、福神以切角双线框集中展示，黑底题签下列三组大字方位，组间用短细线分隔。吉神宜趋、凶煞宜忌分别由黑底题签领起，按行排入另一切角框，以点线区分；底部用小题签标出值神、建除，冲煞置于细线横签中，框外保留节气。万年历使用山水线刻及水波纹，保留整月公农历网格。纹饰为纯黑原生绘制，回纹、云纹与梅枝主描边约三像素；切角框、题签内边、花瓣与分隔保留细粗层次，位于文字以外。万年历行距按四/五/六行铺开，不增加背景图片或整帧缓存。
The single-day almanac uses a double frame, key frets and clouds without a month grid. Beneath the solar/lunar dates and stems, paired activity columns use round seals and plum branches for short lists. A chamfered double frame groups the three god directions beneath a black title plaque, with large direction text and short fine dividers. A second chamfered panel has black plaques for each spirit list and a dotted separator. Small guardian/officer plaques and an outlined clash strip finish the interior, with solar terms outside the frame. The month face retains its full solar/lunar grid, landscape and waves. Native pure-black ornaments use roughly three-pixel main strokes for frets, clouds and plum branches, with finer panel, plaque, petal and separator details outside text. Monthly spacing fills four/five/six rows without background assets or another framebuffer.

日历以北京时间（UTC+8）生成，优先读系统时钟，未初始化时可读已同步的PMU快照。产品时间范围为2024–2099年；无有效时间时显示「请校准时间」。画面在进入锁屏时生成，待机跨日不会新增定时唤醒；重新锁屏即可更新。冷启动的密码背景缓存跨日或换子样式后失效并重画，认证仍由原有锁屏流程负责。
Use Beijing time (UTC+8), preferring the system clock with a synced PMU fallback. Invalid/product-out-of-range clocks show a calibration prompt. Faces update on entry, without midnight wake timers. A cold PIN backdrop rejects stale dates/variants and redraws through existing authentication.

历法模型支持公历2000–2099年的全部有效日期，处理闰月和四/五/六行月份。干支月、宜忌、神煞及建除使用lunar-javascript的默认日粒度规则（`getMonthInGanZhi()` / `getDayYi()` / `getDayJi()` / `getDayJiShen()` / `getDayXiongSha()` / `getZhiXing()` / `getDayTianShen()`），节气当天整日按新月份规则；不根据节气时分切换。喜神、福神分别取 `getDayPositionXiDesc()` 和默认流派的 `getDayPositionFuDesc()`（`POSITION_FU_2`）。这些方位和神煞均为民俗资料。老黄历宜忌各按实际词宽居中排成最多四行：四项以内使用32px，更长列表使用26px；万年历宜忌各最多四项。老黄历的吉神、凶煞各在题签右侧按词宽左对齐排成最多两行，固定26px，整组垂直居中；未尽列表显示省略号，不提供吉时或活动决策。
The offline model covers all valid dates in 2000–2099, leap months and four/five/six-row grids. Month stems, activity lists, spirits, officers and guardians use upstream's default civil-day convention rather than exact solar-term transition times. Joy and fortune directions use the default getters, including fortune's `POSITION_FU_2` school. Each almanac activity column wraps by measured word width into up to four centered rows, using 32px for lists of up to four items and 26px for longer lists. The month face shows up to four items per list. Spirit lists wrap into up to two left-aligned 26px rows beside their plaques, vertically centered as a group. Overflow ends in an ellipsis; no hourly auspiciousness is calculated.

## 持久化与职责 / Persistence and ownership

- `settings` 的 `lock_face` 单字节编码为 `主样式×2+日历子样式`；主样式0–4，子样式0老黄历/1万年历。一个NVS值避免主、子样式部分写入。没有此键时兼容原 `lock_ui`（0–3），子样式默认老黄历。写入/提交成功后才发布内存选择。
- v38标识 `PICOSETc`，在v37票根背景块与阅读历史之间追加5字节的独立校验子样式块。v1–v37恢复为老黄历子样式且保留原主样式；旧版本不得携带日历主样式4。先验证全部尾块，再执行恢复。
- `main/time/calendar` 只读模型；`main/ui/ui_calendar` 纯绘制；`main/lock/lock_calendar` 读取设备时间并提供缓存身份；`sleep` 继续拥有显示、休眠及系统字体交还。不增加整帧缓存，也不联网。

The single packed NVS byte owns the coherent style/variant selection, with legacy-key fallback and publication after successful persistence. V38 appends a checked five-byte calendar tail before history; legacy backups retain their original face and default variant. Model, painting, clock/cache bridge and sleep/display/authentication remain separate owners.

## 数据与重生成 / Data and regeneration

历法输入固定为 `tools/vendor/lunar-javascript/lunar.cjs` 1.7.7，MIT许可及来源在同目录。固件只编译生成的静态C表，不包含JavaScript运行时，也不在构建时下载文件。衬线数字输入是 `tools/vendor/libre-bodoni/digits.ttf` 的0–9子集（SIL OFL 1.1）；固件只包含单色数字位图。所有原有汉字记录保持逐字节不变，仅补充宜忌词条需要的「磑」。
Pinned MIT calendar input generates static C tables, with no firmware JS runtime or build-time network access. Spirit lists use 12 month branches × 60 day cycles, guardian/officer tables use 12 × 12 month/day branches, and directions use 10 day stems; the oracle checks every date against the original getters. The OFL numeral subset generates monochrome digit bitmaps. The Han supplement retains all prior records and adds the rare mill-installation glyph.

```sh
node tools/generate_calendar_data.cjs
node tools/generate_calendar_data.cjs --check
python3 tools/generate_calendar_digits.py
python3 tools/generate_calendar_digits.py --check
python3 tools/gen_ui_hanzi.py --src /path/to/NotoSansSC-variable.ttf
python3 tools/run_reader_baseline.py calendar calendar_ui calendar_settings_ui lock_style settings_backup lock
```

数字生成需要Pillow，汉字补充生成需要fontTools及宿主C编译器；普通固件构建使用已生成资源。源摘要写在生成头文件，第三方许可随公开刷机站点的许可证目录分发。真机显示、字体卡故障及电源场景仍需设备验收。
Digit generation requires Pillow; Han generation requires fontTools and a host compiler. Normal builds consume committed assets. Generated headers record input hashes and public staging carries third-party notices. Optical, removable-font and power behavior still need device acceptance.
