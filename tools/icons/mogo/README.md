# mogo 项目图标 / Project icons

`mogo-cellular-signal.svg` 根据用户提供的信号柱参考重新绘制，使用四根从左到右递增、底边对齐的实心圆角柱；不包含截图或系统图标文件。快捷栏、设置入口和状态栏共用此图标。

`mogo-cellular-signal.svg` is an original drawing based on the user's signal-bar reference: four ascending, bottom-aligned bars with rounded ends. It contains no screenshot or system artwork and is shared by quick controls, Settings and the status bar.

源文件使用 24×24 画布与圆头描边。图标名以 `mogo-` 开头，追加到 `tools/gen_ui_icons.py` 的 `ICONS` 末尾后运行 `python3 tools/gen_ui_icons.py`，不要手改 `main/assets/ui_icons.h`。

Sources use a 24×24 canvas and round-cap strokes. Prefix names with `mogo-`, append them to `ICONS` in `tools/gen_ui_icons.py` and regenerate with `python3 tools/gen_ui_icons.py`; never edit the generated header by hand.
