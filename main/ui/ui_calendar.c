/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 */
/* 中文：老黄历与万年历的纯绘制；保持白底黑字和固定画幅，不改变字体设置。
 * English: Pure almanac/month-calendar drawing in black on white, without changing font preferences.
 */
#include "ui_calendar.h"
#include "ui_kit.h"
#include "assets/calendar_digits.h"
#include <stdio.h>
#include <string.h>
static const char *const weekdays[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
static const char *const animals[]={"鼠","牛","虎","兔","龙","蛇","马","羊","猴","鸡","狗","猪"};
static void text(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align,bool white) {
    ui_text_fixed_vc(fb,x,y,px,s,align,white);
}
static void fitted(uint8_t *fb,int x,int y,int px,int width,const char *s,enum EpdFontFlags align,bool white) {
    while(px>16&&ui_text_fixed_width_px(px,s)>width)--px;
    text(fb,x,y,px,s,align,white);
}
static void line(uint8_t *fb,int x,int y,int width,int height) {
    epd_fill_rect((EpdRect){x,y,width,height},UI_GRAY_BLACK,fb);
}
typedef struct { int16_t x,y; } ornament_point_t;
// 装饰用纯黑三像素描边；沿主轴的法向扩宽，避免竖线仍只有一像素。
// Pure-black three-pixel strokes expand across the main axis, retaining vertical weight.
static void ornament_stroke(uint8_t *fb,ornament_point_t a,ornament_point_t b) {
    int dx=b.x-a.x,dy=b.y-a.y;
    bool vertical=(dx<0?-dx:dx)<(dy<0?-dy:dy);
    for(int offset=-1;offset<=1;++offset)
        epd_draw_line(a.x+(vertical?offset:0),a.y+(vertical?0:offset),
            b.x+(vertical?offset:0),b.y+(vertical?0:offset),UI_GRAY_BLACK,fb);
}
static void ornament_curve(uint8_t *fb,ornament_point_t a,ornament_point_t b,ornament_point_t c,ornament_point_t d) {
    ornament_point_t last=a;
    for(int t=1;t<=24;++t){
        int u=24-t;
        ornament_point_t p={
            (u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x)/13824,
            (u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y)/13824};
        ornament_stroke(fb,last,p);last=p;
    }
}
static ornament_point_t ornament_at(int x,int y,int sx,int sy,int dx,int dy) {
    return (ornament_point_t){x+sx*dx,y+sy*dy};
}
static void key_corner(uint8_t *fb,int x,int y,int sx,int sy) {
    static const ornament_point_t path[]={{0,24},{0,0},{24,0},{24,6},{6,6},{6,18},{18,18},{18,12},{12,12}};
    for(unsigned i=1;i<sizeof(path)/sizeof(path[0]);++i)
        ornament_stroke(fb,ornament_at(x,y,sx,sy,path[i-1].x,path[i-1].y),ornament_at(x,y,sx,sy,path[i].x,path[i].y));
}
static void fret_band(uint8_t *fb,int y) {
    static const ornament_point_t path[]={{0,18},{0,0},{34,0},{34,18},{10,18},{10,8},{24,8}};
    for(int x=48;x<636;x+=42)for(unsigned i=1;i<sizeof(path)/sizeof(path[0]);++i)
        ornament_stroke(fb,ornament_at(x,y,1,1,path[i-1].x,path[i-1].y),ornament_at(x,y,1,1,path[i].x,path[i].y));
}
static void cloud(uint8_t *fb,int x,int y,int sx) {
    static const ornament_point_t curves[][4]={
        {{2,30},{2,14},{22,12},{28,21}},{{28,21},{25,0},{58,-2},{64,17}},
        {{64,17},{74,6},{95,14},{91,29}},{{91,29},{108,26},{111,45},{93,45}},
        {{18,45},{4,45},{0,38},{2,30}},{{32,29},{34,18},{51,18},{53,29}},
        {{53,29},{54,39},{38,40},{39,31}},{{73,31},{76,24},{86,27},{83,34}}};
    for(unsigned i=0;i<sizeof(curves)/sizeof(curves[0]);++i){
        ornament_point_t p[4];
        for(int j=0;j<4;++j)p[j]=ornament_at(x,y,sx,1,curves[i][j].x,curves[i][j].y);
        ornament_curve(fb,p[0],p[1],p[2],p[3]);
    }
    ornament_stroke(fb,ornament_at(x,y,sx,1,18,45),ornament_at(x,y,sx,1,93,45));
    ornament_stroke(fb,ornament_at(x,y,sx,1,36,53),ornament_at(x,y,sx,1,100,53));
    ornament_stroke(fb,ornament_at(x,y,sx,1,58,60),ornament_at(x,y,sx,1,86,60));
}
static void lozenge(uint8_t *fb,int x,int y) {
    const ornament_point_t p[]={{x,y-5},{x+5,y},{x,y+5},{x-5,y},{x,y-5}};
    for(int i=1;i<5;++i)ornament_stroke(fb,p[i-1],p[i]);
}
static void landscape(uint8_t *fb) {
    const ornament_point_t ridge[]={{264,167},{286,153},{300,141},{310,147},{320,150},{335,130},{344,125},{357,108},{367,123},{376,129},{389,146},{405,162},{418,151},{433,137},{444,150},{455,153},{478,166},{530,166}};
    for(int radius=12;radius<=14;++radius)epd_draw_circle(440,109,radius,UI_GRAY_BLACK,fb);
    for(unsigned i=1;i<sizeof(ridge)/sizeof(ridge[0]);++i)ornament_stroke(fb,ridge[i-1],ridge[i]);
    ornament_curve(fb,(ornament_point_t){357,110},(ornament_point_t){352,133},(ornament_point_t){369,143},(ornament_point_t){368,162});
    ornament_curve(fb,(ornament_point_t){351,125},(ornament_point_t){345,137},(ornament_point_t){347,148},(ornament_point_t){335,160});
    ornament_curve(fb,(ornament_point_t){366,127},(ornament_point_t){370,143},(ornament_point_t){381,147},(ornament_point_t){388,162});
    for(int i=0;i<4;++i){
        ornament_stroke(fb,(ornament_point_t){371+i*5,137+i*6},(ornament_point_t){377+i*6,140+i*6});
        ornament_stroke(fb,(ornament_point_t){435+i*7,145+i*4},(ornament_point_t){432+i*7,155+i*3});
    }
    ornament_curve(fb,(ornament_point_t){286,166},(ornament_point_t){308,155},(ornament_point_t){327,157},(ornament_point_t){346,168});
    ornament_curve(fb,(ornament_point_t){280,172},(ornament_point_t){342,163},(ornament_point_t){406,184},(ornament_point_t){502,174});
    ornament_stroke(fb,(ornament_point_t){306,182},(ornament_point_t){388,182});
    ornament_stroke(fb,(ornament_point_t){426,182},(ornament_point_t){480,182});
}
static void water_band(uint8_t *fb,int y) {
    for(int x=48;x<636;x+=84)for(int inset=0;inset<24;inset+=8)
        ornament_curve(fb,(ornament_point_t){x+inset,y+26},(ornament_point_t){x+14+inset,y-6+inset},
            (ornament_point_t){x+70-inset,y-6+inset},(ornament_point_t){x+84-inset,y+26});
}
static int number_width(int value,int height) {
    int width=calendar_digit_widths[value%10];
    if(value>=10)width+=calendar_digit_widths[value/10]+8;
    return width*height/256;
}
static void number(uint8_t *fb,int value,int x,int top,int height) {
    char digits[4];snprintf(digits,sizeof(digits),"%d",value);
    for(const char *p=digits;*p;++p){
        unsigned d=(unsigned)(*p-'0');int source_width=calendar_digit_widths[d],stride=(source_width+7)/8;
        int width=source_width*height/256;
        const uint8_t *bits=calendar_digit_bits+calendar_digit_offsets[d];
        for(int y=0;y<height;++y)for(int dx=0;dx<width;++dx){
            int sx=dx*source_width/width,sy=y*256/height;
            if(bits[sy*stride+sx/8]&(1u<<(7-sx%8)))epd_draw_pixel(x+dx,top+y,UI_GRAY_BLACK,fb);
        }
        x+=width+8*height/256;
    }
}
static void vertical(uint8_t *fb,int x,int cy,const char *s) {
    int count=(int)strlen(s)/3;
    for(int i=0;i<count;++i){char ch[4]={0};memcpy(ch,s+i*3,3);text(fb,x,cy+(2*i-count+1)*21,33,ch,EPD_DRAW_ALIGN_CENTER,false);}
}
static void stems(const calendar_date_t *d,char *out,unsigned capacity) {
    snprintf(out,capacity,"%s年 · %s月 · %s日",calendar_cycle_name(d->year_cycle),calendar_cycle_name(d->month_cycle),calendar_cycle_name(d->day_cycle));
}
static void folk(uint8_t *fb,const calendar_date_t *d,int x,int right,int y) {
    char s[96];
    snprintf(s,sizeof(s),"冲%s（%s）· 煞%s",d->clash,calendar_cycle_name((d->day_cycle+54)%60),d->sha);
    fitted(fb,x,y,24,(right-x)*2/3,s,EPD_DRAW_ALIGN_LEFT,false);
    snprintf(s,sizeof(s),"财神 · %s",d->wealth);text(fb,right,y,24,s,EPD_DRAW_ALIGN_RIGHT,false);
}
static void season(uint8_t *fb,const calendar_date_t *d,int y) {
    for(int i=0;i<2;++i){
        char s[64];int day;const char *term=calendar_term(d->year,d->month,i,&day);
        snprintf(s,sizeof(s),"%s · %d月%d日",term,d->month,day);
        text(fb,i?636:48,y,24,s,i?EPD_DRAW_ALIGN_RIGHT:EPD_DRAW_ALIGN_LEFT,false);
    }
}
static void yiji(uint8_t *fb,const char *words,int x,int top,int width,int count,int px) {
    char rows[2][160]={{0}};int row=0,items=0;const char *p=words;
    while(*p&&items<count*2){
        const char *end=strchr(p,' ');size_t n=end?(size_t)(end-p):strlen(p);
        row=items/count;size_t used=strlen(rows[row]);
        if(used){memcpy(rows[row]+used,"　",3);used+=3;}
        if(used+n+4<sizeof(rows[row])){memcpy(rows[row]+used,p,n);rows[row][used+n]=0;}
        ++items;p=end?end+1:p+n;
    }
    if(*p)strcat(rows[1],"…");
    if(!rows[0][0])snprintf(rows[0],sizeof(rows[0]),"无");
    for(int i=0;i<2;++i)if(rows[i][0])fitted(fb,x,top+i*(px+10),px,width,rows[i],EPD_DRAW_ALIGN_LEFT,false);
}
static void badge(uint8_t *fb,int x,int y,int size,bool yi) {
    EpdRect box={x,y,size,size};
    if(yi)epd_fill_rect(box,UI_GRAY_BLACK,fb);else epd_draw_rect(box,UI_GRAY_BLACK,fb);
    text(fb,x+size/2,y+size/2,size*2/3,yi?"宜":"忌",EPD_DRAW_ALIGN_CENTER,yi);
}
// 按实际字宽排词，末行预留省略号；有高度时整组垂直居中。
// Wrap measured words with a final-row ellipsis, centering the block within an optional height.
static int almanac_words(uint8_t *fb,const char *words,int x,int y,int width,int px,int limit,int height,enum EpdFontFlags align) {
    char rows[4][192]={{0}};int row=0;
    const char *p=words;
    while(*p){
        const char *end=strchr(p,' ');size_t length=end?(size_t)(end-p):strlen(p);
        char candidate[224];int used=snprintf(candidate,sizeof(candidate),"%s%s%.*s%s",rows[row],rows[row][0]?"　":"",(int)length,p,row==limit-1&&end?"…":"");
        if(used<0||(size_t)used>=sizeof(rows[row])||ui_text_fixed_width_px(px,candidate)>width){
            if(row+1<limit){++row;continue;}
            strcat(rows[row],"…");break;
        }
        size_t offset=strlen(rows[row]);
        if(offset){memcpy(rows[row]+offset,"　",3);offset+=3;}
        memcpy(rows[row]+offset,p,length);rows[row][offset+length]=0;
        p=end?end+1:p+length;
    }
    if(!rows[0][0]&&row==0)snprintf(rows[0],sizeof(rows[0]),"无");
    if(height)y+=(height-row*34)/2;
    for(int i=0;i<=row;++i)if(rows[i][0])text(fb,x,y+i*34,px,rows[i],align,false);
    return row+1;
}
static void engraving(uint8_t *fb,int x,int y,const ornament_point_t (*curves)[4],unsigned count) {
    for(unsigned i=0;i<count;++i){
        ornament_point_t p[4];
        for(int j=0;j<4;++j)p[j]=ornament_at(x,y,1,1,curves[i][j].x,curves[i][j].y);
        ornament_curve(fb,p[0],p[1],p[2],p[3]);
    }
}
static void blossom(uint8_t *fb,int x,int y) {
    static const ornament_point_t petals[]={{0,-7},{7,-2},{4,6},{-4,6},{-7,-2}};
    for(unsigned i=0;i<5;++i)epd_draw_circle(x+petals[i].x,y+petals[i].y,5,UI_GRAY_BLACK,fb);
    epd_fill_circle(x,y,2,UI_GRAY_BLACK,fb);
}
static void plum_branch(uint8_t *fb,int x,int y) {
    static const ornament_point_t curves[][4]={
        {{-104,24},{-42,16},{17,-2},{72,-26}},{{-91,24},{-36,22},{20,1},{63,-18}},
        {{-8,3},{-13,-7},{-18,-13},{-19,-23}},{{25,-10},{22,-20},{24,-24},{29,-31}},
        {{-46,15},{-36,1},{-28,2},{-30,12}},{{43,-15},{48,-3},{61,-2},{60,-14}}};
    engraving(fb,x,y,curves,sizeof(curves)/sizeof(curves[0]));
    blossom(fb,x+72,y-26);blossom(fb,x-19,y-23);blossom(fb,x+29,y-31);
    epd_fill_circle(x-62,y+15,3,UI_GRAY_BLACK,fb);
}
static void almanac_activities(uint8_t *fb,const calendar_date_t *d) {
    const char *const words[]={d->yi,d->ji};
    for(int i=0;i<2;++i){
        int x=i?492:192;
        if(i){epd_draw_circle(x,538,25,UI_GRAY_BLACK,fb);epd_draw_circle(x,538,23,UI_GRAY_BLACK,fb);}
        else epd_fill_circle(x,538,25,UI_GRAY_BLACK,fb);
        text(fb,x,538,36,i?"忌":"宜",EPD_DRAW_ALIGN_CENTER,!i);
        for(int side=-1;side<=1;side+=2){
            ornament_curve(fb,(ornament_point_t){x+side*40,538},(ornament_point_t){x+side*58,525},
                (ornament_point_t){x+side*78,550},(ornament_point_t){x+side*100,537});
            lozenge(fb,x+side*112,537);
        }
        unsigned items=1;for(const char *p=words[i];*p;++p)if(*p==' ')++items;
        int rows=almanac_words(fb,words[i],x,582,260,items<=4?32:26,4,0,EPD_DRAW_ALIGN_CENTER);
        if(rows<=2)plum_branch(fb,x,680);
    }
}
static void almanac_cut_frame(uint8_t *fb,EpdRect r,int cut,uint8_t gray) {
    int right=r.x+r.width-1,bottom=r.y+r.height-1;
    const ornament_point_t p[]={{r.x+cut,r.y},{right-cut,r.y},{right,r.y+cut},{right,bottom-cut},
        {right-cut,bottom},{r.x+cut,bottom},{r.x,bottom-cut},{r.x,r.y+cut}};
    for(unsigned i=0;i<8;++i)epd_draw_line(p[i].x,p[i].y,p[(i+1)%8].x,p[(i+1)%8].y,gray,fb);
}
static void almanac_plaque(uint8_t *fb,EpdRect r,const char *s,int px) {
    const int cut=6;
    for(int y=0;y<r.height;++y){
        int inset=y<cut?cut-y:y>=r.height-cut?y-(r.height-cut-1):0;
        epd_fill_rect((EpdRect){r.x+inset,r.y+y,r.width-inset*2,1},UI_GRAY_BLACK,fb);
    }
    almanac_cut_frame(fb,(EpdRect){r.x+3,r.y+3,r.width-6,r.height-6},4,UI_GRAY_WHITE);
    text(fb,r.x+r.width/2,r.y+r.height/2,px,s,EPD_DRAW_ALIGN_CENTER,true);
}
static void almanac_details(uint8_t *fb,const calendar_date_t *d) {
    almanac_cut_frame(fb,(EpdRect){56,737,572,125},10,UI_GRAY_BLACK);
    almanac_cut_frame(fb,(EpdRect){57,738,570,123},9,UI_GRAY_BLACK);
    almanac_cut_frame(fb,(EpdRect){61,742,562,115},7,UI_GRAY_BLACK);
    almanac_plaque(fb,(EpdRect){266,721,152,36},"三神吉位",24);
    const char *const labels[]={"财神","喜神","福神"};
    const char *const positions[]={d->wealth,d->joy,d->fortune};
    for(int i=0;i<3;++i){
        int x=152+i*190;
        text(fb,x,779,24,labels[i],EPD_DRAW_ALIGN_CENTER,false);
        text(fb,x,821,42,positions[i],EPD_DRAW_ALIGN_CENTER,false);
        if(i<2){
            line(fb,x+95,778,1,52);
            epd_fill_circle(x+95,773,2,UI_GRAY_BLACK,fb);
            epd_fill_circle(x+95,835,2,UI_GRAY_BLACK,fb);
        }
    }
    almanac_cut_frame(fb,(EpdRect){56,884,572,181},10,UI_GRAY_BLACK);
    almanac_cut_frame(fb,(EpdRect){60,888,564,173},8,UI_GRAY_BLACK);
    const char *const titles[]={"吉神宜趋","凶煞宜忌"};
    const char *const spirits[]={d->auspicious,d->inauspicious};
    for(int i=0;i<2;++i){
        almanac_plaque(fb,(EpdRect){74,903+i*92,122,40},titles[i],24);
        almanac_words(fb,spirits[i],218,890+i*92,390,26,2,70,EPD_DRAW_ALIGN_LEFT);
    }
    for(int x=77;x<607;x+=6)line(fb,x,974,2,1);
    char s[96];
    almanac_plaque(fb,(EpdRect){68,1083,66,32},"值神",22);
    snprintf(s,sizeof(s),"%s（%s）",d->guardian,d->guardian_type);
    fitted(fb,146,1099,24,270,s,EPD_DRAW_ALIGN_LEFT,false);
    almanac_plaque(fb,(EpdRect){450,1083,66,32},"建除",22);
    snprintf(s,sizeof(s),"%s日",d->officer);text(fb,532,1099,26,s,EPD_DRAW_ALIGN_LEFT,false);
    almanac_cut_frame(fb,(EpdRect){148,1125,388,33},7,UI_GRAY_BLACK);
    snprintf(s,sizeof(s),"冲%s（%s）· 煞%s",d->clash,calendar_cycle_name((d->day_cycle+54)%60),d->sha);
    text(fb,342,1141,26,s,EPD_DRAW_ALIGN_CENTER,false);
}
static void grid(uint8_t *fb,const calendar_date_t *d,bool large) {
    static const char *const labels[]={"日","一","二","三","四","五","六"};
    int left=large?48:56,width=large?588:572;
    int first_y=large?518:934;
    int rows=(d->first_weekday+d->days+6)/7;
    int step=(large?300:200)/(rows-1);
    for(int col=0;col<7;++col)text(fb,left+(2*col+1)*width/14,large?465:899,26,labels[col],EPD_DRAW_ALIGN_CENTER,false);
    for(int day=1;day<=d->days;++day){
        int index=d->first_weekday+day-1,cx=left+(2*(index%7)+1)*width/14,cy=first_y+(index/7)*step;
        bool today=day==d->day;char s[40];
        if(today){
            if(large)epd_fill_rect((EpdRect){cx-33,cy-20,66,59},UI_GRAY_BLACK,fb);
            else epd_fill_circle(cx,cy,17,UI_GRAY_BLACK,fb);
        }
        snprintf(s,sizeof(s),"%d",day);text(fb,cx,cy,large?30:25,s,EPD_DRAW_ALIGN_CENTER,today);
        if(large){
            calendar_date_t cell;calendar_date(d->year,d->month,day,&cell);calendar_lunar_text(&cell,true,s,sizeof(s));
            fitted(fb,cx,cy+25,23,76,s,EPD_DRAW_ALIGN_CENTER,today);
            for(int t=0;t<2;++t){int term_day;calendar_term(d->year,d->month,t,&term_day);if(day==term_day)line(fb,cx-18,cy+39,36,1);}
        }
    }
}
void ui_calendar_draw(uint8_t *fb,const calendar_date_t *d,bool monthly) {
    memset(fb,255,UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2);
    text(fb,44,42,24,"mogo",EPD_DRAW_ALIGN_LEFT,false);
    text(fb,640,42,24,monthly?"万年历":"老黄历",EPD_DRAW_ALIGN_RIGHT,false);
    if(!d||!d->year){
        text(fb,342,522,48,"请校准时间",EPD_DRAW_ALIGN_CENTER,false);
        text(fb,342,590,26,"设置 → 日期与时间",EPD_DRAW_ALIGN_CENTER,false);
        text(fb,342,644,24,"校准后重新锁屏即可显示日历",EPD_DRAW_ALIGN_CENTER,false);
        return;
    }
    char s[96],lunar[64];calendar_lunar_text(d,false,lunar,sizeof(lunar));
    if(!monthly){
        epd_draw_rect((EpdRect){32,69,620,1103},UI_GRAY_BLACK,fb);
        epd_draw_rect((EpdRect){33,70,618,1101},UI_GRAY_BLACK,fb);
        epd_draw_rect((EpdRect){40,77,604,1087},UI_GRAY_BLACK,fb);
        key_corner(fb,48,85,1,1);key_corner(fb,635,85,-1,1);
        key_corner(fb,48,1155,1,-1);key_corner(fb,635,1155,-1,-1);
        lozenge(fb,342,89);line(fb,280,89,45,1);line(fb,360,89,45,1);
        snprintf(s,sizeof(s),"公元 %d年",d->year);text(fb,48,132,24,s,EPD_DRAW_ALIGN_LEFT,false);
        snprintf(s,sizeof(s),"%s · %s年",calendar_cycle_name(d->year_cycle),animals[d->year_cycle%12]);
        text(fb,636,132,24,s,EPD_DRAW_ALIGN_RIGHT,false);line(fb,48,158,588,1);
        fret_band(fb,176);
        vertical(fb,96,310,calendar_month_name(d->month));vertical(fb,588,310,weekdays[d->weekday]);
        number(fb,d->day,342-number_width(d->day,184)/2,210,184);
        fitted(fb,342,437,40,352,lunar,EPD_DRAW_ALIGN_CENTER,false);
        stems(d,s,sizeof(s));text(fb,342,489,26,s,EPD_DRAW_ALIGN_CENTER,false);
        cloud(fb,62,435,1);cloud(fb,622,435,-1);
        almanac_activities(fb,d);almanac_details(fb,d);season(fb,d,1186);
    }else{
        landscape(fb);
        int w=number_width(d->month,78);number(fb,d->month,48,95,78);
        text(fb,48+w+20,151,34,"月",EPD_DRAW_ALIGN_LEFT,false);
        snprintf(s,sizeof(s),"%d",d->year);text(fb,636,151,28,s,EPD_DRAW_ALIGN_RIGHT,false);line(fb,48,192,588,2);
        number(fb,d->day,48,250,112);
        text(fb,280,260,27,weekdays[d->weekday],EPD_DRAW_ALIGN_LEFT,false);
        fitted(fb,280,310,39,356,lunar,EPD_DRAW_ALIGN_LEFT,false);
        stems(d,s,sizeof(s));fitted(fb,280,357,24,356,s,EPD_DRAW_ALIGN_LEFT,false);
        water_band(fb,398);
        line(fb,48,439,588,1);line(fb,48,490,588,1);grid(fb,d,true);
        line(fb,48,878,588,2);line(fb,48,994,588,1);line(fb,342,899,1,74);
        badge(fb,48,901,46,true);yiji(fb,d->yi,111,913,211,2,26);
        badge(fb,366,901,46,false);yiji(fb,d->ji,428,913,208,2,26);
        season(fb,d,1047);water_band(fb,1080);
        line(fb,48,1128,588,1);folk(fb,d,48,636,1162);
    }
}
