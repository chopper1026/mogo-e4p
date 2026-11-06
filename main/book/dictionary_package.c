/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：固定文件名与同目录交还防止半包启用和任意路径写入。
 * English: Fixed filenames and directory handoff prevent partial activation and arbitrary writes. */
#include "dictionary_package.h"
#include "cJSON.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const names[DICTIONARY_PACKAGE_FILES] = {
    "chinese.LICENSE.txt", "chinese.manifest.json", "chinese.mgdict", "licenses/CC-BY-SA-4.0.txt",
    "licenses/OpenCC-Apache-2.0.txt", "licenses/OpenCC-NOTICE.txt", "licenses/Unicode-3.0.txt", "licenses/mapull-MIT.txt"
};
static int hex(unsigned char c) { return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1; }
static bool digest(const char *text, uint8_t *value) {
    if (!text || strlen(text)!=64) return false;
    for (unsigned i=0;i<32;++i) {
        int a=hex((unsigned char)text[i*2]),b=hex((unsigned char)text[i*2+1]);
        if (a<0 || b<0) return false;
        value[i]=(uint8_t)(a*16+b);
    }
    return true;
}
static bool integer(cJSON *value, double min, double max) {
    return cJSON_IsNumber(value) && value->valuedouble>=min && value->valuedouble<=max &&
        value->valuedouble==(uint32_t)value->valuedouble;
}
bool dictionary_package_parse(const char *text, dictionary_package_t *package) {
    if (!text || !package || strnlen(text,8193)>8192) return false;
    memset(package,0,sizeof(*package));
    const char *end=NULL; cJSON *json=cJSON_ParseWithOpts(text,&end,true);
    if (!json) return false;
    cJSON *schema=cJSON_GetObjectItemCaseSensitive(json,"schemaVersion"), *format=cJSON_GetObjectItemCaseSensitive(json,"format"),
        *id=cJSON_GetObjectItemCaseSensitive(json,"packageId"), *files=cJSON_GetObjectItemCaseSensitive(json,"files");
    uint8_t id_bytes[32]; bool ok=integer(schema,1,1) && integer(format,1,1) && cJSON_IsString(id) &&
        digest(id->valuestring,id_bytes) && cJSON_IsArray(files) && cJSON_GetArraySize(files)==DICTIONARY_PACKAGE_FILES;
    bool found[DICTIONARY_PACKAGE_FILES]={0};
    if (ok) snprintf(package->id,sizeof(package->id),"%s",id->valuestring);
    for (int i=0;ok && i<DICTIONARY_PACKAGE_FILES;++i) {
        cJSON *item=cJSON_GetArrayItem(files,i), *name=cJSON_GetObjectItemCaseSensitive(item,"name"),
            *url=cJSON_GetObjectItemCaseSensitive(item,"url"), *size=cJSON_GetObjectItemCaseSensitive(item,"size"),
            *sha=cJSON_GetObjectItemCaseSensitive(item,"sha256");
        int slot=-1;
        if (cJSON_IsString(name)) for(int j=0;j<DICTIONARY_PACKAGE_FILES;++j) if(!strcmp(name->valuestring,names[j]))slot=j;
        if (slot<0 || found[slot] || !cJSON_IsString(url) || !cJSON_IsString(sha)) { ok=false; break; }
        dictionary_package_file_t *file=&package->files[slot];
        uint32_t cap=slot==2 ? 128u*1024*1024 : 256u*1024;
        char expected[208]; snprintf(expected,sizeof(expected),"/api/v2/components/dictionary/packages/%s/files/%s",package->id,names[slot]);
        if (!integer(size,slot==2 ? 64 : 1,cap) || strcmp(expected,url->valuestring) || !digest(sha->valuestring,file->sha256)) { ok=false; break; }
        snprintf(file->name,sizeof(file->name),"%s",names[slot]);
        snprintf(file->url,sizeof(file->url),"%s%s",DICTIONARY_PACKAGE_ORIGIN,expected);
        file->size=(uint32_t)size->valuedouble; package->size+=file->size; found[slot]=true;
    }
    cJSON_Delete(json); return ok;
}
static bool path_join(char *path,size_t cap,const char *root,const char *name) {
    int n=snprintf(path,cap,"%s/%s",root,name); return n>0 && (size_t)n<cap;
}
static bool directory(const char *path) { struct stat st; return !stat(path,&st) && S_ISDIR(st.st_mode); }
static bool mkdir_ok(const char *path) { return !mkdir(path,0775) || (errno==EEXIST && directory(path)); }
static void clear_stage(const char *root) {
    char path[256];
    for(int i=0;i<DICTIONARY_PACKAGE_FILES;++i) if(path_join(path,sizeof(path),root,names[i])) unlink(path);
    if(path_join(path,sizeof(path),root,"licenses")) rmdir(path);
    rmdir(root);
}
bool dictionary_package_path(const char *root,char *path,size_t cap) {
    const char *candidates[]={"installed/chinese.mgdict",".previous/chinese.mgdict","chinese.mgdict"};
    struct stat st;
    for(unsigned i=0;i<3;++i) if(path_join(path,cap,root,candidates[i]) && !stat(path,&st) && S_ISREG(st.st_mode)) return true;
    return path_join(path,cap,root,"chinese.mgdict");
}
bool dictionary_package_install(const char *root,const dictionary_package_t *package,
    dictionary_package_fetch_fn fetch,book_dictionary_cancel_fn cancelled,void *context) {
    char stage[192],active[192],previous[192],path[256];
    if(!root || !package || !fetch || !path_join(stage,sizeof(stage),root,".install") ||
        !path_join(active,sizeof(active),root,"installed") || !path_join(previous,sizeof(previous),root,".previous") || !mkdir_ok(root)) return false;
    if(!directory(active) && directory(previous) && rename(previous,active)) return false;
    clear_stage(stage);
    if(!mkdir_ok(stage) || !path_join(path,sizeof(path),stage,"licenses") || !mkdir_ok(path)) return false;
    bool ok=true;
    for(int i=0;ok && i<DICTIONARY_PACKAGE_FILES;++i) {
        struct stat st;
        ok=!(cancelled && cancelled(context)) && !strcmp(package->files[i].name,names[i]) &&
            path_join(path,sizeof(path),stage,names[i]) && fetch(&package->files[i],path,context) &&
            !stat(path,&st) && st.st_size==package->files[i].size;
    }
    book_dictionary_result_t *result=NULL;
    if(ok) {
        result=calloc(1,sizeof(*result));
        ok=result && path_join(path,sizeof(path),stage,"chinese.mgdict");
        if(ok) {
            book_dictionary_status_t status=book_dictionary_lookup(path,"的",result,cancelled,context);
            ok=status==BOOK_DICTIONARY_OK || status==BOOK_DICTIONARY_NOT_FOUND;
        }
    }
    free(result);
    if(ok && !(cancelled && cancelled(context))) {
        clear_stage(previous);
        bool had=directory(active);
        ok=(!had || !rename(active,previous)) && !rename(stage,active);
        if(!ok && had && !directory(active)) (void)rename(previous,active);
    } else ok=false;
    if(!ok) clear_stage(stage);
    return ok;
}
