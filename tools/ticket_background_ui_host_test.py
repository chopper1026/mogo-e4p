#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：真实票根背景选择页、系统字库、点按边界和保存失败回归，可导出 PNG。
English: Native ticket-background choices, UI fonts, hit bounds and failed-save feedback, with PNG export.
"""
import argparse
from pathlib import Path
import re
import tempfile

import file_ui_host_test as native
import reader_title_style_preview as host

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = ROOT / "main/apps/app_device_settings.c"
KIT = ROOT / "main/ui/ui_kit.c"


def branch(source, condition):
    at = source.index(condition)
    begin = source.index("{", at)
    scan = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', lambda m: " " * len(m[0]), source)
    end, depth = begin + 1, 1
    while depth:
        depth += (scan[end] == "{") - (scan[end] == "}")
        end += 1
    return source[at:end]


def unit():
    source = SETTINGS.read_text()
    code = native.unit().split("typedef struct {char path[288];", 1)[0]
    settings_header = (ROOT / "main/settings/settings.h").read_text()
    begin = settings_header.index("typedef enum {\n    APP_TICKET_BACKGROUND_TRANSPARENT")
    end = settings_header.index("} app_ticket_background_t;", begin) + len("} app_ticket_background_t;")
    code += "\n" + settings_header[begin:end] + "\n"
    code += source[source.index("typedef enum { SETTINGS_MAIN"):source.index("} settings_page_t;") + len("} settings_page_t;")]
    code += r'''
typedef enum {APP_CALENDAR_ALMANAC, APP_CALENDAR_MONTH} app_calendar_style_t;
static app_calendar_style_t app_settings_calendar_style(void){return APP_CALENDAR_ALMANAC;}
static settings_page_t s_page;
static app_ticket_background_t selected_background;
static uint8_t selected_style;
enum {APP_LOCK_STYLE_CLOCK=5};
static bool save_fails,s_bookmark_picker,s_collage_prepare_pending;
static char s_notice[96];
static unsigned writes,scans;
app_ticket_background_t app_settings_ticket_background(void){return selected_background;}
bool app_settings_set_ticket_background(app_ticket_background_t value){++writes;if(save_fails)return false;selected_background=value;return true;}
uint8_t app_settings_lock_style(void){return selected_style;}
void app_settings_set_lock_style(uint8_t value){selected_style=value;}
const char *app_settings_wallpaper_path(void){return "";}
static void wallpaper_scan(void){++scans;}
static void settings_divider(uint8_t *fb,int y,int x,int width){ui_draw_separator(fb,y,x,width,0x60);}
static void row(uint8_t *fb,int y,const char *label,const char *value){(void)fb;(void)y;(void)label;(void)value;assert(0);}
'''
    for name in ("settings_card", "back_header", "section", "settings_fit_text", "lock_style_row"):
        code += host.function(name, SETTINGS) + "\n"
    render = host.function("render", SETTINGS)
    code += "\nstatic void paint(uint8_t *fb){ui_clear_page(fb);\n"
    code += branch(render, "if (s_page == SETTINGS_TICKET_BACKGROUND)") + "\n"
    code += branch(render, "if (s_page == SETTINGS_LOCK_STYLE)") + "\n}\n"
    gesture = host.function("on_gesture", SETTINGS)
    code += "\nstatic app_redraw_t tap(int x,int y){ui_gesture_event_t event={.x0=x,.y0=y};const ui_gesture_event_t *ev=&event;\n"
    code += branch(gesture, "if (s_page == SETTINGS_TICKET_BACKGROUND && y < 190)") + "\n"
    code += branch(gesture, "if (s_page == SETTINGS_TICKET_BACKGROUND) {") + "\n"
    code += branch(gesture, "if (s_page == SETTINGS_LOCK_STYLE) {") + "\nreturn APP_REDRAW_NONE;}\n"
    key = host.function("on_key", SETTINGS)
    code += "\nstatic app_redraw_t back(void){\n"
    code += branch(key, "if (s_page == SETTINGS_TICKET_BACKGROUND)") + "\nreturn APP_REDRAW_NONE;}\n"
    code += r'''
int main(int argc,char **argv){
    assert(argc==2&&ttf_font_open_builtin()==ESP_OK);
    uint8_t *fb=malloc(684*1216/2);assert(fb);char path[1024];
    for(font_percent=100;font_percent<=200;font_percent+=10){
        selected_style=2;s_page=SETTINGS_LOCK_STYLE;selected_background=APP_TICKET_BACKGROUND_COVER;
        assert(tap(300,486)==APP_REDRAW_PAGE&&s_page==SETTINGS_TICKET_BACKGROUND&&selected_style==0);
        assert(selected_background==APP_TICKET_BACKGROUND_COVER&&!scans&&!s_collage_prepare_pending);
        for(unsigned choice=0;choice<2;++choice){
            EpdRect box={36,300+(int)choice*154,612,132};
            unsigned before=writes;
            assert(tap(35,box.y+66)==APP_REDRAW_NONE&&tap(648,box.y+66)==APP_REDRAW_NONE);
            assert(tap(300,432)==APP_REDRAW_NONE&&tap(300,453)==APP_REDRAW_NONE&&tap(300,586)==APP_REDRAW_NONE);
            assert(writes==before);
            assert(tap(36,box.y)==APP_REDRAW_PAGE&&selected_background==(app_ticket_background_t)choice);
            assert(tap(647,box.y+131)==APP_REDRAW_PAGE);
            save_fails=true;
            assert(tap(300,300+(int)(1-choice)*154+66)==APP_REDRAW_PAGE);
            assert(selected_background==(app_ticket_background_t)choice&&strstr(s_notice,"保存失败"));
            paint(fb);
            save_fails=false;
            assert(tap(300,box.y+66)==APP_REDRAW_PAGE&&!s_notice[0]);
            paint(fb);
            assert(pixel_at(fb,609,box.y+66)==0);
            assert(pixel_at(fb,609,300+(1-(int)choice)*154+66)==0xf0);
            assert(pixel_at(fb,10,500)==0xf0&&pixel_at(fb,500,750)==0xf0);
            snprintf(path,sizeof(path),"%s/ticket-background-%u-%u.raw",argv[1],choice,font_percent);save(path,fb);
            s_page=SETTINGS_LOCK_STYLE;paint(fb);
            snprintf(path,sizeof(path),"%s/lock-style-%u-%u.raw",argv[1],choice,font_percent);save(path,fb);
            assert(tap(300,486)==APP_REDRAW_PAGE&&selected_background==(app_ticket_background_t)choice);
        }
        assert(back()==APP_REDRAW_PAGE&&s_page==SETTINGS_LOCK_STYLE);
        assert(tap(300,486)==APP_REDRAW_PAGE&&tap(60,110)==APP_REDRAW_PAGE&&s_page==SETTINGS_LOCK_STYLE);
    }
    free(fb);ttf_font_unload();
    puts("PASS: native 100..200% ticket background choices, selection marks, tap edges/gaps, parent returns and failed-save retry");
}
'''
    return code


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        native.run(args.output.resolve(), unit())
    else:
        with tempfile.TemporaryDirectory(prefix="ticket-background-ui-") as directory:
            native.run(Path(directory), unit())
