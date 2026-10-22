#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：验证按键控制示意图按机身实体顺序摆位（中、左、右），卡片文字跟着同一张槽位表。
# English: Verify the key sheet follows the physical key order (middle, left, right) and that the
# card labels come from the same slot table used by hit testing.
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'main/apps/app_book.c'
text = source.read_text()

settings = (ROOT / 'main/settings.h').read_text()
enum = settings[settings.index('typedef enum {\n    APP_READER_KEY_PREV'):
               settings.index('} app_reader_key_action_t;') + len('} app_reader_key_action_t;')]
macros = text[text.index('#define KEY_SETTINGS_TOP'):
              text.index('static const unsigned s_reader_key_slots')]
slots = text[text.index('static const unsigned s_reader_key_slots'):
             text.index(';', text.index('static const unsigned s_reader_key_slots')) + 1]

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_BLACK 0x00
#define UI_GRAY_WHITE 0xff
#define UI_ICON_CHEVRON_DOWN 0
#define S_KEY_LABEL_Y 600
typedef int ui_icon_t;
'''
unit += enum + '\n'
unit += macros
unit += slots + ';\n'
unit += r'''
static uint8_t s_fb[1];
static char s_labels[3][16];
static char s_actions[3][16];
static int s_volume_text;
static int s_switch_x = -1, s_switch_on = -1;
static int s_circle_x = -1, s_circle_count;
static app_reader_key_action_t s_keys[3] = {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT};
static app_reader_key_action_t s_hold = APP_READER_KEY_HOME;
static app_reader_key_action_t s_double = APP_READER_KEY_NONE;
static app_reader_key_action_t app_settings_reader_key_action(unsigned key) { return s_keys[key]; }
static app_reader_key_action_t app_settings_reader_hold_action(void) { return s_hold; }
static app_reader_key_action_t app_settings_reader_double_action(void) { return s_double; }
static bool app_settings_reader_volume_turn(void) { return true; }
static void draw_sheet(uint8_t *fb, int top, const char *title) {(void)fb;(void)top;(void)title;}
static void draw_sheet_back(uint8_t *fb, int top) {(void)fb;(void)top;}
static void ui_fill_round_rect(uint8_t *fb, EpdRect r, int radius, uint8_t color) {(void)fb;(void)r;(void)radius;(void)color;}
static void ui_draw_control_frame(uint8_t *fb, EpdRect r, int radius, uint8_t color) {(void)fb;(void)r;(void)radius;(void)color;}
static void ui_draw_icon(uint8_t *fb, int cx, int cy, int size, ui_icon_t icon, uint8_t gray) {
    (void)fb;(void)cx;(void)cy;(void)size;(void)icon;(void)gray;
}
static void epd_draw_circle(int x, int y, int r, uint8_t color, uint8_t *fb) {
    (void)color;(void)fb;
    if (y == KEY_ICON_Y && r == 25) { s_circle_x = x; ++s_circle_count; }
}
static void ui_text_box(uint8_t *fb, EpdRect box, int px, const char *s,
                        enum EpdFontFlags align, bool inv) {
    (void)fb;(void)px;(void)align;(void)inv;
    if (box.y == KEY_CARDS_TOP + 52) {
        int slot = (box.x - 48) / 208;
        assert(slot >= 0 && slot < 3);
        snprintf(s_actions[slot], sizeof(s_actions[slot]), "%s", s);
    }
    if (box.y == KEY_VOLUME_TOP + 24 && box.x == 58) s_volume_text = 1;
}
static void reader_panel_text_vc(uint8_t *fb, int x, int cy, int px, const char *s,
                                 enum EpdFontFlags align, bool inv) {
    (void)fb;(void)px;(void)align;(void)inv;
    if (cy != S_KEY_LABEL_Y) return;
    int slot = (x - 134) / 208;
    assert(slot >= 0 && slot < 3);
    snprintf(s_labels[slot], sizeof(s_labels[slot]), "%s", s);
}
static void draw_reading_switch(uint8_t *fb, EpdRect track, bool on) {
    (void)fb;
    s_switch_x = track.x;
    s_switch_on = on;
}
'''
for name in ('reader_key_card', 'reader_key_label', 'draw_key_settings'):
    unit += function(name, source) + '\n'
unit += r'''
int main(void) {
    // 左格是中键圆圈，中格是左键，右格是右键；卡片文字跟着逻辑键走。
    // The left slot is the middle-key circle, the middle slot the left key, the right the right key.
    assert(s_reader_key_slots[0] == 1 && s_reader_key_slots[1] == 0 && s_reader_key_slots[2] == 2);
    draw_key_settings(s_fb);
    assert(!strcmp(s_labels[0], "中键") && !strcmp(s_labels[1], "左键") && !strcmp(s_labels[2], "右键"));
    assert(s_circle_count == 1 && s_circle_x == 134);
    assert(!strcmp(s_actions[0], "阅读工具栏"));
    assert(!strcmp(s_actions[1], "上一页"));
    assert(!strcmp(s_actions[2], "下一页"));
    // 音量键翻页在双击行下方，开关落在该行右侧。
    // The volume-key row sits under the double press with its switch on the right.
    assert(KEY_VOLUME_TOP >= KEY_DOUBLE_TOP + KEY_DOUBLE_HEIGHT);
    assert(s_volume_text && s_switch_x == 556 && s_switch_on == 1);
    puts("PASS: key sheet keeps the physical middle/left/right order and the volume row below the double press");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    c, exe = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
