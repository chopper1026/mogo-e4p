/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 传书页负责两种浏览器接入模式生命周期、书源注入与状态展示；HTTP和文件接收属于独立组件。
 * Transfer page owns both browser modes, storage injection and status; the component receives files.
 * CloudZao 使用独立接收入口，不将云端客户端归为浏览器传书模式。
 * CloudZao has a separate receive entry and is not a browser transfer mode.
 *
 * 冻结：按用户双模式要求提供热点与已有WiFi；离开传书时停服，配网后已连接的 STA 保持在线；深睡/断电会中断传输。
 * render只画快照；内置存储单文件上限由book_store提供，不格式化TF卡。
 * 上传并发扫描曾欠载，页内增加预填余量；离页停服后恢复，不改波形或像素时钟。
 * 用户要求设备端配网：停服后扫描选网，ASCII密码只在连接保存时写入，离开输入页即清空。
 * 用户要求输入光标：密码框常亮光标可轻点定位，显示/隐藏保留位置，离页一并清空。
 * 用户授权新输入法：WiFi 密码共用英文、大小写、数字及符号键盘，不启用中文候选。
 * 用户验收要求停止后恢复进入传书前的页面或菜单位置；遗忘网络必须确认。
 * 用户要求联网后提供网址二维码；热点用单码切换连接/网页，网络变更清除旧码。
 * 卡失效时停止并汇合接收任务，清除旧容量与二维码；当前请求不得切换存储源。
 * 配网列表与确认页底栏可切换主页面；密码页底部保留输入操作键，已连接的 STA 在离页后保活。
 * 正在上传时禁止主动切页；USB 退出必须先断开电脑再启动本机重新挂载。
 * 用户要求多网络：附近列表标记已保存，独立列表可连接、改密码和逐个遗忘；六行复用底部说明空间。
 * User-requested multi-network UI marks saved nearby entries and provides connect/edit/forget management; six rows reclaim footer space.
 * Frozen: AP/STA transfer stops on exit while a provisioned connected STA stays online; deep sleep or power loss interrupts transfer.
 * Render only paints snapshots; book_store defines the flash file limit. Never format the TF card.
 * Concurrent uploads underrun scan queues; increase prefill until service exit without changing waveforms or pixel clocks.
 * User-requested device provisioning scans while stopped; save ASCII passwords only on connect and clear input on leaving the editor.
 * User-requested caret: tap to position a steady password caret; show/hide preserves position, and exit clears it too.
 * Authorized keyboard revision: WiFi passwords share English, case, number and symbol keys without Chinese candidates.
 * 用户要求移除密码页的冗余说明和字数；打字只局刷，布局切换才重画整个键盘。
 * User requested removing redundant password hints and the counter; typing stays partial, and only layout switches repaint the whole keyboard.
 * Acceptance requires returning to the page or menu position used to enter transfer; forgetting WiFi requires confirmation.
 * User-requested URL QR follows network readiness; AP switches one code between joining and browsing, discarding stale codes on changes.
 * Lost media stops and joins reception and clears capacity/QR; never switch storage beneath an active request.
 * Provisioning lists and confirmation keep the main tabs active; the password editor keeps its input actions, and connected STA persists after exit.
 * Navigation waits for an active upload; USB exit disconnects the host before local remount.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_title.h"
#include "display.h"
#include "display_refresh.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "wifi_transfer.h"
#include "network_local_wifi.h"
#include "memory_peak.h"
#include "board_sd.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_wifi_qr.h"
#include "usb_storage.h"
#include "cloudzao_service.h"
#include "app_transfer_mode.h"
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
#include "assets/cloudzao_download_qr.h"
#include "assets/cloudzao_mark.h"
#include <time.h>
#endif

static const char* TAG = "transfer_page";
static book_store_root_t s_root;
static wifi_transfer_status_t s_status;
static bool s_start_pending, s_settle, s_any_changed;
static bool s_media_lost;
static int s_pressed = -1;
static wifi_transfer_mode_t s_mode = WIFI_TRANSFER_MODE_AP;
static bool s_session_started;
static uint32_t s_owned_session;
static bool s_owned_session_uses_sd;
static int64_t s_poll_ms;
static uint64_t s_free;
static size_t s_heap_before, s_internal_before;
static EpdRect s_area;
static bool s_qr_url, s_qr_ready;
static char s_usb_message[96];
typedef enum {
    TRANSFER_ENTRY_NONE,
    TRANSFER_ENTRY_WIFI_SETUP,
    TRANSFER_ENTRY_WIFI_UPLOAD,
    TRANSFER_ENTRY_HOTSPOT,
    TRANSFER_ENTRY_USB,
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    TRANSFER_ENTRY_CLOUDZAO,
#endif
} transfer_entry_t;
static transfer_entry_t s_entry_requested, s_entry = TRANSFER_ENTRY_WIFI_SETUP;
static bool s_wifi_settings_only, s_usb_entry;
static bool s_usb_start_pending;
static int64_t s_usb_retry_ms;
static bool s_usb_host_connected;
static uint64_t s_usb_capacity_bytes;

static uint32_t s_signature_revision, s_signature_seen;
static bool transfer_signature_get(char *out, size_t cap) {
    const char *text = app_settings_status_signature();
    if (!out || !cap || strlen(text) >= cap) return false;
    snprintf(out, cap, "%s", text); return true;
}
static esp_err_t transfer_signature_set(const char *text) {
    app_settings_set_status_signature(text);
    if (strcmp(app_settings_status_signature(), text)) return ESP_FAIL;
    __atomic_add_fetch(&s_signature_revision, 1, __ATOMIC_RELEASE);
    return ESP_OK;
}

static bool transfer_book_file(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt") || !strcasecmp(ext, ".mobi"));
}

static void transfer_file_deleted(const char *path) {
    if (transfer_book_file(path)) {
        (void)book_progress_forget(path);
        (void)book_title_clear(path);
    }
    if (!strcmp(app_settings_system_font_path(), path)) app_settings_set_system_font_path("");
    if (!strcmp(app_settings_font_path(), path)) app_settings_set_font_path("");
    if (!strcmp(app_settings_wallpaper_path(), path)) app_settings_set_wallpaper_path("");
}

static void transfer_file_moved(const char *old_path, const char *new_path, uint32_t size) {
    if (transfer_book_file(old_path)) {
        book_progress_t progress = {0};
        bool saved = book_progress_load(old_path, size, &progress);
        char last[BOOK_STORE_PATH_MAX] = {0};
        bool was_last = book_progress_last_path(last, sizeof(last)) && !strcmp(last, old_path);
        if (saved) (void)book_progress_save(new_path, &progress);
        (void)book_progress_forget(old_path);
        if (was_last) (void)book_progress_set_last_path(new_path);
    }
    if (transfer_book_file(old_path)) (void)book_title_clear(old_path);
    if (!strcmp(app_settings_system_font_path(), old_path)) app_settings_set_system_font_path(new_path);
    if (!strcmp(app_settings_font_path(), old_path)) app_settings_set_font_path(new_path);
    if (!strcmp(app_settings_wallpaper_path(), old_path)) app_settings_set_wallpaper_path(new_path);
}

static bool transfer_set_wallpaper(const char *path) {
    if (strncmp(path, "/sdcard/pictures/", 17)) return false;
    app_settings_set_wallpaper_path(path);
    if (strcmp(app_settings_wallpaper_path(), path)) return false;
    app_settings_set_lock_style(1);
    return app_settings_lock_style() == 1;
}

void app_transfer_request_usb_start(void) {
    s_entry_requested = TRANSFER_ENTRY_USB;
}

void app_transfer_request_wifi_setup(void) {
    s_entry_requested = TRANSFER_ENTRY_WIFI_SETUP;
}

void app_transfer_request_wifi_upload(void) {
    s_entry_requested = TRANSFER_ENTRY_WIFI_UPLOAD;
}

void app_transfer_request_hotspot_start(void) {
    s_entry_requested = TRANSFER_ENTRY_HOTSPOT;
}

static void clear_qr(void) {
    ui_wifi_qr_clear();
    s_qr_ready = false;
}

// 仅在网络快照变化或用户切换时编码，render与上传进度不触发编码。
// Encode only on network snapshot changes or user switching, never from render or upload progress.
static void prepare_qr(void) {
    clear_qr();
    if (!s_status.network_ready) return;
    s_qr_ready = s_qr_url || s_mode == WIFI_TRANSFER_MODE_STA
        ? ui_wifi_qr_prepare_url(s_status.url)
        : ui_wifi_qr_prepare(s_status.ssid, WIFI_TRANSFER_PASSWORD);
}

typedef enum { TRANSFER_HOME, TRANSFER_NETWORKS, TRANSFER_PASSWORD,
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    TRANSFER_CLOUDZAO,
#endif
} transfer_view_t;
#define NETWORK_ROWS 6
#define PASSWORD_MAX 64
static transfer_view_t s_view;
static wifi_transfer_network_t s_networks[WIFI_TRANSFER_SCAN_MAX], s_selected;
static size_t s_network_count, s_network_page;
static wifi_transfer_saved_network_t s_saved_networks[WIFI_TRANSFER_SAVED_MAX];
static size_t s_saved_count;
static bool s_show_saved, s_saved_detail;
static char s_forget_ssid[33];
static bool s_scan_pending, s_network_connect_pending, s_saved_configured, s_password_visible, s_forget_confirm;
static char s_saved_ssid[33], s_password[PASSWORD_MAX + 1], s_network_message[96];
static ui_text_edit_t s_password_input;

static esp_err_t s_scan_error;

static void render(app_ctx_t* ctx, uint8_t* fb);
static void stop_session(void);
static bool stop_upload_if_idle(void);
static bool stop_wifi_for_provisioning(void);

static void transfer_header(uint8_t *fb, const char *title, const char *subtitle) {
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, title, EPD_DRAW_ALIGN_CENTER, false);
    if (subtitle && subtitle[0]) ui_text(fb, 36, 153, 20, subtitle, EPD_DRAW_ALIGN_LEFT, false);
    if (subtitle && subtitle[0]) ui_hairline(fb, 190, 36, 612, UI_GRAY_LIGHT);
}

/* ---- 设备端配网 / Device provisioning ---- */
static void fit_label(char* text, int px, int width) {
    while (*text && ui_text_fixed_width_px(ui_text_effective_px(px), text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void clear_password(void) {
    volatile char* p = s_password;
    for (size_t i = 0; i < sizeof(s_password); ++i) p[i] = 0;
    ui_text_edit_init(&s_password_input, s_password, sizeof(s_password));
    s_password_visible = false;
    ui_keyboard_end();
}

static EpdRect network_control_rect(int id) {
    if (id >= 0 && id < NETWORK_ROWS) return (EpdRect){36, 442 + id * 88, 612, 88};
    if (id == 6) return (EpdRect){501, 222, 123, 56};
    if (id == 11) return s_forget_confirm ? ui_row_rect(0, 2, 600, UI_BTN_H) :
        s_saved_detail ? (EpdRect){36, 544, 612, 76} : (EpdRect){501, 293, 123, 50};
    if (id == 12) return ui_row_rect(1, 2, 600, UI_BTN_H);
    if (id == 13) return (EpdRect){36, 320, 612, 76};
    if (id == 14) return (EpdRect){36, 432, 612, 76};
    if (id == 15) return (EpdRect){36, 374, 300, 52};
    if (id == 16) return (EpdRect){348, 374, 300, 52};
    if (id == 7) return (EpdRect){36, 990, 64, 66};
    if (id == 8) return (EpdRect){112, 990, 460, 66};
    if (id == 9) return (EpdRect){584, 990, 64, 66};
    return (EpdRect){0};
}

static EpdRect password_control_rect(int id) {
    if (id == 44) return (EpdRect){36, 434, 278, 68};
    if (id == 45) return (EpdRect){500, 215, 148, 58};
    if (id == 46) return (EpdRect){330, 434, 318, 68};
    if (id == 47) return (EpdRect){UI_MARGIN, 288, ui_content_width(), 86};
    return (EpdRect){0};
}

static void provisioning_button(uint8_t* fb, EpdRect rect, const char* text, int id) {
    if (s_pressed == id) ui_draw_pressed_round_rect(fb, rect, UI_BTN_RADIUS);
    ui_draw_button(fb, rect, text, false);
}

static void network_border(uint8_t *fb, EpdRect rect, int radius) {
    ui_draw_round_rect(fb, rect, radius, 0x50);
    ui_draw_round_rect(fb, (EpdRect){rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2}, radius - 1, 0x50);
}

static void network_button(uint8_t *fb, int id, const char *label, bool dark) {
    EpdRect rect = network_control_rect(id);
    ui_fill_round_rect(fb, rect, 16, s_pressed == id ? 0xb0 : dark ? UI_GRAY_BLACK : UI_GRAY_WHITE);
    if (!dark) network_border(fb, rect, 16);
    if (id == 7 || id == 9) {
        size_t count = s_show_saved ? s_saved_count : s_network_count;
        bool enabled = id == 7 ? s_network_page > 0 : (s_network_page + 1) * NETWORK_ROWS < count;
        ui_draw_icon(fb, rect.x + rect.width / 2, rect.y + rect.height / 2, 32,
                     id == 7 ? UI_ICON_CHEVRON_LEFT : UI_ICON_CHEVRON_RIGHT, enabled ? UI_GRAY_BLACK : 0xa0);
    } else ui_text_box(fb, (EpdRect){rect.x + 8, rect.y + 5, rect.width - 16, rect.height - 10},
                       24, label, EPD_DRAW_ALIGN_CENTER, dark && s_pressed != id);
}

static int saved_network_index(const char *ssid) {
    for (size_t i = 0; i < s_saved_count; ++i)
        if (!strcmp(s_saved_networks[i].ssid, ssid)) return (int)i;
    return -1;
}

static void refresh_saved_networks(void) {
    wifi_transfer_saved_network_t networks[WIFI_TRANSFER_SAVED_MAX];
    size_t count = 0;
    if (wifi_transfer_get_saved_networks(networks, &count) == ESP_OK) {
        memcpy(s_saved_networks, networks, sizeof(networks));
        s_saved_count = count;
        s_saved_configured = count != 0;
        snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", count ? networks[0].ssid : "");
        size_t visible = s_show_saved ? s_saved_count : s_network_count;
        if (s_network_page && s_network_page * NETWORK_ROWS >= visible) --s_network_page;
    } else snprintf(s_network_message, sizeof(s_network_message), "读取已保存网络失败，请重试");
}

static void draw_networks(uint8_t* fb) {
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, UI_GRAY_WHITE, fb);
    if (s_forget_confirm) {
        transfer_header(fb, "遗忘网络", "仅清除 WiFi 连接信息，不影响图书");
        char name[33];
        snprintf(name, sizeof(name), "%s", s_forget_ssid);
        fit_label(name, UI_PX_BODY, ui_content_width());
        ui_text(fb, UI_MARGIN, 350, UI_PX_BODY, name, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, UI_MARGIN, 450, UI_PX_CAPTION, s_network_message, EPD_DRAW_ALIGN_LEFT, false);
        provisioning_button(fb, network_control_rect(11), "确认遗忘", 11);
        provisioning_button(fb, network_control_rect(12), "取消", 12);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_saved_detail) {
        transfer_header(fb, "已保存网络", NULL);
        char name[33]; snprintf(name, sizeof(name), "%s", s_selected.ssid);
        fit_label(name, 29, 600);
        ui_text_box(fb, (EpdRect){42, 200, 600, 60}, 29, name, EPD_DRAW_ALIGN_LEFT, false);
        provisioning_button(fb, network_control_rect(13), "连接", 13);
        provisioning_button(fb, network_control_rect(14), "修改密码", 14);
        provisioning_button(fb, network_control_rect(11), "遗忘网络", 11);
        char message[96]; snprintf(message, sizeof(message), "%s", s_network_message);
        fit_label(message, 21, 600);
        ui_text(fb, 42, 674, 21, message, EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    transfer_header(fb, "无线网络", NULL);
    ui_text_box(fb, (EpdRect){42, 161, 600, 36}, 24, "当前网络", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect current = {36, 210, 612, 142};
    ui_fill_round_rect(fb, current, 22, UI_GRAY_WHITE);
    network_border(fb, current, 22);
    if (s_saved_configured) {
        char saved[40]; snprintf(saved, sizeof(saved), "%s", s_saved_ssid); fit_label(saved, 27, 375);
        ui_nav_wifi_icon(fb, 74, 250, 28, UI_GRAY_BLACK);
        ui_text_box(fb, (EpdRect){104, 221, 375, 44}, 27, saved, EPD_DRAW_ALIGN_LEFT, false);
        const char *state = s_status.network_ready ? "已连接" :
            s_network_connect_pending || s_status.state == WIFI_TRANSFER_STARTING ? "正在连接…" : "已保存 · 尚未连接";
        ui_text_box(fb, (EpdRect){64, 266, 419, 36}, 21, state, EPD_DRAW_ALIGN_LEFT, false);
        char address[80];
        const char *url = !strncmp(s_status.url, "http://", 7) ? s_status.url + 7 : s_status.url;
        snprintf(address, sizeof(address), "%.79s", s_network_message[0] ? s_network_message :
                 s_status.network_ready ? url[0] ? url : "已连接，可传书和对时" : "连接后可传书和对时");
        fit_label(address, 19, 419);
        ui_text_box(fb, (EpdRect){64, 306, 419, 36}, 19, address, EPD_DRAW_ALIGN_LEFT, false);
        network_button(fb, 6, s_status.network_ready ? "断开" : "连接", !s_status.network_ready);
        if (s_pressed == 11) ui_draw_pressed_round_rect(fb, network_control_rect(11), 14);
        ui_text_box(fb, network_control_rect(11), 21, "遗忘网络", EPD_DRAW_ALIGN_CENTER, false);
    } else {
        ui_text_box(fb, (EpdRect){64, 224, 556, 48}, 27, "尚未保存网络", EPD_DRAW_ALIGN_LEFT, false);
        char message[96]; snprintf(message, sizeof(message), "%s", s_network_message[0] ?
                                  s_network_message : "请从下方列表选择 2.4 GHz WiFi");
        fit_label(message, 21, 556);
        ui_text_box(fb, (EpdRect){64, 288, 556, 48}, 21, message, EPD_DRAW_ALIGN_LEFT, false);
    }
    size_t visible = s_show_saved ? s_saved_count : s_network_count;
    for (int tab = 0; tab < 2; ++tab) {
        EpdRect rect = network_control_rect(15 + tab);
        bool active = (tab != 0) == s_show_saved;
        if (s_pressed == 15 + tab) ui_draw_pressed_round_rect(fb, rect, 0);
        char label[48]; snprintf(label, sizeof(label), "%s %u", tab ? "已保存" : "附近网络",
                                 (unsigned)(tab ? s_saved_count : s_network_count));
        ui_text_box(fb, (EpdRect){rect.x, rect.y, rect.width, rect.height - 6}, 23, label, EPD_DRAW_ALIGN_CENTER, false);
        ui_fill_rect_fast((EpdRect){rect.x, rect.y + rect.height - 3, rect.width, active ? 3 : 1},
                          active ? UI_GRAY_BLACK : UI_GRAY_LIGHT, fb);
    }
    EpdRect list = {36, 442, 612, NETWORK_ROWS * 88};
    ui_fill_round_rect(fb, list, 22, UI_GRAY_WHITE);
    if (s_scan_pending && !s_show_saved) {
        ui_text_vc(fb, 342, 655, 25, "正在扫描 2.4 GHz WiFi", EPD_DRAW_ALIGN_CENTER, false);
    } else if (!visible) {
        ui_text_vc(fb, 342, 620, 27, s_show_saved ? "尚未保存网络" : s_scan_error == ESP_OK ? "没有发现可用网络" : "扫描没有完成",
                   EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 678, 22, s_show_saved ? "附近网络中连接后即可保存" : s_scan_error == ESP_OK ? "请靠近路由器后重新扫描" : "点下方按钮自动重试",
                   EPD_DRAW_ALIGN_CENTER, false);
    }
    for (int row = 0; (s_show_saved || !s_scan_pending) && row < NETWORK_ROWS; ++row) {
        size_t index = s_network_page * NETWORK_ROWS + row;
        if (index >= visible) break;
        const char *ssid = s_show_saved ? s_saved_networks[index].ssid : s_networks[index].ssid;
        EpdRect rect = network_control_rect(row);
        if (s_pressed == row) ui_draw_pressed_round_rect(fb, rect, 0);
        if (row) ui_fill_rect_fast((EpdRect){64, rect.y, 556, 2}, UI_GRAY_BLACK, fb);
        char name[33], detail[64];
        snprintf(name, sizeof(name), "%s", ssid);
        fit_label(name, 28, 520);
        ui_text_box(fb, (EpdRect){64, rect.y + 6, 520, 40}, 28, name, EPD_DRAW_ALIGN_LEFT, false);
        bool saved = saved_network_index(ssid) >= 0;
        bool connected = s_status.network_ready && !strcmp(s_status.wifi_ssid, ssid);
        if (s_show_saved) snprintf(detail, sizeof(detail), "%s", connected ? "已连接" :
                                  !strcmp(s_saved_ssid, ssid) ? "默认网络" : "已保存");
        else {
            const wifi_transfer_network_t *network = &s_networks[index];
            snprintf(detail, sizeof(detail), "%s · %s%s", network->rssi > -60 ? "信号强" : network->rssi > -75 ? "信号良好" : "信号一般",
                     !network->supported ? "暂不支持" : connected ? "已连接 · 已保存" : saved ? "已保存" :
                     network->requires_password ? "需要密码" : "开放网络",
                     !network->supported && saved ? " · 已保存" : "");
        }
        ui_text_box(fb, (EpdRect){64, rect.y + 49, 520, 32}, 21, detail, EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_icon(fb, 615, rect.y + 44, 26, UI_ICON_CHEVRON_RIGHT, UI_GRAY_BLACK);
    }
    network_border(fb, list, 22);
    network_button(fb, 7, NULL, false);
    network_button(fb, 9, NULL, false);
    char paging[64];
    snprintf(paging, sizeof(paging), "%s · %u/%u", s_show_saved ? "已保存" : s_scan_error == ESP_OK ? "重新扫描" : "重试扫描",
             (unsigned)s_network_page + 1, (unsigned)(visible ? (visible + NETWORK_ROWS - 1) / NETWORK_ROWS : 1));
    if (s_show_saved) ui_text_box(fb, network_control_rect(8), 24, paging, EPD_DRAW_ALIGN_CENTER, false);
    else network_button(fb, 8, paging, false);
    ui_nav_draw(fb, 3);
}

static void draw_password(uint8_t* fb) {
    ui_clear_page(fb);
    transfer_header(fb, "连接 WiFi", NULL);
    char name[33];
    snprintf(name, sizeof(name), "%s", s_selected.ssid);
    fit_label(name, UI_PX_BODY, ui_content_width());
    ui_text(fb, UI_MARGIN, 176, UI_PX_BODY, name, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_input_draw(fb, &s_password_input, password_control_rect(47), UI_PX_BODY,
                       !s_password_visible, NULL);
    ui_text_fixed_vc(fb, 36, 399, 22, s_network_message[0] ? s_network_message : "支持英文、数字、符号和空格", EPD_DRAW_ALIGN_LEFT, false);
    provisioning_button(fb, password_control_rect(44), "取消", 44);
    provisioning_button(fb, password_control_rect(45), s_password_visible ? "隐藏" : "显示", 45);
    provisioning_button(fb, password_control_rect(46), "连接并保存", 46);
    ui_keyboard_draw(fb, 560);
}

#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
static cz_status_t s_cloud_status, s_cloud_bind_status;
static bool s_cloud_requested, s_cloud_cancelling, s_cloud_poll_running;
static int64_t s_cloud_poll_at;
typedef enum { CLOUD_PANEL_TRANSFER, CLOUD_PANEL_HISTORY, CLOUD_PANEL_SETTINGS } cloud_panel_t;
static cloud_panel_t s_cloud_panel;
typedef enum { CLOUD_NOTICE_HIDDEN, CLOUD_NOTICE_FIRST_READ, CLOUD_NOTICE_REVIEW } cloud_notice_t;
static cloud_notice_t s_cloud_notice;
static bool s_cloud_notice_save_failed;

static bool cloud_notice_input_owned(app_ctx_t *ctx) {
    (void)ctx;
    return s_view == TRANSFER_CLOUDZAO && s_cloud_notice != CLOUD_NOTICE_HIDDEN;
}
static unsigned s_cloud_batch_page, s_cloud_history_page;
static bool s_cloud_batch;
static unsigned s_cloud_offer_page, s_cloud_selectable, s_cloud_checked;
static cz_selection_t s_cloud_selection;
static cz_error_t s_cloud_action_error;
#define CLOUD_SAMPLE_PAINT_MS 2000
#define CLOUD_LAYOUT_PAINT_MS 500
static int64_t s_cloud_next_sample_ms, s_cloud_next_layout_ms;
static bool s_cloud_sample_paint;
static cz_transfer_t s_cloud_transfer;
static cz_history_status_t s_cloud_history;
static bool s_cloud_history_confirm, s_cloud_history_clearing, s_cloud_history_clear_failed;

static void cloud_reset_history_actions(void) {
    s_cloud_history_confirm = s_cloud_history_clearing = s_cloud_history_clear_failed = false;
}

void app_transfer_request_cloudzao(void) {
    s_entry_requested = TRANSFER_ENTRY_CLOUDZAO;
}

static void cloud_reset_selection(void) {
    s_cloud_offer_page = s_cloud_selectable = s_cloud_checked = 0;
    s_cloud_selection = (cz_selection_t){0};
    s_cloud_action_error = CZ_OK;
}

static void cloud_leave(void) {
    if (s_view != TRANSFER_CLOUDZAO) return;
    cloudzao_cancel();
    cloudzao_discard_queue();
    cloud_reset_selection();
    s_cloud_batch = false;
    s_cloud_poll_at = 0;
    s_cloud_poll_running = false;
    s_cloud_status = (cz_status_t){0};
    s_cloud_bind_status = (cz_status_t){0};
    s_cloud_requested = s_cloud_cancelling = false;
    s_cloud_batch_page = s_cloud_history_page = 0;
    s_cloud_panel = CLOUD_PANEL_TRANSFER;
    s_cloud_notice = CLOUD_NOTICE_HIDDEN;
    s_cloud_notice_save_failed = false;
    s_cloud_transfer = (cz_transfer_t){0};
    s_cloud_history = (cz_history_status_t){0};
    cloud_reset_history_actions();
    s_cloud_next_sample_ms = s_cloud_next_layout_ms = 0;
    s_view = TRANSFER_HOME;
}

static const char *cloud_error(cz_error_t error) {
    switch (error) {
        case CZ_AUTH: return "请在设置中绑定 CloudZao";
        case CZ_REFRESH_REUSED: return "授权已失效，请重新绑定";
        case CZ_REFRESH_INTERRUPTED: return "授权刷新未完成";
        case CZ_INTEGRATION: return "请核对固件接入 ID";
        case CZ_CLOCK: return "请先校准设备时间";
        case CZ_SLOW_DOWN: return "请求过于频繁，请稍候";
        case CZ_PAIR_EXPIRED: return "绑定码已失效，请重新获取";
        case CZ_ACTIVATION_UNAVAILABLE: return "暂未获取到绑定码";
        case CZ_CREDENTIAL_STORAGE: return "无法保存或读取本机授权";
        case CZ_LINK_EXPIRED: return "下载链接已过期，请刷新列表";
        case CZ_DELIVERY_MISSING: return "推送已变化，请刷新列表";
        case CZ_WIFI: return "请先下拉快捷栏打开wifi";
        case CZ_MEDIA: return "请插入已挂载的 TF 卡";
        case CZ_TIMEOUT: return "接收超时，可再次接收";
        case CZ_CANCELLED: return "已取消";
        case CZ_RESOURCES: return "设备忙或内存不足";
        case CZ_STORAGE: return "TF 卡空间不足或写入失败";
        case CZ_CONFLICT: return "存在内容不同的同名文件，未覆盖";
        case CZ_DIGEST: return "文件大小或摘要不匹配";
        case CZ_SERVER: return "服务请求失败";
        case CZ_INVALID: return "资源或响应格式无效";
        default: return "";
    }
}

enum { CLOUD_BIND, CLOUD_RECEIVE };

static EpdRect cloud_button(int id) {
    if (id == CLOUD_BIND) return (EpdRect){56, 480, 572, 72};
    return (EpdRect){36, 282, 612, 72};
}

static const char *cloud_result_title(void) {
    if (s_cloud_status.busy) {
        if (s_cloud_cancelling) return "正在取消";
        return "正在下载推送";
    }
    if (s_cloud_status.error == CZ_CANCELLED) return "已取消";
    if (s_cloud_status.error) return s_cloud_requested ? "接收已中断" : "暂时无法接收";
    if (!s_cloud_requested) return "等待接收";
    if (!s_cloud_status.total) return "没有待接收推送";
    if (s_cloud_status.failed || s_cloud_status.pending || s_cloud_status.skipped) return "接收结束";
    return "接收完成";
}

static const char *cloud_result_detail(char *out, size_t cap) {
    if (s_cloud_status.busy) {
        if (s_cloud_cancelling) return "正在结束本次操作，请稍候";
        if (!s_cloud_status.total) return "正在核对所选推送";
        snprintf(out, cap, "已处理 %u / %u 项", s_cloud_status.processed, s_cloud_status.total);
        return out;
    }
    if (s_cloud_status.error == CZ_CANCELLED) return "已经保存的文件会保留";
    if (s_cloud_status.error == CZ_SERVER) return "云造请求失败，可稍后再试";
    if (s_cloud_status.error) return cloud_error(s_cloud_status.error);
    if (!s_cloud_requested) return "";
    if (s_cloud_status.item_error) {
        const bool ack = s_cloud_status.item_request == CZ_ACK;
        snprintf(out, cap, "%s：%s", ack ? "确认失败，文件已保存" : "下载或保存失败",
                 cloud_error(s_cloud_status.item_error));
        return out;
    }
    if (s_cloud_status.unavailable) return "部分推送已变化或失效，请重新获取列表";
    if (s_cloud_status.saved) return "已保存的文件可在 TF 卡中查看";
    return "在云造端推送文件后可再次接收";
}

static EpdRect cloud_tab(int index) { return (EpdRect){36 + index * 204, 151, 204, 48}; }

static void cloud_tabs(uint8_t *fb) {
    static const char *const labels[] = {"传输", "历史记录", "设置"};
    ui_fill_rect_fast((EpdRect){36, 199, 612, 2}, 0xc0, fb);
    for (int i = 0; i < 3; ++i) {
        EpdRect rect = cloud_tab(i);
        ui_text_box(fb, rect, 21, labels[i], EPD_DRAW_ALIGN_CENTER, false);
        if (i == (int)s_cloud_panel)
            ui_fill_rect_fast((EpdRect){rect.x + 52, rect.y + 46, 100, 4}, UI_GRAY_BLACK, fb);
    }
}

static const char *cloud_row_label(cz_row_state_t state) {
    switch (state) {
        case CZ_ROW_WAIT: return s_cloud_status.busy ? "等待" : "未处理";
        case CZ_ROW_CHECK: return "准备下载";
        case CZ_ROW_DOWNLOAD: return "下载中";
        case CZ_ROW_VERIFY: return "校验中";
        case CZ_ROW_CONFIRM: return "确认中";
        case CZ_ROW_SAVED: return "已保存";
        case CZ_ROW_PENDING: return "待确认";
        case CZ_ROW_SKIPPED: return "已跳过";
        case CZ_ROW_FAILED: return "失败";
        case CZ_ROW_UNAVAILABLE: return "推送已失效";
        default: return "已中断";
    }
}

static void cloud_bytes(char *out, size_t cap, uint32_t bytes) {
    const unsigned n = (unsigned)bytes;
    const unsigned unit = n >= 1048576 ? 1048576 : n >= 1024 ? 1024 : 1;
    if (unit == 1) snprintf(out, cap, "%u B", n);
    else snprintf(out, cap, "%u.%u %s", n / unit, (n % unit) * 10 / unit, unit == 1024 ? "KiB" : "MiB");
}

static void cloud_name_label(char out[CLOUDZAO_NAME_MAX + 1], const cz_row_t *file, int width) {
    if (file->name[0]) memcpy(out, file->name, sizeof(file->name));
    else strcpy(out, "未命名文件");
    out[CLOUDZAO_NAME_MAX] = 0;
    fit_label(out, 24, width);
}

typedef struct {
    int fill_width;
    char detail[80], rate[24];
} cloud_sample_t;

static EpdRect cloud_sample_area(void) { return (EpdRect){56, 515, 572, 51}; }

static cloud_sample_t cloud_sample(const cz_transfer_t *current) {
    cloud_sample_t sample = {0};
    const uint32_t size = current->file.size;
    const uint32_t used = current->received > size ? size : current->received;
    if (size) sample.fill_width = (int)((uint64_t)used * cloud_sample_area().width / size);
    if (current->file.state == CZ_ROW_DOWNLOAD) {
        char downloaded[20], total[20], rate[20];
        cloud_bytes(downloaded, sizeof(downloaded), used);
        cloud_bytes(total, sizeof(total), size);
        cloud_bytes(rate, sizeof(rate), current->rate);
        const unsigned pct = size ? (unsigned)((uint64_t)used * 100 / size) : 0;
        snprintf(sample.detail, sizeof(sample.detail), "%s / %s · %u%%", downloaded, total, pct);
        snprintf(sample.rate, sizeof(sample.rate), "%s/s", rate);
    } else snprintf(sample.detail, sizeof(sample.detail), "%s", cloud_row_label(current->file.state));
    return sample;
}

static void draw_cloud_sample(uint8_t *fb, const cloud_sample_t *sample) {
    EpdRect bar = {56, 515, cloud_sample_area().width, 8};
    ui_fill_rect_fast(bar, 0xd0, fb);
    bar.width = sample->fill_width;
    if (bar.width) ui_fill_rect_fast(bar, UI_GRAY_BLACK, fb);
    if (sample->rate[0]) {
        ui_text_box(fb, (EpdRect){56, 527, 410, 39}, 18, sample->detail, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_box(fb, (EpdRect){469, 527, 159, 39}, 18, sample->rate, EPD_DRAW_ALIGN_RIGHT, false);
    } else ui_text_box(fb, (EpdRect){56, 527, 572, 39}, 18, sample->detail, EPD_DRAW_ALIGN_LEFT, false);
}

static void cloud_live_progress(uint8_t *fb) {
    char name[CLOUDZAO_NAME_MAX + 1];
    cloud_name_label(name, &s_cloud_transfer.file, 552);
    ui_text_box(fb, (EpdRect){56, 469, 572, 43}, 24, name, EPD_DRAW_ALIGN_LEFT, false);
    const cloud_sample_t sample = cloud_sample(&s_cloud_transfer);
    draw_cloud_sample(fb, &sample);
}

#define CLOUD_BATCH_PAGE_ROWS 5u
#define CLOUD_HISTORY_PAGE_ROWS 6u
#define CLOUD_HISTORY_ROW_TOP 332
#define CLOUD_HISTORY_ROW_HEIGHT 111
#define CLOUD_HISTORY_PAGER_TOP (CLOUD_HISTORY_ROW_TOP + CLOUD_HISTORY_PAGE_ROWS * CLOUD_HISTORY_ROW_HEIGHT + 28)

static EpdRect cloud_pager(bool history, bool next) {
    return (EpdRect){next ? 478 : 36, history ? CLOUD_HISTORY_PAGER_TOP : 1116, 170, 64};
}

static EpdRect cloud_batch_area(void) { return (EpdRect){36, 656, 612, 524}; }

static void draw_cloud_batch(uint8_t *fb) {
    const unsigned count = s_cloud_transfer.rows;
    char line[96];
    const EpdRect batch = cloud_batch_area();
    ui_text_box(fb, (EpdRect){36, batch.y, 220, 41}, 25, "本批文件", EPD_DRAW_ALIGN_LEFT, false);
    if (s_cloud_status.total > CLOUDZAO_LIST_ROWS) {
        snprintf(line, sizeof(line), "仅显示前 %u 项", CLOUDZAO_LIST_ROWS);
        ui_text_box(fb, (EpdRect){260, batch.y, 388, 41}, 18, line, EPD_DRAW_ALIGN_RIGHT, false);
    }
    if (!count) {
        const char *msg = s_cloud_status.busy ? "正在获取待接收文件…" : "暂无本批文件";
        ui_text_box(fb, (EpdRect){36, 716, 612, 80}, 25, msg, EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    const unsigned pages = (count + CLOUD_BATCH_PAGE_ROWS - 1) / CLOUD_BATCH_PAGE_ROWS;
    const unsigned page = s_cloud_batch_page < pages ? s_cloud_batch_page : pages - 1;
    for (unsigned i = 0; i < CLOUD_BATCH_PAGE_ROWS; ++i) {
        const unsigned index = page * CLOUD_BATCH_PAGE_ROWS + i;
        if (index >= count) break;
        cz_row_t file = {0};
        if (!cloudzao_transfer_row(index, &file)) break;
        const int y = 704 + (int)i * 80;
        char name[CLOUDZAO_NAME_MAX + 1], size[20];
        cloud_name_label(name, &file, 580);
        if (file.size) cloud_bytes(size, sizeof(size), file.size);
        else snprintf(size, sizeof(size), "—");
        snprintf(line, sizeof(line), "%u.  %s", index + 1, size);
        ui_text_box(fb, (EpdRect){36, y + 5, 612, 35}, 24, name, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_box(fb, (EpdRect){36, y + 40, 428, 33}, 18, line, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_box(fb, (EpdRect){480, y + 40, 168, 33}, 18, cloud_row_label(file.state), EPD_DRAW_ALIGN_RIGHT, false);
        ui_fill_rect_fast((EpdRect){36, y + 77, 612, 2}, 0xc0, fb);
    }
    const EpdRect pager = cloud_pager(false, false);
    snprintf(line, sizeof(line), "%u / %u", page + 1, pages);
    if (page) ui_draw_button(fb, pager, "上一页", false);
    if (page + 1 < pages) ui_draw_button(fb, cloud_pager(false, true), "下一页", false);
    ui_text_box(fb, (EpdRect){228, pager.y, 228, pager.height}, 20, line, EPD_DRAW_ALIGN_CENTER, false);
}

static const char *cloud_history_format(const char *name) {
    const char *ext = strrchr(name, '.');
    if (ext) {
        if (!strcasecmp(ext, ".epub")) return "EPUB";
        if (!strcasecmp(ext, ".txt")) return "TXT";
        if (!strcasecmp(ext, ".ttf")) return "TTF";
        if (!strcasecmp(ext, ".png")) return "PNG";
    }
    return "文件";
}

static void draw_cloud_history_row(uint8_t *fb, int y, const cz_history_item_t *entry) {
    const EpdRect format = {36, y + 29, 66, 49};
    ui_draw_round_rect(fb, format, 8, 0xc0);
    ui_text_box(fb, (EpdRect){format.x + 4, format.y + 3, format.width - 8, format.height - 6}, 16,
                cloud_history_format(entry->file.name), EPD_DRAW_ALIGN_CENTER, false);

    const EpdRect title = {122, y + 11, 526, 46};
    char name[CLOUDZAO_NAME_MAX + sizeof("…")];
    snprintf(name, sizeof(name), "%s", entry->file.name[0] ? entry->file.name : "未命名文件");
    const int px = ui_text_effective_px(26);
    if (ui_text_fixed_width_px(px, name) > title.width) {
        fit_label(name, 26, title.width - ui_text_fixed_width_px(px, "…"));
        strcat(name, "…");
    }
    ui_text_box(fb, title, 26, name, EPD_DRAW_ALIGN_LEFT, false);

    char size[20] = "—", stamp[28] = "时间未校准", meta[64];
    if (entry->file.size) cloud_bytes(size, sizeof(size), entry->file.size);
    const time_t utc = entry->utc;
    struct tm local;
    if (entry->utc && localtime_r(&utc, &local)) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &local);
    snprintf(meta, sizeof(meta), "%s · %s", stamp, size);
    ui_text_box(fb, (EpdRect){122, y + 57, 368, 37}, 18, meta, EPD_DRAW_ALIGN_LEFT, false);

    const cz_row_state_t state = entry->file.state;
    if (state == CZ_ROW_FAILED) {
        ui_text_box(fb, (EpdRect){500, y + 57, 22, 37}, 19, "!", EPD_DRAW_ALIGN_CENTER, false);
    } else {
        const ui_icon_t icon = state == CZ_ROW_SAVED ? UI_ICON_CHECK :
            state == CZ_ROW_PENDING ? UI_ICON_CLOCK : UI_ICON_ELLIPSIS;
        ui_draw_icon(fb, 511, y + 75, 22, icon, 0x40);
    }
    ui_text_box(fb, (EpdRect){534, y + 57, 114, 37}, 18,
                cloud_row_label(state), EPD_DRAW_ALIGN_LEFT, false);
}

static void draw_cloud_history_pager(uint8_t *fb, bool next) {
    const EpdRect rect = cloud_pager(true, next);
    ui_draw_round_rect(fb, rect, 12, 0xc0);
    ui_draw_icon(fb, rect.x + (next ? rect.width - 24 : 24), rect.y + rect.height / 2, 24,
                 next ? UI_ICON_CHEVRON_RIGHT : UI_ICON_CHEVRON_LEFT, 0x30);
    ui_text_box(fb, (EpdRect){rect.x + (next ? 8 : 45), rect.y + 5, rect.width - 53, rect.height - 10}, 20,
                next ? "下一页" : "上一页", EPD_DRAW_ALIGN_CENTER, false);
}

static EpdRect cloud_history_clear_button(void) { return (EpdRect){520, 220, 128, 58}; }

static EpdRect cloud_history_confirm_button(bool confirm) {
    return (EpdRect){confirm ? 355 : 56, 690, 273, 72};
}

static bool cloud_history_can_clear(void) {
    return s_cloud_history.count && !s_cloud_history.busy && !s_cloud_history.error;
}

static void draw_cloud_history_confirmation(uint8_t *fb) {
    ui_fill_round_rect_raw(fb, (EpdRect){36, 412, 612, 380}, 16, 0xe0);
    ui_text_box(fb, (EpdRect){56, 444, 572, 78}, 32, "清除全部记录？", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){56, 540, 572, 54}, 22, "仅清除当前 TF 卡的接收记录", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){56, 602, 572, 54}, 22, "不会删除文件或影响云端推送", EPD_DRAW_ALIGN_CENTER, false);
    for (int confirm = 0; confirm <= 1; ++confirm) {
        const EpdRect rect = cloud_history_confirm_button(confirm);
        ui_fill_round_rect_raw(fb, rect, 12, confirm ? 0x20 : 0xf0);
        ui_text_box(fb, rect, 24, confirm ? "确认清除" : "取消", EPD_DRAW_ALIGN_CENTER, confirm);
    }
}

static void draw_cloud_history(uint8_t *fb) {
    const unsigned count = s_cloud_history.count;
    const bool busy = s_cloud_history.busy;
    const cz_error_t error = s_cloud_history.error;
    const bool show_actions = cloud_history_can_clear() && !s_cloud_history_confirm;
    const char *detail = s_cloud_history_clear_failed ? "清除失败，可重试" : "";
    char text[80];
    ui_text_box(fb, (EpdRect){36, 217, 460, 65}, 39, "接收记录", EPD_DRAW_ALIGN_LEFT, false);
    if (show_actions) {
        const EpdRect clear = cloud_history_clear_button();
        ui_draw_round_rect(fb, clear, 10, 0xc0);
        ui_text_box(fb, clear, 20, "清除", EPD_DRAW_ALIGN_CENTER, false);
        snprintf(text, sizeof(text), "%u 条%s%s", count, *detail ? " · " : "", detail);
        detail = text;
    }
    ui_text_box(fb, (EpdRect){36, 279, 612, 47}, 19, detail, EPD_DRAW_ALIGN_LEFT, false);
    if (s_cloud_history_confirm) {
        draw_cloud_history_confirmation(fb);
        return;
    }
    if (!count || busy || error) {
        const ui_icon_t icon = busy ? UI_ICON_CLOCK : error ? UI_ICON_FOLDER_OPEN : UI_ICON_LIST;
        const char *title = busy ? (s_cloud_history_clearing ? "正在清除记录" : "正在读取记录") :
            error ? "无法读取记录" : "还没有接收记录";
        const char *empty_detail = busy ? "" : error == CZ_STORAGE ? "请检查 TF 卡，再重新打开历史记录" :
            error ? cloud_error(error) : "接收文件后，结果会显示在这里";
        ui_draw_icon(fb, 342, 522, 56, icon, UI_GRAY_BLACK);
        ui_text_box(fb, (EpdRect){36, 602, 612, 57}, 31, title, EPD_DRAW_ALIGN_CENTER, false);
        ui_text_box(fb, (EpdRect){36, 666, 612, 42}, 20, empty_detail, EPD_DRAW_ALIGN_CENTER, false);
        return;
    }

    ui_fill_rect_fast((EpdRect){36, CLOUD_HISTORY_ROW_TOP - 2, 612, 2}, 0xd0, fb);
    const unsigned pages = (count + CLOUD_HISTORY_PAGE_ROWS - 1) / CLOUD_HISTORY_PAGE_ROWS;
    const unsigned page = s_cloud_history_page < pages ? s_cloud_history_page : pages - 1;
    for (unsigned i = 0; i < CLOUD_HISTORY_PAGE_ROWS; ++i) {
        const unsigned index = page * CLOUD_HISTORY_PAGE_ROWS + i;
        if (index >= count) break;
        cz_history_item_t entry;
        if (!cloudzao_history_item(index, &entry)) break;
        const int y = CLOUD_HISTORY_ROW_TOP + (int)i * CLOUD_HISTORY_ROW_HEIGHT;
        draw_cloud_history_row(fb, y, &entry);
        if (i + 1 < CLOUD_HISTORY_PAGE_ROWS && index + 1 < count)
            ui_fill_rect_fast((EpdRect){122, y + CLOUD_HISTORY_ROW_HEIGHT - 1, 526, 1}, 0xd0, fb);
    }
    if (pages <= 1) return;
    snprintf(text, sizeof(text), "%u / %u", page + 1, pages);
    ui_text_box(fb, (EpdRect){228, CLOUD_HISTORY_PAGER_TOP, 228, 64}, 21, text, EPD_DRAW_ALIGN_CENTER, false);
    if (page) draw_cloud_history_pager(fb, false);
    if (page + 1 < pages) draw_cloud_history_pager(fb, true);
}

static EpdRect cloud_result_area(void) { return (EpdRect){36, 374, 612, 260}; }

static void draw_cloud_result(uint8_t *fb) {
    const bool busy = s_cloud_status.busy;
    ui_draw_round_rect(fb, cloud_result_area(), 22, 0x90);
    ui_text_box(fb, (EpdRect){56, 385, 572, 31}, 17, "传输状态", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){56, 416, 572, 44}, 29, cloud_result_title(), EPD_DRAW_ALIGN_LEFT, false);
    ui_fill_rect_fast((EpdRect){56, 466, 572, 2}, 0xc0, fb);
    if (busy && s_cloud_transfer.active) cloud_live_progress(fb);
    else {
        static const char *const labels[] = {"已保存", "待确认", "跳过", "失败"};
        const unsigned counts[] = {s_cloud_status.saved, s_cloud_status.pending, s_cloud_status.skipped, s_cloud_status.failed};
        const bool counts_known = s_cloud_requested && (s_cloud_status.total || (!busy && !s_cloud_status.error));
        for (int i = 0; i < 4; ++i) {
            char count[12] = "—";
            if (counts_known) snprintf(count, sizeof(count), "%u", counts[i]);
            const int x = 56 + i * 143;
            if (i) ui_fill_rect_fast((EpdRect){x, 484, 1, 77}, 0xc0, fb);
            ui_text_box(fb, (EpdRect){x + 4, 474, 135, 53}, 37, count, EPD_DRAW_ALIGN_CENTER, false);
            ui_text_box(fb, (EpdRect){x + 4, 527, 135, 39}, 18, labels[i], EPD_DRAW_ALIGN_CENTER, false);
        }
    }
    char detail[128];
    ui_text_box(fb, (EpdRect){56, 577, 572, 40}, 18,
                cloud_result_detail(detail, sizeof(detail)), EPD_DRAW_ALIGN_LEFT, false);
}

static void cloud_action_button(uint8_t *fb, EpdRect rect, const char *label, bool primary, bool enabled) {
    ui_fill_round_rect(fb, rect, 18, primary && enabled ? UI_GRAY_BLACK : UI_GRAY_WHITE);
    if (!primary || !enabled) ui_draw_round_rect(fb, rect, 18, enabled ? 0x50 : 0xb0);
    ui_text_box(fb, (EpdRect){rect.x + 12, rect.y + 8, rect.width - 24, rect.height - 16}, 25,
                label, EPD_DRAW_ALIGN_CENTER, primary && enabled);
}

static void cloud_offer_icon_label(uint8_t *fb, EpdRect rect, const char *label, ui_icon_t icon,
                                   bool trailing, bool inverted, uint8_t icon_gray) {
    const int px = trailing ? 23 : 25;
    const int reserve = 42;
    const int width = ui_text_fixed_width_px(ui_text_effective_px(px), label) + 2;
    const int cap = rect.width - reserve - (trailing ? 0 : 32);
    const int text_width = width < cap ? width : cap;
    const int left = trailing ? rect.x + rect.width - text_width - reserve :
        rect.x + (rect.width - text_width - reserve) / 2;
    ui_draw_icon(fb, left + 15, rect.y + rect.height / 2, 28, icon, icon_gray);
    ui_text_box(fb, (EpdRect){left + reserve, rect.y + 8, text_width, rect.height - 16}, px,
                label, trailing ? EPD_DRAW_ALIGN_RIGHT : EPD_DRAW_ALIGN_LEFT, inverted);
}

#define CLOUD_OFFER_PAGE_ROWS 6u
#define CLOUD_OFFER_TABLE_TOP 316
#define CLOUD_OFFER_Y 372
#define CLOUD_OFFER_H 91
#define CLOUD_OFFER_PAGER_Y (CLOUD_OFFER_Y + CLOUD_OFFER_PAGE_ROWS * CLOUD_OFFER_H + 12)
#define CLOUD_OFFER_PAGER_H 54
#define CLOUD_OFFER_SELECTION_Y 1016
#define CLOUD_OFFER_END 1095

static unsigned cloud_offer_count(void) {
    if (!s_cloud_requested || s_cloud_status.error || s_cloud_status.busy) return 0;
    return s_cloud_status.total > CLOUDZAO_LIST_ROWS ? CLOUDZAO_LIST_ROWS : s_cloud_status.total;
}

static EpdRect cloud_offer_submit(void) { return (EpdRect){362, 1021, 286, 72}; }
static EpdRect cloud_offer_fetch(void) {
    if (cloud_offer_count()) return (EpdRect){471, 232, 177, 64};
    EpdRect rect = cloud_offer_submit();
    rect.width = 320;
    rect.x = UI_MARGIN + (ui_content_width() - rect.width) / 2;
    return rect;
}
static EpdRect cloud_offer_all(void) { return (EpdRect){470, CLOUD_OFFER_TABLE_TOP, 178, CLOUD_OFFER_Y - CLOUD_OFFER_TABLE_TOP - 2}; }
static EpdRect cloud_offer_pager(bool next) {
    return (EpdRect){next ? 478 : 36, CLOUD_OFFER_PAGER_Y, 170, CLOUD_OFFER_PAGER_H};
}
static EpdRect cloud_offer_notice(void) {
    const int top = CLOUD_OFFER_PAGER_Y + CLOUD_OFFER_PAGER_H + 2;
    return (EpdRect){36, top, 612, CLOUD_OFFER_SELECTION_Y - top - 2};
}
static EpdRect cloud_offer_rect(unsigned line) { return (EpdRect){36, CLOUD_OFFER_Y + (int)line * CLOUD_OFFER_H, 612, CLOUD_OFFER_H}; }

static const char *cloud_offer_format(cz_kind_t kind) {
    switch (kind) {
        case CZ_KIND_EPUB: return "EPUB";
        case CZ_KIND_TXT: return "TXT";
        case CZ_KIND_TTF: return "TTF";
        case CZ_KIND_PNG: return "PNG";
        default: return "—";
    }
}

static const char *cloud_offer_kind(cz_kind_t kind) {
    switch (kind) {
        case CZ_KIND_EPUB: case CZ_KIND_TXT: return "书籍";
        case CZ_KIND_TTF: return "字体";
        case CZ_KIND_PNG: return "图片";
        default: return "不可下载";
    }
}

static void draw_cloud_offer_mark(uint8_t *fb, int cy, bool checked) {
    const EpdRect box = {610, cy - 17, 34, 34};
    if (checked) {
        ui_fill_round_rect(fb, box, 7, UI_GRAY_BLACK);
        ui_draw_icon(fb, 627, cy, 22, UI_ICON_CHECK, UI_GRAY_WHITE);
    } else ui_draw_round_rect(fb, box, 7, 0x50);
}

static void draw_cloud_offer_list(uint8_t *fb, unsigned count) {
    const bool binding = s_cloud_bind_status.busy;
    const EpdRect rect = cloud_offer_all();
    ui_text_box(fb, (EpdRect){36, rect.y, 276, rect.height}, 20, "文件", EPD_DRAW_ALIGN_LEFT, false);
    if (s_cloud_selectable && !binding) {
        const bool all = s_cloud_checked == s_cloud_selectable;
        ui_text_box(fb, (EpdRect){rect.x, rect.y, 124, rect.height}, 21,
                    all ? "取消全选" : "全选", EPD_DRAW_ALIGN_RIGHT, false);
        draw_cloud_offer_mark(fb, rect.y + rect.height / 2, all);
    }
    ui_fill_rect_fast((EpdRect){36, CLOUD_OFFER_Y - 2, 612, 2}, 0xb0, fb);

    char text[96];
    const unsigned pages = (count + CLOUD_OFFER_PAGE_ROWS - 1) / CLOUD_OFFER_PAGE_ROWS;
    for (unsigned i = 0; i < CLOUD_OFFER_PAGE_ROWS; ++i) {
        const unsigned index = s_cloud_offer_page * CLOUD_OFFER_PAGE_ROWS + i;
        if (index >= count) break;
        cz_row_t file;
        cz_kind_t kind;
        if (!cloudzao_offer(index, &file, &kind)) break;
        const EpdRect rect = cloud_offer_rect(i);
        const int center = rect.y + rect.height / 2;
        const EpdRect tag = {36, center - 22, 68, 44};
        ui_fill_round_rect_raw(fb, tag, 6, kind != CZ_KIND_NONE ? 0xe0 : 0xf0);
        ui_text_box(fb, (EpdRect){tag.x + 5, tag.y + 3, tag.width - 10, tag.height - 6}, 17,
                    cloud_offer_format(kind), EPD_DRAW_ALIGN_CENTER, false);
        char name[CLOUDZAO_NAME_MAX + 1], size[20];
        cloud_name_label(name, &file, 464);
        ui_text_box(fb, (EpdRect){124, rect.y + 8, 464, 40}, 26, name, EPD_DRAW_ALIGN_LEFT, false);
        if (file.size && kind != CZ_KIND_NONE) {
            cloud_bytes(size, sizeof(size), file.size);
            snprintf(text, sizeof(text), "%s · %s", cloud_offer_kind(kind), size);
        } else snprintf(text, sizeof(text), "%s", cloud_offer_kind(kind));
        ui_text_box(fb, (EpdRect){124, rect.y + 48, 464, 34}, 18, text, EPD_DRAW_ALIGN_LEFT, false);
        if (kind != CZ_KIND_NONE) draw_cloud_offer_mark(fb, center, s_cloud_selection.checked[index]);
        ui_fill_rect_fast((EpdRect){36, rect.y + rect.height - 2, 612, 2}, 0xc0, fb);
    }

    const EpdRect pager = cloud_offer_pager(false);
    if (s_cloud_offer_page) {
        ui_draw_icon(fb, 48, pager.y + pager.height / 2, 22, UI_ICON_CHEVRON_LEFT, UI_GRAY_BLACK);
        ui_text_box(fb, (EpdRect){66, pager.y, pager.width - 30, pager.height}, 20, "上一页", EPD_DRAW_ALIGN_LEFT, false);
    }
    if (s_cloud_offer_page + 1 < pages) {
        const EpdRect next = cloud_offer_pager(true);
        ui_draw_icon(fb, 636, next.y + next.height / 2, 22, UI_ICON_CHEVRON_RIGHT, UI_GRAY_BLACK);
        ui_text_box(fb, (EpdRect){next.x, next.y, next.width - 30, next.height}, 20, "下一页", EPD_DRAW_ALIGN_RIGHT, false);
    }
    if (pages > 1) {
        snprintf(text, sizeof(text), "%u / %u", s_cloud_offer_page + 1, pages);
        ui_text_box(fb, (EpdRect){218, pager.y, 248, pager.height}, 20, text, EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_cloud_action_error || s_cloud_status.total > count) {
        snprintf(text, sizeof(text), "仅显示前 %u 项，其余推送保留在云端", count);
        ui_text_box(fb, cloud_offer_notice(), 20,
                    s_cloud_action_error ? cloud_error(s_cloud_action_error) : text, EPD_DRAW_ALIGN_LEFT, false);
    }
}

static void draw_cloud_offer_empty(uint8_t *fb) {
    const bool busy = s_cloud_status.busy;
    const char *title = busy ? (s_cloud_cancelling ? "正在取消" : "正在获取推送列表") :
        s_cloud_status.error == CZ_CANCELLED ? "已取消" : s_cloud_status.error ? "获取列表失败" :
        s_cloud_requested ? "暂无待下载推送" : "先查看推送";
    char http_error[48];
    const char *detail = s_cloud_status.error == CZ_CANCELLED ? "可以重新获取列表" :
        s_cloud_status.error == CZ_TIMEOUT ? "获取列表超时，可重试" :
        s_cloud_status.error ? cloud_error(s_cloud_status.error) : busy ? "" :
        s_cloud_requested ? "在云造推送文件后，再获取列表" : "查看CloudZao推送的书籍、字体和图片";
    if (s_cloud_status.error == CZ_SERVER) {
        if (s_cloud_status.preview_http_error) {
            snprintf(http_error, sizeof(http_error), "云造服务返回 HTTP %u", (unsigned)s_cloud_status.preview_http_error);
            detail = http_error;
        } else detail = "连接或传输失败，可重试";
    }
    ui_draw_icon(fb, 342, 526, 72, UI_ICON_FOLDER_INPUT, UI_GRAY_BLACK);
    ui_text_box(fb, (EpdRect){36, 613, 612, 56}, 32, title, EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){36, 673, 612, 51}, 22, detail, EPD_DRAW_ALIGN_CENTER, false);
}

static void draw_cloud_offer_footer(uint8_t *fb, unsigned count) {
    const bool busy = s_cloud_status.busy, binding = s_cloud_bind_status.busy;
    if (!count) {
        cloud_action_button(fb, cloud_offer_fetch(), busy ? (s_cloud_cancelling ? "正在取消…" : "取消获取") :
                            "获取推送列表", !busy, !binding && !s_cloud_cancelling);
        return;
    }
    const EpdRect rect = cloud_offer_submit();
    char text[28];
    snprintf(text, sizeof(text), "%u 项", s_cloud_checked);
    ui_text_box(fb, (EpdRect){36, CLOUD_OFFER_SELECTION_Y, 306, 29}, 17, "已选文件", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){36, 1044, 306, 51}, 39, text, EPD_DRAW_ALIGN_LEFT, false);
    const bool enabled = s_cloud_checked && !binding;
    ui_fill_round_rect_raw(fb, rect, 18, enabled ? UI_GRAY_BLACK : 0xe0);
    cloud_offer_icon_label(fb, rect, "下载所选", UI_ICON_DOWNLOAD, false, enabled, enabled ? UI_GRAY_WHITE : 0x90);
}

static void draw_cloud_inbox(uint8_t *fb) {
    const unsigned count = cloud_offer_count();
    ui_text_box(fb, (EpdRect){36, 217, 412, 56}, 39, "推送清单", EPD_DRAW_ALIGN_LEFT, false);
    if (count) {
        char sub[32];
        snprintf(sub, sizeof(sub), "%u 项可下载", s_cloud_selectable);
        ui_text_box(fb, (EpdRect){36, 273, 412, 36}, 23, sub, EPD_DRAW_ALIGN_LEFT, false);
        if (!s_cloud_bind_status.busy) {
            cloud_offer_icon_label(fb, cloud_offer_fetch(), "刷新列表", UI_ICON_REFRESH_CW, true, false, UI_GRAY_BLACK);
        }
        draw_cloud_offer_list(fb, count);
    } else draw_cloud_offer_empty(fb);
    draw_cloud_offer_footer(fb, count);
}

static void draw_cloud_receiver(uint8_t *fb) {
    if (!s_cloud_batch) { draw_cloud_inbox(fb); return; }
    ui_text_box(fb, (EpdRect){36, 220, 612, 52}, 32, "下载推送", EPD_DRAW_ALIGN_LEFT, false);
    const bool busy = s_cloud_status.busy, binding = s_cloud_bind_status.busy;
    cloud_action_button(fb, cloud_button(CLOUD_RECEIVE), busy ? (s_cloud_cancelling ? "正在取消…" : "取消下载") :
                        "重新获取推送列表", !busy, !binding && !s_cloud_cancelling);
    draw_cloud_result(fb);
    draw_cloud_batch(fb);
}

static const char *cloud_bind_title(void) {
    const cz_status_t *status = &s_cloud_bind_status;
    if (s_cloud_cancelling) return "正在暂停";
    if (status->busy) return "正在连接 CloudZao";
    if (status->code[0]) return "绑定码";
    if (status->authorized) return "授权可用";
    if (status->error == CZ_CANCELLED) return "绑定已暂停";
    if (status->error == CZ_AUTH || status->error == CZ_REFRESH_REUSED) return "需要重新授权";
    if (status->error == CZ_ACTIVATION_UNAVAILABLE || status->error == CZ_REFRESH_INTERRUPTED)
        return cloud_error(status->error);
    if (status->error) return "暂时无法绑定";
    return "绑定 CloudZao";
}

static const char *cloud_bind_detail(void) {
    const cz_status_t *status = &s_cloud_bind_status;
    if (s_cloud_cancelling || status->busy) return "请稍候";
    if (status->code[0]) return status->error == CZ_TIMEOUT ? "领取授权超时，正在重试" : "请在 CloudZao App 或 CloudZao 小程序输入此验证码";
    if (status->authorized) return "可以接收推送，无需重新绑定";
    if (status->error == CZ_CANCELLED) return "可继续使用未过期的绑定码";
    if (status->error == CZ_TIMEOUT) return "绑定请求超时，可重试";
    if (status->error == CZ_PAIR_EXPIRED) return "绑定会话已失效，请检查 App 绑定";
    if (status->error) return cloud_error(status->error);
    return "在CloudZao App或小程序中添加这台设备";
}

static bool cloud_bind_needs_rebind(const cz_status_t *status) {
    return status->error == CZ_REFRESH_INTERRUPTED || status->rebind_retry;
}

static bool cloud_bind_action_enabled(void) {
    return !s_cloud_status.busy && !s_cloud_cancelling;
}

static EpdRect cloud_notice_entry(void) { return (EpdRect){36, 606, 612, 76}; }

static EpdRect cloud_notice_button(void) { return (EpdRect){80, 761, 524, 76}; }

static void draw_cloud_notice(uint8_t *fb) {
    const EpdRect card = {36, 242, 612, 632};
    ui_fill_rect_fast((EpdRect){0, 64, UI_LOCK_WIDTH, UI_LOCK_HEIGHT - 64}, 0xe0, fb);
    ui_nav_status(fb);
    ui_fill_round_rect(fb, card, 22, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, card, 22, 0x90);
    ui_text_box(fb, (EpdRect){76, 270, 532, 56}, 36, "注意事项", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){76, 330, 532, 42}, 22, "截至 2026 年 11 月 21 日实测",
                EPD_DRAW_ALIGN_CENTER, false);
    static const char *const books[] = {
        "CloudZao App 仅能上传并推送 TXT、EPUB 原文件。",
        "AZW3、MOBI 会被强制转换为 .ebook。",
        "mogo 暂不支持 .ebook，请使用 TXT 或 EPUB。",
    };
    static const char *const wallpapers[] = {
        "CloudZao App 暂时无法推送壁纸。",
        "请通过 CloudZao 网页端上传并推送。",
    };
    ui_text_box(fb, (EpdRect){76, 398, 532, 42}, 28, "书籍", EPD_DRAW_ALIGN_LEFT, false);
    for (unsigned i = 0; i < sizeof(books) / sizeof(books[0]); ++i)
        ui_text_box(fb, (EpdRect){76, 445 + 37 * (int)i, 532, 37}, 24,
                    books[i], EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){76, 580, 532, 42}, 28, "壁纸", EPD_DRAW_ALIGN_LEFT, false);
    for (unsigned i = 0; i < sizeof(wallpapers) / sizeof(wallpapers[0]); ++i)
        ui_text_box(fb, (EpdRect){76, 627 + 37 * (int)i, 532, 37}, 24,
                    wallpapers[i], EPD_DRAW_ALIGN_LEFT, false);
    if (s_cloud_notice_save_failed)
        ui_text_box(fb, (EpdRect){76, 711, 532, 40}, 22, "未能保存确认，请再试一次",
                    EPD_DRAW_ALIGN_CENTER, false);
    const EpdRect action = cloud_notice_button();
    ui_fill_round_rect(fb, action, 18, UI_GRAY_BLACK);
    ui_text_box(fb, action, 27, s_cloud_notice == CLOUD_NOTICE_FIRST_READ ? "我已了解" : "关闭",
                EPD_DRAW_ALIGN_CENTER, true);
}

static void draw_cloud_app_download(uint8_t *fb) {
    const EpdRect card = {36, 712, 612, 270};
    const int pitch = 6, quiet = 4;
    const int side = pitch * (CLOUDZAO_DOWNLOAD_QR_SIDE + 2 * quiet);
    const EpdRect qr = {396, card.y + (card.height - side) / 2, side, side};
    ui_draw_round_rect(fb, card, 18, 0x90);
    ui_text_box(fb, (EpdRect){62, 773, 312, 58}, 29, "下载 CloudZao App", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){62, 838, 312, 38}, 22, "用手机扫描右侧二维码", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){62, 878, 312, 38}, 22, "打开官方下载页", EPD_DRAW_ALIGN_LEFT, false);
    ui_fill_rect_fast(qr, UI_GRAY_WHITE, fb);
    for (int y = 0; y < CLOUDZAO_DOWNLOAD_QR_SIDE; ++y)
        for (int x = 0; x < CLOUDZAO_DOWNLOAD_QR_SIDE; ++x)
            if (cloudzao_download_qr_rows[y] & (UINT32_C(1) << x))
                ui_fill_rect_fast((EpdRect){qr.x + (quiet + x) * pitch, qr.y + (quiet + y) * pitch, pitch, pitch},
                                  UI_GRAY_BLACK, fb);
}

static void draw_cloud_settings(uint8_t *fb) {
    const cz_status_t *bind = &s_cloud_bind_status;
    const bool receiving = s_cloud_status.busy;
    const bool show_code = bind->code[0] && !s_cloud_cancelling;
    const bool show_hint = !s_cloud_cancelling && !bind->busy && !show_code && !bind->authorized;
    const bool refresh_interrupted = bind->error == CZ_REFRESH_INTERRUPTED;
    const bool show_unbind_hint = show_hint && (refresh_interrupted || bind->error == CZ_ACTIVATION_UNAVAILABLE);
    const EpdRect card = {36, 282, 612, 290};
    ui_text_box(fb, (EpdRect){36, 220, 612, 52}, 32, "设备绑定", EPD_DRAW_ALIGN_LEFT, false);
    ui_draw_round_rect(fb, card, 22, 0x90);
    ui_text_box(fb, (EpdRect){56, show_code ? 297 : 329, 572, 47}, 28,
                cloud_bind_title(), EPD_DRAW_ALIGN_CENTER, false);
    if (show_code) ui_text_box(fb, (EpdRect){56, 344, 572, 78}, 58, bind->code, EPD_DRAW_ALIGN_CENTER, false);
    if (show_unbind_hint) {
        ui_text_box(fb, (EpdRect){56, 378, 572, 40}, 21,
                    refresh_interrupted ? "云端可能仍保留绑定" : "若此设备已绑定，请先在",
                    EPD_DRAW_ALIGN_CENTER, false);
        ui_text_box(fb, (EpdRect){56, 418, 572, 40}, 21,
                    refresh_interrupted ? "如需恢复，请先在 App 解绑" : "CloudZao App 或小程序解绑后重试",
                    EPD_DRAW_ALIGN_CENTER, false);
    } else {
        ui_text_box(fb, (EpdRect){56, show_code ? 422 : 383, 572, 44}, 21,
                    cloud_bind_detail(), EPD_DRAW_ALIGN_CENTER, false);
    }

    const EpdRect action = cloud_button(CLOUD_BIND);
    const bool enabled = cloud_bind_action_enabled(), primary = enabled && !bind->busy && !show_code;
    const char *label = receiving ? "接收结束后可操作" : s_cloud_cancelling ? "正在暂停…" :
        bind->busy || show_code ? "暂停绑定" : bind->authorized ? "检查授权" :
        cloud_bind_needs_rebind(bind) ? "重新绑定" : bind->error == CZ_CANCELLED ? "继续绑定" : "绑定 / 查看授权";
    ui_fill_round_rect(fb, action, 18, primary ? UI_GRAY_BLACK : UI_GRAY_WHITE);
    if (!primary) ui_draw_round_rect(fb, action, 18, 0x50);
    ui_text_box(fb, (EpdRect){action.x + 18, action.y + 8, action.width - 36, action.height - 16}, 25,
                label, EPD_DRAW_ALIGN_CENTER, primary);

    const EpdRect notice = cloud_notice_entry();
    ui_draw_round_rect(fb, notice, 18, 0x90);
    ui_text_box(fb, (EpdRect){56, notice.y, 480, notice.height}, 27, "注意事项", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){536, notice.y, 92, notice.height}, 21, "查看", EPD_DRAW_ALIGN_RIGHT, false);
    draw_cloud_app_download(fb);
}

static void draw_cloud_header(uint8_t *fb) {
    const char *title = "CloudZao";
    const int title_px = 34, center_y = 107, gap = 12;
    const int title_width = ui_text_fixed_width_px(ui_text_effective_px(title_px), title);
    const int left = (UI_LOCK_WIDTH - CLOUDZAO_MARK_WIDTH - gap - title_width) / 2;
    const int top = center_y - CLOUDZAO_MARK_HEIGHT / 2;
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    for (int y = 0; y < CLOUDZAO_MARK_HEIGHT; ++y) {
        for (int x = 0; x < CLOUDZAO_MARK_WIDTH; ++x) {
            const uint8_t pair = cloudzao_mark_gray4[(y * CLOUDZAO_MARK_WIDTH + x) / 2];
            const uint8_t gray = (x & 1) ? pair & 0x0f : pair >> 4;
            if (gray != 15) epd_draw_pixel(left + x, top + y, (uint8_t)(gray << 4), fb);
        }
    }
    ui_text_vc(fb, left + CLOUDZAO_MARK_WIDTH + gap, center_y, title_px, title, EPD_DRAW_ALIGN_LEFT, false);
}

static void draw_cloud(uint8_t *fb) {
    ui_clear_page(fb);
    if (s_cloud_notice != CLOUD_NOTICE_HIDDEN) {
        draw_cloud_notice(fb);
        return;
    }
    draw_cloud_header(fb);
    cloud_tabs(fb);
    if (s_cloud_panel == CLOUD_PANEL_TRANSFER) draw_cloud_receiver(fb);
    else if (s_cloud_panel == CLOUD_PANEL_HISTORY) draw_cloud_history(fb);
    else draw_cloud_settings(fb);
}

static app_redraw_t cloud_switch_panel(cloud_panel_t panel) {
    if (s_cloud_panel == panel) return APP_REDRAW_NONE;
    if (s_cloud_panel == CLOUD_PANEL_SETTINGS && (s_cloud_poll_running || s_cloud_bind_status.busy))
        return APP_REDRAW_NONE;
    if (s_cloud_panel == CLOUD_PANEL_SETTINGS) {
        s_cloud_poll_at = 0;
        if (s_cloud_bind_status.code[0]) s_cloud_bind_status = (cz_status_t){.error = CZ_CANCELLED};
    }
    if (panel == CLOUD_PANEL_HISTORY) {
        if (s_cloud_status.busy || s_cloud_bind_status.busy) return APP_REDRAW_NONE;
        const cz_error_t error = cloudzao_start_history();
        s_cloud_history = (cz_history_status_t){.busy = error == CZ_OK, .error = error};
        s_cloud_history_page = 0;
    } else if (s_cloud_panel == CLOUD_PANEL_HISTORY && s_cloud_history.busy) {
        cloudzao_cancel();
    }
    cloud_reset_history_actions();
    s_cloud_panel = panel;
    return APP_REDRAW_PAGE;
}

static bool cloud_tapped(const ui_gesture_event_t *ev, EpdRect rect) {
    return ui_rect_hit(rect, ev->x0, ev->y0) && ui_rect_hit(rect, ev->x, ev->y);
}

static app_redraw_t cloud_fetch_offers(void) {
    cloudzao_discard_queue();
    cloud_reset_selection();
    s_cloud_batch = false;
    const cz_error_t error = cloudzao_start(CZ_JOB_PREVIEW, NULL);
    s_cloud_requested = error == CZ_OK;
    s_cloud_status = (cz_status_t){.busy = s_cloud_requested, .error = error};
    s_cloud_cancelling = false;
    return APP_REDRAW_PAGE;
}

static app_redraw_t cloud_select_paint(app_ctx_t *ctx) {
    const unsigned count = cloud_offer_count();
    s_area = (EpdRect){36, CLOUD_OFFER_TABLE_TOP, 612, CLOUD_OFFER_END - CLOUD_OFFER_TABLE_TOP};
    ui_fill_rect_fast(s_area, UI_GRAY_WHITE, ctx->fb);
    draw_cloud_offer_list(ctx->fb, count);
    draw_cloud_offer_footer(ctx->fb, count);
    s_cloud_sample_paint = false;
    return APP_REDRAW_AREA;
}

static app_redraw_t cloud_cancel_action(void) {
    if (s_cloud_cancelling) return APP_REDRAW_NONE;
    cloudzao_cancel();
    s_cloud_cancelling = true;
    return APP_REDRAW_PAGE;
}

static app_redraw_t cloud_inbox_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (s_cloud_bind_status.busy) return APP_REDRAW_NONE;
    if (cloud_tapped(ev, cloud_offer_fetch()))
        return s_cloud_status.busy ? cloud_cancel_action() : cloud_fetch_offers();
    if (s_cloud_status.busy) return APP_REDRAW_NONE;
    const unsigned count = cloud_offer_count();
    if (!count) return APP_REDRAW_NONE;
    if (cloud_tapped(ev, cloud_offer_submit())) {
        if (!s_cloud_checked) return APP_REDRAW_NONE;
        const cz_error_t error = cloudzao_start(CZ_JOB_DOWNLOAD, &s_cloud_selection);
        if (error != CZ_OK) { s_cloud_action_error = error; return cloud_select_paint(ctx); }
        s_cloud_batch = true;
        s_cloud_batch_page = 0;
        s_cloud_status = (cz_status_t){.busy = true};
        s_cloud_transfer = (cz_transfer_t){0};
        s_cloud_next_sample_ms = s_cloud_next_layout_ms = 0;
        s_cloud_cancelling = false;
        cloud_reset_selection();
        return APP_REDRAW_PAGE;
    }
    if (s_cloud_selectable && cloud_tapped(ev, cloud_offer_all())) {
        const bool select = s_cloud_checked < s_cloud_selectable;
        s_cloud_checked = 0;
        for (unsigned i = 0; i < count; ++i) {
            cz_kind_t kind;
            s_cloud_selection.checked[i] = select && cloudzao_offer(i, NULL, &kind) && kind != CZ_KIND_NONE;
            if (s_cloud_selection.checked[i]) ++s_cloud_checked;
        }
        s_cloud_action_error = CZ_OK;
        return cloud_select_paint(ctx);
    }
    if (s_cloud_offer_page && cloud_tapped(ev, cloud_offer_pager(false))) { --s_cloud_offer_page; return APP_REDRAW_PAGE; }
    if ((s_cloud_offer_page + 1) * CLOUD_OFFER_PAGE_ROWS < count && cloud_tapped(ev, cloud_offer_pager(true))) {
        ++s_cloud_offer_page;
        return APP_REDRAW_PAGE;
    }
    for (unsigned i = 0; i < CLOUD_OFFER_PAGE_ROWS; ++i) {
        const unsigned index = s_cloud_offer_page * CLOUD_OFFER_PAGE_ROWS + i;
        if (index >= count || !cloud_tapped(ev, cloud_offer_rect(i))) continue;
        cz_kind_t kind;
        if (!cloudzao_offer(index, NULL, &kind) || kind == CZ_KIND_NONE) break;
        if (s_cloud_selection.checked[index]) --s_cloud_checked;
        else ++s_cloud_checked;
        s_cloud_selection.checked[index] = !s_cloud_selection.checked[index];
        s_cloud_action_error = CZ_OK;
        return cloud_select_paint(ctx);
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t cloud_history_clear_gesture(const ui_gesture_event_t *ev) {
    if (!s_cloud_history_confirm) {
        if (!cloud_history_can_clear() || !cloud_tapped(ev, cloud_history_clear_button())) return APP_REDRAW_NONE;
        s_cloud_history_confirm = true;
        s_cloud_history_clear_failed = false;
        return APP_REDRAW_PAGE;
    }
    const bool confirm = cloud_tapped(ev, cloud_history_confirm_button(true));
    if (!confirm && !cloud_tapped(ev, cloud_history_confirm_button(false))) return APP_REDRAW_NONE;
    s_cloud_history_confirm = false;
    if (confirm && cloud_history_can_clear()) {
        const cz_error_t error = cloudzao_start_history_clear();
        s_cloud_history.busy = s_cloud_history_clearing = error == CZ_OK;
        s_cloud_history_clear_failed = error != CZ_OK;
    }
    return APP_REDRAW_PAGE;
}

static app_redraw_t cloud_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (s_cloud_notice != CLOUD_NOTICE_HIDDEN) {
        if (ev->type != UI_GESTURE_TAP || !cloud_tapped(ev, cloud_notice_button()))
            return APP_REDRAW_NONE;
        if (s_cloud_notice == CLOUD_NOTICE_FIRST_READ && !cloudzao_acknowledge_notice()) {
            s_cloud_notice_save_failed = true;
        } else {
            s_cloud_notice = CLOUD_NOTICE_HIDDEN;
            s_cloud_notice_save_failed = false;
        }
        return APP_REDRAW_PAGE;
    }
    const int y = ev->y0;
    if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R) {
        if (y < cloud_tab(CLOUD_PANEL_TRANSFER).y || y >= UI_NAV_TOP) return APP_REDRAW_NONE;
        const int panel = (int)s_cloud_panel + (ev->type == UI_GESTURE_SWIPE_L ? 1 : -1);
        if (panel < CLOUD_PANEL_TRANSFER || panel > CLOUD_PANEL_SETTINGS) return APP_REDRAW_NONE;
        return cloud_switch_panel((cloud_panel_t)panel);
    }
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (cloud_tapped(ev, (EpdRect){22, 65, 90, 83})) {
        if (s_cloud_history_confirm) { s_cloud_history_confirm = false; return APP_REDRAW_PAGE; }
        cloud_leave();
        // CloudZao 只从文件管理进入；快捷设置可能已消耗通用返回记录。
        // Files is the only CloudZao entry; quick settings may have consumed the generic return history.
        ui_nav_request(ctx, 2);
        return APP_REDRAW_NONE;
    }
    for (int panel = CLOUD_PANEL_TRANSFER; panel <= CLOUD_PANEL_SETTINGS; ++panel)
        if (cloud_tapped(ev, cloud_tab(panel))) return cloud_switch_panel((cloud_panel_t)panel);
    if (s_cloud_panel == CLOUD_PANEL_SETTINGS) {
        if (cloud_tapped(ev, cloud_notice_entry())) {
            s_cloud_notice = CLOUD_NOTICE_REVIEW;
            return APP_REDRAW_PAGE;
        }
        if (s_cloud_status.busy) return APP_REDRAW_NONE;
        if (cloud_tapped(ev, cloud_button(CLOUD_BIND))) {
            if (!cloud_bind_action_enabled()) return APP_REDRAW_NONE;
            if (s_cloud_bind_status.busy || s_cloud_poll_running) {
                cloudzao_cancel();
                s_cloud_cancelling = true;
                s_cloud_poll_at = 0;
            } else if (s_cloud_poll_at) {
                s_cloud_poll_at = 0;
                s_cloud_bind_status = (cz_status_t){.error = CZ_CANCELLED};
            } else {
                const bool rebind = cloud_bind_needs_rebind(&s_cloud_bind_status);
                const cz_error_t result = cloudzao_start(rebind ? CZ_JOB_REBIND : CZ_JOB_BIND, NULL);
                s_cloud_bind_status = (cz_status_t){.busy = result == CZ_OK, .error = result, .rebind_retry = rebind};
                s_cloud_cancelling = false;
            }
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    const bool history = s_cloud_panel == CLOUD_PANEL_HISTORY;
    if (history && (s_cloud_history_confirm || cloud_tapped(ev, cloud_history_clear_button())))
        return cloud_history_clear_gesture(ev);
    if (!history && !s_cloud_batch) return cloud_inbox_gesture(ctx, ev);
    unsigned *page = history ? &s_cloud_history_page : &s_cloud_batch_page;
    const unsigned per_page = history ? CLOUD_HISTORY_PAGE_ROWS : CLOUD_BATCH_PAGE_ROWS;
    const unsigned count = history ? s_cloud_history.count : s_cloud_transfer.rows;
    if (history && (s_cloud_history.busy || s_cloud_history.error || count <= per_page)) return APP_REDRAW_NONE;
    if (*page && cloud_tapped(ev, cloud_pager(history, false))) { --*page; return APP_REDRAW_PAGE; }
    if ((*page + 1) * per_page < count && cloud_tapped(ev, cloud_pager(history, true))) {
        ++*page;
        return APP_REDRAW_PAGE;
    }
    if (history || s_cloud_bind_status.busy || !cloud_tapped(ev, cloud_button(CLOUD_RECEIVE))) return APP_REDRAW_NONE;
    return s_cloud_status.busy ? cloud_cancel_action() : cloud_fetch_offers();
}

static void cloud_before_lock(app_ctx_t *ctx) {
    (void)ctx;
    if (s_view != TRANSFER_CLOUDZAO) return;
    const cloud_notice_t notice = s_cloud_notice;
    const bool notice_save_failed = s_cloud_notice_save_failed;
    cloud_leave();
    s_view = TRANSFER_CLOUDZAO;
    s_cloud_notice = notice;
    s_cloud_notice_save_failed = notice_save_failed;
    if (notice == CLOUD_NOTICE_REVIEW) s_cloud_panel = CLOUD_PANEL_SETTINGS;
}
#endif

static void queue_network_start(wifi_transfer_mode_t mode) {
    s_mode = mode;
    s_qr_url = mode == WIFI_TRANSFER_MODE_STA;
    clear_qr();
    s_view = s_wifi_settings_only ? TRANSFER_NETWORKS : TRANSFER_HOME;
    s_forget_confirm = false;
    s_show_saved = s_saved_detail = false;
    s_forget_ssid[0] = 0;
    s_network_page = 0;
    s_scan_pending = false;
    s_pressed = -1;
    clear_password();
    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = s_mode;
    wifi_transfer_get_saved_wifi(s_status.wifi_ssid, &s_status.wifi_configured);
    s_saved_configured = s_status.wifi_configured;
    snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", s_status.wifi_ssid);
    refresh_saved_networks();
    if (s_wifi_settings_only) {
        s_network_connect_pending = true;
        snprintf(s_network_message, sizeof(s_network_message), "正在连接 %s…", s_status.wifi_ssid);
    } else s_start_pending = true;
}

static bool enter_networks(void) {
    if (!stop_upload_if_idle()) return false;
    s_view = TRANSFER_NETWORKS;
    s_forget_confirm = false;
    s_show_saved = s_saved_detail = false;
    s_network_count = s_network_page = 0;
    s_scan_pending = true;
    s_scan_error = ESP_OK;
    s_network_message[0] = 0;
    s_saved_ssid[0] = 0;
    s_saved_configured = false;
    wifi_transfer_get_saved_wifi(s_saved_ssid, &s_saved_configured);
    refresh_saved_networks();
    clear_password();
    return true;
}

static void start_hotspot_service(void) {
    if (s_session_started && s_status.changed_count) s_any_changed = true;
    s_mode = WIFI_TRANSFER_MODE_AP;
    s_qr_url = false;
    clear_qr();
    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = s_mode;
    s_start_pending = true;
    s_view = TRANSFER_HOME;
}

// 提示页先呈现，下一轮才同步扫描；输入页不轮询已停服的传书状态。
// Present the scan notice first, scan synchronously next tick, and do not poll stopped transfer state in the editor.
static esp_err_t start_owned_session(const wifi_transfer_cfg_t *cfg) {
    uint32_t session;
    const esp_err_t result = network_service_start_local_wifi(cfg, &session);
    wifi_transfer_get_status(&s_status);
    if (session) {
        s_owned_session = session;
        s_owned_session_uses_sd = !cfg->network_only && !cfg->is_flash;
    } else if (s_status.session_id != s_owned_session) {
        s_owned_session = 0;
        s_owned_session_uses_sd = false;
    }
    s_session_started = result == ESP_OK;
    return result;
}

static app_redraw_t network_ui_tick(app_ctx_t* ctx) {
    if (s_view != TRANSFER_NETWORKS || s_forget_confirm || ctx->consumed) return APP_REDRAW_NONE;
    if (s_network_connect_pending) {
        s_network_connect_pending = false;
        wifi_transfer_cfg_t cfg = {.mode = WIFI_TRANSFER_MODE_STA, .network_only = true};
        esp_err_t err = start_owned_session(&cfg);
        if (err != ESP_OK) {
            const char *hint = memory_peak_hint(err);
            snprintf(s_network_message, sizeof(s_network_message), "%s", hint ? hint : "连接启动失败，请重试");
        }
        return APP_REDRAW_PAGE;
    }
    if (s_session_started) {
        wifi_transfer_service_poll();
        wifi_transfer_status_t next = {0};
        wifi_transfer_get_status(&next);
        bool changed = next.state != s_status.state || next.network_ready != s_status.network_ready ||
                       next.last_error != s_status.last_error;
        s_status = next;
        if (s_status.network_ready) snprintf(s_network_message, sizeof(s_network_message), "连接成功");
        else if (s_status.state == WIFI_TRANSFER_ERROR) snprintf(s_network_message, sizeof(s_network_message), "连接失败，请检查密码或信号");
        return changed ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
    }
    if (!s_scan_pending || s_show_saved || s_saved_detail) return APP_REDRAW_NONE;
    s_scan_pending = false;
    s_network_count = s_network_page = 0;
    // The scan notice is already in the framebuffer. Drop rebuildable glyph
    // bitmaps before starting WiFi, then recreate them while drawing the result.
    // This prevents the hosted radio from competing with the UI cache at init.
    ttf_font_cache_clear();
    esp_err_t err = network_service_scan_wifi(s_networks, &s_network_count);
    s_scan_error = err;
    if (err != ESP_OK) {
        s_network_count = 0;
        const char *hint = memory_peak_hint(err);
        snprintf(s_network_message, sizeof(s_network_message), "%s",
                 hint ? hint : err == ESP_ERR_NO_MEM ? "内存已清理，请点下方按钮重试" : "扫描失败，请点下方按钮重试");
    }
    else if (!s_network_count) snprintf(s_network_message, sizeof(s_network_message), "未发现网络，请靠近路由器后重扫");
    else s_network_message[0] = 0;
    s_pressed = -1;
    return APP_REDRAW_PAGE;
}

static int provisioning_hit(uint16_t x, uint16_t y) {
    if (s_forget_confirm) {
        for (int i = 11; i <= 12; ++i) if (ui_rect_hit(network_control_rect(i), x, y)) return i;
        return -1;
    }
    if (s_view == TRANSFER_PASSWORD) {
        for (int i = 44; i < 48; ++i) if (ui_rect_hit(password_control_rect(i), x, y)) return i;
    } else {
        size_t count = s_show_saved ? s_saved_count : s_network_count;
        for (int i = 0; i <= 16; ++i) {
            if (s_saved_detail) {
                if (i != 11 && i != 13 && i != 14) continue;
            } else if (i == 10 || i == 12 || i == 13 || i == 14) continue;
            if (i < NETWORK_ROWS && ((!s_show_saved && s_scan_pending) || s_network_page * NETWORK_ROWS + i >= count)) continue;
            if ((i == 6 || i == 11) && !s_saved_configured) continue;
            if (i == 7 && !s_network_page) continue;
            if (i == 9 && (s_network_page + 1) * NETWORK_ROWS >= count) continue;
            if (i == 8 && s_show_saved) continue;
            if (ui_rect_hit(network_control_rect(i), x, y)) return i;
        }
    }
    return -1;
}

static app_redraw_t connect_saved_network(const char *ssid) {
    if (!stop_wifi_for_provisioning()) {
        snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再连接");
        return APP_REDRAW_PAGE;
    }
    s_network_connect_pending = false;
    esp_err_t err = wifi_transfer_select_saved_wifi(ssid);
    if (err == ESP_OK) queue_network_start(WIFI_TRANSFER_MODE_STA);
    else snprintf(s_network_message, sizeof(s_network_message), "读取网络失败，请重试");
    return APP_REDRAW_PAGE;
}

static app_redraw_t provisioning_action(int id) {
    if (s_forget_confirm) {
        if (id == 12) { s_forget_confirm = false; s_network_message[0] = 0; }
        else if (id == 11) {
            // 断联后页面标记可能已丢失；以组件的真实会话为准，上传中不能清配置。
            // A failed reconnect may lose the page flag; stop the actual component session, never an active upload.
            if (!stop_wifi_for_provisioning()) {
                snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再遗忘");
                return APP_REDRAW_PAGE;
            }
            s_network_connect_pending = s_scan_pending = false;
            esp_err_t err = wifi_transfer_forget_saved_wifi(s_forget_ssid);
            if (err == ESP_OK) {
                wifi_transfer_get_status(&s_status);
                s_forget_confirm = false;
                s_saved_detail = false;
                refresh_saved_networks();
                snprintf(s_network_message, sizeof(s_network_message), "已遗忘网络");
            } else snprintf(s_network_message, sizeof(s_network_message), "遗忘失败，请重试或取消");
        }
        return APP_REDRAW_PAGE;
    }
    if (s_view == TRANSFER_NETWORKS) {
        if (s_saved_detail) {
            if (id == 13) return connect_saved_network(s_selected.ssid);
            if (id == 14) {
                if (!stop_wifi_for_provisioning()) {
                    snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再修改");
                    return APP_REDRAW_PAGE;
                }
                s_network_connect_pending = s_scan_pending = false;
                clear_password(); s_network_message[0] = 0;
                s_selected.requires_password = true;
                s_saved_detail = false;
                ui_keyboard_begin(&s_password_input, true);
                s_view = TRANSFER_PASSWORD;
            } else if (id == 11) {
                snprintf(s_forget_ssid, sizeof(s_forget_ssid), "%s", s_selected.ssid);
                s_forget_confirm = true; s_network_message[0] = 0;
            }
            return APP_REDRAW_PAGE;
        }
        if (id < NETWORK_ROWS) {
            size_t index = s_network_page * NETWORK_ROWS + (size_t)id;
            if (s_show_saved) {
                s_selected = (wifi_transfer_network_t){.supported = true,
                    .requires_password = s_saved_networks[index].requires_password};
                snprintf(s_selected.ssid, sizeof(s_selected.ssid), "%s", s_saved_networks[index].ssid);
                s_saved_detail = true; s_network_message[0] = 0;
                return APP_REDRAW_PAGE;
            }
            s_selected = s_networks[s_network_page * NETWORK_ROWS + id];
            if (!s_selected.supported) {
                snprintf(s_network_message, sizeof(s_network_message), "此网络认证暂不支持，请选择其他网络");
                return APP_REDRAW_PAGE;
            }
            if (saved_network_index(s_selected.ssid) >= 0) return connect_saved_network(s_selected.ssid);
            if (!stop_wifi_for_provisioning()) {
                snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再连接");
                return APP_REDRAW_PAGE;
            }
            s_network_connect_pending = false;
            if (s_saved_count == WIFI_TRANSFER_SAVED_MAX) {
                snprintf(s_network_message, sizeof(s_network_message), "已保存 8 个网络，请先遗忘不用的网络");
                return APP_REDRAW_PAGE;
            }
            if (!s_selected.requires_password) {
                esp_err_t err = wifi_transfer_save_wifi(s_selected.ssid, "");
                if (err == ESP_OK) {
                    queue_network_start(WIFI_TRANSFER_MODE_STA);
                    return APP_REDRAW_PAGE;
                }
                snprintf(s_network_message, sizeof(s_network_message), "开放网络保存失败，请重试");
                return APP_REDRAW_PAGE;
            }
            clear_password();
            s_network_message[0] = 0;
            ui_keyboard_begin(&s_password_input, true);
            s_view = TRANSFER_PASSWORD;
        } else if (id == 6) {
            if (s_status.network_ready) {
                if (!stop_wifi_for_provisioning()) {
                    snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再断开");
                    return APP_REDRAW_PAGE;
                }
                s_network_connect_pending = false;
                snprintf(s_network_message, sizeof(s_network_message), "已断开，连接信息保留");
            } else {
                // 未连上不等于驱动已停止；重试前先释放仍在等待或超时的旧 STA。
                // Disconnected does not mean stopped; release the pending or timed-out STA before retrying.
                if (!stop_wifi_for_provisioning()) {
                    snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再连接");
                    return APP_REDRAW_PAGE;
                }
                s_network_connect_pending = false;
                queue_network_start(WIFI_TRANSFER_MODE_STA);
            }
        }
        else if (id == 7 && s_network_page) --s_network_page;
        else if (id == 8) {
            if (!stop_wifi_for_provisioning()) {
                snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再扫描");
                return APP_REDRAW_PAGE;
            }
            s_network_connect_pending = false;
            s_scan_pending = true; s_scan_error = ESP_OK; s_network_message[0] = 0;
        }
        else if (id == 9 && (s_network_page + 1) * NETWORK_ROWS < (s_show_saved ? s_saved_count : s_network_count)) ++s_network_page;
        else if (id == 10) queue_network_start(s_mode);
        else if (id == 11) {
            snprintf(s_forget_ssid, sizeof(s_forget_ssid), "%s", s_saved_ssid);
            s_forget_confirm = true; s_network_message[0] = 0;
        }
        else if (id == 15 || id == 16) {
            s_show_saved = id == 16; s_network_page = 0;
            refresh_saved_networks();
        }
        return APP_REDRAW_PAGE;
    }
    size_t len = strlen(s_password);
    if (id == 44) { clear_password(); s_network_message[0] = 0; s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
    else if (id == 45) s_password_visible = !s_password_visible;
    else if (id == 46) {
        if (s_selected.requires_password && len < 8) {
            snprintf(s_network_message, sizeof(s_network_message), "加密网络密码至少 8 个字符");
        } else {
            esp_err_t err = wifi_transfer_save_wifi(s_selected.ssid,
                s_selected.requires_password ? s_password : "");
            if (err == ESP_OK) { queue_network_start(WIFI_TRANSFER_MODE_STA); return APP_REDRAW_PAGE; }
            snprintf(s_network_message, sizeof(s_network_message), "%s", err == ESP_ERR_INVALID_ARG ?
                     "密码须 8–63 字符或 64 位十六进制" : err == ESP_ERR_NO_MEM ?
                     "已保存 8 个网络，请先遗忘不用的网络" : "保存失败，输入已保留，请重试");
        }
    }
    return APP_REDRAW_AREA;
}

static bool s_input_layout, s_input_settle;
static app_redraw_t password_paint(app_ctx_t *ctx, bool field, bool controls) {
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, 560, password_control_rect(47),
        UI_PX_BODY, !s_password_visible, NULL, field);
    if (controls) {
        ui_clear_rect_fast(ctx->fb, (EpdRect){36, 377, 612, 36});
        ui_text_fixed_vc(ctx->fb, 36, 399, 22, s_network_message[0] ? s_network_message : "支持英文、数字、符号和空格", EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 44; i <= 46; ++i) {
            EpdRect button = password_control_rect(i); ui_clear_rect_fast(ctx->fb, button);
            provisioning_button(ctx->fb, button, i == 44 ? "取消" : i == 45 ? s_password_visible ? "隐藏" : "显示" : "连接并保存", i);
        }
        EpdRect extra = {UI_MARGIN, 215, ui_content_width(), 287};
        update.area = update.area.width ? ui_rect_union(update.area, extra) : extra;
    }
    s_area = update.area; s_input_layout = update.layout; s_input_settle = update.settle; s_settle = false;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static app_redraw_t provisioning_gesture(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if (s_view == TRANSFER_PASSWORD) {
        if (ev->type == UI_GESTURE_PRESS)
            return ui_keyboard_press(ev->x0, ev->y0, 560, ctx->now_ms) ? password_paint(ctx, false, false) : APP_REDRAW_NONE;
        bool feedback = ev->type != UI_GESTURE_LONG_PRESS && ui_keyboard_release();
        if (ev->type != UI_GESTURE_TAP) return feedback ? password_paint(ctx, false, false) : APP_REDRAW_NONE;
        int x = ev->x0, y = ev->y0;
        if (ui_text_input_tap(&s_password_input, password_control_rect(47), UI_PX_BODY, !s_password_visible, NULL, x, y)) {
            return password_paint(ctx, true, false);
        }
        for (int i = 44; i <= 46; ++i) if (ui_rect_hit(password_control_rect(i), x, y)) {
            app_redraw_t changed = provisioning_action(i);
            if (changed == APP_REDRAW_AREA) return password_paint(ctx, true, true);
            return changed;
        }
        ui_keyboard_result_t result = ui_keyboard_tap(x, y, 560, ctx->now_ms);
        if (result == UI_KEYBOARD_DONE) {
            app_redraw_t changed = provisioning_action(46);
            if (changed == APP_REDRAW_PAGE) return changed;
            result = UI_KEYBOARD_CHANGED;
        }
        if (result == UI_KEYBOARD_CHANGED) return password_paint(ctx, false, y >= 990 && x >= 534);
        return APP_REDRAW_NONE;
    }
    int old = s_pressed;
    int origin = provisioning_hit(ev->x0, ev->y0);
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    bool action = ev->type == UI_GESTURE_TAP && old >= 0 && old == origin && provisioning_hit(ev->x, ev->y) == origin;
    EpdRect old_rect = s_view == TRANSFER_PASSWORD ? password_control_rect(old >= 0 ? old : origin >= 0 ? origin : 0)
                                                  : network_control_rect(old >= 0 ? old : origin >= 0 ? origin : 0);
    if (action) {
        if (s_view == TRANSFER_PASSWORD && origin == 47) {
            ui_text_input_tap(&s_password_input, password_control_rect(47), UI_PX_BODY,
                              !s_password_visible, NULL, ev->x0, ev->y0);
            render(ctx, ctx->fb);
            s_area = password_control_rect(47);
            s_settle = false;
            return APP_REDRAW_AREA;
        }
        app_redraw_t redraw = provisioning_action(origin);
        if (redraw == APP_REDRAW_PAGE) return redraw;
        render(ctx, ctx->fb);
        s_area = ui_rect_union(old_rect, (EpdRect){UI_MARGIN, 240, ui_content_width(), 744});
        s_settle = false;
        return APP_REDRAW_AREA;
    }
    if (old == s_pressed) return APP_REDRAW_NONE;
    render(ctx, ctx->fb);
    s_area = old_rect;
    s_settle = false;
    return APP_REDRAW_AREA;
}

static EpdRect status_rect(void) { return (EpdRect){36, 676, 612, 270}; }
static book_store_root_t s_capacity_roots[] = {{.is_flash = false}, {.is_flash = true}};
static uint64_t free_bytes(void* arg) { return book_store_free_bytes(arg); }

static void draw_status(uint8_t* fb) {
    EpdRect area = status_rect();
    ui_clear_rect_fast(fb, area);
    ui_fill_round_rect(fb, area, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, area, 24, UI_GRAY_BLACK);
    const char* state = "网络已停止";
    if (s_start_pending || s_status.state == WIFI_TRANSFER_STARTING) state = "正在准备传书…";
    else if (s_status.state == WIFI_TRANSFER_READY) state = "等待上传";
    else if (s_status.state == WIFI_TRANSFER_UPLOADING) state = "正在接收";
    else if (s_status.state == WIFI_TRANSFER_ERROR) state = s_status.network_ready ? "上传失败，请重试" :
        s_mode == WIFI_TRANSFER_MODE_AP ? "热点传书未启动" : "WiFi 传书未启动";
    if (s_media_lost) state = "存储已移除，传书已停止";
    ui_text(fb, area.x + 28, area.y + 24, 26, state, EPD_DRAW_ALIGN_LEFT, false);
    char line[256];
    if (s_media_lost) snprintf(line, sizeof(line), "重新进入传书可使用内置存储");
    else if (s_status.state == WIFI_TRANSFER_ERROR && !s_status.network_ready) {
        const char *hint = memory_peak_hint(s_status.last_error);
        snprintf(line, sizeof(line), "原因：%s", hint ? hint : esp_err_to_name(s_status.last_error));
    }
    else if (s_mode == WIFI_TRANSFER_MODE_AP)
        snprintf(line, sizeof(line), "已连接 %u 台 · 已完成 %u 本", s_status.sta_count, s_status.done_count);
    else snprintf(line, sizeof(line), "同网浏览器上传 · 已完成 %u 本", s_status.done_count);
    ui_text(fb, area.x + 28, area.y + 68, 19, line, EPD_DRAW_ALIGN_LEFT, false);
    snprintf(line, sizeof(line), "%s", s_status.cur_name);
    while (*line && ttf_text_width_px(ui_text_effective_px(19), line) > area.width - 280) {
        size_t n = strlen(line) - 1;
        while (n && ((unsigned char)line[n] & 0xc0) == 0x80) --n;
        line[n] = 0;
    }
    ui_text(fb, area.x + 28, area.y + 108, 19, line[0] ? line : "尚无正在传输的文件", EPD_DRAW_ALIGN_LEFT, false);
    snprintf(line, sizeof(line), "%.1f / %.1f MB", s_status.cur_bytes / 1048576.0, s_status.cur_total / 1048576.0);
    ui_text(fb, area.x + area.width - 28, area.y + 108, 19, line, EPD_DRAW_ALIGN_RIGHT, false);
    EpdRect track = {area.x + 28, area.y + 150, area.width - 56, 10};
    ui_fill_round_rect(fb, track, 5, 0xc8);
    if (s_status.cur_total) {
        uint64_t width = (uint64_t)s_status.cur_bytes * track.width / s_status.cur_total;
        track.width = width > (uint64_t)track.width ? track.width : (int)width;
        ui_fill_round_rect(fb, track, 5, UI_GRAY_BLACK);
    }
    char capacity[112];
    if (s_media_lost) snprintf(capacity, sizeof(capacity), "TF 卡已移除，原存储已停用");
    else snprintf(capacity, sizeof(capacity), "%s · 剩余 %.1f MB%s", s_root.is_flash ? "内置" : "TF 卡",
                  s_free / 1048576.0, s_root.is_flash ? " · 单文件 ≤ 1 MB" : "");
    ui_text(fb, area.x + 28, area.y + 190, 18, capacity, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, area.x + 28, area.y + 228, 18,
            s_status.last_error != ESP_OK ? "传输未完成，请检查网络后重试" : "同名文件会覆盖；完成后自动更新书架",
            EPD_DRAW_ALIGN_LEFT, false);
}

static EpdRect control_rect(int id) {
    if (id == 0) return (EpdRect){36, 608, 296, 56};
    if (id == 1) return (EpdRect){352, 608, 296, 56};
    if (id == 2) return (EpdRect){36, 976, 612, 76};
    return (EpdRect){0};
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    if (s_usb_entry) {
        ui_clear_page(fb);
        bool active = usb_storage_active();
        transfer_header(fb, "USB 读卡", NULL);
        ui_text(fb, 42, 181, 20, "连接状态", EPD_DRAW_ALIGN_LEFT, false);
        EpdRect status = {36, 211, 612, 190};
        ui_fill_round_rect(fb, status, 24, UI_GRAY_WHITE);
        ui_draw_round_rect(fb, status, 24, 0x90);
        const char *state = s_usb_start_pending ? "正在准备 TF 卡" :
            !active ? "USB 读卡未开启" :
            s_usb_host_connected ? "电脑已连接" : "等待电脑连接";
        ui_text(fb, 64, 241, 31, state, EPD_DRAW_ALIGN_LEFT, false);
        if (active) {
            ui_text(fb, 64, 296, 20, "磁盘名称  SD_CARD", EPD_DRAW_ALIGN_LEFT, false);
            char size[64];
            snprintf(size, sizeof(size), "TF 卡容量  %.1f GB", s_usb_capacity_bytes / 1073741824.0);
            ui_text(fb, 64, 337, 20, size, EPD_DRAW_ALIGN_LEFT, false);
        } else ui_text(fb, 64, 309, 20, s_usb_message, EPD_DRAW_ALIGN_LEFT, false);

        ui_text(fb, 42, 450, 20, "文件存放位置", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, (EpdRect){36, 478, 612, 206}, 24, UI_GRAY_WHITE);
        ui_text(fb, 64, 510, 27, "books", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 553, 19, "EPUB、TXT 书籍", EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb, 580, 64, 556, 0xb0);
        ui_text(fb, 64, 597, 27, "fonts", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 640, 19, "TTF、OTF 字体", EPD_DRAW_ALIGN_LEFT, false);

        ui_text(fb, 42, 740, 20, "完成传输", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, (EpdRect){36, 766, 612, 147}, 24, UI_GRAY_WHITE);
        ui_text(fb, 64, 802, 21, "1  在电脑上等待复制完成", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 855, 21, "2  安全弹出 SD_CARD，再结束读卡", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, control_rect(2), 22, s_pressed == 2 ? 0x60 : 0x38);
        ui_text_vc(fb, 342, 1014, 24, active ? "结束 USB 读卡" : "返回文件管理",
                   EPD_DRAW_ALIGN_CENTER, true);
        return;
    }
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    if (s_view == TRANSFER_CLOUDZAO) { draw_cloud(fb); return; }
#endif
    if (s_view == TRANSFER_NETWORKS) { draw_networks(fb); return; }
    if (s_view == TRANSFER_PASSWORD) { draw_password(fb); return; }
    ui_clear_page(fb);
    transfer_header(fb, s_mode == WIFI_TRANSFER_MODE_AP ? "热点传书" : "WiFi 传书", NULL);
    EpdRect connection = {36, 168, 612, 132};
    ui_fill_round_rect(fb, connection, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, connection, 24, UI_GRAY_BLACK);
    char name[64];
    snprintf(name, sizeof(name), "%s", s_mode == WIFI_TRANSFER_MODE_AP ? s_status.ssid : s_status.wifi_ssid);
    if (!name[0]) snprintf(name, sizeof(name), "%s", s_mode == WIFI_TRANSFER_MODE_AP ? "mogo 热点" : "已保存的 WiFi");
    fit_label(name, 26, 420);
    ui_text(fb, 66, 190, 26, name, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 66, 236, 19,
            s_mode == WIFI_TRANSFER_MODE_AP ? "口令：" WIFI_TRANSFER_PASSWORD :
            s_status.network_ready ? "设备已连接；手机需同网" :
            s_start_pending || s_status.state == WIFI_TRANSFER_STARTING ? "正在连接已保存网络…" :
            s_status.wifi_configured ? "已保存，尚未连接" : "尚未选择 WiFi",
            EPD_DRAW_ALIGN_LEFT, false);
    const bool stopped = !s_start_pending && s_status.state == WIFI_TRANSFER_STOPPED;
    ui_text_vc(fb, 605, 234, 20, s_status.network_ready ? "服务已开启" : stopped ? "已停止" : "准备中",
               EPD_DRAW_ALIGN_RIGHT, false);
    int url_px = UI_PX_CAPTION;
    while (url_px > 14 && ttf_text_width_px(url_px, s_status.url) > 560) --url_px;
    if (s_status.network_ready && s_qr_ready)
        ui_wifi_qr_draw(fb, (EpdRect){222, 330, 240, 240});
    else {
        ui_draw_round_rect(fb, (EpdRect){222, 330, 240, 240}, 8, 0xa0);
        ui_text_vc(fb, 342, 450, 22,
                   stopped ? "网络已停止" : s_start_pending ? "正在生成二维码…" : "等待网络地址…",
                   EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_mode == WIFI_TRANSFER_MODE_AP) {
        ui_text_vc(fb, 342, 583, 20,
                   s_status.network_ready ? s_status.url : "热点就绪后显示访问地址",
                   EPD_DRAW_ALIGN_CENTER, false);
        for (int i = 0; i < 2; ++i) {
            EpdRect tab = control_rect(i);
            ui_fill_round_rect(fb, tab, 18, (s_qr_url == (i == 1)) ? 0x42 : UI_GRAY_WHITE);
            ui_draw_round_rect(fb, tab, 18, 0x92);
            ui_text_vc(fb, tab.x + tab.width / 2, tab.y + tab.height / 2, 21,
                       i ? "打开传书网页" : "连接 mogo 热点", EPD_DRAW_ALIGN_CENTER, s_qr_url == (i == 1));
        }
    } else {
        ui_text_vc(fb, 342, 602, url_px,
                   s_status.network_ready ? s_status.url : "服务启动后显示访问地址", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 642, 18, "手机或电脑扫码后，即可在浏览器上传内容", EPD_DRAW_ALIGN_CENTER, false);
    }
    draw_status(fb);
    EpdRect back = control_rect(2);
    if (s_pressed == 2) ui_draw_pressed_round_rect(fb, back, UI_BTN_RADIUS);
    ui_draw_button(fb, back,
                   s_mode == WIFI_TRANSFER_MODE_AP && s_status.state == WIFI_TRANSFER_ERROR && !s_status.network_ready
                       ? "重试热点传书" : stopped ? "返回" : "停止传输服务", false);
    ui_nav_draw(fb, 2);
}

static void on_enter(app_ctx_t* ctx) {
    (void)ctx;
    // 导航返回仍打开原入口，但只在显式请求时启动上传或 USB 服务。
    // History returns reopen the previous entry; only explicit requests start upload or USB services.
    const bool requested = s_entry_requested != TRANSFER_ENTRY_NONE;
    if (requested) s_entry = s_entry_requested;
    s_entry_requested = TRANSFER_ENTRY_NONE;
    s_wifi_settings_only = s_entry == TRANSFER_ENTRY_WIFI_SETUP;
    s_usb_entry = s_entry == TRANSFER_ENTRY_USB;
    s_mode = s_entry == TRANSFER_ENTRY_HOTSPOT ? WIFI_TRANSFER_MODE_AP : WIFI_TRANSFER_MODE_STA;
    display_set_bulk_io(true);
    s_media_lost = false;
    memset(&s_status, 0, sizeof(s_status));
    s_qr_url = s_mode == WIFI_TRANSFER_MODE_STA;
    clear_qr();
    memset(&s_root, 0, sizeof(s_root));
    s_heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    s_internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_start_pending = false;
    s_usb_message[0] = 0;
    s_usb_start_pending = false;
    s_usb_retry_ms = 0;
    s_usb_host_connected = false;
    s_usb_capacity_bytes = 0;
    s_view = TRANSFER_HOME;
    s_forget_confirm = false;
    s_show_saved = s_saved_detail = false;
    s_saved_count = s_network_count = s_network_page = 0;
    s_saved_ssid[0] = s_network_message[0] = s_forget_ssid[0] = 0;
    s_saved_configured = false;
    s_scan_pending = false;
    s_scan_error = ESP_OK;
    s_network_connect_pending = false;
    clear_password();
    s_session_started = false;
    s_owned_session = 0;
    s_owned_session_uses_sd = false;
    s_pressed = -1;
    s_settle = s_any_changed = false;
    s_status.mode = s_mode;
    s_poll_ms = 0;
    s_free = 0;
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    if (s_entry == TRANSFER_ENTRY_CLOUDZAO) {
        s_view = TRANSFER_CLOUDZAO;
        s_cloud_notice = cloudzao_notice_acknowledged() ? CLOUD_NOTICE_HIDDEN : CLOUD_NOTICE_FIRST_READ;
        s_cloud_notice_save_failed = false;
        return;
    }
#endif
    if (s_usb_entry) {
        if (requested) {
            board_sd_info_t usb_sd = {0};
            if (board_sd_get_info(&usb_sd) == ESP_OK) s_usb_capacity_bytes = usb_sd.capacity_bytes;
            s_usb_start_pending = true;
            snprintf(s_usb_message, sizeof(s_usb_message), "正在卸载本机 TF 卡并启动 USB…");
        }
        return;
    }
    if (s_entry == TRANSFER_ENTRY_HOTSPOT) {
        if (requested) start_hotspot_service();
        return;
    }

    wifi_transfer_get_saved_wifi(s_status.wifi_ssid, &s_status.wifi_configured);
    if (!s_wifi_settings_only && s_status.wifi_configured) {
        // 先排定前台启动；准入后再由网络事务替换常驻会话。
        // Queue foreground startup; replace the current session only after admission.
        if (requested) queue_network_start(WIFI_TRANSFER_MODE_STA);
        return;
    }

    s_view = TRANSFER_NETWORKS;
    if (s_wifi_settings_only) {
        wifi_transfer_get_status(&s_status);
        if (s_status.network_ready && s_status.mode == WIFI_TRANSFER_MODE_STA) {
            s_session_started = true;
            s_saved_configured = s_status.wifi_configured;
            snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", s_status.wifi_ssid);
            refresh_saved_networks();
            snprintf(s_network_message, sizeof(s_network_message), "连接成功");
            return;
        }
    }
    if (!enter_networks()) {
        snprintf(s_network_message, sizeof(s_network_message), "网络正在结束，请稍后再试");
    }
}

static void stop_session(void) {
    clear_qr();
    wifi_transfer_get_status(&s_status);
    if (s_owned_session && s_status.session_id == s_owned_session && s_status.changed_count)
        s_any_changed = true;
    s_session_started = false;
    s_owned_session = 0;
    s_owned_session_uses_sd = false;
}

static bool stop_upload_if_idle(void) {
    if (!network_service_stop_owned_wifi(s_owned_session, false)) return false;
    stop_session();
    s_start_pending = false;
    return true;
}

static bool stop_wifi_for_provisioning(void) {
    // 用户明确断开、换网或遗忘；仅此类动作可停用页面并未创建的空闲 STA。
    // Explicit disconnect, network selection or forget may stop idle STA not started by this page.
    if (!network_service_stop_local_wifi()) return false;
    stop_session();
    s_start_pending = false;
    return true;
}

// 必须先等待 HTTP 退出，再清除旧根；不会让在途请求写到另一存储。
// Join HTTP before clearing the old root; in-flight requests never move to another storage.
static void transfer_on_media_lost(app_ctx_t* ctx) {
    (void)ctx;
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    if (s_view == TRANSFER_CLOUDZAO) {
        if (s_cloud_panel == CLOUD_PANEL_HISTORY) {
            cloud_reset_history_actions();
            s_cloud_history = (cz_history_status_t){.error = CZ_MEDIA};
        }
        return;
    }
#endif
    // 配网不依赖 TF 卡，也不能因拔卡取消输入或网络连接。
    // Provisioning does not depend on the TF card; media loss must not cancel input or networking.
    if (s_view != TRANSFER_HOME || s_usb_entry) return;
    if (s_owned_session ? !s_owned_session_uses_sd : !s_start_pending || s_root.is_flash) return;
    const bool stopped = network_service_stop_owned_wifi(s_owned_session, true);
    if (!stopped) network_service_drop_owned_wifi(s_owned_session, true);
    stop_session();
    s_status = (wifi_transfer_status_t){.mode = s_mode};
    s_start_pending = s_scan_pending = s_network_connect_pending = false;
    clear_password();
    s_pressed = -1;
    s_free = 0;
    if (stopped) memset(&s_root, 0, sizeof(s_root));
    s_media_lost = true;
    s_settle = false;
}

static void transfer_on_exit(app_ctx_t* ctx) {
    (void)ctx;
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    cloud_leave();
#endif
    s_usb_start_pending = false;
    if (usb_storage_active()) {
        esp_err_t err = usb_storage_stop();
        if (err == ESP_OK) s_any_changed = true;
        else ESP_LOGW(TAG, "USB remained active on page exit: %s", esp_err_to_name(err));
    }
    clear_password();
    s_scan_pending = s_network_connect_pending = false;
    wifi_transfer_status_t latest = {0};
    wifi_transfer_get_status(&latest);
    bool keep_sta = s_wifi_settings_only && latest.network_ready && latest.network_only &&
                    latest.mode == WIFI_TRANSFER_MODE_STA;
    if (!keep_sta && !network_service_stop_owned_wifi(s_owned_session, false))
        network_service_drop_owned_wifi(s_owned_session, false);
    stop_session();
    s_wifi_settings_only = false;
    s_usb_entry = false;
    display_set_bulk_io(false);
    if (s_any_changed) book_store_notify_changed();
    ESP_LOGI(TAG, "leave keep_sta=%d heap before=%u after=%u internal_before=%u internal_after=%u",
             keep_sta,
             (unsigned)s_heap_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)s_internal_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    if (s_view == TRANSFER_CLOUDZAO) {
        if (s_cloud_notice == CLOUD_NOTICE_FIRST_READ) return APP_REDRAW_NONE;
        if (s_cloud_panel == CLOUD_PANEL_HISTORY) {
            if (!s_cloud_history.busy) return APP_REDRAW_NONE;
            const cz_history_status_t history = cloudzao_history_status();
            if (history.busy) return APP_REDRAW_NONE;
            s_cloud_history = history;
            if (s_cloud_history_clearing) {
                s_cloud_history_clearing = false;
                s_cloud_history_clear_failed = history.error != CZ_OK && history.error != CZ_MEDIA;
                if (s_cloud_history_clear_failed) s_cloud_history.error = CZ_OK;
                else s_cloud_history_page = 0;
            }
            return APP_REDRAW_PAGE;
        }
        if (s_cloud_bind_status.busy || s_cloud_poll_running) {
            cz_status_t status = cloudzao_status();
            if (status.busy) return APP_REDRAW_NONE;
            if (s_cloud_cancelling) status = (cz_status_t){.error = CZ_CANCELLED,
                .rebind_retry = s_cloud_bind_status.rebind_retry && !status.code[0]};
            const bool same_waiting_card = s_cloud_poll_running && status.code[0] &&
                !strcmp(status.code, s_cloud_bind_status.code) &&
                (status.error == CZ_TIMEOUT) == (s_cloud_bind_status.error == CZ_TIMEOUT);
            s_cloud_bind_status = status;
            s_cloud_poll_running = false;
            s_cloud_cancelling = false;
            s_cloud_poll_at = status.poll_after_s ? ctx->now_ms + (int64_t)status.poll_after_s * 1000 : 0;
            return same_waiting_card ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
        }
        if (s_cloud_panel == CLOUD_PANEL_SETTINGS && s_cloud_poll_at && ctx->now_ms >= s_cloud_poll_at) {
            const cz_error_t error = cloudzao_start(CZ_JOB_POLL, NULL);
            if (error == CZ_RESOURCES) { s_cloud_poll_at = ctx->now_ms + 2000; return APP_REDRAW_NONE; }
            s_cloud_poll_at = 0;
            if (error == CZ_OK) { s_cloud_poll_running = true; return APP_REDRAW_NONE; }
            s_cloud_bind_status = (cz_status_t){.error = error};
            return APP_REDRAW_PAGE;
        }
        if (!s_cloud_status.busy) return APP_REDRAW_NONE;
        const cz_status_t status = cloudzao_status();
        if (!s_cloud_batch) {
            if (status.busy) return APP_REDRAW_NONE;
            s_cloud_status = status;
            if (status.error == CZ_OK) s_cloud_bind_status = (cz_status_t){.authorized = true};
            else if (status.error == CZ_AUTH || status.error == CZ_REFRESH_REUSED ||
                     status.error == CZ_REFRESH_INTERRUPTED)
                s_cloud_bind_status = (cz_status_t){.error = status.error};
            s_cloud_cancelling = false;
            const unsigned count = cloud_offer_count();
            for (unsigned i = 0; i < count; ++i) {
                cz_kind_t kind;
                if (cloudzao_offer(i, NULL, &kind) && kind != CZ_KIND_NONE) ++s_cloud_selectable;
            }
            return APP_REDRAW_PAGE;
        }
        cz_transfer_t transfer = cloudzao_transfer_status();
        if (!status.busy || s_cloud_panel != CLOUD_PANEL_TRANSFER) {
            s_cloud_status = status;
            s_cloud_transfer = transfer;
            if (!status.busy) {
                if (status.error == CZ_AUTH || status.error == CZ_REFRESH_REUSED ||
                    status.error == CZ_REFRESH_INTERRUPTED)
                    s_cloud_bind_status = (cz_status_t){.error = status.error};
                s_cloud_cancelling = false;
            }
            return status.busy ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
        }

        const bool total_changed = status.total != s_cloud_status.total;
        const bool repaint_card = total_changed || status.processed != s_cloud_status.processed ||
            transfer.active != s_cloud_transfer.active ||
            (transfer.active && (transfer.current != s_cloud_transfer.current ||
                                 transfer.file.state != s_cloud_transfer.file.state)) ||
            (!transfer.active && (status.saved != s_cloud_status.saved || status.pending != s_cloud_status.pending ||
                                  status.skipped != s_cloud_status.skipped || status.failed != s_cloud_status.failed));
        const bool repaint_batch = total_changed || transfer.revision != s_cloud_transfer.revision;
        if (repaint_card || repaint_batch) {
            if (ctx->now_ms < s_cloud_next_layout_ms) return APP_REDRAW_NONE;
            s_cloud_status = status;
            // 批次单独更新时，保留卡片已经推屏的采样值。/ Keep the card's displayed sample on batch-only updates.
            if (!repaint_card) {
                transfer.received = s_cloud_transfer.received;
                transfer.rate = s_cloud_transfer.rate;
            }
            s_cloud_transfer = transfer;
            if (repaint_card) {
                s_area = cloud_result_area();
                ui_fill_rect_fast(s_area, UI_GRAY_WHITE, ctx->fb);
                draw_cloud_result(ctx->fb);
                s_cloud_next_sample_ms = ctx->now_ms + CLOUD_SAMPLE_PAINT_MS;
            }
            if (repaint_batch) {
                const EpdRect batch = cloud_batch_area();
                ui_fill_rect_fast(batch, UI_GRAY_WHITE, ctx->fb);
                draw_cloud_batch(ctx->fb);
                s_area = repaint_card ? ui_rect_union(s_area, batch) : batch;
            }
            s_cloud_next_layout_ms = ctx->now_ms + CLOUD_LAYOUT_PAINT_MS;
            s_cloud_sample_paint = false;
            return APP_REDRAW_AREA;
        }
        if (!transfer.active || transfer.file.state != CZ_ROW_DOWNLOAD || ctx->now_ms < s_cloud_next_sample_ms ||
            (transfer.received == s_cloud_transfer.received && transfer.rate == s_cloud_transfer.rate))
            return APP_REDRAW_NONE;

        const cloud_sample_t before = cloud_sample(&s_cloud_transfer), next = cloud_sample(&transfer);
        s_cloud_status = status;
        s_cloud_transfer = transfer;
        s_cloud_next_sample_ms = ctx->now_ms + CLOUD_SAMPLE_PAINT_MS;
        if (before.fill_width == next.fill_width && !strcmp(before.detail, next.detail) && !strcmp(before.rate, next.rate))
            return APP_REDRAW_NONE;
        s_area = cloud_sample_area();
        ui_fill_rect_fast(s_area, UI_GRAY_WHITE, ctx->fb);
        draw_cloud_sample(ctx->fb, &next);
        s_cloud_sample_paint = true;
        return APP_REDRAW_AREA;
    }
#endif
    if (s_view == TRANSFER_PASSWORD) {
        if (ctx->consumed) return APP_REDRAW_NONE;
        bool held = !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
        if (ui_keyboard_hold_tick(held, ctx->touch ? ctx->touch->x : 0,
            ctx->touch ? ctx->touch->y : 0, ctx->now_ms) == UI_KEYBOARD_CHANGED) return password_paint(ctx, false, false);
        if (ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) == UI_KEYBOARD_CHANGED)
            return password_paint(ctx, false, false);
    }
    uint32_t revision = __atomic_load_n(&s_signature_revision, __ATOMIC_ACQUIRE);
    if (revision != s_signature_seen) {
        s_signature_seen = revision;
        return APP_REDRAW_PAGE;
    }
    if (s_usb_entry) {
        if (!s_usb_start_pending) {
            bool connected = usb_storage_connected();
            if (connected == s_usb_host_connected) return APP_REDRAW_NONE;
            s_usb_host_connected = connected;
            return APP_REDRAW_PAGE;
        }
        if (s_usb_retry_ms && ctx->now_ms - s_usb_retry_ms < 250) return APP_REDRAW_NONE;
        s_usb_retry_ms = ctx->now_ms;
        esp_err_t err = usb_storage_start();
        if (err == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_usb_start_pending = false;
        s_usb_host_connected = usb_storage_connected();
        if (err != ESP_OK)
            snprintf(s_usb_message, sizeof(s_usb_message), "USB 模式启动失败：%s", esp_err_to_name(err));
        return APP_REDRAW_PAGE;
    }
    if (usb_storage_active()) return APP_REDRAW_NONE;
    if (s_view != TRANSFER_HOME) return network_ui_tick(ctx);
    if (s_start_pending) {
        s_start_pending = false;
        // 传书模式也应在启动无线电前释放可重建字形，避免内部内存不足。
        // Release rebuildable glyphs before starting the radio for uploads too.
        ttf_font_cache_clear();
        esp_err_t err = book_store_upload_root(&s_root);
        if (err == ESP_OK) {
            s_media_lost = false;
            s_free = book_store_free_bytes(&s_root);
            wifi_transfer_cfg_t cfg = {.mode = s_mode, .root_dir = s_root.path, .is_flash = s_root.is_flash,
                .file_limit = book_store_file_limit(&s_root), .free_bytes_cb = free_bytes, .free_bytes_ctx = &s_capacity_roots[s_root.is_flash],
                .file_changed_cb = book_progress_forget, .file_deleted_cb = transfer_file_deleted,
                .file_moved_cb = transfer_file_moved,
                .wallpaper_set_cb = transfer_set_wallpaper,
                .signature_get_cb = transfer_signature_get, .signature_set_cb = transfer_signature_set,
                .title_get_cb = NULL, .title_set_cb = NULL};
            err = start_owned_session(&cfg);
            if (err == ESP_OK) s_free = book_store_free_bytes(&s_root);
        }
        s_status.mode = s_mode;
        if (err != ESP_OK) { s_status.state = WIFI_TRANSFER_ERROR; s_status.last_error = err; }
        if (err == ESP_OK) prepare_qr();
        else clear_qr();
        ESP_LOGI(TAG, "start root=%s result=%s", s_root.path, esp_err_to_name(err));
        return APP_REDRAW_PAGE;
    }
    // 返回已停止的传书页时，不把系统中其他 WiFi 会话显示成传书服务。
    // A restored, stopped upload page must not show an unrelated WiFi session as an upload service.
    if (!s_owned_session || ctx->consumed || ctx->now_ms - s_poll_ms < 2000) return APP_REDRAW_NONE;
    s_poll_ms = ctx->now_ms;
    wifi_transfer_service_poll();
    wifi_transfer_status_t next = {0};
    wifi_transfer_get_status(&next);
    // 接收期间让屏幕保持静止，网页端显示实时进度；避免刷屏与写卡争用。
    // Keep the panel still during reception; the browser owns live progress.
    if (next.state == WIFI_TRANSFER_UPLOADING && s_status.state == WIFI_TRANSFER_UPLOADING) {
        s_status.cur_bytes = next.cur_bytes;
        s_status.sta_count = next.sta_count;
        return APP_REDRAW_NONE;
    }
    if (s_status.state == WIFI_TRANSFER_ERROR && next.state == WIFI_TRANSFER_STOPPED) return APP_REDRAW_NONE;
    bool network_changed = next.network_ready != s_status.network_ready ||
        next.mode != s_status.mode || strcmp(next.ssid, s_status.ssid) ||
        next.wifi_configured != s_status.wifi_configured || strcmp(next.url, s_status.url) ||
        strcmp(next.wifi_ssid, s_status.wifi_ssid);
    if (!network_changed && next.state == s_status.state && next.sta_count == s_status.sta_count &&
        next.cur_bytes == s_status.cur_bytes && next.cur_total == s_status.cur_total &&
        next.done_count == s_status.done_count && next.changed_count == s_status.changed_count && next.last_error == s_status.last_error &&
        !strcmp(next.cur_name, s_status.cur_name)) return APP_REDRAW_NONE;
    s_settle = next.changed_count != s_status.changed_count || next.done_count != s_status.done_count ||
        (s_status.state == WIFI_TRANSFER_UPLOADING && next.state != WIFI_TRANSFER_UPLOADING);
    s_status = next;
    if (network_changed) prepare_qr();
    if (s_settle) {
        s_free = book_store_free_bytes(&s_root);
        draw_status(ctx->fb);
        s_area = status_rect();
        return APP_REDRAW_AREA;
    }
    if (network_changed) { s_free = book_store_free_bytes(&s_root); return APP_REDRAW_PAGE; }
    draw_status(ctx->fb);
    s_area = status_rect();
    return APP_REDRAW_AREA;
}

static int hit_control(uint16_t x, uint16_t y) {
    if (usb_storage_active()) return ui_rect_hit(control_rect(2), x, y) ? 2 : -1;
    if (s_mode == WIFI_TRANSFER_MODE_AP && s_view == TRANSFER_HOME)
        for (int i = 0; i < 2; ++i) if (ui_rect_hit(control_rect(i), x, y)) return i;
    return ui_rect_hit(control_rect(2), x, y) ? 2 : -1;
}

static app_redraw_t stop_usb_for_navigation(app_ctx_t *ctx, bool home) {
    s_usb_start_pending = false;
    if (usb_storage_active()) {
        esp_err_t err = usb_storage_stop();
        if (err != ESP_OK) {
            snprintf(s_usb_message, sizeof(s_usb_message), "USB 写入未完成：%s，请在电脑弹出后重试", esp_err_to_name(err));
            return APP_REDRAW_PAGE;
        }
        s_any_changed = true;
    }
    if (home) ui_nav_request(ctx, 0);
    else ctx->request_return = true;
    return APP_REDRAW_NONE;
}

static app_redraw_t on_gesture(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    if (s_view == TRANSFER_CLOUDZAO) return cloud_gesture(ctx, ev);
#endif
    if (s_status.state == WIFI_TRANSFER_UPLOADING) return APP_REDRAW_NONE;
    if (ev->type == UI_GESTURE_TAP && s_view != TRANSFER_PASSWORD && !usb_storage_active()) {
        int tab = ui_nav_hit(ev->x0, ev->y0);
        if (tab >= 0 && tab == ui_nav_hit(ev->x, ev->y)) {
            if (s_view == TRANSFER_HOME && !stop_upload_if_idle()) return APP_REDRAW_NONE;
            s_pressed = -1;
            ui_nav_request(ctx, tab);
            return APP_REDRAW_NONE;
        }
    }
    if (ev->type == UI_GESTURE_TAP && ev->y0 < 160 && ev->x0 < 120) {
        if (s_usb_entry || usb_storage_active()) return stop_usb_for_navigation(ctx, false);
        if (s_view == TRANSFER_PASSWORD) { clear_password(); s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
        if (s_view == TRANSFER_NETWORKS) {
            if (s_forget_confirm) { s_forget_confirm = false; s_network_message[0] = 0; return APP_REDRAW_PAGE; }
            if (s_saved_detail) { s_saved_detail = false; s_network_message[0] = 0; return APP_REDRAW_PAGE; }
        }
        if (s_view == TRANSFER_HOME && !stop_upload_if_idle()) return APP_REDRAW_NONE;
        ctx->request_return = true;
        return APP_REDRAW_NONE;
    }
    if (s_view != TRANSFER_HOME) return provisioning_gesture(ctx, ev);
    int old = s_pressed;
    int origin = hit_control(ev->x0, ev->y0);
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    if (ev->type == UI_GESTURE_TAP && old >= 0 && old == origin &&
        hit_control(ev->x, ev->y) == origin) {
        if (s_usb_entry) {
            return stop_usb_for_navigation(ctx, false);
        }
        if (origin == 0 || origin == 1) {
            bool url = origin == 1;
            if (s_qr_url != url) {
                s_qr_url = url;
                prepare_qr();
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        if (origin == 2) {
            if (s_mode == WIFI_TRANSFER_MODE_AP && s_status.state == WIFI_TRANSFER_ERROR && !s_status.network_ready) {
                start_hotspot_service();
                return APP_REDRAW_PAGE;
            }
            // 即使没有切页历史也先停服，取消尚未执行的启动请求。
            // Stop even without navigation history and cancel a queued start.
            if (!stop_upload_if_idle()) return APP_REDRAW_NONE;
            ctx->request_return = true;
            return APP_REDRAW_NONE;
        }
    }
    if (s_pressed == old) return APP_REDRAW_NONE;
    render(ctx, ctx->fb);
    s_settle = false;
    s_area = control_rect(old >= 0 ? old : s_pressed);
    return APP_REDRAW_AREA;
}

static app_redraw_t on_key(app_ctx_t *ctx, int key) {
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    if (s_view == TRANSFER_CLOUDZAO) {
        if (s_cloud_notice == CLOUD_NOTICE_FIRST_READ) return APP_REDRAW_NONE;
        if (s_cloud_notice == CLOUD_NOTICE_REVIEW) {
            s_cloud_notice = CLOUD_NOTICE_HIDDEN;
            return APP_REDRAW_PAGE;
        }
        if (s_cloud_history_confirm && key != UI_KEY_2) {
            s_cloud_history_confirm = false;
            return APP_REDRAW_PAGE;
        }
        cloud_leave();
        ui_nav_request(ctx, key == UI_KEY_2 ? 0 : 2);
        return APP_REDRAW_NONE;
    }
#endif
    if (s_usb_entry || usb_storage_active()) return stop_usb_for_navigation(ctx, key == UI_KEY_2);
    if (s_view == TRANSFER_HOME && !stop_upload_if_idle()) return APP_REDRAW_NONE;
    if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_view == TRANSFER_PASSWORD) { clear_password(); s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
    if (s_view == TRANSFER_NETWORKS) {
        if (s_forget_confirm) { s_forget_confirm = false; s_network_message[0] = 0; return APP_REDRAW_PAGE; }
        if (s_saved_detail) { s_saved_detail = false; s_network_message[0] = 0; return APP_REDRAW_PAGE; }
    }
    ctx->request_return = true;
    return APP_REDRAW_NONE;
}

static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw != APP_REDRAW_AREA) return false;
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    if (s_view == TRANSFER_CLOUDZAO) {
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl,
            s_cloud_sample_paint ? DISPLAY_REFRESH_FEEDBACK : DISPLAY_REFRESH_DETAIL, s_area, DISPLAY_REFRESH_DEFAULT));
        return true;
    }
#endif
    if (s_view == TRANSFER_PASSWORD) {
        if (s_input_settle) {
            guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_SETTLE, s_area, DISPLAY_REFRESH_DEFAULT));
            s_input_settle = false;
            return true;
        }
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, s_input_layout ? DISPLAY_REFRESH_DETAIL : DISPLAY_REFRESH_FEEDBACK, s_area, DISPLAY_REFRESH_DEFAULT));
        return true;
    }
    guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, s_settle ? DISPLAY_REFRESH_LAYOUT : DISPLAY_REFRESH_MOTION, s_area, DISPLAY_REFRESH_DEFAULT));
    s_settle = false;
    return true;
}

static bool transfer_menu_handle_enabled(app_ctx_t* ctx) {
    (void)ctx;
    return false;
}

const app_desc_t app_transfer = {
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    .on_before_lock = cloud_before_lock,
    .modal_input_owned = cloud_notice_input_owned,
#endif
    .on_media_lost = transfer_on_media_lost,
    .title = "传书 Transfer", .detail = "热点或已有 WiFi，浏览器上传", .enter_full = false,
    .on_enter = on_enter, .on_exit = transfer_on_exit, .render = render, .present = present,
    .on_tick = on_tick, .on_gesture = on_gesture, .on_key = on_key, .owns_keys = true,
    .menu_handle_enabled = transfer_menu_handle_enabled,
};
