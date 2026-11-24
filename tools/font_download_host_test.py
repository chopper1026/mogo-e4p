"""在临时目录测试实际下载器，可传入真实 LXGWNeoXiHeiScreen.ttf。/ Exercise the real installer with an optional upstream TTF."""
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/services/font_download.c').read_text()
with tempfile.TemporaryDirectory(dir=root / 'build') as tmp:
    work = Path(tmp)
    unit = source[source.index('#define DIR '):source.index('static void worker')]
    (work / 'receive.inc').write_text(unit.replace('"/sdcard/fonts"', '"fonts"'))
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(work), '-I' + str(root / 'main/services'),
                    str(root / 'tools/font_download_host_test.c'), '-o', str(work / 'test')], check=True)
    packs = [str(Path(p).resolve()) for p in os.getenv('MOGO_TEST_FONT_PACKS', '').split(os.pathsep) if p]
    subprocess.run([str(work / 'test'), *packs[:1]], cwd=work, check=True)
