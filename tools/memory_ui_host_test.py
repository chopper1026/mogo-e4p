#!/usr/bin/env python3
"""中文：验证内存页采样、清理、返回及真实字体绘制，可导出原生 PNG。
English: Check memory-page sampling, cleanup, navigation and native font rendering, optionally exporting PNGs.
"""
import argparse
from pathlib import Path
import re
import tempfile

import file_ui_host_test as native
import reader_title_style_preview as host

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = ROOT / "main/apps/app_device_settings.c"


def block(body, marker):
    start = body.index(marker)
    at, depth = body.index("{", start) + 1, 1
    while depth:
        depth += (body[at] == "{") - (body[at] == "}")
        at += 1
    return body[start:at]


def unit():
    source = SETTINGS.read_text()
    code = native.unit().split("typedef struct {char path[288];", 1)[0]
    code += r'''
#include "memory_manager.h"
#include "display_geometry.h"
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif
static mm_snapshot_t backend_snapshot,s_memory_snapshot;
static mm_clean_report_t backend_report,s_memory_report;
static int64_t s_memory_next_sample_ms,s_memory_last_present_ms;
static bool s_memory_shortcut_back,s_memory_present_pending,s_memory_dirty,s_enter_memory_from_quick;
static bool s_enter_ble_from_quick,s_enter_haptics_from_quick,s_enter_network_from_quick;
static unsigned samples,reports,requests,profile_prepares;
static int requested_tab=-1;
static char s_main_wifi[33];
static bool s_main_wifi_saved;
static void settings_prepare_profile(void){++profile_prepares;}
static int wifi_transfer_get_saved_wifi(char *ssid,bool *saved){ssid[0]=0;*saved=false;return 0;}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;requested_tab=tab;}
void memory_inventory_snapshot(const display_state_t *display,mm_snapshot_t *out){(void)display;*out=backend_snapshot;++samples;}
void mm_get_clean_report(mm_clean_report_t *out){*out=backend_report;++reports;}
esp_err_t mm_request_clean(mm_clean_reason_t reason,uint32_t *id){
 assert(reason==MM_CLEAN_MEMORY_PAGE&&!id);++requests;
 if(backend_snapshot.clean_state==MM_CLEAN_REQUESTED||backend_snapshot.clean_state==MM_CLEAN_RUNNING)return ESP_ERR_INVALID_STATE;
 backend_snapshot.clean_state=MM_CLEAN_REQUESTED;++backend_snapshot.clean_request_id;return ESP_OK;
}
const char *mm_owner_name(mm_owner_t owner){
 const char *names[]={"系统/显示","阅读/字体","UI 缓存","网络传输","文件任务","未追踪/外部"};return names[owner];
}
static void ui_draw_button(uint8_t *fb,EpdRect rect,const char *text,bool active){
 ui_fill_round_rect(fb,rect,8,active?UI_GRAY_LIGHT:UI_GRAY_WHITE);
 ui_draw_control_frame(fb,rect,8,UI_GRAY_BLACK);ui_text_box(fb,rect,24,text,EPD_DRAW_ALIGN_CENTER,false);
}
'''
    code += re.search(r"typedef enum \{ SETTINGS_MAIN,.*?\} settings_page_t;", source, re.S).group(0)
    code += "\nstatic settings_page_t s_page,s_enter_page;\n"
    for name in ("settings_card", "settings_divider", "back_header", "memory_size_text", "memory_clean_state_text",
                 "memory_snapshot_changed", "memory_pool_card", "memory_area_hint", "memory_render_content",
                 "app_device_settings_request_memory"):
        code += host.function(name, SETTINGS) + "\n"
    code += "static void enter(app_ctx_t *ctx){s_memory_shortcut_back=false;s_page=SETTINGS_MAIN;\n"
    code += block(host.function("on_enter", SETTINGS), "if (s_enter_page == SETTINGS_MEMORY)")
    code += "\ns_enter_page=SETTINGS_MAIN;s_enter_memory_from_quick=false;}\n"
    code += "static app_redraw_t tick(app_ctx_t *ctx){\n"
    code += block(host.function("on_tick", SETTINGS), "if (s_page == SETTINGS_MEMORY)")
    code += "\nreturn APP_REDRAW_NONE;}\n"
    code += "static app_redraw_t tap(app_ctx_t *ctx,const ui_gesture_event_t *ev){\n"
    code += block(host.function("on_gesture", SETTINGS), "if (s_page == SETTINGS_MEMORY)")
    code += "\nreturn APP_REDRAW_NONE;}\n"
    code += "static app_redraw_t key(app_ctx_t *ctx,int key){bool home=key==1;\n"
    for marker in ("if (home)", "if (s_page == SETTINGS_MEMORY && s_memory_shortcut_back)", "if (s_page == SETTINGS_MEMORY)"):
        code += block(host.function("settings_navigate", SETTINGS), marker) + "\n"
    code += "return APP_REDRAW_NONE;}\n"
    code += r'''
static void draw(app_ctx_t *ctx){ui_clear_page(ctx->fb);ui_nav_status(ctx->fb);back_header(ctx->fb,"内存管理");memory_render_content(ctx->fb);ui_nav_draw(ctx->fb,3);}
static void memory_save(const char *directory,const char *name,const uint8_t *fb){
 char path[1024];snprintf(path,sizeof(path),"%s/%s.raw",directory,name);FILE *out=fopen(path,"wb");assert(out);
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x)fputc(ui_read_pixel(fb,x,y)*16,out);fclose(out);
}
int main(int argc,char **argv){
 assert(argc==2);uint8_t *fb=malloc(684*1216/2),*before=malloc(684*1216/2);assert(fb&&before);
 app_ctx_t ctx={.fb=fb,.now_ms=100};
 backend_snapshot=(mm_snapshot_t){.pool={{.total=512*1024,.free_bytes=128*1024,.largest_free=64*1024,.minimum_free=96*1024,.used_bytes=384*1024},
  {.total=8*1024*1024,.free_bytes=3*1024*1024,.largest_free=2*1024*1024,.minimum_free=2*1024*1024,.used_bytes=5*1024*1024}},
  .owner={[MM_OWNER_SYSTEM]={.psram_bytes=1663488},[MM_OWNER_READER]={.psram_bytes=2*1024*1024},
          [MM_OWNER_UI_CACHE]={.psram_bytes=128*1024},[MM_OWNER_FILE_JOB]={.psram_bytes=32768},
          [MM_OWNER_UNTRACKED]={.internal_bytes=384*1024,.psram_bytes=5*1024*1024-1663488-2*1024*1024-128*1024-32768}}};
 app_device_settings_request_memory(true);enter(&ctx);assert(s_page==SETTINGS_MEMORY&&s_memory_shortcut_back&&s_enter_page==SETTINGS_MAIN);
 assert(tick(&ctx)==APP_REDRAW_NONE);unsigned count=samples;ctx.now_ms=599;assert(tick(&ctx)==APP_REDRAW_NONE&&samples==count);
 ++backend_snapshot.updated_ms;ctx.now_ms=600;assert(tick(&ctx)==APP_REDRAW_NONE);
 --backend_snapshot.pool[MM_POOL_PSRAM].free_bytes;ctx.now_ms=1100;assert(tick(&ctx)==APP_REDRAW_AREA&&s_memory_present_pending);
 s_memory_present_pending=false;
 backend_snapshot.pool[MM_POOL_PSRAM].free_bytes-=1024;ctx.now_ms=1600;assert(tick(&ctx)==APP_REDRAW_NONE);
 ctx.now_ms=2100;assert(tick(&ctx)==APP_REDRAW_AREA);s_memory_present_pending=false;
 backend_snapshot.clean_state=MM_CLEAN_PARTIAL;ctx.now_ms=2600;assert(tick(&ctx)==APP_REDRAW_AREA);s_memory_present_pending=false;
 ui_gesture_event_t event={.x0=342,.y0=920};unsigned request_count=requests;
 assert(tap(&ctx,&event)==APP_REDRAW_PAGE&&requests==request_count+1&&!s_memory_present_pending);
 assert(tap(&ctx,&event)==APP_REDRAW_PAGE&&requests==request_count+2&&!s_memory_present_pending);
 event.x0=10;assert(tap(&ctx,&event)==APP_REDRAW_NONE&&requests==request_count+2);
 event=(ui_gesture_event_t){.x0=85,.y0=1160};assert(tap(&ctx,&event)==APP_REDRAW_NONE&&requested_tab==0);
 for(int origin=0;origin<2;++origin)for(int back=0;back<3;++back){
  ctx.request_return=false;app_device_settings_request_memory(origin!=0);enter(&ctx);
  event=(ui_gesture_event_t){.x0=58,.y0=114};
  app_redraw_t redraw=back==0?tap(&ctx,&event):key(&ctx,back==1?0:2);
  assert(redraw==(origin?APP_REDRAW_NONE:APP_REDRAW_PAGE)&&ctx.request_return==(origin!=0));
 }
 app_device_settings_request_memory(true);enter(&ctx);ctx.request_return=false;requested_tab=-1;
 assert(key(&ctx,1)==APP_REDRAW_NONE&&requested_tab==0&&!ctx.request_return);
 app_device_settings_request_memory(false);enter(&ctx);
 unsigned read_count=samples+reports,old_requests=requests;
 for(unsigned scale=100;scale<=200;scale+=100){font_percent=scale;draw(&ctx);memory_save(argv[1],scale==100?"memory-normal":"memory-large-font",fb);}
 memcpy(before,fb,684*1216/2);assert(ui_font_cache_clear()>0);assert(ui_font_cache_clear()==0);
 draw(&ctx);assert(!memcmp(before,fb,684*1216/2)&&samples+reports==read_count&&requests==old_requests);
 for(int state=MM_CLEAN_IDLE;state<=MM_CLEAN_PARTIAL;++state){
  s_memory_snapshot.clean_state=state;s_memory_snapshot.clean_request_id=12345;
  s_memory_snapshot.pressure=state==MM_CLEAN_PARTIAL;s_memory_snapshot.allocation_failures=1234;
  s_memory_snapshot.inventory_partial=state==MM_CLEAN_PARTIAL;
  s_memory_report.released_psram=1024*1024;s_memory_report.released_internal=4096;
  draw(&ctx);if(state==MM_CLEAN_PARTIAL)memory_save(argv[1],"memory-partial",fb);
 }
 memset(before,0x33,684*1216/2);memcpy(fb,before,684*1216/2);memory_render_content(fb);
 const EpdRect area=display_rect_to_epd(memory_area_hint(&ctx));
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
  if(x<area.x||x>=area.x+area.width||y<area.y||y>=area.y+area.height)
   assert(ui_read_pixel(fb,x,y)==ui_read_pixel(before,x,y));
 (void)ui_font_cache_clear();free(fb);free(before);
 puts("PASS: 500ms samples, 1s data refresh, immediate state changes, no read/cleanup during drawing, button/nav/origin/Home routing, native 100/200% typography, all cleanup states, partial paint bounds and identical glyph-cache rebuild");
 return 0;
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
        with tempfile.TemporaryDirectory(prefix="memory-ui-") as directory:
            native.run(Path(directory), unit())
