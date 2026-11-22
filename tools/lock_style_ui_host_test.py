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
    code+='\n#include "book/book_lock_cover.h"\n'
    settings_header=(ROOT/'main/settings/settings.h').read_text()
    code+='\n'+settings_header[settings_header.index('typedef enum {\n    APP_OFF_SYSTEM'):settings_header.index('/// 与浅睡锁屏独立')]
    code+=r'''
static app_shutdown_wallpaper_t off_value={.show_hint=1};
static bool off_job,off_ready,off_result_ok=true,off_commit_failure;
static unsigned off_requests,off_cancels;
static const app_shutdown_wallpaper_t *app_settings_shutdown_wallpaper(void){return &off_value;}
static bool app_settings_set_shutdown_wallpaper(const app_shutdown_wallpaper_t *v){if(off_commit_failure)return false;off_value=*v;return true;}
static bool mm_free(void *p){free(p);return true;}
static void shutdown_wallpaper_cancel(void){++off_cancels;off_job=off_ready=false;}
static app_shutdown_wallpaper_t off_candidate;
static void shutdown_wallpaper_request(const app_shutdown_wallpaper_t *v,bool rebuild){off_candidate=*v;off_candidate.image_id=42;off_job=true;off_ready=false;++off_requests;(void)rebuild;}
static bool shutdown_wallpaper_take(app_shutdown_wallpaper_t *v,bool *ok,uint8_t **thumb){if(!off_job||!off_ready)return false;off_job=false;*v=off_candidate;*ok=off_result_ok;*thumb=*ok?malloc(160*284/2):NULL;if(*thumb)memset(*thumb,0x99,160*284/2);return true;}
static const char *shutdown_wallpaper_error(void){return "TF卡未就绪";}
#define APP_BOOKMARK_MODE_CROP 0
static unsigned bookmark_prepares;static bool bookmark_prepare_crop;
static void book_lock_bookmark_prepare_mode(const char *path,bool crop_to_fill){
 (void)path;++bookmark_prepares;bookmark_prepare_crop=crop_to_fill;
}
static uint8_t selected_style=1;
enum {APP_LOCK_STYLE_BOOK_COVER=6};
static unsigned ticket_choice,calendar_choice,writes,requests,revision,cache_epoch,progress_revision,collage_revision;
static unsigned face_prepares,pauses,scans,cover_prepares;
static unsigned cover_labels,cover_main,cover_contain,cover_fill,unrelated_labels;
static bool cover_missing;
bool book_lock_cover_preview(uint8_t *pixels,bool crop){
 ++cover_prepares;if(cover_missing)return false;
 if(crop)memset(pixels,0x88,BOOK_LOCK_PREVIEW_BYTES);
 else{memset(pixels,255,BOOK_LOCK_PREVIEW_BYTES);
 for(int y=30;y<240;++y)memset(pixels+y*80+9,0x66,62);}
 return true;
}
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
static bool lock_cover_crop;
static bool app_settings_lock_cover_crop(void){return lock_cover_crop;}
typedef int app_wallpaper_mode_t;
static const char *app_settings_wallpaper_path(void){return wallpaper;}
static int app_settings_wallpaper_mode(void){return (int)wallpaper_mode;}
static bool app_settings_set_wallpaper_mode(int mode){if(mode>1||failed_save)return false;wallpaper_mode=(unsigned)mode;return true;}
static const char *app_settings_bookmark_path(void){return bookmark;}
typedef int app_bookmark_mode_t;
static unsigned bookmark_mode;
static int app_settings_bookmark_mode(void){return (int)bookmark_mode;}
static bool app_settings_set_bookmark_mode(int mode){if(mode>1||failed_save)return false;bookmark_mode=(unsigned)mode;return true;}
static bool app_settings_select_lock(uint8_t style,const char *path,unsigned option){
 ++writes;if(failed_save)return false;selected_style=style;
 if(style==0)ticket_choice=option;if(style==4)calendar_choice=option;
 if(style==APP_LOCK_STYLE_BOOK_COVER)lock_cover_crop=option!=0;
 if(style==1){snprintf(wallpaper,sizeof(wallpaper),"%s",path);wallpaper_mode=option;}
 if(style==3){snprintf(bookmark,sizeof(bookmark),"%s",path);bookmark_mode=option;}
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
    src=src.replace('    char fit[288];', '''    if(r.y>=680){
        if(!strcmp(text,"书籍封面"))++cover_labels;
        if(!strcmp(text,"最近阅读的封面"))++cover_main;
        if(!strcmp(text,"保留封面全图，不裁剪"))++cover_contain;
        if(!strcmp(text,"居中裁剪，铺满屏幕"))++cover_fill;
        if(!strcmp(text,"你的书架，自动拼贴")||!strcmp(text,"系统默认")||!strcmp(text,"显示关机状态"))++unrelated_labels;
    }
    char fit[288];''')
    code+=src
    code+=host.function("draw_shutdown_hint", ROOT/"main/ui/ui_power_dialog.c")+"\n"
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
static void check_cover_panel(uint8_t *fb,bool shutdown,bool has_cover){
 bool crop=shutdown?off_value.cover_crop:lock_cover_crop;
 unsigned tone=crop?0x80:0x60;
 cover_labels=cover_main=cover_contain=cover_fill=unrelated_labels=0;
 ui_lock_style_render(fb);
 assert(cover_labels==1&&cover_main==1&&cover_contain==!crop&&cover_fill==crop&&!unrelated_labels);
 EpdRect card=shutdown?off_style_rect(4):card_rect(TAB_BOOK_COVER);
 assert((pixel_at(fb,card.x+card.width/2,card.y+34)==tone)==has_cover);
 assert((pixel_at(fb,90,810)==tone)==has_cover);
}
static void check_shutdown_hint(uint8_t *fb){
 const char *hint="已关机 · 长按电源键开机";
 int above=0,below=0;assert(ui_font_has_text(hint));
 ui_font_measure_line_px(36,hint,&above,&below);
 assert(above+below>0&&above+below<=36);
 const int width=ui_text_fixed_width_px(36,hint)+16, x0=(684-width)/2;
 memset(fb,0x55,684*1216/2);draw_shutdown_hint(fb);
 unsigned ink=0;
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x){
  unsigned tone=pixel_at(fb,x,y);
  if(y<1180||x<x0||x>=x0+width)assert(tone==0x50);
  else if(tone<0xf0)++ink;
 }
 assert(ink>100);
}
int main(int argc,char **argv){
 assert(argc==2&&ttf_font_open_builtin()==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);check_shutdown_hint(fb);memset(fb,255,684*1216/2);
 for(unsigned i=0;i<sizeof(fixture);++i)fixture[i]=(uint8_t)(i/160);
 for(int b=0;b<2;++b)for(int i=0;i<3;++i){char name[64];snprintf(name,sizeof(name),b?"bookmark-%d":"wall-%d",i);if(!fixture_read(name,small_fixture[b][i],160,284))memset(small_fixture[b][i],(i+4)*17,160*284/2);}
 fixture_read("wall-0",fixture,342,608);
 for(font_percent=100;font_percent<=200;font_percent+=10){
  ui_lock_style_open(fb);assert(pump()==1);
  assert(s_count==7&&s_catalogue_ready);
  for(int tab=0;tab<STANDBY_STYLE_COUNT;++tab){
   EpdRect r=card_rect(tab);assert(tap(r.x,r.y)&&s_tab==tab);
   if(tab==0||tab==3)assert(tap(36,758));
   if(tab==1||tab==4)assert(tap(36,762));
   if(tab==TAB_BOOK_COVER){
    assert(selected_style==APP_LOCK_STYLE_BOOK_COVER && !tap(352,762) && !tap(36,762));
    assert(tap(card_rect(5).x,card_rect(5).y) && selected_style==APP_LOCK_STYLE_CLOCK);
    failed_save=true;assert(tap(r.x,r.y)&&selected_style==APP_LOCK_STYLE_CLOCK);failed_save=false;
    assert(tap(r.x,r.y)&&selected_style==APP_LOCK_STYLE_BOOK_COVER);
    check_cover_panel(fb,false,true);
    const unsigned before_mode=wallpaper_mode;
    assert(tap(408,110)&&lock_cover_crop&&wallpaper_mode==before_mode);
    pump();check_cover_panel(fb,false,true);
    assert(tap(card_rect(5).x,card_rect(5).y)&&tap(r.x,r.y)&&lock_cover_crop);
    assert(tap(538,110)&&!lock_cover_crop);
   }
   ui_lock_style_render(fb);char file[1024];snprintf(file,sizeof(file),"%s/inline-%d-%u.raw",argv[1],tab,font_percent);save(file,fb);
  }
  off_value.fit=0;unsigned lock_before=selected_style,writes_before=writes;
  assert(tap(352,148)&&s_off_tab&&selected_style==lock_before&&writes==writes_before);
  assert(tap(36,258)&&s_off_browse==APP_OFF_CUSTOM);
  assert(tap(36,758)&&off_job&&selected_style==lock_before);
  off_ready=true;ui_lock_style_tick(12000);pump();
  assert(off_value.style==APP_OFF_CUSTOM&&off_value.image_id==42&&!s_off_loading);
  ui_lock_style_render(fb);char off_file[1024];snprintf(off_file,sizeof(off_file),"%s/shutdown-custom-%u.raw",argv[1],font_percent);save(off_file,fb);
  unsigned ticket_before=ticket_choice,calendar_before=calendar_choice;
  for(int mode=1;mode<OFF_STYLE_COUNT;++mode){
   EpdRect r=off_style_rect(mode);assert(tap(r.x,r.y)&&s_off_browse==off_values[mode]);
   if(mode==1||mode==3){
    assert(off_value.style!=off_values[mode]);
    assert(tap(352,762)&&off_value.style==off_values[mode]);
    assert((mode==1?off_value.ticket_background:off_value.calendar_style)==1);
    off_commit_failure=true;assert(tap(36,762));off_commit_failure=false;
    assert((mode==1?off_value.ticket_background:off_value.calendar_style)==1);
   }else{
    assert(off_value.style==off_values[mode]);
    if(off_value.style==APP_OFF_BOOK_COVER){
     assert(!tap(36,762)&&!tap(352,762));check_cover_panel(fb,true,true);
     off_commit_failure=true;assert(tap(408,110)&&!off_value.cover_crop);off_commit_failure=false;
     assert(tap(408,110)&&off_value.cover_crop&&!off_value.fit&&!lock_cover_crop&&!off_job);
     pump();check_cover_panel(fb,true,true);
     assert(tap(538,110)&&!off_value.cover_crop);
    }
   }
   assert(ticket_choice==ticket_before&&calendar_choice==calendar_before&&selected_style==lock_before);
   ui_lock_style_render(fb);snprintf(off_file,sizeof(off_file),"%s/shutdown-%d-%u.raw",argv[1],mode,font_percent);save(off_file,fb);
  }
  assert(tap(36,258)&&off_value.style==APP_OFF_SYSTEM);
  unsigned hint=off_value.show_hint;assert(tap(524,623)&&off_value.show_hint!=hint);
  assert(tap(528,110)&&off_job);off_commit_failure=true;off_ready=true;ui_lock_style_tick(12500);
  assert(off_value.fit==0&&strstr(s_notice,"设置保存失败")&&strstr(s_notice,"保留原选择"));off_commit_failure=false;
  assert(tap(36,758)&&off_job);off_result_ok=false;off_ready=true;ui_lock_style_tick(12600);
  assert(off_value.style==APP_OFF_SYSTEM&&strstr(s_notice,"TF卡未就绪"));off_result_ok=true;
  assert(tap(36,758)&&off_job);assert(tap(36,148)&&!s_off_tab&&!off_job);
  assert(selected_style==lock_before&&writes==writes_before);
  assert(tap(card_rect(4).x,card_rect(4).y)&&s_tab==4);unsigned before=writes;
  assert(!tap(35,762)&&!tap(648,762)&&writes==before);
  assert(tap(352,762)&&selected_style==4&&calendar_choice==1);
  failed_save=true;assert(tap(36,762)&&calendar_choice==1&&strstr(s_notice,"保存失败"));failed_save=false;
  assert(tap(card_rect(1).x,card_rect(1).y)&&s_tab==1);assert(tap(352,762)&&selected_style==0&&ticket_choice==1);
  assert(tap(36,258)&&s_tab==0);assert(tap(528,110)&&wallpaper_mode==1);
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
  assert(tap(card_rect(3).x,card_rect(3).y)&&s_tab==3);assert(image_count()==7);unsigned prepared=bookmark_prepares;
  assert(tap(36,758)&&selected_style==3&&bookmark_prepares==prepared+1);
  assert(tap(528,110)&&bookmark_mode==1);
  assert(bookmark_prepares==prepared+2&&!bookmark_prepare_crop);
  failed_save=true;assert(tap(398,110)&&bookmark_mode==1&&strstr(s_notice,"保存失败"));failed_save=false;
  assert(tap(398,110)&&bookmark_mode==0&&bookmark_prepares==prepared+3&&bookmark_prepare_crop);
  for(int i=0;i<8;++i)ui_lock_style_tick(5000+i*200);
  ++revision;assert(!ui_lock_style_tick(7000)); // 不可见预读完成不得刷新。 / Completed invisible prefetch must not refresh.
  assert(tap(36,258));images_ready=false;complete_during_state=true;
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
 // 新阅读或无书时不沿用旧封面，失败占位也会完成一轮定稿。
 // Changing or losing the latest book retires the prior cover and settles the empty state once.
 unsigned prior_cover=cover_prepares;
 ui_lock_style_close();++progress_revision;cover_missing=true;selected_style=APP_LOCK_STYLE_BOOK_COVER;
 ui_lock_style_open(fb);assert(!s_faces[FACE_BOOK_COVER]);assert(pump()==1&&cover_prepares==prior_cover+1);
 assert(s_face_done[FACE_BOOK_COVER]&&!s_faces[FACE_BOOK_COVER]);check_cover_panel(fb,false,false);
 assert(tap(352,148)&&s_off_tab);
 EpdRect cover_card=off_style_rect(4);assert(tap(cover_card.x,cover_card.y));
 check_cover_panel(fb,true,false);assert(pump()==1&&cover_prepares==prior_cover+1&&!pump());
 ui_lock_style_close();cover_missing=false;ui_lock_style_open(fb);
 assert(pump()==1&&cover_prepares==prior_cover+2);check_cover_panel(fb,false,true);
 ui_lock_style_media_lost();assert(!s_faces[FACE_BOOK_COVER]);assert(pump()==1&&cover_prepares==prior_cover+2);
 ui_lock_style_close();ui_lock_style_media_lost();assert(!s_items&&!s_faces[2]);
 free(fb);ttf_font_unload();puts("PASS: inline styles, paging/hold, cancellation, failures/media, 100–200% font bounds, cached reentry, dependency invalidation and one full settle per visible batch");
}
'''
    return code

def main():
    p=argparse.ArgumentParser();p.add_argument('--assets',type=Path);p.add_argument('--output',type=Path,default=ROOT/'build/lock-style-ui');args=p.parse_args()
    native.run(args.output,unit(args.assets))
if __name__=='__main__':main()
