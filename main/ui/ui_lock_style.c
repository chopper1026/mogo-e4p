/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 用户2026-11-11确认：六类缩略图常驻，下方同页点选即用；长按是可选预览。
 * User-approved: six persistent style thumbnails, inline immediate selection and optional hold preview.
 * 用户实屏反馈修订：小图跨页面复用，后台结果攒齐后一次整屏定稿，禁止逐张局刷。
 * Device-feedback revision: reuse thumbnails across visits and settle each completed visible batch once.
 */
#include "ui_lock_style.h"
#include "lock_style_images.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include "ui_calendar.h"
#include "ui_lock_clock.h"
#include "book_ticket.h"
#include "book_progress.h"
#include "book_lock_collage.h"
#include "ttf_font.h"
#include "device_power.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const uint8_t values[]={1,0,2,3,4,APP_LOCK_STYLE_CLOCK};
static const char *const names[]={"壁纸","阅读票根","书架拼贴","透明书封","日历","简约时钟"};
static const char *const options[]={"透明背景","封面背景","老黄历","万年历"};
static int s_tab,s_pages[2],s_preview=-1,s_pending=-1,s_count;
static unsigned s_seen;
static bool s_open,s_catalogue_ready,s_scan_pending,s_offline,s_batch_changed;
static unsigned s_cache_epoch;
static lock_image_item_t *s_items;
static uint8_t *s_faces[6],*s_background;
static bool s_face_done[6];
static uint64_t s_face_keys[6];
static char s_notice[96];
static EpdRect s_dirty;
static lock_image_state_t s_image_states[6];
static bool s_states_valid;
EpdRect ui_lock_style_dirty(void){return s_dirty;}
static void dirty(EpdRect r){
    if(!s_dirty.height){s_dirty=r;return;}
    int x=s_dirty.x<r.x?s_dirty.x:r.x,y=s_dirty.y<r.y?s_dirty.y:r.y;
    int right=s_dirty.x+s_dirty.width>r.x+r.width?s_dirty.x+s_dirty.width:r.x+r.width;
    int bottom=s_dirty.y+s_dirty.height>r.y+r.height?s_dirty.y+s_dirty.height:r.y+r.height;
    s_dirty=(EpdRect){x,y,right-x,bottom-y};
}
static EpdRect card_rect(int i){return (EpdRect){36+(i%3)*210,202+(i/3)*238,192,222};}
static EpdRect option_rect(int i){return (EpdRect){36+i*316,762,296,278};}
static EpdRect image_rect(int i){return (EpdRect){36+i*210,758,192,330};}
static EpdRect wallpaper_mode_rect(int i){return (EpdRect){398+i*130,86,122,50};}
static EpdRect bookmark_mode_rect(int i){return (EpdRect){398+i*130,86,122,50};}
static bool images_tab(void){return s_tab==0||s_tab==3;}
static bool bookmark_image_path(const char *path){
    const char *ext=path?strrchr(path,'.'):NULL;
    return ext&&(!strcasecmp(ext,".png")||!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg"));
}
static const char *selected_path(void){return s_tab==3?app_settings_bookmark_path():app_settings_wallpaper_path();}
static const char *grid_path(bool bookmark){
    const char *selected=bookmark?app_settings_bookmark_path():app_settings_wallpaper_path();
    if(selected[0])return selected;
    for(int i=0;i<s_count;++i)if(!bookmark||bookmark_image_path(s_items[i].path))return s_items[i].path;
    return "";
}
static bool matches(int i){return s_tab!=3||bookmark_image_path(s_items[i].path);}
static int image_count(void){int n=0;for(int i=0;i<s_count;++i)if(matches(i))++n;return n;}
static int image_index(int at){for(int i=0;i<s_count;++i)if(matches(i)&&at--==0)return i;return -1;}
static int page(void){return s_pages[s_tab==3];}
static void visible_states(lock_image_state_t states[6]);
static bool visible_ready(void);
static void request_images(void){
    lock_image_request_t requests[LOCK_IMAGE_REQUESTS];int n=0;
    if(s_preview>=0&&s_preview<s_count)requests[n++]=(lock_image_request_t){s_items[s_preview].path,s_tab==3,true};
    if(images_tab()){
        for(int i=0;i<3;++i){int at=image_index(page()*3+i);if(at>=0)requests[n++]=(lock_image_request_t){s_items[at].path,s_tab==3,false};}
    }
    if(grid_path(false)[0]&&n<LOCK_IMAGE_REQUESTS)requests[n++]=(lock_image_request_t){grid_path(false),false,false};
    if(grid_path(true)[0]&&n<LOCK_IMAGE_REQUESTS)requests[n++]=(lock_image_request_t){grid_path(true),true,false};
    if(images_tab()&&s_preview<0)for(int i=0;i<3&&n<LOCK_IMAGE_REQUESTS;++i){int at=image_index((page()+1)*3+i);if(at>=0)requests[n++]=(lock_image_request_t){s_items[at].path,s_tab==3,false};}
    lock_images_request(requests,n);
    // 先取版本再取状态，避免最后一张恰好完成时被标成已观察却未显示。
    // Read revision before states so a concurrent final completion cannot be acknowledged unseen.
    s_seen=lock_images_revision();visible_states(s_image_states);s_states_valid=true;
}
void ui_lock_style_close(void){
    lock_images_pause();
    free(s_background);s_background=NULL;
    s_open=false;s_pending=s_preview=-1;s_states_valid=false;
}
static void invalidate_cache(void){
    free(s_items);s_items=NULL;s_count=0;s_catalogue_ready=false;
    for(int i=0;i<6;++i){free(s_faces[i]);s_faces[i]=NULL;s_face_done[i]=false;}
    s_cache_epoch=lock_images_epoch();
}
static time_t preview_time(void){
    time_t utc=time(NULL);const pmu_snapshot_t *power=device_power_get();
    if(utc<1704067200&&power&&power->time_synced)utc=power->unix_sec;
    return utc;
}
static void validate_faces(void){
    uint64_t font=UINT64_C(14695981039346656037);
    const char *path=ttf_font_path();
    for(const unsigned char *p=(const unsigned char *)(path?path:"");*p;++p)font=(font^*p)*UINT64_C(1099511628211);
    font=(font^app_settings_system_font_size())*UINT64_C(1099511628211);
    font=(font^app_settings_system_contrast())*UINT64_C(1099511628211);
    time_t utc=preview_time();uint64_t day=utc>=1704067200?(utc+8*3600)/86400:0;
    for(int i=0;i<6;++i){
        uint64_t data=i<2?((uint64_t)book_progress_revision()<<32)^day:i<4?day:i==4?(utc>=1704067200?(uint64_t)utc/60:0):book_lock_collage_preview_revision();
        uint64_t key=(font^data)*UINT64_C(1099511628211);
        if(key!=s_face_keys[i]){s_face_done[i]=false;s_face_keys[i]=key;}
    }
}
void ui_lock_style_open(const uint8_t *background){
    ui_lock_style_close();
    if(s_cache_epoch!=lock_images_epoch())invalidate_cache();
    s_open=true;s_offline=false;s_scan_pending=true;s_notice[0]=0;s_states_valid=false;s_batch_changed=false;
    validate_faces();
    s_pages[0]=s_pages[1]=0;s_tab=1;
    for(int i=0;i<6;++i)if(values[i]==app_settings_lock_style())s_tab=i;
    for(int i=0,bookmark=0;i<s_count;++i){
        if(!strcmp(s_items[i].path,app_settings_wallpaper_path()))s_pages[0]=i/3;
        if(bookmark_image_path(s_items[i].path)){if(!strcmp(s_items[i].path,app_settings_bookmark_path()))s_pages[1]=bookmark/3;++bookmark;}
    }
    if(!s_face_done[0]||!s_face_done[1])s_background=heap_caps_malloc(684u*1216u/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(s_background){if(background)memcpy(s_background,background,684u*1216u/2);else memset(s_background,255,684u*1216u/2);}
    lock_images_open();request_images();
}
void ui_lock_style_media_lost(void){
    lock_images_close();invalidate_cache();s_catalogue_ready=true;s_scan_pending=false;s_offline=true;s_preview=s_pending=-1;s_batch_changed=true;
    free(s_background);s_background=NULL;
    snprintf(s_notice,sizeof(s_notice),"TF 卡已断开");
}
void ui_lock_style_media_ready(void){
    lock_images_close();invalidate_cache();s_pending=s_preview=-1;
    if(s_open){s_scan_pending=true;s_offline=false;s_notice[0]=0;validate_faces();lock_images_open();request_images();s_batch_changed=true;}
}
static void outline(uint8_t *fb,EpdRect r,int radius,int width,uint8_t gray){
    for(int i=0;i<width;++i)ui_draw_round_rect_raw(fb,(EpdRect){r.x+i,r.y+i,r.width-2*i,r.height-2*i},radius>i?radius-i:0,gray);
}
static void label(uint8_t *fb,EpdRect r,int px,const char *text,bool center){
    // 大字号受卡片边界约束，缩图本身保持物理尺寸；文字测量和绘制使用同一字库。
    // Bound enlarged labels by their card while keeping the artwork's physical size fixed.
    px=ui_text_effective_px(px);if(px>r.height-2)px=r.height-2;
    while(px>18&&ui_text_fixed_width_px(px,text)>r.width) --px;
    char fit[288];snprintf(fit,sizeof(fit),"%s",text);
    while(fit[0]&&ui_text_fixed_width_px(px,fit)>r.width){size_t n=strlen(fit)-1;while(n&&((unsigned char)fit[n]&0xc0)==0x80)--n;fit[n]=0;}
    ui_text_fixed_context_aa_vc(fb,center?r.x+r.width/2:r.x,r.y+r.height/2,px,fit,fit,center?EPD_DRAW_ALIGN_CENTER:EPD_DRAW_ALIGN_LEFT,0,255);
}
static void checkmark(uint8_t *fb,int x,int y){
    epd_fill_circle(x,y,17,0,fb);
    for(int i=-1;i<=1;++i){epd_draw_line(x-8,y+i,x-2,y+6+i,255,fb);epd_draw_line(x-2,y+6+i,x+9,y-7+i,255,fb);}
}
static void paint_pixels(uint8_t *fb,const uint8_t *pixels,unsigned width,unsigned height,EpdRect area){
    for(int y=0;y<area.height;++y)for(int x=0;x<area.width;++x){
        unsigned at=(unsigned)y*height/area.height*width+(unsigned)x*width/area.width;
        uint8_t gray=((pixels[at/2]>>((at&1)*4))&15)*17;
        epd_draw_pixel(area.x+x,area.y+y,gray,fb);
    }
}
static void placeholder(uint8_t *fb,EpdRect r,const char *text){
    outline(fb,r,0,1,0xd0);
    int x=r.x+r.width/2,y=r.y+r.height/2;
    ui_draw_icon(fb,x,y-14,r.width>100?36:28,UI_ICON_FILE,0x90);
    if(r.width>110)label(fb,(EpdRect){r.x+4,y+22,r.width-8,32},22,text,true);
}
static const uint8_t *image_pixels(const char *path,bool bookmark,bool large,lock_image_state_t *state){
    if(!path[0]){*state=LOCK_IMAGE_FAILED;return NULL;}
    return lock_images_pixels(path,bookmark,large,state);
}
static void paint_image(uint8_t *fb,const char *path,bool bookmark,bool large,EpdRect r){
    lock_image_state_t state;const uint8_t *p=image_pixels(path,bookmark,large,&state);
    if(p)paint_pixels(fb,p,large?342:160,large?608:284,r);
    else placeholder(fb,r,state==LOCK_IMAGE_FAILED?"无法加载":"加载中");
}
static int face_for(int tab){return tab==1?(int)app_settings_ticket_background():tab==4?2+(int)app_settings_calendar_style():tab==5?4:5;}
static void paint_face(uint8_t *fb,int tab,EpdRect area){
    if(tab==0||tab==3){paint_image(fb,grid_path(tab==3),tab==3,false,area);return;}
    int face=face_for(tab);
    if(s_faces[face])paint_pixels(fb,s_faces[face],160,284,area);
    else placeholder(fb,area,s_face_done[face]?(tab==1?"暂无阅读":"暂无预览"):"准备中");
}
static void visible_states(lock_image_state_t states[6]){
    for(int i=0;i<6;++i)states[i]=LOCK_IMAGE_FAILED;
    if(s_preview>=0){image_pixels(s_items[s_preview].path,s_tab==3,true,&states[5]);return;}
    image_pixels(grid_path(false),false,false,&states[0]);
    image_pixels(grid_path(true),true,false,&states[1]);
    if(images_tab())for(int i=0;i<3;++i){int at=image_index(page()*3+i);if(at>=0)image_pixels(s_items[at].path,s_tab==3,false,&states[2+i]);}
}
static bool visible_ready(void){
    if(!s_catalogue_ready||s_scan_pending||!s_states_valid)return false;
    // 使用本轮已记录的状态；再次采样可能在最后一张完成的竞态中重复定稿。
    // Settle the recorded snapshot; resampling a racing completion could trigger two full refreshes.
    for(int i=0;i<6;++i)if(s_image_states[i]==LOCK_IMAGE_PENDING)return false;
    if(s_preview>=0)return true;
    for(int i=0;i<6;++i)if(!s_face_done[i])return false;
    return true;
}
void ui_lock_style_render(uint8_t *fb){
    ui_clear_page(fb);ui_nav_status(fb);
    ui_draw_icon(fb,57,115,44,UI_ICON_CHEVRON_LEFT,0);
    label(fb,(EpdRect){96,80,s_preview>=0?500:282,64},44,s_preview>=0?"图片预览":"锁屏样式",false);
    if(s_preview<0&&(s_tab==0||s_tab==3)){
        for(int i=0;i<2;++i){
            EpdRect r=s_tab==0?wallpaper_mode_rect(i):bookmark_mode_rect(i);
            bool active=s_tab==0 ? (unsigned)app_settings_wallpaper_mode()==(unsigned)i :
                (unsigned)app_settings_bookmark_mode()==(unsigned)i;
            outline(fb,r,9,active?3:1,active?0:0xb0);
            label(fb,(EpdRect){r.x+4,r.y+2,r.width-8,r.height-4},21,i==0?"裁剪填满":"完整显示",true);
        }
    }
    if(s_preview>=0){
        paint_image(fb,s_items[s_preview].path,s_tab==3,true,(EpdRect){90,184,504,896});
        label(fb,(EpdRect){36,142,612,36},24,s_items[s_preview].name,true);
        ui_nav_draw(fb,3);return;
    }
    label(fb,(EpdRect){36,148,612,38},24,s_notice[0]?s_notice:
        ((s_tab==0||s_tab==3)?"裁剪填满铺满屏幕，完整显示保留原图比例":"点选即用 · 长按图片预览"),false);
    for(int i=0;i<6;++i){
        EpdRect r=card_rect(i);bool active=app_settings_lock_style()==values[i];
        outline(fb,r,12,s_tab==i?3:1,s_tab==i?0:0xb0);
        paint_face(fb,i,(EpdRect){r.x+50,r.y+12,92,164});
        outline(fb,(EpdRect){r.x+50,r.y+12,92,164},0,1,0xd0);
        label(fb,(EpdRect){r.x+8,r.y+182,176,38},27,names[i],true);
        if(active)checkmark(fb,r.x+171,r.y+23);
    }
    if(images_tab()){
        label(fb,(EpdRect){36,687,390,48},30,s_tab==3?"书封图片":"壁纸图片",false);
        int count=image_count(),pages=(count+2)/3;if(!pages)pages=1;
        EpdRect prev={452,683,64,64},next={584,683,64,64};
        outline(fb,prev,10,1,0xb0);outline(fb,next,10,1,0xb0);
        ui_draw_icon(fb,484,715,32,UI_ICON_CHEVRON_LEFT,page()?0:0xb0);
        ui_draw_icon(fb,616,715,32,UI_ICON_CHEVRON_RIGHT,page()+1<pages?0:0xb0);
        char position[24];snprintf(position,sizeof(position),"%d/%d",page()+1,pages);
        label(fb,(EpdRect){518,686,64,58},23,position,true);
        if(!count){
            label(fb,(EpdRect){36,826,612,52},30,!s_catalogue_ready?"正在查找图片":s_offline?"请插入 TF 卡":"暂无可选图片",true);
            label(fb,(EpdRect){36,890,612,46},24,s_tab==3?"将 PNG / JPG 书封放入 pictures 文件夹":"将 JPG / PNG 放入 pictures 文件夹",true);
        }
        for(int i=0;i<3;++i){
            int at=image_index(page()*3+i);if(at<0)break;EpdRect r=image_rect(i);
            bool active=app_settings_lock_style()==values[s_tab]&&!strcmp(selected_path(),s_items[at].path);
            paint_image(fb,s_items[at].path,s_tab==3,false,(EpdRect){r.x+16,764,160,284});
            outline(fb,(EpdRect){r.x+12,760,168,292},4,active?3:1,active?0:0xb0);
            label(fb,(EpdRect){r.x+2,1055,188,36},24,s_items[at].name,true);
            if(active)checkmark(fb,r.x+162,780);
        }
    }else if(s_tab==1||s_tab==4){
        label(fb,(EpdRect){36,687,360,48},30,s_tab==1?"票根背景":"日历版式",false);
        for(int i=0;i<2;++i){
            EpdRect r=option_rect(i);int face=(s_tab==1?0:2)+i;
            bool active=app_settings_lock_style()==values[s_tab]&&i==(s_tab==1?(int)app_settings_ticket_background():(int)app_settings_calendar_style());
            outline(fb,r,12,active?3:1,active?0:0xb0);
            if(s_faces[face])paint_pixels(fb,s_faces[face],160,284,(EpdRect){r.x+22,r.y+24,126,224});
            else placeholder(fb,(EpdRect){r.x+22,r.y+24,126,224},s_face_done[face]?"暂无阅读":"准备中");
            label(fb,(EpdRect){r.x+164,r.y+92,112,46},27,options[face],true);
            if(active)checkmark(fb,r.x+270,r.y+27);
        }
        label(fb,(EpdRect){36,1050,612,38},23,s_tab==1?"保留票面、书摘与阅读统计":"按锁屏时的日期生成",false);
    }else{
        label(fb,(EpdRect){36,687,612,48},30,names[s_tab],false);
        paint_face(fb,s_tab,(EpdRect){52,752,180,320});
        label(fb,(EpdRect){266,820,382,56},30,s_tab==5?"每分钟更新":"你的书架，自动拼贴",false);
        label(fb,(EpdRect){266,890,382,44},24,s_tab==5?"浅睡时持续显示时间":"预览为上次生成效果",false);
        if(s_tab==2&&!s_faces[5])label(fb,(EpdRect){266,948,382,44},24,"首次锁屏后显示预览",false);
    }
    ui_nav_draw(fb,3);
}
static uint8_t frame_gray(const uint8_t *fb,int x,int y){
    int px=x,py=y;
    switch(epd_get_rotation()){
        case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;
        case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;
        case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;
        default:break;
    }
    return epd_get_pixel(px,py,epd_width(),epd_height(),fb)>>4;
}
static uint8_t *reduce_frame(const uint8_t *fb){
    uint8_t *pixels=heap_caps_malloc(LOCK_THUMB_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!pixels)return NULL;
    for(unsigned y=0;y<284;++y)for(unsigned x=0;x<160;x+=2){
        unsigned pair=0;
        for(unsigned i=0;i<2;++i){unsigned sum=0;
            for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx)
                sum+=frame_gray(fb,((x+i)*4+1+dx*2)*684/(160*4),(y*4+1+dy*2)*1216/(284*4));
            pair|=((sum+2)/4)<<(i*4);
        }
        pixels[(y*160+x)/2]=pair;
    }
    return pixels;
}
static void prepare_face(int face){
    if(s_offline&&(face<2||face==5)){s_face_done[face]=true;return;}
    uint8_t *canvas=heap_caps_malloc(684u*1216u/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_face_done[face]=true;if(!canvas){
        free(s_faces[face]);s_faces[face]=NULL;s_face_keys[face]=0;
        if(face==1){free(s_background);s_background=NULL;}
        return;
    }
    memset(canvas,255,684u*1216u/2);bool ok=true;
    if(face<2){if(s_background)memcpy(canvas,s_background,684u*1216u/2);ok=book_ticket_preview(canvas,face==1);}
    else if(face<5){
        time_t utc=preview_time();
        calendar_date_t date;bool valid=calendar_from_utc((int64_t)utc*1000,&date);
        if(face<4)ui_calendar_draw(canvas,valid?&date:NULL,face==3);
        else{time_t china=utc+8*3600;struct tm tm={0};gmtime_r(&china,&tm);
            ui_lock_clock_face_t clock={.valid=valid,.hour=tm.tm_hour,.minute=tm.tm_min,.month=tm.tm_mon+1,.day=tm.tm_mday,.weekday=tm.tm_wday};
            ui_lock_clock_draw(canvas,&clock);
        }
    }else ok=book_lock_collage_preview(canvas);
    uint8_t *pixels=ok?reduce_frame(canvas):NULL;
    free(s_faces[face]);s_faces[face]=pixels;
    // 失效后无法重建不能继续展示旧书；本次结束等待，下次进入再尝试。
    // Failed replacement must not display a stale book; settle now and retry on the next visit.
    if(!pixels)s_face_keys[face]=0;
    free(canvas);
    if(face==1){free(s_background);s_background=NULL;}
}
static bool save_choice(const char *path,unsigned option){
    if(s_tab==0)option=(unsigned)app_settings_wallpaper_mode();
    if(s_tab==3)option=(unsigned)app_settings_bookmark_mode();
    bool ok=app_settings_select_lock(values[s_tab],path,option);
    snprintf(s_notice,sizeof(s_notice),"%s",ok?"已保存":"保存失败，请重试");
    return ok;
}
static bool save_wallpaper_mode(unsigned mode){
    bool ok=app_settings_set_wallpaper_mode((app_wallpaper_mode_t)mode);
    snprintf(s_notice,sizeof(s_notice),"%s",ok?"显示方式已保存":"保存失败，请重试");
    return ok;
}
static bool save_bookmark_mode(unsigned mode){
    bool ok=app_settings_set_bookmark_mode((app_bookmark_mode_t)mode);
    snprintf(s_notice,sizeof(s_notice),"%s",ok?"显示方式已保存":"保存失败，请重试");
    return ok;
}
bool ui_lock_style_tick(int64_t now_ms){
    (void)now_ms;
    if(!s_open)return false;
    bool changed=false;s_dirty=(EpdRect){0};
    if(s_scan_pending){
        bool finished=false;int count=0;lock_image_item_t *items=lock_images_catalogue(&count,&finished);
        if(finished){
            changed=!s_catalogue_ready;
            if(items){
                bool same=s_count==count;
                for(int i=0;same&&i<count;++i)same=!strcmp(s_items[i].path,items[i].path)&&s_items[i].key==items[i].key;
                if(!same){free(s_items);s_items=items;s_count=count;s_pending=s_preview=-1;changed=true;}else free(items);
            }
            s_catalogue_ready=true;s_scan_pending=false;
            if(changed){
            s_pages[0]=s_pages[1]=0;
            for(int i=0,bookmark=0;i<s_count;++i){
                if(!strcmp(s_items[i].path,app_settings_wallpaper_path()))s_pages[0]=i/3;
                if(bookmark_image_path(s_items[i].path)){if(!strcmp(s_items[i].path,app_settings_bookmark_path()))s_pages[1]=bookmark/3;++bookmark;}
            }
            }
            // 旧像素可能刚因源身份变化退休；和上次状态比较后才替换快照。
            // Retired source pixels must be compared with the previous visible snapshot.
            lock_image_state_t previous[6];memcpy(previous,s_image_states,sizeof(previous));
            request_images();
            if(memcmp(previous,s_image_states,sizeof(previous)))changed=true;
        }
    }
    if(s_pending>=0){
        lock_image_state_t state;const uint8_t *p=image_pixels(s_items[s_pending].path,s_tab==3,false,&state);
        if(p){save_choice(s_items[s_pending].path,0);s_pending=-1;request_images();dirty((EpdRect){36,148,612,944});changed=true;}
        else if(state==LOCK_IMAGE_FAILED){snprintf(s_notice,sizeof(s_notice),"图片无法加载，请换一张");s_pending=-1;dirty((EpdRect){36,148,612,38});changed=true;}
    }
    // 分tick准备但不逐张刷屏；完成当前可见批次后统一定稿。
    // Prepare across ticks without presenting each thumbnail; settle the visible batch once.
    if(s_preview<0&&s_catalogue_ready){
        int face=-1;for(int i=0;i<6;++i)if(!s_face_done[i]){face=i;break;}
        if(face>=0){prepare_face(face);
        int tab=face<2?1:face<4?4:face==4?5:2;
        if(face_for(tab)==face)dirty(card_rect(tab));
        if(s_tab==tab)dirty((EpdRect){36,752,612,340});
        changed=changed||s_dirty.height>0;
        }
    }
    unsigned revision=lock_images_revision();
    if(revision!=s_seen||!s_states_valid){
        lock_image_state_t states[6];visible_states(states);
        for(int i=0;i<6;++i)if(!s_states_valid||states[i]!=s_image_states[i]){
            if(i==5&&s_preview>=0)dirty((EpdRect){90,184,504,896});
            else if(s_preview<0){
                if(i<2)dirty(card_rect(i==0?0:3));
                else if(i<5&&images_tab()&&image_index(page()*3+i-2)>=0)dirty(image_rect(i-2));
            }
        }
        memcpy(s_image_states,states,sizeof(states));s_states_valid=true;s_seen=revision;
        changed=changed||s_dirty.height>0;
    }
    s_batch_changed=s_batch_changed||changed;
    if(!s_batch_changed||!visible_ready())return false;
    s_batch_changed=false;s_dirty=(EpdRect){0,0,684,1216};
    return true;
}
bool ui_lock_style_back(void){if(s_preview<0)return false;s_preview=-1;s_notice[0]=0;request_images();return true;}
bool ui_lock_style_gesture(const ui_gesture_event_t *ev){
    if(s_preview>=0){if(ev->type==UI_GESTURE_TAP&&ev->y0<190)return ui_lock_style_back();return false;}
    bool tap=ev->type==UI_GESTURE_TAP,hold=ev->type==UI_GESTURE_LONG_PRESS;
    if(tap&&(s_tab==0||s_tab==3))for(int i=0;i<2;++i){
        EpdRect mode_rect=s_tab==0?wallpaper_mode_rect(i):bookmark_mode_rect(i);
        if(ui_rect_hit(mode_rect,ev->x0,ev->y0)){
            if(s_tab==0)save_wallpaper_mode((unsigned)i);else save_bookmark_mode((unsigned)i);
            return true;
        }
    }
    if(tap)for(int i=0;i<6;++i)if(ui_rect_hit(card_rect(i),ev->x0,ev->y0)){
        s_tab=i;s_pending=-1;s_notice[0]=0;
        if(i==1)save_choice(NULL,app_settings_ticket_background());
        else if(i==5||i==2)save_choice(NULL,0);
        // 壁纸/书封和日历在下方明确选项后启用；看分类不会意外替换当前锁屏。
        // Enable image/calendar styles after an explicit inline choice.
        request_images();return true;
    }
    if(images_tab()){
        int delta=0;
        if((tap&&ui_rect_hit((EpdRect){584,683,64,64},ev->x0,ev->y0))||((ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_U)&&ev->y0>=683))delta=1;
        if((tap&&ui_rect_hit((EpdRect){452,683,64,64},ev->x0,ev->y0))||((ev->type==UI_GESTURE_SWIPE_R||ev->type==UI_GESTURE_SWIPE_D)&&ev->y0>=683))delta=-1;
        if(delta){int next=page()+delta;if(next<0||next*3>=image_count())return false;s_pages[s_tab==3]=next;s_pending=-1;s_notice[0]=0;request_images();return true;}
        if(tap||hold)for(int i=0;i<3;++i)if(ui_rect_hit(image_rect(i),ev->x0,ev->y0)){
            int at=image_index(page()*3+i);if(at<0)return false;
            if(hold){s_preview=at;s_pending=-1;s_notice[0]=0;request_images();return true;}
            s_pending=-1;
            lock_image_state_t state;const uint8_t *p=image_pixels(s_items[at].path,s_tab==3,false,&state);
            if(p){save_choice(s_items[at].path,0);request_images();}
            else if(state==LOCK_IMAGE_FAILED)snprintf(s_notice,sizeof(s_notice),"图片无法加载，请换一张");
            else{s_pending=at;snprintf(s_notice,sizeof(s_notice),"加载后应用，可继续挑选");}
            return true;
        }
    }else if(tap&&(s_tab==1||s_tab==4))for(int i=0;i<2;++i)if(ui_rect_hit(option_rect(i),ev->x0,ev->y0)){save_choice(NULL,i);return true;}
    return false;
}
