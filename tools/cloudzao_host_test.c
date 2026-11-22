/*
 * 中文：真实协议和文件流程，模拟 HTTPS 响应，不使用账号或云端。
 * English: Exercise real framing and file workflows with synthetic HTTPS responses and no account or cloud.
 */
#include "cloudzao_receive.h"
#include "cloudzao_history.h"
#include "cloudzao_partner.h"
#include <sys/stat.h>
#include "cloudzao_wire.h"
#include <assert.h>
#include <errno.h>
#include <psa/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAC "AA:BB:CC:DD:EE:FF"
#define KEY "pat_test.access"
#define BASE "/xiaozhi/api/partner/v1"
#define BIND_PATH BASE "/pairings"
#define LIST_PATH BASE "/deliveries"
#define TARGET "/objects/a%2Fb?sig=x%2B1&expires=12"
#define URL "https://cdn.example.invalid" TARGET
#define OSS_HOST "bucket.oss-cn-hangzhou.aliyuncs.com"
#define OSS_URL "http://" OSS_HOST TARGET
#define EP_SIGNATURE "?Expires=12&Signature=x%2B1%3d&OSSAccessKeyId=fixture"
#define EP_RAW_PATH "/objects/a%2fb/书📖.epub"
#define EP_WIRE_PATH "/objects/a%2fb/%E4%B9%A6%F0%9F%93%96.epub"
#define EP_OSS_URL "http://" OSS_HOST EP_RAW_PATH EP_SIGNATURE
#define DIGEST "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
#define ITEM_URL(kind,format_,mime,name,id,sha,url) \
    "{\"kind\":\"" kind "\",\"format\":\"" format_ "\",\"mediaType\":\"" mime \
    "\",\"name\":\"" name "\",\"deliveryId\":\"" id "\",\"sizeBytes\":3,\"sha256\":\"" sha "\",\"downloadUrl\":\"" url "\"}"
#define ITEM(kind,format_,mime,name,id,sha) ITEM_URL(kind,format_,mime,name,id,sha,URL)
#define TXT(name,id,sha) ITEM("BOOK", "TXT", "text/plain", name, id, sha)
#define LIST(items) "{\"items\":[" items "]}"

typedef struct {
    const char *root, *json, *replacement_json, *expected_ack, *list_reply;
    unsigned downloads, confirmations, books, lists, replacement_on, ack_timeouts;
    cz_error_t interrupt, ack_result, interrupt_during_download;
    uint64_t space;
    bool oversized;
    unsigned queued, finished, stage_mask;
    size_t written;
    uint32_t history_next;
} test_t;

static bool fail_crypto_init;
int psa_crypto_init(void) { return fail_crypto_init ? -1 : PSA_SUCCESS; }

static bool present(const char *root, const char *file) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, file);
    return access(path, F_OK) == 0;
}
static cz_error_t stopped(void *context) { return ((test_t *)context)->interrupt; }
static uint64_t available(void *context) { return ((test_t *)context)->space; }
static void added(void *context) { ++((test_t *)context)->books; }
static void progress(void *context, const cz_status_t *status) {
    (void)context;
    assert(status->busy && status->processed <= status->total && status->pending <= status->saved);
}

static void observe_row(void *context, unsigned index, const cz_row_t *row, bool active) {
    test_t *test = context;
    assert(!row->name[0] || cloudzao_name_valid(row->name));
    if (row->state == CZ_ROW_WAIT) { assert(!active && index == test->queued); ++test->queued; }
    else if (active) { assert(row->state == CZ_ROW_CHECK); test->written = 0; }
    else { assert(index == test->finished); ++test->finished; }
}

static void observe_stage(void *context, cz_row_state_t state, size_t bytes) {
    test_t *test = context;
    test->stage_mask |= 1u << state;
    if (state == CZ_ROW_DOWNLOAD) { assert(bytes >= test->written && bytes <= 3); test->written = bytes; }
}

static void observe_result(void *context, const cz_row_t *row) {
    test_t *test = context;
    const cz_ops_t ops = {.context = test, .root = test->root, .stop = stopped};
    assert(cloudzao_history_record(&ops, &test->history_next, row, 1700000000));
}

static cz_error_t stop_history_clear_after_lookup(void *context) {
    unsigned *checks = context;
    return ++*checks == 1 ? CZ_OK : CZ_MEDIA;
}

static void history_regression(test_t *test) {
    const cz_ops_t ops = {.context = test, .root = test->root, .stop = stopped};
    cz_history_item_t items[CLOUDZAO_HISTORY_ROWS];
    unsigned count = 0;
    assert(!cloudzao_history_read(&ops, items, &count) && count > 0);
    char path[512]; snprintf(path, sizeof(path), "%s/.readpico/cloudzao/history.v1", test->root);
    cz_row_t row = {.size = 3, .state = CZ_ROW_SAVED};
    uint32_t next = 0;
    for (unsigned i = 0; i < 40; ++i) {
        snprintf(row.name, sizeof(row.name), "file-%02u.txt", i);
        assert(cloudzao_history_record(&ops, &next, &row, 1700000000 + i));
    }
    assert(!cloudzao_history_read(&ops, items, &count) && count == CLOUDZAO_HISTORY_ROWS);
    assert(!strcmp(items[0].file.name, "file-39.txt") && items[0].utc == 1700000039);
    assert(!strcmp(items[CLOUDZAO_HISTORY_ROWS-1].file.name, "file-08.txt"));
    struct stat file;
    assert(!stat(path, &file) && file.st_size == 144 * CLOUDZAO_HISTORY_ROWS);
    FILE *f = fopen(path, "r+b"); assert(f);
    assert(!fseek(f, (long)((next-1) % CLOUDZAO_HISTORY_ROWS) * 144, SEEK_SET));
    assert(fputc(0, f) == 0 && !fclose(f));
    assert(!cloudzao_history_read(&ops, items, &count) && count == CLOUDZAO_HISTORY_ROWS - 1);
    assert(!strcmp(items[0].file.name, "file-38.txt"));
    test->interrupt = CZ_CANCELLED;
    assert(!cloudzao_history_record(&ops, &next, &row, 0));
    assert(cloudzao_history_read(&ops, items, &count) == CZ_CANCELLED && !count);
    assert(cloudzao_history_clear(&ops) == CZ_CANCELLED && !stat(path, &file));
    test->interrupt = CZ_OK;
    char sibling[512]; snprintf(sibling, sizeof(sibling), "%s/.readpico/cloudzao/keep.txt", test->root);
    f = fopen(sibling, "wb"); assert(f && fputc('a', f) == 'a' && !fclose(f));
    unsigned checks = 0;
    const cz_ops_t removed = {.root = test->root, .context = &checks, .stop = stop_history_clear_after_lookup};
    assert(cloudzao_history_clear(&removed) == CZ_MEDIA && checks == 2 && !stat(path, &file));
    assert(present(test->root, "books/中文.txt") && present(test->root, ".readpico/cloudzao/keep.txt"));
    assert(cloudzao_history_clear(&ops) == CZ_OK);
    assert(stat(path, &file) && errno == ENOENT);
    assert(cloudzao_history_clear(&ops) == CZ_OK);
    assert(cloudzao_history_read(&ops, items, &count) == CZ_OK && count == 0);
    assert(present(test->root, "books/中文.txt") && present(test->root, ".readpico/cloudzao/keep.txt"));
    assert(!mkdir(path, 0700));
    assert(cloudzao_history_clear(&ops) == CZ_STORAGE);
    assert(!rmdir(path));
    next = 0;
    assert(cloudzao_history_record(&ops, &next, &row, 1700000040));
    assert(cloudzao_history_read(&ops, items, &count) == CZ_OK && count == 1);
    assert(!strcmp(items[0].file.name, row.name) && items[0].utc == 1700000040);
    puts("CloudZao history clear is local, cancellable and idempotent: PASS");
}

static cz_error_t request(void *context, cz_request_t operation, const cz_http_request_t *req,
                          cz_sink_t sink, void *sink_context) {
    test_t *test = context;
    char wire[CLOUDZAO_CHUNK], host[254];
    cz_http_request_t authenticated = *req;
    if (operation != CZ_DOWNLOAD) authenticated.token = KEY;
    assert(cloudzao_wire_request(operation, &authenticated, host, sizeof(host), wire, sizeof(wire)));
    if (operation != CZ_DOWNLOAD) {
        assert(!strcmp(host, "claw.cloudzao.cn") && strstr(wire, "\r\nHost: claw.cloudzao.cn\r\n"));
        assert(strstr(wire, "Authorization: Bearer " KEY "\r\n"));
        assert(!strstr(wire, "X-Resource-Key") && !strstr(wire, "Device-Id") && !req->url && !req->token);
    }
    if (operation == CZ_DOWNLOAD) {
        assert(!req->token && !req->delivery_id && !req->body);
        const bool unicode_oss = !strcmp(req->url, EP_OSS_URL);
        if (unicode_oss || !strcmp(req->url, OSS_URL)) assert(!strcmp(host, OSS_HOST));
        else assert(!strcmp(req->url, URL) && !strcmp(host, "cdn.example.invalid"));
        assert(!strstr(wire, KEY) && !strstr(wire, MAC) && !strstr(wire, "Authorization") && !strstr(wire, "Device-Id") && !strstr(wire, "X-Resource-Key"));
        assert(strstr(wire, unicode_oss ? "GET " EP_WIRE_PATH EP_SIGNATURE " HTTP/1.1" : "GET " TARGET " HTTP/1.1"));
        assert(req->limit == 3);
        ++test->downloads;
        assert(sink(sink_context, "a", 1));
        test->interrupt = test->interrupt_during_download;
        bool accepted = sink(sink_context, "bc", 2);
        return accepted ? CZ_OK : CZ_INVALID;
    }
    if (operation == CZ_ACK) {
        ++test->confirmations;
        char expected[192];
        snprintf(expected, sizeof(expected), "POST " LIST_PATH "/%s/acknowledgements HTTP/1.1", req->delivery_id);
        assert(strstr(wire, expected) && !req->body && !sink);
        if (test->expected_ack) assert(!strcmp(req->delivery_id, test->expected_ack));
        if (!strcmp(req->delivery_id, "book-1")) assert(present(test->root, "books/中文.txt"));
        if (test->ack_timeouts) { --test->ack_timeouts; return CZ_TIMEOUT; }
        return test->ack_result;
    }
    assert(operation == CZ_LIST && strstr(wire, "GET " LIST_PATH " HTTP/1.1") && !req->body);
    ++test->lists;
    if (test->list_reply) {
        cz_response_t response;
        cloudzao_wire_begin(&response, CZ_LIST, req->limit, sink, sink_context);
        const char *p = test->list_reply;
        for (; *p; ++p) if (!cloudzao_wire_feed(&response, p, 1)) return response.error;
        return response.done ? response.error : CZ_INVALID;
    }
    if (test->oversized) {
        static char big[CLOUDZAO_JSON_MAX + 1];
        return sink(sink_context, big, sizeof(big)) ? CZ_OK : CZ_INVALID;
    }
    const char *body = test->replacement_json && test->lists >= test->replacement_on ? test->replacement_json : test->json;
    return sink(sink_context, body, strlen(body)) ? CZ_OK : CZ_INVALID;
}

static int64_t now_fixed(void *context) { (void)context; return 1794787500; }

static cz_queue_t test_queue;
static cz_status_t run_job(test_t *test, cz_job_t job, const char *json) {
    test->json = json;
    test->queued = test->finished = test->stage_mask = 0;
    static char buffer[CLOUDZAO_JSON_MAX + 1];
    static unsigned char chunk[CLOUDZAO_CHUNK];
    cz_ops_t ops = {.context = test, .root = test->root, .stop = stopped, .request = request,
                    .now = now_fixed, .free_bytes = available, .book_added = added, .progress = progress,
                    .row = observe_row, .stage = observe_stage, .record = observe_result};
    return cloudzao_run(&ops, job, &test_queue, buffer, chunk);
}

static cz_status_t run(test_t *test, const char *json) {
    cz_status_t preview = run_job(test, CZ_JOB_PREVIEW, json);
    if (preview.error) return preview;
    bool any = false;
    for (unsigned i = 0; i < test_queue.count; ++i) {
        test_queue.selection.checked[i] = test_queue.offers[i].kind != CZ_KIND_NONE;
        any |= test_queue.selection.checked[i];
    }
    return any ? run_job(test, CZ_JOB_DOWNLOAD, json) : preview;
}

static void empty_preview_regression(void) {
    test_t f = {.list_reply = "HTTP/1.1 200 \r\nTransfer-Encoding: chunked\r\n\r\n"
        "c\r\n" LIST("") "\r\n0\r\n\r\n"};
    test_queue.count = 1;
    fail_crypto_init = true;
    cz_status_t status = run_job(&f, CZ_JOB_PREVIEW, NULL);
    assert(status.error == CZ_OK && !status.total && !test_queue.count && f.lists == 1);
    assert(!f.downloads && !f.confirmations && !f.finished);
    f.list_reply = NULL;
    status = run_job(&f, CZ_JOB_PREVIEW, LIST(TXT("book.txt", "test-book", DIGEST)));
    assert(status.error == CZ_RESOURCES);
    fail_crypto_init = false;
    f.list_reply = "HTTP/1.1 503 \r\nTransfer-Encoding: chunked\r\n\r\n"
        "c\r\n" LIST("") "\r\n0\r\n\r\n";
    assert(run_job(&f, CZ_JOB_PREVIEW, NULL).error == CZ_SERVER);
    f.list_reply = "HTTP/1.1 200 \r\nContent-Length: 0\r\n\r\n";
    assert(run_job(&f, CZ_JOB_PREVIEW, NULL).error == CZ_INVALID);
    puts("CloudZao empty preview, HTTP error and missing list body: PASS");
}

static bool count(void *context, const void *data, size_t length) {
    (void)data;
    *(size_t *)context += length;
    return true;
}
static void framing(void) {
    assert(cloudzao_oss_host(OSS_HOST, sizeof(OSS_HOST) - 1));
    assert(cloudzao_oss_host("bucket.ALIYUNCS.COM", sizeof("bucket.ALIYUNCS.COM") - 1));
    assert(!cloudzao_oss_host("fakealiyuncs.com", sizeof("fakealiyuncs.com") - 1));
    assert(!cloudzao_oss_host("bucket.aliyuncs.com.example.invalid", sizeof("bucket.aliyuncs.com.example.invalid") - 1));
    const char *content = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    const char *chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n2\r\nbc\r\n0\r\n\r\n";
    const char *redirect = "HTTP/1.1 302 Found\r\nLocation: https://other.invalid/\r\nContent-Length: 3\r\n\r\nabc";
    const char *unauthorized = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n";
    cz_response_t response;
    size_t received = 0;
    cloudzao_wire_begin(&response, CZ_DOWNLOAD, 3, count, &received);
    for (size_t i = 0; i < strlen(content); ++i) assert(cloudzao_wire_feed(&response, content+i, 1));
    assert(response.done && received == 3);
    received = 0;
    cloudzao_wire_begin(&response, CZ_DOWNLOAD, 3, count, &received);
    assert(cloudzao_wire_feed(&response, chunked, strlen(chunked)) && response.done && received == 3);
    received = 0;
    cloudzao_wire_begin(&response, CZ_DOWNLOAD, 2, count, &received);
    assert(!cloudzao_wire_feed(&response, content, strlen(content)) && !received && response.error == CZ_INVALID);
    cloudzao_wire_begin(&response, CZ_DOWNLOAD, 2, count, &received);
    assert(!cloudzao_wire_feed(&response, chunked, strlen(chunked)) && received == 1 && response.error == CZ_INVALID);
    received = 0;
    cloudzao_wire_begin(&response, CZ_DOWNLOAD, 3, count, &received);
    assert(!cloudzao_wire_feed(&response, redirect, strlen(redirect)) && !received && response.error == CZ_SERVER);
    cloudzao_wire_begin(&response, CZ_LIST, 3, count, &received);
    assert(cloudzao_wire_feed(&response, unauthorized, strlen(unauthorized)) && response.done && response.error == CZ_AUTH);
    const char *other_conflict = "HTTP/1.1 409 Conflict\r\nContent-Length: 2\r\n\r\n{}";
    cloudzao_wire_begin(&response, CZ_BIND, 0, NULL, NULL);
    assert(cloudzao_wire_feed(&response, other_conflict, strlen(other_conflict)) && response.done && response.error == CZ_SERVER);
    const char *activation_error = "{\"error\":\"activation_unavailable\"}";
    char conflict[128];
    snprintf(conflict, sizeof(conflict), "HTTP/1.1 409 Conflict\r\nContent-Length: %zu\r\n\r\n%s",
             strlen(activation_error), activation_error);
    cloudzao_wire_begin(&response, CZ_EXCHANGE, 0, NULL, NULL);
    assert(cloudzao_wire_feed(&response, conflict, strlen(conflict)) && response.done && response.error == CZ_SERVER);
    cloudzao_wire_begin(&response, CZ_DOWNLOAD, 3, count, &received);
    assert(cloudzao_wire_feed(&response, content, strlen(content) - 1) && !response.done);
    assert(!cloudzao_wire_feed(&response, "", 0));
    char host[254], wire[CLOUDZAO_CHUNK];
    cz_http_request_t req = {.url = "https://claw.cloudzao.cn@other.invalid/x"};
    assert(!cloudzao_wire_request(CZ_DOWNLOAD, &req, host, sizeof(host), wire, sizeof(wire)));
    req.url = OSS_URL; req.token = KEY;
    assert(!cloudzao_wire_request(CZ_DOWNLOAD, &req, host, sizeof(host), wire, sizeof(wire)));
    const cz_http_request_t invalid_token = {.token = "invalid\r\nAuthorization: injected"};
    assert(!cloudzao_wire_request(CZ_LIST, &invalid_token, host, sizeof(host), wire, sizeof(wire)));
    const char *bad_id[] = {"..", "a/b", "a%2fb", "a?b", "a\r\nb"};
    for (unsigned i = 0; i < sizeof(bad_id) / sizeof(*bad_id); ++i) {
        cz_http_request_t ack = {.token = KEY, .delivery_id = bad_id[i]};
        assert(!cloudzao_wire_request(CZ_ACK, &ack, host, sizeof(host), wire, sizeof(wire)));
    }
    const char *acknowledged = "HTTP/1.1 204 No Content\r\n\r\n";
    cloudzao_wire_begin(&response, CZ_ACK, 0, NULL, NULL);
    assert(cloudzao_wire_feed(&response, acknowledged, strlen(acknowledged)) && response.done && response.error == CZ_OK);
    cloudzao_wire_begin(&response, CZ_ACK, 0, NULL, NULL);
    assert(!cloudzao_wire_feed(&response, content, strlen(content)));
    req.token = NULL;
    const char *rejected[] = {
        "http://cdn.example.invalid" TARGET, "http://bucket.aliyuncs.com.example.invalid" TARGET,
        "http://bucket.aliyuncs.com@other.aliyuncs.com" TARGET, "http://bucket.aliyuncs.com:80" TARGET,
        OSS_URL "#fragment", "https://host.example.invalid:80" TARGET,
        "https://cdn.example.invalid" EP_RAW_PATH EP_SIGNATURE,
        "https://" OSS_HOST "/object.epub?name=书", "https://" OSS_HOST "/\xC0\xAF.epub",
        "https://" OSS_HOST "/\xED\xA0\x80.epub", "https://" OSS_HOST "/\xE4\xB9.epub",
        "https://" OSS_HOST "/书 .epub", "https://" OSS_HOST "/书.epub?x=\r\n",
    };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        req.url = rejected[i];
        assert(!cloudzao_wire_request(CZ_DOWNLOAD, &req, host, sizeof(host), wire, sizeof(wire)));
    }
    req.url = EP_OSS_URL;
    const size_t written = cloudzao_wire_request(CZ_DOWNLOAD, &req, host, sizeof(host), wire, sizeof(wire));
    assert(written && written == strlen(wire) && !strcmp(host, OSS_HOST));
    assert(!memcmp(wire, "GET " EP_WIRE_PATH EP_SIGNATURE " HTTP/1.1\r\n", sizeof("GET " EP_WIRE_PATH EP_SIGNATURE " HTTP/1.1\r\n") - 1));
    assert(!strstr(wire, "Authorization:") && !strstr(wire, "书"));
    assert(!cloudzao_wire_request(CZ_DOWNLOAD, &req, host, sizeof(host), wire, written));
    assert(cloudzao_wire_request(CZ_DOWNLOAD, &req, host, sizeof(host), wire, written + 1) == written);
    char max_url[CLOUDZAO_URL_MAX + 1] = "https://" OSS_HOST "/书";
    const size_t prefix = strlen(max_url);
    memset(max_url + prefix, 'x', CLOUDZAO_URL_MAX - prefix - 6);
    max_url[CLOUDZAO_URL_MAX - 6] = 0;
    req.url = max_url;
    assert(cloudzao_wire_request(CZ_DOWNLOAD, &req, host, sizeof(host), wire, sizeof(wire)));
    max_url[CLOUDZAO_URL_MAX - 6] = 'x';
    assert(!cloudzao_wire_request(CZ_DOWNLOAD, &req, host, sizeof(host), wire, sizeof(wire)));
}

#define PAIR_RESPONSE "{\"activationCode\":\"012345\",\"deviceCode\":\"pdc_fixture\",\"expiresIn\":86400,\"pollInterval\":5}"
#define TOKEN_REPLY(access, refresh) "{\"accessToken\":\"" access "\",\"refreshToken\":\"" refresh \
    "\",\"expiresIn\":900,\"refreshExpiresIn\":2592000}"

typedef struct {
    int64_t now, request_seconds;
    cz_session_record_t durable;
    cz_request_t expected;
    const char *body, *token;
    unsigned calls[6], deny_list, deny_ack;
    int http, list_http;
    cz_error_t transport, refresh_cause;
    unsigned refresh_failures;
    bool refresh_sent, refresh_needs_rebind;
    bool fail_write, reject_tokens, unsent, pair_after_refresh;
} partner_fixture_t;

static int64_t partner_clock(void *context) { return ((partner_fixture_t *)context)->now; }
static cz_error_t partner_stop(void *context) { (void)context; return CZ_OK; }
static bool partner_persist(void *context, const cz_session_record_t *record) {
    partner_fixture_t *f = context;
    assert(cloudzao_partner_record_valid(record));
    if (f->fail_write || (f->reject_tokens && record->state == CZ_SESSION_AUTHORIZED)) return false;
    f->durable = *record;
    return true;
}

static void partner_refresh_failed(void *context, cz_error_t cause, bool maybe_sent, bool rebind_required) {
    partner_fixture_t *f = context;
    ++f->refresh_failures;
    f->refresh_cause = cause;
    f->refresh_sent = maybe_sent;
    f->refresh_needs_rebind = rebind_required;
}

static cz_error_t partner_raw(void *context, cz_request_t op, const cz_http_request_t *req,
                              cz_sink_t sink, void *sink_context) {
    partner_fixture_t *f = context;
    char wire[1024], host[254];
    assert(cloudzao_wire_request(op, req, host, sizeof(host), wire, sizeof(wire)));
    assert(!strcmp(host, "claw.cloudzao.cn") && strstr(wire, "\r\nHost: claw.cloudzao.cn\r\n"));
    ++f->calls[op];
    f->now += f->request_seconds;
    unsigned http = f->http;
    const char *body = f->body;
    if (op == CZ_LIST || op == CZ_ACK) {
        assert(req->token && !req->body && !req->url);
        unsigned *deny = op == CZ_LIST ? &f->deny_list : &f->deny_ack;
        if (*deny) {
            --*deny;
            http = 401;
            body = "{\"error\":\"invalid_device_credential\"}";
        } else {
            assert(!f->token || !strcmp(req->token, f->token));
            http = op == CZ_LIST ? (f->list_http ? f->list_http : 200) : 204;
            body = op == CZ_LIST ? LIST("") : "";
        }
    } else {
        assert(op == f->expected && !req->token && !strstr(wire, "Authorization:"));
        if (op == CZ_BIND) {
            assert(strstr(wire, "POST " BIND_PATH " HTTP/1.1"));
            assert(!strcmp(req->body, "{\"integrationId\":\"integration_fixture\",\"deviceMac\":\"" MAC
                          "\",\"currentVersion\":\"1.1.6\"}"));
        } else if (op == CZ_EXCHANGE) {
            assert(strstr(wire, "POST " BASE "/device-tokens HTTP/1.1"));
            assert(!strcmp(req->body, "{\"deviceCode\":\"pdc_fixture\"}"));
        } else if (op == CZ_REFRESH) {
            assert(strstr(wire, "POST " BASE "/device-tokens/refresh HTTP/1.1"));
            assert(strstr(req->body, "\"refreshToken\":\"prf_fixture"));
            assert(f->durable.state == CZ_SESSION_ROTATING && !f->durable.refresh[0]);
            if (f->pair_after_refresh) {
                f->expected = CZ_BIND;
                f->http = 200;
                f->body = PAIR_RESPONSE;
            }
        }
        if (op == CZ_BIND && f->pair_after_refresh) assert(f->durable.state == CZ_SESSION_EMPTY);
        if (req->maybe_sent) *req->maybe_sent = !f->unsent;
        if (f->transport) return f->transport;
    }
    cz_response_t response;
    const int n = snprintf(wire, sizeof(wire), "HTTP/1.1 %u Test\r\nContent-Length: %zu\r\n\r\n%s", http, strlen(body), body);
    assert(n > 0 && (size_t)n < sizeof(wire));
    cloudzao_wire_begin(&response, op, req->limit, sink, sink_context);
    for (int i = 0; i < n; ++i) {
        if (!cloudzao_wire_feed(&response, wire + i, 1)) return response.error;
    }
    assert(response.done);
    return response.error;
}

static cz_status_t pair(cz_partner_t *p, bool poll) {
    return cloudzao_partner_pair(p, poll ? CZ_JOB_POLL : CZ_JOB_BIND, "integration_fixture", MAC, "1.1.6");
}

static cz_error_t inbox(cz_partner_t *p, cz_request_t operation) {
    size_t received = 0;
    const cz_http_request_t req = {.limit = 128, .delivery_id = operation == CZ_ACK ? "book-fixture" : NULL};
    return cloudzao_partner_request(p, operation, &req, operation == CZ_LIST ? count : NULL, &received);
}

static void partner_regression(void) {
    partner_fixture_t f = {.now = 1794787500, .expected = CZ_BIND, .http = 409,
        .body = "{\"error\":\"activation_unavailable\"}"};
    const cz_ops_t ops = {.context = &f, .request = partner_raw, .now = partner_clock, .stop = partner_stop};
    char reply[CLOUDZAO_PARTNER_RESPONSE_CAP];
    cz_partner_t p = {.raw = &ops, .persist = partner_persist, .response = reply};
    assert(inbox(&p, CZ_LIST) == CZ_AUTH && f.calls[CZ_LIST] == 0);
    assert(pair(&p, false).error == CZ_ACTIVATION_UNAVAILABLE && !f.calls[CZ_LIST]);
    assert(cloudzao_partner_pair(&p, CZ_JOB_BIND, "", MAC, "1.1.6").error == CZ_INTEGRATION && f.calls[CZ_BIND] == 1);
    f.http = 200; f.body = "{\"activationCode\":12345}";
    assert(pair(&p, false).error == CZ_INVALID && !f.durable.state);
    f.body = PAIR_RESPONSE;
    cz_status_t status = pair(&p, false);
    assert(status.error == CZ_OK && !strcmp(status.code, "012345") && status.poll_after_s == 5);
    assert(f.durable.state == CZ_SESSION_PAIRING && !strcmp(f.durable.credential, "pdc_fixture"));
    p.record = f.durable;
    status = pair(&p, false);
    assert(status.error == CZ_PAIR_PENDING && status.poll_after_s == 5 && f.calls[CZ_BIND] == 3);
    assert(pair(&p, true).error == CZ_PAIR_PENDING && !f.calls[CZ_EXCHANGE]);
    f.now += 5; f.expected = CZ_EXCHANGE; f.http = 409; f.body = "{\"error\":\"authorization_pending\"}";
    assert(pair(&p, true).error == CZ_PAIR_PENDING && f.calls[CZ_EXCHANGE] == 1);
    f.now += 5; f.http = 429; f.body = "{\"error\":\"slow_down\"}";
    status = pair(&p, true);
    assert(status.error == CZ_SLOW_DOWN && status.poll_after_s == 30);
    p.record = f.durable;
    f.now += 29;
    assert(pair(&p, true).poll_after_s == 30 && f.calls[CZ_EXCHANGE] == 2);
    f.now += 30; f.http = 200; f.body = TOKEN_REPLY("pat_fixture.first", "prf_fixture.first");
    assert(pair(&p, true).authorized && f.durable.state == CZ_SESSION_AUTHORIZED && !f.durable.code[0]);
    assert(f.durable.expires_at == f.now + 900 && f.durable.refresh_expires_at == f.now + 2592000);
    const cz_session_record_t first = f.durable;
    p.record = f.durable; f.token = "pat_fixture.first";
    assert(inbox(&p, CZ_LIST) == CZ_OK && !f.calls[CZ_REFRESH]);
    f.now = 0;
    assert(inbox(&p, CZ_LIST) == CZ_CLOCK && f.calls[CZ_LIST] == 1);
    f.now = first.issued_at + 815; f.expected = CZ_REFRESH;
    f.body = TOKEN_REPLY("pat_fixture.second", "prf_fixture.second"); f.token = "pat_fixture.second";
    assert(inbox(&p, CZ_LIST) == CZ_OK && f.calls[CZ_REFRESH] == 1 && f.calls[CZ_LIST] == 2);
    assert(f.durable.refresh_expires_at == first.refresh_expires_at);
    f.deny_ack = 1; f.body = TOKEN_REPLY("pat_fixture.third", "prf_fixture.third"); f.token = "pat_fixture.third";
    assert(inbox(&p, CZ_ACK) == CZ_OK && f.calls[CZ_REFRESH] == 2 && f.calls[CZ_ACK] == 2);
    f.deny_list = 2; f.body = TOKEN_REPLY("pat_fixture.fourth", "prf_fixture.fourth");
    assert(inbox(&p, CZ_LIST) == CZ_AUTH && f.calls[CZ_REFRESH] == 3 && f.calls[CZ_LIST] == 4);
    f.deny_list = 1; f.http = 429; f.body = "{\"error\":\"slow_down\"}";
    assert(inbox(&p, CZ_LIST) == CZ_SLOW_DOWN && f.calls[CZ_REFRESH] == 4 && f.durable.state == CZ_SESSION_AUTHORIZED);
    f.deny_list = 1;
    assert(inbox(&p, CZ_LIST) == CZ_SLOW_DOWN && f.calls[CZ_REFRESH] == 4);
    f.now += 30; f.http = 401; f.body = "{\"error\":\"refresh_reused\"}"; f.deny_list = 1;
    assert(inbox(&p, CZ_LIST) == CZ_REFRESH_REUSED && f.durable.state == CZ_SESSION_EMPTY);

    f.http = 200; f.fail_write = true; f.now = first.expires_at;
    p.record = f.durable = first;
    assert(inbox(&p, CZ_LIST) == CZ_CREDENTIAL_STORAGE && f.calls[CZ_REFRESH] == 5);
    f.fail_write = false; f.transport = CZ_TIMEOUT; f.unsent = true;
    assert(inbox(&p, CZ_LIST) == CZ_TIMEOUT && f.durable.state == CZ_SESSION_AUTHORIZED);
    f.unsent = false;
    assert(inbox(&p, CZ_LIST) == CZ_REFRESH_INTERRUPTED && f.durable.state == CZ_SESSION_ROTATING);
    p.record = f.durable; f.transport = CZ_OK;
    assert(inbox(&p, CZ_LIST) == CZ_REFRESH_INTERRUPTED && f.calls[CZ_REFRESH] == 7);
    p.record = f.durable = first; f.reject_tokens = true; f.body = TOKEN_REPLY("pat_fixture.lost", "prf_fixture.lost");
    assert(inbox(&p, CZ_ACK) == CZ_REFRESH_INTERRUPTED && f.durable.state == CZ_SESSION_ROTATING);
    f.reject_tokens = false; p.record = f.durable = first;
    f.body = TOKEN_REPLY("pat_fixture.lost", "prf_fixture.first");
    assert(inbox(&p, CZ_ACK) == CZ_REFRESH_INTERRUPTED && f.durable.state == CZ_SESSION_ROTATING);
    f.expected = CZ_BIND; f.body = PAIR_RESPONSE;
    assert(cloudzao_partner_pair(&p, CZ_JOB_REBIND, "integration_fixture", MAC, "1.1.6").poll_after_s == 5);
    f.expected = CZ_EXCHANGE; f.now += 5; f.http = 410; f.body = "{\"error\":\"pairing_expired\"}";
    assert(pair(&p, true).error == CZ_PAIR_EXPIRED && f.durable.state == CZ_SESSION_EMPTY);
    assert(pair(&p, true).error == CZ_PAIR_EXPIRED);
    puts("CloudZao Partner pairing, reboot, backoff, rotation, 401 and ambiguous refresh: PASS");
}

static void partner_check_authorization_regression(void) {
    const int64_t now = 1794787500;
    const cz_session_record_t first = {.state = CZ_SESSION_AUTHORIZED, .issued_at = now - 30,
        .expires_at = now + 870, .refresh_expires_at = now + 2591970,
        .credential = "pat_fixture.first", .refresh = "prf_fixture.first"};
    partner_fixture_t f = {.now = now, .durable = first, .token = "pat_fixture.first"};
    const cz_ops_t ops = {.context = &f, .request = partner_raw, .now = partner_clock, .stop = partner_stop};
    char reply[CLOUDZAO_PARTNER_RESPONSE_CAP];
    cz_partner_t p = {.raw = &ops, .persist = partner_persist, .response = reply, .record = first};

    cz_status_t status = pair(&p, false);
    assert(status.authorized && status.error == CZ_OK && f.calls[CZ_LIST] == 1);
    assert(!f.calls[CZ_REFRESH] && !f.calls[CZ_BIND] && !f.calls[CZ_ACK] && !f.calls[CZ_DOWNLOAD]);

    f.list_http = 503;
    status = pair(&p, false);
    assert(!status.authorized && status.error == CZ_SERVER && !status.code[0]);
    assert(!f.calls[CZ_REFRESH] && !f.calls[CZ_BIND] && !strcmp(f.durable.refresh, first.refresh));
    f.list_http = 0;

    f.deny_list = 1; f.expected = CZ_REFRESH; f.http = 200;
    f.body = TOKEN_REPLY("pat_fixture.second", "prf_fixture.second"); f.token = "pat_fixture.second";
    status = pair(&p, false);
    assert(status.authorized && f.calls[CZ_LIST] == 4 && f.calls[CZ_REFRESH] == 1 && !f.calls[CZ_BIND]);

    // 模拟 App 解绑：访问令牌仍在本机有效期内，但云端拒绝访问和刷新。
    // Simulate app unlinking: the token is locally unexpired, but the server rejects access and refresh.
    f.deny_list = 1; f.http = 401; f.body = "{\"error\":\"invalid_device_credential\"}";
    f.pair_after_refresh = true;
    status = pair(&p, false);
    assert(!status.authorized && status.error == CZ_OK && !strcmp(status.code, "012345") && status.poll_after_s == 5);
    assert(f.calls[CZ_LIST] == 5 && f.calls[CZ_REFRESH] == 2 && f.calls[CZ_BIND] == 1);
    assert(f.durable.state == CZ_SESSION_PAIRING && !f.durable.refresh[0]);
    status = pair(&p, false);
    assert(status.error == CZ_PAIR_PENDING && !strcmp(status.code, "012345") && f.calls[CZ_BIND] == 1);

    // 即使刷新成功，云端再次拒绝也不保留旧授权。/ A second denial invalidates the grant even if refresh succeeded.
    p.record = f.durable = first;
    f.deny_list = 2; f.expected = CZ_REFRESH; f.http = 200;
    f.body = TOKEN_REPLY("pat_fixture.third", "prf_fixture.third");
    status = pair(&p, false);
    assert(!status.authorized && status.error == CZ_OK && !strcmp(status.code, "012345"));
    assert(f.calls[CZ_LIST] == 7 && f.calls[CZ_REFRESH] == 3 && f.calls[CZ_BIND] == 2);
    assert(f.durable.state == CZ_SESSION_PAIRING && !f.durable.refresh[0]);
    puts("CloudZao Settings checks live authorization and re-pairs after unlink: PASS");
}

static void partner_interrupted_refresh_regression(void) {
    const int64_t now = 1794787500;
    const cz_session_record_t first = {.state = CZ_SESSION_AUTHORIZED, .issued_at = now - 900,
        .expires_at = now, .refresh_expires_at = now + 86400,
        .credential = "pat_fixture.first", .refresh = "prf_fixture.first"};
    partner_fixture_t f = {.now = now, .durable = first, .expected = CZ_REFRESH, .http = 200,
        .body = TOKEN_REPLY("pat_fixture.next", "prf_fixture.next"), .transport = CZ_TIMEOUT, .unsent = true};
    const cz_ops_t ops = {.context = &f, .request = partner_raw, .now = partner_clock, .stop = partner_stop};
    char reply[CLOUDZAO_PARTNER_RESPONSE_CAP];
    cz_partner_t p = {.raw = &ops, .persist = partner_persist, .refresh_failed = partner_refresh_failed,
        .response = reply, .record = first};

    assert(pair(&p, false).error == CZ_TIMEOUT && f.durable.state == CZ_SESSION_AUTHORIZED);
    assert(f.refresh_failures == 1 && f.refresh_cause == CZ_TIMEOUT && !f.refresh_sent && !f.refresh_needs_rebind);
    assert(!strcmp(f.durable.refresh, first.refresh) && !f.calls[CZ_BIND]);

    f.unsent = false;
    cz_status_t status = pair(&p, false);
    assert(status.error == CZ_REFRESH_INTERRUPTED && !status.authorized && !status.poll_after_s);
    assert(f.refresh_failures == 2 && f.refresh_cause == CZ_TIMEOUT && f.refresh_sent && f.refresh_needs_rebind);
    assert(f.durable.state == CZ_SESSION_ROTATING && !f.durable.refresh[0] && f.calls[CZ_REFRESH] == 2);

    p.record = f.durable;
    assert(pair(&p, false).error == CZ_REFRESH_INTERRUPTED); // 重启后也不自动领码。/ No automatic code after reboot either.
    assert(inbox(&p, CZ_LIST) == CZ_REFRESH_INTERRUPTED && inbox(&p, CZ_ACK) == CZ_REFRESH_INTERRUPTED);
    assert(!f.calls[CZ_BIND] && !f.calls[CZ_LIST] && !f.calls[CZ_ACK] && f.calls[CZ_REFRESH] == 2);

    f.expected = CZ_BIND; f.transport = CZ_OK; f.http = 409;
    f.body = "{\"error\":\"activation_unavailable\"}";
    status = cloudzao_partner_pair(&p, CZ_JOB_REBIND, "integration_fixture", MAC, "1.1.6");
    assert(status.error == CZ_ACTIVATION_UNAVAILABLE && status.rebind_retry && !status.poll_after_s);
    assert(f.durable.state == CZ_SESSION_ROTATING && f.calls[CZ_BIND] == 1);
    f.http = 200; f.body = PAIR_RESPONSE;
    status = cloudzao_partner_pair(&p, CZ_JOB_REBIND, "integration_fixture", MAC, "1.1.6");
    assert(status.error == CZ_OK && !status.rebind_retry && !strcmp(status.code, "012345") && status.poll_after_s == 5);
    assert(f.durable.state == CZ_SESSION_PAIRING && f.calls[CZ_BIND] == 2 && f.calls[CZ_REFRESH] == 2);
    f.now += 5; f.expected = CZ_EXCHANGE; f.body = TOKEN_REPLY("pat_fixture.recovered", "prf_fixture.recovered");
    assert(pair(&p, true).authorized && f.durable.state == CZ_SESSION_AUTHORIZED && f.calls[CZ_EXCHANGE] == 1);
    assert(!strcmp(f.durable.refresh, "prf_fixture.recovered") && f.calls[CZ_REFRESH] == 2);
    puts("CloudZao interrupted refresh diagnosis, no replay, explicit re-pair: PASS");
}

static void partner_pair_timeout_regression(void) {
    partner_fixture_t f = {.now = 1794787500, .expected = CZ_BIND, .http = 200, .body = PAIR_RESPONSE};
    const cz_ops_t ops = {.context = &f, .request = partner_raw, .now = partner_clock, .stop = partner_stop};
    char reply[CLOUDZAO_PARTNER_RESPONSE_CAP];
    cz_partner_t p = {.raw = &ops, .persist = partner_persist, .response = reply};
    assert(pair(&p, false).poll_after_s == 5);
    f.expected = CZ_EXCHANGE;
    f.now += 5;
    f.transport = CZ_TIMEOUT;
    cz_status_t status = pair(&p, true);
    assert(status.error == CZ_TIMEOUT && !strcmp(status.code, "012345") && status.poll_after_s == 10);
    assert(!status.authorized && f.calls[CZ_BIND] == 1 && f.calls[CZ_EXCHANGE] == 1);
    assert(f.durable.state == CZ_SESSION_PAIRING && !strcmp(f.durable.credential, "pdc_fixture"));

    p.record = f.durable;
    assert(pair(&p, false).poll_after_s == 5 && f.calls[CZ_BIND] == 1);
    f.now += status.poll_after_s;
    f.transport = CZ_OK;
    f.body = TOKEN_REPLY("pat_fixture.first", "prf_fixture.first");
    assert(pair(&p, true).authorized && f.calls[CZ_EXCHANGE] == 2);
    assert(f.durable.state == CZ_SESSION_AUTHORIZED && !strcmp(f.durable.refresh, "prf_fixture.first"));

    p.record = f.durable = (cz_session_record_t){0};
    f.expected = CZ_BIND;
    f.body = "{\"activationCode\":\"012345\",\"deviceCode\":\"pdc_fixture\",\"expiresIn\":60,\"pollInterval\":15}";
    assert(pair(&p, false).poll_after_s == 15);
    f.expected = CZ_EXCHANGE;
    f.now += 15;
    f.transport = CZ_TIMEOUT;
    status = pair(&p, true);
    assert(status.error == CZ_TIMEOUT && status.poll_after_s == 15);
    f.now = f.durable.expires_at - 8;
    f.request_seconds = 4;
    status = pair(&p, true);
    assert(status.error == CZ_TIMEOUT && status.poll_after_s == 4 && status.code[0]);
    f.request_seconds = 6;
    status = pair(&p, true);
    assert(status.error == CZ_PAIR_EXPIRED && !status.poll_after_s && !status.code[0]);
    assert(pair(&p, true).error == CZ_PAIR_EXPIRED && f.calls[CZ_EXCHANGE] == 5);

    f.request_seconds = 0;
    f.expected = CZ_BIND;
    f.transport = CZ_OK;
    assert(pair(&p, false).poll_after_s == 15);
    f.expected = CZ_EXCHANGE;
    f.now += 15;
    f.transport = CZ_TIMEOUT;
    status = pair(&p, true);
    assert(status.error == CZ_TIMEOUT && status.code[0]);
    f.now += status.poll_after_s;
    f.transport = CZ_OK;
    f.http = 410;
    f.body = "{\"error\":\"pairing_expired\"}";
    status = pair(&p, true);
    assert(status.error == CZ_PAIR_EXPIRED && !status.authorized && !status.poll_after_s && !status.code[0]);
    assert(f.durable.state == CZ_SESSION_EMPTY);
    assert(pair(&p, true).error == CZ_PAIR_EXPIRED && f.calls[CZ_BIND] == 3 && f.calls[CZ_EXCHANGE] == 7);
    puts("CloudZao Partner exchange timeout, retry, and expiry: PASS");
}

static void selection_regression(const char *root) {
#define SELECT_BOOK TXT("choice.txt", "choice-book", DIGEST)
#define SELECT_IMAGE ITEM("BADGE", "", "image/png", "choice.png", "choice-image", DIGEST)
#define SELECT_FONT ITEM("FONT", "", "font/ttf", "choice.ttf", "choice-font", DIGEST)
#define UNSUPPORTED_ITEM ITEM("FONT", "", "application/octet-stream", "choice.ef", "choice-unsupported", DIGEST)
    const char *queue = LIST(SELECT_BOOK "," SELECT_IMAGE "," UNSUPPORTED_ITEM "," SELECT_FONT);
    test_t test = {.root = root, .space = 1ULL << 30, .expected_ack = "choice-image"};
    cz_status_t status = run_job(&test, CZ_JOB_PREVIEW, queue);
    assert(!status.error && status.total == 4 && test_queue.count == 4 && test.lists == 1);
    assert(test_queue.offers[0].kind == CZ_KIND_TXT && test_queue.offers[1].kind == CZ_KIND_PNG &&
           test_queue.offers[2].kind == CZ_KIND_NONE && test_queue.offers[3].kind == CZ_KIND_TTF);
    assert(!test.downloads && !test.confirmations && !test.queued && !test.finished && !test.books);
    assert(!present(root, "books") && !present(root, "pictures") && !present(root, "fonts") && !present(root, ".readpico"));
    test_queue.selection = (cz_selection_t){0};
    test_queue.selection.checked[2] = true;
    assert(run_job(&test, CZ_JOB_DOWNLOAD, queue).error == CZ_INVALID && test.lists == 1);
    test_queue.selection.checked[1] = test_queue.selection.checked[3] = true;
    // 云端重排、换签、删除与新推送均不能扩大用户选择。
    // Reordering, URL refresh, removal and newly pushed files never expand selection.
    status = run_job(&test, CZ_JOB_DOWNLOAD, LIST(SELECT_BOOK "," TXT("new.txt", "new-book", DIGEST) ","
        ITEM_URL("BADGE", "", "image/png", "choice.png", "choice-image", DIGEST, OSS_URL) "," SELECT_IMAGE));
    assert(!status.error && status.total == 2 && status.saved == 1 && !status.pending && status.unavailable == 1);
    assert(test.lists == 2 && test.downloads == 1 && test.confirmations == 1 && test.finished == 2);
    assert(present(root, "pictures/choice.png") && !present(root, "books") && !present(root, "fonts"));
    assert(!run_job(&test, CZ_JOB_PREVIEW, queue).error);
    test_queue.selection = (cz_selection_t){.checked = {true}};
    status = run_job(&test, CZ_JOB_DOWNLOAD, LIST(TXT("choice.txt", "choice-book",
        "aa7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") ","
        TXT("choice.txt", "different-task", DIGEST)));
    assert(!status.error && status.unavailable == 1 && !status.saved && !present(root, "books"));
    assert(test.downloads == 1 && test.confirmations == 1);
    test.interrupt = CZ_CANCELLED;
    assert(run_job(&test, CZ_JOB_DOWNLOAD, queue).error == CZ_CANCELLED && test.downloads == 1 && test.confirmations == 1);
#undef SELECT_BOOK
#undef SELECT_IMAGE
#undef SELECT_FONT
#undef UNSUPPORTED_ITEM
}

static void expired_link_regression(const char *root) {
    char expired[512];
    const char *valid = LIST(TXT("expiring.txt", "expiry-id", DIGEST));
    assert(snprintf(expired, sizeof(expired), "%.*s,\"downloadExpiresAt\":1794787499}]}", (int)strlen(valid) - 3, valid) < (int)sizeof(expired));
    test_t test = {.root = root, .space = 1ULL << 30, .replacement_json = valid, .replacement_on = 3, .ack_timeouts = 1};
    cz_status_t status = run(&test, expired);
    assert(!status.error && status.saved == 1 && status.pending == 0 && test.lists == 3 && test.downloads == 1 && test.confirmations == 2);
    test.replacement_json = NULL;
    status = run_job(&test, CZ_JOB_PREVIEW, LIST("{\"kind\":\"book\",\"format\":\"txt\",\"mediaType\":\"text/plain\",\"name\":\"no-digest.txt\",\"deliveryId\":\"no-digest\",\"sizeBytes\":3}"));
    assert(!status.error && test_queue.offers[0].kind == CZ_KIND_NONE);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    framing();
    empty_preview_regression();
    test_t test = {.root = argv[1], .space = 1ULL << 30};
    partner_regression();
    partner_check_authorization_regression();
    partner_interrupted_refresh_regression();
    partner_pair_timeout_regression();
    char selection_root[512];
    assert(snprintf(selection_root, sizeof(selection_root), "%s/selection", argv[1]) < (int)sizeof(selection_root));
    assert(!mkdir(selection_root, 0775));
    selection_regression(selection_root);
    cz_status_t status;
    assert(!run(&test, LIST("")).total && test.lists == 1);
    assert(run(&test, "{\"code\":401,\"data\":[]}").error == CZ_INVALID);
    assert(run(&test, "{\"code\":500,\"data\":[]}").error == CZ_INVALID);
    assert(run(&test, "{\"items\":[]}junk").error == CZ_INVALID);
    assert(run(&test, "{\"items\":[[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]]}").error == CZ_INVALID);
    test.oversized = true;
    assert(run(&test, "").error == CZ_INVALID);
    test.oversized = false;
    status = run(&test, LIST(TXT("../escape.txt", "book-0", DIGEST) "," TXT("a\\u0000.txt", "book-0", DIGEST)));
    assert(status.error == CZ_INVALID && !test.downloads && !test.confirmations);
    status = run(&test, LIST(TXT("../escape.txt", "book-0", DIGEST) "," TXT(".hidden.txt", "book-0", DIGEST)
        "," ITEM("FONT", "", "application/octet-stream", "unsupported.ef", "font-1", DIGEST)));
    assert(!status.error && status.total == 3 && !test.downloads && !test.confirmations);
    for (unsigned i = 0; i < 3; ++i) assert(test_queue.offers[i].kind == CZ_KIND_NONE);
    assert(!test_queue.offers[0].file.name[0] && !test_queue.offers[1].file.name[0]);
    status = run(&test, LIST(TXT("bad.txt", "book-0", "aa7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
    assert(!status.error && status.failed == 1 && !test.confirmations && !present(test.root, "books/bad.txt"));
    test.downloads = 0;
    test.ack_timeouts = 2;
    status = run(&test, LIST(TXT("中文.txt", "book-1", DIGEST)));
    assert(!status.error && status.saved == 1 && status.pending == 1 && status.item_request == CZ_ACK && test.confirmations == 2 && test.downloads == 1 && test.books == 1);
    assert(test.queued == 1 && test.finished == 1 && test.written == 3);
    assert(test.stage_mask == ((1u << CZ_ROW_DOWNLOAD) | (1u << CZ_ROW_VERIFY) | (1u << CZ_ROW_CONFIRM)));
    test.ack_result = CZ_OK;
    assert(!run(&test, LIST("")).total && test.confirmations == 2 && test.downloads == 1);
    status = run(&test, LIST(TXT("中文.txt", "book-1", DIGEST)));
    assert(!status.error && status.saved == 1 && !status.pending && test.confirmations == 3 && test.downloads == 1 && test.books == 1);
    status = run(&test, LIST(TXT("中文.txt", "book-2", "aa7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
    assert(status.item_error == CZ_CONFLICT && test.confirmations == 3 && test.downloads == 1);
    status = run(&test, LIST(ITEM_URL("BOOK", "EPUB", "application/epub+zip", "中文.epub", "book-3", DIGEST, EP_OSS_URL) ","
        ITEM("BADGE", "", "image/png", "wall.png", "badge-3", DIGEST) "," ITEM("FONT", "", "font/ttf", "font.ttf", "font-3", DIGEST)));
    assert(!status.error && status.saved == 3 && !status.pending && test.books == 2);
    assert(present(test.root, "books/中文.epub") && present(test.root, "pictures/wall.png") && present(test.root, "fonts/font.ttf"));
    status = run(&test, LIST(ITEM_URL("BOOK", "TXT", "text/plain", "oss.txt", "book-4", DIGEST, OSS_URL)));
    assert(!status.error && status.saved == 1 && !status.pending && present(test.root, "books/oss.txt"));
    unsigned confirmations = test.confirmations, downloads = test.downloads;
    status = run(&test, LIST(ITEM_URL("BOOK", "TXT", "text/plain", "insecure.txt", "book-5", DIGEST,
        "http://cdn.example.invalid" TARGET)));
    assert(!status.error && !status.saved && test_queue.offers[0].kind == CZ_KIND_NONE);
    assert(test.confirmations == confirmations && test.downloads == downloads && !present(test.root, "books/insecure.txt"));
    test.interrupt_during_download = CZ_CANCELLED;
    status = run(&test, LIST(TXT("cancel.txt", "book-5", DIGEST)));
    assert(status.error == CZ_CANCELLED && test.confirmations == confirmations && !present(test.root, "books/cancel.txt"));
    assert(!present(test.root, ".readpico/cloudzao/incoming.part"));
    test.interrupt = CZ_OK;
    test.interrupt_during_download = CZ_MEDIA;
    status = run(&test, LIST(TXT("pull.txt", "book-5", DIGEST)));
    assert(status.error == CZ_MEDIA && test.confirmations == confirmations && !present(test.root, "books/pull.txt"));
    assert(present(test.root, ".readpico/cloudzao/incoming.part"));
    test.interrupt = test.interrupt_during_download = CZ_OK;
    assert(!run(&test, LIST("")).error && present(test.root, ".readpico/cloudzao/incoming.part"));
    test.space = 0;
    status = run(&test, LIST(TXT("full.txt", "book-5", DIGEST)));
    assert(status.item_error == CZ_STORAGE && test.confirmations == confirmations);
    assert(!present(test.root, ".readpico/cloudzao/incoming.part"));
    char many[1024] = "{\"items\":[";
    for (unsigned i = 0; i < CLOUDZAO_LIST_ROWS + 3; ++i) strcat(many, i ? ",{}" : "{}");
    strcat(many, "]}");
    status = run(&test, many);
    assert(!status.error && status.total == CLOUDZAO_LIST_ROWS + 3 && test_queue.count == CLOUDZAO_LIST_ROWS);
    assert(!test.queued && !test.finished);
    history_regression(&test);
    expired_link_regression(argv[1]);
    puts("CloudZao framing, storage, acknowledgement and cancellation passed");
    return 0;
}
