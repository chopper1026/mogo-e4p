"""手势设置的原生绘图与字号边界。/ Native gesture settings rendering and font-size bounds."""
import argparse
from pathlib import Path
import re
import tempfile
import file_ui_host_test as native
from reader_title_style_preview import enum_block, function

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT/'main/apps/app_device_settings.c'


def unit():
    code = native.unit().split('typedef struct {char path[288];', 1)[0]
    defines = (ROOT/'main/ui/ui_kit.h').read_text() + native.KIT.read_text()
    for key in ('UI_PX_BTN', 'UI_BTN_RADIUS', 'UI_SEL_INSET', 'UI_SEL_RING'):
        code += re.search(r'^#define '+key+r' \d+', defines, re.M).group() + '\n'
    code += '''static bool enabled; static unsigned preset; static char s_notice[96];
static bool app_settings_gestures(void){return enabled;}
static unsigned app_settings_gesture_width(void){return preset;}
'''
    code += function('app_settings_gesture_edge_px', ROOT/'main/settings/settings.c')+'\n'
    for name in ('ui_draw_selected_round_rect', 'ui_draw_choice_round_rect', 'ui_draw_button'):
        code += function(name, native.KIT) + '\n'
    for name in ('settings_card', 'settings_divider', 'back_header', 'section', 'setting_toggle_track',
                 'setting_toggle', 'gesture_width_rect', 'draw_gestures'):
        code += function(name, PAGE) + '\n'
    code += enum_block(PAGE, 'SETTINGS_MAIN', 'settings_page_t')
    code += '''static settings_page_t s_page;
static bool s_config_confirm,s_boot_pending,s_local_pending,s_upgrade_restart,s_upgrade_confirm;
static bool s_batt_calibration_confirm,s_reading_bg_confirm,s_network_shortcut_back;
static struct {bool busy;} s_update;
static int s_upgrade_job; enum {UPGRADE_JOB_NONE};
static bool pico_online_busy(void){return false;}
static app_redraw_t settings_navigate(app_ctx_t *ctx,bool home){
    (void)ctx; assert(!home && !s_config_confirm); s_page=SETTINGS_MAIN; return APP_REDRAW_PAGE;
}
'''
    code += function('settings_edge_back', PAGE) + '\n'
    code += '''int main(int argc,char **argv){
    assert(argc==2 && ttf_font_open_builtin()==ESP_OK);
    app_ctx_t ctx={0}; s_page=SETTINGS_CONFIG;s_config_confirm=true;strcpy(s_notice,"restore?");
    app_back_result_t back=settings_edge_back(&ctx);
    assert(back.accepted && back.redraw==APP_REDRAW_PAGE && !s_config_confirm && !s_notice[0] && s_page==SETTINGS_CONFIG);
    assert(settings_edge_back(&ctx).accepted && s_page==SETTINGS_MAIN && !settings_edge_back(&ctx).accepted);
    uint8_t *fb=malloc(684*1216/2); assert(fb); char path[1024];
    for(int scale=100;scale<=200;scale+=10)for(int state=0;state<=3;++state){
        enabled=state>0; preset=enabled?(unsigned)state-1:1; font_percent=scale;
        ui_clear_page(fb);ui_nav_status(fb);draw_gestures(fb);
        assert(pixel_at(fb,42,601)==(enabled?0xb0:0xf0));
        for(int y=862;y<918;++y)for(int x=48;x<636;++x)assert(pixel_at(fb,x,y)==0xf0);
        if(scale==100 && state==2){snprintf(path,sizeof(path),"%s/gestures.raw",argv[1]);save(path,fb);}
    }
    free(fb);ttf_font_unload();return 0;
}
'''
    return code


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gesture-settings-') as tmp:
        native.run((args.output or Path(tmp)).resolve(), unit())
