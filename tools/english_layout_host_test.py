"""英文控件用产品词典和原生字形回归，可导出预览。/ Regress English controls with the shipped catalog and glyphs, with optional previews."""
import argparse
from pathlib import Path
import tempfile

import file_ui_host_test as native
import reader_title_style_preview as host

ROOT = native.ROOT
BOOK = ROOT / 'main/apps/app_book.c'
SETTINGS = ROOT / 'main/apps/app_device_settings.c'


def with_english(code):
    # 宿主没有 S31 链接符号，以词典只读区代替；保留生产二分查找和 RAM 保护。
    # Bound the host literals in place of S31 linker symbols; keep actual lookup and the RAM guard.
    locale = (ROOT / 'main/ui/ui_locale.c').read_text().replace('#include "ui_locale.h"', '''
#include "ui_language.h"
#undef ui_tr
const char *ui_tr(const char *) __attribute__((format_arg(1)));
ui_language_t ui_language(void){return UI_LANG_EN;}
''').replace('extern const char _rodata_start[], _rodata_end[];',
             'extern uintptr_t _rodata_start, _rodata_end;')
    return locale + r'''
uintptr_t _rodata_start=UINTPTR_MAX,_rodata_end;
static void bound_catalog(void){
    for(unsigned i=0;i<sizeof(catalog)/sizeof(*catalog);++i){
        const char *items[]={catalog[i].key,catalog[i].tw,catalog[i].en};
        for(unsigned j=0;j<3;++j){uintptr_t a=(uintptr_t)items[j],b=a+strlen(items[j])+1;
            if(a<_rodata_start)_rodata_start=a;if(b>_rodata_end)_rodata_end=b;}
    }
}
''' + code


def unit():
    code = with_english(native.unit(True).split('int main(int argc,char **argv)')[0])
    # 在生产字面选择后观察最终绘制字号；检查同组字不串列，不仅检查坐标点。
    # Observe the final painted size and extent after localization, not just the anchors.
    marker = 'void ui_text_fixed_context_vc('
    code = code.replace(marker, '''
static int group_y, group_px, group_n, last_right;
static const char *expect_suffix;
static int min_value_px;
static void observe(int x,int y,int px,const char *s,enum EpdFontFlags align){
    if (!group_y || group_y != y || !*s) return;
    int w=ui_text_fixed_width_px(px,s);
    int left=x-(align==EPD_DRAW_ALIGN_CENTER?w/2:align==EPD_DRAW_ALIGN_RIGHT?w:0);
    if(group_px<0)group_px=px;
    if(group_px>0)assert(group_px==px);
    assert(px>=14 && left>=last_right+4 && left+w<=648);
    if(expect_suffix && align==EPD_DRAW_ALIGN_RIGHT){
        size_t n=strlen(s),m=strlen(expect_suffix);assert(n>m && !strcmp(s+n-m,expect_suffix));
        assert(px>=min_value_px);expect_suffix=NULL;
        for(size_t i=0;i<n-m;){assert(!memcmp(s+i,"图",3));i+=3;}
    } else for(const unsigned char *p=(const unsigned char *)s;*p;++p) assert(*p<128);
    last_right=left+w;++group_n;
}
''' + marker, 1)
    marker = 'if (!framebuffer || !text || !text[0] || px <= 0) return;'
    code = code.replace(marker, marker+'\n    observe(x,center_y,px,text,align);', 1)
    code += '''
#define BOOK_TOOL_COUNT 5
#define READER_WIDTH 684
#define READER_HEIGHT 1216
#define READER_BAR_TOP 1096
#define READER_TOOL_ICON_PX 40
static bool marked,s_shelf_selecting;
static int s_pressed_control=-1;
static bool bookmark_is_current(void){return marked;}
static void check_line(int cy,bool uniform){group_y=cy;group_px=uniform?-1:0;group_n=last_right=0;}
'''
    for name in ('reader_tools_area', 'tool_rect', 'draw_reader_tool_icon', 'draw_reader_toolbar',
                 'shelf_header_action_rect', 'draw_shelf_header_button'):
        code += host.function(name, BOOK) + '\n'
    code += host.function('ui_text_value_box', native.KIT) + '\n'
    code += host.function('settings_pair', SETTINGS) + '\n'
    code += r'''
int main(int argc,char **argv){
    assert(argc==2 && ttf_font_open_builtin()==ESP_OK);
    bound_catalog();
    char user_name[]="设置";assert(!strcmp(ui_tr("设置"),"Settings") && ui_tr(user_name)==user_name);
    char pager[40];snprintf(pager,sizeof(pager),ui_tr("第 %d 页 · 共 %d 页"),1,2);assert(!strcmp(pager,"Page 1 of 2"));
    uint8_t *fb=malloc(684*1216/2);assert(fb);app_ctx_t ctx={.fb=fb};char path[1024];
    for(font_percent=100;font_percent<=200;font_percent+=10){
        group_y=0;fixture(true);render(&ctx,fb);
        for(int i=0;i<2;++i){
            int cy=i?385:347;check_line(cy,true);render(&ctx,fb);assert(group_n==4);
        }
        if(font_percent%50==0){snprintf(path,sizeof(path),"%s/files-en-%u.raw",argv[1],font_percent);save(path,fb);}
        ui_clear_page(fb);check_line(120,true);
        for(int i=0;i<4;++i)draw_shelf_header_button(fb,shelf_header_action_rect(i),i);
        assert(group_n==3);
        int normal=0;
        for(int i=0;i<2;++i){marked=i;check_line(1179,true);draw_reader_toolbar(fb);assert(group_n==5);
            if(!i)normal=group_px;else assert(group_px==normal);}
        check_line(500,false);
        settings_pair(fb,(EpdRect){100,463,494,74},25,"蓝牙翻页器",20,"已关闭");assert(group_n==2);
        check_line(650,false);expect_suffix="…";min_value_px=ui_text_effective_px(20)*3/4;
        const char long_name[]="图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图图";
        settings_pair(fb,(EpdRect){100,613,494,74},25,"WiFi",20,long_name);assert(group_n==2&&!expect_suffix);
        if(font_percent%50==0){snprintf(path,sizeof(path),"%s/controls-en-%u.raw",argv[1],font_percent);save(path,fb);}
    }
    free(fb);ttf_font_unload();puts("PASS: English Files, shelf, settings and reader at all 11 scales; shared sizes, separated ink and untranslated user content");
}
'''
    return code


def run(output):
    import calendar_ui_host_test as calendar
    native.run(output, unit())
    # 黄历另用其实际绘图和字框检查，覆盖两个版式及无时钟状态。
    # Reuse the calendar renderer and bounds checks for both faces and the unset-clock state.
    code = calendar.unit().split('int main(int argc,char **argv)')[0]
    code = code.replace('if(y>450&&y<1160', 's=ui_tr(s);if(y>450&&y<1160').replace('!strcmp(s,labels[i])', '!strcmp(s,ui_tr(labels[i]))')
    code += r'''
int main(int argc,char **argv){
    assert(argc==2 && ttf_font_open_builtin()==ESP_OK);bound_catalog();
    uint8_t *fb=malloc(684*1216/2);assert(fb);calendar_date_t date;
    for(int month=1;month<=12;++month){
        assert(calendar_date(2026,month,11,&date));
        for(int style=0;style<2;++style){render_face(fb,&date,style);
            if(month==11)screenshot(fb,argv[1],style?"calendar-en-month":"calendar-en-almanac");}
    }
    render_face(fb,NULL,false);render_face(fb,NULL,true);
    free(fb);ttf_font_unload();puts("PASS: English almanac and month faces, 12 months and unset clock");
}
'''
    native.run(output, with_english(code))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        run(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='english-ui-') as folder:
            run(Path(folder))
