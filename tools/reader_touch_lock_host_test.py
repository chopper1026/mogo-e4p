#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：验证真实阅读触摸开关、快捷键、双击及实体键恢复路径。
English: Exercise actual reader touch toggles, bindings, double presses and physical-key recovery.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
book = ROOT / 'main/apps/app_book.c'
settings = (ROOT / 'main/settings/settings.h').read_text()
start = settings.index('typedef enum {\n    APP_READER_KEY_PREV')
end = settings.index('} app_reader_key_action_t;', start) + len('} app_reader_key_action_t;')
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { bool touched; } app_touch_t;
typedef struct {int64_t now_ms; bool user_activity,request_return;app_touch_t *touch;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {SHELF,READING,TOC,READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_NOTE};
#define ESP_OK 0
#define READER_MIDDLE_DOUBLE_MS 340
#define READER_VOLUME_POLL_MS 30
#define READER_UP_KEY_DEBOUNCE_MS 30
#define READER_UP_KEY_LONG_MS 1000
#define BOARD_VOLUME_UP 1
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,s_reader_slider=-1,s_pressed_control=-1;
static int64_t s_stats_activity_ms,s_reader_notice_until;
static char s_reader_notice[128];
static bool s_reader_tools_toggle,s_middle_pending;
static int64_t s_middle_pending_ms;
static struct {bool active;} s_dictionary_selection;
static bool disabled,bound,fail_save,volume_enabled=true,up_pressed;
static uint8_t volume_keys;
static unsigned turns,up_reads,volume_reads,invalidations;
static bool app_settings_reader_touch_disabled(void){return disabled;}
static bool app_settings_reader_touch_toggle_bound(void){return bound;}
static bool app_settings_set_reader_touch_disabled(bool on){if(fail_save||(on&&!bound))return false;disabled=on;return true;}
static void reader_indent_hold_cancel(void){}
static void dictionary_cancel(void){s_dictionary_selection.active=false;}
static void middle_pending_clear(void){s_middle_pending=false;}
static void invalidate_prep(void){++invalidations;}
static void copy_text(char *out,size_t cap,const char *text){snprintf(out,cap,"%s",text);}
static bool app_book_reader_body_visible(void){return s_view==READING&&s_reader_panel==READER_PANEL_NONE;}
static app_redraw_t dictionary_dismiss(void){dictionary_cancel();return APP_REDRAW_PAGE;}
static app_redraw_t turn_page(app_ctx_t *ctx,int direction){(void)ctx;assert(direction==1||direction==-1);++turns;s_reader_panel=READER_PANEL_NONE;return APP_REDRAW_PAGE;}
static app_redraw_t reader_return(app_ctx_t *ctx){ctx->request_return=true;return APP_REDRAW_PAGE;}
static app_redraw_t toggle_reader_fullscreen(app_ctx_t *ctx){(void)ctx;return APP_REDRAW_PAGE;}
static app_redraw_t reader_manual_refresh(app_ctx_t *ctx){(void)ctx;return APP_REDRAW_FULL;}
static bool frontlight_ready(void){return false;}
static bool app_settings_frontlight_on(void){return false;}
static void app_settings_set_frontlight_on(bool on){(void)on;}
static unsigned app_settings_frontlight_brightness(void){return 40;}
static unsigned app_settings_frontlight_cct(void){return 50;}
static int frontlight_apply(bool on,unsigned brightness,unsigned cct){(void)on;(void)brightness;(void)cct;return 0;}
static int board_read_up_key(bool *pressed){++up_reads;*pressed=up_pressed;return ESP_OK;}
static int board_read_volume_keys(uint8_t *keys){++volume_reads;*keys=volume_keys;return ESP_OK;}
static bool app_settings_reader_volume_turn(void){return volume_enabled;}
'''
unit += settings[start:end] + '\n'
unit += r'''
static app_reader_key_action_t s_middle_pending_action,up_action=APP_READER_KEY_TOUCH;
static app_reader_key_action_t single_action=APP_READER_KEY_TOUCH,double_action=APP_READER_KEY_NONE;
static app_reader_key_action_t app_settings_reader_up_action(void){return up_action;}
static app_reader_key_action_t app_settings_reader_key_action(unsigned key){assert(key==1);return single_action;}
static app_reader_key_action_t app_settings_reader_double_action(void){return double_action;}
'''
for name in ('reader_notice_tick','reader_toggle_touch','screen_touch_disabled','toggle_reader_tools_panel',
             'reader_key_action','middle_short_press','middle_pending_tick',
             'reader_up_key_tick','reader_volume_tick'):
    unit += function(name, book) + '\n'
unit += r'''
int main(void){
 app_touch_t contact={0};app_ctx_t ctx={.now_ms=1000,.touch=&contact};
 assert(!screen_touch_disabled(&ctx));
 reader_toggle_touch(&ctx);assert(!disabled&&strstr(s_reader_notice,"按键控制")&&strstr(s_reader_notice,"触摸开关")&&s_reader_notice_until==3000);
 ctx.now_ms=2999;assert(reader_notice_tick(&ctx)==APP_REDRAW_NONE&&s_reader_notice[0]);
 ctx.now_ms=3000;assert(reader_notice_tick(&ctx)==APP_REDRAW_PAGE&&!s_reader_notice[0]);
 assert(reader_notice_tick(&ctx)==APP_REDRAW_NONE);
 s_view=SHELF;reader_toggle_touch(&ctx);ctx.now_ms+=2000;
 assert(reader_notice_tick(&ctx)==APP_REDRAW_NONE&&!s_reader_notice[0]);s_view=READING;
 bound=true;fail_save=true;reader_toggle_touch(&ctx);assert(!disabled&&strstr(s_reader_notice,"未保存"));
 fail_save=false;s_reader_panel=READER_PANEL_TOOLS;s_dictionary_selection.active=true;s_reader_slider=2;s_pressed_control=1;
 reader_key_action(&ctx,APP_READER_KEY_TOUCH);
 assert(disabled&&screen_touch_disabled(&ctx)&&!s_dictionary_selection.active&&s_reader_panel==READER_PANEL_NONE&&s_reader_slider==-1&&s_pressed_control==-1&&ctx.user_activity);
 assert(strstr(s_reader_notice,"触摸已关闭")&&strstr(s_reader_notice,"快捷键恢复"));
 for(int view=SHELF;view<=TOC;++view){s_view=view;assert(screen_touch_disabled(&ctx)==(view==READING));}s_view=READING;
 // 绑定在中键双击、单击时，从任意阅读弹层都能恢复；失败保留关闭状态。
 // Middle double/single bindings recover from reader sheets; failed saves keep the disabled policy.
 s_reader_panel=READER_PANEL_TOOLS;single_action=APP_READER_KEY_NEXT;double_action=APP_READER_KEY_TOUCH;
 assert(middle_short_press(&ctx)==APP_REDRAW_NONE&&disabled);ctx.now_ms+=100;
 assert(middle_short_press(&ctx)==APP_REDRAW_PAGE&&!disabled&&!turns);
 reader_toggle_touch(&ctx);single_action=APP_READER_KEY_TOUCH;double_action=APP_READER_KEY_NONE;
 assert(middle_short_press(&ctx)==APP_REDRAW_PAGE&&!disabled);
 reader_toggle_touch(&ctx);fail_save=true;reader_key_action(&ctx,APP_READER_KEY_TOUCH);assert(disabled);fail_save=false;
 // 上键恢复在正文、工具栏和说明弹层均可用，音量键在禁触工具栏中仍可翻页。
 // Up-key recovery works in the body, toolbar and note sheet; volume turns work in locked reader sheets.
 for(int panel=READER_PANEL_NONE;panel<=READER_PANEL_NOTE;++panel){
  disabled=true;s_reader_panel=panel;ctx.now_ms+=1000;up_pressed=true;reader_up_key_tick(&ctx);
  ctx.now_ms+=80;up_pressed=false;assert(reader_up_key_tick(&ctx)==APP_REDRAW_PAGE&&!disabled);
 }
 disabled=true;s_reader_panel=READER_PANEL_TOOLS;up_action=APP_READER_KEY_NEXT;
 ctx.now_ms+=1000;up_pressed=true;reader_up_key_tick(&ctx);ctx.now_ms+=80;up_pressed=false;
 assert(reader_up_key_tick(&ctx)==APP_REDRAW_PAGE&&turns==1);
 disabled=true;s_reader_panel=READER_PANEL_TOOLS;ctx.now_ms+=1000;volume_keys=0;reader_volume_tick(&ctx);
 ctx.now_ms+=80;volume_keys=1;assert(reader_volume_tick(&ctx)==APP_REDRAW_PAGE&&turns==2);
 s_view=SHELF;unsigned before=volume_reads;ctx.now_ms+=1000;reader_volume_tick(&ctx);assert(volume_reads==before);
 // 绑定双击后挂起的单击只在超时且抬手后执行一次。
 // A deferred single after a double binding fires once after timeout and lift.
 s_view=READING;disabled=false;single_action=APP_READER_KEY_TOUCH;double_action=APP_READER_KEY_NEXT;
 middle_short_press(&ctx);ctx.now_ms+=400;assert(middle_pending_tick(&ctx)==APP_REDRAW_PAGE&&disabled);
 assert(middle_pending_tick(&ctx)==APP_REDRAW_NONE);
 assert(invalidations>=10&&up_reads>=8);
 puts("PASS: actual reader touch toggle prerequisites, failures, page scope, panel recovery, middle single/double/timeout, up-key and volume dispatch");
}
'''
with tempfile.TemporaryDirectory(prefix='touch-lock-') as directory:
    folder = Path(directory)
    source, binary = folder / 'test.c', folder / 'test'
    source.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
