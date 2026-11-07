/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：真实包读取器的成功、取消与破损边界。
 * English: Actual package reader success, cancellation and malformed boundaries. */
#include "book_dictionary.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool cancel(void *context){unsigned *n=context;return ++*n>2;}
int main(int argc,char **argv){
    assert(argc>=3);book_dictionary_result_t r;
    if(!strcmp(argv[1],"paths")) {
        char path[1024], manual[1024], installed[1024], previous[1024];
        snprintf(manual,sizeof(manual),"%s/chinese.mgdict",argv[2]);
        snprintf(installed,sizeof(installed),"%s/installed/chinese.mgdict",argv[2]);
        snprintf(previous,sizeof(previous),"%s/.previous/chinese.mgdict",argv[2]);
        assert(book_dictionary_path(argv[2],path,sizeof(path))&&!strcmp(path,manual));
        char directory[1024];snprintf(directory,sizeof(directory),"%s/.previous",argv[2]);assert(!mkdir(directory,0700));
        FILE *f=fopen(previous,"wb");assert(f);fclose(f);
        assert(book_dictionary_path(argv[2],path,sizeof(path))&&!strcmp(path,previous));
        snprintf(directory,sizeof(directory),"%s/installed",argv[2]);assert(!mkdir(directory,0700));
        f=fopen(installed,"wb");assert(f);fclose(f);
        assert(book_dictionary_path(argv[2],path,sizeof(path))&&!strcmp(path,installed));
        f=fopen(manual,"wb");assert(f);fclose(f);
        assert(book_dictionary_path(argv[2],path,sizeof(path))&&!strcmp(path,manual));
        assert(!book_dictionary_path(argv[2],path,2));assert(!book_dictionary_path(NULL,path,sizeof(path)));
        puts("PASS: manual file precedence, both legacy fallbacks, missing file and bounded path");return 0;
    }
    if(!strcmp(argv[1],"bad")) {assert(book_dictionary_lookup(argv[2],"踽",&r,NULL,NULL)!=BOOK_DICTIONARY_OK);return 0;}
    if(!strcmp(argv[1],"feedback")){
        const char *keys[]={"青铜","青銅","破解","踽","翩跹","蹉跎","伛偻","觊觎"};
        for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){
            assert(book_dictionary_lookup(argv[2],keys[i],&r,NULL,NULL)==BOOK_DICTIONARY_OK);
            assert(r.reading[0]&&r.definition[0]&&!strstr(r.definition,"未提供中文释义"));
            if(i<2){assert(strstr(r.definition,"合金")&&strstr(r.definition,"铜"));assert(!strstr(r.definition,"銅"));}
            if(i==2)assert(strstr(r.definition,"解开")&&strstr(r.definition,"难题"));
            printf("%s: %s | %s\n",keys[i],r.reading,r.definition);
        }
        puts("PASS: feedback words and previously empty entries through the actual device parser");return 0;
    }
    if(!strcmp(argv[1],"real")){
        const char *keys[]={"踽","踽踽独行","觊觎","龃龉","龘","鬱","𠮷"};
        for(unsigned i=0;i<7;++i){book_dictionary_status_t status=book_dictionary_lookup(argv[2],keys[i],&r,NULL,NULL);printf("%s: %d %s | %.240s\n",keys[i],status,r.reading,r.definition);assert(status==BOOK_DICTIONARY_OK||status==BOOK_DICTIONARY_NOT_FOUND);}return 0;
    }
    assert(book_dictionary_lookup(argv[2],"踽",&r,NULL,NULL)==BOOK_DICTIONARY_OK);
    assert(!strcmp(r.reading,"jǔ")&&!strcmp(r.definition,"原创测试释义"));
    assert(book_dictionary_lookup(argv[2],"𠮷",&r,NULL,NULL)==BOOK_DICTIONARY_OK&&!r.reading[0]);
    assert(book_dictionary_lookup(argv[2],"未收录",&r,NULL,NULL)==BOOK_DICTIONARY_NOT_FOUND);
    assert(book_dictionary_lookup(argv[2],"\xc0\xaf",&r,NULL,NULL)==BOOK_DICTIONARY_CORRUPT);
    unsigned n=0;assert(book_dictionary_lookup(argv[2],"踽",&r,cancel,&n)==BOOK_DICTIONARY_CANCELLED);
    assert(book_dictionary_lookup("/not/a/dictionary","踽",&r,NULL,NULL)==BOOK_DICTIONARY_MISSING);
    puts("PASS: real compressed package, exact/extension keys, absent fields, strict UTF-8, cancellation and missing package");
}
