#!/usr/bin/env python3
"""Inject every allocation failure into the production directory JSON response."""
from pathlib import Path
import subprocess
import tempfile

from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]


def main():
    production = (ROOT / "components/wifi_transfer/wifi_transfer.c").read_text()
    send_json = production[production.index("static esp_err_t send_json("):
                           production.index("static esp_err_t file_response(")]
    listing = production[production.index("#define TRANSFER_FILE_PAGE_SIZE"):
                         production.index("static int copy_sd_file(")]
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
typedef int esp_err_t;
typedef struct { int unused; } httpd_req_t;
enum { ESP_OK, ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND };
enum { MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2 };
static struct { bool is_flash; } s_cfg;
static char root[256];
static size_t allocations, fail_at, live;
static unsigned responses;
static void *allocate(size_t size) {
    if (++allocations == fail_at) return NULL;
    void *memory = malloc(size);
    assert(memory); ++live; return memory;
}
static void deallocate(void *memory) {
    if (!memory) return;
    assert(live); --live; free(memory);
}
static void *heap_caps_calloc(size_t count, size_t size, int caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    void *memory = allocate(count * size);
    if (memory) memset(memory, 0, count * size);
    return memory;
}
static void heap_caps_free(void *memory) { deallocate(memory); }
static bool management_request(httpd_req_t *req) { (void)req; return true; }
static size_t httpd_req_get_url_query_len(httpd_req_t *req) { (void)req; return 0; }
static esp_err_t httpd_req_get_url_query_str(httpd_req_t *req, char *out, size_t size) {
    (void)req; (void)out; (void)size; return ESP_ERR_NOT_FOUND;
}
static esp_err_t httpd_query_key_value(const char *query, const char *key, char *out, size_t size) {
    (void)query; (void)key; (void)out; (void)size; return ESP_ERR_NOT_FOUND;
}
static bool sd_decode_relative(const char *input, char *out, bool empty) {
    (void)input; (void)out; (void)empty; return false;
}
static bool sd_absolute(const char *relative, char *path, size_t size) {
    assert(!relative[0]);
    return snprintf(path, size, "%s", root) < (int)size;
}
static esp_err_t files_error(httpd_req_t *req, const char *status, const char *message) {
    (void)req; (void)message; assert(!strcmp(status, "500 Internal Server Error"));
    return ESP_ERR_NO_MEM;
}
static esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) {
    (void)req; assert(!strcmp(type, "application/json; charset=utf-8")); return ESP_OK;
}
static esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *value) {
    (void)req; assert(!strcmp(key, "Cache-Control") && !strcmp(value, "no-store")); return ESP_OK;
}
static esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body) {
    (void)req;
    assert(strstr(body, "\"total\":2") && strstr(body, "\"pages\":1"));
    assert(strstr(body, "\"name\":\"book.txt\"") && strstr(body, "\"name\":\"folder\""));
    ++responses; return ESP_OK;
}
'''
    harness += send_json + listing
    harness += r'''
int main(int argc, char **argv) {
    char path[512];
    assert(argc == 2 && strlen(argv[1]) < sizeof(root));
    snprintf(root, sizeof(root), "%s", argv[1]); assert(!mkdir(root, 0700));
    snprintf(path, sizeof(path), "%s/folder", root); assert(!mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/book.txt", root);
    FILE *book = fopen(path, "wb"); assert(book); assert(!fclose(book));
    cJSON_Hooks hooks = { .malloc_fn = allocate, .free_fn = deallocate };
    cJSON_InitHooks(&hooks);
    httpd_req_t req = {0};
    assert(files_list_handler(&req) == ESP_OK && responses == 1 && !live);
    size_t count = allocations;
    for (fail_at = 1; fail_at <= count; ++fail_at) {
        allocations = responses = 0;
        assert(files_list_handler(&req) == ESP_ERR_NO_MEM);
        assert(!live && !responses);
    }
    fail_at = allocations = responses = 0;
    assert(files_list_handler(&req) == ESP_OK && responses == 1 && !live);
    cJSON_InitHooks(NULL);
    assert(!unlink(path));
    snprintf(path, sizeof(path), "%s/folder", root); assert(!rmdir(path));
    assert(!rmdir(root));
    printf("PASS: real directory listing releases all allocations at %zu failure points and recovers\n", count);
}
'''
    with tempfile.TemporaryDirectory(prefix="transfer-memory-host-") as temporary:
        work = Path(temporary)
        env, compiler = toolchain(work)
        source, binary = work / "test.c", work / "test"
        source.write_text(harness)
        cjson = ROOT / "managed_components/espressif__cjson/cJSON"
        subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-deprecated-declarations", "-g", "-fsanitize=address,undefined",
                       "-I" + str(cjson), str(source), str(cjson / "cJSON.c"),
                       "-o", str(binary)], env=env, check=True)
        subprocess.run([str(binary), str(work / "files")], env=env, check=True)


if __name__ == "__main__":
    main()
