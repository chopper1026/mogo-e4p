/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：直接验证触屏配网状态机及输入边界。
 * English: Exercise the device provisioning state machine and input bounds.
 * 冻结：仅供宿主测试，不访问网络。/ Frozen: Host tests only; no network access.
 */
#include "transfer_ui_test_env.h"
#define TRANSFER_UI_RECORD_REFRESH
#include "../main/apps/app_transfer.c"
const char *ui_locale_tag(void){return "zh-CN";}
#include <assert.h>
#include "../components/wifi_transfer/transfer_signature.h"
static bool test_usb_active;
static esp_err_t test_usb_stop_error;
esp_err_t usb_storage_start(void) { test_usb_active = true; return ESP_OK; }
esp_err_t usb_storage_stop(void) {
    if (test_usb_stop_error != ESP_OK) return test_usb_stop_error;
    test_usb_active = false;
    return ESP_OK;
}
bool usb_storage_active(void) { return test_usb_active; }
bool usb_storage_connected(void) { return test_usb_active; }
static void tap(app_ctx_t* ctx, EpdRect rect) {
    ui_gesture_event_t ev = {.type = UI_GESTURE_PRESS, .x0 = rect.x + 2, .y0 = rect.y + 2, .x = rect.x + 2, .y = rect.y + 2};
    app_redraw_t pressed = on_gesture(ctx, &ev);
    // 整页刷新在 app_loop 中会取消活动手势；入口反馈必须保留松手事件。
    // app_loop cancels active gestures on a page refresh; feedback must preserve release.
    assert(pressed != APP_REDRAW_PAGE && pressed != APP_REDRAW_FULL);
    ev.type = UI_GESTURE_TAP;
    on_gesture(ctx, &ev);
}
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
static unsigned test_cloud_starts, test_cloud_cancels, test_history_reads, test_history_clears;
static cz_error_t test_history_clear_start_error;
static cz_job_t test_cloud_job;
static unsigned test_offer_rows;
static cz_selection_t test_downloaded_selection;
static cz_error_t test_cloud_start_error;
static unsigned test_batch_first, test_batch_drawn;
static unsigned test_history_first, test_history_drawn;
static cz_status_t test_cloud_status;
static cz_history_status_t test_history_status;
static cz_transfer_t test_transfer_status;
static bool test_notice_acknowledged = true, test_notice_save_error;
static unsigned test_notice_writes;

bool cloudzao_notice_acknowledged(void) { return test_notice_acknowledged; }
bool cloudzao_acknowledge_notice(void) {
    ++test_notice_writes;
    if (test_notice_save_error) return false;
    test_notice_acknowledged = true;
    return true;
}

cz_error_t cloudzao_start(cz_job_t job, const cz_selection_t *selection) {
    assert((job == CZ_JOB_DOWNLOAD) == (selection != NULL));
    if (test_cloud_start_error) return test_cloud_start_error;
    test_cloud_job = job;
    if (selection) test_downloaded_selection = *selection;
    ++test_cloud_starts;
    return CZ_OK;
}
void cloudzao_discard_queue(void) { test_offer_rows = 0; }
bool cloudzao_offer(unsigned index, cz_row_t *out, cz_kind_t *kind) {
    if (index >= test_offer_rows) return false;
    if (kind) *kind = index == 2 ? CZ_KIND_NONE : index == 7 ? CZ_KIND_PNG : CZ_KIND_TXT;
    if (out) *out = (cz_row_t){.size = 2000, .name = "queued-file"};
    return true;
}
cz_status_t cloudzao_status(void) { return test_cloud_status; }
cz_transfer_t cloudzao_transfer_status(void) { return test_transfer_status; }
bool cloudzao_transfer_row(unsigned index, cz_row_t *out) {
    assert(index < test_transfer_status.rows);
    if (!test_batch_drawn++) test_batch_first = index;
    *out = (cz_row_t){.size=8192, .state=CZ_ROW_WAIT, .name="listed.txt"};
    return true;
}
cz_error_t cloudzao_start_history(void) { ++test_history_reads; return CZ_OK; }
cz_error_t cloudzao_start_history_clear(void) {
    ++test_history_clears;
    if (test_history_clear_start_error) return test_history_clear_start_error;
    test_history_status.busy = true;
    return CZ_OK;
}
cz_history_status_t cloudzao_history_status(void) { return test_history_status; }
bool cloudzao_history_item(unsigned index, cz_history_item_t *out) {
    if (index >= test_history_status.count) return false;
    if (!test_history_drawn++) test_history_first = index;
    *out = (cz_history_item_t){.file = {.size = 1536, .state = CZ_ROW_SAVED, .name = "novel.EPUB"}};
    return true;
}
void cloudzao_cancel(void) { ++test_cloud_cancels; }
bool cloudzao_key_valid(const char *key) { return !strcmp(key, "test-key"); }

static const char *test_expected_cloud_text;
static bool test_cloud_text_drawn, test_key_setting_drawn;
static void observe_cloud_text(const char *text) {
    assert(!strstr(text, "本机中转") && !strstr(text, "单机联调凭据"));
    if (!strcmp(text, test_expected_cloud_text)) test_cloud_text_drawn = true;
    if (!strcmp(text, "设置联调密钥")) test_key_setting_drawn = true;
}

static void assert_cloud_text(app_ctx_t *ctx, const char *text) {
    test_expected_cloud_text = text;
    test_cloud_text_drawn = test_key_setting_drawn = false;
    test_observe_text = observe_cloud_text;
    draw_cloud(ctx->fb);
    test_observe_text = NULL;
    assert(test_cloud_text_drawn && !test_key_setting_drawn);
}

static void assert_cloud_settings_text(app_ctx_t *ctx, const char *text) {
    assert(s_cloud_panel == CLOUD_PANEL_SETTINGS);
    assert_cloud_text(ctx, text);
}

static unsigned test_mark_black, test_mark_edge;

static void observe_cloud_mark(int x, int y, uint8_t gray) {
    assert(x > 86 && x < UI_LOCK_WIDTH / 2 && y > 93 && y < 120);
    assert(gray < 0xf0 && (gray & 0x0f) == 0);
    if (gray == 0) ++test_mark_black;
    else ++test_mark_edge;
}

static void cloud_header_regression(app_ctx_t *ctx) {
    test_observe_pixel = observe_cloud_mark;
    draw_cloud_header(ctx->fb);
    test_observe_pixel = NULL;
    assert(test_mark_black > 0 && test_mark_edge > 0);
}

static void cloud_back_regression(app_ctx_t *ctx) {
    const EpdRect back = {36, 79, 50, 50};
    for (int resumed = 0; resumed <= 1; ++resumed) {
        for (int i = CLOUD_PANEL_TRANSFER; i <= CLOUD_PANEL_SETTINGS; ++i) {
            for (int key = 0; key <= 3; ++key) {
                app_transfer_request_cloudzao();
                on_enter(ctx);
                // 模拟从快捷设置返回，CloudZao 重入时没有新的入口请求。
                // Model returning from quick settings: CloudZao reenters without a new entry request.
                if (resumed) {
                    transfer_on_exit(ctx);
                    on_enter(ctx);
                }
                tap(ctx, cloud_tab(i));
                assert(s_cloud_panel == (cloud_panel_t)i && !ctx->request_return);
                if (i == CLOUD_PANEL_SETTINGS) tap(ctx, cloud_button(CLOUD_BIND));
                if (i == CLOUD_PANEL_TRANSFER) tap(ctx, cloud_offer_fetch());
                const unsigned cancels = test_cloud_cancels;
                test_nav_index = -1;
                if (!key) tap(ctx, back);
                else assert(on_key(ctx, UI_KEY_1 + key - 1) == APP_REDRAW_NONE);
                assert(test_nav_index == (key == 2 ? 0 : 2) && !ctx->request_return && s_view != TRANSFER_CLOUDZAO);
                assert(test_cloud_cancels == cancels + 1 && !s_cloud_bind_status.code[0] && !s_cloud_poll_at);
                assert(!s_cloud_bind_status.busy && !s_cloud_history.busy && !s_cloud_status.busy);
                transfer_on_exit(ctx);
                test_nav_index = -1;
            }
        }
    }
}

static void cloud_direct_entry_regression(app_ctx_t *ctx) {
    app_transfer_request_cloudzao();
    on_enter(ctx);
    assert(s_view == TRANSFER_CLOUDZAO && s_entry_requested == TRANSFER_ENTRY_NONE);
    assert(!s_start_pending && !s_scan_pending && !s_usb_start_pending);
    assert(!test_cloud_starts && on_tick(ctx) == APP_REDRAW_NONE);
    assert_cloud_text(ctx, "查看CloudZao推送的书籍、字体和图片");
    tap(ctx, cloud_tab(CLOUD_PANEL_SETTINGS));
    assert_cloud_settings_text(ctx, "在CloudZao App或小程序中添加这台设备");
    tap(ctx, cloud_tab(CLOUD_PANEL_TRANSFER));
    const EpdRect receive = cloud_button(CLOUD_RECEIVE);
    const EpdRect fetch = cloud_offer_fetch();
    const EpdRect download = cloud_offer_submit();
    const ui_gesture_event_t outside = {.type=UI_GESTURE_TAP, .x0=fetch.x+4, .y0=fetch.y+4,
                                        .x=fetch.x+4, .y=fetch.y-1};
    assert(on_gesture(ctx, &outside) == APP_REDRAW_NONE && !test_cloud_starts);
    assert(fetch.x - UI_MARGIN == UI_MARGIN + ui_content_width() - (fetch.x + fetch.width));
    assert(fetch.y == download.y && fetch.y + fetch.height <= UI_NAV_TOP);
    EpdRect right_edge = {download.x + download.width - 8, download.y, 8, download.height};
    tap(ctx, right_edge);
    assert(!test_cloud_starts);
    assert(download.y + download.height <= UI_NAV_TOP);
    tap(ctx, fetch);
    assert(test_cloud_starts == 1 && test_cloud_job == CZ_JOB_PREVIEW && s_cloud_status.busy && !s_cloud_batch);
    test_offer_rows = 9;
    test_cloud_status = (cz_status_t){.total = 9};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_cloud_selectable == 8 && !s_cloud_checked);
    const EpdRect refresh = cloud_offer_fetch();
    assert(refresh.y + refresh.height < CLOUD_OFFER_TABLE_TOP);
    assert(cloud_offer_all().y + cloud_offer_all().height < cloud_offer_rect(0).y);
    const EpdRect last = cloud_offer_rect(CLOUD_OFFER_PAGE_ROWS - 1);
    const EpdRect pager = cloud_offer_pager(false);
    const EpdRect notice = cloud_offer_notice();
    assert(last.y + last.height < pager.y);
    assert(pager.y + pager.height < notice.y && notice.height > 0);
    assert(notice.y + notice.height < CLOUD_OFFER_SELECTION_Y);
    assert(pager.y + pager.height + 40 >= download.y);
    tap(ctx, download);
    tap(ctx, cloud_offer_rect(2));
    assert(!s_cloud_checked && test_cloud_starts == 1);
    tap(ctx, cloud_offer_all());
    assert(s_cloud_checked == 8 && s_cloud_selection.checked[8] && !s_cloud_selection.checked[2]);
    assert(s_area.y == CLOUD_OFFER_TABLE_TOP && s_area.y + s_area.height == CLOUD_OFFER_END);
    assert(present(ctx, APP_REDRAW_AREA) && test_refresh_intent == DISPLAY_REFRESH_DETAIL);
    tap(ctx, cloud_offer_all());
    assert(!s_cloud_checked && !s_cloud_selection.checked[8]);
    tap(ctx, cloud_offer_rect(0));
    tap(ctx, cloud_offer_pager(true));
    tap(ctx, cloud_offer_rect(1));
    assert(s_cloud_offer_page == 1 && s_cloud_checked == 2 && s_cloud_selection.checked[0] && s_cloud_selection.checked[7]);
    tap(ctx, pager);
    assert(!s_cloud_offer_page && s_cloud_checked == 2);
    tap(ctx, cloud_offer_pager(true));
    tap(ctx, cloud_tab(CLOUD_PANEL_SETTINGS));
    tap(ctx, cloud_tab(CLOUD_PANEL_TRANSFER));
    assert(s_cloud_offer_page == 1 && s_cloud_checked == 2);
    test_cloud_start_error = CZ_MEDIA;
    tap(ctx, download);
    assert(s_cloud_action_error == CZ_MEDIA && s_cloud_checked == 2 && !s_cloud_batch && test_cloud_starts == 1);
    test_cloud_start_error = CZ_OK;
    tap(ctx, download);
    assert(test_cloud_starts == 2 && test_cloud_job == CZ_JOB_DOWNLOAD && s_cloud_status.busy && s_cloud_batch);
    assert(test_downloaded_selection.checked[0] && test_downloaded_selection.checked[7]);
    for (unsigned i = 1; i < CLOUDZAO_LIST_ROWS; ++i) if (i != 7) assert(!test_downloaded_selection.checked[i]);
    test_cloud_status = (cz_status_t){.busy=true, .total=8, .processed=2, .saved=1, .pending=1};
    test_transfer_status = (cz_transfer_t){.rows=8, .revision=1, .active=true, .received=4096, .rate=2048,
        .file={.size=8192, .state=CZ_ROW_DOWNLOAD, .name="test.txt"}};
    assert(on_tick(ctx) == APP_REDRAW_AREA && !strcmp(cloud_result_title(), "正在下载推送"));
    assert(s_cloud_transfer.received==4096 && s_cloud_transfer.rate==2048);
    const EpdRect batch = cloud_batch_area();
    assert(s_area.y == cloud_result_area().y && s_area.y + s_area.height == batch.y + batch.height);
    assert(test_batch_drawn == CLOUD_BATCH_PAGE_ROWS && test_batch_first == 0);
    assert(present(ctx, APP_REDRAW_AREA) && test_refresh_intent == DISPLAY_REFRESH_DETAIL);

    // 当前项在本页时，速率或进度也只推数值条。/ A visible active row must not refresh on byte or rate changes.
    test_transfer_status.received += 512;
    ctx->now_ms += CLOUD_SAMPLE_PAINT_MS - 1;
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    ++ctx->now_ms;
    assert(on_tick(ctx) == APP_REDRAW_AREA);
    assert(s_area.y == cloud_sample_area().y && s_area.height == cloud_sample_area().height);
    assert(test_batch_drawn == CLOUD_BATCH_PAGE_ROWS);
    assert(present(ctx, APP_REDRAW_AREA) && test_refresh_intent == DISPLAY_REFRESH_FEEDBACK);
    assert(test_refresh_area.y + test_refresh_area.height < batch.y);

    // 速率归零更新一次，停滞时不周期刷屏。/ Paint a stalled rate once, then remain idle.
    test_transfer_status.rate = 0;
    ctx->now_ms += CLOUD_SAMPLE_PAINT_MS;
    assert(on_tick(ctx) == APP_REDRAW_AREA && s_cloud_transfer.rate == 0);
    ctx->now_ms += CLOUD_SAMPLE_PAINT_MS;
    assert(on_tick(ctx) == APP_REDRAW_NONE);

    // 阶段变化不必等实时采样，但密集切换会合并。/ Stages bypass the sample timer but coalesce rapid transitions.
    test_transfer_status.file.state = CZ_ROW_VERIFY;
    ++test_transfer_status.revision;
    assert(on_tick(ctx) == APP_REDRAW_AREA && test_batch_drawn == 2 * CLOUD_BATCH_PAGE_ROWS);
    assert(present(ctx, APP_REDRAW_AREA) && test_refresh_intent == DISPLAY_REFRESH_DETAIL);
    test_transfer_status.file.state = CZ_ROW_DOWNLOAD;
    test_transfer_status.file.size = 8 * 1048576;
    test_transfer_status.received = 4 * 1048576;
    ++test_transfer_status.revision;
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    ctx->now_ms += CLOUD_LAYOUT_PAINT_MS;
    assert(on_tick(ctx) == APP_REDRAW_AREA);
    tap(ctx, cloud_tab(CLOUD_PANEL_HISTORY));
    assert(!test_history_reads && s_cloud_panel == CLOUD_PANEL_TRANSFER);
    tap(ctx, cloud_pager(false, true));
    assert(s_cloud_panel == CLOUD_PANEL_TRANSFER && s_cloud_batch_page == 1);
    test_batch_drawn = 0;
    draw_cloud(ctx->fb);
    assert(test_batch_first == 5 && test_batch_drawn == 3);
    tap(ctx, cloud_tab(CLOUD_PANEL_TRANSFER));
    assert(s_cloud_batch_page == 1);
    test_transfer_status.received += 512;
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    ctx->now_ms += CLOUD_SAMPLE_PAINT_MS;
    assert(on_tick(ctx) == APP_REDRAW_NONE); // 进度条和格式化数字未变。/ Same displayed bar and numbers.
    test_transfer_status.received += 256 * 1024;
    ctx->now_ms += CLOUD_SAMPLE_PAINT_MS;
    assert(on_tick(ctx) == APP_REDRAW_AREA && s_area.height == cloud_sample_area().height);
    assert(test_batch_drawn == 3);

    tap(ctx, cloud_tab(CLOUD_PANEL_SETTINGS));
    assert(s_cloud_panel == CLOUD_PANEL_SETTINGS && !test_cloud_cancels);
    assert_cloud_settings_text(ctx, "下载 CloudZao App");
    assert_cloud_settings_text(ctx, "接收结束后可操作");
    tap(ctx, cloud_button(CLOUD_BIND));
    assert(test_cloud_starts == 2 && s_view == TRANSFER_CLOUDZAO);
    test_cloud_status.pending = 0;
    ctx->now_ms += 1000;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_status.pending == 0);
    tap(ctx, cloud_tab(CLOUD_PANEL_TRANSFER));
    assert(s_cloud_batch_page == 1 && s_cloud_status.busy);
    tap(ctx, cloud_pager(false, false));
    assert(s_cloud_batch_page == 0);
    tap(ctx, receive);
    tap(ctx, receive);
    assert(test_cloud_cancels == 1 && !strcmp(cloud_result_title(), "正在取消"));
    test_cloud_status.busy = false;
    test_cloud_status.error = CZ_CANCELLED;
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !strcmp(cloud_result_title(), "已取消"));

    tap(ctx, cloud_tab(CLOUD_PANEL_SETTINGS));
    assert_cloud_settings_text(ctx, "授权可用");
    assert_cloud_settings_text(ctx, "检查授权");
    tap(ctx, cloud_button(CLOUD_BIND));
    assert(test_cloud_starts == 3 && test_cloud_job == CZ_JOB_BIND && s_cloud_bind_status.busy && !s_cloud_status.busy);
    test_cloud_status = (cz_status_t){.code="001234", .poll_after_s=5};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !strcmp(cloud_bind_title(), "绑定码"));
    assert_cloud_settings_text(ctx, "001234");
    assert(s_cloud_transfer.rows == 8 && s_cloud_status.error == CZ_CANCELLED);
    ctx->now_ms = s_cloud_poll_at - 1;
    assert(on_tick(ctx) == APP_REDRAW_NONE && !s_cloud_poll_running);
    ++ctx->now_ms;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_poll_running && test_cloud_job == CZ_JOB_POLL);
    test_cloud_status.error = CZ_PAIR_PENDING;
    assert(on_tick(ctx) == APP_REDRAW_NONE && !s_cloud_poll_running && s_cloud_poll_at > ctx->now_ms);
    ctx->now_ms = s_cloud_poll_at;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_poll_running);
    test_cloud_status.error = CZ_TIMEOUT;
    test_cloud_status.poll_after_s = 10;
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_cloud_poll_at == ctx->now_ms + 10000);
    assert_cloud_settings_text(ctx, "领取授权超时，正在重试");
    assert_cloud_settings_text(ctx, "001234");
    ctx->now_ms = s_cloud_poll_at - 1;
    assert(on_tick(ctx) == APP_REDRAW_NONE && !s_cloud_poll_running);
    ++ctx->now_ms;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_poll_running && test_cloud_job == CZ_JOB_POLL);
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_poll_at == ctx->now_ms + 10000);
    ctx->now_ms = s_cloud_poll_at;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_poll_running);
    test_cloud_status.error = CZ_PAIR_PENDING;
    test_cloud_status.poll_after_s = 5;
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_cloud_poll_at == ctx->now_ms + 5000);
    assert_cloud_settings_text(ctx, "请在 CloudZao App 或 CloudZao 小程序输入此验证码");
    ctx->now_ms = s_cloud_poll_at;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_poll_running);
    test_cloud_status = (cz_status_t){.authorized=true};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !s_cloud_poll_at);
    assert_cloud_settings_text(ctx, "授权可用");
    assert_cloud_settings_text(ctx, "可以接收推送，无需重新绑定");
    tap(ctx, cloud_button(CLOUD_BIND));
    test_cloud_status = (cz_status_t){.error = CZ_ACTIVATION_UNAVAILABLE};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !s_cloud_poll_at && !s_cloud_bind_status.authorized);
    assert_cloud_settings_text(ctx, "暂未获取到绑定码");
    assert_cloud_settings_text(ctx, "若此设备已绑定，请先在");
    assert_cloud_settings_text(ctx, "CloudZao App 或小程序解绑后重试");
    assert_cloud_settings_text(ctx, "绑定 / 查看授权");
    const unsigned starts_after_unavailable = test_cloud_starts;
    ctx->now_ms += 10000;
    assert(on_tick(ctx) == APP_REDRAW_NONE && test_cloud_starts == starts_after_unavailable);
    tap(ctx, cloud_button(CLOUD_BIND));
    test_cloud_status = (cz_status_t){.error = CZ_SERVER};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert_cloud_settings_text(ctx, "服务请求失败");
    tap(ctx, cloud_button(CLOUD_BIND));
    assert(test_cloud_job == CZ_JOB_BIND);
    test_cloud_status = (cz_status_t){.error=CZ_REFRESH_INTERRUPTED};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !s_cloud_poll_at);
    assert_cloud_settings_text(ctx, "授权刷新未完成");
    assert_cloud_settings_text(ctx, "云端可能仍保留绑定");
    assert_cloud_settings_text(ctx, "如需恢复，请先在 App 解绑");
    assert_cloud_settings_text(ctx, "重新绑定");
    tap(ctx, cloud_button(CLOUD_BIND));
    assert(test_cloud_job == CZ_JOB_REBIND);
    test_cloud_status = (cz_status_t){.error=CZ_WIFI, .rebind_retry=true};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !s_cloud_poll_at);
    assert_cloud_settings_text(ctx, "重新绑定");
    tap(ctx, cloud_button(CLOUD_BIND));
    assert(test_cloud_job == CZ_JOB_REBIND);
    test_cloud_status = (cz_status_t){.error=CZ_WIFI};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !strcmp(cloud_bind_title(), "暂时无法绑定"));
    assert(!strcmp(cloud_bind_detail(), "请先下拉快捷栏打开wifi"));
    tap(ctx, cloud_button(CLOUD_BIND));
    test_cloud_status = (cz_status_t){.error=CZ_TIMEOUT};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !s_cloud_poll_at);
    assert_cloud_settings_text(ctx, "绑定请求超时，可重试");
    tap(ctx, cloud_button(CLOUD_BIND));
    test_cloud_status = (cz_status_t){.code = "012345", .poll_after_s = 5};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    ctx->now_ms = s_cloud_poll_at;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_poll_running);
    test_cloud_status = (cz_status_t){.error = CZ_TIMEOUT, .code = "012345", .poll_after_s = 10};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    const unsigned starts_before_pause = test_cloud_starts;
    tap(ctx, cloud_button(CLOUD_BIND));
    ctx->now_ms += 10000;
    assert(on_tick(ctx) == APP_REDRAW_NONE && test_cloud_starts == starts_before_pause && !s_cloud_poll_at);
    assert_cloud_settings_text(ctx, "绑定已暂停");
    tap(ctx, cloud_button(CLOUD_BIND));
    test_cloud_status = (cz_status_t){.error = CZ_PAIR_PENDING, .code = "012345", .poll_after_s = 5};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    ctx->now_ms = s_cloud_poll_at;
    assert(on_tick(ctx) == APP_REDRAW_NONE && s_cloud_poll_running);
    test_cloud_status = (cz_status_t){.error = CZ_PAIR_EXPIRED};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !s_cloud_poll_at);
    assert_cloud_settings_text(ctx, "绑定会话已失效，请检查 App 绑定");
    assert(s_cloud_transfer.rows == 8 && s_cloud_status.error == CZ_CANCELLED);
    tap(ctx, cloud_tab(CLOUD_PANEL_HISTORY));
    assert(test_history_reads == 1 && s_cloud_panel == CLOUD_PANEL_HISTORY && s_cloud_history.busy);
    test_history_status.count = 3;
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_cloud_history.count == 3);
    app_transfer.on_media_lost(ctx);
    assert(s_cloud_history.error == CZ_MEDIA && !s_cloud_history.count);
    tap(ctx, (EpdRect){36, 79, 50, 50});
    assert(test_nav_index == 2 && !ctx->request_return && test_cloud_cancels == 2 &&
           !s_cloud_bind_status.code[0] && !s_cloud_poll_at);
    transfer_on_exit(ctx);

    test_nav_index = -1;
    app_transfer_request_cloudzao();
    on_enter(ctx);
    s_cloud_panel = CLOUD_PANEL_SETTINGS;
    s_cloud_bind_status = (cz_status_t){.error = CZ_TIMEOUT, .code = "012345", .poll_after_s = 10};
    s_cloud_poll_at = ctx->now_ms + 10000;
    app_transfer.on_before_lock(ctx);
    assert(!ctx->request_return && s_view == TRANSFER_CLOUDZAO && test_cloud_cancels == 3);
    assert(!s_cloud_bind_status.code[0] && !s_cloud_poll_at && !s_cloud_requested && !s_cloud_status.busy);
    const unsigned starts_before_unlock = test_cloud_starts;
    ctx->now_ms += 10000;
    assert(on_tick(ctx) == APP_REDRAW_NONE && test_cloud_starts == starts_before_unlock);
    assert(on_key(ctx, UI_KEY_1) == APP_REDRAW_NONE && test_nav_index == 2 && !ctx->request_return);
    transfer_on_exit(ctx);
    test_nav_index = -1;
    cloud_back_regression(ctx);
    app_transfer_request_cloudzao();
    on_enter(ctx);
    tap(ctx, fetch);
    test_cloud_status = (cz_status_t){.busy = true};
    tap(ctx, fetch);
    assert(s_cloud_cancelling && on_tick(ctx) == APP_REDRAW_NONE);
    test_cloud_status = (cz_status_t){.error = CZ_CANCELLED};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !s_cloud_cancelling && !cloud_offer_count());
    tap(ctx, fetch);
    test_cloud_status = (cz_status_t){.error = CZ_SERVER, .preview_http_error = 503};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !cloud_offer_count());
    assert_cloud_text(ctx, "获取列表失败");
    assert_cloud_text(ctx, "云造服务返回 HTTP 503");
    tap(ctx, fetch);
    test_cloud_status = (cz_status_t){.error = CZ_SERVER};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !cloud_offer_count());
    assert_cloud_text(ctx, "连接或传输失败，可重试");
    tap(ctx, fetch);
    test_cloud_status = (cz_status_t){.error = CZ_TIMEOUT};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !cloud_offer_count());
    assert_cloud_text(ctx, "获取列表超时，可重试");
    tap(ctx, fetch);
    test_cloud_status = (cz_status_t){0};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && !cloud_offer_count());
    assert_cloud_text(ctx, "暂无待下载推送");
    tap(ctx, fetch);
    test_offer_rows = 4;
    test_cloud_status = (cz_status_t){.total = 4};
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_cloud_selectable == 3);
    tap(ctx, cloud_offer_rect(0));
    assert(s_cloud_checked == 1);
    tap(ctx, refresh);
    assert(test_cloud_job == CZ_JOB_PREVIEW && s_cloud_status.busy && !s_cloud_checked && !test_offer_rows);
    transfer_on_exit(ctx);
    unsigned starts = test_cloud_starts;
    on_enter(ctx);
    assert(s_view == TRANSFER_CLOUDZAO && s_cloud_panel == CLOUD_PANEL_TRANSFER);
    assert(!s_start_pending && !s_scan_pending && !s_usb_start_pending);
    assert(on_tick(ctx) == APP_REDRAW_NONE && test_cloud_starts == starts);
    transfer_on_exit(ctx);
    puts("CloudZao queue preview, selection, progress, cancellation, binding, back and lock: PASS");
}

static app_redraw_t cloud_swipe(app_ctx_t *ctx, ui_gesture_type_t type, uint16_t y) {
    const bool left = type == UI_GESTURE_SWIPE_L;
    const ui_gesture_event_t swipe = {.type = type, .x0 = left ? 550 : 135,
                                     .x = left ? 135 : 550, .y0 = y, .y = y};
    return on_gesture(ctx, &swipe);
}

static void cloud_notice_regression(app_ctx_t *ctx) {
    test_notice_acknowledged = false;
    const unsigned starts = test_cloud_starts, reads = test_history_reads;
    app_transfer_request_cloudzao();
    on_enter(ctx);
    assert(s_cloud_notice == CLOUD_NOTICE_FIRST_READ && app_transfer.modal_input_owned(ctx));
    assert_cloud_text(ctx, "我已了解");
    assert_cloud_text(ctx, "截至 2026 年 11 月 21 日实测");
    assert_cloud_text(ctx, "CloudZao App 仅能上传并推送 TXT、EPUB 原文件。");
    assert_cloud_text(ctx, "AZW3、MOBI 会被强制转换为 .ebook。");
    assert_cloud_text(ctx, "请通过 CloudZao 网页端上传并推送。");

    tap(ctx, (EpdRect){22, 65, 90, 83});
    tap(ctx, cloud_tab(CLOUD_PANEL_SETTINGS));
    tap(ctx, cloud_tab(CLOUD_PANEL_HISTORY));
    tap(ctx, cloud_button(CLOUD_BIND));
    tap(ctx, cloud_button(CLOUD_RECEIVE));
    const EpdRect action = cloud_notice_button();
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_L, action.y + 2) == APP_REDRAW_NONE);
    const ui_gesture_event_t release_inside = {.type = UI_GESTURE_TAP, .x0 = action.x - 1, .y0 = action.y + 2,
                                               .x = action.x + 2, .y = action.y + 2};
    assert(on_gesture(ctx, &release_inside) == APP_REDRAW_NONE);
    for (int key = UI_KEY_1; key <= UI_KEY_3; ++key) assert(on_key(ctx, key) == APP_REDRAW_NONE);
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    transfer_on_media_lost(ctx);
    cloud_before_lock(ctx);
    assert(s_view == TRANSFER_CLOUDZAO && s_cloud_notice == CLOUD_NOTICE_FIRST_READ);
    assert(s_cloud_panel == CLOUD_PANEL_TRANSFER && !test_notice_writes && !test_notice_acknowledged);
    assert(test_cloud_starts == starts && test_history_reads == reads && !ctx->request_return && !ctx->request_app);

    test_notice_save_error = true;
    tap(ctx, action);
    assert(s_cloud_notice == CLOUD_NOTICE_FIRST_READ && test_notice_writes == 1 && !test_notice_acknowledged);
    assert_cloud_text(ctx, "未能保存确认，请再试一次");
    transfer_on_exit(ctx);
    app_transfer_request_cloudzao();
    on_enter(ctx);
    assert(s_cloud_notice == CLOUD_NOTICE_FIRST_READ);

    test_notice_save_error = false;
    tap(ctx, action);
    assert(s_cloud_notice == CLOUD_NOTICE_HIDDEN && !app_transfer.modal_input_owned(ctx));
    assert(s_cloud_panel == CLOUD_PANEL_TRANSFER && test_notice_acknowledged && test_notice_writes == 2);
    transfer_on_exit(ctx);
    // 重新进入只读持久化状态；页面状态已由离开清除。/ Reentry reads only the persisted state after exit clears page state.
    app_transfer_request_cloudzao();
    on_enter(ctx);
    assert(s_cloud_notice == CLOUD_NOTICE_HIDDEN);
    tap(ctx, cloud_tab(CLOUD_PANEL_SETTINGS));
    assert_cloud_text(ctx, "注意事项");
    tap(ctx, cloud_notice_entry());
    assert(s_cloud_notice == CLOUD_NOTICE_REVIEW && app_transfer.modal_input_owned(ctx));
    assert_cloud_text(ctx, "截至 2026 年 11 月 21 日实测");
    assert_cloud_text(ctx, "mogo 暂不支持 .ebook，请使用 TXT 或 EPUB。");
    assert_cloud_text(ctx, "CloudZao App 暂时无法推送壁纸。");
    assert_cloud_text(ctx, "关闭");
    tap(ctx, action);
    assert(s_cloud_notice == CLOUD_NOTICE_HIDDEN && s_cloud_panel == CLOUD_PANEL_SETTINGS);
    tap(ctx, cloud_notice_entry());
    assert(on_key(ctx, UI_KEY_1) == APP_REDRAW_PAGE);
    assert(s_cloud_notice == CLOUD_NOTICE_HIDDEN && s_cloud_panel == CLOUD_PANEL_SETTINGS);
    assert(test_notice_writes == 2 && !ctx->request_return && !ctx->request_app);
    transfer_on_exit(ctx);
}

static void cloud_swipe_regression(app_ctx_t *ctx) {
    app_transfer_request_cloudzao();
    on_enter(ctx);
    const unsigned reads = test_history_reads, cancels = test_cloud_cancels, starts = test_cloud_starts;
    const uint16_t tab_y = cloud_tab(CLOUD_PANEL_TRANSFER).y + 2, body_y = 450;
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_R, body_y) == APP_REDRAW_NONE);
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_L, 110) == APP_REDRAW_NONE);
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_L, UI_NAV_TOP) == APP_REDRAW_NONE);
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_U, body_y) == APP_REDRAW_NONE);
    assert(s_cloud_panel == CLOUD_PANEL_TRANSFER && test_history_reads == reads);

    s_cloud_status.busy = true;
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_L, body_y) == APP_REDRAW_NONE);
    assert(s_cloud_panel == CLOUD_PANEL_TRANSFER && test_history_reads == reads);
    s_cloud_status.busy = false;
    s_cloud_history_page = 2;
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_L, tab_y) == APP_REDRAW_PAGE);
    assert(s_cloud_panel == CLOUD_PANEL_HISTORY && s_cloud_history.busy && !s_cloud_history_page);
    assert(test_history_reads == reads + 1);
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_L, body_y) == APP_REDRAW_PAGE);
    assert(s_cloud_panel == CLOUD_PANEL_SETTINGS && test_cloud_cancels == cancels + 1);
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_L, body_y) == APP_REDRAW_NONE);

    s_cloud_bind_status.busy = true;
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_R, body_y) == APP_REDRAW_NONE);
    assert(s_cloud_panel == CLOUD_PANEL_SETTINGS && test_history_reads == reads + 1);
    s_cloud_bind_status.busy = false;
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_R, body_y) == APP_REDRAW_PAGE);
    assert(s_cloud_panel == CLOUD_PANEL_HISTORY && test_history_reads == reads + 2);
    test_history_status = (cz_history_status_t){0};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_R, body_y) == APP_REDRAW_PAGE);
    assert(s_cloud_panel == CLOUD_PANEL_TRANSFER && test_cloud_cancels == cancels + 1);
    assert(test_cloud_starts == starts && !ctx->request_return);
    transfer_on_exit(ctx);
    puts("CloudZao tab swipes: PASS");
}

static void assert_history_text(app_ctx_t *ctx, const char *text) {
    test_expected_cloud_text = text;
    test_cloud_text_drawn = false;
    test_observe_text = observe_cloud_text;
    draw_cloud(ctx->fb);
    test_observe_text = NULL;
    assert(test_cloud_text_drawn);
}

static void cloud_history_regression(app_ctx_t *ctx) {
    assert(!strcmp(cloud_history_format("novel.EPUB"), "EPUB"));
    assert(!strcmp(cloud_history_format("notes.txt"), "TXT"));
    assert(!strcmp(cloud_history_format("font.ttf"), "TTF"));
    assert(!strcmp(cloud_history_format("image.png"), "PNG"));
    assert(!strcmp(cloud_history_format("no-extension"), "文件"));
    app_transfer_request_cloudzao();
    on_enter(ctx);
    tap(ctx, cloud_tab(CLOUD_PANEL_HISTORY));
    assert_history_text(ctx, "正在读取记录");
    test_history_status = (cz_history_status_t){.count = 8};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    const EpdRect next = cloud_pager(true, true), prev = cloud_pager(true, false);
    assert(next.y + next.height <= UI_NAV_TOP);
    tap(ctx, prev);
    test_history_drawn = 0;
    assert_history_text(ctx, "8 条");
    assert(s_cloud_history_page == 0 && test_history_first == 0 && test_history_drawn == CLOUD_HISTORY_PAGE_ROWS);
    tap(ctx, next);
    tap(ctx, next);
    test_history_drawn = 0;
    assert_history_text(ctx, "2 / 2");
    assert(s_cloud_history_page == 1 && test_history_first == CLOUD_HISTORY_PAGE_ROWS && test_history_drawn == 2);
    tap(ctx, prev);
    assert(s_cloud_history_page == 0);
    assert_history_text(ctx, "时间未校准 · 1.5 KiB");
    test_history_drawn = 0;
    s_cloud_history = (cz_history_status_t){0};
    assert_history_text(ctx, "还没有接收记录");
    s_cloud_history_page = 1;
    s_cloud_history.error = CZ_MEDIA;
    tap(ctx, prev);
    assert(s_cloud_history_page == 1);
    assert_history_text(ctx, "请插入已挂载的 TF 卡");
    s_cloud_history.error = CZ_STORAGE;
    assert_history_text(ctx, "请检查 TF 卡，再重新打开历史记录");
    assert(test_history_drawn == 0);
    transfer_on_exit(ctx);
    puts("CloudZao history layout states and pager: PASS");
}

static void cloud_history_clear_regression(app_ctx_t *ctx) {
    app_transfer_request_cloudzao();
    on_enter(ctx);
    ctx->request_return = false;
    const EpdRect history = cloud_tab(CLOUD_PANEL_HISTORY), clear = cloud_history_clear_button();
    const EpdRect confirm = cloud_history_confirm_button(true), cancel = cloud_history_confirm_button(false);
    tap(ctx, history);
    tap(ctx, clear);
    assert(!s_cloud_history_confirm && !test_history_clears);
    test_history_status = (cz_history_status_t){.count = 8};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    tap(ctx, cloud_pager(true, true));
    assert_history_text(ctx, "清除");
    tap(ctx, (EpdRect){520, 279, 128, 47});
    tap(ctx, (EpdRect){36, 279, 464, 47});
    assert(!s_cloud_history_confirm && !test_history_clears);
    tap(ctx, clear);
    assert(s_cloud_history_confirm && !test_history_clears);
    assert(transfer_zone(ctx, 342, 550) == APP_HORIZONTAL_BLOCK);
    assert(app_transfer.edge_back(ctx).redraw == APP_REDRAW_PAGE && !s_cloud_history_confirm && !test_history_clears);
    tap(ctx, clear);
    assert_history_text(ctx, "仅清除当前 TF 卡的接收记录");
    tap(ctx, clear);
    assert(s_cloud_history_confirm && !test_history_clears);
    tap(ctx, cancel);
    assert(!s_cloud_history_confirm && !test_history_clears && s_cloud_history_page == 1);
    tap(ctx, clear);
    tap(ctx, (EpdRect){22, 65, 90, 83});
    assert(!s_cloud_history_confirm && !ctx->request_return && !test_history_clears);
    tap(ctx, clear);
    assert(on_key(ctx, UI_KEY_1) == APP_REDRAW_PAGE);
    assert(!s_cloud_history_confirm && !ctx->request_return && !test_history_clears);

    tap(ctx, clear);
    assert(cloud_swipe(ctx, UI_GESTURE_SWIPE_L, confirm.y + 10) == APP_REDRAW_PAGE);
    assert(s_cloud_panel == CLOUD_PANEL_SETTINGS && !s_cloud_history_confirm && !test_history_clears);
    tap(ctx, history);
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    tap(ctx, clear);
    transfer_on_media_lost(ctx);
    tap(ctx, confirm);
    assert(!s_cloud_history_confirm && !test_history_clears && s_cloud_history.error == CZ_MEDIA);
    tap(ctx, cloud_tab(CLOUD_PANEL_SETTINGS));
    tap(ctx, history);
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    tap(ctx, clear);
    cloud_before_lock(ctx);
    assert(!s_cloud_history_confirm && !test_history_clears && s_cloud_panel == CLOUD_PANEL_TRANSFER);
    tap(ctx, history);
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    tap(ctx, cloud_pager(true, true));

    test_history_clear_start_error = CZ_RESOURCES;
    tap(ctx, clear);
    tap(ctx, confirm);
    assert_history_text(ctx, "8 条 · 清除失败，可重试");
    assert(test_history_clears == 1 && !s_cloud_history.busy && s_cloud_history.count == 8);
    test_history_clear_start_error = CZ_OK;
    tap(ctx, clear);
    tap(ctx, confirm);
    tap(ctx, confirm);
    assert(test_history_clears == 2 && s_cloud_history.busy && s_cloud_history_clearing);
    assert_history_text(ctx, "正在清除记录");
    assert(on_tick(ctx) == APP_REDRAW_NONE);
    test_history_status = (cz_history_status_t){.count = 8, .error = CZ_STORAGE};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    test_history_drawn = 0;
    assert_history_text(ctx, "8 条 · 清除失败，可重试");
    assert(test_history_drawn == 2 && s_cloud_history_page == 1);
    tap(ctx, clear);
    tap(ctx, confirm);
    assert(test_history_clears == 3);
    test_history_status = (cz_history_status_t){0};
    assert(on_tick(ctx) == APP_REDRAW_PAGE);
    assert_history_text(ctx, "还没有接收记录");
    tap(ctx, clear);
    assert(!s_cloud_history_confirm && !s_cloud_history_page && !s_cloud_history.count && test_history_clears == 3);
    transfer_on_exit(ctx);
    puts("CloudZao history clear confirmation, dismissal, failure and success: PASS");
}
#endif

static void qr_regression(app_ctx_t* ctx) {
    test_status = (wifi_transfer_status_t){.mode=WIFI_TRANSFER_MODE_AP,.state=WIFI_TRANSFER_READY,.network_ready=true};
    strcpy(test_status.ssid,"ReadPico-test");
    strcpy(test_status.url,"http://192.168.4.1");
    app_transfer_request_hotspot_start();
    on_enter(ctx);
    s_qr_url = true;
    assert(on_tick(ctx) == APP_REDRAW_PAGE && s_owned_session);
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(s_qr_ready && s_qr_url && !strcmp(test_qr_payload,test_status.url));
    tap(ctx, control_rect(0));
    assert(!s_qr_url && !strcmp(test_qr_payload,test_status.ssid));
    tap(ctx, control_rect(1));
    assert(s_qr_url && !strcmp(test_qr_payload,test_status.url));
    int encodes = test_qr_encodes;
    render(ctx,ctx->fb);
    assert(test_qr_encodes == encodes);
    test_status.cur_bytes++;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(test_qr_encodes == encodes);
    strcpy(test_status.url,"http://192.168.4.2");
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!strcmp(test_qr_payload,test_status.url));
    test_status.network_ready=false;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!s_qr_ready && !test_qr_payload[0]);
    encodes=test_qr_encodes;
    render(ctx,ctx->fb);
    assert(test_qr_encodes==encodes);
    test_status.network_ready=true;
    test_qr_failure=true;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!s_qr_ready);
    test_qr_failure=false;
    queue_network_start(WIFI_TRANSFER_MODE_STA);
    assert(!s_qr_ready && !test_qr_payload[0]);
    test_status.mode=WIFI_TRANSFER_MODE_STA;
    strcpy(test_status.url,"http://10.20.30.40");
    on_tick(ctx);
    assert(s_qr_url && s_qr_ready && !strcmp(test_qr_payload,test_status.url));
    encodes=test_qr_encodes;
    render(ctx,ctx->fb);
    assert(test_qr_encodes==encodes);
    stop_session();
    assert(!s_qr_ready && !test_qr_payload[0]);
    test_status=(wifi_transfer_status_t){0};
    s_mode=WIFI_TRANSFER_MODE_AP;
    test_qr_encodes=0;
    ctx->now_ms=0;
}
// 页面标记可能因旧会话重连失败而丢失，确认遗忘仍须释放实际射频并取消排队连接。
// A failed restart can lose the page's session flag; forgetting must still stop the radio and queued reconnect.
static void wifi_recovery_regression(app_ctx_t* ctx) {
    test_configured = s_saved_configured = true;
    s_view = TRANSFER_NETWORKS;
    s_wifi_settings_only = true;
    s_session_started = false;
    s_start_pending = s_network_connect_pending = s_scan_pending = false;
    test_status = s_status = (wifi_transfer_status_t){.mode=WIFI_TRANSFER_MODE_STA,
        .state=WIFI_TRANSFER_ERROR,.wifi_configured=true};
    test_wifi_running = true;
    int stops = test_stop_count;
    tap(ctx, network_control_rect(6));
    assert(!test_wifi_running && test_stop_count > stops && s_network_connect_pending);
    network_ui_tick(ctx);
    assert(s_session_started && !s_network_connect_pending);
    // 上传中不能遗忘；停止后可以重试。/ Forgetting waits for an active upload, then remains retryable.
    test_wifi_running = true;
    s_session_started = false;
    s_network_connect_pending = true;
    tap(ctx, network_control_rect(11));
    test_busy = true;
    stops = test_stop_count;
    int forgets = test_forget_count;
    tap(ctx, network_control_rect(11));
    assert(s_forget_confirm && test_wifi_running && test_stop_count == stops && test_forget_count == forgets);
    test_busy = false;
    tap(ctx, network_control_rect(11));
    assert(!s_forget_confirm && !test_wifi_running && !test_configured && !s_saved_configured);
    assert(!s_start_pending && !s_network_connect_pending && !s_scan_pending);
    network_ui_tick(ctx);
    assert(!s_session_started && !s_network_connect_pending);
    // 遗忘后重新选择同名网络会进入密码输入。/ Selecting the forgotten network asks for a password again.
    s_network_page = 0;
    tap(ctx, network_control_rect(0));
    assert(s_view == TRANSFER_PASSWORD && !s_password[0]);
    clear_password();
    s_wifi_settings_only = false;
    s_forget_confirm = false;
    test_status = (wifi_transfer_status_t){0};
}
void (*test_observe_text)(const char *text);
void (*test_observe_pixel)(int x, int y, uint8_t gray);
static bool test_password_hint_drawn, test_password_error_drawn;
static void observe_password_text(const char *text) {
    assert(strcmp(text, "输入完成后连接，密码只保存在本机"));
    assert(strcmp(text, "轻点输入，可切换大小写"));
    assert(!strstr(text, "/64"));
    if (!strcmp(text, "支持英文、数字、符号和空格")) test_password_hint_drawn = true;
    if (!strcmp(text, "加密网络密码至少 8 个字符")) test_password_error_drawn = true;
}
static void password_caret_regression(app_ctx_t *ctx) {
    clear_password();ui_keyboard_begin(&s_password_input,true);
    assert(s_password_input.cursor==0);
    tap(ctx,(EpdRect){24,1010,30,30}); // 数字页。/ Number panel.
    for(int i=0;i<8;i++)tap(ctx,(EpdRect){24+i*64,740,30,30});
    assert(!strcmp(s_password,"12345678")&&s_password_input.cursor==8);
    EpdRect field=password_control_rect(47);
    assert(s_area.y == field.y && !s_input_layout);
    tap(ctx,(EpdRect){field.x+17+3*20-2,329,4,4});assert(s_password_input.cursor==3);
    tap(ctx,(EpdRect){24+64,740,30,30});assert(!strcmp(s_password,"123245678")&&s_password_input.cursor==4);
    tap(ctx,password_control_rect(45));assert(s_password_visible&&s_password_input.cursor==4);
    tap(ctx,password_control_rect(45));assert(!s_password_visible&&s_password_input.cursor==4);
    ui_text_edit_place(&s_password_input,3);
    tap(ctx,(EpdRect){600,896,30,30});assert(!strcmp(s_password,"12245678")&&s_password_input.cursor==2);
    ui_text_edit_place(&s_password_input,0);tap(ctx,(EpdRect){600,896,30,30});assert(!strcmp(s_password,"12245678"));
    clear_password();assert(!s_password[0]&&!s_password_input.cursor&&!s_password_visible);
    ui_keyboard_begin(&s_password_input,true);
    strcpy(s_password,"abcdefgh");ui_text_edit_init(&s_password_input,s_password,sizeof(s_password));
    cst836u_touch_t touch={.touched=true,.count=1,.x=610,.y=930};ctx->touch=&touch;ctx->now_ms=1000;
    ui_gesture_event_t held={.type=UI_GESTURE_PRESS,.x0=610,.y0=930,.x=610,.y=930};
    assert(on_gesture(ctx,&held)==APP_REDRAW_AREA && !s_input_layout);
    ctx->consumed=true;assert(on_tick(ctx)==APP_REDRAW_NONE);ctx->consumed=false;
    ctx->now_ms=1499;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdefgh"));
    ctx->now_ms=1500;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcdefg"));assert(!s_input_layout);
    ctx->now_ms=1620;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcdef"));
    touch.count=2;ctx->now_ms=1740;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdef"));
    touch.count=1;ctx->now_ms=2000;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdef"));
    held.type=UI_GESTURE_PRESS;on_gesture(ctx,&held);ctx->now_ms=2500;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcde"));
    held.type=UI_GESTURE_CANCEL;touch.touched=false;on_gesture(ctx,&held);ctx->now_ms=2800;
    assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcde"));ctx->touch=NULL;ctx->now_ms=0;
    clear_password();ui_keyboard_begin(&s_password_input,true);
}

static void multi_network_regression(app_ctx_t *ctx) {
    test_multi_profiles = true; test_profile_count = WIFI_TRANSFER_SAVED_MAX;
    test_status = (wifi_transfer_status_t){0}; test_busy = test_wifi_running = false;
    test_scan_count = 8; test_scan_error = test_forget_error = ESP_OK;
    for (size_t i = 0; i < test_profile_count; ++i) {
        test_profiles[i].requires_password = true;
        snprintf(test_profiles[i].ssid, 33, "saved-%u", (unsigned)i);
    }
    strcpy(test_profiles[0].ssid, "中文家庭网络");
    app_transfer_request_wifi_setup(); on_enter(ctx); network_ui_tick(ctx);
    assert(s_saved_count == WIFI_TRANSFER_SAVED_MAX && !s_show_saved);
    for (int i = 0; i < NETWORK_ROWS; ++i) {
        EpdRect row = network_control_rect(i);
        assert(row.y == 442 + 88 * i && row.y + row.height <= 970);
        assert(provisioning_hit(row.x, row.y) == i);
        assert(provisioning_hit(row.x + row.width, row.y) != i);
    }
    unsigned left = test_icons[UI_ICON_CHEVRON_LEFT], right = test_icons[UI_ICON_CHEVRON_RIGHT];
    draw_networks(ctx->fb);
    assert(test_icons[UI_ICON_CHEVRON_LEFT] > left && test_icons[UI_ICON_CHEVRON_RIGHT] > right);
    assert(provisioning_hit(40, 991) == -1);
    tap(ctx, network_control_rect(7)); assert(s_network_page == 0);
    int saves = test_save_count, selects = test_selected_count;
    tap(ctx, network_control_rect(0));
    assert(test_selected_count == selects + 1 && test_save_count == saves && s_network_connect_pending);
    assert(s_view == TRANSFER_NETWORKS && !s_password[0]);
    network_ui_tick(ctx);
    // 存满时不覆盖其他网络；改密码与遗忘仍可用。/ A full store never evicts profiles; edit and forget remain available.
    tap(ctx, network_control_rect(1));
    assert(s_view == TRANSFER_NETWORKS && strstr(s_network_message, "8") && test_save_count == saves);
    assert(transfer_zone(ctx, 342, 550) == APP_HORIZONTAL_TABS);
    assert(app_transfer.step_tab(ctx,-1) == APP_REDRAW_NONE && !s_show_saved);
    assert(app_transfer.step_tab(ctx,1) == APP_REDRAW_PAGE && s_show_saved);
    assert(app_transfer.step_tab(ctx,1) == APP_REDRAW_NONE && s_show_saved);
    tap(ctx, network_control_rect(16)); assert(s_show_saved && s_saved_count == 8);
    tap(ctx, network_control_rect(9)); assert(s_network_page == 1);
    tap(ctx, network_control_rect(9)); assert(s_network_page == 1);
    tap(ctx, network_control_rect(0)); assert(s_saved_detail && !strcmp(s_selected.ssid, "saved-6"));
    ctx->request_return = false;
    tap(ctx, (EpdRect){36, 79, 50, 50}); assert(!s_saved_detail && !ctx->request_return && s_show_saved);
    tap(ctx, network_control_rect(0)); tap(ctx, network_control_rect(11));
    assert(s_forget_confirm && !strcmp(s_forget_ssid, "saved-6"));
    tap(ctx, network_control_rect(12)); assert(s_saved_detail && !s_forget_confirm);
    tap(ctx, network_control_rect(11)); tap(ctx, network_control_rect(11));
    assert(!s_saved_detail && !s_forget_confirm && s_saved_count == 7 && s_saved_configured);
    assert(!strcmp(s_saved_ssid, "中文家庭网络") && saved_network_index("saved-6") < 0);
    tap(ctx, network_control_rect(0)); assert(s_saved_detail && !strcmp(s_selected.ssid, "saved-7"));
    tap(ctx, network_control_rect(14)); assert(s_view == TRANSFER_PASSWORD && !s_password[0]);
    strcpy(s_password, "discarded"); s_password_visible = true;
    app_back_result_t back = app_transfer.edge_back(ctx);
    assert(back.accepted && back.redraw == APP_REDRAW_PAGE && s_view == TRANSFER_NETWORKS && s_saved_detail);
    assert(!strcmp(s_selected.ssid, "saved-7") && s_network_page == 1 && !s_password[0] && !s_password_visible);
    tap(ctx, network_control_rect(14)); tap(ctx, password_control_rect(44));
    assert(s_view == TRANSFER_NETWORKS && s_show_saved && s_saved_detail);
    assert(on_key(ctx, UI_KEY_1) == APP_REDRAW_PAGE && !s_saved_detail && !ctx->request_return);
    tap(ctx, network_control_rect(15)); assert(!s_show_saved && !s_network_page);
    tap(ctx, network_control_rect(1)); assert(s_view == TRANSFER_PASSWORD);
    back = app_transfer.edge_back(ctx);
    assert(back.accepted && s_view == TRANSFER_NETWORKS && !s_saved_detail && !s_show_saved);
    test_multi_profiles = false; s_wifi_settings_only = false;
    s_view = TRANSFER_NETWORKS;
}

static void transfer_entry_regression(app_ctx_t *ctx) {
    const EpdRect back = {36, 79, 50, 50};
    app_transfer_request_wifi_setup();
    on_enter(ctx);
    assert(s_wifi_settings_only && s_view == TRANSFER_NETWORKS && s_scan_pending);

    // TF 失效不能打断扫描或清除正在编辑的密码。
    // Media loss must not interrupt scanning or erase a password being edited.
    transfer_on_media_lost(ctx);
    assert(s_view == TRANSFER_NETWORKS && s_scan_pending && !s_media_lost);
    network_ui_tick(ctx);
    tap(ctx, network_control_rect(0));
    assert(s_view == TRANSFER_PASSWORD);
    tap(ctx, (EpdRect){24,740,30,30});
    assert(strlen(s_password) == 1);
    transfer_on_media_lost(ctx);
    assert(s_view == TRANSFER_PASSWORD && strlen(s_password) == 1);
    tap(ctx, back);
    assert(s_view == TRANSFER_NETWORKS && !s_password[0] && !ctx->request_return);

    // 快捷栏先提交重进请求，再退出旧页；返回历史则没有新请求。
    // Quick re-entry queues before exiting; history return has no new request.
    app_transfer_request_wifi_setup();
    transfer_on_exit(ctx);
    on_enter(ctx);
    assert(s_wifi_settings_only && s_entry_requested == TRANSFER_ENTRY_NONE);
    tap(ctx, back);
    assert(ctx->request_return && s_view == TRANSFER_NETWORKS);
    transfer_on_exit(ctx);
    ctx->request_return = false;
    on_enter(ctx);
    assert(s_wifi_settings_only && s_view == TRANSFER_NETWORKS && !s_start_pending);
    assert(on_key(ctx, UI_KEY_1) == APP_REDRAW_NONE && ctx->request_return);
    transfer_on_exit(ctx);
    ctx->request_return = false;

    // 恢复热点页不能重启服务或复用无关 STA 的地址和二维码。
    // Restoring Hotspot must neither restart it nor reuse an unrelated STA's URL or QR.
    app_transfer_request_hotspot_start();
    on_enter(ctx);
    assert(s_start_pending);
    transfer_on_exit(ctx);
    test_wifi_running = true;
    test_status = (wifi_transfer_status_t){.state=WIFI_TRANSFER_READY,.network_ready=true,
        .network_only=true,.mode=WIFI_TRANSFER_MODE_STA,.session_id=999};
    strcpy(test_status.url, "http://192.0.2.1");
    const int stops = test_stop_count;
    on_enter(ctx);
    assert(s_view == TRANSFER_HOME && s_mode == WIFI_TRANSFER_MODE_AP && !s_start_pending);
    ctx->now_ms += 3000;
    assert(on_tick(ctx) == APP_REDRAW_NONE && !s_status.network_ready && !s_qr_ready);
    transfer_on_media_lost(ctx);
    assert(!s_media_lost && s_view == TRANSFER_HOME);
    transfer_on_exit(ctx);
    app_transfer_request_wifi_setup();
    on_enter(ctx);
    assert(s_wifi_settings_only && s_view == TRANSFER_NETWORKS && s_status.network_ready);
    app_transfer_request_wifi_setup();
    transfer_on_exit(ctx);
    on_enter(ctx);
    assert(s_wifi_settings_only && !s_scan_pending && s_status.network_ready);
    transfer_on_media_lost(ctx);
    transfer_on_exit(ctx);
    assert(test_stop_count == stops && test_wifi_running && test_status.network_ready);
    app_transfer_open_setup_wifi(ctx);
    assert(ctx->request_app == &app_transfer); ctx->request_app = NULL;
    on_enter(ctx);
    assert(transfer_modal(ctx) && !ctx->request_return);
    network_ui_tick(ctx);
    assert(ctx->request_return); ctx->request_return = false;
    transfer_on_exit(ctx);
    assert(app_transfer_take_setup_wifi_success() && !app_transfer_take_setup_wifi_success());
    assert(test_stop_count == stops && test_wifi_running);
    app_transfer_open_setup_wifi(ctx); ctx->request_app = NULL;
    on_enter(ctx);
    tap(ctx, back);
    assert(ctx->request_return); ctx->request_return = false;
    transfer_on_exit(ctx);
    assert(!app_transfer_take_setup_wifi_success());
    test_wifi_running = false;
    test_status = (wifi_transfer_status_t){0};
    ctx->now_ms = 0;
}

int main(void) {
    assert(transfer_signature_valid("",0)&&transfer_signature_valid("中文🙂",10));
    assert(!transfer_signature_valid("a\0b",3)&&!transfer_signature_valid("\xed\xa0\x80",3));
    assert(!transfer_signature_valid("\xc0\xaf",2)&&!transfer_signature_valid("\xf4\x90\x80\x80",4));
    assert(!transfer_signature_valid("\xe4\xb8",2)&&!transfer_signature_valid("a\nb",3));
    strcpy(test_signature,"reader");char signature[96];assert(transfer_signature_get(signature,sizeof(signature))&&!strcmp(signature,"reader"));
    test_signature_fail=true;assert(transfer_signature_set("新签名")==ESP_FAIL&&!strcmp(test_signature,"reader"));
    test_signature_fail=false;assert(transfer_signature_set("新签名")==ESP_OK&&!strcmp(test_signature,"新签名"));
    assert(s_signature_revision==1);s_signature_seen=s_signature_revision;
    uint8_t fb = 0;
    app_ctx_t ctx = {.fb = &fb};
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    cloud_header_regression(&ctx);
    cloud_direct_entry_regression(&ctx);
    cloud_swipe_regression(&ctx);
    cloud_history_regression(&ctx);
    cloud_history_clear_regression(&ctx);
    cloud_notice_regression(&ctx);
#endif
    qr_regression(&ctx);
    test_configured = true;
    app_transfer_request_wifi_upload();
    on_enter(&ctx);
    assert(s_view == TRANSFER_HOME && s_start_pending && !s_scan_pending);
    assert(s_mode == WIFI_TRANSFER_MODE_STA);
    on_tick(&ctx);
    tap(&ctx, control_rect(2));
    assert(s_view == TRANSFER_HOME && !s_start_pending && ctx.request_return);
    transfer_on_exit(&ctx);
    ctx.request_return = false;
    test_configured = false;
    for (int i = 44; i < 48; ++i) {
        EpdRect r = password_control_rect(i);
        assert(r.x >= 0 && r.x + r.width <= UI_LOCK_WIDTH);
        assert(r.y >= 0 && r.y + r.height <= UI_LOCK_HEIGHT);
    }
    app_transfer_request_wifi_upload();
    on_enter(&ctx);
    assert(s_view == TRANSFER_NETWORKS && s_scan_pending && !s_start_pending);
    network_ui_tick(&ctx);
    assert(s_network_count == 8 && !s_scan_pending);
    test_configured = s_saved_configured = true;
    tap(&ctx, (EpdRect){540, UI_NAV_TOP + 16, 20, 20});
    assert(test_nav_index == 3);
    test_nav_index = -1;
    assert(s_view == TRANSFER_NETWORKS);
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 0);
    tap(&ctx, network_control_rect(12));
    assert(test_forget_count == 0 && s_saved_configured && !s_forget_confirm);
    tap(&ctx, network_control_rect(11));
    test_forget_error = ESP_FAIL;
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 1 && s_saved_configured && s_forget_confirm);
    test_forget_error = ESP_OK;
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 2 && !s_saved_configured);
    test_configured = true;
    enter_networks();
    network_ui_tick(&ctx);
    tap(&ctx, network_control_rect(9));
    assert(s_network_page == 1);
    tap(&ctx, network_control_rect(7));
    assert(s_network_page == 0);
    // 断开保留凭据，上传期间拒绝断开，之后仍可重连。
    // Disconnect keeps credentials, refuses an active upload and allows reconnecting.
    test_status.network_ready = s_status.network_ready = true;
    s_session_started = true;
    test_busy = true;
    int stops = test_stop_count;
    tap(&ctx, network_control_rect(6));
    assert(test_stop_count == stops && s_status.network_ready);
    test_busy = false;
    tap(&ctx, network_control_rect(6));
    assert(test_stop_count > stops && !s_session_started && s_saved_configured && test_configured);
    assert(!s_start_pending && !s_network_connect_pending && test_forget_count == 2);
    test_status.network_ready = s_status.network_ready = false;
    tap(&ctx, network_control_rect(6));
    assert(s_start_pending && s_mode == WIFI_TRANSFER_MODE_STA);
    s_start_pending = false; s_view = TRANSFER_NETWORKS;
    tap(&ctx, network_control_rect(9));
    assert(s_network_page == 1);
    tap(&ctx, network_control_rect(1));
    assert(s_view == TRANSFER_PASSWORD && !strcmp(s_selected.ssid, "network7"));
    s_view = TRANSFER_NETWORKS; s_network_page = 0;
    tap(&ctx, network_control_rect(0));
    assert(s_view == TRANSFER_PASSWORD && !s_password[0]);
    test_observe_text = observe_password_text;
    draw_password(ctx.fb);
    assert(test_password_hint_drawn);
    tap(&ctx, password_control_rect(46));
    assert(test_save_count == 0 && s_view == TRANSFER_PASSWORD && test_password_error_drawn);
    password_caret_regression(&ctx);
    test_observe_text = NULL;
    ui_gesture_event_t cancel = {.type = UI_GESTURE_PRESS,.x0 = 42,.y0 = 422,.x = 42,.y = 422};
    on_gesture(&ctx, &cancel);
    cancel.type = UI_GESTURE_TAP;
    cancel.x = 650;
    on_gesture(&ctx, &cancel);
    assert(!s_password[0]);
    // 容量及保存失败时保留密码。/ Capacity and preservation after failed saving.
    tap(&ctx,(EpdRect){24,1010,30,30});
    for(int i=0;i<70;i++)tap(&ctx,(EpdRect){24,740,30,30});assert(strlen(s_password)==64);
    tap(&ctx,(EpdRect){600,896,30,30});assert(strlen(s_password)==63);
    tap(&ctx, password_control_rect(45));
    assert(s_password_visible);
    test_save_error = ESP_FAIL;
    tap(&ctx, password_control_rect(46));
    assert(s_view == TRANSFER_PASSWORD && strlen(s_password) == 63);
    test_save_error = ESP_OK;
    tap(&ctx, password_control_rect(46));
    assert(s_view == TRANSFER_HOME && s_mode == WIFI_TRANSFER_MODE_STA && s_start_pending);
    assert(!s_password[0]);
    assert(strcmp(test_saved_ssid, "中文家庭网络") == 0);
    enter_networks();
    network_ui_tick(&ctx);
    tap(&ctx, network_control_rect(0));
    tap(&ctx,(EpdRect){24,740,30,30});
    tap(&ctx, password_control_rect(44));
    assert(s_view == TRANSFER_NETWORKS && !s_password[0]);
    test_scan_count = 0;
    tap(&ctx, network_control_rect(8));
    network_ui_tick(&ctx);
    assert(!s_network_count && s_network_message[0]);
    test_scan_error = ESP_FAIL;
    tap(&ctx, network_control_rect(8));
    network_ui_tick(&ctx);
    assert(!s_network_count && s_network_message[0]);
    tap(&ctx, network_control_rect(6));
    assert(s_view == TRANSFER_HOME && s_start_pending && s_mode == WIFI_TRANSFER_MODE_STA);
    int stops_before = test_stop_count;
    tap(&ctx, control_rect(2));
    assert(test_stop_count == stops_before && !s_start_pending);
    assert(s_view == TRANSFER_HOME && ctx.request_return && ctx.request_app == NULL);
    transfer_on_exit(&ctx);
    ctx.request_return = false;
    app_transfer_request_hotspot_start();
    on_enter(&ctx);
    on_tick(&ctx);
    test_busy = true;
    stops_before = test_stop_count;
    tap(&ctx, control_rect(2));
    assert(test_stop_count == stops_before && s_view == TRANSFER_HOME && !ctx.request_return);
    tap(&ctx, (EpdRect){540, UI_NAV_TOP + 16, 20, 20});
    assert(test_stop_count == stops_before && test_nav_index == -1);
    test_busy = false;
    test_status.changed_count = 1;
    ctx.now_ms = 3000;
    assert(on_tick(&ctx) == APP_REDRAW_AREA);
    assert(s_status.changed_count == 1);
    s_session_started = true;
    transfer_on_exit(&ctx);
    assert(test_store_changes == 1);
    app_transfer_request_hotspot_start();
    on_enter(&ctx);
    on_tick(&ctx);
    s_root.is_flash = false;
    s_free = 123456;
    test_status.state = WIFI_TRANSFER_UPLOADING;
    test_status.network_ready = true;
    stops_before = test_stop_count;
    app_transfer.on_media_lost(&ctx);
    assert(test_stop_count == stops_before + 1);
    assert(s_media_lost && !s_free && !s_qr_ready && !s_root.path[0]);
    assert(!s_start_pending && !s_session_started && s_view == TRANSFER_HOME);
    assert(s_status.state == WIFI_TRANSFER_STOPPED && !s_status.network_ready);
    s_root.is_flash = true;
    app_transfer.on_media_lost(&ctx);
    assert(test_stop_count == stops_before + 1);
    transfer_on_exit(&ctx);
    test_status = (wifi_transfer_status_t){0};
    ctx.request_return = false;
    test_configured = true;
    app_transfer_request_wifi_upload();
    on_enter(&ctx);
    assert(s_view == TRANSFER_HOME && s_start_pending);
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && s_view == TRANSFER_HOME);
    ctx.request_return = false;
    app_transfer_request_hotspot_start();
    on_enter(&ctx);
    assert(s_view == TRANSFER_HOME && s_start_pending);
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && s_view == TRANSFER_HOME);
    ctx.request_return = false;
    app_transfer_request_usb_start();
    on_enter(&ctx);
    assert(s_usb_start_pending && !test_usb_active);
    assert(on_tick(&ctx) == APP_REDRAW_PAGE);
    assert(s_usb_entry && s_view == TRANSFER_HOME && test_usb_active);
    test_usb_stop_error = ESP_FAIL;
    tap(&ctx, (EpdRect){36, 79, 50, 50});
    assert(test_usb_active && !ctx.request_return && s_view == TRANSFER_HOME);
    assert(on_key(&ctx, UI_KEY_2) == APP_REDRAW_PAGE && !ctx.request_app);
    assert(test_usb_active && s_usb_message[0]);
    test_usb_stop_error = ESP_OK;
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && !test_usb_active);
    transfer_on_exit(&ctx);
    ctx.request_return = false;
    on_enter(&ctx);
    assert(s_usb_entry && !s_usb_start_pending);
    assert(on_tick(&ctx) == APP_REDRAW_NONE && !test_usb_active);
    transfer_on_exit(&ctx);
    app_transfer_request_wifi_upload();
    on_enter(&ctx);
    enter_networks();
    test_scan_count = 8; test_scan_error = ESP_OK;
    network_ui_tick(&ctx);
    wifi_recovery_regression(&ctx);
    multi_network_regression(&ctx);
    transfer_entry_regression(&ctx);
    puts("transfer_ui_host_test: PASS");
}
