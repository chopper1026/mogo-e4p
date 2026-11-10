#!/usr/bin/env python3
"""Exercise the real Home library service with cancellable IO and owned cover doubles."""
from pathlib import Path
import re
import subprocess
import tempfile
from run_reader_baseline import toolchain
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "home_library.h"
#include "book_shelf_scan.h"
#include "book_cover_jobs.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct {bool present,mounted;} board_sd_info_t;
static board_sd_info_t sd={true,true};
static unsigned store_revision=1,progress_revision=1,scans,submits,takes;
static bool recent_mode,save_ok=true,flash_ready=true,fast_mode,asset_allowed=true;
static bool fail_alloc,fail_catalog,fail_read,fail_open,probe_pending,cancelled,queue_pending;
static unsigned contrast=100;
static size_t largest=4*1024*1024;
static const char *root="/sdcard/books";
static int board_sd_get_info(board_sd_info_t *out){*out=sd;return probe_pending?ESP_ERR_NOT_FINISHED:ESP_OK;}
static unsigned book_store_revision(void){return store_revision;}
static unsigned book_progress_revision(void){return progress_revision;}
static bool book_store_flash_ready(void){return flash_ready;}
static const char *app_settings_books_dir(void){return root;}
static bool app_settings_main_fast_refresh(void){return fast_mode;}
static unsigned app_settings_system_contrast(void){return contrast;}
static bool app_home_library_recent(void){return recent_mode;}
static bool app_home_library_set_recent(bool value){if(!save_ok)return false;recent_mode=value;return true;}
static bool pico_boot_asset_allowed(const char *path){(void)path;return asset_allowed;}
static size_t heap_caps_get_largest_free_block(int caps){assert(caps==1);return largest;}
static void *heap_caps_calloc(size_t n,size_t size,int caps){assert(caps==3);return fail_alloc?NULL:calloc(n,size);}
static uint32_t esp_random(void){static uint32_t state=1026;state^=state<<13;state^=state>>17;state^=state<<5;return state;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"rp_shelf")&&mode==NVS_READONLY);*h=fail_open?0:42;return fail_open?99:ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==42);}
bool book_shelf_hidden_read(nvs_handle_t h,const char *path){assert(h==42);(void)path;return false;}
static book_shelf_entry_t catalog[12];
static time_t file_modified[12];
static unsigned missing_files;
static int path_index(const char *path){const char *leaf=strrchr(path,'/');return leaf?atoi(leaf+1):-1;}
static int cover_stat(const char *path,struct stat *st){
 if(!strstr(path,".epub")||(!sd.mounted&&!strncmp(path,"/sdcard/",8)))return -1;
 int i=path_index(path);assert(i>=0&&i<12);if(missing_files&(1u<<i))return -1;
 *st=(struct stat){.st_mode=S_IFREG,.st_size=catalog[i].size,.st_mtime=file_modified[i]};return 0;
}
#define stat(...) cover_stat(__VA_ARGS__)
void book_shelf_scan_dir(const book_shelf_scan_t *q,const char *path,bool is_flash,nvs_handle_t fav,nvs_handle_t hidden,bool *truncated,bool *unreadable,bool *skipped){
 (void)is_flash;(void)fav;(void)skipped;assert(hidden==42);assert(q->category_add(path)==0&&!q->favorite_read(10,path));
 ++scans;if(q->cancel&&q->cancel(q->cancel_ctx))return;
 if(fail_catalog){*truncated=true;return;}if(fail_read){*unreadable=true;return;}
 if(strcmp(path,"/flash/books"))return;
 *q->catalog.entries=malloc(sizeof(catalog));assert(*q->catalog.entries);memcpy(*q->catalog.entries,catalog,sizeof(catalog));*q->catalog.count=12;*q->catalog.capacity=12;
}
bool book_shelf_scan_backfill(const book_shelf_scan_t *q,bool *truncated){(void)truncated;assert(!q->favorite_read(9,"/flash/old.epub"));return false;}
static book_cover_job_t jobs[9];
static unsigned current_generation;
bool book_cover_jobs_submit(unsigned gen,unsigned slot,const book_cover_job_t *q){assert(gen==current_generation&&slot>=1&&slot<=5);jobs[slot]=*q;++submits;return true;}
int book_cover_jobs_take(unsigned gen,unsigned slot,book_cover_result_t *out){assert(gen==current_generation&&slot>=1&&slot<=5);++takes;if(queue_pending)return 0;
 const book_cover_job_t *q=&jobs[slot];unsigned stride=q->fast?22:176;
 uint8_t *pixels=malloc(stride*240);assert(pixels);memset(pixels,q->fast?0xaa:128,stride*240);
 int i=path_index(q->path);
 *out=(book_cover_result_t){.pixels=pixels,.width=176,.height=240,.fast=q->fast,.size=catalog[i].size,.modified=file_modified[i]};return 1;
}
static bool cancel(void *ctx){assert(ctx==&cancelled);return cancelled;}
'''
BODY=r'''
static void generation(void){home_library_cover_generation(++current_generation);}
static void seed(void){for(unsigned i=0;i<12;++i){snprintf(catalog[i].path,sizeof(catalog[i].path),"/%s/books/%u.epub",i%2?"sdcard":"flash",i);snprintf(catalog[i].name,sizeof(catalog[i].name),"书%u",i);catalog[i].has_progress=i<4;catalog[i].recent=i;catalog[i].size=100;file_modified[i]=1000+i;}}
static void refresh(void){home_library_enter();assert(home_library_view().loading);assert(home_library_poll(cancel,&cancelled));assert(!home_library_view().loading);}
static void load_all(void){generation();for(unsigned i=0;i<5;++i)(void)home_library_poll(cancel,&cancelled);assert(!home_library_poll(cancel,&cancelled));}
static void cached_files(void){
 unsigned old_scans=scans,old_submits=submits;const uint8_t *pixels[5];
 for(unsigned i=0;i<5;++i)pixels[i]=home_library_view().covers[i].pixels;
 // Navigation and reader round trips retain artwork, with zero re-submission.
 for(unsigned trip=0;trip<80;++trip){home_library_leave();home_library_enter();generation();assert(!home_library_poll(cancel,&cancelled));
  for(unsigned i=0;i<5;++i)assert(home_library_view().covers[i].pixels==pixels[i]);}
 assert(scans==old_scans&&submits==old_submits);
 ++progress_revision;catalog[path_index(home_library_view().books[0].path)].pct=79;
 home_library_leave();refresh();generation();assert(!home_library_poll(cancel,&cancelled));
 for(unsigned i=0;i<5;++i)assert(home_library_view().covers[i].pixels==pixels[i]);assert(submits==old_submits);
 // Same-size replacement and changed metadata invalidate only that book.
 unsigned id=path_index(home_library_view().books[2].path);++file_modified[id];
 home_library_leave();home_library_enter();assert(!home_library_view().covers[2].pixels);
 for(unsigned i=0;i<5;++i)if(i!=2)assert(home_library_view().covers[i].pixels==pixels[i]);
 load_all();assert(submits==old_submits+1);pixels[2]=home_library_view().covers[2].pixels;
 strcpy(catalog[id].author,"新版作者");++store_revision;home_library_leave();refresh();assert(!home_library_view().covers[2].pixels);
 for(unsigned i=0;i<5;++i)if(i!=2)assert(home_library_view().covers[i].pixels==pixels[i]);load_all();
 // Size changes and missing files also discard stale art on entry.
 ++catalog[id].size;++store_revision;home_library_leave();refresh();assert(!home_library_view().covers[2].pixels);load_all();
 missing_files=1u<<id;home_library_leave();home_library_enter();assert(!home_library_view().covers[2].pixels);missing_files=0;load_all();
}
int main(void){
 seed();assert(!home_library_poll(cancel,&cancelled)&&!scans);
 home_library_enter();probe_pending=true;assert(!home_library_poll(cancel,&cancelled)&&!scans);probe_pending=false;
 cancelled=true;assert(!home_library_poll(cancel,&cancelled));assert(home_library_view().loading&&!home_library_view().count);cancelled=false;
 assert(home_library_poll(cancel,&cancelled));assert(home_library_view().count==5&&home_library_needs_generation());
 home_library_select(3);char focus[288];strcpy(focus,home_library_view().books[3].path);home_library_select(99);assert(home_library_view().selected==3);
 generation();queue_pending=true;assert(!home_library_poll(cancel,&cancelled));queue_pending=false;
 for(unsigned i=0;i<5;++i)assert(home_library_poll(cancel,&cancelled));assert(!home_library_poll(cancel,&cancelled));
 for(unsigned i=0;i<5;++i)assert(home_library_view().covers[i].pixels&&home_library_view().covers[i].width<=176&&home_library_view().covers[i].height<=240);
 cached_files();
 unsigned old_scans=scans;home_library_leave();assert(!home_library_poll(cancel,&cancelled));home_library_enter();assert(!home_library_view().loading&&scans==old_scans&&!strcmp(home_library_view().books[home_library_view().selected].path,focus));
 save_ok=false;assert(!home_library_recent(true)&&!home_library_view().recent&&*home_library_view().notice);save_ok=true;
 assert(home_library_recent(true)&&home_library_view().count==4&&home_library_view().books[0].recent==3&&home_library_needs_generation());
 load_all();const uint8_t *recent_pixels[4];for(unsigned i=0;i<4;++i)recent_pixels[i]=home_library_view().covers[i].pixels;
 unsigned old_submits=submits;catalog[0].recent=99;++progress_revision;home_library_leave();refresh();
 assert(home_library_view().books[0].recent==99&&home_library_view().covers[0].pixels==recent_pixels[3]);
 for(unsigned i=1;i<4;++i)assert(home_library_view().covers[i].pixels==recent_pixels[i-1]);
 generation();assert(!home_library_poll(cancel,&cancelled)&&submits==old_submits);
 assert(home_library_recent(false)&&!strcmp(home_library_view().books[home_library_view().selected].path,focus));
 // A failed/truncated scan retains the entire last-good batch, never commits a biased subset.
 book_mini_item_t old[5];memcpy(old,home_library_view().books,sizeof(old));
 fail_catalog=true;home_library_invalidate();assert(home_library_poll(cancel,&cancelled));assert(*home_library_view().notice&&!memcmp(old,home_library_view().books,sizeof(old)));fail_catalog=false;
 fail_read=true;refresh();assert(*home_library_view().notice&&!memcmp(old,home_library_view().books,sizeof(old)));fail_read=false;
 fail_open=true;refresh();assert(*home_library_view().notice&&!memcmp(old,home_library_view().books,sizeof(old)));fail_open=false;refresh();
 home_library_shuffle();assert(home_library_poll(cancel,&cancelled));for(unsigned i=0;i<5;++i)for(unsigned j=0;j<5;++j)assert(strcmp(old[i].path,home_library_view().books[j].path));
 generation();fail_alloc=true;assert(!home_library_poll(cancel,&cancelled));fail_alloc=false;
 for(unsigned i=0;i<5;++i)assert(!home_library_view().covers[i].pixels);
 home_library_cover_mode_changed();generation();largest=1;assert(!home_library_poll(cancel,&cancelled));largest=4*1024*1024;
 fast_mode=true;assert(home_library_poll(cancel,&cancelled)&&home_library_needs_generation());generation();assert(home_library_poll(cancel,&cancelled));
 unsigned selected=home_library_view().selected;assert(home_library_view().covers[selected].fast);
 // Unmounted media disappear immediately and cannot be backfilled from stale history.
 sd=(board_sd_info_t){0};home_library_media_lost();for(unsigned i=0;i<home_library_view().count;++i)assert(strncmp(home_library_view().books[i].path,"/sdcard/",8));
 assert(home_library_poll(cancel,&cancelled));for(unsigned i=0;i<home_library_view().count;++i)assert(strncmp(home_library_view().books[i].path,"/sdcard/",8));
 home_library_leave();assert(!home_library_poll(cancel,&cancelled));
 free_covers();
 puts("PASS: service cancellation, 80 cached page/reader returns without cover jobs, progress/recent reordering reuse, per-file invalidation, mode rollback, partial/OOM rollback, novel shuffle, cover ownership/bounds, fast format and media loss");
}
'''
with tempfile.TemporaryDirectory(prefix='home-library-service-') as d:
 w=Path(d);env,cc=toolchain(w)
 (w/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NOT_FINISHED 7\n#define ESP_ERR_NVS_NOT_FOUND 8\n')
 (w/'nvs.h').write_text('#pragma once\n#include "esp_err.h"\ntypedef int nvs_handle_t;\n#define NVS_READONLY 0\nesp_err_t nvs_open(const char *,int,nvs_handle_t *);\nvoid nvs_close(nvs_handle_t);\n')
 src=re.sub(r'^#include[^\n]*\n','',(ROOT/'main/services/home_library.c').read_text(),flags=re.M)
 (w/'test.c').write_text(PRE+src+BODY)
 subprocess.run(cc+['-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(w),'-Imain/book','-Imain/services',str(w/'test.c'),'main/book/book_mini_library.c','-o',str(w/'test')],cwd=ROOT,env=env,check=True)
 subprocess.run([str(w/'test')],env=env,check=True)
