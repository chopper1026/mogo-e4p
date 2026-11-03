/*
 * 中文：真实天气模型的边界、过期、跨日与坏响应回归。
 * English: Real weather model regressions for bounds, expiry, midnight and malformed responses.
 */
#include "weather_model.h"
#include "weather_test_data.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static weather_location_t city;
static int64_t now;
static bool parse(cJSON *root) {
    char *json=cJSON_PrintUnformatted(root);weather_forecast_t f;
    bool ok=weather_forecast_parse(json,strlen(json),now,&city,&f);free(json);return ok;
}
static void invalid_field(const char *field,const char *value) {
    cJSON *root=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON_ReplaceItemInObjectCaseSensitive(root,field,cJSON_CreateString(value));
    assert(!parse(root));cJSON_Delete(root);
}
int main(void) {
    now=weather_timestamp("2026-11-02T00:30:00Z");assert(now);
    assert(weather_location_parse(WEATHER_TEST_CITY,strlen(WEATHER_TEST_CITY),&city));
    weather_forecast_t f={0};
    assert(weather_forecast_parse(WEATHER_TEST_FORECAST,strlen(WEATHER_TEST_FORECAST),now,&city,&f));
    assert(weather_day_for(&f,now,false)->humidity==68&&weather_day_for(&f,now,true)->min_c==16);
    char text[193];
    weather_date_text(weather_local_day(now),text,sizeof(text));assert(!strcmp(text,"11月02日  星期一"));
    weather_time_text(f.days[0].sunrise,text,sizeof(text));assert(!strcmp(text,"06:15"));
    weather_time_text(f.days[0].sunset,text,sizeof(text));assert(!strcmp(text,"17:12"));
    weather_time_text(0,text,sizeof(text));assert(!strcmp(text,"--:--"));
    assert(weather_forecast_valid(&f,f.serve-1)&&!weather_forecast_valid(&f,f.serve));
    assert(!weather_day_for(&f,0,false)&&!weather_forecast_valid(&f,now-3600));
    assert(!weather_forecast_parse("{} trailing",11,now,&city,&f));
    invalid_field("source","Other");invalid_field("fetchedAt","2026-11-02T08:20:00Z");
    invalid_field("freshUntil","2026-11-01T00:00:00Z");invalid_field("serveUntil","2026-11-03T00:00:00Z");
    invalid_field("stale","false");invalid_field("schemaVersion","1");
    const char *bad_dates[]={"2026-02-29T00:00:00Z","2026-11-02T24:00:00Z","2026-11-02T00:60:00Z",
        "2026-11-02T00:00:60Z","2026-11-02T00:00:00+08:00","2026-11-02T00:00:00.Z","abc"};
    for(unsigned i=0;i<sizeof(bad_dates)/sizeof(*bad_dates);++i)assert(!weather_timestamp(bad_dates[i]));
    assert(weather_timestamp("2028-02-29T23:59:59.123Z"));
    for(int i=0;i<8;++i) {
        cJSON *root=cJSON_Parse(WEATHER_TEST_FORECAST);cJSON *d=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"days"),0);
        if(i==0)cJSON_SetNumberValue(cJSON_GetObjectItem(d,"dayHumidityPct"),101);
        if(i==1)cJSON_SetNumberValue(cJSON_GetObjectItem(d,"minC"),25);
        if(i==2)cJSON_ReplaceItemInObject(d,"date",cJSON_CreateString("2026-11-03"));
        if(i==3)cJSON_ReplaceItemInObject(d,"conditionCode",cJSON_CreateString("abc"));
        if(i==4)cJSON_ReplaceItemInObject(d,"sunrise",cJSON_CreateString("2026-11-02T22:15:00Z"));
        if(i==5)cJSON_DeleteItemFromObject(d,"sunset");
        if(i==6)cJSON_ReplaceItemInObject(d,"conditionText",cJSON_CreateString("晴\n伪造"));
        if(i==7)cJSON_SetNumberValue(cJSON_GetObjectItem(d,"dayHumidityPct"),1.5);
        assert(!parse(root));cJSON_Delete(root);
    }
    cJSON *root=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON *d=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"days"),0);
    cJSON_ReplaceItemInObject(d,"sunrise",cJSON_CreateNull());cJSON_ReplaceItemInObject(d,"sunset",cJSON_CreateNull());
    assert(parse(root));cJSON_Delete(root);
    bool enabled,available;
    assert(weather_catalog_parse(WEATHER_TEST_CATALOG,strlen(WEATHER_TEST_CATALOG),&enabled,&available)&&enabled&&available);
    assert(!weather_catalog_parse("{\"schemaVersion\":2,\"components\":[]}",35,&enabled,&available));
    root=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON_ReplaceItemInObject(root,"fetchedAt",cJSON_CreateString("2026-11-02T15:30:00Z"));
    cJSON_ReplaceItemInObject(root,"freshUntil",cJSON_CreateString("2026-11-02T16:30:00Z"));
    cJSON_ReplaceItemInObject(root,"serveUntil",cJSON_CreateString("2026-11-02T21:30:00Z"));
    char *json=cJSON_PrintUnformatted(root);now=weather_timestamp("2026-11-02T16:10:00Z");
    assert(weather_forecast_parse(json,strlen(json),now,&city,&f));
    assert(weather_day_for(&f,now,false)->humidity==82&&!weather_day_for(&f,now,true));
    free(json);cJSON_Delete(root);
    assert(weather_scene(999)==WEATHER_SCENE_NEUTRAL&&weather_scene(100)==WEATHER_SCENE_SUN);
    assert(weather_scene(302)==WEATHER_SCENE_STORM&&weather_scene(400)==WEATHER_SCENE_SNOW);
    assert(weather_url_encode("杭州 &?",text,sizeof(text))&&!strcmp(text,"%E6%9D%AD%E5%B7%9E%20%26%3F"));
    assert(!weather_url_encode("杭州",text,5)&&!text[0]);
    assert(weather_query_valid("杭州")&&weather_query_valid("hangzhou")&&!weather_query_valid("杭")&&!weather_query_valid("\xff\xff"));
    char nested[100];memset(nested,'[',40);memset(nested+40,']',40);nested[80]=0;
    weather_error_code(nested,80,text,sizeof(text));assert(!text[0]);
    f.stale=255;assert(!weather_forecast_valid(&f,now));
    city.timezone[0]=0;assert(!weather_location_valid(&city));
    puts("PASS: weather strict protocol, dates, solar UTC conversion, ranges, stale/expiry, unknown scenes, UTF-8 query");
}
