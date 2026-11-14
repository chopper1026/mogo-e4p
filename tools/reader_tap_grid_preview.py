#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：原生4bpp九宫格/动作选择器与三种模式，检查真实字体边界。/ Render native grid controls and all modes with real glyph/bounds checks."""
import argparse
import re
from pathlib import Path
import reader_toolbar_preview as renderer

ROOT = Path(__file__).resolve().parents[1]
original_unit = renderer.unit


def unit():
    code = original_unit().replace('static unsigned app_settings_reader_tap_mode(void){return app_settings_reader_vertical_turn()?1:0;}', 'static unsigned preview_mode;\nstatic unsigned app_settings_reader_tap_mode(void){return preview_mode;}')
    module = (ROOT / 'main/ui/ui_reader_tap_grid.c').read_text()
    module = '\n'.join(line for line in module.splitlines() if not line.startswith('#include "'))
    code = code.replace('int main(int argc,char **argv){', '#include "book/book_reader_input.h"\n#include "ui_reader_tap_grid.h"\n' + module + '\nint main(int argc,char **argv){')
    marker = re.search(r'\s+printf\("PASS: [^"]*actual reader renders', code)
    assert marker, 'reader toolbar preview pass marker'
    at = marker.start()
    code = code[:at] + r'''
 for(font_scale=100;font_scale<=200;font_scale+=50){
  for(unsigned mode=0;mode<3;++mode){
   background(fb);ui_text_set_system_scale(true);draw_sheet(fb,270,"翻页模式");draw_sheet_back(fb,270);
   ui_reader_turn_settings_draw(fb,mode,true);
   char path[1024];snprintf(path,sizeof(path),"%s/turn-mode-%u-%u.raw",argv[1],mode,font_scale);save(path,fb);
  }
  static const char *const names[]={"上一页","下一页","工具栏","返回主页","手动全刷","前光开关","不操作"};
  static const char *const labels[]={"上一页","下一页","下一页","上一页","工具栏","下一页","前光开关","手动全刷","不操作"};
  preview_mode=2;background(fb);s_reader_panel=READER_PANEL_REFRESH_SETTINGS;s_reader_settings_group=UI_READER_SETTINGS_INPUT;draw_reader_panel(fb);
  char settings_path[1024];snprintf(settings_path,sizeof(settings_path),"%s/settings-grid-%u.raw",argv[1],font_scale);save(settings_path,fb);
  EpdRect cells[9];for(unsigned i=0;i<9;++i)cells[i]=book_reader_grid_cell((EpdRect){36,96,612,1104},i);
  memset(fb,0,684*1216/2);ui_reader_tap_grid_draw(fb,cells,labels,4,"");assert(ui_read_pixel(fb,0,200)==12);
  memset(fb,255,684*1216/2);ui_reader_tap_grid_draw(fb,cells,labels,4,"");assert(ui_read_pixel(fb,0,200)==15);
  for(int picker=0;picker<2;++picker){
   background(fb);ui_text_set_system_scale(true);
   ui_reader_tap_grid_draw(fb,cells,labels,4,"");
   if(picker)ui_reader_tap_picker_draw(fb,4,names,2);
   char path[1024];snprintf(path,sizeof(path),"%s/tap-%s-%u.raw",argv[1],picker?"actions":"grid",font_scale);save(path,fb);
   ui_text_set_system_scale(false);
  }
 }
''' + code[at:]
    return code.replace('PASS: 18 actual reader renders', 'PASS: native reader/grid renders')


def run(output):
    renderer.unit = unit
    renderer.run(output)
    from PIL import Image, ImageDraw, ImageFont
    frames = [('turn-mode-2-100', '翻页模式'), ('tap-grid-100', '自定义九宫格'), ('tap-actions-100', '区域功能')]
    canvas = Image.new('RGB', (3*342+4*24, 716), '#ededeb')
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial Unicode.ttf', 20) if Path('/System/Library/Fonts/Supplemental/Arial Unicode.ttf').exists() else None
    for i, (name, title) in enumerate(frames):
        x = 24+i*366
        draw.text((x, 22), title, font=font, fill='#222222')
        canvas.paste(Image.open(output/(name+'.png')).resize((342,608)), (x,68))
    canvas.save(output/'tap-grid-overview.png')
    print('PASS: 36 native renders, actual UI fonts at 100/150/200%, all seven action labels, transparent grid and three tap modes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/reader-tap-grid/native')
    run(parser.parse_args().output.resolve())
