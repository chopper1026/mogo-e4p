#!/usr/bin/env python3
"""Exercise real sampling, recency, focus, media loss and UTF-8 boundaries."""
from pathlib import Path
import subprocess
import tempfile
from run_reader_baseline import toolchain
ROOT=Path(__file__).resolve().parents[1]
UNIT=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_mini_library.h"
static uint32_t rng=7;
static uint32_t next(void *ctx){(void)ctx;rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static book_shelf_entry_t entries[10000];
static book_mini_library_t model;
static void seed(unsigned n){memset(entries,0,sizeof(entries));for(unsigned i=0;i<n;++i){
 snprintf(entries[i].path,sizeof(entries[i].path),"/%s/books/%05u.epub",i%2?"sdcard":"flash",i);
 snprintf(entries[i].name,sizeof(entries[i].name),"书名%u",i);strcpy(entries[i].author,"作者");
 entries[i].size=10+i;entries[i].has_progress=(i%3)==0;entries[i].recent=i;entries[i].favorite=i==0;
 entries[i].pct=31;
}}
static void update(unsigned n,bool shuffle){book_mini_library_update(&model,entries,n,shuffle,next,NULL);}
static void unique(void){for(unsigned i=0;i<model.random_count;++i)for(unsigned j=i+1;j<model.random_count;++j)assert(strcmp(model.random[i].path,model.random[j].path));}
int main(void){
 unsigned sizes[]={0,1,3,5,6,9,10,12,10000};
 for(unsigned s=0;s<sizeof(sizes)/sizeof(*sizes);++s){unsigned n=sizes[s];seed(n);memset(&model,0,sizeof(model));update(n,false);
  assert(model.random_count==(n<5?n:5));unsigned reads=(n+2)/3;assert(model.recent_count==(reads<5?reads:5));unique();
  for(unsigned i=1;i<model.recent_count;++i)assert(model.recent[i-1].recent>model.recent[i].recent);
  book_mini_item_t old[5];memcpy(old,model.random,sizeof(old));
  if(n){model.random_selected=model.random_count-1;char focus[288];strcpy(focus,model.random[model.random_selected].path);update(n,false);assert(!strcmp(model.random[model.random_selected].path,focus));}
  update(n,true);unique();unsigned repeats=0;for(unsigned i=0;i<model.random_count;++i)for(unsigned j=0;j<(n<5?n:5);++j)repeats+=!strcmp(old[j].path,model.random[i].path);
  unsigned expected=n<5?n:n<10?10-n:0;assert(repeats==expected);
 }
 seed(12);memset(&model,0,sizeof(model));update(12,false);
 char focus[288];strcpy(focus,model.random[2].path);model.random_selected=2;
 for(unsigned i=0;i<12;++i)if(!strcmp(entries[i].path,model.random[0].path))entries[i].removed=true;
 update(12,false);assert(model.random_count==5&&!strcmp(model.random[model.random_selected].path,focus));unique();
 // Source metadata refreshes surviving random entries, while recent ignores favorites/unread.
 for(unsigned i=0;i<12;++i){entries[i].has_progress=false;entries[i].recent=9999;strcpy(entries[i].author,"新作者");}
 update(12,false);assert(!model.recent_count);for(unsigned i=0;i<5;++i)assert(!strcmp(model.random[i].author,"新作者"));
 seed(5);memset(&model,0,sizeof(model));for(unsigned i=0;i<5;++i){entries[i].has_progress=true;entries[i].recent=7;}
 update(5,false);for(unsigned i=1;i<5;++i)assert(strcmp(model.recent[i-1].path,model.recent[i].path)<0);
 book_mini_library_drop_card(&model);assert(model.random_count==3&&model.recent_count==3);
 for(unsigned i=0;i<3;++i)assert(!strncmp(model.random[i].path,"/flash/",7));
 seed(1);for(unsigned i=0;i<84;++i)memcpy(entries[0].name+i*3,"书",3);entries[0].name[252]=0;entries[0].pct=255;
 memset(&model,0,sizeof(model));update(1,false);assert(strlen(model.random[0].title)==126&&model.random[0].pct==100);
 puts("PASS: 0/1/3/5/6/9/10/12/10000 books, unique sampling, maximal batch novelty, stable survivors/focus, true recency/ties, media loss, metadata and UTF-8");
}
'''
with tempfile.TemporaryDirectory(prefix='mini-library-model-') as d:
 w=Path(d);env,cc=toolchain(w);(w/'test.c').write_text(UNIT)
 subprocess.run(cc+['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Imain/book',str(w/'test.c'),'main/book/book_mini_library.c','-o',str(w/'test')],cwd=ROOT,env=env,check=True)
 subprocess.run([str(w/'test')],env=env,check=True)
