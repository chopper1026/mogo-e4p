#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：阅读提示的原生字库、层级、白底和清除边界检查，可导出 PNG。
English: Native reader-notice fonts, layering, white paper and erase bounds, with PNG export.
"""
import argparse
from pathlib import Path
import re
import tempfile

import file_ui_host_test as native
import reader_title_style_preview as host

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / "main/apps/app_book.c"
KIT = ROOT / "main/ui/ui_kit.c"


def unit():
    base = native.unit().split("typedef struct {char path[288];", 1)[0]
    base += host.function("ui_text_set_system_scale", KIT) + "\n"
    view = (ROOT / "main/ui/ui_reader_notice.c").read_text()
    base += re.sub(r'^#include[^\n]*\n', '', view, flags=re.M)
    # 文案来自真实动作分支，避免预览与产品各维护一份。/ Pull copy from the action so previews cannot drift.
    messages = re.findall(r'"(?:\\.|[^"\\])*"', host.function("reader_toggle_touch", BOOK))
    assert len(messages) == 4
    base += "\nstatic const char *messages[]={" + ",".join(messages) + ',"书签已添加"};\n'
    base += r'''
static void bounds(uint8_t *fb,EpdRect area){
    unsigned ink=0;
    for(int y=0;y<1216;++y)for(int x=0;x<684;++x){
        uint8_t pixel=pixel_at(fb,x,y);
        if(x<area.x||x>=area.x+area.width||y<area.y||y>=area.y+area.height)assert(pixel==0x70);
        else if(pixel==0)++ink;
    }
    assert(ink>500);
    // 卡内留白必须为纯白，无灰色底；字形和图标保留抗锯齿。/ Paper is white; glyph/icon antialiasing remains.
    assert(pixel_at(fb,area.x+24,area.y+area.height/2)==0xf0);
}
int main(int argc,char **argv){
    assert(argc==2&&ttf_font_open_builtin()==ESP_OK);
    uint8_t *fb=malloc(684*1216/2);assert(fb);char path[1024];
    assert(!ui_reader_notice_area("",120).width&&!ui_reader_notice_area(NULL,120).height);
    memset(fb,0x77,684*1216/2);ui_reader_notice_draw(fb,"",120);
    for(size_t i=0;i<684*1216/2;++i)assert(fb[i]==0x77);
    for(font_percent=100;font_percent<=200;font_percent+=10)for(unsigned i=0;i<5;++i){
        memset(fb,0x77,684*1216/2);
        EpdRect area=ui_reader_notice_area(messages[i],font_percent);
        allowed_top=area.y;allowed_bottom=area.y+area.height;
        ui_reader_notice_draw(fb,messages[i],font_percent);bounds(fb,area);
        assert(area.y+area.height<600);
        // 同一清除区域覆盖全部提示像素。/ The returned erase area covers every notice pixel.
        ui_fill_rect_fast(area,0x70,fb);
        for(size_t j=0;j<684*1216/2;++j)assert(fb[j]==0x77);
        memset(fb,255,684*1216/2);ui_reader_notice_draw(fb,messages[i],font_percent);
        snprintf(path,sizeof(path),"%s/notice-%u-%u.raw",argv[1],i,font_percent);save(path,fb);
    }
    free(fb);ttf_font_unload();puts("PASS: native notice copy/glyphs, 100..200% bounds, pure white paper and complete erase area");
}
'''
    outer = host.function("draw_reader_outer", BOOK)
    assert outer.index("ui_reader_notice_draw") > outer.index("ui_reader_dictionary_draw") > outer.index("draw_reader_panel")
    assert "reader_notice_tick(ctx)" in host.function("on_tick", BOOK)
    return base


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        native.run(args.output.resolve(), unit())
    else:
        with tempfile.TemporaryDirectory(prefix="reader-notice-") as directory:
            native.run(Path(directory), unit())
