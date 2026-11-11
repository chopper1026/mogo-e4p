#!/usr/bin/env python3
"""中文：执行触摸震动直达设置的进入、控制及返回分支。/ English: Exercise actual haptics shortcut entry, controls and back branches."""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = ROOT / "main/apps/app_device_settings.c"


def branch(body, marker):
    start = body.index(marker)
    statement_end = body.index(";", start)
    if statement_end < body.index("{", start):
        return body[start:statement_end + 1]
    at = body.index("{", start) + 1
    depth = 1
    while depth:
        depth += (body[at] == "{") - (body[at] == "}")
        at += 1
    return body[start:at]


source = SETTINGS.read_text()
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
typedef struct {bool request_return,home;} app_ctx_t;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int x0;} ui_gesture_event_t;
static void settings_bluetooth_open(void){}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void ui_nav_request(app_ctx_t *ctx,int tab){assert(tab==0);ctx->home=true;}
static char s_main_wifi[33];
static bool s_main_wifi_saved;
static void wifi_transfer_get_saved_wifi(char *ssid,bool *saved){ssid[0]=0;*saved=false;}
static void settings_prepare_profile(void){}
static bool s_ble_shortcut_back,s_enter_ble_from_quick,s_haptics_shortcut_back,s_enter_haptics_from_quick;
static bool s_upgrade_confirm,s_upgrade_online,s_upgrade_shortcut_back;
enum {PICO_UPDATE_AVAILABLE};
static struct {int state;bool busy;} s_update;
static void upgrade_open(void){}
static bool available=true,enabled=true;
static uint8_t intensity=2;
static unsigned writes,pulses;
static bool haptics_available(void){return available;}
static bool app_settings_touch_haptics(void){return enabled;}
static void app_settings_set_touch_haptics(bool on){enabled=on;++writes;}
static void app_settings_set_haptic_intensity(uint8_t level){intensity=level;++writes;}
static void haptics_pulse(void){if(enabled)++pulses;}
'''
unit += re.search(r"typedef enum \{ SETTINGS_MAIN,.*?\} settings_page_t;", source, re.S).group(0) + "\n"
unit += "static settings_page_t s_page,s_enter_page;\n"
for name in ("app_device_settings_request_bluetooth", "app_device_settings_request_haptics"):
    unit += function(name, SETTINGS) + "\n"
entry = function("on_enter", SETTINGS)
unit += "static void enter_shortcut(void){\n" + re.search(r"s_page = SETTINGS_MAIN;", entry).group(0) + "\n"
unit += entry[entry.index("    s_ble_shortcut_back = false;"):entry.rfind("}")] + "}\n"
gesture = function("on_gesture", SETTINGS)
key = function("on_key", SETTINGS)
stale = "if (s_haptics_shortcut_back && s_page == SETTINGS_MAIN)"
unit += "static app_redraw_t tap(app_ctx_t *ctx,int x,int y){ui_gesture_event_t e={x};const ui_gesture_event_t *ev=&e;\n"
for marker in (stale, "if (s_page == SETTINGS_HAPTIC && s_haptics_shortcut_back && y < 190)", "if (s_page != SETTINGS_MAIN && y < 190)", "if (s_page == SETTINGS_HAPTIC)"):
    unit += branch(gesture, marker) + "\n"
unit += "return APP_REDRAW_NONE;}\n"
unit += "static app_redraw_t press(app_ctx_t *ctx,int key){\n"
for marker in (stale, "if (key == 1)", "if (s_page == SETTINGS_HAPTIC && s_haptics_shortcut_back)", "if (s_page != SETTINGS_MAIN)"):
    unit += branch(key, marker) + "\n"
unit += "return APP_REDRAW_NONE;}\n"
unit += r'''
int main(void){
 for(int origin=0;origin<2;++origin)for(int back=0;back<3;++back){
  app_ctx_t ctx={0};app_device_settings_request_haptics(origin!=0);enter_shortcut();
  assert(s_page==SETTINGS_HAPTIC&&s_haptics_shortcut_back==(origin!=0));
  assert(s_enter_page==SETTINGS_MAIN&&!s_enter_haptics_from_quick&&!s_enter_ble_from_quick);
  enabled=false;unsigned old_writes=writes,old_pulses=pulses;
  assert(tap(&ctx,500,320)==APP_REDRAW_PAGE&&enabled&&writes==old_writes+1&&pulses==old_pulses+1);
  for(int level=0;level<3;++level){
   assert(tap(&ctx,500,500+level*112)==APP_REDRAW_PAGE&&intensity==level&&!ctx.request_return);
   assert(s_haptics_shortcut_back==(origin!=0));
  }
  app_redraw_t redraw=back==0?tap(&ctx,58,114):press(&ctx,back==1?0:2);
  assert(redraw==(origin?APP_REDRAW_NONE:APP_REDRAW_PAGE));
  assert(ctx.request_return==(origin!=0)&&!s_haptics_shortcut_back);
  if(!origin)assert(s_page==SETTINGS_MAIN);
  enter_shortcut();assert(!s_haptics_shortcut_back&&s_page!=SETTINGS_HAPTIC);
 }
 app_ctx_t ctx={0};app_device_settings_request_haptics(true);enter_shortcut();
 assert(press(&ctx,1)==APP_REDRAW_NONE&&ctx.home&&!ctx.request_return);
 app_device_settings_request_haptics(false);enter_shortcut();assert(!s_haptics_shortcut_back);
 app_device_settings_request_haptics(true);app_device_settings_request_bluetooth(false);enter_shortcut();
 assert(s_page==SETTINGS_BLUETOOTH&&!s_haptics_shortcut_back&&!s_ble_shortcut_back);
 app_device_settings_request_bluetooth(true);app_device_settings_request_haptics(false);enter_shortcut();
 assert(s_page==SETTINGS_HAPTIC&&!s_haptics_shortcut_back&&!s_ble_shortcut_back);
 ctx=(app_ctx_t){0};available=false;unsigned old_writes=writes;
 assert(tap(&ctx,500,320)==APP_REDRAW_NONE&&writes==old_writes&&!ctx.request_return);
 app_device_settings_request_haptics(true);enter_shortcut();s_page=SETTINGS_MAIN;
 assert(tap(&ctx,500,320)==APP_REDRAW_NONE&&!s_haptics_shortcut_back);
 puts("PASS: haptics shortcut entry is one-shot; controls retain origin; touch/left/right return to origin; internal visits return to Settings; Home and superseding requests cannot retain a stale target");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="haptics-shortcut-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    src, exe = work / "test.c", work / "test"
    src.write_text(unit)
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-g", str(src), "-o", str(exe)], env=env, check=True)
    subprocess.run([str(exe)], env=env, check=True)
