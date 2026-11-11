#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: Apache-2.0
"""中文：执行蓝牙设置的真实控制器、原生字体和绘制，可导出 host 图。
English: Exercise the production Bluetooth controller, native fonts and rasterizer, with optional PNGs.
"""
import argparse
from pathlib import Path
import re
import tempfile

import file_ui_host_test as native
import reader_title_style_preview as host

ROOT = Path(__file__).resolve().parents[1]


def block(body, marker):
    start = body.index(marker)
    at, depth = body.index("{", start) + 1, 1
    while depth:
        depth += (body[at] == "{") - (body[at] == "}")
        at += 1
    return body[start:at]


def unit():
    code = native.unit().split("typedef struct {char path[288];", 1)[0]
    code += '\n#include "' + str(ROOT / 'components/ble_page_turner/include/ble_page_turner.h') + '"\n'
    fixture = (ROOT / "tools/bluetooth_ui_host_test.c").read_text()
    mocks, tests = fixture.split("// PRODUCTION_CONTROLLER\n")
    code += mocks
    code += re.sub(r'^#include[^\n]*\n', '', (ROOT / 'main/apps/settings_bluetooth.c').read_text(), flags=re.M)
    settings = ROOT / 'main/apps/app_device_settings.c'
    gesture, key = (host.function(name, settings) for name in ('on_gesture', 'on_key'))
    # Execute the owning page's actual routing too, preserving Back/Home/origin behavior.
    code += '''
enum {SETTINGS_MAIN, SETTINGS_BLUETOOTH};
static int s_page, requested_tab=-1, profile_prepares;
static bool s_ble_shortcut_back;
static char s_main_wifi[33];
static bool s_main_wifi_saved;
static void wifi_transfer_get_saved_wifi(char *name,bool *saved){name[0]=0;*saved=false;}
static void settings_prepare_profile(void){++profile_prepares;}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;requested_tab=tab;}
static app_redraw_t owner_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev){
'''
    code += block(gesture, 'if (s_page == SETTINGS_BLUETOOTH)')
    code += '\nreturn APP_REDRAW_NONE;}\nstatic app_redraw_t owner_key(app_ctx_t *ctx,int key){\n'
    for marker in ('if (s_page == SETTINGS_BLUETOOTH)', 'if (key == 1)', 'if (s_page != SETTINGS_MAIN)'):
        code += block(key, marker) + '\n'
    code += 'return APP_REDRAW_NONE;}\n'
    code += tests
    return code


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        native.run(args.output.resolve(), unit())
    else:
        with tempfile.TemporaryDirectory(prefix='bluetooth-ui-') as directory:
            native.run(Path(directory), unit())
