/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#define FF_FS_REENTRANT 1
#define FR_OK 0
#define AM_HID 2
#define AM_SYS 4
typedef struct { uint8_t fattrib; } FILINFO;
static uint8_t mock_fat_attributes;
static inline int f_stat(const char *path, FILINFO *info) { (void)path; info->fattrib=mock_fat_attributes; return FR_OK; }
typedef uint8_t BYTE;
typedef struct {uint32_t free_clst; BYTE ldrv;} FATFS;
typedef struct {struct {FATFS *fs;}obj;} FF_DIR;
int f_opendir(FF_DIR *root,const char *path);
int f_closedir(FF_DIR *root);
int ff_mutex_take(int drive);
void ff_mutex_give(int drive);
