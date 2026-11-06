/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：当月逐书明细与跨月七日打卡摘要；快照兼容旧版，日期回拨不虚构旧月数据。
 * English: Monthly book details and a cross-month seven-day streak, with legacy snapshots and no invented history on rollback.
 */
#include "reading_journal_model.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool month_valid(uint32_t month) {
    return month >= 202401 && month <= 209912 && month % 100 >= 1 && month % 100 <= 12;
}
static int month_days(int year, int month) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month-1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
bool reading_journal_date(int64_t utc_ms, reading_journal_date_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (utc_ms < INT64_C(1704067200000) || utc_ms >= INT64_C(4102416000000) - 8*3600000) return false;
    time_t civil = (time_t)(utc_ms / 1000 + 8*3600);
    struct tm date = {0};
    if (!gmtime_r(&civil, &date)) return false;
    *out = (reading_journal_date_t){
        .year = date.tm_year+1900, .month = date.tm_mon+1, .day = date.tm_mday,
        .weekday = (date.tm_wday+6)%7, .serial = (int32_t)(civil/86400),
    };
    out->days = month_days(out->year, out->month);
    out->first_weekday = (out->weekday + 35 - (out->day-1)%7)%7;
    return true;
}
void reading_journal_clear(reading_journal_model_t *model) {
    reading_journal_record_t *row = model->records;
    while (row) { reading_journal_record_t *next = row->next; free(row); row = next; }
    model->records = NULL;
    model->count = 0;
}
static uint32_t streak_sum(uint32_t a, uint32_t b) {
    return b >= READING_STREAK_GOAL_MS-a ? READING_STREAK_GOAL_MS : a+b;
}
static bool streak_sync(reading_streak_t *streak, int32_t day) {
    if (streak->day == day) return false;
    if (!streak->day || day < streak->day || day-streak->day > READING_STREAK_DAYS) {
        memset(streak,0,sizeof(*streak)); streak->day=day; return true;
    }
    while (streak->day < day) {
        streak->before=streak->milliseconds[0]>=READING_STREAK_GOAL_MS ? streak->before+1 : 0;
        memmove(streak->milliseconds,streak->milliseconds+1,(READING_STREAK_DAYS-1)*sizeof(uint32_t));
        streak->milliseconds[READING_STREAK_DAYS-1]=0; ++streak->day;
    }
    return true;
}
void reading_journal_streak_add(reading_journal_model_t *model, int32_t day, uint32_t milliseconds) {
    int offset=model->streak.day-day;
    if (!model->streak.day || offset<0 || offset>=READING_STREAK_DAYS) return;
    uint32_t *value=&model->streak.milliseconds[READING_STREAK_DAYS-1-offset];
    *value=streak_sum(*value,milliseconds);
}
bool reading_journal_streak_import(reading_journal_model_t *model, const reading_journal_date_t *date) {
    if (!date || !date->serial || model->month!=(uint32_t)(date->year*100+date->month)) return false;
    reading_streak_t old=model->streak;
    (void)streak_sync(&model->streak,date->serial);
    uint32_t totals[31]={0};
    for (const reading_journal_record_t *r=model->records; r; r=r->next)
        totals[r->day-1]=streak_sum(totals[r->day-1],r->milliseconds);
    for (int day=1;day<=date->day;++day) {
        int index=READING_STREAK_DAYS-1+day-date->day;
        if (index>=0 && totals[day-1]>model->streak.milliseconds[index])
            model->streak.milliseconds[index]=totals[day-1];
    }
    unsigned before=0;
    for (int day=date->day-READING_STREAK_DAYS;day>0 && totals[day-1]>=READING_STREAK_GOAL_MS;--day) ++before;
    if (before>model->streak.before) model->streak.before=before;
    return memcmp(&old,&model->streak,sizeof(old))!=0;
}
reading_streak_view_t reading_journal_streak_view(const reading_journal_model_t *model, const reading_journal_date_t *date) {
    reading_streak_view_t view={0};
    if (!date || !date->serial || model->streak.day!=date->serial) return view;
    view.valid=true;
    memcpy(view.milliseconds,model->streak.milliseconds,sizeof(view.milliseconds));
    for (int i=0;i<READING_STREAK_DAYS;++i) {
        time_t civil=(time_t)(date->serial-READING_STREAK_DAYS+1+i)*86400;
        struct tm value={0};
        if (gmtime_r(&civil,&value)) view.dates[i]=(uint8_t)value.tm_mday;
    }
    int i=view.milliseconds[READING_STREAK_DAYS-1]>=READING_STREAK_GOAL_MS ? READING_STREAK_DAYS-1 : READING_STREAK_DAYS-2;
    for (;i>=0 && view.milliseconds[i]>=READING_STREAK_GOAL_MS;--i) ++view.days;
    if (i<0) view.days+=model->streak.before;
    return view;
}
bool reading_journal_sync(reading_journal_model_t *model, const reading_journal_date_t *date) {
    if (!date || !date->serial) return false;
    bool changed = false;
    if (!model->streak.day && model->scene_day && model->month!=(uint32_t)(date->year*100+date->month)) {
        reading_journal_date_t known;
        if (model->scene_day<=date->serial &&
            reading_journal_date((int64_t)model->scene_day*86400000-8*INT64_C(3600000),&known))
            changed |= reading_journal_streak_import(model,&known);
    }
    if (!model->streak.day || date->serial<model->streak.day)
        changed |= reading_journal_streak_import(model,date);
    changed |= streak_sync(&model->streak,date->serial);
    uint32_t month = (uint32_t)(date->year*100+date->month);
    if (model->month != month) {
        reading_journal_clear(model); model->month = month; changed = true;
    }
    if (model->scene_day != date->serial) {
        if (!model->scene_ready) model->scene = (unsigned)date->serial % READING_JOURNAL_SCENES;
        else if (model->scene_day && date->serial > model->scene_day)
            model->scene = (model->scene + (unsigned)(date->serial-model->scene_day)) % READING_JOURNAL_SCENES;
        model->scene_ready = true; model->scene_day = date->serial; changed = true;
    }
    return changed;
}
void reading_journal_next_scene(reading_journal_model_t *model) {
    model->scene = (model->scene+1)%READING_JOURNAL_SCENES;
    model->scene_ready = true;
}
uint64_t reading_journal_book_id(const char *path, uint32_t size) {
    uint64_t id = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) id = (id ^ *p)*UINT64_C(1099511628211);
    for (unsigned i=0; i<4; ++i) id = (id ^ ((size>>(8*i))&255))*UINT64_C(1099511628211);
    return id ? id : 1;
}
static uint32_t add_sat(uint32_t a, uint32_t b) { return UINT32_MAX-a < b ? UINT32_MAX : a+b; }
bool reading_journal_add(reading_journal_model_t *model, unsigned day, uint64_t book,
                         const char *title, unsigned chapter, uint32_t milliseconds, uint32_t turns) {
    if (!month_valid(model->month) || !day || day > (unsigned)month_days(model->month/100, model->month%100) ||
        !book || !title || !*title || chapter >= READING_JOURNAL_CHAPTERS) return false;
    reading_journal_record_t *row = model->records;
    while (row && (row->day != day || row->book != book)) row = row->next;
    if (!row) {
        if (model->count >= READING_JOURNAL_RECORDS) return false;
        row = heap_caps_calloc(1, sizeof(*row), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!row) return false;
        row->day = (uint8_t)day; row->book = book;
        size_t length = strlen(title);
        if (length >= sizeof(row->title)) {
            length = sizeof(row->title)-1;
            while (length && ((uint8_t)title[length]&192)==128) --length;
        }
        memcpy(row->title, title, length);
        row->next = model->records; model->records = row; ++model->count;
    }
    uint8_t mask = 1u << (chapter%8);
    if (!(row->visited[chapter/8]&mask)) { row->visited[chapter/8] |= mask; ++row->chapters; }
    row->milliseconds = add_sat(row->milliseconds, milliseconds);
    row->turns = add_sat(row->turns, turns);
    return true;
}
reading_journal_totals_t reading_journal_totals(const reading_journal_model_t *model, unsigned day) {
    reading_journal_totals_t total = {0}; uint32_t days = 0;
    for (const reading_journal_record_t *r=model->records; r; r=r->next) {
        if (day && r->day != day) continue;
        total.milliseconds += r->milliseconds;
        total.turns = add_sat(total.turns, r->turns); total.chapters = add_sat(total.chapters, r->chapters);
        days |= 1u << (r->day-1);
        bool seen = false;
        for (const reading_journal_record_t *p=model->records; p != r; p=p->next)
            if ((!day || p->day == day) && p->book == r->book) { seen = true; break; }
        if (!seen) ++total.books;
    }
    for (; days; days &= days-1) ++total.days;
    return total;
}
const reading_journal_record_t *reading_journal_at(const reading_journal_model_t *model, unsigned day, unsigned index) {
    for (const reading_journal_record_t *r=model->records; r; r=r->next)
        if (r->day == day && !index--) return r;
    return NULL;
}
static void put32(uint8_t *p, uint32_t n) { for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(n>>(8*i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t checksum(const uint8_t *data, size_t length) {
    uint32_t value = 2166136261u;
    for (size_t i=0; i<length; ++i) if (i<28 || i>=32) value=(value^data[i])*16777619u;
    return value;
}
static size_t bits_length(const reading_journal_record_t *r) {
    size_t n=sizeof(r->visited); while (n && !r->visited[n-1]) --n; return n;
}
size_t reading_journal_encode(const reading_journal_model_t *model, uint64_t sequence, uint8_t *out, size_t capacity) {
    size_t size=68;
    for (const reading_journal_record_t *r=model->records; r; r=r->next) size += 20+strlen(r->title)+bits_length(r);
    if (!out) return size;
    if (capacity<size || size>READING_JOURNAL_FILE_LIMIT) return 0;
    memset(out,0,68); memcpy(out,"KJR2",4); put32(out+4,(uint32_t)size);
    put32(out+8,(uint32_t)sequence); put32(out+12,(uint32_t)(sequence>>32));
    put32(out+16,model->month); put32(out+20,(uint32_t)model->scene_day);
    out[24]=(uint8_t)model->scene; out[25]=model->scene_ready;
    out[26]=(uint8_t)model->count; out[27]=(uint8_t)(model->count>>8);
    put32(out+32,(uint32_t)model->streak.day); put32(out+36,model->streak.before);
    for (unsigned i=0;i<READING_STREAK_DAYS;++i) put32(out+40+4*i,model->streak.milliseconds[i]);
    size_t at=68;
    for (const reading_journal_record_t *r=model->records; r; r=r->next) {
        size_t title=strlen(r->title), bits=bits_length(r);
        put32(out+at,(uint32_t)r->book); put32(out+at+4,(uint32_t)(r->book>>32));
        put32(out+at+8,r->milliseconds); put32(out+at+12,r->turns);
        out[at+16]=r->day; out[at+17]=(uint8_t)title;
        out[at+18]=(uint8_t)bits; out[at+19]=(uint8_t)(bits>>8); at+=20;
        memcpy(out+at,r->title,title); at+=title;
        memcpy(out+at,r->visited,bits); at+=bits;
    }
    put32(out+28,checksum(out,size)); return size;
}
bool reading_journal_decode(reading_journal_model_t *model, uint64_t *sequence, const uint8_t *data, size_t length) {
    if (!data || length<32 || length>READING_JOURNAL_FILE_LIMIT || (memcmp(data,"KJR1",4) && memcmp(data,"KJR2",4)) ||
        get32(data+4)!=length || checksum(data,length)!=get32(data+28)) return false;
    reading_journal_model_t candidate = {
        .month=get32(data+16), .scene_day=(int32_t)get32(data+20), .scene=data[24], .scene_ready=data[25]!=0,
    };
    unsigned rows=data[26]+256u*data[27];
    if ((candidate.month && !month_valid(candidate.month)) || candidate.scene>=READING_JOURNAL_SCENES ||
        data[25]>1 || rows>READING_JOURNAL_RECORDS || (rows && !candidate.month) ||
        candidate.scene_day<0 || candidate.scene_day>47481) return false;
    reading_journal_record_t **tail=&candidate.records;
    size_t at=32;
    if (!memcmp(data,"KJR2",4)) {
        if (length<68) return false;
        candidate.streak.day=(int32_t)get32(data+32); candidate.streak.before=get32(data+36);
        if (candidate.streak.day<0 || candidate.streak.day>47481 || candidate.streak.before>27759 ||
            (candidate.streak.day && candidate.streak.day<19723)) return false;
        for (unsigned i=0;i<READING_STREAK_DAYS;++i) {
            candidate.streak.milliseconds[i]=get32(data+40+4*i);
            if (candidate.streak.milliseconds[i]>READING_STREAK_GOAL_MS ||
                (!candidate.streak.day && candidate.streak.milliseconds[i])) return false;
        }
        if (!candidate.streak.day && candidate.streak.before) return false;
        at=68;
    }
    for (unsigned i=0; i<rows; ++i) {
        if (length-at<20) goto failed;
        uint64_t book=get32(data+at)|((uint64_t)get32(data+at+4)<<32);
        unsigned day=data[at+16], title=data[at+17], bits=data[at+18]+256u*data[at+19];
        if (!book || !day || day>(unsigned)month_days(candidate.month/100,candidate.month%100) ||
            !title || title>=128 || !bits || bits>1024 || length-at-20<title+bits) goto failed;
        for (reading_journal_record_t *p=candidate.records; p; p=p->next)
            if (p->book==book && p->day==day) goto failed;
        reading_journal_record_t *r=heap_caps_calloc(1,sizeof(*r),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!r) goto failed;
        *tail=r; tail=&r->next; ++candidate.count;
        r->book=book; r->day=(uint8_t)day; r->milliseconds=get32(data+at+8); r->turns=get32(data+at+12);
        at+=20;
        if (memchr(data+at,0,title)) goto failed;
        memcpy(r->title,data+at,title); at+=title;
        memcpy(r->visited,data+at,bits); at+=bits;
        for (unsigned j=0; j<bits; ++j)
            for (uint8_t v=r->visited[j]; v; v &= (uint8_t)(v-1)) ++r->chapters;
        if (!r->chapters) goto failed;
    }
    if (at!=length) goto failed;
    reading_journal_clear(model); *model=candidate;
    if (sequence) *sequence=get32(data+8)|((uint64_t)get32(data+12)<<32);
    return true;
failed:
    reading_journal_clear(&candidate); return false;
}
