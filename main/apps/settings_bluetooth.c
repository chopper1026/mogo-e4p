/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：蓝牙设置的状态卡、分页设备列表、设备管理与行内按键学习。
 * English: Bluetooth status, paged discovery, peer management and inline key learning.
 * 冻结：采用用户确认的 WiFi 同款版式；绘制与点击共用几何和可见设备快照。
 * Frozen: use the approved WiFi-style layout and shared geometry/visible peer snapshots.
 * 蓝牙协议、最近设备和持久化仍由 ble_page_turner 持有；协议栈启停仍由主循环协调。
 * The BLE component owns protocol, recent peers and persistence; the app loop owns stack lifecycle.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "settings_bluetooth.h"
#include "ble_page_turner.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include "wifi_transfer.h"

#define BLE_ROWS 6
#define BLE_ROW_H 88
typedef enum { BLE_HOME, BLE_SCAN, BLE_DETAIL, BLE_SCAN_CONFIRM, BLE_FORGET_CONFIRM } ble_view_t;
typedef struct {
    bool enabled, running, connected, connecting, scanning, wifi_busy;
    bool bound[2];
    uint8_t bond_count, device_count;
    char addr[18], name[32];
    ble_pt_bond_t bonds[BLE_PT_MAX_BONDS];
    ble_pt_device_t devices[BLE_PT_MAX_DEVICES];
} ble_model_t;
static ble_model_t s_model;
static ble_view_t s_view;
static ble_pt_bond_t s_selected;
static int s_scan_page, s_ble_learning;
static char s_ble_notice[96], s_ble_feedback[64], s_learning_addr[18], s_connect_addr[18];
static bool s_scan_drop_requested, s_scan_started, s_disconnect_pending;
static int64_t s_scan_retry_at, s_sample_at;

// 无广播名的地址回落结果不进入产品列表。/ Omit unnamed address-only discovery results.
static bool ble_scan_visible(const ble_pt_device_t *dev) { return dev && dev->has_name; }
static int ble_scan_visible_count(void) {
    int count = 0;
    for (uint8_t i = 0; i < ble_pt_device_count(); ++i) if (ble_scan_visible(ble_pt_device(i))) ++count;
    return count;
}
static const ble_pt_device_t *ble_scan_visible_at(int row) {
    if (row < 0) return NULL;
    for (uint8_t i = 0; i < ble_pt_device_count(); ++i) {
        const ble_pt_device_t *dev = ble_pt_device(i);
        if (ble_scan_visible(dev) && row-- == 0) return dev;
    }
    return NULL;
}
static bool live_peer(const char *addr) { return s_model.connected && !strcasecmp(addr, s_model.addr); }
static bool available(void) { return s_model.enabled && s_model.running && !s_model.wifi_busy; }
static bool can_learn(void) { return available() && s_model.connected; }

// 报告只在 tick 和显式操作中消费；render 保持纯绘制。/ Consume reports in ticks/actions, never in render.
static void drain_keys(void) {
    ble_pt_event_t key;
    ble_pt_raw_t raw;
    while (ble_pt_pop_key(&key)) {}
    while (ble_pt_pop_raw(&raw)) {}
}
static bool ble_receive_feedback(void) {
    ble_pt_event_t unused;
    while (ble_pt_pop_key(&unused)) {}
    bool changed = false;
    ble_pt_raw_t raw;
    while (ble_pt_pop_raw(&raw)) {
        if (!raw.pressed || !ble_pt_connected()) continue;
        if (s_ble_learning) {
            ble_pt_action_t action = s_ble_learning == 1 ? BLE_PT_ACTION_PREV : BLE_PT_ACTION_NEXT;
            esp_err_t error = ble_pt_bind(action, &raw.frame);
            snprintf(s_ble_feedback, sizeof(s_ble_feedback), "%s", error == ESP_OK ?
                     (action == BLE_PT_ACTION_PREV ? "识别成功 · 已绑定上一页" : "识别成功 · 已绑定下一页") :
                     "识别成功 · 绑定保存失败，请重试");
            s_ble_learning = 0;
            changed = true;
            while (ble_pt_pop_raw(&raw)) {}
            break;
        }
        if (!s_ble_feedback[0]) {
            snprintf(s_ble_feedback, sizeof(s_ble_feedback), "识别成功 · 已收到翻页器按键");
            changed = true;
        }
    }
    return changed;
}
static bool capture(void) {
    ble_model_t next = {0};
    next.enabled = app_settings_ble_turner();
    next.running = ble_pt_running();
    next.connected = ble_pt_connected();
    next.connecting = ble_pt_connecting();
    next.scanning = ble_pt_scanning();
    wifi_transfer_status_t network;
    wifi_transfer_get_status(&network);
    next.wifi_busy = network.state != WIFI_TRANSFER_STOPPED;
    if (next.connected) {
        snprintf(next.addr, sizeof(next.addr), "%s", ble_pt_connected_addr());
        snprintf(next.name, sizeof(next.name), "%s", ble_pt_connected_name());
        next.bound[0] = ble_pt_binding(BLE_PT_ACTION_PREV, NULL);
        next.bound[1] = ble_pt_binding(BLE_PT_ACTION_NEXT, NULL);
    }
    for (uint8_t i = 0; i < ble_pt_bond_count() && i < BLE_PT_MAX_BONDS; ++i) {
        const ble_pt_bond_t *peer = ble_pt_bond(i);
        if (peer) next.bonds[next.bond_count++] = *peer;
    }
    if (s_view == BLE_SCAN && s_scan_started) {
        int count = ble_scan_visible_count();
        for (int i = 0; i < count && i < BLE_PT_MAX_DEVICES; ++i) {
            const ble_pt_device_t *peer = ble_scan_visible_at(i);
            if (!peer) continue;
            ble_pt_device_t *saved = &next.devices[next.device_count++];
            *saved = *peer;
            // 只显示信号档位，忽略同一档内的抖动，避免持续整页重绘。/ Quantize RSSI to avoid redraws within a displayed band.
            saved->rssi = peer->rssi > -60 ? -50 : peer->rssi > -75 ? -65 : -85;
        }
    }
    bool changed = memcmp(&next, &s_model, sizeof(next)) != 0;
    s_model = next;
    int pages = (s_model.device_count + BLE_ROWS - 1) / BLE_ROWS;
    if (pages < 1) pages = 1;
    if (s_scan_page >= pages) s_scan_page = pages - 1;
    return changed;
}

typedef struct { EpdRect peers, learn[2]; int title, footer; } ble_layout_t;
static ble_layout_t ble_layout(void) {
    int rows = s_model.bond_count;
    ble_layout_t l = {.peers = {36, 448, 612, (rows ? rows : 2) * BLE_ROW_H}};
    l.title = l.peers.y + l.peers.height + 22;
    int top = l.title + 56;
    for (int i = 0; i < 2; ++i) {
        int height = s_ble_learning == i + 1 && rows < 4 ? 140 : BLE_ROW_H;
        l.learn[i] = (EpdRect){36, top, 612, height};
        top += height;
    }
    l.footer = top + 16;
    return l;
}
typedef enum { CONTROL_TOGGLE, CONTROL_DISCONNECT, CONTROL_ADD, CONTROL_LEARN_PREV, CONTROL_LEARN_NEXT,
               CONTROL_SCAN_PREV, CONTROL_SCAN_NEXT, CONTROL_CONNECT, CONTROL_FORGET,
               CONTROL_CONFIRM, CONTROL_CANCEL } ble_control_t;
static EpdRect control_rect(ble_control_t control) {
    switch (control) {
        case CONTROL_TOGGLE: return (EpdRect){36, 150, 612, 60};
        case CONTROL_DISCONNECT: return (EpdRect){501, 237, 123, 56};
        case CONTROL_ADD: return (EpdRect){482, 380, 166, 52};
        case CONTROL_LEARN_PREV: case CONTROL_LEARN_NEXT: {
            EpdRect row = ble_layout().learn[control == CONTROL_LEARN_NEXT];
            return (EpdRect){501, row.y + 16, 123, 56};
        }
        case CONTROL_SCAN_PREV: return (EpdRect){36, 978, 64, 66};
        case CONTROL_SCAN_NEXT: return (EpdRect){584, 978, 64, 66};
        case CONTROL_CONNECT: return (EpdRect){36, 410, 612, 76};
        case CONTROL_FORGET: return (EpdRect){36, 510, 612, 76};
        case CONTROL_CONFIRM: return (EpdRect){36, 540, 294, 76};
        case CONTROL_CANCEL: return (EpdRect){354, 540, 294, 76};
    }
    return (EpdRect){0};
}
static EpdRect peer_rect(int row, bool scan) {
    return (EpdRect){36, (scan ? 430 : 448) + row * BLE_ROW_H, 612, BLE_ROW_H};
}
static void text(uint8_t *fb, EpdRect box, int px, const char *label, bool center) {
    ui_text_box(fb, box, px, label, center ? EPD_DRAW_ALIGN_CENTER : EPD_DRAW_ALIGN_LEFT, false);
}
static void border(uint8_t *fb, EpdRect r, int radius) {
    ui_draw_round_rect(fb, r, radius, 0x50);
    ui_draw_round_rect(fb, (EpdRect){r.x + 1, r.y + 1, r.width - 2, r.height - 2}, radius - 1, 0x50);
}
static void rule(uint8_t *fb, int y) { ui_fill_rect_fast((EpdRect){64, y, 556, 2}, UI_GRAY_BLACK, fb); }
static void button(uint8_t *fb, ble_control_t control, const char *label, bool dark, bool enabled) {
    EpdRect r = control_rect(control);
    ui_fill_round_rect(fb, r, 16, dark && enabled ? UI_GRAY_BLACK : UI_GRAY_WHITE);
    if (!dark || !enabled) border(fb, r, 16);
    if (control == CONTROL_SCAN_PREV || control == CONTROL_SCAN_NEXT)
        ui_draw_icon(fb, r.x + r.width / 2, r.y + r.height / 2, 32,
                     control == CONTROL_SCAN_PREV ? UI_ICON_CHEVRON_LEFT : UI_ICON_CHEVRON_RIGHT, enabled ? 0 : 0xa0);
    else ui_text_box(fb, (EpdRect){r.x + 8, r.y + 5, r.width - 16, r.height - 10}, 23, label,
                     EPD_DRAW_ALIGN_CENTER, dark && enabled);
    if (!enabled) {
        for (int y = r.y; y < r.y + r.height; ++y) for (int x = r.x; x < r.x + r.width; ++x) {
            uint8_t value = ui_read_pixel(fb, x, y);
            if (value < 9) epd_draw_pixel(x, y, 0x90, fb);
        }
    }
}
static void header(uint8_t *fb, const char *title) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    text(fb, (EpdRect){124, 79, 436, 56}, 34, title, true);
    ui_nav_draw(fb, 3);
}
static void draw_peer(uint8_t *fb, EpdRect row, const char *name, const char *status, bool live) {
    text(fb, (EpdRect){64, row.y + 8, 485, 40}, 27, name, false);
    text(fb, (EpdRect){64, row.y + 50, 485, 30}, 20, status, false);
    if (live) ui_draw_icon(fb, 580, row.y + 44, 26, UI_ICON_CHECK, UI_GRAY_BLACK);
    ui_draw_icon(fb, 615, row.y + 44, 26, UI_ICON_CHEVRON_RIGHT, UI_GRAY_BLACK);
}
static void draw_home(uint8_t *fb) {
    header(fb, "蓝牙翻页器");
    text(fb, (EpdRect){42, 161, 300, 36}, 24, "启用蓝牙", false);
    ui_fill_round_rect(fb, (EpdRect){560, 160, 88, 44}, 22, s_model.enabled ? UI_GRAY_BLACK : 0xb0);
    epd_fill_circle(s_model.enabled ? 626 : 582, 182, 17, UI_GRAY_WHITE, fb);
    border(fb, (EpdRect){36, 222, 612, 142}, 22);
    ui_draw_icon(fb, 75, 260, 32, UI_ICON_BLUETOOTH, s_model.enabled ? UI_GRAY_BLACK : 0x80);
    const char *name = !s_model.enabled ? "蓝牙已关闭" : s_model.wifi_busy ? "蓝牙暂时不可用" :
                       s_model.connected ? (s_model.name[0] ? s_model.name : s_model.addr) :
                       s_model.connecting ? "正在连接翻页器…" : !s_model.running ? "蓝牙启动中…" : "尚未连接翻页器";
    text(fb, (EpdRect){104, 236, s_model.connected ? 380 : 512, 44}, 27, name, false);
    const char *status = !s_model.enabled ? "已配对的设备会保留" : s_model.wifi_busy ? "WiFi 使用中，蓝牙暂停" :
                         s_model.connected ? "已连接 · 可以翻页" : s_model.connecting ? "请保持设备靠近墨水屏" :
                         !s_model.running ? "正在准备蓝牙，请稍候" : "添加设备，开始遥控翻页";
    text(fb, (EpdRect){64, 283, 534, 34}, 21, status, false);
    const char *hint = s_ble_notice[0] ? s_ble_notice : !s_model.enabled ? "打开上方开关即可使用" :
                       s_model.wifi_busy ? "结束 WiFi 使用后再连接" : s_model.connected ? "按键只在阅读时翻页" : "请让翻页器进入配对模式";
    text(fb, (EpdRect){64, 323, 556, 30}, 19, hint, false);
    if (s_model.connected && available()) button(fb, CONTROL_DISCONNECT, "断开", false, true);
    char label[40]; snprintf(label, sizeof(label), "已配对 %u", s_model.bond_count);
    text(fb, (EpdRect){42, 386, 300, 38}, 24, label, false);
    button(fb, CONTROL_ADD, "添加设备", true, available());
    ble_layout_t l = ble_layout();
    border(fb, l.peers, 22);
    if (!s_model.bond_count) {
        ui_draw_icon(fb, 342, 491, 36, UI_ICON_BLUETOOTH, UI_GRAY_BLACK);
        text(fb, (EpdRect){64, 522, 556, 40}, 26, "还没有配对的设备", true);
        text(fb, (EpdRect){64, 568, 556, 34}, 21, "轻点「添加设备」搜索翻页器", true);
    }
    for (int i = 0; i < s_model.bond_count; ++i) {
        const ble_pt_bond_t *peer = &s_model.bonds[i];
        EpdRect r = peer_rect(i, false);
        bool live = live_peer(peer->addr);
        if (i) rule(fb, r.y);
        const char *detail = !s_model.enabled ? "已配对 · 蓝牙已关闭" : s_model.wifi_busy ? "已配对 · 蓝牙暂停" :
                             live ? "已连接" : s_model.connecting && !strcasecmp(peer->addr, s_connect_addr) ? "正在连接…" : "已配对 · 未连接";
        draw_peer(fb, r, peer->name[0] ? peer->name : peer->addr, detail, live);
    }
    text(fb, (EpdRect){42, l.title, 260, 38}, 24, "按键设置", false);
    if (!can_learn()) text(fb, (EpdRect){320, l.title, 322, 38}, 20, "连接后可学习", false);
    border(fb, (EpdRect){36, l.learn[0].y, 612, l.learn[0].height + l.learn[1].height}, 22);
    for (int i = 0; i < 2; ++i) {
        EpdRect r = l.learn[i];
        bool learning = s_ble_learning == i + 1;
        if (i) rule(fb, r.y);
        text(fb, (EpdRect){64, r.y + 8, 356, 40}, 26, i ? "下一页" : "上一页", false);
        text(fb, (EpdRect){64, r.y + 49, 410, 31}, 20, learning ? "请按翻页器上要用的键" :
             s_model.bound[i] ? "已学习 · 自定义按键" : "内置按键", false);
        if (learning && r.height > BLE_ROW_H) {
            ui_draw_icon(fb, 81, r.y + 110, 24, UI_ICON_BLUETOOTH, UI_GRAY_BLACK);
            text(fb, (EpdRect){108, r.y + 91, 370, 38}, 21, "正在等待按键…", false);
        }
        button(fb, i ? CONTROL_LEARN_NEXT : CONTROL_LEARN_PREV, learning ? "取消" : s_model.bound[i] ? "重学" : "学习",
               learning, can_learn() && (!s_ble_learning || learning));
    }
    if (l.footer + 32 <= UI_NAV_TOP - 4) text(fb, (EpdRect){42, l.footer, 600, 32}, 20,
        s_ble_feedback[0] ? s_ble_feedback : s_ble_learning ? "按键识别后自动保存" : "上下、左右及 PageUp / PageDown 均可翻页", false);
}
static void draw_scan(uint8_t *fb) {
    header(fb, "添加翻页器");
    border(fb, (EpdRect){36, 174, 612, 172}, 22);
    ui_draw_icon(fb, 76, 216, 34, UI_ICON_BLUETOOTH, UI_GRAY_BLACK);
    text(fb, (EpdRect){108, 191, 512, 48}, 27, "让翻页器进入配对模式", false);
    text(fb, (EpdRect){64, 249, 556, 36}, 21, "保持设备靠近墨水屏", false);
    text(fb, (EpdRect){64, 293, 556, 34}, 20, s_ble_notice[0] ? s_ble_notice : "搜索期间，当前翻页器会断开", false);
    text(fb, (EpdRect){42, 374, 300, 38}, 24, "附近设备", false);
    text(fb, (EpdRect){414, 374, 234, 38}, 21,
         !available() ? "蓝牙不可用" : s_model.scanning ? "正在搜索…" : "准备搜索…", true);
    border(fb, (EpdRect){36, 430, 612, BLE_ROWS * BLE_ROW_H}, 22);
    if (!s_model.device_count) {
        ui_draw_icon(fb, 342, 597, 52, UI_ICON_SEARCH, UI_GRAY_BLACK);
        text(fb, (EpdRect){64, 661, 556, 44}, 27, "暂未发现设备", true);
        text(fb, (EpdRect){64, 718, 556, 38}, 22, "确认翻页器已进入配对模式", true);
        text(fb, (EpdRect){64, 764, 556, 34}, 21, !available() ? "蓝牙恢复后会继续搜索" :
             s_model.scanning ? "正在继续搜索，请稍候" : "正在准备搜索，请稍候", true);
    }
    for (int row = 0; row < BLE_ROWS; ++row) {
        int index = s_scan_page * BLE_ROWS + row;
        if (index >= s_model.device_count) break;
        const ble_pt_device_t *peer = &s_model.devices[index];
        EpdRect r = peer_rect(row, true);
        if (row) rule(fb, r.y);
        char detail[64]; snprintf(detail, sizeof(detail), "%s · %s", peer->rssi > -60 ? "信号强" :
                                 peer->rssi > -75 ? "信号良好" : "信号一般", peer->bonded ? "已配对" : "未配对");
        draw_peer(fb, r, peer->name, detail, false);
    }
    int pages = (s_model.device_count + BLE_ROWS - 1) / BLE_ROWS;
    if (pages < 1) pages = 1;
    button(fb, CONTROL_SCAN_PREV, "", false, s_scan_page > 0);
    button(fb, CONTROL_SCAN_NEXT, "", false, s_scan_page + 1 < pages);
    char page[48]; snprintf(page, sizeof(page), "已发现 %u 台 · %d/%d", s_model.device_count, s_scan_page + 1, pages);
    text(fb, (EpdRect){112, 978, 460, 66}, 23, page, true);
    text(fb, (EpdRect){42, 1060, 600, 32}, 20, "仅支持 BLE 设备，轻点名称连接", true);
}
static void draw_detail(uint8_t *fb) {
    header(fb, "设备详情");
    border(fb, (EpdRect){36, 196, 612, 170}, 22);
    ui_draw_icon(fb, 76, 243, 34, UI_ICON_BLUETOOTH, UI_GRAY_BLACK);
    text(fb, (EpdRect){110, 220, 510, 48}, 29, s_selected.name[0] ? s_selected.name : s_selected.addr, false);
    bool live = live_peer(s_selected.addr);
    const char *state = live ? "已连接 · 可以翻页" : s_model.connecting && !strcasecmp(s_connect_addr, s_selected.addr) ? "正在连接…" : "已配对 · 未连接";
    text(fb, (EpdRect){64, 282, 556, 36}, 22, state, false);
    char address[48]; snprintf(address, sizeof(address), "地址  %s", s_selected.addr);
    text(fb, (EpdRect){64, 325, 556, 30}, 19, address, false);
    button(fb, CONTROL_CONNECT, live ? "断开连接" : "连接", !live, available() && !s_model.connecting);
    button(fb, CONTROL_FORGET, "遗忘设备", false, !s_model.connecting);
    text(fb, (EpdRect){42, 620, 600, 38}, 21, s_ble_notice[0] ? s_ble_notice : "遗忘会清除配对与自定义按键", false);
}
static void draw_confirm(uint8_t *fb) {
    bool forget = s_view == BLE_FORGET_CONFIRM;
    header(fb, forget ? "遗忘设备" : "添加翻页器");
    border(fb, (EpdRect){36, 222, 612, 260}, 22);
    text(fb, (EpdRect){64, 249, 556, 56}, 29, forget ? "遗忘这台翻页器？" : "断开当前翻页器？", true);
    text(fb, (EpdRect){64, 323, 556, 48}, 27, forget ? (s_selected.name[0] ? s_selected.name : s_selected.addr) : s_model.name, true);
    text(fb, (EpdRect){64, 391, 556, 38}, 21, forget ? "配对与自定义按键将被清除" : "添加设备需要先断开当前连接", true);
    text(fb, (EpdRect){64, 435, 556, 30}, 20, forget ? "下次使用时需要重新配对" : "已配对的设备和按键设置会保留", true);
    button(fb, CONTROL_CONFIRM, forget ? "确认遗忘" : "继续搜索", true, true);
    button(fb, CONTROL_CANCEL, "取消", false, true);
}
void settings_bluetooth_render(uint8_t *fb) {
    if (s_view == BLE_SCAN) draw_scan(fb);
    else if (s_view == BLE_DETAIL) draw_detail(fb);
    else if (s_view == BLE_SCAN_CONFIRM || s_view == BLE_FORGET_CONFIRM) draw_confirm(fb);
    else draw_home(fb);
}
void settings_bluetooth_open(void) {
    s_view = BLE_HOME;
    s_scan_page = s_ble_learning = 0;
    s_ble_notice[0] = s_ble_feedback[0] = s_learning_addr[0] = s_connect_addr[0] = 0;
    s_scan_drop_requested = s_scan_started = s_disconnect_pending = false;
    s_sample_at = s_scan_retry_at = 0;
    drain_keys();
    capture();
}
void settings_bluetooth_close(void) {
    if (s_view == BLE_SCAN) ble_pt_scan_stop();
    s_view = BLE_HOME;
    s_ble_learning = 0;
    s_disconnect_pending = false;
    s_learning_addr[0] = s_ble_feedback[0] = 0;
    drain_keys();
}
bool settings_bluetooth_back(void) {
    if (s_view == BLE_FORGET_CONFIRM) s_view = BLE_DETAIL;
    else if (s_view != BLE_HOME) {
        if (s_view == BLE_SCAN) ble_pt_scan_stop();
        s_view = BLE_HOME;
    } else if (s_ble_learning) {
        s_ble_learning = 0;
        snprintf(s_ble_feedback, sizeof(s_ble_feedback), "已取消学习");
    } else return false;
    s_ble_notice[0] = 0;
    capture();
    return true;
}
static void begin_scan(void) {
    s_view = BLE_SCAN;
    s_scan_started = false;
    s_disconnect_pending = false;
    s_scan_page = s_ble_learning = 0;
    s_ble_notice[0] = s_ble_feedback[0] = 0;
    ble_pt_cancel_recent();
    s_scan_drop_requested = ble_pt_connected();
    if (ble_pt_connected() || ble_pt_connecting()) ble_pt_disconnect();
    // 真正起扫放到 tick，等待异步断连落地。/ Wait for asynchronous teardown before starting discovery on a tick.
    s_scan_retry_at = 0;
    capture();
    s_model.device_count = 0;
}
static void connect_peer(const char *addr) {
    // 组件自己处理换设备的异步交接，不能在记录目标前先断开。/ Let the component queue peer switching before disconnecting.
    snprintf(s_connect_addr, sizeof(s_connect_addr), "%s", addr);
    s_disconnect_pending = false;
    s_ble_learning = 0;
    s_ble_feedback[0] = 0;
    esp_err_t error = ble_pt_connect(s_connect_addr);
    snprintf(s_ble_notice, sizeof(s_ble_notice), "%s", error == ESP_OK ? "正在连接…" : "连接失败，请重试");
    capture();
}
bool settings_bluetooth_tick(int64_t now_ms) {
    bool changed = false;
    bool learning_lost = s_ble_learning && (!app_settings_ble_turner() || !ble_pt_running() || !ble_pt_connected() ||
                                           strcasecmp(s_learning_addr, ble_pt_connected_addr()));
    if (learning_lost) {
        s_ble_learning = 0;
        snprintf(s_ble_feedback, sizeof(s_ble_feedback), "连接已变化，请重新学习");
        drain_keys();
        changed = true;
    }
    if (s_view == BLE_HOME) changed |= ble_receive_feedback();
    else drain_keys();
    if (s_view == BLE_SCAN && app_settings_ble_turner() && ble_pt_running()) {
        if (ble_pt_connected()) {
            if (!s_scan_drop_requested) { ble_pt_disconnect(); s_scan_drop_requested = true; }
        } else {
            s_scan_drop_requested = false;
            if (!ble_pt_connecting() && !ble_pt_scanning() && now_ms >= s_scan_retry_at) {
                ble_pt_scan_start(BLE_PT_SCAN_FOREVER);
                s_scan_started = ble_pt_scanning();
                s_scan_retry_at = now_ms + 2000;
                snprintf(s_ble_notice, sizeof(s_ble_notice), "%s", s_scan_started ? "" : "扫描未启动，正在重试");
                changed = true;
            }
        }
    }
    char why[96];
    bool failure = ble_pt_take_start_failure(why, sizeof(why)) || ble_pt_take_failure(why, sizeof(why));
    if (failure) {
        snprintf(s_ble_notice, sizeof(s_ble_notice), "%s", why);
        changed = true;
    }
    if (s_disconnect_pending && !ble_pt_connected()) {
        s_disconnect_pending = false;
        if (!failure) s_ble_notice[0] = 0;
        changed = true;
    }
    // 扫描结果最多每秒更新一次；点击期间由外层延迟 tick，命中使用已画出的快照。
    // Refresh scan results at most once a second; the owner defers ticks while a finger is held.
    if (changed || s_view != BLE_SCAN || now_ms >= s_sample_at) {
        char old_addr[18]; snprintf(old_addr, sizeof(old_addr), "%s", s_model.addr);
        changed |= capture();
        s_sample_at = now_ms + 1000;
        if (strcmp(old_addr, s_model.addr)) {
            if (!learning_lost) s_ble_feedback[0] = 0;
            if (s_model.connected && s_view != BLE_SCAN && !failure) s_ble_notice[0] = 0;
            changed = true;
        }
    }
    return changed;
}
bool settings_bluetooth_gesture(const ui_gesture_event_t *ev) {
    if (ev->type != UI_GESTURE_TAP) return false;
    int x = ev->x0, y = ev->y0;
    if (x < 36 || x >= 648 || y < 150 || y >= UI_NAV_TOUCH_TOP) return false;
    if (s_view == BLE_SCAN_CONFIRM || s_view == BLE_FORGET_CONFIRM) {
        if (ui_rect_hit(control_rect(CONTROL_CANCEL), x, y)) return settings_bluetooth_back();
        if (!ui_rect_hit(control_rect(CONTROL_CONFIRM), x, y)) return false;
        if (s_view == BLE_SCAN_CONFIRM) {
            if (app_settings_ble_turner() && ble_pt_running()) begin_scan();
            else { s_view = BLE_HOME; snprintf(s_ble_notice, sizeof(s_ble_notice), "蓝牙不可用，请稍后重试"); capture(); }
        } else {
            if (ble_pt_connected() && !strcasecmp(s_selected.addr, ble_pt_connected_addr())) ble_pt_disconnect();
            ble_pt_forget(s_selected.addr);
            s_view = BLE_HOME;
            snprintf(s_ble_notice, sizeof(s_ble_notice), "已遗忘该设备");
            capture();
        }
        return true;
    }
    if (s_view == BLE_SCAN) {
        if (ui_rect_hit(control_rect(CONTROL_SCAN_PREV), x, y) && s_scan_page > 0) { --s_scan_page; return true; }
        if (ui_rect_hit(control_rect(CONTROL_SCAN_NEXT), x, y) && (s_scan_page + 1) * BLE_ROWS < s_model.device_count) { ++s_scan_page; return true; }
        if (!app_settings_ble_turner() || !ble_pt_running()) return false;
        for (int row = 0; row < BLE_ROWS; ++row) {
            int index = s_scan_page * BLE_ROWS + row;
            if (index >= s_model.device_count || !ui_rect_hit(peer_rect(row, true), x, y)) continue;
            char addr[18]; snprintf(addr, sizeof(addr), "%s", s_model.devices[index].addr);
            ble_pt_scan_stop(); s_view = BLE_HOME; connect_peer(addr); return true;
        }
        return false;
    }
    if (s_view == BLE_DETAIL) {
        if (ui_rect_hit(control_rect(CONTROL_FORGET), x, y) && !ble_pt_connecting()) { s_view = BLE_FORGET_CONFIRM; return true; }
        if (!ui_rect_hit(control_rect(CONTROL_CONNECT), x, y) || !available() || ble_pt_connecting()) return false;
        if (ble_pt_connected() && !strcasecmp(s_selected.addr, ble_pt_connected_addr())) {
            s_disconnect_pending = true;
            ble_pt_disconnect(); snprintf(s_ble_notice, sizeof(s_ble_notice), "正在断开…"); capture();
        } else connect_peer(s_selected.addr);
        return true;
    }
    if (ui_rect_hit(control_rect(CONTROL_TOGGLE), x, y)) {
        bool wanted = !app_settings_ble_turner();
        app_settings_set_ble_turner(wanted);
        if (app_settings_ble_turner() != wanted) snprintf(s_ble_notice, sizeof(s_ble_notice), "保存失败，请重试");
        else {
            if (!wanted) { ble_pt_cancel_recent(); ble_pt_reset_failure(); }
            s_ble_learning = 0; s_ble_feedback[0] = 0;
            s_ble_notice[0] = 0; drain_keys();
        }
        capture(); return true;
    }
    if (ui_rect_hit(control_rect(CONTROL_DISCONNECT), x, y) && can_learn()) {
        s_disconnect_pending = true;
        ble_pt_cancel_recent(); ble_pt_disconnect(); s_ble_learning = 0; s_ble_feedback[0] = 0;
        snprintf(s_ble_notice, sizeof(s_ble_notice), "正在断开…"); capture(); return true;
    }
    if (ui_rect_hit(control_rect(CONTROL_ADD), x, y) && available()) {
        s_ble_learning = 0; s_ble_feedback[0] = 0;
        if (ble_pt_connected() || ble_pt_connecting()) s_view = BLE_SCAN_CONFIRM;
        else begin_scan();
        return true;
    }
    for (int i = 0; i < 2; ++i) {
        if (!ui_rect_hit(control_rect(i ? CONTROL_LEARN_NEXT : CONTROL_LEARN_PREV), x, y) || !can_learn()) continue;
        if (s_ble_learning == i + 1) { s_ble_learning = 0; snprintf(s_ble_feedback, sizeof(s_ble_feedback), "已取消学习"); }
        else if (!s_ble_learning && ble_pt_connected() && !strcasecmp(s_model.addr, ble_pt_connected_addr())) {
            drain_keys(); ble_pt_learn_begin(); s_ble_learning = i + 1;
            snprintf(s_learning_addr, sizeof(s_learning_addr), "%s", s_model.addr);
            s_ble_feedback[0] = s_ble_notice[0] = 0;
        } else return false;
        return true;
    }
    for (int row = 0; row < s_model.bond_count; ++row) {
        if (!ui_rect_hit(peer_rect(row, false), x, y)) continue;
        s_selected = s_model.bonds[row]; s_view = BLE_DETAIL; s_ble_learning = 0; s_ble_notice[0] = 0;
        return true;
    }
    return false;
}
