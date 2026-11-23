/*
 * 中文：CloudZao Partner v1 配对与单次轮换状态机，存储和 HTTP 由调用方提供。
 * English: CloudZao Partner v1 pairing and single-use rotation; storage and HTTP are injected.
 */
#pragma once
#include "cloudzao_receive.h"

#define CLOUDZAO_PARTNER_RESPONSE_CAP 2049u

typedef enum { CZ_SESSION_EMPTY, CZ_SESSION_PAIRING, CZ_SESSION_AUTHORIZED, CZ_SESSION_ROTATING } cz_session_t;
typedef struct {
    cz_session_t state;
    int64_t issued_at, expires_at, refresh_expires_at, retry_at;
    uint32_t poll_interval;
    char code[7];
    char credential[CLOUDZAO_TOKEN_MAX + 1]; ///< deviceCode 或访问令牌。/ Device code or access token.
    char refresh[CLOUDZAO_TOKEN_MAX + 1];
} cz_session_record_t;

typedef struct {
    const cz_ops_t *raw;
    char *response; ///< 调用方提供固定容量的凭据响应缓冲。/ Caller-owned fixed-capacity credential reply buffer.
    cz_session_record_t record;
    /// 成功时完整提交整条记录；轮换标记落盘后才可发送刷新请求。
    /// Commit the entire record; send the refresh only after the rotation marker is durable.
    bool (*persist)(void *context, const cz_session_record_t *record);
    /// 可选：只报告错误和刷新阶段标记，不传递凭据或回包。/ Optional: report errors and rotation flags, never credentials or reply data.
    void (*refresh_failed)(void *context, cz_error_t cause, bool maybe_sent, bool rebind_required);
} cz_partner_t;

/// 恢复待配对或联网验证授权；刷新中断时，仅显式 REBIND 可再申请绑定码。
/// Resume pending pairing or verify the grant online; only explicit REBIND may request a code after interrupted rotation.
cz_status_t cloudzao_partner_pair(cz_partner_t *partner, cz_job_t job, const char *integration, const char *mac, const char *version);
/// 下载不附加凭据；清单与确认在 401 后至多刷新并重试一次。/ Downloads omit credentials; list and ACK refresh and retry at most once on 401.
cz_error_t cloudzao_partner_request(cz_partner_t *partner, cz_request_t operation, const cz_http_request_t *request,
                                   cz_sink_t sink, void *sink_context);
/// 不允许 NVS 损坏或不同版本的记录成为有效凭据。/ Reject malformed persisted records.
bool cloudzao_partner_record_valid(const cz_session_record_t *record);
void cloudzao_partner_wipe(void *data, size_t size);
