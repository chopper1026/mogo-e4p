# mogo E4p 1.2.1 · LGPL 重新链接材料 / LGPL relinking inputs

## 下载重新链接材料 / Download the relinking inputs

下载本提交的 GitHub 源码归档，解包后在目录中执行：
Download GitHub's archive of this commit, unpack it, and run these commands inside:

```sh
shasum -a 256 -c SHA256SUMS
cat chunks/part-* > official-relink.zip
shasum -a 256 -c RELINK_ARCHIVE.sha256
unzip official-relink.zip -d relink-inputs
(cd relink-inputs && shasum -a 256 -c ../INPUT_SHA256SUMS)
```

`official-relink.zip` 逐字节对应正式 CI 的 `lgpl-relink` 产物（73,872,621 字节）；
分块存于本 Git 引用，不依赖 Actions 的保留期限。将解出的 `relink-inputs/` 内容覆盖到对应固件源码根目录，再按下文执行重链接。
The reconstructed archive is byte-identical to the official CI `lgpl-relink` artifact (73,872,621 bytes).
The chunks are tracked by this Git ref rather than expiring with Actions artifacts. Overlay the unpacked `relink-inputs/` on the tagged firmware source and follow the instructions below.

- 对应固件源代码 / Corresponding firmware source: [v1.2.1](https://github.com/chopper1026/mogo-e4p/tree/8629fdd614effb7962bbc42daee2f0941b553d2c)（由 GitHub 从标签生成源码下载 / use the tag source archive）。
- 同次固件构建 / Official build: [#45780030283](https://github.com/chopper1026/mogo-e4p/actions/runs/45780030283).
- 原始 ELF SHA-256 / Expected ELF SHA-256: `93dc7ce5fafc9b0ef429905538270cb489744cc0d778acfa2c387ce5af2ec7b3`.
- `INPUT_SHA256SUMS` 覆盖解包后的构建输入和依赖 / covers the extracted build inputs and dependencies.

本目录只补充标签没有的产物：构建使用的第三方组件源码与许可、静态库、应用目标代码、链接脚本与响应文件、sdkconfig、原链接命令等。不包含项目整仓源码，也不包含生成的 CloudZao 接入 ID 头文件。
This tree supplies the same-build inputs not present in the source tag: managed dependency sources and notices, static libraries, application objects, linker scripts and response files, sdkconfig and the original linker command. It does not duplicate the project source or include the generated CloudZao integration header.

1. 下载并解压 [v1.2.1 标签源码](https://github.com/chopper1026/mogo-e4p/archive/8629fdd614effb7962bbc42daee2f0941b553d2c.tar.gz)，再将 `relink-inputs/` 内容覆盖到源码根目录。/ Extract the tagged source, then overlay `relink-inputs/` at its root.
2. 使用 `linux/amd64` 的 `espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c` 容器，将合并目录挂载至 `/__w/mogo-e4p/mogo-e4p`，以该目录为工作目录。/ Run this container on `linux/amd64` with the combined tree mounted there as its working directory. ESP-IDF: `fff9895c82d744c7237be8847347bdd1b07c6643`.
3. 在容器内执行 / In the container, run:

   ```sh
   . "$IDF_PATH/export.sh"
   cd build
   cp Read_Pico.elf Read_Pico.reference.elf
   bash relink-command.sh
   cmp Read_Pico.reference.elf Read_Pico.elf
   ```

使用标签中的 LGPL 组件源码及随附依赖重建相应静态库后，可用原链接命令和本目录的应用目标代码重新链接。标签中的许可及第三方许可继续有效；项目学习条款不限制第三方授予的修改、调试和重新链接权利。
After rebuilding a modified LGPL library from the applicable tagged or supplied source, reuse the original linker command and the supplied application objects. The project study terms do not restrict modification, debugging, or relinking rights under third-party licenses.
