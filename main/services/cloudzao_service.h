/*
 * 中文：CloudZao 接收会话与卸载前的文件交还。
 * English: CloudZao receive sessions and file handoff before unmount.
 */
#pragma once
#include <stdbool.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
#include "cloudzao_receive.h"
/// 发布构建已配置接入 ID 时显示入口。/ Show the entry when the release build has an integration ID.
bool cloudzao_available(void);
/// 读取这版注意事项的本机确认状态，缺失或读失败视为未确认。/ Read this notice revision's local acknowledgement; missing or unreadable means unconfirmed.
bool cloudzao_notice_acknowledged(void);
/// 仅由确认按钮调用；提交成功才返回 true。/ Call only from the acknowledgement button; true on successful commit.
bool cloudzao_acknowledge_notice(void);
/// 启动单次操作；下载必须提供勾选，参数只在启动时复制。/ Start one job; download requires a selection, copied at start.
cz_error_t cloudzao_start(cz_job_t job, const cz_selection_t *selection);
/// 只暴露预览元数据，不暴露用于匹配的指纹。/ Expose preview metadata only, not matching fingerprints.
bool cloudzao_offer(unsigned index, cz_row_t *out, cz_kind_t *kind);
void cloudzao_discard_queue(void);
cz_status_t cloudzao_status(void);
cz_transfer_t cloudzao_transfer_status(void);
bool cloudzao_transfer_row(unsigned index, cz_row_t *out);
/// 离线读取当前 TF 卡的历史，由同一工作门控管理。/ Read current-card history offline under the same worker gate.
cz_error_t cloudzao_start_history(void);
/// 离线清除当前 TF 卡的历史，沿用工作门控和卡交还。/ Clear current-card history offline under the worker and media-handoff gates.
cz_error_t cloudzao_start_history_clear(void);
cz_history_status_t cloudzao_history_status(void);
bool cloudzao_history_item(unsigned index, cz_history_item_t *out);
void cloudzao_cancel(void);
void cloudzao_media_lost(void);
/// 仅主循环消费一次，合并同批书架失效通知。/ Main loop consumes coalesced bookshelf invalidation once.
bool cloudzao_take_books_changed(void);
/// 取消后续业务并等待归还；已经尝试发送的刷新可限时收尾，等待超时不终止任务。
/// Cancel follow-up work and wait for handoff; a started refresh may finish within its deadline, and a wait timeout does not kill it.
bool cloudzao_quiesce(unsigned timeout_ms);
#else
static inline bool cloudzao_quiesce(unsigned timeout_ms) { (void)timeout_ms; return true; }
#endif
