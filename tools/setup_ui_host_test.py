"""三语离线引导、格式参数和内置字体覆盖。/ Offline setup, format signatures and embedded UI coverage."""
import json
import re
from fontTools.ttLib import TTFont
import subprocess
import tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
rows=[json.loads('['+line[1:-2]+']') for line in (root/'main/ui/locale_catalog.inc').read_text().splitlines() if line.startswith('{')]
keys=[r[0] for r in rows]
assert keys==sorted(set(keys))
formats=re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|[lhzjtL])?[diuoxXfFeEgGaAcsp])')
chars=set('简体中文繁體中文English霞鹜新晰黑霞鶩新晰黑')
for key,tw,en in rows:
    assert formats.findall(key)==formats.findall(tw)==formats.findall(en),key
    for value in (key,tw,en):chars.update(value)
# 核对交付的字库，不只检查生成器的输入。/ Inspect the shipped font, not just generator inputs.
with TTFont(root/'main/assets/builtin.ttf') as font:
    missing = sorted(ord(c) for c in chars if c not in '\r\n\t' and ord(c) not in font.getBestCmap())
    assert not missing, missing
with tempfile.TemporaryDirectory(dir=root/'build') as temp:
    t=Path(temp);source=(root/'main/apps/app_setup.c').read_text();(t/'setup_page.inc').write_text(re.sub(r'^#include .*$', '', source, flags=re.M))
    # 测试映像用其只读字符串的最小/最大地址替代 S31 链接符号，查找代码保持原样。
    # Bound the host image's literals in place of S31 linker symbols; retain the real lookup.
    catalog = (root/'main/ui/ui_locale.c').read_text().replace('#include "ui_locale.h"', '#undef ui_tr')
    catalog = catalog.replace('extern const char _rodata_start[], _rodata_end[];',
                              'extern uintptr_t _rodata_start, _rodata_end;')
    (t/'catalog.inc').write_text(catalog)
    subprocess.run(['cc','-std=gnu11','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',*['-I'+str(p) for p in [t,root/'tools/transfer_ui_stubs',root/'components/wifi_transfer/include',root/'main/app',root/'main/settings',root/'main/services',root/'main/ui']],str(root/'tools/setup_ui_host_test.c'),'-o',str(t/'test')],check=True)
    subprocess.run([str(t/'test')],check=True)
