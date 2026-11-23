#!/usr/bin/env python3
"""运行离线 CloudZao 报文、下载、文件提交和确认回归。
Run offline CloudZao framing, download, publication and acknowledgement regressions.
"""
import os
from pathlib import Path
import platform
import re
import subprocess
import tempfile

from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
IDF = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf"))
PARSER = IDF / "components/http_parser"
JSON = ROOT / "managed_components/espressif__cjson/cJSON"

with tempfile.TemporaryDirectory(prefix="mogo-cloudzao-") as tmp:
    binary = Path(tmp) / "test"
    service = ROOT / "main/services/cloudzao_service.c"
    source = service.read_text()
    limits = re.findall(r"^#define CZ_(?:IDLE|TOKEN_REPLY_IDLE)_US .*$", source, re.M)
    assert len(limits) == 2
    worker = source[source.index("typedef struct {"):source.index("static void progress(")]
    names = ("cloudzao_cancel", "cloudzao_quiesce", "stop", "active", "would_block", "request")
    (Path(tmp) / "cloudzao_refresh_service.inc").write_text("\n".join(
        [*limits, worker, *(function(name, service) for name in names)]))
    # 复用实际升级提交门控，不复制产品逻辑。/ Exercise the real upgrade commit gate, not a copy.
    settings = function("on_tick", ROOT / "main/apps/app_device_settings.c")
    start = settings.index("        if (s_update.state == PICO_UPDATE_READY")
    end = settings.index("\n        uint32_t now", start)
    (Path(tmp) / "cloudzao_refresh_ota.inc").write_text(settings[start:end] + "\n")
    # 使用仓库 runner 的 ASan/UBSan 工具链。/ Use the repository runner's ASan/UBSan toolchain.
    subprocess.run([
        "clang", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-g", "-O1",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-Wno-deprecated-declarations", "-Imain/services", "-Itools/cloudzao_stubs",
        f"-I{PARSER}", f"-I{JSON}", f"-I{tmp}", str(PARSER / "http_parser.c"), str(JSON / "cJSON.c"),
        "tools/cloudzao_host_test.c", "tools/cloudzao_refresh_host_test.c",
        "main/services/cloudzao_wire.c", "main/services/cloudzao_receive.c",
        "main/services/cloudzao_history.c", "main/services/cloudzao_partner.c",
        *([] if platform.system() == "Darwin" else ["-lcrypto"]), "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary), tmp], check=True)
