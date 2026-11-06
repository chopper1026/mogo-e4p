/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：有界安装清单与临时目录提交；网络由调用方提供。
 * English: Bounded package manifests and staged installation; callers own networking. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "book_dictionary.h"
#define DICTIONARY_PACKAGE_FILES 8
#define DICTIONARY_PACKAGE_ORIGIN "https://mogo.mantake.shop"
#define DICTIONARY_PACKAGE_MANIFEST DICTIONARY_PACKAGE_ORIGIN "/api/v2/components/dictionary/manifest"
typedef struct { char name[48], url[240]; uint32_t size; uint8_t sha256[32]; } dictionary_package_file_t;
typedef struct { char id[65]; dictionary_package_file_t files[DICTIONARY_PACKAGE_FILES]; uint32_t size; } dictionary_package_t;
typedef bool (*dictionary_package_fetch_fn)(const dictionary_package_file_t *file, const char *destination, void *context);
bool dictionary_package_parse(const char *json, dictionary_package_t *package);
/// 全部文件下载与校验成功后才切换目录；旧目录保留到下一次安装。
/// Switch only after every verified file is present; retain the previous directory until the next install.
bool dictionary_package_install(const char *root, const dictionary_package_t *package,
    dictionary_package_fetch_fn fetch, book_dictionary_cancel_fn cancelled, void *context);
/// 优先自动安装目录，恢复时可读上一份；手工安装路径仍可用。
/// Prefer the installed directory, fall back to its previous generation or a manual package.
bool dictionary_package_path(const char *root, char *path, size_t capacity);
