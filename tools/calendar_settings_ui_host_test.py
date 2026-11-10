#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际日历选择页、字号、点按范围、返回与持久化失败反馈。
English: Native calendar choices, font sizes, tap boundaries, parent returns and failed-save feedback.
"""
import argparse
from pathlib import Path
import tempfile
import file_ui_host_test as native
import reader_title_style_preview as host
import ticket_background_ui_host_test as ticket
ROOT=Path(__file__).resolve().parents[1]
SETTINGS=ROOT/'main/apps/app_device_settings.c'

def unit():
    code=ticket.unit().split('static void paint',1)[0]
    code=code.replace('static app_calendar_style_t app_settings_calendar_style(void){return APP_CALENDAR_ALMANAC;}',
                      'static app_calendar_style_t selected_calendar;\nstatic app_calendar_style_t app_settings_calendar_style(void){return selected_calendar;}')
    code+='\nbool app_settings_set_calendar_style(app_calendar_style_t s){++writes;if(save_fails)return false;selected_style=4;selected_calendar=s;return true;}\n'
    render=host.function('render',SETTINGS)
    code+='\nstatic void paint(uint8_t *fb){ui_clear_page(fb);\n'
    code+=ticket.branch(render,'if (s_page == SETTINGS_CALENDAR)')+'\n'
    code+=ticket.branch(render,'if (s_page == SETTINGS_LOCK_STYLE)')+'\n}\n'
    gesture=host.function('on_gesture',SETTINGS)
    code+='\nstatic app_redraw_t tap(int x,int y){ui_gesture_event_t event={.x0=x,.y0=y};const ui_gesture_event_t *ev=&event;\n'
    for condition in ('if (s_page == SETTINGS_CALENDAR && y < 190)', 'if (s_page == SETTINGS_CALENDAR) {', 'if (s_page == SETTINGS_LOCK_STYLE) {'):
        code+=ticket.branch(gesture,condition)+'\n'
    code+='return APP_REDRAW_NONE;}\nstatic app_redraw_t back(void){\n'
    code+=ticket.branch(host.function('on_key',SETTINGS),'if (s_page == SETTINGS_CALENDAR)')+'\nreturn APP_REDRAW_NONE;}\n'
    code+=r'''
int main(int argc,char **argv){
    assert(argc==2&&ttf_font_open_builtin()==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);char path[1024];
    for(font_percent=100;font_percent<=200;font_percent+=10){
        selected_style=2;selected_calendar=APP_CALENDAR_MONTH;s_page=SETTINGS_LOCK_STYLE;unsigned before=writes;
        assert(tap(35,876)==APP_REDRAW_NONE&&tap(648,876)==APP_REDRAW_NONE);
        assert(tap(300,819)==APP_REDRAW_NONE&&tap(300,932)==APP_REDRAW_NONE);
        assert(tap(36,820)==APP_REDRAW_PAGE&&s_page==SETTINGS_CALENDAR&&selected_style==2&&writes==before);
        for(int choice=0;choice<2;++choice){
            EpdRect box={36,300+choice*154,612,132};
            assert(tap(35,box.y+66)==APP_REDRAW_NONE&&tap(648,box.y+66)==APP_REDRAW_NONE);
            assert(tap(300,432)==APP_REDRAW_NONE&&tap(300,453)==APP_REDRAW_NONE&&tap(300,586)==APP_REDRAW_NONE);
            assert(tap(36,box.y)==APP_REDRAW_PAGE&&selected_style==4&&selected_calendar==(app_calendar_style_t)choice);
            assert(tap(647,box.y+131)==APP_REDRAW_PAGE);
            save_fails=true;assert(tap(300,300+(1-choice)*154+66)==APP_REDRAW_PAGE);
            assert(selected_calendar==(app_calendar_style_t)choice&&strstr(s_notice,"保存失败"));paint(fb);
            save_fails=false;assert(tap(300,box.y+66)==APP_REDRAW_PAGE&&strstr(s_notice,"已设"));paint(fb);
            assert(pixel_at(fb,609,box.y+66)==0&&pixel_at(fb,609,300+(1-choice)*154+66)==0xf0);
            snprintf(path,sizeof(path),"%s/calendar-picker-%d-%u.raw",argv[1],choice,font_percent);save(path,fb);
            assert(back()==APP_REDRAW_PAGE&&s_page==SETTINGS_LOCK_STYLE);paint(fb);
            assert(pixel_at(fb,609,876)==0);snprintf(path,sizeof(path),"%s/calendar-parent-%d-%u.raw",argv[1],choice,font_percent);save(path,fb);
            assert(tap(647,931)==APP_REDRAW_PAGE&&s_page==SETTINGS_CALENDAR);
        }
        assert(tap(60,110)==APP_REDRAW_PAGE&&s_page==SETTINGS_LOCK_STYLE&&selected_style==4);
    }
    free(fb);ttf_font_unload();puts("PASS: native calendar entry and two choices at 100..200%, no mutation on entry, hit bounds/gaps, marks, back navigation and failed-save retry");
}
'''
    return code

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args()
    if args.output: native.run(args.output.resolve(),unit())
    else:
        with tempfile.TemporaryDirectory(prefix='calendar-settings-ui-') as folder:native.run(Path(folder),unit())
