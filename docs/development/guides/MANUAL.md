# 说明书维护 / Manual Maintenance

操作正文维护在原路径的 [简明 JSON](../../user-manual.zh-CN.json) 和 [图文 JSON](../../user-manual-detailed.zh-CN.json)，不可只修生成 HTML/PDF。图文输入包含版本、型号、日期和适用范围；简明生成器从配套站点清单读取版本，不能把当前源码版号当作已发布版本。
Maintain the two original JSON inputs, not only generated output. The detailed source declares scope/model/date; the simple generator reads release identity from its paired site manifest.

## 生成链与前提 / Generation Requirements

| 输入 / Input | 工具 / Generator | 输出与依赖 / Output And Requirements |
| --- | --- | --- |
| `docs/user-manual.zh-CN.json`，配套 `flash/manifest.json`、screens 图片 | [build_user_manual.py](../../../tools/build_user_manual.py) | 旧站点 HTML 与 PDF；需要完整配套 `flash/`、ReportLab、Pillow 及覆盖文本的静态 CJK 字体。默认写 `flash/`，不能对发布文件直接试跑 / Legacy site outputs; do not run against immutable releases |
| `docs/user-manual-detailed.zh-CN.json`，原生绘图 PNG 目录 | [build_detailed_manual.py](../../../tools/build_detailed_manual.py) | 指定目录的 HTML、检索索引和可选 PDF；验证任务 ID、步骤、图解名/图片。PDF 另需字体/ReportLab / Explicit preview directory, checked IDs/diagrams |

本 checkout 缺少旧 `flash/` 站点目录，也没有完整配套的原生图解 PNG 集。JSON 与引用检查可以执行；完整 HTML/PDF 重生成和图解视觉一致性尚未验证。不可复制旧板图片伪装当前设备，也不据输入文件存在宣称官网已同步。[旧官网维护说明](../history/WEBSITE-MANUAL.md)仅作历史参考。
This checkout lacks the legacy site directory and a complete native diagram set. Input/reference checks are available; complete HTML/PDF generation and diagram parity remain unverified. Do not substitute old-board images or infer deployment from retained inputs.

准备好与当前源码匹配的图解后，在被忽略的构建目录预览（不带 `--website`，不写发布目录）：
With matching native diagrams available, preview in an ignored build directory:

```sh
python3 tools/build_detailed_manual.py \
  --content docs/user-manual-detailed.zh-CN.json \
  --assets build/manual-native-assets \
  --output build/manual-preview
```

校对两份 JSON 与 README 三语言中的设备型号、独立信息栏、手工词库、官方 OTA、已撤下的设置 BOOT 入口、隐藏的 TF 升级入口和[备份覆盖](PERSISTENCE.md)。已撤下入口不得保留为当前步骤或挂上旧操作图；`boot-mode` 等现有任务 ID 仍保留。保留旧任务 ID，避免损坏已有锚点；原生验证不校验产品语义。图片须标注源码身份/示例数据，天气示例使用上海，不使用石家庄。官网由外部平台独立维护；本仓库修改不会自动更新其正文或部署。
Check target, independent chrome controls, manual dictionary installation, official OTA, withdrawn Settings BOOT and card update entries, and backup scope across both inputs and all READMEs. Keep retired instructions and diagrams out of current procedures while retaining task anchors such as `boot-mode`. Preserve task IDs; native validation does not establish semantics. Label screenshot provenance; weather examples use Shanghai. External platform publication requires separate synchronization.
