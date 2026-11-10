/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：五本实体封面的环形扇列；逆向定点旋转、绘制和命中使用同一几何。
 * English: Circular fan of five covers, sharing inverse fixed-point rotation for drawing and hit testing. */
#include "ui_mini_library.h"
#include "ui_kit.h"
#include "ui_nav_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CARD_W 174
#define CARD_H 218
typedef struct { int x,y,c,s; } card_t;
static int offset(unsigned i, unsigned selected, unsigned count) {
    int d = (int)((i + count - selected) % count);
    return d > (int)count / 2 ? d - (int)count : d;
}
static card_t geometry(EpdRect r, unsigned i, unsigned count, unsigned selected, ui_mini_library_frame_t f) {
    static const int sine[] = {0,18,36,54,71,89,107,125,143,160,178};
    int old = offset(i,f.from < count ? f.from : selected,count), now = offset(i,selected,count);
    int d = (old * (256 - (int)f.progress) + now * (int)f.progress) * (int)f.spread / 256;
    int angle = d * 5 / 256, a = abs(angle);
    return (card_t){r.x+342+d*105/256, r.y+184+abs(d)*6/256+(256-(int)f.spread)*12/256,
                    1024-sine[a]*sine[a]/2048, angle < 0 ? -sine[a] : sine[a]};
}
static void inverse(card_t c, int x, int y, int *u, int *v) {
    int dx=x-c.x,dy=y-c.y;
    *u=(dx*c.c+dy*c.s)/1024+CARD_W/2;
    *v=(-dx*c.s+dy*c.c)/1024+CARD_H/2;
}
static bool inside(EpdRect r,int x,int y) { return x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height; }
static void text(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align) { ui_text_fixed_vc(fb,x,y,px,s,align,false); }
static void fitted(uint8_t *fb,int x,int y,int px,const char *value,int width,enum EpdFontFlags align) {
    char shown[196];snprintf(shown,sizeof(shown),"%s",value); bool clipped=false;
    while (*shown && ui_text_fixed_width_px(px,shown)+(clipped?ui_text_fixed_width_px(px,"..."):0)>width) {
        size_t n=strlen(shown)-1;while(n && ((unsigned char)shown[n]&192)==128)--n;shown[n]=0;clipped=true;
    }
    if(clipped)strcat(shown,"...");
    text(fb,x,y,px,shown,align);
}
static void card(uint8_t *fb,card_t c,const home_library_cover_t *cover,bool chosen,bool motion,unsigned index) {
    int iw=CARD_W-8,ih=CARD_H-8;
    if(cover && cover->pixels) {
        if(cover->width*(unsigned)ih>cover->height*(unsigned)iw)ih=cover->height*iw/cover->width;
        else iw=cover->width*ih/cover->height;
    }
    for(int y=c.y-126;y<=c.y+130;++y)for(int x=c.x-110;x<=c.x+114;++x) {
        int u,v;inverse(c,x,y,&u,&v);
        if(u<0||u>=CARD_W+4||v<0||v>=CARD_H+4)continue;
        uint8_t gray=255;
        if(u>=CARD_W||v>=CARD_H)gray=(u==CARD_W+3||v==CARD_H+3)?0:255;
        else if(u<2||v<2||u>=CARD_W-2||v>=CARD_H-2||u==7)gray=0;
        else if(cover && cover->pixels && u>=(CARD_W-iw)/2 && u<(CARD_W+iw)/2 && v>=(CARD_H-ih)/2 && v<(CARD_H+ih)/2) {
            unsigned sx=(unsigned)(u-(CARD_W-iw)/2)*cover->width/iw,sy=(unsigned)(v-(CARD_H-ih)/2)*cover->height/ih;
            gray=cover->fast?(cover->pixels[sy*((cover->width+7)/8)+sx/8]&(1u<<(sx&7))?255:0):cover->pixels[sy*cover->width+sx];
            if(motion)gray=gray>=144?255:0;
        } else if(!cover || !cover->pixels) {
            // 等封面时以纸页线条呈现，书名始终在扇列下方。/ Paper lines while artwork loads; the title always remains below the deck.
            if((v==44||v==46||v==170) && u>20 && u<CARD_W-16)gray=0;
            if(v>72&&v<145&&((u+v+(int)index*17)%61<2))gray=0;
        }
        if(chosen && u>=CARD_W-27 && u<CARD_W-15 && v<25 && (v<19||abs(u-(CARD_W-21))>v-19))gray=0;
        epd_draw_pixel(x,y,gray,fb);
    }
}
void ui_mini_library_draw(uint8_t *fb,EpdRect r,const home_library_view_t *v,ui_mini_library_frame_t f) {
    if(r.width!=684||r.height!=472||r.x||r.y<0||r.y+472>UI_NAV_TOP||v->count>5)return;
    ui_fill_rect_fast(r,255,fb);ui_draw_separator(fb,r.y,36,612,144);
    text(fb,36,r.y+38,32,"迷你书库",EPD_DRAW_ALIGN_LEFT);
    text(fb,432,r.y+38,24,"随机",EPD_DRAW_ALIGN_CENTER);
    text(fb,582,r.y+38,24,"最近阅读",EPD_DRAW_ALIGN_CENTER);
    ui_fill_rect_fast((EpdRect){v->recent?534:408,r.y+60,v->recent?96:48,2},0,fb);
    char meta[164] = "";
    if(v->count) {
        for(int depth=2;depth>=0;--depth)for(unsigned i=0;i<v->count;++i)
            if(abs(offset(i,v->selected,v->count))==depth)card(fb,geometry(r,i,v->count,v->selected,f),v->covers?&v->covers[i]:NULL,i==v->selected,f.motion,i);
        if(!v->covers || !v->covers[v->selected].pixels) {
            fitted(fb,342,r.y+170,24,v->books[v->selected].title,140,EPD_DRAW_ALIGN_CENTER);
        }
        for(unsigned i=0;i<v->count;++i) {
            int x=r.x+342+((int)i*2-(int)v->count+1)*36;
            char n[16];snprintf(n,sizeof(n),"%02u",i+1);text(fb,x,r.y+336,23,n,EPD_DRAW_ALIGN_CENTER);
            if(i==v->selected)ui_fill_rect_fast((EpdRect){x-12,r.y+355,24,3},0,fb);
        }
        const book_mini_item_t *b=&v->books[v->selected];
        fitted(fb,36,r.y+386,34,b->title,612,EPD_DRAW_ALIGN_LEFT);
        if(b->has_progress)snprintf(meta,sizeof(meta),"%s%s已读 %u%%",b->author,b->author[0]?" · ":"",b->pct);
        else snprintf(meta,sizeof(meta),"%s%s还未翻开",b->author,b->author[0]?" · ":"");
    } else {
        ui_draw_icon(fb,342,r.y+168,54,UI_ICON_BOOK_OPEN,0);
        text(fb,342,r.y+237,30,v->loading?"正在整理书匣":v->recent?"还没有阅读记录":"书匣等你放入第一本书",EPD_DRAW_ALIGN_CENTER);
        text(fb,342,r.y+282,23,v->recent?"先去书架打开一本书":"将图书放入书架，即可在这里挑选",EPD_DRAW_ALIGN_CENTER);
    }
    // 留空导航上方不可全像素整理的末24行。/ Keep changing ink above the final 24 rows protected from full-pixel cleanup.
    const char *detail = v->notice&&*v->notice?v->notice:v->loading?"正在整理书架...":meta;
    if(*detail)fitted(fb,36,r.y+430,24,detail,v->recent?612:478,EPD_DRAW_ALIGN_LEFT);
    if(!v->recent) {
        ui_draw_icon(fb,550,r.y+430,24,UI_ICON_REFRESH_CW,0);
        text(fb,648,r.y+430,23,"换一批",EPD_DRAW_ALIGN_RIGHT);
    }
}
ui_mini_hit_t ui_mini_library_hit(EpdRect r,const home_library_view_t *v,int x,int y) {
    if(!inside(r,x,y))return(ui_mini_hit_t){0};
    if(inside((EpdRect){384,r.y+10,100,58},x,y))return(ui_mini_hit_t){UI_MINI_RANDOM,0};
    if(inside((EpdRect){510,r.y+10,150,58},x,y))return(ui_mini_hit_t){UI_MINI_RECENT,0};
    if(!v->recent && inside((EpdRect){526,r.y+408,134,44},x,y))return(ui_mini_hit_t){UI_MINI_SHUFFLE,0};
    if(!v->count||v->count>5)return(ui_mini_hit_t){0};
    for(unsigned i=0;i<v->count;++i) {
        int cx=342+((int)i*2-(int)v->count+1)*36;
        if(inside((EpdRect){cx-36,r.y+313,72,52},x,y))return(ui_mini_hit_t){UI_MINI_SELECT,i};
    }
    ui_mini_library_frame_t f={v->selected,256,256,false};
    for(int depth=0;depth<=2;++depth)for(int i=(int)v->count-1;i>=0;--i) {
        if(abs(offset((unsigned)i,v->selected,v->count))!=depth)continue;
        int u,w;inverse(geometry(r,(unsigned)i,v->count,v->selected,f),x,y,&u,&w);
        if(u>=0&&u<CARD_W&&w>=0&&w<CARD_H)
            return(ui_mini_hit_t){(unsigned)i==v->selected?UI_MINI_OPEN:UI_MINI_SELECT,(unsigned)i};
    }
    return(ui_mini_hit_t){0};
}
