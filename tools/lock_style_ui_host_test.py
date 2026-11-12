#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：验证实际同页选择器、100–200%字号、失败处理和延迟图片结果。
English: Actual inline selector, 100–200% fonts, failures and deferred images."""
import argparse
from pathlib import Path
import re,json
import file_ui_host_test as native
import reader_title_style_preview as host
from ticket_background_ui_host_test import branch
ROOT=Path(__file__).resolve().parents[1]

def unit(assets=None):
    code=native.unit().split('typedef struct {char path[288];',1)[0]
    code+='\n#define HOST_ASSETS '+json.dumps(str(assets.resolve()) if assets else '')+'\n'
    code+='\n'+re.sub(r'^#(?:include|pragma)[^\n]*\n','',(ROOT/'main/services/lock_style_images.h').read_text(),flags=re.M)
    code+=r'''
static uint8_t selected_style=1;
static unsigned ticket_choice,calendar_choice,writes,requests,revision,cache_epoch,progress_revision,collage_revision;
static unsigned face_prepares,pauses,scans;
static unsigned state_calls,complete_on_state_call,complete_ready_mask;
static unsigned ready_mask=~0u;
static bool catalogue_changed,ticket_preview_failed;
static time_t fake_time=1794355200;
static time_t host_time(time_t *p){if(p)*p=fake_time;return fake_time;}
#define time host_time
static bool failed_save,available=true,images_ready=true,images_fail;
static bool complete_during_state;
static char wallpaper[288]="/sdcard/pictures/00.jpg",bookmark[288]="/sdcard/pictures/03.png";
static uint8_t fixture[342*608/2],small_fixture[2][3][160*284/2];
static bool fixture_read(const char *name,uint8_t *out,unsigned ow,unsigned oh){
 if(!HOST_ASSETS[0])return false;
 char path[2048];snprintf(path,sizeof(path),"%s/%s.gray",HOST_ASSETS,name);
 FILE *f=fopen(path,"rb");if(!f)return false;uint32_t wh[2];assert(fread(wh,4,2,f)==2);
 uint8_t *gray=malloc((size_t)wh[0]*wh[1]);assert(gray&&fread(gray,1,(size_t)wh[0]*wh[1],f)==(size_t)wh[0]*wh[1]);fclose(f);
 for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow;x+=2){
  unsigned sy=y*wh[1]/oh,a=gray[sy*wh[0]+x*wh[0]/ow]/17,b=gray[sy*wh[0]+(x+1)*wh[0]/ow]/17;
  out[(y*ow+x)/2]=a|(b<<4);
 }
 free(gray);return true;
}
static void fixture_frame(uint8_t *fb,const char *name,int gray){
 uint8_t *p=malloc(684u*1216u/2);assert(p);
 if(!fixture_read(name,p,684,1216)){memset(fb,gray,684u*1216u/2);free(p);return;}
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x){unsigned at=y*684+x;epd_draw_pixel(x,y,((p[at/2]>>((at&1)*4))&15)*17,fb);}free(p);
}
static int app_settings_ticket_background(void){return ticket_choice;}
static int app_settings_calendar_style(void){return calendar_choice;}
static uint8_t app_settings_lock_style(void){return selected_style;}
static unsigned wallpaper_mode;
typedef int app_wallpaper_mode_t;
static const char *app_settings_wallpaper_path(void){return wallpaper;}
static int app_settings_wallpaper_mode(void){return (int)wallpaper_mode;}
static bool app_settings_set_wallpaper_mode(int mode){if(mode>1||failed_save)return false;wallpaper_mode=(unsigned)mode;return true;}
static const char *app_settings_bookmark_path(void){return bookmark;}
static bool app_settings_select_lock(uint8_t style,const char *path,unsigned option){
 ++writes;if(failed_save)return false;selected_style=style;
 if(style==0)ticket_choice=option;if(style==4)calendar_choice=option;
 if(style==1){snprintf(wallpaper,sizeof(wallpaper),"%s",path);wallpaper_mode=option;}
 if(style==3)snprintf(bookmark,sizeof(bookmark),"%s",path);
 return true;
}
#define APP_LOCK_STYLE_CLOCK 5
static unsigned book_progress_revision(void){return progress_revision;}
static unsigned book_lock_collage_preview_revision(void){return collage_revision;}
static unsigned app_settings_system_contrast(void){return 100;}
static void lock_images_pause(void){++pauses;}
static void lock_images_close(void){++cache_epoch;}
static unsigned lock_images_epoch(void){return cache_epoch;}
static void lock_images_open(void){available=true;++revision;}
static unsigned lock_images_revision(void){return revision;}
static void lock_images_request(const lock_image_request_t *r,int n){assert(n<=8);for(int i=0;i<n;++i)assert(r[i].path);++requests;}
static lock_image_item_t *lock_images_catalogue(int *count,bool *finished){
 *finished=available;*count=7;if(!available)return NULL;available=false;++scans;
 lock_image_item_t *p=calloc(7,sizeof(*p));assert(p);
 for(int i=0;i<7;++i){snprintf(p[i].path,sizeof(p[i].path),"/sdcard/pictures/%02d.%s",i,i>=3?"png":"jpg");snprintf(p[i].name,sizeof(p[i].name),"海岸风景与阅读书封%d.%s",i,i>=3?"png":"jpg");p[i].key=i+(catalogue_changed?100:0);}return p;
}
static const uint8_t *lock_images_pixels(const char *p,bool b,bool large,lock_image_state_t *state){
 const char *last=strrchr(p,'/');int index=last?atoi(last+1):0;
 *state=images_fail?LOCK_IMAGE_FAILED:images_ready&&(ready_mask&(1u<<index))?LOCK_IMAGE_READY:LOCK_IMAGE_PENDING;
 if(++state_calls==complete_on_state_call){ready_mask|=complete_ready_mask;++revision;}
 if(complete_during_state){complete_during_state=false;images_ready=true;++revision;}
 if(*state!=LOCK_IMAGE_READY)return NULL;
 int n=index%3;
 return large?fixture:small_fixture[b][n];
}
static bool book_ticket_preview(uint8_t *fb,bool cover){++face_prepares;if(ticket_preview_failed)return false;fixture_frame(fb,cover?"ticket-cover":"ticket-transparent",cover?0xaa:0xff);return true;}
static bool book_lock_collage_preview(uint8_t *fb){++face_prepares;fixture_frame(fb,"collage-native",0xbb);return true;}
typedef struct {bool time_synced;unsigned unix_sec;}pmu_snapshot_t;
static const pmu_snapshot_t *device_power_get(void){static pmu_snapshot_t p={true,1794355200};return &p;}
typedef struct {int year,month,day;}calendar_date_t;
static bool calendar_from_utc(int64_t ms,calendar_date_t *d){(void)ms;*d=(calendar_date_t){2026,11,11};return true;}
static void ui_calendar_draw(uint8_t *fb,const calendar_date_t *d,bool month){(void)d;++face_prepares;fixture_frame(fb,month?"monthly":"almanac",month?0xcc:0xdd);}
typedef struct {bool valid;int hour,minute,month,day,weekday;}ui_lock_clock_face_t;
static void ui_lock_clock_draw(uint8_t *fb,const ui_lock_clock_face_t *f){(void)f;++face_prepares;fixture_frame(fb,"clock-lockscreen-host",0xee);}
'''
    # 上方原型由测试替身提供，仅移除服务声明并保留类型。 / Prototypes above are test doubles; strip the service declarations, not its types.
    code=re.sub(r'^(?:void lock_images_|bool lock_images_|unsigned lock_images_|lock_image_item_t \*lock_images_|const uint8_t \*lock_images_)[^\n]*;\n','',code,flags=re.M)
    src=re.sub(r'^#include[^\n]*\n','',(ROOT/'main/ui/ui_lock_style.c').read_text(),flags=re.M)
    # 原生绘图前用实际字形度量检查边界。 / Bounds checks use actual font metrics before native drawing.
    src=src.replace('    ui_text_fixed_context_aa_vc(fb,center?', '''    int above=0,below=0;ui_font_measure_line_px(px,fit,&above,&below);
    assert(ui_text_fixed_width_px(px,fit)<=r.width);assert(above+below<=r.height);
    ui_text_fixed_context_aa_vc(fb,center?''')
    code+=src
    code+=r'''
enum {SETTINGS_MAIN,SETTINGS_LOCK_STYLE};
static int s_page,nav=-1,profile_refresh;
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;nav=tab;}
static void settings_prepare_profile(void){++profile_refresh;}
'''
    settings=ROOT/'main/apps/app_device_settings.c'
    for name,args,source_name,condition in (
        ('route_gesture','app_ctx_t *ctx,const ui_gesture_event_t *ev','on_gesture','if (s_page == SETTINGS_LOCK_STYLE)'),
        ('route_key','app_ctx_t *ctx,int key','on_key','if (s_page==SETTINGS_LOCK_STYLE)'),
        ('route_tick','app_ctx_t *ctx','on_tick','if (s_page == SETTINGS_LOCK_STYLE'),
        ('route_before_lock','void','on_before_lock','if(s_page==SETTINGS_LOCK_STYLE)')):
        code+='\nstatic app_redraw_t '+name+'('+args+'){\n'+branch(host.function(source_name,settings),condition)+'\nreturn APP_REDRAW_NONE;\n}\n'
    code+=r'''
static bool tap(int x,int y){ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=x,.y0=y};return ui_lock_style_gesture(&e);}
static bool hold(int x,int y){ui_gesture_event_t e={.type=UI_GESTURE_LONG_PRESS,.x0=x,.y0=y};return ui_lock_style_gesture(&e);}
static unsigned pump(void){unsigned paints=0;for(int i=0;i<12;++i)paints+=ui_lock_style_tick(200*(i+1));return paints;}
int main(int argc,char **argv){
 assert(argc==2&&ttf_font_open_builtin()==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);memset(fb,255,684*1216/2);
 for(unsigned i=0;i<sizeof(fixture);++i)fixture[i]=(uint8_t)(i/160);
 for(int b=0;b<2;++b)for(int i=0;i<3;++i){char name[64];snprintf(name,sizeof(name),b?"bookmark-%d":"wall-%d",i);if(!fixture_read(name,small_fixture[b][i],160,284))memset(small_fixture[b][i],(i+4)*17,160*284/2);}
 fixture_read("wall-0",fixture,342,608);
 for(font_percent=100;font_percent<=200;font_percent+=10){
  ui_lock_style_open(fb);assert(pump()==1);
  assert(s_count==7&&s_catalogue_ready);
  for(int tab=0;tab<6;++tab){
   EpdRect r=card_rect(tab);assert(tap(r.x,r.y)&&s_tab==tab);
   if(tab==0||tab==3)assert(tap(36,758));
   if(tab==1||tab==4)assert(tap(36,762));
   ui_lock_style_render(fb);char file[1024];snprintf(file,sizeof(file),"%s/inline-%d-%u.raw",argv[1],tab,font_percent);save(file,fb);
  }
  assert(tap(246,440)&&s_tab==4);unsigned before=writes;
  assert(!tap(35,762)&&!tap(648,762)&&writes==before);
  assert(tap(352,762)&&selected_style==4&&calendar_choice==1);
  failed_save=true;assert(tap(36,762)&&calendar_choice==1&&strstr(s_notice,"保存失败"));failed_save=false;
  assert(tap(246,202)&&s_tab==1);assert(tap(352,762)&&selected_style==0&&ticket_choice==1);
  assert(tap(36,202)&&s_tab==0);assert(tap(528,110)&&wallpaper_mode==1);
  failed_save=true;assert(tap(398,110)&&wallpaper_mode==1&&strstr(s_notice,"保存失败"));failed_save=false;
  assert(tap(398,110)&&wallpaper_mode==0);assert(tap(36,758)&&selected_style==1);
  before=writes;assert(hold(36,758)&&s_preview==0&&writes==before);ui_lock_style_render(fb);
  assert(ui_lock_style_back()&&page()==0&&writes==before);
  assert(tap(584,683)&&page()==1);assert(tap(452,683)&&page()==0);assert(!tap(452,683));
  images_ready=false;assert(tap(246,758)&&s_pending==1&&writes==before);
  assert(tap(584,683)&&s_pending==-1);images_ready=true;ui_lock_style_tick(3000);assert(writes==before);
  // 已就绪的选择替代待加载选择，旧结果不能覆盖新选择。 / A ready tap supersedes a pending pick; the old result cannot overwrite the later choice.
  images_ready=false;assert(tap(36,758)&&s_pending==3);images_ready=true;
  assert(tap(246,758)&&s_pending==-1&&strstr(wallpaper,"04.png"));before=writes;ui_lock_style_tick(3500);assert(writes==before);
  images_fail=true;assert(tap(36,758)&&strstr(s_notice,"无法加载")&&writes==before);images_fail=false;
  images_ready=false;assert(tap(36,758)&&s_pending==3);images_ready=true;ui_lock_style_tick(4000);assert(selected_style==1&&strstr(wallpaper,"03.png"));
  assert(tap(36,440)&&s_tab==3);assert(image_count()==7);assert(tap(36,758)&&selected_style==3);
  for(int i=0;i<8;++i)ui_lock_style_tick(5000+i*200);
  ++revision;assert(!ui_lock_style_tick(7000)); // 不可见预读完成不得刷新。 / Completed invisible prefetch must not refresh.
  assert(tap(36,202));images_ready=false;complete_during_state=true;
  request_images();assert(ui_lock_style_tick(7300)); // 状态快照与最后完成竞争时仍能观察到结果。 / Final worker completion racing the snapshot stays observable.
  ui_lock_style_media_lost();assert(!s_items&&!s_count&&s_preview==-1&&s_pending==-1);ui_lock_style_render(fb);
  ui_lock_style_media_ready();ui_lock_style_tick(6000);assert(s_count==7);ui_lock_style_close();
  strcpy(wallpaper,"/sdcard/pictures/00.jpg");selected_style=1;
 }
 // 真实设置分发覆盖长按、返回、底部导航、按键和锁前清理。 / Actual settings dispatch keeps holds, Back, bottom navigation, keys and lock cleanup connected.
 app_ctx_t ctx={.fb=fb};s_page=SETTINGS_LOCK_STYLE;ui_lock_style_open(fb);ui_lock_style_tick(8000);
 ui_gesture_event_t ev={.type=UI_GESTURE_LONG_PRESS,.x0=36,.y0=758};
 assert(route_gesture(&ctx,&ev)==APP_REDRAW_PAGE&&s_preview==0);
 ev=(ui_gesture_event_t){.type=UI_GESTURE_TAP,.x0=57,.y0=115};
 assert(route_gesture(&ctx,&ev)==APP_REDRAW_PAGE&&s_preview==-1&&s_open&&s_page==SETTINGS_LOCK_STYLE);
 assert(route_gesture(&ctx,&ev)==APP_REDRAW_PAGE&&!s_open&&s_page==SETTINGS_MAIN&&profile_refresh);
 s_page=SETTINGS_LOCK_STYLE;ui_lock_style_open(fb);ev.x0=50;ev.y0=1170;
 assert(route_gesture(&ctx,&ev)==APP_REDRAW_PAGE&&!s_open&&nav==0);
 s_page=SETTINGS_LOCK_STYLE;ui_lock_style_open(fb);ui_lock_style_tick(8500);assert(hold(36,758));
 assert(route_key(&ctx,0)==APP_REDRAW_PAGE&&s_open&&s_preview==-1);
 assert(route_key(&ctx,2)==APP_REDRAW_PAGE&&!s_open&&s_page==SETTINGS_MAIN);
 s_page=SETTINGS_LOCK_STYLE;ui_lock_style_open(fb);nav=-1;
 assert(route_key(&ctx,1)==APP_REDRAW_NONE&&!s_open&&nav==0);
 s_page=SETTINGS_LOCK_STYLE;ui_lock_style_open(fb);ui_lock_style_tick(9000);images_ready=false;assert(tap(36,758)&&s_pending>=0);
 route_before_lock();assert(!s_open&&s_pending==-1&&s_page==SETTINGS_MAIN&&!ui_lock_style_tick(9500));images_ready=true;
 // 可见冷图分批完成时，也只做一次最终刷新。 / A cold visible batch settles exactly once, even when files finish in separate ticks.
 ui_lock_style_media_lost();selected_style=1;strcpy(wallpaper,"/sdcard/pictures/00.jpg");strcpy(bookmark,"/sdcard/pictures/03.png");
 ready_mask=0;ui_lock_style_open(fb);assert(pump()==0&&face_prepares);
 for(int i=0;i<3;++i){ready_mask|=1u<<i;++revision;assert(!ui_lock_style_tick(10000+i*200));}
 s_page=SETTINGS_LOCK_STYLE;complete_on_state_call=state_calls+2;complete_ready_mask=1u<<3;++revision;
 assert(route_tick(&ctx)==APP_REDRAW_NONE); // 最后一张在本轮状态采样中完成。 / Last image completes during the current status snapshot.
 assert(route_tick(&ctx)==APP_REDRAW_FULL);assert(!pump());
 ready_mask=~0u;++revision;assert(!pump()); // 不可见的下一页完成不驱动显示。 / Invisible next-page completion does not drive the display.
 unsigned before_faces=face_prepares,before_scans=scans;uint8_t *retained=s_faces[2];
 ui_lock_style_close();assert(s_faces[2]==retained&&s_items&&s_count==7);
 ui_lock_style_open(fb);assert(s_faces[2]==retained&&s_count==7);ui_lock_style_render(fb);
 assert(!pump()&&face_prepares==before_faces&&scans==before_scans+1); // 重进仅核对元数据。 / Reentry only validates metadata.
 ui_lock_style_close();fake_time+=60;ui_lock_style_open(fb);assert(pump()==1&&face_prepares==before_faces+1); // 跨分钟仅更新时钟预览。 / Minute changes refresh only the clock.
 before_faces=face_prepares;ui_lock_style_close();++progress_revision;ui_lock_style_open(fb);assert(pump()==1&&face_prepares==before_faces+2);
 before_faces=face_prepares;ui_lock_style_close();++collage_revision;ui_lock_style_open(fb);assert(pump()==1&&face_prepares==before_faces+1);
 before_faces=face_prepares;ui_lock_style_close();fake_time+=86400;ui_lock_style_open(fb);assert(pump()==1&&face_prepares==before_faces+5);
 ui_lock_style_close();++progress_revision;ticket_preview_failed=true;ui_lock_style_open(fb);assert(pump()==1&&!s_faces[0]&&!s_faces[1]);
 ui_lock_style_close();ticket_preview_failed=false;ui_lock_style_open(fb);assert(pump()==1&&s_faces[0]&&s_faces[1]);
 ui_lock_style_close();++cache_epoch;ui_lock_style_open(fb);assert(!s_items&&!s_faces[2]);assert(pump()==1);
 // 延迟扫描只报告一次目录变化，失败图片也可结束批次等待。 / A late scan discovers a changed catalogue once; failed image slots still settle the batch.
 ui_lock_style_close();catalogue_changed=true;images_fail=true;ui_lock_style_open(fb);assert(pump()==1);images_fail=false;
 ui_lock_style_close();ui_lock_style_media_lost();assert(!s_items&&!s_faces[2]);
 free(fb);ttf_font_unload();puts("PASS: inline styles, paging/hold, cancellation, failures/media, 100–200% font bounds, cached reentry, dependency invalidation and one full settle per visible batch");
}
'''
    return code

def main():
    p=argparse.ArgumentParser();p.add_argument('--assets',type=Path);p.add_argument('--output',type=Path,default=ROOT/'build/lock-style-ui');args=p.parse_args()
    native.run(args.output,unit(args.assets))
if __name__=='__main__':main()
