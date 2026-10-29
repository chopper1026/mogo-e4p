# 代码结构与依赖边界 / Architecture and dependency boundaries

本项目的产品目标是 Metalio E-INK4 Plus（ESP32-S31）。构建入口是根目录与 main 的 CMakeLists.txt；页面以 app_registry.c 和 app_desc_t 为准。
The product targets Metalio E-INK4 Plus (ESP32-S31). Root/main CMakeLists.txt define the build; app_registry.c and app_desc_t define pages.

## 目录职责 / Ownership

| 目录 / Directory | 职责与边界 / Ownership and limits |
| --- | --- |
| `main/app_main.c` | 启动装配，最后进入应用循环。/ Boot assembly, then the app loop. |
| `main/app/` | 页面契约、跨页请求接口、输入、导航、介质与生命周期调度。/ Page contracts, cross-page request APIs, input, navigation, media and lifecycle coordination. |
| `main/apps/` | 产品页面控制器及页面专用的目录、文件树视图。/ Product page controllers and their TOC/file-tree views. |
| `main/book/` | 书源、TXT/EPUB、排版、阅读会话、书架规则、进度与封面缓存。/ Sources, TXT/EPUB, layout, reader sessions, shelf rules, progress and cover caches. |
| `main/image/` | 通用图像探测、JPEG/PNG 解码、裁剪与缩放，包含硬件 JPEG 适配。不得依赖书籍、UI、字体或页面。/ Generic image probing, JPEG/PNG decoding, crop/scale and hardware JPEG adaptation; no book, UI, font or page dependencies. |
| `main/ui/` | 公共绘制、控件、交互反馈及题头样式；绘制接口不承担页面状态转换。/ Shared drawing, controls, feedback and heading styles; paint APIs do not own page transitions. |
| `main/font/` | 字体来源、加载、字形缓存与渲染。系统/阅读字体的选择策略在 services。/ Font sources, loading, glyph caches and rendering; services owns system/reader face policy. |
| `main/services/` | 跨页面使用的 USB 存储和字体上下文服务，负责各自资源的交还。/ Cross-page USB storage and font-context services, each owning resource handoff. |
| `main/factory/` | 启动标定与硬件自检。/ Startup calibration and hardware self-tests. |
| `components/` | BSP、芯片驱动、可复用功能组件及第三方源码；不得包含 main 头文件。/ BSP, drivers, reusable components and third-party sources; no main headers. |
| `tools/` | 宿主回归、资源生成、诊断与发布工具；分类入口见该目录 README。/ Host tests, asset generators, diagnostics and release tools; see its README. |
| `docs/local/`、`build/` | 本地计划、日志、验收证据和构建产物，均不提交。/ Untracked plans, logs, acceptance evidence and build outputs. |

## 依赖规则 / Dependency rules

- 页面通过 app 中的公开契约协作，不包含另一个页面的实现。app 的调度代码不得包含 apps 目录的头文件。跨页请求仍由对应页面消费，不能借移动头文件改变生命周期。
  Pages collaborate through app contracts without including another page implementation. App coordinators must not include apps headers. The target page still consumes each cross-page request; moving a header does not transfer lifecycle ownership.
- 书籍封面依赖 image；壁纸、阅读背景和图片页直接依赖 image。book_cover.h 暂时转出原有图像声明以兼容既有调用方，新增通用图像调用应直接使用 image_decode.h。
  Book covers depend on image; wallpaper, reader backgrounds and image pages use image directly. book_cover.h forwards legacy image declarations for compatibility; new generic consumers should include image_decode.h.
- main 的头文件搜索路径为组件私有。目录边界由结构检查补充约束，不表示每个目录都是独立 IDF 组件。
  Main header search paths are component-private. Structural checks enforce directory boundaries; directories are not automatically separate IDF components.
- main 根目录仍保留显示、睡眠、设置、时间和 OTA 等系统功能；新增模块放入最接近的职责目录，避免继续将所有服务堆在根目录。
  Main retains display, sleep, settings, time and OTA services at its root; place new modules under the nearest owner instead of adding every service there.
- 资源所有权是接口的一部分：预绘交还后才能释放字体/章节；USB 与文件工作者交还存储后才能重挂。移动文件时保留取消、失败和重试路径。
  Ownership is part of the API: return prefetch borrows before freeing fonts/chapters, and return USB/worker storage ownership before remounting. Moves preserve cancellation, failure and retry paths.

## 构建与验证 / Build and verification

```sh
python3 tools/check_architecture.py
python3 tools/run_reader_baseline.py
idf.py --preview build
```

结构检查覆盖跨目录头文件依赖、产品源文件登记和既有测试入口；阅读回归执行真实模块及对应宿主替身，并保留 ASan/UBSan。构建和宿主测试不能替代设备显示、睡眠、触摸与存储验收。
Structural checks cover header boundaries, product source registration and existing test entry points. Reader regressions run real modules with host doubles and ASan/UBSan. Builds and host tests do not replace device display, sleep, input or storage acceptance.

## 兼容与第三方代码 / Compatibility and third-party code

read_pico_*、cst836u_*、fca9555_* 包含当前 S31 使用的兼容入口；名称不代表代码已失效。通用图像模块也保留 book_image_* 等原有函数名。删除前需核对构建、注册、实际消费者与宿主工具。
read_pico_*, cst836u_* and fca9555_* include active S31 compatibility interfaces. Generic image code also retains book_image_* API names. Check the build, registration, actual consumers and host tools before deletion.

第三方组件保留其许可和必要测试/示例；不能按目录大小判断它们都进入产品固件。生成字体、图标与波形表按各自生成流程维护。
Third-party components retain licenses and needed tests/examples. Directory size does not indicate what is linked into firmware. Maintain generated fonts, icons and waveform tables through their generators.
