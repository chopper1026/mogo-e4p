#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：验证已确认的阅读设置共用矩形、点按周期、二级页与间隙不穿透。
# English: Validate approved reading-sheet geometry, cycles, secondary destinations and inert gaps.
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
from reader_settings_test_support import geometry

ROOT = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {unsigned chapter,page,offset;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {READER_PANEL_NONE,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_TURN_SETTINGS,READER_PANEL_KEY_SETTINGS,READER_PANEL_CHROME_SETTINGS,READER_PANEL_TOOLS};
static int s_reader_panel, s_turns, invalidates, toggles[4], full_pages = 10, effect;
static int s_key_scroll,s_chrome_scroll;
static bool s_key_drag,s_chrome_drag;
static char s_chrome_error[64];
static bool ui_rect_hit(EpdRect r,unsigned x,unsigned y){return x>=(unsigned)r.x&&x<(unsigned)(r.x+r.width)&&y>=(unsigned)r.y&&y<(unsigned)(r.y+r.height);}
static unsigned app_settings_reader_full_pages(void){return full_pages;}
static void app_settings_set_reader_full_pages(unsigned n){full_pages=n;}
static unsigned app_settings_reader_turn_effect(void){return effect;}
static void app_settings_set_reader_turn_effect(unsigned n){effect=n;}
static app_redraw_t apply_reader_option(app_ctx_t *ctx,int i){(void)ctx;assert(i>=0&&i<4);++toggles[i];return APP_REDRAW_PAGE;}
static void invalidate_prep(void){++invalidates;}
static unsigned touch_toggles;
static app_redraw_t reader_toggle_touch(app_ctx_t *ctx){(void)ctx;++touch_toggles;return APP_REDRAW_PAGE;}
'''
source = ROOT/'main/apps/app_book.c'
unit+=geometry()+'\nstatic ui_reader_settings_group_t s_reader_settings_group;\n'
unit+='static unsigned scale=100;\nstatic unsigned app_settings_system_font_size(void){return scale;}\n'
for name in ('reader_sheet_back_rect', 'reading_settings_top', 'reading_setting_rect', 'reading_settings_action'):
    unit += function(name, source)+'\n'
unit += r'''
int main(void){
    app_ctx_t ctx={2,3,1234};
    const unsigned expected[]={15,30,0,5,10};
    s_reader_settings_group=UI_READER_SETTINGS_REFRESH;
    EpdRect interval=reading_setting_rect(0),ripple=reading_setting_rect(1);
    for(unsigned i=0;i<5;++i){s_turns=100;assert(reading_settings_action(&ctx,interval.x+10,interval.y+10)==APP_REDRAW_PAGE);assert(full_pages==(int)expected[i]&&!s_turns);}
    full_pages=77;reading_settings_action(&ctx,interval.x+10,interval.y+10);assert(full_pages==5);
    for(unsigned i=0;i<6;++i){reading_settings_action(&ctx,ripple.x+10,ripple.y+10);assert(effect==(int)((i+1)%2));}
    s_reader_settings_group=UI_READER_SETTINGS_INPUT;
    EpdRect touch=reading_setting_rect(9),keys=reading_setting_rect(3),mode=reading_setting_rect(2);
    assert(touch.width==612&&keys.y>mode.y&&keys.x==mode.x);
    assert(reading_settings_action(&ctx,touch.x,touch.y)==APP_REDRAW_PAGE&&touch_toggles==1);
    reading_settings_action(&ctx,mode.x,mode.y);assert(s_reader_panel==READER_PANEL_TURN_SETTINGS);
    reading_settings_action(&ctx,keys.x,keys.y);assert(s_reader_panel==READER_PANEL_KEY_SETTINGS&&!s_key_scroll&&!s_key_drag);
    s_reader_settings_group=UI_READER_SETTINGS_PAGE;
    EpdRect chrome=reading_setting_rect(7);s_chrome_scroll=100;s_chrome_drag=true;s_chrome_error[0]='x';
    reading_settings_action(&ctx,chrome.x,chrome.y);
    assert(s_reader_panel==READER_PANEL_CHROME_SETTINGS&&!s_chrome_scroll&&!s_chrome_drag&&!s_chrome_error[0]);
    for(int i=0;i<3;++i){EpdRect r=reading_setting_rect(4+i);reading_settings_action(&ctx,r.x,r.y);assert(toggles[i]==1);}
    EpdRect styles=reading_setting_rect(8);reading_settings_action(&ctx,styles.x,styles.y);assert(toggles[3]==1);
    for(scale=100;scale<=200;++scale)for(int group=0;group<UI_READER_SETTINGS_GROUP_COUNT;++group){
        s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
        EpdRect tab=ui_reader_settings_tab_rect(scale,group);
        s_reader_settings_group=(group+1)%UI_READER_SETTINGS_GROUP_COUNT;
        int before=invalidates;
        assert(reading_settings_action(&ctx,tab.x,tab.y)==APP_REDRAW_PAGE&&invalidates==before+1);
        assert(s_reader_settings_group==(ui_reader_settings_group_t)group);
        assert(reading_settings_action(&ctx,tab.x,tab.y)==APP_REDRAW_NONE);
        unsigned count=0;
        for(int i=0;i<10;++i){
            EpdRect r=reading_setting_rect(i);
            if(ui_reader_settings_group(i)!=s_reader_settings_group){assert(!r.width&&!r.height);continue;}
            ++count;assert(r.width==612&&r.y>=reading_settings_top()+196&&r.y+r.height<=1200);
            for(int j=0;j<i;++j)if(ui_reader_settings_group(j)==s_reader_settings_group){
                EpdRect q=reading_setting_rect(j);assert(r.y+r.height<=q.y||q.y+q.height<=r.y);
            }
            const int points[4][2]={{r.x,r.y},{r.x+r.width-1,r.y},{r.x,r.y+r.height-1},{r.x+r.width-1,r.y+r.height-1}};
            for(int k=0;k<4;++k){
                s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
                assert(reading_settings_action(&ctx,points[k][0],points[k][1])==APP_REDRAW_PAGE);
                if(i==2)assert(s_reader_panel==READER_PANEL_TURN_SETTINGS);
                else if(i==3)assert(s_reader_panel==READER_PANEL_KEY_SETTINGS);
                else if(i==7)assert(s_reader_panel==READER_PANEL_CHROME_SETTINGS);
            }
            s_reader_panel=READER_PANEL_REFRESH_SETTINGS;before=invalidates;
            assert(reading_settings_action(&ctx,r.x+r.width,r.y)==APP_REDRAW_NONE);
            assert(reading_settings_action(&ctx,r.x,r.y+r.height)==APP_REDRAW_NONE);
            assert(invalidates==before&&s_reader_panel==READER_PANEL_REFRESH_SETTINGS);
        }
        assert(count==(group==UI_READER_SETTINGS_INPUT?3:group==UI_READER_SETTINGS_PAGE?5:2));
        // Short groups do not expose the longer group's invisible rows.
        int empty_y=reading_settings_top()+196+(int)count*(120+(int)(scale-100)*28/100)+2;
        if(count<5){before=invalidates;assert(reading_settings_action(&ctx,342,empty_y)==APP_REDRAW_NONE&&before==invalidates);}
        reading_settings_action(&ctx,64,reading_settings_top()+55);assert(s_reader_panel==READER_PANEL_TOOLS);
        s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
        if(group!=UI_READER_SETTINGS_PAGE)assert(reading_settings_action(&ctx,342,1170)==APP_REDRAW_NONE&&s_reader_panel==READER_PANEL_REFRESH_SETTINGS);
        s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
        reading_settings_action(&ctx,342,reading_settings_top()-1);assert(s_reader_panel==READER_PANEL_NONE);
    }
    puts("PASS: grouped settings at 100-200%, all ten actions, reclaimed footer controls, hidden rows inert, tabs, top-left Back, cycles and failure-state reset");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    c,exe=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
