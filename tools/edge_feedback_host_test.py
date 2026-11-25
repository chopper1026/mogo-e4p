"""验证真实边缘回执各帧、四向坐标与原图恢复。/ Verify edge cue frames, four rotations and exact restoration."""
from pathlib import Path
import subprocess
import tempfile
from reader_title_style_preview import function

ROOT = Path(__file__).resolve().parents[1]
EPD = ROOT / 'components/epdiy/src/epdiy.c'
unit = '''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "app_edge_feedback.h"
#include "display.h"
#include "display_refresh.h"
#include "ui_kit.h"
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT, EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
static enum EpdRotation display_rotation;
static enum EpdRotation epd_get_rotation(void){return display_rotation;}
static int epd_width(void){return 1216;}
static int epd_height(void){return 684;}
typedef struct {uint16_t x,y;} Coord_xy;
#define _swap_int(a,b) {int tmp=a;a=b;b=tmp;}
'''
unit += '\n'.join(function(n, EPD) for n in ('_rotate', 'epd_get_pixel', 'epd_draw_pixel',
                                            'epd_rotated_display_width', 'epd_rotated_display_height'))
unit += '\n' + function('ui_read_pixel', ROOT / 'main/ui/ui_kit.c') + '\n'
unit += r'''
static uint8_t frame[684*1216/2], original[sizeof(frame)];
static bool held, ready, right;
static int top, pushes, fail_at, guards, full;
static display_state_t state;
static bool check_shape;
enum { W=48, H=137, MID=68 };
static uint8_t left_frames[2][H][W];
static unsigned at(const uint8_t *fb,EpdRect a,int col,int row){
    return ui_read_pixel(fb, a.x+(right?W-1-col:col), a.y+row);
}
bool display_present_begin(void){assert(!held);return held=true;}
void display_present_end(bool lock){assert(held && lock);held=false;}
bool display_local_feedback_ready(void){assert(held);return ready;}
void display_request_full_refresh(void){assert(held && fail_at);++full;}
void guard_draw_result(display_state_t *h,enum EpdDrawError e){
    assert(h==&state && e!=EPD_DRAW_SUCCESS && !memcmp(frame,original,sizeof(frame)));++guards;
}
enum EpdDrawError display_refresh_area(display_state_t *h,display_refresh_intent_t intent,EpdRect a,unsigned flags){
    ++pushes; assert(held && h==&state && flags==DISPLAY_REFRESH_DEFAULT && pushes<=3);
    assert(a.width==W && a.height==H && a.y==top && a.x==(right?epd_rotated_display_width()-W:0));
    if(pushes<=2){
        assert(intent==DISPLAY_REFRESH_FEEDBACK);
        const int reach=pushes==1?29:42, tip=pushes==1?17:25, wing=pushes==1?7:9;
        // 折角无横杆，两条斜线朝屏内汇合，尖端与外弧之间留白。/ Shaftless arms converge inward with clear space before the rim.
        for(int x=0;x<tip-2;++x)assert(at(frame,a,x,MID)==15);
        for(int y=-wing;y<=wing;++y)assert(at(frame,a,tip-abs(y),MID+y)==0);
        assert(at(frame,a,tip+2,MID)==15 && at(frame,a,tip,MID-4)==15);
        for(int x=tip-2;x<=tip+1;++x)assert(at(frame,a,x,MID)==0);
        assert(at(frame,a,tip-wing,MID-wing-1)==0 && at(frame,a,tip-wing,MID-wing-2)==15);
        assert(at(frame,a,reach-4,MID)==15);
        for(int x=reach-3;x<reach;++x)assert(at(frame,a,x,MID)==0);
        assert(at(frame,a,reach,MID)==at(original,a,reach,MID));
        if(check_shape){
            int last=0;
            for(int row=0;row<H;++row){
                int width=0;
                for(int col=0;col<W;++col){
                    unsigned px=at(frame,a,col,row);
                    if(px!=8){assert(col==width);++width;}
                    assert(px==at(frame,a,col,H-1-row));
                    if(right)assert(left_frames[pushes-1][row][col]==px);
                    else left_frames[pushes-1][row][col]=px;
                }
                if(row<=MID){assert(width>=last && width<=last+2);last=width;}
                if(!row)assert(!width);
                if(row==MID/2)assert(width==(reach+1)/2);
                if(row==3*MID/4)assert(width*100 >= reach*89);
                if(row==MID)assert(width==reach);
            }
        }
    }else assert(intent==DISPLAY_REFRESH_DETAIL && !memcmp(frame,original,sizeof(frame)));
    return pushes==fail_at?EPD_DRAW_INVALID_CROP:EPD_DRAW_SUCCESS;
}
int main(void){
    app_ctx_t ctx={.fb=frame,.hl=&state};
    // 在中灰底上比较完整镜像，第二帧不得留下第一帧的墨线。/ Compare full mirrored silhouettes on gray; no first-frame ink may trail.
    memset(original,0x88,sizeof(original));memcpy(frame,original,sizeof(frame));ready=check_shape=true;top=200-MID;
    for(int side=0;side<2;++side){right=side;pushes=0;app_edge_feedback_show(&ctx,right,200);assert(pushes==3);}
    check_shape=false;
    for(unsigned i=0;i<sizeof(frame);++i)original[i]=(uint8_t)(i*37u);
    memcpy(frame,original,sizeof(frame));
    for(display_rotation=0;display_rotation<=3;++display_rotation) for(int side=0;side<2;++side){
        int h=epd_rotated_display_height(); right=side;ready=true;
        for(int pos=0;pos<3;++pos)for(fail_at=0;fail_at<=3;++fail_at){
            int y=(int[]){0,h/2,h-1}[pos]; top=(int[]){0,y-MID,h-H}[pos];pushes=full=guards=0;
            app_edge_feedback_show(&ctx,right,y);
            assert(!held && pushes==(fail_at?fail_at:3) && full==!!fail_at && guards==full);
            assert(!memcmp(frame,original,sizeof(frame)));
        }
    }
    ready=false;pushes=0;app_edge_feedback_show(&ctx,false,100);assert(!held && !pushes);
    puts("PASS: shallow contour, inward chevrons, mirrored motion, four rotations, bounds, 4bpp restore, all failure points");
}
'''
with tempfile.TemporaryDirectory(prefix='edge-feedback-') as tmp:
    source = Path(tmp) / 'test.c'
    source.write_text(unit)
    binary = source.with_suffix('')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-Itools/app_loop_stubs', '-Imain/app', '-Imain/display', '-Imain', str(source),
                    'main/app/app_edge_feedback.c', '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
