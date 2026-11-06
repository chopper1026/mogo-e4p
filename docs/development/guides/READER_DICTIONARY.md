# 阅读字典 / Reader Dictionary

在阅读正文上长按约半秒选中一个字，继续拖动可选词语，松手打开释义卡片。长按选区两端的圆点可调整范围。卡片显示在上半屏或下半屏；点「关闭」、卡片外的正文、翻页手势或翻页键先关闭卡片，本次操作不翻书。目录从底部工具栏的「目录」进入，正文和留白长按均不再打开目录。
Hold body text for about half a second, drag to select a phrase, then release to look it up. Hold either endpoint to adjust the range. Close, an outside tap or a turn action dismisses the sheet without turning the book. Use the toolbar for the TOC; body and blank holds no longer open it.

关闭卡片回到纯文字页时，整屏做一次归一化刷新，使覆盖区与其余正文墨度一致；有阅读背景时保留背景灰阶。插图页保留图片刷新路径。端点拖动中只做跟手刷新，取消后抬手再归一。
Closing a sheet onto text normalizes the whole screen to equalize the covered and uncovered text, preserving reading-background grays. Illustration pages retain their image output. Endpoint drags use live updates and normalize after release if cancelled.

字典标题、读音、释义、例句、来源和操作按钮直接使用当前阅读字体，与系统界面字体独立，不调用界面内置字库补字。未收录整词时可反复点「逐字」循环查所选单字；多义、全部读音及例句通过「上一屏 / 下一屏」查看。字体缺字与词库缺项分别提示；缺字时保留原文查询并显示 Unicode 编码。选择含变体选择符或组合符时逐字查询保留它们。
All sheet text uses the current reader face directly, independently of the UI face and without the UI bitmap fallback. Per-character lookup cycles through the selected text; paging retains readings, definitions and examples. Missing glyphs and missing dictionary fields are separate conditions, with Unicode identifiers retained for missing headword glyphs.

## 安装 / Installation

把格式版本 1 的 `chinese.mgdict` 放入 TF 卡根目录的 `dictionaries/`，完整路径为 `/sdcard/dictionaries/chinese.mgdict`。先退出 USB 读卡模式，再进入阅读。词库不嵌入固件，不自动下载，也不需要网络；文件名大小写须一致。更新词库前关闭阅读或退出到书架，保留安装包的许可、署名、来源清单与校验值。
Place a version-1 `chinese.mgdict` under the card's root `dictionaries/` directory, leave USB export, then read. The firmware neither embeds nor downloads dictionary data. Close reading before replacing a package and retain its attribution, licenses, manifest and checksums.

支持 TXT、EPUB 和已支持的传统 MOBI 的可选择正文；图片、章节装饰题头、页眉页脚及跨页选词不在选区内。每次最多32个 Unicode 标量、128个 UTF-8 字节。极密页面超过2048个字格，或字格表分配/绘制失败时，本页拒绝选字并提示；调整字号后可重试。用户阅读字体本身缺字时须更换覆盖这些字的字体，词库数量不能证明字体覆盖。
Selection covers text in TXT, EPUB and supported traditional MOBI, excluding pictures, decorative chapter headers, chrome and cross-page ranges. A range is capped at 32 Unicode scalars/128 UTF-8 bytes. Pages exceeding 2048 glyph cells or failing map allocation/drawing refuse selection explicitly. Dictionary coverage does not establish font coverage.

## 来源与转换 / Sources And Conversion

词库合并[中文维基词典的 Kaikki 提取](https://kaikki.org/zhwiktionary/%E6%BC%A2%E8%AA%9E/)与[码谱汉语拼音辞典](https://github.com/mapull/chinese-dictionary)的中文词语、成语和单字释义。词库制包时使用 OpenCC `t2s` 将释义和例句转为简体，保留原词头并补入简体查询别名；维基词典的异体/词形跳转在导入后解引用，不把跳转链接计为释义。删除既无读音又无释义的空条目；有读音但无释义的条目如实显示缺项。
Merge Chinese Wiktionary with mapull's Chinese word/idiom/character definitions. Build-time OpenCC `t2s` simplifies definitions/examples and adds simplified lookup aliases while retaining original keys. Resolve form/variant links after importing instead of counting redirects as definitions. Remove completely empty headwords and retain explicit reading-only entries.

[Unihan](https://www.unicode.org/reports/tr38/)仅补充已有读音、笔画与部首编号资料；不把英文 `kDefinition` 翻译或冒充中文释义，也不推断语境读音。补充词库上游声明部分原始语料来源无法确认，数据不作完整性或权威性保证；具体快照、输入摘要和实际释义数量记录在 manifest。
Unihan supplements readings and structural fields only; English `kDefinition` is not presented as a Chinese gloss, and pronunciation is not inferred from context. The supplemental upstream notes uncertain provenance for some collected material; coverage and authority are not guaranteed. The manifest records snapshots, hashes and actual definition counts.

维基词典适用 CC BY-SA 4.0，码谱数据按上游 MIT 声明保留，Unicode 补充数据适用 Unicode License v3；OpenCC 转换工具及词表保留 Apache-2.0 许可。它们不改用固件的个人学习许可。分发转换包时携带完整许可、贡献者署名、词头页面历史链接、转换说明及输入/输出摘要；字体另受其自身许可约束。固件源码不包含外部完整词库或额外字体。
Retain Wiktionary's CC BY-SA 4.0, mapull's upstream MIT notice, Unihan's Unicode License v3 and the converter's OpenCC Apache-2.0 notice. These are separate from the firmware's personal-study terms. Distributions carry full licenses, attribution, conversion notes and hashes; extra fonts/full datasets remain external.

制包和字典宿主回归先安装 `tools/dictionary-requirements.txt` 中固定版本的 OpenCC；只有 `--font` 字形盘点还需要 `fonttools`。固件运行时无此依赖。对每份输入固定 SHA-256，并从下载页填写实际快照日期、Wiktextract 提交和补充词库提交，未知项不填成既定日期。
Install the pinned host OpenCC dependency before package conversion or dictionary tests; optional font coverage also needs `fonttools`. The firmware has no OpenCC runtime dependency. Pin every input digest and record actual snapshot/source revisions.

```sh
python3 -m pip install -r tools/dictionary-requirements.txt
python3 tools/build_dictionary.py \
  --wiktionary build/dictionary-data/zh.jsonl.gz \
  --unihan build/dictionary-data/Unihan.zip \
  --dump-date 2026-11-01 --extraction-date 2026-11-02 \
  --wiktextract-revision 19f9a3006a405fb7bfb40193dca41c7117a009a5 \
  --chinese-dictionary /path/to/chinese-dictionary \
  --chinese-dictionary-revision e804ada333b68afddfdccbe8dcc938a72da157a7 \
  --font /path/to/reader.ttf \
  --output build/dictionary-data/chinese.mgdict
```

`--wiktionary` 可重复传入同一快照的其他汉语标签文件。`--chinese-dictionary` 可选，目录须含上游 `word/word.json` 与 `character/char_detail.json`。输出为词库、`.manifest.json`（来源、有效释义数量、50字/30词及青铜/破解样本）和 `.LICENSE.txt`（署名摘要）；完整许可文本由分发方随包保留。转换按 UTF-8 字节序生成定长索引和独立 zlib 词条块；相同输入、Python/zlib/OpenCC 版本和参数生成相同字节。字段超限会以省略号截断。
Repeat `--wiktionary` for related labels; the optional supplemental folder contains upstream word and character files. Output includes provenance, actual-gloss counts, 82 coverage samples and attribution; complete licenses accompany distributions. Identical inputs and Python/zlib/OpenCC versions reproduce the package; oversized fields are truncated.

## 维护契约 / Maintenance Contract

- `ttf_*_observed` 在真实落笔过程中同步交付原文字节范围和字格，`book_layout` 使用本页/预绘页双表，只有完整且同代次的预绘结果随画面接管；观察器不分配、不读词库。/ Actual glyph draws report source spans/cells to bounded current/prefetch maps; only complete matching generations may be adopted.
- `reader_selection` 只借用本次调用的章节和字格；工作任务只复制最多128字节的查询。`reader_dictionary` 串行替换查询，取消后等待退出并关闭文件，再释放任务结果；不触碰字体、画面或阅读进度。/ The worker owns a small query copy, drains cancellation and never touches chapter buffers, fonts, frames or progress.
- 卡失效、USB、换章节、离页、锁屏和字体/布局失效均撤销旧选区或查询。`render` 只画已有结果，查询在事件/工作任务运行，结果在主任务 tick 接收。/ Media/USB/lifecycle/layout changes invalidate results; rendering consumes completed state only.
- 双表最多64KiB，单词条解压最多8192字节；文件最大为有符号32位长度、最多100万词头，每次二分索引并校验头、来源和词条 CRC。词条损坏、版本不兼容、卡读错与未安装分别返回。/ Maps cap at 64KiB and decoded entries at 8192 bytes; bounded binary search validates header/source/entry CRC and distinguishes package/version/I/O errors.

格式1：64字节小端文件头含 magic `MGDICT\r\n`、版本、文件长度、词条数、索引/键/压缩数据/来源区起点及来源长度；偏移40是来源CRC，偏移60是前60字节CRC。每条20字节索引为 `<IIHHIHH>`：键偏移、数据偏移、压缩长度、原始长度、原始CRC、键长度、保留0。解压内容为四个16位字段长度和对应UTF-8的读音、释义、例句、对应词头。其余字段含义以转换器和读取器共同测试为准，格式不兼容时递增版本。
Version 1 has a 64-byte little-endian header and 20-byte `<IIHHIHH>` index rows. Decoded entries start with four 16-bit field lengths followed by UTF-8 readings, definitions, examples and alias headwords. Incompatible format changes require a new version.

```sh
python3 tools/run_reader_baseline.py dictionary book controls input prefetch present session chapter turn reflow loop media font_faces
python3 tools/book_layout_draw_host_test.py
python3 tools/check_architecture.py
```

宿主测试覆盖 UTF-8/变体、真实字体观察及混合字体、双页接管、旋转选区、异步取消/替换、损坏包和面板编排。S31 编译、TF 卡冷/热查询时间、额外RAM峰值、常规翻页 p50/p95、实屏字形、触摸手柄和残影须分别验证，不能由主机通过推定。
Host coverage includes Unicode, real font observations/mixed faces, map adoption, rotated selection, cancellation/replacement, corrupt packages and page orchestration. Build, real-card timing/RAM, turn latency and physical touch/optical acceptance remain separate evidence.
