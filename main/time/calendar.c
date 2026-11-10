/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 */
/* 中文：只读离线历法，民俗信息用于文化展示；不联网或写设备状态。
 * English: Read-only offline calendar and cultural folk information; no network or device mutations.
 */
#include "calendar.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef struct { int32_t serial; int16_t year; int8_t month; uint8_t days; } calendar_lunar_month_t;
typedef struct { uint16_t offset; uint8_t cycle; } calendar_month_cycle_t;
#include "assets/calendar_data.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static const char *const month_names[] = {"正月","二月","三月","四月","五月","六月","七月","八月","九月","十月","冬月","腊月"};
static const char *const day_names[] = {"初一","初二","初三","初四","初五","初六","初七","初八","初九","初十","十一","十二","十三","十四","十五","十六","十七","十八","十九","二十","廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十"};
static int days_in_month(int y,int m) {
    static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0));
}
static int32_t civil_serial(int y,int m,int d) {
    y-=m<=2; int era=y/400; unsigned yo=(unsigned)(y-era*400);
    unsigned doy=(153*(unsigned)(m+(m>2?-3:9))+2)/5+(unsigned)d-1;
    return era*146097+(int)(yo*365+yo/4-yo/100+doy)-719468;
}
const char *calendar_month_name(int month) {
    static const char *const names[]={"一月","二月","三月","四月","五月","六月","七月","八月","九月","十月","十一月","十二月"};
    return month>=1&&month<=12?names[month-1]:"";
}
const char *calendar_cycle_name(int cycle) { return cycle>=0&&cycle<60?calendar_ganzhi[cycle]:""; }
const char *calendar_term(int year,int month,int which,int *day) {
    if(day)*day=0;
    if(year<2000||year>2099||month<1||month>12||which<0||which>1)return "";
    int i=(month-1)*2+which;
    if(day)*day=calendar_term_days[year-2000][i];
    return calendar_terms[i];
}
bool calendar_date(int year,int month,int day,calendar_date_t *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(year<2000||year>2099||month<1||month>12||day<1||day>days_in_month(year,month))return false;
    int32_t serial=civil_serial(year,month,day);
    unsigned lo=0,hi=COUNT(calendar_lunar_months);
    while(lo+1<hi){unsigned mid=(lo+hi)/2;if(calendar_lunar_months[mid].serial<=serial)lo=mid;else hi=mid;}
    const calendar_lunar_month_t *l=&calendar_lunar_months[lo];
    if(serial<l->serial||serial>=l->serial+l->days)return false;
    int offset=serial-10957;
    unsigned a=0,b=COUNT(calendar_month_cycles);
    while(a+1<b){unsigned mid=(a+b)/2;if(calendar_month_cycles[mid].offset<=offset)a=mid;else b=mid;}
    *out=(calendar_date_t){.year=year,.month=month,.day=day,.weekday=(serial+4)%7,.days=days_in_month(year,month),
        .first_weekday=(civil_serial(year,month,1)+4)%7,.lunar_year=l->year,.lunar_month=l->month,
        .lunar_day=serial-l->serial+1,.year_cycle=(l->year-4)%60,.month_cycle=calendar_month_cycles[a].cycle,
        .day_cycle=(offset+54)%60};
    unsigned pair=(unsigned)(out->month_cycle*60+out->day_cycle);
    out->yi=calendar_yiji[calendar_yiji_pairs[pair][0]];out->ji=calendar_yiji[calendar_yiji_pairs[pair][1]];
    static const char *const wealth[]={"东北","东北","西南","西南","正北","正北","正东","正东","正南","正南"};
    static const char *const animals[]={"鼠","牛","虎","兔","龙","蛇","马","羊","猴","鸡","狗","猪"};
    static const char *const sha[]={"南","东","北","西"};
    out->wealth=wealth[out->day_cycle%10];out->clash=animals[(out->day_cycle+6)%12];out->sha=sha[out->day_cycle%4];
    unsigned spirit=(unsigned)(out->month_cycle%12*60+out->day_cycle);
    unsigned role=(unsigned)(out->month_cycle%12*12+out->day_cycle%12);
    out->joy=calendar_joy[out->day_cycle%10];out->fortune=calendar_fortune[out->day_cycle%10];
    out->auspicious=calendar_folk[calendar_spirit_pairs[spirit][0]];
    out->inauspicious=calendar_folk[calendar_spirit_pairs[spirit][1]];
    out->officer=calendar_folk[calendar_day_roles[role][0]];
    out->guardian=calendar_folk[calendar_day_roles[role][1]];
    out->guardian_type=calendar_folk[calendar_day_roles[role][2]];
    return true;
}
bool calendar_from_utc(int64_t ms,calendar_date_t *out) {
    if(out)memset(out,0,sizeof(*out));
    if(ms<INT64_C(1704067200000)||ms>=INT64_C(4102416000000))return false;
    time_t civil=(time_t)(ms/1000+8*3600);struct tm tm={0};
    return gmtime_r(&civil,&tm)&&calendar_date(tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,out);
}
void calendar_lunar_text(const calendar_date_t *d,bool short_label,char *out,unsigned capacity) {
    if(!out||!capacity)return;
    out[0]=0;if(!d||d->lunar_day<1||d->lunar_day>30||!d->lunar_month)return;
    if(short_label)for(int i=0;i<2;++i){int day=0;const char *term=calendar_term(d->year,d->month,i,&day);if(day==d->day){snprintf(out,capacity,"%s",term);return;}}
    int month=d->lunar_month<0?-d->lunar_month:d->lunar_month;
    if(short_label&&d->lunar_day!=1)snprintf(out,capacity,"%s",day_names[d->lunar_day-1]);
    else snprintf(out,capacity,"%s%s%s%s",short_label?"":"农历",d->lunar_month<0?"闰":"",month_names[month-1],short_label?"":day_names[d->lunar_day-1]);
}
