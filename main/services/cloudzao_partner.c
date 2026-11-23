/*
 * 中文：只在现有 Wi-Fi 工作任务里交换凭据；不用后台线程保活。
 * English: Exchange credentials within existing Wi-Fi jobs; no background keepalive thread.
 */
#include "cloudzao_partner.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

#define PARTNER_RESPONSE_MAX (CLOUDZAO_PARTNER_RESPONSE_CAP - 1)
#define PAIR_MAX_SECONDS 86400u
#define REFRESH_MAX_SECONDS (30u * 86400u)
#define REFRESH_AHEAD_SECONDS 90u
#define SLOW_DOWN_SECONDS 30u
#define PAIR_TIMEOUT_RETRY_SECONDS 10u

static const cz_session_record_t empty = {0}, rotating = {.state = CZ_SESSION_ROTATING};

void cloudzao_partner_wipe(void *data, size_t size) {
    for (volatile unsigned char *p = data; size; --size) *p++ = 0;
}

static bool pin_valid(const char *pin) {
    return pin && strlen(pin) == 6 && strspn(pin, "0123456789") == 6;
}

bool cloudzao_partner_record_valid(const cz_session_record_t *r) {
    if (r->state == CZ_SESSION_EMPTY || r->state == CZ_SESSION_ROTATING) return true;
    if (r->issued_at <= 0 || r->expires_at <= r->issued_at || r->expires_at > 253402300799LL ||
        r->credential[CLOUDZAO_TOKEN_MAX] || !cloudzao_token_valid(r->credential)) return false;
    if (r->state == CZ_SESSION_PAIRING)
        return !r->code[6] && pin_valid(r->code) && r->poll_interval && r->poll_interval <= PAIR_MAX_SECONDS;
    return r->state == CZ_SESSION_AUTHORIZED && r->refresh_expires_at >= r->expires_at &&
        r->refresh_expires_at <= 253402300799LL && !r->refresh[CLOUDZAO_TOKEN_MAX] && cloudzao_token_valid(r->refresh);
}

static bool store(cz_partner_t *p, const cz_session_record_t *r) {
    if (!p->persist(p->raw->context, r)) return false;
    cloudzao_partner_wipe(&p->record, sizeof(p->record));
    p->record = *r;
    return true;
}

static bool prepare_refresh(void *context) { return store(context, &rotating); }

typedef struct { char *body; size_t used; } response_t;
static bool collect(void *ctx, const void *data, size_t size) {
    response_t *reply = ctx;
    if (size > PARTNER_RESPONSE_MAX - reply->used || memchr(data, 0, size)) return false;
    memcpy(reply->body + reply->used, data, size);
    reply->used += size;
    return true;
}

static const char *member(const cJSON *root, const char *key) {
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, key));
}

static uint32_t seconds(const cJSON *root, const char *key, uint32_t max) {
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(number) || number->valuedouble < 1 || number->valuedouble > max) return 0;
    uint32_t value = (uint32_t)number->valuedouble;
    return number->valuedouble == value ? value : 0;
}

static void delete_reply(cJSON *root) {
    cJSON *field = NULL;
    cJSON_ArrayForEach(field, root) {
        if (cJSON_IsString(field)) cloudzao_partner_wipe(field->valuestring, strlen(field->valuestring));
    }
    cJSON_Delete(root);
}

static cz_error_t json_request(cz_partner_t *p, cz_request_t operation, const char *body, cJSON **out, cz_refresh_send_t *refresh) {
    response_t reply = {.body = p->response};
    const cz_http_request_t request = {.body = body, .limit = PARTNER_RESPONSE_MAX, .refresh = refresh};
    cz_error_t error = p->raw->request(p->raw->context, operation, &request, collect, &reply);
    reply.body[reply.used] = 0;
    *out = error == CZ_OK ? cloudzao_json_parse(reply.body, reply.used) : NULL;
    cloudzao_partner_wipe(reply.body, reply.used + 1);
    return error == CZ_OK && !cJSON_IsObject(*out) ? CZ_INVALID : error;
}

static cz_error_t save_tokens(cz_partner_t *p, const cJSON *root, int64_t now, int64_t refresh_cap) {
    const char *access = member(root, "accessToken"), *refresh = member(root, "refreshToken");
    uint32_t life = seconds(root, "expiresIn", REFRESH_MAX_SECONDS);
    uint32_t refresh_life = seconds(root, "refreshExpiresIn", REFRESH_MAX_SECONDS);
    if (!life || !refresh_life || !cloudzao_token_valid(access) || !cloudzao_token_valid(refresh)) return CZ_INVALID;
    cz_session_record_t next = {.state = CZ_SESSION_AUTHORIZED, .issued_at = now,
        .expires_at = now + life, .refresh_expires_at = now + refresh_life};
    if (refresh_cap && next.refresh_expires_at > refresh_cap) next.refresh_expires_at = refresh_cap;
    if (next.expires_at > next.refresh_expires_at) next.expires_at = next.refresh_expires_at;
    strcpy(next.credential, access);
    strcpy(next.refresh, refresh);
    cz_error_t error = store(p, &next) ? CZ_OK : CZ_CREDENTIAL_STORAGE;
    cloudzao_partner_wipe(&next, sizeof(next));
    return error;
}

static cz_error_t ensure_access(cz_partner_t *p, bool force) {
    int64_t now = p->raw->now(p->raw->context);
    if (!now) return CZ_CLOCK;
    const cz_session_record_t *r = &p->record;
    if (r->state == CZ_SESSION_ROTATING) return CZ_REFRESH_INTERRUPTED;
    if (r->state != CZ_SESSION_AUTHORIZED || now < r->issued_at || now >= r->refresh_expires_at) return CZ_AUTH;
    int64_t ahead = (r->expires_at - r->issued_at) / 2;
    if (ahead > REFRESH_AHEAD_SECONDS) ahead = REFRESH_AHEAD_SECONDS;
    if (!force && now + ahead < r->expires_at) return CZ_OK;
    if (now < r->retry_at) return CZ_SLOW_DOWN;
    cz_session_record_t previous = *r;
    cz_error_t error = p->raw->stop(p->raw->context);
    cz_refresh_send_t send = {.prepare = prepare_refresh, .context = p};
    if (error == CZ_OK) {
        // 回包丢失时保留已轮换标记，防止重启后重发可能作废的令牌。
        // Keep a rotation marker on an ambiguous result so reboot cannot replay a possibly spent token.
        char body[CLOUDZAO_TOKEN_MAX + 24];
        snprintf(body, sizeof(body), "{\"refreshToken\":\"%s\"}", previous.refresh);
        cJSON *root = NULL;
        error = json_request(p, CZ_REFRESH, body, &root, &send);
        cloudzao_partner_wipe(body, sizeof(body));
        if (error == CZ_OK) {
            const char *next_refresh = member(root, "refreshToken");
            error = next_refresh && !strcmp(next_refresh, previous.refresh) ? CZ_INVALID :
                save_tokens(p, root, now, previous.refresh_expires_at);
        }
        else if (!send.maybe_sent || error == CZ_SLOW_DOWN) {
            if (error == CZ_SLOW_DOWN) previous.retry_at = now + SLOW_DOWN_SECONDS;
            if (p->record.state == CZ_SESSION_ROTATING && !store(p, &previous)) error = CZ_CREDENTIAL_STORAGE;
        } else if (error == CZ_AUTH || error == CZ_REFRESH_REUSED) {
            if (!store(p, &empty)) error = CZ_CREDENTIAL_STORAGE;
        }
        delete_reply(root);
    }
    cloudzao_partner_wipe(&previous, sizeof(previous));
    const bool rebind_required = p->record.state == CZ_SESSION_ROTATING;
    if (error != CZ_OK && p->refresh_failed)
        p->refresh_failed(p->raw->context, error, send.maybe_sent, rebind_required);
    return rebind_required && error != CZ_CANCELLED && error != CZ_MEDIA ? CZ_REFRESH_INTERRUPTED : error;
}

static bool discard_list_body(void *context, const void *data, size_t size) {
    (void)context;
    (void)data;
    (void)size;
    return true;
}

static void pending_status(cz_status_t *status, const cz_session_record_t *record, int64_t now, uint32_t delay) {
    if (now < record->issued_at || now >= record->expires_at) { status->error = CZ_PAIR_EXPIRED; return; }
    memcpy(status->code, record->code, sizeof(status->code));
    int64_t remaining = record->expires_at - now;
    status->poll_after_s = delay < remaining ? delay : (uint32_t)remaining;
}

cz_status_t cloudzao_partner_pair(cz_partner_t *p, cz_job_t job, const char *integration, const char *mac, const char *version) {
    const bool poll = job == CZ_JOB_POLL;
    cz_status_t status = {.rebind_retry = job == CZ_JOB_REBIND};
    if (job != CZ_JOB_BIND && !poll && !status.rebind_retry) { status.error = CZ_INVALID; return status; }
    const cz_session_record_t *r = &p->record;
    if (r->state == CZ_SESSION_ROTATING && !status.rebind_retry) {
        status.error = CZ_REFRESH_INTERRUPTED;
        return status;
    }
    int64_t now = p->raw->now(p->raw->context);
    if (!now) { status.error = CZ_CLOCK; return status; }
    if (!poll && r->state == CZ_SESSION_AUTHORIZED && now >= r->issued_at && now < r->refresh_expires_at) {
        // 本机有效期不能反映 App 解绑；只用清单请求验证授权，不保存清单。
        // Local expiry cannot detect app unlinking; verify through the list endpoint without retaining its body.
        const cz_http_request_t request = {.limit = CLOUDZAO_JSON_MAX};
        status.error = cloudzao_partner_request(p, CZ_LIST, &request, discard_list_body, NULL);
        if (status.error != CZ_AUTH && status.error != CZ_REFRESH_REUSED) {
            status.authorized = status.error == CZ_OK;
            if (status.authorized) status.rebind_retry = false;
            return status;
        }
        if (r->state != CZ_SESSION_EMPTY && !store(p, &empty)) {
            status.error = CZ_CREDENTIAL_STORAGE;
            return status;
        }
        now = p->raw->now(p->raw->context);
        if (!now) { status.error = CZ_CLOCK; return status; }
    }
    if (r->state == CZ_SESSION_PAIRING && now >= r->issued_at && now < r->expires_at) {
        if (!poll || now < r->retry_at) {
            status.rebind_retry = false;
            status.error = CZ_PAIR_PENDING;
            uint32_t delay = r->poll_interval;
            if (now < r->retry_at && r->retry_at - now > delay) delay = (uint32_t)(r->retry_at - now);
            pending_status(&status, r, now, delay);
            return status;
        }
        char body[CLOUDZAO_TOKEN_MAX + 24];
        snprintf(body, sizeof(body), "{\"deviceCode\":\"%s\"}", r->credential);
        cJSON *root = NULL;
        status.error = json_request(p, CZ_EXCHANGE, body, &root, NULL);
        cloudzao_partner_wipe(body, sizeof(body));
        if (status.error == CZ_OK) {
            status.error = save_tokens(p, root, now, 0);
            status.authorized = status.error == CZ_OK;
        } else if (status.error == CZ_PAIR_EXPIRED) {
            if (!store(p, &empty)) status.error = CZ_CREDENTIAL_STORAGE;
        } else if (status.error == CZ_PAIR_PENDING || status.error == CZ_SLOW_DOWN || status.error == CZ_TIMEOUT) {
            uint32_t delay = r->poll_interval;
            if (status.error == CZ_TIMEOUT && delay < PAIR_TIMEOUT_RETRY_SECONDS) delay = PAIR_TIMEOUT_RETRY_SECONDS;
            // 按回包后的时间退避；超时不清除原会话，也不另申请绑定码。
            // Schedule from request completion; a timeout neither clears nor replaces the pairing.
            now = p->raw->now(p->raw->context);
            if (!now) status.error = CZ_CLOCK;
            else if (now < r->issued_at || now >= r->expires_at) status.error = CZ_PAIR_EXPIRED;
            if (status.error == CZ_SLOW_DOWN) {
                delay = delay < SLOW_DOWN_SECONDS ? SLOW_DOWN_SECONDS : delay;
                cz_session_record_t next = *r;
                next.poll_interval = delay;
                next.retry_at = now + delay;
                if (!store(p, &next)) status.error = CZ_CREDENTIAL_STORAGE;
                cloudzao_partner_wipe(&next, sizeof(next));
            }
            if (status.error == CZ_PAIR_PENDING || status.error == CZ_SLOW_DOWN || status.error == CZ_TIMEOUT)
                pending_status(&status, r, now, delay);
        }
        delete_reply(root);
        return status;
    }
    if (poll) { status.error = CZ_PAIR_EXPIRED; return status; }
    status.error = p->raw->stop(p->raw->context);
    if (status.error != CZ_OK) return status;
    if (!cloudzao_token_valid(integration)) { status.error = CZ_INTEGRATION; return status; }
    if (!version || !*version || strlen(version) > 31 || !cloudzao_token_valid(version) ||
        !mac || strlen(mac) != 17 || strspn(mac, "0123456789ABCDEFabcdef:") != 17) {
        status.error = CZ_INVALID;
        return status;
    }
    char body[CLOUDZAO_TOKEN_MAX + 128];
    snprintf(body, sizeof(body), "{\"integrationId\":\"%s\",\"deviceMac\":\"%s\",\"currentVersion\":\"%s\"}", integration, mac, version);
    cJSON *root = NULL;
    status.error = json_request(p, CZ_BIND, body, &root, NULL);
    if (status.error == CZ_OK) {
        const char *code = member(root, "activationCode"), *device = member(root, "deviceCode");
        uint32_t life = seconds(root, "expiresIn", PAIR_MAX_SECONDS);
        uint32_t interval = seconds(root, "pollInterval", PAIR_MAX_SECONDS);
        if (!life || !interval || !pin_valid(code) || !cloudzao_token_valid(device)) status.error = CZ_INVALID;
        else {
            cz_session_record_t next = {.state = CZ_SESSION_PAIRING, .issued_at = now,
                .expires_at = now + life, .poll_interval = interval, .retry_at = now + interval};
            memcpy(next.code, code, sizeof(next.code));
            strcpy(next.credential, device);
            if (store(p, &next)) {
                status.rebind_retry = false;
                pending_status(&status, &next, now, interval);
            } else status.error = CZ_CREDENTIAL_STORAGE;
            cloudzao_partner_wipe(&next, sizeof(next));
        }
    }
    delete_reply(root);
    return status;
}

cz_error_t cloudzao_partner_request(cz_partner_t *p, cz_request_t operation, const cz_http_request_t *request,
                                   cz_sink_t sink, void *sink_context) {
    if (operation == CZ_DOWNLOAD) return p->raw->request(p->raw->context, operation, request, sink, sink_context);
    if (operation != CZ_LIST && operation != CZ_ACK) return CZ_INVALID;
    cz_error_t error = p->raw->stop(p->raw->context);
    if (error != CZ_OK) return error;
    if ((error = ensure_access(p, false)) != CZ_OK) return error;
    // 已发出的刷新可先提交新凭据，取消后不再发送清单或确认。
    // An in-flight refresh can commit first; cancellation must still stop listing or acknowledgement.
    if ((error = p->raw->stop(p->raw->context)) != CZ_OK) return error;
    cz_http_request_t authenticated = *request;
    authenticated.token = p->record.credential;
    error = p->raw->request(p->raw->context, operation, &authenticated, sink, sink_context);
    if (error != CZ_AUTH) return error;
    if ((error = ensure_access(p, true)) != CZ_OK) return error;
    if ((error = p->raw->stop(p->raw->context)) != CZ_OK) return error;
    return p->raw->request(p->raw->context, operation, &authenticated, sink, sink_context);
}
