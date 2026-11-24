/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户 2026-11-11 修订：锁屏样式改为六类缩略图及同页选择，图片加载独立于绘图和输入。
 * User revision: lock styles use six thumbnails and inline choices, with image I/O outside drawing/input.
 * 用户实屏反馈：锁屏样式保留小图，加载完成只整屏定稿一次，避免逐张局刷导致文字变灰。
 * Device feedback: retain thumbnails and settle completed batches once to avoid faded text from repeated partial updates.
 * 用户新增：阅读票根提供透明或封面背景，独立记忆选择并在保存失败时保留原值。
 * User addition: tickets offer transparent/cover backgrounds with independent persistence and failed-save retention.
 * 用户 2026-11-11 确认：蓝牙设置采用 WiFi 同款状态卡和分页列表，设备详情管理配对，按键学习留在动作行内。
 * Approved revision: Bluetooth uses WiFi-style cards/paging, peer details and inline learning.
 * 中文：分组设置页；网络短时对时后 PMU 持续走时，开机恢复系统时钟。
 * English: Grouped settings; temporary WiFi sync seeds the PMU, whose RTC restores time at boot.
 * 用户修订：设置列表滑动仅差分刷新内容，滑动过程中不周期插入黑白清屏。
 * User revision: settings lists scroll with content-only differentials and no periodic black/white wipe during swipes.
 * 用户授权新输入法：资料卡和签名共用九宫格/全键盘及离线词语候选，保存仍由设置页负责。
 * Authorized keyboard revision: profile and signature share T9/QWERTY and offline phrases; this page still owns saving.
 * 用户修订：资料卡与签名输入只画变化区域，不因输入次数触发整屏黑白清屏。
 * User revision: profile/signature input paints changed regions without whole-screen wipes based on typing counts.
 * 用户修订：主页刷新模式只列普通与水波纹，快刷按用户 2026-10-21 要求撤下；两种模式均固定底栏。
 * User revision: the main-page refresh page lists only ordinary and water; fast was withdrawn per the user
 * on 2026-10-21, and both remaining modes retain navigation.
 * 用户修订：联网升级先画反馈再启动任务；进度只追加黑色段，不擦除已完成段，不累计整页清屏。升级前释放不在显示的封面缓存。
 * User revision: present feedback before starting online work; append black progress without erasing completed segments or counting page cleanup. Release off-screen cover caches before updating.
 * 用户确认：字号并入系统字体并预览；选择字体后询问同步阅读字体，设置按连接/显示/个性化/升级分组。
 * Approved revision: size and preview live with the UI face; ask before syncing the reading face, and group connection/display/personalization/maintenance.
 * 用户修订：设置题头与列表周围留白保持纯白，普通主页切入/周期整理复用书架同轮分区输出；题头柔和，内容白像素只向白擦除，灰阶保留。普通列表滑动使用柔和差分；手动全刷、离散滚动和水波纹节奏保持。
 * User revision: keep setting chrome/list whitespace white. Ordinary root entry/periodic cleanup reuse simultaneous shelf regions with soft chrome, directional white content and preserved grays. Scroll ordinary lists with soft differences; retain manual full refresh, discrete gestures and ripple pacing.
 * 用户撤回跟手滚动：三种主页模式统一恢复一次手势推进一段列表并差分刷新的原逻辑，无惯性、浅墨或停稳补刷。
 * User rollback: all main modes return to one discrete list step per gesture with differential updates, without inertia, light motion ink or idle restoration.
 * 用户 2026-10-27 修订：设置「升级和恢复」恢复系统升级入口，BOOT 刷机入口仍撤下。
 * User revision of 2026-10-27: restore system upgrade in maintenance; keep the BOOT flashing entry withdrawn.
 * 用户 2026-11-02 修订：隐藏 TF 卡升级入口；自动 OTA 默认开启，每十二小时只检查清单并提示确认。
 * User revision of 2026-11-02: hide the TF upgrade entry; automatic OTA defaults on, checking only the manifest every twelve hours before offering confirmation.
 * 用户新增：个性化功能新增「触摸震动」入口，页内包含开关与强度档位，打开或改档即时震一记作预览。
 * User addition: personal settings gain a Touch vibration entry whose page holds the switch and intensity steps; enabling or changing the step previews one pulse.
 * 用户 2026-10-25 新增、2026-11-23 重设计：个性化功能在触摸震动下方新增「阅读背景」入口；入口使用两列缩略图卡片，解码延迟到 tick。
 * User addition of 2026-10-25 and redesign of 2026-11-23: personalization gains a Reading background entry under Touch vibration; the entry uses a two-column thumbnail-card grid and defers decoding to the tick.
 * 用户 2026-10-26 新增：设置列表最底部一行居中的版本号与固件构建日期，随列表滚动露出。
 * User addition of 2026-10-26: the settings list ends with a centred version and firmware build
 * date line, revealed by scrolling to the bottom.
 * 用户 2026-11-03 新增：阅读背景下面保留组件库入口，天气隐藏后仍可重新添加。
 * User addition of 2026-11-03: keep Components below Reading background so hidden weather can be enabled again.
 */
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "app.h"
#include "app_content_open.h"
#include "app_device_settings_mode.h"
#include "settings_bluetooth.h"
#include "app_components_mode.h"
#include "app_registry.h"
#include "app_transfer_mode.h"
#include "settings.h"
#include "haptics.h"
#include "lock_pin_flow.h"
#include "ui_pinpad.h"
#include "boot_state.h"
#include "ble_page_turner.h"
#include "device_power.h"
#include "frontlight.h"
#include "pmu_protocol.h"
#include "wifi_transfer.h"
#include "esp_log.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_shelf_notepad.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "ui_nav.h"
#include "ui_wallpaper.h"
#include "ui_lock_style.h"
#include "ui_reader_background.h"
#include "ui_avatar.h"
#include "ui_builtin_image.h"
#include "ui_image_dither.h"
#include "image_decode.h"
#include "book_search.h"
#include "book_store.h"
#include "book_cover.h"
#include "ota_update.h"
#include "ota_online.h"
#include "ota_auto.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "display.h"
#include "display_refresh.h"
#include "pmu_selftest.h"
#include "cloudzao_service.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "soc/rtc_cntl_reg.h"
#endif
#include "time_service.h"
#include "network_service.h"
#include "cellular_service.h"
#include "memory_manager.h"
#include "memory_peak.h"
#include "memory_inventory.h"

static void fit_value(char *value, int width);
static void settings_prepare_profile(void);
static void settings_card(uint8_t *fb, EpdRect rect, int radius, uint8_t fill, uint8_t edge);
static char s_notice[96];
// 主页刷新模式只列出普通与水波纹；快刷已撤下，旧设置加载时回落普通。
// Main-page refresh lists ordinary and water only; fast was retired and stored values fall back to ordinary.
static const app_main_refresh_mode_t s_main_refresh_choices[] = {
    APP_MAIN_REFRESH_NORMAL, APP_MAIN_REFRESH_WATER,
};
#define MAIN_REFRESH_CHOICE_COUNT ((int)(sizeof(s_main_refresh_choices) / sizeof(s_main_refresh_choices[0])))
typedef enum { SETTINGS_MAIN, SETTINGS_WIFI, SETTINGS_NETWORK, SETTINGS_TIME,
               SETTINGS_TIME_EDIT, SETTINGS_SHELF_STYLE, SETTINGS_SYSTEM_FONT,
               SETTINGS_SYSTEM_SIZE, SETTINGS_SYSTEM_CONTRAST, SETTINGS_MAIN_REFRESH, SETTINGS_LOCK_STYLE,
               SETTINGS_READING_BG, SETTINGS_READING_BG_PREVIEW,
               SETTINGS_CONFIG, SETTINGS_UPGRADE, SETTINGS_BOOT,
               SETTINGS_POWER_SLEEP, SETTINGS_AUTO_LOCK, SETTINGS_HAPTIC,
               SETTINGS_POWER_LOG, SETTINGS_BATT_CALIBRATION, SETTINGS_CHARGE_CURRENT,
               SETTINGS_PROFILE, SETTINGS_AVATAR,
               SETTINGS_BLUETOOTH,
               SETTINGS_MEMORY,
               SETTINGS_QUICK_BAR,
               SETTINGS_LOCK_PIN, SETTINGS_PIN_ENTRY, SETTINGS_TEXT_EDIT } settings_page_t;
static settings_page_t s_page;
static char s_quick_notice[96];
// 快捷栏长按蓝牙或震动圆键的待定子页；on_enter 消费一次后回到主列表。
// Pending sub-page from a quick-bar Bluetooth or haptics hold; on_enter consumes it once and returns to the main list.
static settings_page_t s_enter_page;
// 本次直达是否从其他页面进入（返回键回到来源页）；以及蓝牙子页当前是否仍按这次直达返回。
// Whether this direct visit came from another page (back returns to the origin), and
// whether the Bluetooth sub-page still uses that back target.
static bool s_enter_ble_from_quick;
static bool s_ble_shortcut_back;
// 触摸震动直达有独立返回标记，设置页内重进时仍返回主列表。
// Haptics keeps its own direct-visit back flag; re-entry within Settings still returns to the main list.
static bool s_enter_haptics_from_quick;
static bool s_haptics_shortcut_back;
static bool s_enter_network_from_quick;
static bool s_network_shortcut_back;
static bool s_enter_memory_from_quick;
static bool s_memory_shortcut_back;
static network_status_t s_network_snapshot;
static const char *s_network_notice;
static mm_snapshot_t s_memory_snapshot;
static mm_clean_report_t s_memory_report;
static int64_t s_memory_next_sample_ms;
static int64_t s_memory_last_present_ms;
static bool s_memory_present_pending;
static bool s_memory_dirty;
enum {
    NETWORK_TOGGLE_Y = 272,
    NETWORK_TOGGLE_H = 126,
    NETWORK_ROUTE_SECTION_Y = 434,
    NETWORK_ROUTE_ROW_Y = 472,
    NETWORK_ROW_H = 70,
};

static const char *settings_cellular_status(const network_status_t *network) {
    if (!network->cellular_enabled) return "已关闭";
    if (network->cellular_ready) return "已连接";
    switch (network->cellular_state) {
        case CELLULAR_SERVICE_NO_SIM: return "未检测到 SIM 卡";
        case CELLULAR_SERVICE_SIM_NOT_READY:
            return network->cellular_detail[0] ? network->cellular_detail : "SIM 卡未就绪";
        case CELLULAR_SERVICE_NO_REGISTRATION: return "网络注册被拒";
        case CELLULAR_SERVICE_ERROR: return "连接失败";
        case CELLULAR_SERVICE_PROBING: return "正在检测模块";
        case CELLULAR_SERVICE_STARTING: return "正在连接";
        default: return "未连接";
    }
}

static bool settings_network_poll(void) {
    network_status_t next;
    network_service_get_status(&next);
    const bool changed = s_page == SETTINGS_NETWORK
        ? memcmp(&next, &s_network_snapshot, sizeof(next)) != 0
        : s_page == SETTINGS_MAIN && next.cellular_capable != s_network_snapshot.cellular_capable;
    if (changed) s_network_snapshot = next;
    return changed;
}

static ui_pinpad_t s_pinpad;
static lock_pin_flow_t s_pinflow;
static bool s_pin_pending;
static unsigned s_pin_failures;
static int64_t s_pin_retry_at;
static void pin_setup_close(void) {
    ui_pinpad_end(&s_pinpad); lock_pin_flow_end(&s_pinflow); s_pin_pending = false;
}
static void pin_setup_open(app_ctx_t *ctx, bool disable) {
    pin_setup_close(); lock_pin_flow_begin(&s_pinflow, disable);
    ui_pinpad_begin(&s_pinpad, ctx->fb, lock_pin_flow_title(&s_pinflow));
    s_page = SETTINGS_PIN_ENTRY;
}
static app_redraw_t pin_paint(app_ctx_t *ctx, EpdRect area) {
    ui_pinpad_paint(ctx->fb, &s_pinpad, area);
    guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_DETAIL, area, DISPLAY_REFRESH_DEFAULT));
    return APP_REDRAW_DONE;
}
static app_redraw_t pin_setup_tick(app_ctx_t *ctx) {
    if (s_pinpad.blocked && ctx->now_ms >= s_pin_retry_at) {
        ui_pinpad_reset(&s_pinpad, NULL, "请重新输入");
        return pin_paint(ctx, ui_pinpad_entry_area());
    }
    if (!s_pin_pending) return APP_REDRAW_NONE;
    s_pin_pending = false;
    lock_pin_step_t prior = s_pinflow.step;
    esp_err_t error = lock_pin_flow_input(&s_pinflow, s_pinpad.digits);
    if (error == ESP_OK && s_pinflow.step == PIN_FLOW_DONE) {
        pin_setup_close(); s_page = SETTINGS_LOCK_PIN;
        snprintf(s_notice, sizeof(s_notice), "%s", lock_pin_enabled() ? "锁屏密码已保存" : "锁屏密码已关闭");
        return APP_REDRAW_PAGE;
    }
    const char *notice = error == ESP_OK ? "" : error == ESP_ERR_INVALID_RESPONSE ? "两次输入不同，请重设" :
        error == ESP_ERR_INVALID_ARG ? "原密码不正确，请重试" : "保存失败，请重试";
    ui_pinpad_reset(&s_pinpad, lock_pin_flow_title(&s_pinflow), notice);
    if (error == ESP_ERR_INVALID_ARG && prior == PIN_FLOW_OLD && ++s_pin_failures >= 3) {
        s_pin_failures = 0; s_pin_retry_at = ctx->now_ms + 10000;
        snprintf(s_pinpad.notice, sizeof(s_pinpad.notice), "请等待 10 秒后重试"); s_pinpad.blocked = true;
    }
    return pin_paint(ctx, (EpdRect){122, 218, 440, 247});
}
static int s_main_scroll;
static bool s_scroll_drag_consumed, s_scroll_present_pending;
static int s_font_page, s_wallpaper_page;
static bool s_sync_pending, s_sync_waiting;
static bool s_font_sync_confirm;
static char s_font_sync_path[TTF_FONT_PATH_MAX];
static bool s_config_confirm;
static bool s_batt_calibration_confirm;
static device_power_batt_calibration_status_t s_batt_calibration_status;
static int64_t s_batt_calibration_next_ms;
// 屏幕修复：点一下就开始。整屏盲刷要几秒，活儿交给 tick——不能在触摸回调里做。
// Panel repair: one tap starts it. The blind full-screen drive takes seconds, so the work goes to the
// tick rather than a touch callback.
static bool s_panel_repair_pending;
static bool s_boot_pending;
static bool s_upgrade_confirm, s_upgrade_online, s_local_pending, s_upgrade_restart;
static bool s_upgrade_shortcut_back;
static pico_ota_info_t s_local_update;
static EXT_RAM_BSS_ATTR pico_update_status_t s_update;
static char s_upgrade_notice[96];
static uint32_t s_update_drawn_percent, s_update_drawn_at;
static pico_update_state_t s_update_drawn_state;
static bool s_update_drawn_busy, s_upgrade_offer_pending;
typedef enum { UPGRADE_JOB_NONE, UPGRADE_JOB_CHECK, UPGRADE_JOB_DOWNLOAD } upgrade_job_t;
static upgrade_job_t s_upgrade_job;
static int s_upgrade_bar_width;
static uint32_t upgrade_percent(void) {
    uint32_t percent = s_update.release.size ? (uint64_t)s_update.received * 100 / s_update.release.size : 0;
    return percent > 100 ? 100 : percent;
}
static void upgrade_remember(uint32_t now) {
    s_update_drawn_state = s_update.state;
    s_update_drawn_busy = s_update.busy;
    s_update_drawn_percent = upgrade_percent();
    s_update_drawn_at = now;
}
// 结果等任务收尾后一次显示；进度只在五秒且增长五个百分点后补画新增像素。
// Show results once after worker cleanup; append progress after five seconds and five percentage points.
static app_redraw_t upgrade_status_redraw(uint32_t now) {
    bool downloading = s_update.state == PICO_UPDATE_DOWNLOADING;
    if (s_update.busy && s_update.state != PICO_UPDATE_CHECKING && !downloading)
        return APP_REDRAW_NONE;
    bool offer = s_upgrade_offer_pending && !s_update.busy && s_update.state == PICO_UPDATE_AVAILABLE;
    if (offer) {
        s_upgrade_offer_pending = false;
        s_upgrade_confirm = s_upgrade_online = true;
    } else if (!s_update.busy) s_upgrade_offer_pending = false;
    if (offer || s_update.state != s_update_drawn_state || s_update.busy != s_update_drawn_busy) {
        upgrade_remember(now);
        return APP_REDRAW_PAGE;
    }
    uint32_t percent = upgrade_percent();
    if (downloading && percent >= s_update_drawn_percent + 5 && now - s_update_drawn_at >= 5000) {
        upgrade_remember(now);
        return APP_REDRAW_AREA;
    }
    return APP_REDRAW_NONE;
}
static EpdRect upgrade_progress_area(void) { return (EpdRect){52, 468, 580, 64}; }
static void draw_upgrade_percent(uint8_t *fb) {
    uint32_t percent = upgrade_percent();
    EpdRect number = {496, 472, 132, 42};
    ui_fill_rect_fast(number, UI_GRAY_WHITE, fb);
    char progress[32]; snprintf(progress, sizeof(progress), "%lu%%", (unsigned long)percent);
    ui_text_fixed(fb, 624, 477, 26, progress, EPD_DRAW_ALIGN_RIGHT, false);
    ui_image_bw_rect(fb, number);
}
static void draw_upgrade_progress(uint8_t *fb) {
    ui_fill_rect_fast(upgrade_progress_area(), UI_GRAY_WHITE, fb);
    draw_upgrade_percent(fb);
    ui_fill_rect_fast((EpdRect){60, 518, 564, 10}, UI_GRAY_BLACK, fb);
    ui_fill_rect_fast((EpdRect){61, 519, 562, 8}, UI_GRAY_WHITE, fb);
    s_upgrade_bar_width = (int)(562 * upgrade_percent() / 100);
    if (s_upgrade_bar_width)
        ui_fill_rect_fast((EpdRect){61, 519, s_upgrade_bar_width, 8}, UI_GRAY_BLACK, fb);
}
static void draw_upgrade_progress_delta(uint8_t *fb) {
    int width = (int)(562 * upgrade_percent() / 100);
    if (width <= s_upgrade_bar_width) return;
    draw_upgrade_percent(fb);
    ui_fill_rect_fast((EpdRect){61 + s_upgrade_bar_width, 519, width - s_upgrade_bar_width, 8}, UI_GRAY_BLACK, fb);
    s_upgrade_bar_width = width;
}
// 排队时只改 UI 快照；本次画面推送结束后的 tick 才启动 TLS/擦写。
// Scheduling changes only the UI snapshot; start TLS/flash on the tick after its presentation.
static void upgrade_schedule(bool downloading, uint32_t now) {
    s_upgrade_job = downloading ? UPGRADE_JOB_DOWNLOAD : UPGRADE_JOB_CHECK;
    s_update.state = downloading ? PICO_UPDATE_DOWNLOADING : PICO_UPDATE_CHECKING;
    s_update.busy = true;
    s_update.received = 0;
    snprintf(s_update.message, sizeof(s_update.message), "%s", downloading
        ? "正在下载，短暂断网会自动重连" : "正在连接并检查更新");
    s_upgrade_offer_pending = !downloading;
    upgrade_remember(now);
}
static app_redraw_t upgrade_start_pending(void) {
    bool downloading = s_upgrade_job == UPGRADE_JOB_DOWNLOAD;
    s_upgrade_job = UPGRADE_JOB_NONE;
    app_home_cover_mode_changed();
    app_book_cover_mode_changed();
    esp_err_t error = downloading ? pico_online_download() : pico_online_check();
    pico_online_get_status(&s_update);
    if (error == ESP_OK) return APP_REDRAW_NONE;
    s_upgrade_offer_pending = false;
    s_update.busy = false;
    if (s_update.state != PICO_UPDATE_FAILED) s_update.message[0] = 0;
    s_update.state = PICO_UPDATE_FAILED;
    if (!s_update.message[0]) snprintf(s_update.message, sizeof(s_update.message), "%s",
        error == ESP_ERR_NO_MEM ? "内存不足，无法启动更新任务" :
        downloading ? "无法开始下载，请稍后重试" : "无法检查更新，请稍后重试");
    snprintf(s_upgrade_notice, sizeof(s_upgrade_notice), "%s", s_update.message);
    return APP_REDRAW_PAGE;
}
// 日志按 UTF-8 字符和实际字宽换行，限制六行，不影响下载时的局部刷新。
// Wrap notes on UTF-8 boundaries using measured widths; cap at six lines without changing download refreshes.
static bool upgrade_note_line(const char **cursor, char *line, size_t capacity) {
    if (!cursor || !*cursor || !line || capacity < 5) return false;
    const char *p = *cursor;
    while (*p && (strchr("\r\n\t ;", *p) || !strncmp(p, "；", 3)))
        p += !strncmp(p, "；", 3) ? 3 : 1;
    size_t used = 0;
    line[0] = 0;
    while (*p && !strchr("\r\n;", *p) && strncmp(p, "；", 3)) {
        unsigned char lead = (unsigned char)*p;
        size_t bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (bytes > strlen(p) || used + bytes >= capacity) break;
        memcpy(line + used, p, bytes);
        line[used + bytes] = 0;
        if (used && ui_text_fixed_width_px(26, line) > 540) { line[used] = 0; break; }
        used += bytes;
        p += bytes;
    }
    *cursor = p;
    return used != 0;
}
static void draw_upgrade_notes(uint8_t *fb) {
    const char *cursor = s_update.release.notes;
    char line[sizeof(s_update.release.notes)];
    if (!*cursor) {
        ui_text_fixed(fb, 60, 530, 26, "本次更新暂无说明", EPD_DRAW_ALIGN_LEFT, false);
        return;
    }
    for (int row = 0; row < 6 && upgrade_note_line(&cursor, line, sizeof(line)); ++row) {
        if (row == 5 && *cursor) {
            size_t used = strlen(line);
            while (used && (used + 4 > sizeof(line) || ui_text_fixed_width_px(26, line) > 514)) {
                do { --used; } while (used && ((unsigned char)line[used] & 0xc0) == 0x80);
                line[used] = 0;
            }
            memcpy(line + used, "…", 4);
        }
        ui_text_fixed(fb, 60, 530 + row * 38, 26, line, EPD_DRAW_ALIGN_LEFT, false);
    }
}
static EpdRect upgrade_confirm_button(bool online, bool accept) {
    return (EpdRect){accept ? 364 : 60, online ? 842 : 613, 260, 64};
}
static void upgrade_open(void) {
    pico_online_get_status(&s_update);
    s_upgrade_confirm = s_local_pending = s_upgrade_restart = false;
    s_upgrade_offer_pending = false;
    s_upgrade_job = UPGRADE_JOB_NONE;
    s_upgrade_bar_width = 0;
    upgrade_remember(esp_timer_get_time() / 1000);
    s_upgrade_notice[0] = 0;
    s_page = SETTINGS_UPGRADE;
}
bool app_device_settings_navigation_busy(void) {
    return s_boot_pending || s_local_pending || s_upgrade_restart;
}

bool app_device_settings_offer_update(bool already_in_settings) {
    if (already_in_settings) {
        if ((s_page != SETTINGS_MAIN && s_page != SETTINGS_UPGRADE) || s_upgrade_confirm) return false;
        pico_online_get_status(&s_update);
        if (s_update.busy || s_update.state != PICO_UPDATE_AVAILABLE) return false;
        upgrade_open();
        s_upgrade_confirm = s_upgrade_online = true;
        s_upgrade_shortcut_back = false;
    } else s_enter_page = SETTINGS_UPGRADE;
    return true;
}
static const char *const TAG = "device_settings";
// 主设置始终单行；大字号只改变字形，字体预览单独保留足够空间。
// Main rows never stack; larger glyphs share one centerline, with extra space only in font previews.

static bool settings_stacked(void) { return app_settings_system_font_size() >= 150; }
static int settings_profile_height(void) { return 106; }
static int settings_main_row_height(void) {
    return 82;
}
#define SETTINGS_ROW_H settings_main_row_height()
#define SETTINGS_DISPLAY_ROWS 6
#define SETTINGS_PERSONAL_ROWS 8
// BOOT 刷机入口已按用户要求从设置列表撤下；页面代码保留，恢复入口只需加回一行。
// The BOOT flashing entry is hidden per the user's request; the page stays, re-adding one row restores it.
#define SETTINGS_MAINTENANCE_ROWS 3
#define SETTINGS_WIRELESS_Y (180 + settings_profile_height() + 77)
static int settings_wireless_rows(void) {
    network_status_t network = {0};
    network_service_get_status(&network);
    return network.cellular_capable ? 4 : 3;
}
#define SETTINGS_DISPLAY_Y (SETTINGS_WIRELESS_Y + settings_wireless_rows() * SETTINGS_ROW_H + 78)
#define SETTINGS_PERSONAL_Y (SETTINGS_DISPLAY_Y + SETTINGS_DISPLAY_ROWS * SETTINGS_ROW_H + 78)
#define SETTINGS_MAINTENANCE_Y (SETTINGS_PERSONAL_Y + SETTINGS_PERSONAL_ROWS * SETTINGS_ROW_H + 78)
// 电源管理集中充电档位、本机电量学习及日志。/ Power management groups charge profiles, local battery learning and logging.
#define SETTINGS_POWER_ROWS 3
#define SETTINGS_POWER_Y (SETTINGS_MAINTENANCE_Y + SETTINGS_MAINTENANCE_ROWS * SETTINGS_ROW_H + 78)
// 设置行图标：32 像素盒，在行高 68 里垂直居中；墨色统一，避免一行一个灰度。
// Setting row icons: a 32 px box centred in the 68 px row with one shared ink level.
#define SETTINGS_ICON_PX 32
#define SETTINGS_ICON_INK 0x58
// 版本行排在最后一组下方，是列表的最后一行内容；间距按行距留白，不跟设置组抢位置。
// The version footer follows the last group as the list's final content line, spaced like a group
// gap so it never crowds the settings.
#define SETTINGS_VERSION_GAP 40
static int settings_version_center_y(void) {
    return SETTINGS_POWER_Y + SETTINGS_POWER_ROWS * SETTINGS_ROW_H + SETTINGS_VERSION_GAP
           + ui_text_effective_px(20) / 2;
}
// 主页面滚动上限：把版本行完整推到导航栏之上，多留 12px 余量。
// Main-page scroll limit: bring the whole version line above the nav bar with 12 px to spare.
static int settings_scroll_max(void) {
    return settings_version_center_y() + (ui_text_effective_px(20) + 1) / 2
           - UI_NAV_REFRESH_END + 12;
}
#define SETTINGS_SCROLL_MAX settings_scroll_max()
static char s_editor[96], s_editor_notice[80];
static ui_text_edit_t s_editor_input;
static bool s_editor_signature;
static const char *system_font_label(const char *path) {
    if (!path || !path[0]) return "思源黑体";
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    static char stem[TTF_FONT_NAME_MAX];
    snprintf(stem, sizeof(stem), "%s", name);
    size_t len = strlen(stem);
    if (len >= 4 && !strcasecmp(stem + len - 4, ".ttf")) stem[len - 4] = 0;
    if (!strcasecmp(stem, "Hei")) return "思源黑体（TF 卡）";
    return ttf_font_localized_name(stem);
}
typedef struct {
    EpdRect size, preview;
    int preview_title, font_title, first, row_height, slots;
} system_font_layout_t;
static system_font_layout_t system_font_layout(void) {
    system_font_layout_t l = settings_stacked()
        ? (system_font_layout_t){{36,197,612,183},{36,471,612,216},413,722,785,142,2}
        : (system_font_layout_t){{36,197,612,100},{36,379,612,154},322,560,615,99,4};
    return l;
}
static EpdRect system_font_item_rect(int slot) {
    system_font_layout_t l = system_font_layout();
    return (EpdRect){36,l.first + slot * l.row_height,612,l.row_height - 16};
}
static EpdRect system_font_sync_button(bool sync) { return (EpdRect){sync ? 351 : 72,683,261,74}; }
static void draw_system_font_sync(uint8_t *fb) {
    // 仅一个有界弹窗，不额外打开阅读字体。/ One bounded dialog; never load the reading face here.
    settings_card(fb,(EpdRect){48,389,588,401},28,UI_GRAY_WHITE,0x58);
    ui_text_fixed_vc(fb,342,448,32,"同步到阅读字体？",EPD_DRAW_ALIGN_CENTER,false);
    char name[80]; snprintf(name,sizeof(name),"已选择「%s」",system_font_label(s_font_sync_path));
    while (ui_text_fixed_width_px(27,name) > 520 && strlen(name) > 6) {
        size_t n = strlen(name) - 1;
        while (n && ((unsigned char)name[n] & 0xc0) == 0x80) --n;
        name[n] = 0;
    }
    ui_text_fixed_vc(fb,342,516,27,name,EPD_DRAW_ALIGN_CENTER,false);
    ui_text_fixed_vc(fb,342,570,24,"是否让阅读正文也使用这个字体？",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_fixed_vc(fb,342,614,23,s_notice[0]?s_notice:"不同步时，保留原来的阅读字体。",EPD_DRAW_ALIGN_CENTER,false);
    for (int i=0;i<2;++i) {
        EpdRect r=system_font_sync_button(i);
        settings_card(fb,r,20,i ? UI_GRAY_BLACK : UI_GRAY_WHITE,0x58);
        ui_text_fixed_vc(fb,r.x+r.width/2,r.y+r.height/2,25,i ? "同步阅读字体" : "仅系统字体",EPD_DRAW_ALIGN_CENTER,i);
    }
}
static int s_year, s_month, s_day, s_hour, s_minute;
typedef struct { char path[288]; char name[96]; } wallpaper_item_t;
// 壁纸列表放 PSRAM，按需扩容，不以固定数量截断 TF 卡目录。
// The wallpaper list lives in PSRAM and grows on demand instead of truncating the SD directory.
static wallpaper_item_t *s_wallpapers;
static size_t s_wallpaper_capacity;

static bool wallpapers_reserve(size_t needed) {
    if (needed <= s_wallpaper_capacity) return true;
    size_t capacity = s_wallpaper_capacity ? s_wallpaper_capacity : 64;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) { capacity = needed; break; }
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(wallpaper_item_t)) return false;
    wallpaper_item_t *next = heap_caps_realloc(s_wallpapers, capacity * sizeof(*next),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!next) return false;
    s_wallpapers = next;
    s_wallpaper_capacity = capacity;
    return true;
}
static bool wallpapers_alloc(void) { return wallpapers_reserve(1); }
static int s_wallpaper_count;
static int s_wallpaper_selected;

#define READING_BG_THUMB_SLOTS 6
#define READING_BG_THUMB_WIDTH 128
#define READING_BG_THUMB_HEIGHT 136
#define READING_BG_THUMB_DECODE_WIDTH 64
#define READING_BG_THUMB_DECODE_HEIGHT 68
#define READING_BG_CARD_TOP 320
#define READING_BG_CARD_WIDTH 300
#define READING_BG_CARD_HEIGHT 202
#define READING_BG_CARD_GAP_X 12
#define READING_BG_CARD_GAP_Y 12
static EpdRect reading_bg_card_rect(int slot) {
    if (slot < 0 || slot >= READING_BG_THUMB_SLOTS) return (EpdRect){0};
    return (EpdRect){36 + (slot % 2) * (READING_BG_CARD_WIDTH + READING_BG_CARD_GAP_X),
                     READING_BG_CARD_TOP + (slot / 2) * (READING_BG_CARD_HEIGHT + READING_BG_CARD_GAP_Y),
                     READING_BG_CARD_WIDTH, READING_BG_CARD_HEIGHT};
}
static EpdRect reading_bg_restore_rect(void) { return (EpdRect){36, 966, 252, 60}; }
static EpdRect reading_bg_thumbnail_rect(int slot) {
    EpdRect card = reading_bg_card_rect(slot);
    return (EpdRect){card.x + 14, card.y + 14, READING_BG_THUMB_WIDTH, READING_BG_THUMB_HEIGHT};
}
static uint8_t *s_reading_bg_thumbnails[READING_BG_THUMB_SLOTS];
static uint8_t s_reading_bg_thumbnail_state[READING_BG_THUMB_SLOTS];
static int s_reading_bg_thumbnail_page = -1;

static void reading_bg_thumbnails_release(void) {
    for (unsigned i = 0; i < READING_BG_THUMB_SLOTS; ++i) {
        free(s_reading_bg_thumbnails[i]);
        s_reading_bg_thumbnails[i] = NULL;
        s_reading_bg_thumbnail_state[i] = 0;
    }
    s_reading_bg_thumbnail_page = -1;
}
static void reading_bg_thumbnails_reset(int page) {
    reading_bg_thumbnails_release();
    s_reading_bg_thumbnail_page = page;
}
static bool wallpaper_png(const char *path) {
    const char *ext = strrchr(path ? path : "", '.');
    return ext && !strcasecmp(ext, ".png");
}
static bool reading_bg_thumbnail_prepare(int slot) {
    if (slot < 0 || slot >= READING_BG_THUMB_SLOTS) return false;
    int index = s_wallpaper_page * READING_BG_THUMB_SLOTS + slot;
    if (index < 0 || index >= s_wallpaper_count) return false;
    uint8_t *gray = malloc((size_t)READING_BG_THUMB_DECODE_WIDTH * READING_BG_THUMB_DECODE_HEIGHT);
    if (!gray) return false;
    bool ok = book_image_file_grayscale(s_wallpapers[index].path, wallpaper_png(s_wallpapers[index].path),
                                        READING_BG_THUMB_DECODE_WIDTH, READING_BG_THUMB_DECODE_HEIGHT, gray);
    if (!ok) { free(gray); return false; }
    free(s_reading_bg_thumbnails[slot]);
    s_reading_bg_thumbnails[slot] = gray;
    return true;
}
static const uint8_t *reading_bg_thumbnail_at(int slot) {
    return s_reading_bg_thumbnail_page == s_wallpaper_page && s_reading_bg_thumbnail_state[slot] == 1 ? s_reading_bg_thumbnails[slot] : NULL;
}
static bool reading_bg_thumbnail_tick(bool touched) {
    if (s_reading_bg_thumbnail_page != s_wallpaper_page)
        reading_bg_thumbnails_reset(s_wallpaper_page);
    // 空闲 tick 每次解一张，整页就绪后再刷新。/ Decode one image per idle tick and refresh once the page is ready.
    if (!touched) {
        int visible = s_wallpaper_count - s_wallpaper_page * READING_BG_THUMB_SLOTS;
        if (visible > READING_BG_THUMB_SLOTS) visible = READING_BG_THUMB_SLOTS;
        for (int slot = 0; slot < visible; ++slot) {
            if (s_reading_bg_thumbnail_state[slot]) continue;
            s_reading_bg_thumbnail_state[slot] = reading_bg_thumbnail_prepare(slot) ? 1 : 2;
            if (slot + 1 == visible) return true;
            break;
        }
    }
    return false;
}
static void reading_bg_thumbnail_draw(uint8_t *fb, EpdRect rect, const uint8_t *gray) {
    if (!gray) {
        ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2, 16, "暂无预览", EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    for (int y = 0; y < rect.height && y < READING_BG_THUMB_HEIGHT; ++y) {
        const unsigned sy = (unsigned)y * READING_BG_THUMB_DECODE_HEIGHT / (unsigned)rect.height;
        for (int x = 0; x < rect.width && x < READING_BG_THUMB_WIDTH; ++x) {
            const unsigned sx = (unsigned)x * READING_BG_THUMB_DECODE_WIDTH / (unsigned)rect.width;
            epd_draw_pixel(rect.x + x, rect.y + y,
                           ui_image_dither_gray(gray[(size_t)sy * READING_BG_THUMB_DECODE_WIDTH + sx],
                                                rect.x + x, rect.y + y), fb);
        }
    }
}

// 阅读背景选择：预览解码延迟到 tick，ready 只在解码成功后置位，notice 兼作列表页反馈行。
// Reading-background picker: preview decoding defers to the tick, ready is set only after a
// successful decode, and notice doubles as the list page's feedback line.
static bool s_reading_bg_confirm, s_reading_bg_ready, s_reading_bg_pending;
static char s_reading_bg_notice[96];

static bool wallpaper_scan_dir(const char *root) {
    if (!wallpapers_alloc()) return false;
    bool complete = true;
    DIR *dir = opendir(root);
    if (!dir) return errno == ENOENT;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) { complete = errno == 0; break; }
        if (entry->d_name[0] == '.' || !strncmp(entry->d_name, "._", 2)) continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || (strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".png"))) continue;
        if (!wallpapers_reserve((size_t)s_wallpaper_count + 1u)) { complete = false; break; }
        wallpaper_item_t *item = &s_wallpapers[s_wallpaper_count];
        int written = snprintf(item->path, sizeof(item->path), "%s/%s", root, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(item->path)) continue;
        struct stat st;
        if (stat(item->path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) continue;
        size_t name_len = strnlen(entry->d_name, sizeof(item->name) - 1);
        memcpy(item->name, entry->d_name, name_len);
        item->name[name_len] = 0;
        ++s_wallpaper_count;
    }
    return closedir(dir) == 0 && complete;
}
static void wallpaper_scan(void) {
    s_wallpaper_count = s_wallpaper_page = 0;
    wallpaper_scan_dir("/sdcard/pictures");
    wallpaper_scan_dir("/sdcard/images");
    wallpaper_scan_dir("/sdcard");
}
// 阅读背景的保存只接受 /sdcard/pictures 前缀，列表就不得显示其它目录，否则点了也存不上。
// The reading-background setter accepts only the /sdcard/pictures prefix, so the list must not
// offer other folders; picking one would fail to persist.
static void reading_bg_scan(void) {
    reading_bg_thumbnails_release();
    s_wallpaper_count = s_wallpaper_page = 0;
    s_reading_bg_notice[0] = 0;
    if (!wallpaper_scan_dir("/sdcard/pictures"))
        snprintf(s_reading_bg_notice, sizeof(s_reading_bg_notice), "图片列表未完整读取");
    s_wallpaper_selected = 0;
    for (int i = 0; i < s_wallpaper_count; ++i)
        if (!strcmp(s_wallpapers[i].path, app_settings_book_background_path())) s_wallpaper_selected = i;
}

static void profile_editor_open(bool signature) {
    s_editor_signature = signature;
    snprintf(s_editor, sizeof(s_editor), "%s", signature ? app_settings_status_signature() : app_settings_device_name());
    ui_text_edit_init(&s_editor_input, s_editor, sizeof(s_editor));
    s_editor_notice[0] = 0;
    ui_keyboard_begin(&s_editor_input, false);
    s_page = SETTINGS_TEXT_EDIT;
}

static int days_in_month(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) return 29;
    return days[month - 1];
}

// 公历日期转 UTC 天数；界面统一按北京时间 UTC+8 编辑。
// Convert a Gregorian date to UTC days; the editor uses Beijing time (UTC+8).
static int64_t civil_days(int year, int month, int day) {
    year -= month <= 2;
    int era = year / 400;
    int yoe = year - era * 400;
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

static void time_open(void) {
    time_t now = time(NULL);
    if (now < 1704067200) {
        const pmu_snapshot_t *pmu = device_power_get();
        if (pmu && pmu->time_synced && pmu->unix_sec >= 1704067200) now = pmu->unix_sec;
    }
    if (now < 1704067200) {
        char month[4] = "Jan"; int day = 1, year = 2024;
        if (sscanf(__DATE__, "%3s %d %d", month, &day, &year) != 3) {
            strcpy(month, "Jan"); day = 1; year = 2024;
        }
        static const char *const months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        int m = 1; while (m < 12 && strcmp(month, months[m - 1])) ++m;
        s_year = year; s_month = m; s_day = day; s_hour = s_minute = 0;
    } else {
        struct tm date;
        time_t local = now + 8 * 3600;
        gmtime_r(&local, &date);
        s_year = date.tm_year + 1900; s_month = date.tm_mon + 1; s_day = date.tm_mday;
        s_hour = date.tm_hour; s_minute = date.tm_min;
    }
    s_notice[0] = 0;
    s_page = SETTINGS_TIME_EDIT;
}

static void time_adjust(int field, int step) {
    if (field == 0) s_year = s_year + step > 2099 ? 2024 : s_year + step < 2024 ? 2099 : s_year + step;
    if (field == 1) s_month = s_month + step > 12 ? 1 : s_month + step < 1 ? 12 : s_month + step;
    if (field == 2) s_day = s_day + step > days_in_month(s_year, s_month) ? 1 :
                            s_day + step < 1 ? days_in_month(s_year, s_month) : s_day + step;
    if (field == 3) s_hour = (s_hour + step + 24) % 24;
    if (field == 4) s_minute = (s_minute + step + 60) % 60;
    if (s_day > days_in_month(s_year, s_month)) s_day = days_in_month(s_year, s_month);
}

static bool time_save(void) {
    int64_t epoch = civil_days(s_year, s_month, s_day) * 86400 +
                    s_hour * 3600 + s_minute * 60 - 8 * 3600;
    if (epoch < 1704067200 || epoch > 4102444799LL) return false;
    struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
    if (settimeofday(&tv, NULL)) return false;
    uint8_t payload[4] = {(uint8_t)epoch, (uint8_t)(epoch >> 8),
                          (uint8_t)(epoch >> 16), (uint8_t)(epoch >> 24)};
    esp_err_t err = device_power_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
    if (err == ESP_OK) err = device_power_cmd(PMU_CMD_TIME_GET, NULL, 0);
    const pmu_snapshot_t *pmu = device_power_get();
    if (err != ESP_OK || !pmu || !pmu->time_synced) {
        snprintf(s_notice, sizeof(s_notice), "系统时间已设置，PMU 同步失败");
        return false;
    }
    snprintf(s_notice, sizeof(s_notice), "时间已保存，并同步到电源管理芯片");
    s_page = SETTINGS_TIME;
    return true;
}

static void settings_card(uint8_t *fb, EpdRect rect, int radius, uint8_t fill, uint8_t edge) {
    ui_fill_round_rect(fb, rect, radius, fill);
    ui_draw_control_frame(fb, rect, radius, edge);
}
static void settings_divider(uint8_t *fb, int y, int x, int width) {
    ui_draw_separator(fb, y, x, width, 0x60);
}
static void settings_value(uint8_t *fb,int x,int cy,int px,const char *value) {
    char shown[128]; snprintf(shown,sizeof(shown),"%s",value);
    size_t n=strlen(shown);
    bool arrow=n>=3 && !strcmp(shown+n-3,"›");
    if (arrow) {
        shown[n-=3]=0;
        while (n && shown[n-1]==' ') shown[--n]=0;
        ui_draw_icon(fb,x-4,cy,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
    }
    ui_text_vc(fb,x-(arrow?28:0),cy,px,shown,EPD_DRAW_ALIGN_RIGHT,false);
}
static void row(uint8_t *fb, int y, const char *label, const char *value) {
    ui_text(fb, 54, y + 17, 27, label, EPD_DRAW_ALIGN_LEFT, false);
    settings_value(fb,627,y+20+ui_text_effective_px(21)/2,21,value);
    settings_divider(fb, y + 68, 54, 575);
}
static void section(uint8_t *fb, int y, const char *title) {
    ui_text(fb, 40, y, 22, title, EPD_DRAW_ALIGN_LEFT, false);
}

/* ---- 日期与时间页 / Date and time page ---- */

// 渲染与命中测试共用同一组坐标，避免"画在一处、点在别处"。
// Painting and hit testing share these coordinates, so a control is never drawn where it cannot be tapped.
enum {
    TIME_ROW_CURRENT_Y = 296,
    TIME_ROW_AUTO_Y = 380,
    TIME_ROW_WIFI_Y = 470,
    TIME_ROW_MANUAL_Y = 560,
    TIME_ROW_HIT_H = 70,
    TIME_LAST_LINE_Y = 660,
    TIME_HINT_LINE_Y = 700,
    TIME_ADVICE_LINE_Y = 740,
    TIME_NOTICE_LINE_Y = 830,
};

// 上次对时按"多久以前"显示比记日期好读；从未对时和时钟无效另说。
// The last sync reads better as an age than as a date; never-synced and an invalid clock differ.
static void time_last_sync_text(char *out, size_t cap) {
    uint32_t last = app_settings_time_sync_last();
    time_t now = time(NULL);
    if (!last) { snprintf(out, cap, "上次对时：从未"); return; }
    if (now < 1704067200) {
        struct tm tm;
        time_t local = (time_t)last + 8 * 3600;
        if (gmtime_r(&local, &tm))
            snprintf(out, cap, "上次对时：%04d-%02d-%02d %02d:%02d",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        else
            snprintf(out, cap, "上次对时：--");
        return;
    }
    int64_t age = (int64_t)now - (int64_t)last;
    if (age < 0) age = 0;
    if (age < 60) snprintf(out, cap, "上次对时：刚刚");
    else if (age < 3600) snprintf(out, cap, "上次对时：%lld 分钟前", (long long)(age / 60));
    else if (age < 86400) snprintf(out, cap, "上次对时：%lld 小时前", (long long)(age / 3600));
    else snprintf(out, cap, "上次对时：%lld 天前", (long long)(age / 86400));
}

// 自动对时能否生效取决于开关与已保存的网络，缺哪一样就说哪一样。
// Auto-sync needs the switch and a saved network; name whichever one is missing.
static void time_auto_hint(char *out, size_t cap) {
    if (!app_settings_auto_time_sync()) {
        snprintf(out, cap, "自动对时已关闭，只能按上面的按钮手动对时。");
        return;
    }
    network_status_t network = {0};
    network_service_get_status(&network);
    char ssid[33];
    bool saved_wifi = false;
    (void)wifi_transfer_get_saved_wifi(ssid, &saved_wifi);
    const bool wifi_configured = saved_wifi || network.wifi_enabled || network.wifi_ready;
    const bool cellular_available = network.cellular_capable && network.cellular_enabled;
    if (!wifi_configured && !cellular_available) {
        snprintf(out, cap, "自动对时需要先启用 Wi-Fi 或 4G 网络。");
        return;
    }
    snprintf(out, cap, "自动对时每 6 小时一次。");
}

static void back_header(uint8_t *fb, const char *title) {
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, title, EPD_DRAW_ALIGN_CENTER, false);
    settings_divider(fb, 174, 36, 612);
}

static EpdRect standby_mode_rect(unsigned row, bool timed) {
    return (EpdRect){36, 300 + (int)row * 154, 612, row == 2 && timed ? 222 : 132};
}

static EpdRect standby_duration_rect(unsigned column) {
    return (EpdRect){64 + (int)column * 188, 738, 172, 66};
}

static void setting_icon(uint8_t *fb, int index, int cx, int cy) {
    // 每个设置项对应一个生成图标；索引顺序由各分组传入的 icons[] 数组决定。
    // One generated icon per setting row; the index order comes from each group's icons[] array.
    static const ui_icon_t icons[] = {
        UI_ICON_WIFI,              // 0 无线连接
        UI_ICON_BLUETOOTH,         // 1 蓝牙
        UI_ICON_TYPE,              // 2 系统字体、阅读操作
        UI_ICON_A_LARGE_SMALL,     // 3 系统字号
        UI_ICON_LIBRARY_BIG,       // 4 书架样式
        UI_ICON_TICKET,            // 5 锁屏样式
        UI_ICON_CLOCK,             // 6 日期与时间
        UI_ICON_CONTRAST,          // 7 系统对比度
        UI_ICON_COPY,              // 8 保存与恢复
        UI_ICON_POWER,             // 9 待机方式
        UI_ICON_TEXT_ALIGN_START,  // 10 状态栏签名
        UI_ICON_REFRESH_CW,        // 11 首页强刷
        UI_ICON_DOWNLOAD,          // 12 系统升级
        UI_ICON_CPU,               // 13 BOOT 刷机
        UI_ICON_EYE_CLOSED,        // 14 自动休眠
        UI_ICON_SLIDERS_HORIZONTAL, // 15 主页刷新模式 / Main refresh mode
        UI_ICON_LOCK,              // 16 锁屏密码 / lock screen password
        UI_ICON_ROTATE_CW,         // 17 屏幕修复 / panel repair
        UI_ICON_VIBRATE,           // 18 触摸震动 / touch vibration
        UI_ICON_BOOK_OPEN,         // 19 阅读背景 / reading background
        UI_ICON_ZAP,               // 20 电源管理 / power management
        UI_ICON_LIBRARY_BIG,       // 21 组件库 / components
        UI_ICON_MOGO_CELLULAR_SIGNAL, // 22 移动网络
    };
    if (index < 0 || index >= (int)(sizeof(icons) / sizeof(icons[0]))) return;
    ui_draw_icon(fb, cx, cy, SETTINGS_ICON_PX, icons[index], SETTINGS_ICON_INK);
}

static void settings_fit_text(char *value, int px, int width) {
    while (value[0] && ui_text_fixed_width_px(ui_text_effective_px(px),value)>width) {
        size_t n=strlen(value)-1;while(n&&((unsigned char)value[n]&0xc0)==0x80)--n;value[n]=0;
    }
}
static void fit_value(char *value, int width) { settings_fit_text(value,20,width); }

static void setting_group(uint8_t *fb, int title_y, const char *title,
                          int card_y, const int icons[], const char *const labels[],
                          const char *const values[], int count) {
    const int row_height = SETTINGS_ROW_H;
    title_y -= s_main_scroll;
    card_y -= s_main_scroll;
    // 屏外的行不测字、不生成字形；保留卡片边界与原滚动坐标。
    // Skip off-screen glyph work while preserving card boundaries and scroll coordinates.
    if (card_y + count * row_height <= 160 || title_y >= UI_NAV_TOP) return;
    if (title_y + ui_text_effective_px(20) > 160 && title_y < UI_NAV_TOP)
        ui_text(fb, 42, title_y, 20, title, EPD_DRAW_ALIGN_LEFT, false);
    settings_card(fb, (EpdRect){36, card_y, 612, count * row_height}, 22,
                  UI_GRAY_WHITE, 0x70);
    for (int i = 0; i < count; ++i) {
        int y = card_y + i * row_height;
        if (y + row_height <= 160 || y >= UI_NAV_TOP) continue;
        if (i) settings_divider(fb, y, 88, 544);
        setting_icon(fb, icons[i], 67, y + row_height / 2);
        char shown[128]; snprintf(shown,sizeof(shown),"%s",values[i]);
        int value_width = 594 - 100 - 20 - ui_text_fixed_width_px(ui_text_effective_px(25),labels[i]);
        if (value_width > 215) value_width = 215;
        if (value_width < 0) value_width = 0;
        fit_value(shown,value_width);
        ui_text_vc(fb,100,y+row_height/2,25,labels[i],EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,594,y+row_height/2,20,shown,EPD_DRAW_ALIGN_RIGHT,false);
        // 导航是图标，不依赖用户字体是否包含 › 字形，也不会被截短掉。
        // Navigation is an icon, independent of the font cmap and value truncation.
        ui_draw_icon(fb,625,y+row_height/2,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
    }
}

static void setting_toggle_track(uint8_t *fb, EpdRect track, bool active) {
    ui_fill_round_rect(fb, track, 19, active ? 0x58 : 0xc4);
    int cx = active ? track.x + 49 : track.x + 20;
    epd_fill_circle(cx, track.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, track.y + 19, 16, UI_GRAY_BLACK, fb);
}

static void setting_toggle(uint8_t *fb, int y, const char *title, const char *detail, bool active) {
    settings_card(fb, (EpdRect){36, y, 612, 126}, 22, UI_GRAY_WHITE, 0x70);
    ui_text(fb, 59, y + 24, 27, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 59, y + 75, 19, detail, EPD_DRAW_ALIGN_LEFT, false);
    setting_toggle_track(fb, (EpdRect){555, y + 43, 69, 39}, active);
}

enum { SHELF_STYLE_CARD_COUNT = 4 };
static EpdRect shelf_style_card(int index) {
    return index < 2 ? (EpdRect){36, 263 + index * 300, 612, 277} :
                       (EpdRect){36 + (index - 2) * 314, 863, 298, 230};
}
static EpdRect shelf_style_titles_row(EpdRect card) {
    return (EpdRect){card.x, card.y + 228, card.width, card.height - 228};
}
static uint8_t shelf_style_id(int index) {
    static const uint8_t ids[] = {1, 2, 5, APP_SHELF_STYLE_NOTEPAD};
    return ids[index];
}
static const char *shelf_style_name(uint8_t style) {
    if (style == APP_SHELF_STYLE_NOTEPAD) return "便签本";
    return style == 5 ? "列表书架" : style == 1 ? "深色书轨" : "亚克力书架";
}
static void draw_compact_shelf_style(uint8_t *fb, EpdRect card, uint8_t style, bool active) {
    const EpdRect art = {card.x + 22, card.y + 13, card.width - 44, 145};
    if (style == APP_SHELF_STYLE_NOTEPAD) {
        ui_notepad_thumbnail(fb, art);
    } else {
        ui_fill_round_rect(fb, art, 9, UI_GRAY_WHITE);
        for (int i = 0; i < 3; ++i) {
            const int y = art.y + 10 + 43 * i;
            ui_fill_rect_fast((EpdRect){art.x + 10, y, 25, 34}, i == 1 ? UI_GRAY_LIGHT : UI_GRAY_BLACK, fb);
            ui_hairline(fb, y + 5, art.x + 47, 110, UI_GRAY_BLACK);
            ui_hairline(fb, y + 15, art.x + 47, 57, 0x50);
            ui_fill_rect_fast((EpdRect){art.x + 47, y + 25, art.width - 60, 3}, 0xc0, fb);
            ui_fill_rect_fast((EpdRect){art.x + 47, y + 25, 40 + 25 * i, 3}, 0x40, fb);
        }
    }
    const int title_px = ui_text_effective_px(23), detail_px = ui_text_effective_px(17);
    int x = card.x + 22, cy = card.y + 179, rx = card.x + card.width - 35;
    ui_text_fixed_vc(fb, x, cy, title_px > 30 ? 30 : title_px, shelf_style_name(style), EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed_vc(fb, x, card.y + 207, detail_px > 22 ? 22 : detail_px,
                     style == APP_SHELF_STYLE_NOTEPAD ? "无封面 · 每页 10 本" : "含封面 · 每页 4 本", EPD_DRAW_ALIGN_LEFT, false);
    epd_draw_circle(rx, cy, 13, UI_GRAY_BLACK, fb);
    if (active) epd_fill_circle(rx, cy, 6, UI_GRAY_BLACK, fb);
}

static void draw_style_thumbnail(uint8_t *fb, int style, int top, bool titles) {
    if (top < 242 || top + 170 >= UI_NAV_TOP) return;
    const int left = 99;
    for (int i = 0; i < 3; ++i) {
        int x = left + 20 + i * 155;
        EpdRect cover = {x, top, 116, 145};
        ui_fill_rect_fast(cover, i == 1 ? UI_GRAY_BLACK : i == 2 ? UI_GRAY_LIGHT : UI_GRAY_BLACK, fb);
        ui_draw_round_rect(fb, cover, 1, 0x30);
        ui_draw_round_rect(fb, (EpdRect){x + 7, top + 7, 102, 130}, 1, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 47, x + 28, 60, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 54, x + 39, 38, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 111, x + 44, 28, i == 1 ? 0xc0 : 0xe0);
    }
    if (style == 1) {
        ui_fill_rect_fast((EpdRect){left, top + 143, 486, 16}, UI_GRAY_BLACK, fb);
        ui_hairline(fb, top + 143, left, 486, 0x80);
    } else if (style == 2) {
        ui_draw_acrylic_guard(fb, (EpdRect){left, top + 129, 486, 35});

    }
    if (titles) for (int i = 0; i < 3; ++i)
        ui_fill_rect_fast((EpdRect){left + 43 + i * 155, top + 172, 70, 3}, UI_GRAY_BLACK, fb);
}

// 仅 82x82 灰阶头像，不缓存整屏或第二份字库；文件变更/拔卡会使缓存失效。
// One 82x82 gray avatar, never a full frame or second font; file changes/removal invalidate it.
static EXT_RAM_BSS_ATTR uint8_t s_avatar_pixels[82*82];
static char s_avatar_path[288];
static bool s_avatar_ready, s_avatar_pending;
static char s_main_wifi[33];
static bool s_main_wifi_saved;
// 版本行取应用描述里的版本号与 UTC 构建日期，把 __DATE__ 的 "Oct 26 2026" 排成 2026-10-26。
// The footer takes the app description's version and UTC build date, rendering __DATE__'s
// "Oct 26 2026" as 2026-10-26.
static const char *settings_version_text(void) {
    static char text[128];
    const esp_app_desc_t *desc = esp_app_get_description();
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = 0;
    for (int i = 0; i < 12; ++i)
        if (!strncmp(desc->date, months[i], 3)) { month = i + 1; break; }
    int day = atoi(desc->date + 4), year = atoi(desc->date + 7);
    if (month && day > 0 && year > 0)
        snprintf(text, sizeof(text), "版本 %s · 构建 %04d-%02d-%02d", desc->version, year, month, day);
    else
        snprintf(text, sizeof(text), "版本 %s · 构建 %s", desc->version, desc->date);
    return text;
}
// 系统字号最大 200% 时版本行会顶到两侧白边，按可用宽度收一级字号再画。
// At the 200% system size the footer would reach the white margins, so step the face down to fit.
static void draw_settings_version(uint8_t *fb, int center_y) {
    const char *text = settings_version_text();
    int px = ui_text_effective_px(20);
    while (px > 14 && ui_text_fixed_width_px(px, text) > 600) --px;
    ui_text_fixed_vc(fb, 342, center_y, px, text, EPD_DRAW_ALIGN_CENTER, false);
}

static void memory_size_text(char *out, size_t cap, size_t bytes) {
    if (bytes >= 1024u * 1024u)
        snprintf(out, cap, "%u.%u MB", (unsigned)(bytes / (1024u * 1024u)),
                 (unsigned)((bytes % (1024u * 1024u)) * 10u / (1024u * 1024u)));
    else
        snprintf(out, cap, "%u KB", (unsigned)((bytes + 512u) / 1024u));
}

static const char *memory_clean_state_text(mm_clean_state_t state) {
    switch (state) {
        case MM_CLEAN_REQUESTED: return "等待主循环执行";
        case MM_CLEAN_RUNNING: return "正在回收可重建缓存";
        case MM_CLEAN_DONE: return "上次清理已完成";
        case MM_CLEAN_PARTIAL: return "部分资源仍在使用";
        default: return "尚未清理";
    }
}

static bool memory_snapshot_changed(const mm_snapshot_t *left, const mm_snapshot_t *right) {
    mm_snapshot_t a = *left;
    mm_snapshot_t b = *right;
    // updated_ms 是采样时间戳，不应单独触发屏幕刷新。
    // updated_ms is a sampling timestamp, not a visible state change.
    a.updated_ms = 0;
    b.updated_ms = 0;
    return memcmp(&a, &b, sizeof(a)) != 0;
}

static void memory_pool_card(uint8_t *fb, int y, const char *title, const mm_pool_snapshot_t *pool) {
    char free_text[24], largest_text[24], minimum_text[24], used_text[24], total_text[24], summary[64];
    memory_size_text(free_text, sizeof(free_text), pool->free_bytes);
    memory_size_text(largest_text, sizeof(largest_text), pool->largest_free);
    memory_size_text(minimum_text, sizeof(minimum_text), pool->minimum_free);
    memory_size_text(used_text, sizeof(used_text), pool->used_bytes);
    memory_size_text(total_text, sizeof(total_text), pool->total);
    snprintf(summary, sizeof(summary), "%s · 已用 %u%%", total_text,
             pool->total ? (unsigned)((uint64_t)pool->used_bytes * 100u / pool->total) : 0);
    settings_card(fb, (EpdRect){36, y, 612, 136}, 22, UI_GRAY_WHITE, 0x70);
    ui_text_fixed_vc(fb, 60, y + 26, 25, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed_vc(fb, 616, y + 26, 17, summary, EPD_DRAW_ALIGN_RIGHT, false);
    ui_text_fixed_vc(fb, 60, y + 76, 19, "可用", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed_vc(fb, 224, y + 76, 19, free_text, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed_vc(fb, 350, y + 76, 19, "最大连续", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed_vc(fb, 610, y + 76, 19, largest_text, EPD_DRAW_ALIGN_RIGHT, false);
    ui_text_fixed_vc(fb, 60, y + 112, 17, "历史最低", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed_vc(fb, 224, y + 112, 17, minimum_text, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed_vc(fb, 350, y + 112, 17, "已用", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed_vc(fb, 610, y + 112, 17, used_text, EPD_DRAW_ALIGN_RIGHT, false);
}

static display_rect_t memory_area_hint(app_ctx_t *ctx) {
    (void)ctx;
    return display_rect_from_epd((EpdRect){0, 218, UI_LOCK_WIDTH, UI_NAV_REFRESH_END - 218});
}

static void memory_render_content(uint8_t *fb) {
    ui_fill_rect_fast((EpdRect){0, 218, UI_LOCK_WIDTH, UI_NAV_REFRESH_END - 218}, UI_GRAY_WHITE, fb);
    ui_text_fixed_vc(fb, 36, 232, 19, "实时采样 500ms · 屏幕按变化更新", EPD_DRAW_ALIGN_LEFT, false);
    memory_pool_card(fb, 260, "内部 RAM", &s_memory_snapshot.pool[MM_POOL_INTERNAL]);
    memory_pool_card(fb, 404, "PSRAM", &s_memory_snapshot.pool[MM_POOL_PSRAM]);
    ui_text_fixed_vc(fb, 36, 578, 19, s_memory_snapshot.inventory_partial
        ? "可归属资源 · 繁忙项稍后重试" : "可归属资源 · 含常驻内存与缓存", EPD_DRAW_ALIGN_LEFT, false);
    settings_card(fb, (EpdRect){36, 600, 612, 252}, 22, UI_GRAY_WHITE, 0x70);
    for (int i = 0; i < MM_OWNER_COUNT; ++i) {
        const int y = 630 + i * 40;
        char value[24];
        const size_t bytes = s_memory_snapshot.owner[i].internal_bytes + s_memory_snapshot.owner[i].psram_bytes;
        memory_size_text(value, sizeof(value), bytes);
        ui_text_fixed_vc(fb, 64, y, 20, mm_owner_name((mm_owner_t)i), EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed_vc(fb, 594, y, 20, value, EPD_DRAW_ALIGN_RIGHT, false);
        if (i + 1 < MM_OWNER_COUNT) settings_divider(fb, y + 20, 64, 530);
    }
    ui_draw_button(fb, (EpdRect){36, 882, 612, 76}, "一键清理可重建内存",
        s_memory_snapshot.clean_state == MM_CLEAN_RUNNING || s_memory_snapshot.clean_state == MM_CLEAN_REQUESTED);
    char status[96];
    snprintf(status, sizeof(status), "%s · 第 %u 次", memory_clean_state_text(s_memory_snapshot.clean_state),
             (unsigned)s_memory_snapshot.clean_request_id);
    ui_text_fixed_vc(fb, 342, 990, 20, status, EPD_DRAW_ALIGN_CENTER, false);
    char released[24], released_status[96];
    memory_size_text(released, sizeof(released), s_memory_report.released_internal + s_memory_report.released_psram);
    snprintf(released_status, sizeof(released_status), "可用净增 %s（内部 %u KB · PSRAM %u KB）",
             released, (unsigned)(s_memory_report.released_internal / 1024u),
             (unsigned)(s_memory_report.released_psram / 1024u));
    ui_text_fixed_vc(fb, 342, 1027, 17, released_status, EPD_DRAW_ALIGN_CENTER, false);
    snprintf(status, sizeof(status), "%s · 分配未通过 %u 次",
             s_memory_snapshot.pressure ? "内存紧张，自动清理已启用" : "内存正常，自动监测中",
             (unsigned)s_memory_snapshot.allocation_failures);
    ui_text_fixed_vc(fb, 342, 1070, 17, status, EPD_DRAW_ALIGN_CENTER, false);
}

enum { QUICK_SETTINGS_TOP = 300, QUICK_SETTINGS_ROW_H = 76 };
static void quick_settings_render(uint8_t *fb) {
    static const char *const labels[APP_QUICK_ITEM_COUNT] = {
        "WiFi", "蓝牙", "全屏刷新", "前光", "移动网络", "旋转锁定", "触摸震动", "内存清理", "锁屏"};
    const app_quick_layout_t *layout = app_settings_quick_layout();
    back_header(fb, "快捷栏管理");
    char summary[64];
    snprintf(summary, sizeof(summary), "已选 %u / %u · 最多两排，每排四个", layout->count, APP_QUICK_MAX_VISIBLE);
    ui_text_fixed(fb, 36, 214, 22, summary, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed(fb, 36, 252, 20, s_quick_notice[0] ? s_quick_notice : "点按名称显示或隐藏，右侧箭头调整位置", EPD_DRAW_ALIGN_LEFT, false);
    for (unsigned i = 0; i < APP_QUICK_ITEM_COUNT; ++i) {
        const int cy = QUICK_SETTINGS_TOP + (int)i * QUICK_SETTINGS_ROW_H + QUICK_SETTINGS_ROW_H / 2;
        const bool visible = i < layout->count;
        settings_card(fb, (EpdRect){36, cy - 34, 612, 68}, 14, visible ? 0xe0 : UI_GRAY_WHITE, 0x90);
        ui_draw_round_rect(fb, (EpdRect){62, cy - 14, 28, 28}, 5, UI_GRAY_BLACK);
        if (visible) ui_draw_icon(fb, 76, cy, 24, UI_ICON_CHECK, UI_GRAY_BLACK);
        ui_text_fixed_vc(fb, 112, cy, 26, labels[layout->order[i]], EPD_DRAW_ALIGN_LEFT, false);
        char position[24];
        if (visible) snprintf(position, sizeof(position), "%u-%u", i / 4 + 1, i % 4 + 1);
        else snprintf(position, sizeof(position), "隐藏");
        ui_text_fixed_vc(fb, 426, cy, 20, position, EPD_DRAW_ALIGN_CENTER, false);
        ui_draw_icon(fb, 516, cy, 30, UI_ICON_CHEVRON_UP, visible && i > 0 ? UI_GRAY_BLACK : 0xc0);
        ui_draw_icon(fb, 596, cy, 30, UI_ICON_CHEVRON_DOWN, visible && i + 1 < layout->count ? UI_GRAY_BLACK : 0xc0);
    }
    settings_card(fb, (EpdRect){36, 1006, 612, 64}, 16, UI_GRAY_WHITE, 0x70);
    ui_text_fixed_vc(fb, 342, 1038, 24, "恢复默认", EPD_DRAW_ALIGN_CENTER, false);
}

static app_redraw_t quick_settings_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    int tab = ui_nav_hit(ev->x0, ev->y0);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    if (ev->y0 < 190) {
        s_page = SETTINGS_MAIN;
        settings_prepare_profile();
        return APP_REDRAW_PAGE;
    }
    app_quick_layout_t layout = *app_settings_quick_layout();
    s_quick_notice[0] = 0;
    if (ui_rect_hit((EpdRect){36, 1006, 612, 64}, ev->x0, ev->y0)) {
        layout = (app_quick_layout_t)APP_QUICK_LAYOUT_DEFAULT;
    } else {
        if (ev->x0 < 36 || ev->x0 >= 648 || ev->y0 < QUICK_SETTINGS_TOP ||
            ev->y0 >= QUICK_SETTINGS_TOP + APP_QUICK_ITEM_COUNT * QUICK_SETTINGS_ROW_H)
            return APP_REDRAW_NONE;
        unsigned index = (ev->y0 - QUICK_SETTINGS_TOP) / QUICK_SETTINGS_ROW_H;
        uint8_t item = layout.order[index];
        if (ev->x0 >= 476) {
            int next = (int)index + (ev->x0 < 556 ? -1 : 1);
            if (index >= layout.count || next < 0 || next >= layout.count) return APP_REDRAW_NONE;
            layout.order[index] = layout.order[next];
            layout.order[next] = item;
        } else if (index < layout.count) {
            memmove(&layout.order[index], &layout.order[index + 1], layout.count - index - 1);
            layout.order[--layout.count] = item;
        } else {
            if (layout.count == APP_QUICK_MAX_VISIBLE) {
                snprintf(s_quick_notice, sizeof(s_quick_notice), "最多显示八个按钮，请先隐藏一个");
                return APP_REDRAW_PAGE;
            }
            memmove(&layout.order[layout.count + 1], &layout.order[layout.count], index - layout.count);
            layout.order[layout.count++] = item;
        }
    }
    if (!app_settings_set_quick_layout(&layout))
        snprintf(s_quick_notice, sizeof(s_quick_notice), "保存失败，请重试");
    return APP_REDRAW_PAGE;
}

static const struct {
    device_power_charge_current_t current;
    const char *name;
    const char *rating;
} s_charge_choices[] = {
    {DEVICE_POWER_CHARGE_1A, "日常充电", "1 A · 默认"},
    {DEVICE_POWER_CHARGE_1_5A, "快速充电", "1.5 A"},
};

static EpdRect charge_choice_rect(unsigned index) {
    return (EpdRect){36, 394 + (int)index * 158, 612, 136};
}

static const char *charge_current_label(unsigned current) {
    for (unsigned i = 0; i < sizeof(s_charge_choices) / sizeof(s_charge_choices[0]); ++i) {
        if (current == s_charge_choices[i].current) return s_charge_choices[i].name;
    }
    return "暂不可用";
}

static void charge_current_render(uint8_t *fb) {
    back_header(fb, "充电档位");
    const unsigned current = device_power_charge_current();

    // 状态区只标档位，不模拟电量或充电进度。/ The status block shows the profile, not charge progress.
    ui_fill_round_rect(fb, (EpdRect){36, 212, 612, 154}, 20, UI_GRAY_BLACK);
    ui_text_box(fb, (EpdRect){64, 231, 440, 38}, 23, "当前档位", EPD_DRAW_ALIGN_LEFT, true);
    ui_text_box(fb, (EpdRect){64, 268, 440, 83}, 48, charge_current_label(current), EPD_DRAW_ALIGN_LEFT, true);
    epd_draw_circle(574, 289, 38, UI_GRAY_WHITE, fb);
    ui_draw_icon(fb, 574, 289, 43, UI_ICON_ZAP, UI_GRAY_WHITE);

    for (unsigned i = 0; i < sizeof(s_charge_choices) / sizeof(s_charge_choices[0]); ++i) {
        const EpdRect box = charge_choice_rect(i);
        const bool active = current == s_charge_choices[i].current;
        settings_card(fb, box, 20, UI_GRAY_WHITE, UI_GRAY_BLACK);
        ui_text_box(fb, (EpdRect){64, box.y + 16, 475, 52}, 34, s_charge_choices[i].name,
                    EPD_DRAW_ALIGN_LEFT, false);
        ui_text_box(fb, (EpdRect){64, box.y + 76, 552, 42}, 22, s_charge_choices[i].rating,
                    EPD_DRAW_ALIGN_LEFT, false);
        if (active) {
            epd_fill_circle(600, box.y + 43, 19, UI_GRAY_BLACK, fb);
            ui_draw_icon(fb, 600, box.y + 43, 24, UI_ICON_CHECK, UI_GRAY_WHITE);
        } else {
            epd_draw_circle(600, box.y + 43, 18, current ? UI_GRAY_BLACK : UI_GRAY_LIGHT, fb);
        }
    }

    settings_divider(fb, 737, 36, 612);
    const char *notice = !current ? "重启设备后再试" :
        s_notice[0] ? s_notice : "点按卡片切换，重启后保留";
    ui_text_box(fb, (EpdRect){36, 759, 612, 53}, 27, notice, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){36, 829, 612, 49}, 23, "实际速度受充电器、线材和电量影响。",
                EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 3);
}

static app_redraw_t charge_current_touch(int x, int y) {
    const unsigned current = device_power_charge_current();
    if (!current) return APP_REDRAW_NONE;
    for (unsigned i = 0; i < sizeof(s_charge_choices) / sizeof(s_charge_choices[0]); ++i) {
        if (!ui_rect_hit(charge_choice_rect(i), x, y)) continue;
        if (current == s_charge_choices[i].current) return APP_REDRAW_NONE;
        const esp_err_t err = device_power_set_charge_current(s_charge_choices[i].current);
        snprintf(s_notice, sizeof(s_notice), "%s", err == ESP_OK ? "" : "未能切换，请重试");
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static void batt_calibration_render(uint8_t *fb) {
    device_power_batt_calibration_status_t status;
    device_power_get_batt_calibration(&status);
    back_header(fb, "自动电量校准(beta)");
    setting_toggle(fb, 272, "自动电量校准(beta)", "按本机放电学习", status.enabled);
    section(fb, 438, "学习状态");
    ui_text_box(fb, (EpdRect){48, 482, 588, 42}, 28, status.applied ? "已应用部分校准" :
            !status.enabled ? "学习已停止" : status.sampled_bins ? "正在学习" : "等待有效样本", EPD_DRAW_ALIGN_LEFT, false);
    char coverage[96];
    snprintf(coverage, sizeof(coverage), "已有样本 %u / %u 个区间，可信 %u 个",
             (unsigned)status.sampled_bins, (unsigned)status.total_bins, (unsigned)status.trusted_bins);
    ui_text_box(fb, (EpdRect){48, 530, 588, 40}, 23, coverage, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){48, 578, 588, 40}, 23, "正常使用即可，学习需要多次有效放电", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_box(fb, (EpdRect){48, 620, 588, 40}, 23, "未覆盖区间继续使用系统估算", EPD_DRAW_ALIGN_LEFT, false);
    if (!status.enabled)
        ui_text_box(fb, (EpdRect){48, 662, 588, 40}, 23, "停止学习，已有校准继续生效", EPD_DRAW_ALIGN_LEFT, false);
    ui_draw_button(fb, (EpdRect){36, 706, 612, 78}, s_batt_calibration_confirm ?
                   "确认重置校准" : "重置校准", false);
    if (s_notice[0] || status.save_error)
        ui_text_box(fb, (EpdRect){48, 836, 588, 52}, 22, status.save_error ? "校准保存失败，请重试" : s_notice,
                EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 3);
    return;
}

static app_redraw_t batt_calibration_touch(int x, int y) {
    if (ui_rect_hit((EpdRect){36, 272, 612, 126}, x, y)) {
        device_power_batt_calibration_status_t status;
        device_power_get_batt_calibration(&status);
        const esp_err_t err = device_power_set_batt_calibration(!status.enabled);
        s_batt_calibration_confirm = false;
        snprintf(s_notice, sizeof(s_notice), "%s", err == ESP_OK ?
                 (status.enabled ? "停止学习，已有校准继续生效" : "已开启，等待有效放电样本") : "开关保存失败，请重试");
        device_power_get_batt_calibration(&s_batt_calibration_status);
        return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit((EpdRect){36, 706, 612, 78}, x, y)) {
        if (!s_batt_calibration_confirm) {
            s_batt_calibration_confirm = true;
            s_notice[0] = 0;
        } else {
            const esp_err_t err = device_power_reset_batt_calibration();
            s_batt_calibration_confirm = false;
            snprintf(s_notice, sizeof(s_notice), "%s", err == ESP_OK ?
                     "已恢复系统曲线，学习开关保持不变" : "重置未完整保存，请重试");
            device_power_get_batt_calibration(&s_batt_calibration_status);
        }
        return APP_REDRAW_PAGE;
    }
    if (s_batt_calibration_confirm) {
        s_batt_calibration_confirm = false;
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    if (s_page == SETTINGS_PIN_ENTRY) { ui_pinpad_paint(fb, &s_pinpad, ui_pinpad_full()); return; }
    ui_clear_page(fb);

    // 保留 ui_clear_page 的白底，灰阶只由头像、卡片与字形生成。
    // Keep ui_clear_page's white background; avatars, cards and glyphs alone generate grays.
    ui_nav_status(fb);
    if (s_page == SETTINGS_QUICK_BAR) {
        quick_settings_render(fb);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_MEMORY) {
        back_header(fb, "内存管理");
        memory_render_content(fb);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_LOCK_PIN) {
        back_header(fb, "锁屏密码");
        row(fb, 266, "四位数字密码", !lock_pin_available() ? "暂不可用" : lock_pin_enabled() ? "已开启" : "已关闭");
        if (!lock_pin_available()) ui_text_vc(fb, 342, 408, 24, "锁屏设置暂不可用", EPD_DRAW_ALIGN_CENTER, false);
        else if (lock_pin_enabled()) {
            row(fb, 406, "修改密码", "验证原密码  ›");
            row(fb, 512, "关闭密码", "验证原密码  ›");
        } else row(fb, 406, "开启密码", "设置四位密码  ›");
        if (s_notice[0]) ui_text_vc(fb, 342, 680, 23, s_notice, EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 752, 21, "修改和关闭密码需验证原密码", EPD_DRAW_ALIGN_CENTER, false);
        ui_nav_draw(fb, 3); return;
    }
    if (s_page == SETTINGS_PROFILE) {
        back_header(fb, "个人资料");
        ui_text(fb, 42, 214, 21, "自定义名称与头像", EPD_DRAW_ALIGN_LEFT, false);
        row(fb, 266, "设备名称", app_settings_device_name());
        row(fb, 366, "更换头像", app_settings_avatar_path()[0] ? "已选择图片  ›" : "默认图标  ›");
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_AVATAR) {
        back_header(fb, "选择头像");
        section(fb, 232, "TF 卡 pictures 文件夹中的 JPG / PNG 图片");
        row(fb, 270, "使用默认图标", app_settings_avatar_path()[0] ? "选择  ›" : "当前  ✓");
        if (!s_wallpaper_count) ui_text(fb, 52, 384, 23, "未找到图片，请放入 pictures 文件夹", EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 0; i < 7; ++i) {
            int index = s_wallpaper_page * 7 + i;
            if (index >= s_wallpaper_count) break;
            EpdRect box = {36, 374 + i * 96, 612, 80};
            bool active = !strcmp(s_wallpapers[index].path, app_settings_avatar_path());
            settings_card(fb, box, 17, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[index].name);
            fit_value(name, 490);
            ui_text_vc(fb, 60, box.y + 40, 24, name, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(611, box.y + 40, 7, UI_GRAY_BLACK, fb);
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_TEXT_EDIT) {
        back_header(fb, s_editor_signature ? "状态栏签名" : "设备名称");
        ui_text(fb, 642, 95, 24, "完成", EPD_DRAW_ALIGN_RIGHT, false);
        ui_text(fb, 36, 201, 21, s_editor_signature ? "状态栏中间显示，留空则隐藏" : "显示在设置页的资料卡", EPD_DRAW_ALIGN_LEFT, false);
        ui_text_input_draw(fb, &s_editor_input, (EpdRect){36, 243, 612, 82}, 28, false, NULL);
        if (s_editor_notice[0]) ui_text_fixed_vc(fb, 36, 510, 22, s_editor_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_keyboard_draw(fb, 560);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_SHELF_STYLE) {
        ui_nav_back(fb, 36, 79);
        ui_text_vc(fb, 342, 107, 34, "书架样式", EPD_DRAW_ALIGN_CENTER, false);
        ui_text(fb, 36, 207, 23, "选择书架外观与排列方式", EPD_DRAW_ALIGN_LEFT, false);
        if (s_notice[0]) ui_text_vc(fb, 36, 169, 21, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 0; i < SHELF_STYLE_CARD_COUNT; ++i) {
            EpdRect card = shelf_style_card(i);
            const uint8_t style = shelf_style_id(i);
            const bool active = app_settings_shelf_style() == style;
            const bool titles = app_settings_shelf_show_titles(style);
            const int y = card.y;
            settings_card(fb, card, 24, active ? 0xd0 : UI_GRAY_WHITE, active ? 0x58 : 0x70);
            if (i >= 2) { draw_compact_shelf_style(fb, card, style, active); continue; }
            draw_style_thumbnail(fb, style, y + 11, titles);
            int title_px = ui_text_effective_px(23), detail_px = ui_text_effective_px(17);
            if (title_px > 30) title_px = 30;
            if (detail_px > 22) detail_px = 22;
            ui_text_fixed_vc(fb, 68, y + 207, title_px, shelf_style_name(style), EPD_DRAW_ALIGN_LEFT, false);
            ui_text_fixed_vc(fb, 310, y + 207, detail_px, "每页 9 本", EPD_DRAW_ALIGN_LEFT, false);
            epd_draw_circle(609, y + 207, 13, UI_GRAY_BLACK, fb);
            if (active) epd_fill_circle(609, y + 207, 6, UI_GRAY_BLACK, fb);
            if (style != 5) {
                EpdRect row = shelf_style_titles_row(card);
                settings_divider(fb, row.y, 68, 548);
                int switch_px = ui_text_effective_px(22);
                if (switch_px > 32) switch_px = 32;
                ui_text_fixed_vc(fb, 68, row.y + row.height / 2, switch_px, "显示书名", EPD_DRAW_ALIGN_LEFT, false);
                setting_toggle_track(fb, (EpdRect){546, row.y + 5, 69, 39}, titles);
            }
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_TIME_EDIT) {
        ui_nav_back(fb, 36, 79);
        ui_text(fb, 36, 151, 47, "日期与时间", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 36, 216, 23, "北京时间 · UTC+8", EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb, 259, 36, 612, UI_GRAY_LIGHT);
        static const char *const labels[] = {"年", "月", "日", "时", "分"};
        int values[] = {s_year, s_month, s_day, s_hour, s_minute};
        for (int i = 0; i < 5; ++i) {
            int x = 36 + i * 122;
            ui_text(fb, x + 52, 300, 24, labels[i], EPD_DRAW_ALIGN_CENTER, false);
            ui_draw_control_frame(fb, (EpdRect){x, 354, 106, 62}, 8, UI_GRAY_BLACK);
            ui_text_vc(fb, x + 53, 385, 30, "+", EPD_DRAW_ALIGN_CENTER, false);
            char value[8]; snprintf(value, sizeof(value), i == 0 ? "%04d" : "%02d", values[i]);
            ui_text(fb, x + 53, 456, i == 0 ? 34 : 39, value, EPD_DRAW_ALIGN_CENTER, false);
            ui_draw_control_frame(fb, (EpdRect){x, 535, 106, 62}, 8, UI_GRAY_BLACK);
            ui_text_vc(fb, x + 53, 566, 30, "−", EPD_DRAW_ALIGN_CENTER, false);
        }
        ui_text(fb, 36, 649, 23, "点上方 + 或下方 − 调整数字", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){36, 767, 612, 78}, "保存时间", false);
        ui_draw_button(fb, (EpdRect){36, 874, 612, 78}, "取消", false);
        if (s_notice[0]) ui_text(fb, 36, 996, 22, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        return;
    }
    if (s_page == SETTINGS_WIFI) {
        back_header(fb, "WiFi");
        section(fb, 253, "已保存网络");
        row(fb, 296, s_main_wifi_saved ? s_main_wifi : "尚未配置", s_main_wifi_saved ? "已保存" : "");
        row(fb, 408, "连接或更改网络", "前往传输  ›");
        ui_text(fb, 54, 529, 21, "配网后可在日期与时间中进行网络对时。", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_NETWORK) {
        network_status_t network;
        network_service_get_status(&network);
        back_header(fb, "移动网络");
        section(fb, 232, "移动数据");
        if (network.cellular_capable) {
            setting_toggle(fb, NETWORK_TOGGLE_Y, "4G 移动网络", settings_cellular_status(&network),
                           network.cellular_enabled);
            section(fb, NETWORK_ROUTE_SECTION_Y, "业务联网方式");
            row(fb, NETWORK_ROUTE_ROW_Y, "日期、天气、OTA", network_route_name(network.route));
            char detail[96];
            char signal[12];
            if (network.cellular_signal == 99) snprintf(signal, sizeof(signal), "未知");
            else snprintf(signal, sizeof(signal), "%d/31", network.cellular_signal);
            snprintf(detail, sizeof(detail), "4G：%s · 信号 %s", settings_cellular_status(&network), signal);
            ui_text(fb, 48, 602, 21, detail, EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 48, 648, 21, s_network_notice ? s_network_notice :
                    "移动网络使用内置 SIM 卡；联网方式单独选择。", EPD_DRAW_ALIGN_LEFT, false);
        } else {
            ui_text(fb, 48, 340, 21, network.cellular_capability_known
                    ? "当前硬件未检测到移动网络模块。"
                    : "正在检测移动网络硬件，请稍候。",
                    EPD_DRAW_ALIGN_LEFT, false);
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_SYSTEM_FONT) {
        back_header(fb, "系统字体");
        system_font_layout_t l = system_font_layout();
        settings_card(fb,l.size,22,UI_GRAY_WHITE,0x70);
        char size[16]; snprintf(size,sizeof(size),"%u%%",app_settings_system_font_size());
        if (settings_stacked()) {
            ui_text_vc(fb,60,247,27,"系统字号",EPD_DRAW_ALIGN_LEFT,false);
            ui_text_vc(fb,589,247,27,size,EPD_DRAW_ALIGN_RIGHT,false);
            ui_text_vc(fb,60,328,18,"轻点切换，最大 200%",EPD_DRAW_ALIGN_LEFT,false);
        } else {
            ui_text_vc(fb,58,227,25,"系统字号",EPD_DRAW_ALIGN_LEFT,false);
            ui_text_vc(fb,58,269,18,"轻点切换，最大 200%",EPD_DRAW_ALIGN_LEFT,false);
            ui_text_vc(fb,589,247,26,size,EPD_DRAW_ALIGN_RIGHT,false);
        }
        ui_draw_icon(fb,619,247,28,UI_ICON_ROTATE_CW,0x38);
        section(fb,l.preview_title,"效果预览");
        settings_card(fb,l.preview,22,UI_GRAY_WHITE,0x70);
        ui_text_vc(fb,60,l.preview.y+(settings_stacked()?68:51),29,"阅读，让时间慢下来",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,60,l.preview.y+(settings_stacked()?151:107),25,"Aa 123",EPD_DRAW_ALIGN_LEFT,false);
        section(fb,l.font_title,"选择字体");
        const char *selected = app_settings_system_font_path();
        int count = ttf_font_count(), first = s_font_page*l.slots;
        for (int slot=0;slot<l.slots && first+slot<=count;++slot) {
            int index=first+slot;
            const ttf_font_item_t *item=index?ttf_font_item(index-1):NULL;
            bool active=item?!strcmp(selected,item->path):!selected[0];
            EpdRect box=system_font_item_rect(slot);
            settings_card(fb,box,20,active?0xe0:UI_GRAY_WHITE,0x70);
            char label[TTF_FONT_NAME_MAX];
            snprintf(label,sizeof(label),"%s",item?system_font_label(item->path):"思源黑体（内建）");
            settings_fit_text(label,26,box.width-100);
            ui_text_vc(fb,60,box.y+box.height/2,26,label,EPD_DRAW_ALIGN_LEFT,false);
            if (active) epd_fill_circle(613,box.y+box.height/2,6,UI_GRAY_BLACK,fb);
        }
        char pages[24];snprintf(pages,sizeof(pages),"%d / %d",s_font_page+1,(count+l.slots)/l.slots);
        ui_text_fixed_vc(fb,648,1080,18,pages,EPD_DRAW_ALIGN_RIGHT,false);
        if (s_notice[0]) ui_text_fixed_vc(fb,42,1080,18,s_notice,EPD_DRAW_ALIGN_LEFT,false);
        ui_nav_draw(fb,3);
        if (s_font_sync_confirm) draw_system_font_sync(fb);
        return;
    }
    if (s_page == SETTINGS_SYSTEM_SIZE) {
        back_header(fb, "系统字号");
        section(fb, 248, "界面文字大小");
        for (int i = 0; i < 11; ++i) {
            int value = 100 + i * 10;
            EpdRect box = {36 + (i % 2) * 316, 298 + (i / 2) * 122, 296, 102};
            bool active = app_settings_system_font_size() == value;
            settings_card(fb, box, 20, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            char label[48]; snprintf(label, sizeof(label), "%d%%", value);
            ui_text_vc(fb, box.x + 24, box.y + 51, 29, label, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(box.x + box.width - 25, box.y + 51, 8, UI_GRAY_BLACK, fb);
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_MAIN_REFRESH) {
        back_header(fb, "主页刷新模式");
        section(fb, 248, "首页 · 书架 · 文件管理 · 设置");
        const char* names[] = {"普通", "水波纹"};
        const char* details[] = {"保留灰阶层次，兼顾画面清晰度", "以水波纹切换页面，保留灰阶"};
        for (int i = 0; i < MAIN_REFRESH_CHOICE_COUNT; ++i) {
            const EpdRect box = {36, 298 + i * 158, 612, 132};
            const bool active = app_settings_main_refresh_mode() == s_main_refresh_choices[i];
            settings_card(fb, box, 22, active ? 0xd0 : UI_GRAY_WHITE, 0x58);
            ui_text_vc(fb, 64, box.y + 38, 30, names[i], EPD_DRAW_ALIGN_LEFT, false);
            char detail[128]; snprintf(detail, sizeof(detail), "%s", details[i]); fit_value(detail, 498);
            ui_text_vc(fb, 64, box.y + 89, 22, detail, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(607, box.y + 38, 8, UI_GRAY_BLACK, fb);
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_SYSTEM_CONTRAST) {
        back_header(fb, "系统对比度");
        section(fb, 248, "调整系统界面的灰阶层次与清晰度");
        static const char *const names[] = {"柔和", "标准", "清晰", "鲜明", "高对比"};
        for (int i = 0; i < 5; ++i) {
            int value = 100 + i * 10;
            EpdRect box = {36, 298 + i * 116, 612, 90};
            bool active = app_settings_system_contrast() == value;
            settings_card(fb, box, 20, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text_vc(fb, 64, box.y + 45, 28, names[i], EPD_DRAW_ALIGN_LEFT, false);
            char shown[16]; snprintf(shown, sizeof(shown), "%d%%", value);
            ui_text_vc(fb, 570, box.y + 45, 22, shown, EPD_DRAW_ALIGN_RIGHT, false);
            if (active) epd_fill_circle(605, box.y + 45, 8, UI_GRAY_BLACK, fb);
        }
        ui_nav_draw(fb, 3);
        return;
    }

    if (s_page == SETTINGS_LOCK_STYLE) {
        ui_lock_style_render(fb);
        return;
    }

    if (s_page == SETTINGS_READING_BG) {
        back_header(fb, "阅读背景");
        section(fb, 236, "选择图片作为正文底图");
        char current[128];
        const char *current_path = app_settings_book_background_path();
        const char *current_name = current_path[0] ? strrchr(current_path, '/') : NULL;
        snprintf(current, sizeof(current), "当前：%s", current_path[0] ? (current_name ? current_name + 1 : current_path) : "白底");
        fit_value(current, 360);
        ui_text_vc(fb, 48, 280, 20, current, EPD_DRAW_ALIGN_LEFT, false);
        char count_text[48];
        snprintf(count_text, sizeof(count_text), "共 %d 张", s_wallpaper_count);
        ui_text_vc(fb, 636, 280, 20, count_text, EPD_DRAW_ALIGN_RIGHT, false);
        // 两列卡片展示缩略图、文件名和当前状态，避免把图片选择压成单调文件名列表。
        // Two card columns show thumbnails, filenames and state instead of a flat filename list.
        for (int slot = 0; slot < READING_BG_THUMB_SLOTS; ++slot) {
            int index = s_wallpaper_page * READING_BG_THUMB_SLOTS + slot;
            if (index >= s_wallpaper_count) break;
            EpdRect box = reading_bg_card_rect(slot);
            EpdRect thumb = reading_bg_thumbnail_rect(slot);
            bool active = !strcmp(s_wallpapers[index].path, current_path);
            settings_card(fb, box, 20, active ? 0xc8 : 0xf0, active ? 0x30 : 0x88);
            ui_fill_round_rect(fb, thumb, 10, UI_GRAY_WHITE);
            reading_bg_thumbnail_draw(fb, thumb, reading_bg_thumbnail_at(slot));
            ui_draw_control_frame(fb, thumb, 10, active ? UI_GRAY_BLACK : 0x88);
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[index].name);
            fit_value(name, 126);
            ui_text_box(fb, (EpdRect){box.x + 156, box.y + 22, 128, 60}, 21, name, EPD_DRAW_ALIGN_LEFT, false);
            char number[32]; snprintf(number, sizeof(number), "第 %d 张", index + 1);
            ui_text_box(fb, (EpdRect){box.x + 156, box.y + 88, 128, 38}, 18, number, EPD_DRAW_ALIGN_LEFT, false);
            if (active) {
                ui_fill_round_rect(fb, (EpdRect){box.x + 156, box.y + 154, 128, 36}, 12, UI_GRAY_BLACK);
                ui_text_box(fb, (EpdRect){box.x + 156, box.y + 154, 128, 36}, 18, "当前使用", EPD_DRAW_ALIGN_CENTER, true);
            }
        }
        if (!s_wallpaper_count) {
            settings_card(fb, (EpdRect){36, READING_BG_CARD_TOP, 612, 210}, 22, 0xf0, 0x88);
            ui_text_vc(fb, 342, READING_BG_CARD_TOP + 84, 24, "未找到图片", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 342, READING_BG_CARD_TOP + 134, 20, "请放入 TF 卡的 pictures 文件夹", EPD_DRAW_ALIGN_CENTER, false);
        }
        ui_draw_button(fb, reading_bg_restore_rect(), "恢复白底", false);
        int pages = s_wallpaper_count > 0 ? (s_wallpaper_count + READING_BG_THUMB_SLOTS - 1) / READING_BG_THUMB_SLOTS : 1;
        char page_text[64]; snprintf(page_text, sizeof(page_text), "%d / %d 页 · 上下滑动翻页", s_wallpaper_page + 1, pages);
        ui_text_box(fb, (EpdRect){300,966,348,60}, 19, page_text, EPD_DRAW_ALIGN_RIGHT, false);
        if (s_reading_bg_notice[0])
            ui_text_box(fb, (EpdRect){36,1036,612,54}, 19, s_reading_bg_notice, EPD_DRAW_ALIGN_CENTER, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_READING_BG_PREVIEW) {
        back_header(fb, "预览阅读背景");
        EpdRect image = {166, 194, 352, 626};
        ui_fill_round_rect(fb, image, 8, UI_GRAY_WHITE);
        if (s_reading_bg_ready) ui_reader_background_paint(fb, image, false);
        ui_draw_round_rect(fb, image, 8, 0x70);
        if (!s_reading_bg_ready)
            ui_text_vc(fb, 342, 507, 23, s_reading_bg_pending ? "正在加载预览…" :
                (s_reading_bg_notice[0] ? s_reading_bg_notice : "正在加载预览…"),
                EPD_DRAW_ALIGN_CENTER, false);
        if (s_wallpaper_selected >= 0 && s_wallpaper_selected < s_wallpaper_count) {
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[s_wallpaper_selected].name);
            fit_value(name, 570);
            ui_text_vc(fb, 342, 859, 24, name, EPD_DRAW_ALIGN_CENTER, false);
            char position[40];
            snprintf(position, sizeof(position), "%d / %d · 上下滑动切换", s_wallpaper_selected + 1, s_wallpaper_count);
            ui_text_vc(fb, 342, 908, 20, position, EPD_DRAW_ALIGN_CENTER, false);
        }
        if (s_reading_bg_ready) {
            EpdRect button = {94, 962, 496, 70};
            ui_fill_round_rect(fb, button, 22, 0x30);
            ui_text_vc(fb, 342, 997, 25, "使用阅读背景", EPD_DRAW_ALIGN_CENTER, true);
        }
        if (s_reading_bg_confirm) {
            EpdRect dialog = {68, 435, 548, 306};
            ui_fill_round_rect(fb, dialog, 26, UI_GRAY_WHITE);
            ui_draw_control_frame(fb, dialog, 26, 0x50);
            ui_text_vc(fb, 342, 503, 31, "使用阅读背景？", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 342, 565, 20, "确认后，阅读正文以它为底，文字自动加深", EPD_DRAW_ALIGN_CENTER, false);
            EpdRect cancel = {94, 643, 232, 68}, confirm = {358, 643, 232, 68};
            ui_fill_round_rect(fb, cancel, 18, 0xe0);
            ui_fill_round_rect(fb, confirm, 18, 0x30);
            ui_text_vc(fb, 210, 677, 24, "取消", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 474, 677, 24, "确认", EPD_DRAW_ALIGN_CENTER, true);
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_TIME) {
        back_header(fb, "日期与时间");
        time_t now = time(NULL);
        char shown[64] = "未设置";
        if (now >= 1704067200) {
            struct tm tm; time_t local = now + 8 * 3600;
            if (gmtime_r(&local, &tm)) snprintf(shown, sizeof(shown), "%04d-%02d-%02d  %02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        }
        section(fb, 253, "当前时间 · 北京时间");
        row(fb, TIME_ROW_CURRENT_Y, shown, "");
        row(fb, TIME_ROW_AUTO_Y, "自动对时", app_settings_auto_time_sync() ? "开  ›" : "关  ›");
        row(fb, TIME_ROW_WIFI_Y, "通过网络对时", "立即同步  ›");
        row(fb, TIME_ROW_MANUAL_Y, "手动设置", "编辑  ›");
        char line[96];
        time_last_sync_text(line, sizeof(line));
        ui_text(fb, 54, TIME_LAST_LINE_Y, 21, line, EPD_DRAW_ALIGN_LEFT, false);
        time_auto_hint(line, sizeof(line));
        ui_text(fb, 54, TIME_HINT_LINE_Y, 21, line, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 54, TIME_ADVICE_LINE_Y, 21, "长时间离线可能产生误差，建议定期重新对时。", EPD_DRAW_ALIGN_LEFT, false);
        if (s_sync_pending || s_sync_waiting || s_notice[0])
            ui_text(fb, 54, TIME_NOTICE_LINE_Y, 22, s_sync_pending || s_sync_waiting ? "正在获取网络时间…" : s_notice,
                    EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_AUTO_LOCK) {
        back_header(fb, "自动休眠锁屏");
        section(fb, 228, "无操作后自动锁屏");
        const char *labels[] = {"1 分钟", "5 分钟", "10 分钟", "关闭"};
        const uint8_t values[] = {1, 5, 10, 0};
        for (int i = 0; i < 4; ++i) {
            EpdRect box = {36, 272 + i * 112, 612, 92};
            bool active = app_settings_auto_lock_minutes() == values[i];
            settings_card(fb, box, 20, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text_vc(fb, 64, box.y + box.height / 2, 30, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + box.height / 2, 8, UI_GRAY_BLACK, fb);
        }
        ui_text(fb, 48, 760, 23, "操作后重新计时，传输和升级期间暂停", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 810, 23, "待机方式可选仅浅睡、定时关机或立即关机", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_HAPTIC) {
        back_header(fb, "触摸震动");
        section(fb, 228, "单指按下时短震一次");
        setting_toggle(fb, 272, "触摸震动", "点击屏幕时震一下", app_settings_touch_haptics());
        section(fb, 438, "震动强度");
        const char *labels[] = {"弱", "中", "强"};
        const char *details[] = {"最轻，默认", "适中", "最明显"};
        for (int i = 0; i < 3; ++i) {
            EpdRect box = {36, 482 + i * 112, 612, 92};
            bool active = app_settings_haptic_intensity() == (uint8_t)i;
            settings_card(fb, box, 20, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text_vc(fb, 64, box.y + box.height / 2, 30, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            ui_text_vc(fb, 132, box.y + box.height / 2, 21, details[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + box.height / 2, 8, UI_GRAY_BLACK, fb);
        }
        ui_text(fb, 48, 830, 23, "选中后立即震一下，可反复比较", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 880, 23, "关闭后不再震，档位仍保留", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_BATT_CALIBRATION) { batt_calibration_render(fb); return; }
    if (s_page == SETTINGS_CHARGE_CURRENT) { charge_current_render(fb); return; }
    if (s_page == SETTINGS_POWER_LOG) {
        back_header(fb, "日志调试");
        section(fb, 228, "电量校准日志");
        setting_toggle(fb, 272, "写入 batt.log", "每分钟记录电压、电流与充电状态",
                       app_settings_batt_log());
        ui_text(fb, 48, 440, 23, "写入 TF 卡根目录 batt.log，同时输出到串口", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 485, 23, "卡不在或正在 USB 读卡时自动跳过", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 530, 23, "关闭后不再创建文件，也不写日志", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 575, 23, "默认关闭；开关立即生效", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_POWER_SLEEP) {
        back_header(fb, "待机方式");
        section(fb, 248, "选择锁屏后的待机方式");
        bool timed = app_settings_shutdown_minutes() != 0;
        const char *labels[] = {"彻底关机", "仅浅睡", "浅睡后关机"};
        const char *details[] = {"锁屏即彻底断电；画面保留，长按电源键约 1 秒开机",
                                 "锁屏画面保留；不断电，按键返回关机前页面",
                                 "锁屏后开始计时；提前唤醒取消本轮计时"};
        for (int i = 0; i < 3; ++i) {
            EpdRect box = standby_mode_rect((unsigned)i, timed);
            bool active = i == 2 ? timed :
                app_settings_shutdown_style() == (app_shutdown_style_t)(APP_SHUTDOWN_STAGED + i);
            settings_card(fb, box, 22, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text(fb, 64, box.y + 23, 29, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 64, box.y + 79, 20, details[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + 66, 8, UI_GRAY_BLACK, fb);
        }
        if (timed) {
            const char *durations[] = {"10 分钟", "30 分钟", "60 分钟"};
            for (unsigned i = 0; i < 3; ++i) {
                bool active = app_settings_shutdown_style() == (app_shutdown_style_t)(APP_SHUTDOWN_LIGHT_10 + i);
                ui_draw_button(fb, standby_duration_rect(i), durations[i], active);
            }
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_BOOT) {
        back_header(fb, "电脑刷机");
        settings_card(fb, (EpdRect){36, 254, 612, 298}, 22, UI_GRAY_WHITE, 0x70);
        ui_text(fb, 60, 288, 29, "进入 BOOT 模式", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 350, 22, "用于官网完整刷机或故障恢复", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){60, 438, 564, 78}, s_boot_pending ? "正在进入" : "进入 BOOT 模式", false);
        ui_text(fb, 48, 620, 22, "请用 USB 连接电脑，再打开官网刷机页", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3); return;
    }
    if (s_page == SETTINGS_UPGRADE) {
        back_header(fb, "系统升级");
        settings_card(fb, (EpdRect){36, 202, 612, 120}, 22, UI_GRAY_WHITE, 0x70);
        ui_text(fb, 60, 224, 22, "当前版本", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 264, 29, esp_app_get_description()->version, EPD_DRAW_ALIGN_LEFT, false);
        if (s_upgrade_confirm) {
            settings_card(fb, (EpdRect){36, 356, 612, s_upgrade_online ? 574 : 344}, 22, UI_GRAY_WHITE, 0x70);
            ui_text_fixed(fb, 60, 383, 31, s_upgrade_online ? "发现新版本" : "确认 TF 卡升级", EPD_DRAW_ALIGN_LEFT, false);
            ui_text_fixed(fb, 60, 447, 28, s_upgrade_online ? s_update.release.version : s_local_update.candidate_version,
                    EPD_DRAW_ALIGN_LEFT, false);
            if (s_upgrade_online) {
                ui_text_fixed(fb, 60, 494, 26, "本次更新", EPD_DRAW_ALIGN_LEFT, false);
                draw_upgrade_notes(fb);
            }
            ui_text_fixed(fb, 60, s_upgrade_online ? 770 : 513, 24, "升级期间请保持供电", EPD_DRAW_ALIGN_LEFT, false);
            ui_text_fixed(fb, 60, s_upgrade_online ? 806 : 560, 23, "设置、阅读记录与 TF 卡文件保留", EPD_DRAW_ALIGN_LEFT, false);
            ui_draw_button(fb, upgrade_confirm_button(s_upgrade_online, false), s_upgrade_online ? "稍后" : "取消", false);
            ui_draw_button(fb, upgrade_confirm_button(s_upgrade_online, true), s_upgrade_online ? "开始更新" : "确认升级", true);
        } else {
            settings_card(fb, (EpdRect){36, 356, 612, 278}, 22, UI_GRAY_WHITE, 0x70);
            ui_text(fb, 60, 383, 29, "联网 OTA", EPD_DRAW_ALIGN_LEFT, false);
            char detail[96];
            snprintf(detail, sizeof(detail), "%s", s_update.message[0] ? s_update.message : "连接网络，检查官网发布的新版本");
            fit_value(detail, 540);
            ui_text(fb, 60, 436, 21, detail, EPD_DRAW_ALIGN_LEFT, false);
            if (s_update.state == PICO_UPDATE_AVAILABLE)
                ui_text(fb, 60, 477, 24, s_update.release.version, EPD_DRAW_ALIGN_LEFT, false);
            if (s_update.state == PICO_UPDATE_DOWNLOADING) draw_upgrade_progress(fb);
            ui_draw_button(fb, (EpdRect){60, 550, 564, 62}, s_update.busy ? "取消联网更新" :
                s_update.state == PICO_UPDATE_AVAILABLE ? "开始更新" : "检查更新", s_update.state == PICO_UPDATE_AVAILABLE);
            setting_toggle(fb, 662, "自动检查 OTA 更新", "每 12 小时检查一次", app_settings_auto_ota());
        }
        if (s_upgrade_notice[0]) ui_text(fb, 48, 957, 21, s_upgrade_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 1010, 20, "首次安装或修复，请通过官网完整刷机", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_CONFIG) {
        back_header(fb, "保存与恢复配置");
        section(fb, 248, "换机或刷机后，快速恢复个性化设置");
        settings_card(fb, (EpdRect){36, 299, 612, 197}, 22, UI_GRAY_WHITE, 0x70);
        ui_text(fb, 60, 326, 26, "TF 卡根目录", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 377, 23, "配置备份文件", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 435, 19, "文字排版、设备资料、阅读记录与书签", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){36, 561, 612, 83}, "保存当前配置到 TF 卡", false);
        ui_draw_button(fb, (EpdRect){36, 681, 612, 83},
                       s_config_confirm ? "再次点按，确认恢复配置" : "从 TF 卡恢复配置", false);
        if (s_notice[0]) ui_text(fb, 48, 819, 21, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 899, 19, "包含阅读记录、用户偏好和全部已存 WiFi。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 939, 19, "书籍、字体和图片仍需留在 TF 卡。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 979, 19, "备份含明文 WiFi 密码，请妥善保管。", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_BLUETOOTH) { settings_bluetooth_render(fb); return; }
    const int profile_y=180-s_main_scroll, profile_h=settings_profile_height();
    if (profile_y+profile_h>160) {
        settings_card(fb,(EpdRect){36,profile_y,612,profile_h},24,UI_GRAY_WHITE,0x70);
        int avatar_y=profile_y+(profile_h-82)/2;
        bool avatar_ok=s_avatar_ready;
        if (avatar_ok) ui_wallpaper_draw_avatar(fb,s_avatar_pixels,(EpdRect){57,avatar_y,82,82},20);
        if (app_settings_main_fast_refresh() && avatar_ok) ui_image_bw_rect(fb,(EpdRect){57,avatar_y,82,82});
        if (!avatar_ok) {
            ui_builtin_avatar_draw(fb,57,avatar_y,82);
        }
        char name[64];snprintf(name,sizeof(name),"%s",app_settings_device_name());
        int width=588-ui_text_fixed_width_px(ui_text_effective_px(20),"编辑")-188;
        while (name[0] && ui_text_fixed_width_px(ui_text_effective_px(32),name)>width) {
            size_t n=strlen(name)-1;while(n&&((unsigned char)name[n]&0xc0)==0x80)--n;name[n]=0;
        }
        ui_text_vc(fb,164,profile_y+53,32,name,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,588,profile_y+53,20,"编辑",EPD_DRAW_ALIGN_RIGHT,false);
        ui_draw_icon(fb,625,profile_y+profile_h/2,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
    }
    char wifi[56];snprintf(wifi,sizeof(wifi),"%s",s_main_wifi_saved?s_main_wifi:"未配置");
    network_status_t network;
    network_service_get_status(&network);
    const char *connection_labels[4];
    const char *connection_values[4];
    int connection_icons[4];
    int connection_rows = 0;
    connection_labels[connection_rows] = "WiFi";
    connection_values[connection_rows] = wifi;
    connection_icons[connection_rows++] = 0;
    if (network.cellular_capable) {
        connection_labels[connection_rows] = "移动网络";
        connection_values[connection_rows] = network.route == NETWORK_ROUTE_CELLULAR ? "4G" :
            network.route == NETWORK_ROUTE_AUTO ? "自动" : "Wi-Fi";
        connection_icons[connection_rows++] = 22;
    }
    connection_labels[connection_rows] = "蓝牙翻页器";
    connection_values[connection_rows] = app_settings_ble_turner() ? "已开启" : "已关闭";
    connection_icons[connection_rows++] = 1;
    connection_labels[connection_rows] = "日期与时间";
    connection_values[connection_rows] = "设置";
    connection_icons[connection_rows++] = 6;
    setting_group(fb,SETTINGS_WIRELESS_Y-49,"连接与时间",SETTINGS_WIRELESS_Y,
                  connection_icons,connection_labels,connection_values,connection_rows);
    char font_name[96],contrast[24],idle[24],signature[96],standby[48];
    snprintf(font_name,sizeof(font_name),"%s · %u%%",system_font_label(app_settings_system_font_path()),app_settings_system_font_size());
    snprintf(contrast,sizeof(contrast),"%u%%",app_settings_system_contrast());
    unsigned minutes=app_settings_auto_lock_minutes();
    if(minutes)snprintf(idle,sizeof(idle),"%u 分钟",minutes);else strcpy(idle,"关闭");
    unsigned shutdown_minutes=app_settings_shutdown_minutes();
    if(shutdown_minutes)snprintf(standby,sizeof(standby),"浅睡 %u 分钟后关机",shutdown_minutes);
    else snprintf(standby,sizeof(standby),"%s",app_settings_shutdown_style()==APP_SHUTDOWN_LIGHT?"仅浅睡":"彻底关机");
    const char *display_labels[]={"系统字体","系统对比度","主页刷新模式","首页强刷","自动休眠锁屏","待机方式"};
    const char *display_values[]={font_name,contrast,
        app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER?"水波纹":"普通",
        app_settings_home_full_refresh()?"开启":"关闭",idle,
        standby};
    const int display_icons[]={2,7,15,11,14,9};
    setting_group(fb,SETTINGS_DISPLAY_Y-49,"显示",SETTINGS_DISPLAY_Y,
                  display_icons,display_labels,display_values,SETTINGS_DISPLAY_ROWS);
    snprintf(signature,sizeof(signature),"%s",app_settings_status_signature()[0]?app_settings_status_signature():"未设置");
    const char *personal_labels[]={"书架样式","壁纸","锁屏密码","状态栏签名","触摸震动","阅读背景","组件库","快捷栏管理"};
    char haptic_value[24];
    if(!haptics_available())snprintf(haptic_value,sizeof(haptic_value),"不支持");
    else if(!app_settings_touch_haptics())snprintf(haptic_value,sizeof(haptic_value),"关闭");
    else snprintf(haptic_value,sizeof(haptic_value),"开启 · %s",
        app_settings_haptic_intensity()==APP_HAPTIC_STRONG?"强":
        app_settings_haptic_intensity()==APP_HAPTIC_MEDIUM?"中":"弱");
    char background_value[96];
    const char *background_path=app_settings_book_background_path();
    const char *background_base=background_path[0]?strrchr(background_path,'/'):NULL;
    snprintf(background_value,sizeof(background_value),"%s",
        background_path[0]?(background_base?background_base+1:background_path):"无");
    const char *personal_values[]={shelf_style_name(app_settings_shelf_style()),
        app_settings_lock_style()==APP_LOCK_STYLE_BOOK_COVER?"书籍封面":
        app_settings_lock_style()==APP_LOCK_STYLE_CLOCK?"简约时钟":app_settings_lock_style()==4?"日历":app_settings_lock_style()==3?"透明书封":app_settings_lock_style()==2?"书架拼贴":app_settings_lock_style()==1?"壁纸":"阅读票根",
        lock_pin_enabled()?"已开启":"已关闭",signature,haptic_value,background_value,"","显示与排序"};
    const int personal_icons[]={4,5,16,10,18,19,21,15};
    setting_group(fb,SETTINGS_PERSONAL_Y-49,"个性化功能",SETTINGS_PERSONAL_Y,
                  personal_icons,personal_labels,personal_values,SETTINGS_PERSONAL_ROWS);
    const char *maintenance_labels[]={"保存与恢复","屏幕修复","系统升级"};
    const char *maintenance_values[]={"配置","清除条纹","TF 卡 / 联网"};
    const int maintenance_icons[]={8,17,12};
    setting_group(fb,SETTINGS_MAINTENANCE_Y-49,"升级和恢复",SETTINGS_MAINTENANCE_Y,
                  maintenance_icons,maintenance_labels,maintenance_values,SETTINGS_MAINTENANCE_ROWS);
    char batt_log_value[16];
    snprintf(batt_log_value,sizeof(batt_log_value),"%s",app_settings_batt_log()?"开启":"关闭");
    device_power_batt_calibration_status_t calibration;
    device_power_get_batt_calibration(&calibration);
    const char *power_labels[]={"自动电量校准(beta)", "日志调试", "充电档位"};
    const char *power_values[]={calibration.enabled ? "学习中" : calibration.applied ? "已保留校准" : "关闭", batt_log_value,
        charge_current_label(device_power_charge_current())};
    const int power_icons[]={20,20,20};
    setting_group(fb,SETTINGS_POWER_Y-49,"电源管理",SETTINGS_POWER_Y,
                  power_icons,power_labels,power_values,SETTINGS_POWER_ROWS);
    const int version_half = (ui_text_effective_px(20) + 1) / 2;
    const int version_center_y = settings_version_center_y() - s_main_scroll;
    if (version_center_y - version_half > 160 && version_center_y + version_half < UI_NAV_TOP)
        draw_settings_version(fb, version_center_y);
    // 主列表也要有一行提示位：确认、修复结果都写在这里，不然点了没有任何回音。
    // The main list needs a notice line too: confirmation and the repair result land here, or a tap
    // would have no answer at all.
    ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,160},UI_GRAY_WHITE,fb);
    ui_nav_status(fb);
    ui_text(fb, 36, 91, 52, "设置", EPD_DRAW_ALIGN_LEFT, false);
    if (s_notice[0]) ui_text_fixed_vc(fb, 36, 138, 20, s_notice, EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 3);
    if(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL){
        // 与书架共用逐相输出：状态栏/题头柔和，内容白像素不反向压黑；不影响其他模式。
        // Reuse shelf scan phases: soft status/title and no reverse-blackening on content whites, without changing other modes.
        const EpdRect content={0,160,UI_LOCK_WIDTH,UI_NAV_REFRESH_END-160};
        display_main_transition_gray_bands(&content,1);
    }
}

static void settings_prepare_profile(void) {
    const char *path=app_settings_avatar_path();
    if(pico_boot_recovery()||!path[0]){ui_avatar_request("",60);s_avatar_ready=s_avatar_pending=false;s_avatar_path[0]=0;return;}
    if(strcmp(path,s_avatar_path))s_avatar_ready=false;
    snprintf(s_avatar_path,sizeof(s_avatar_path),"%s",path);
    ui_avatar_request(path,82);
    ui_avatar_state_t state=ui_avatar_poll(path,82,s_avatar_pixels);
    s_avatar_ready=state==UI_AVATAR_READY;s_avatar_pending=state==UI_AVATAR_PENDING;
}
void app_device_settings_request_bluetooth(bool return_to_origin) {
    s_enter_page = SETTINGS_BLUETOOTH;
    s_enter_ble_from_quick = return_to_origin;
    s_enter_haptics_from_quick = false;
}
void app_device_settings_request_haptics(bool return_to_origin) {
    s_enter_page = SETTINGS_HAPTIC;
    s_enter_haptics_from_quick = return_to_origin;
    s_enter_ble_from_quick = false;
}
void app_device_settings_request_network(bool return_to_origin) {
    s_enter_page = SETTINGS_NETWORK;
    s_enter_network_from_quick = return_to_origin;
    s_enter_ble_from_quick = false;
    s_enter_haptics_from_quick = false;
}
void app_device_settings_request_memory(bool return_to_origin) {
    s_enter_page = SETTINGS_MEMORY;
    s_enter_memory_from_quick = return_to_origin;
    s_enter_ble_from_quick = false;
    s_enter_haptics_from_quick = false;
    s_enter_network_from_quick = false;
}
static void on_enter(app_ctx_t *ctx) {
    ui_lock_style_close();
    (void)ctx;
    pin_setup_close();
    s_notice[0] = 0;
    s_page = SETTINGS_MAIN;
    s_main_scroll = s_font_page = s_wallpaper_page = 0;
    s_scroll_drag_consumed = s_scroll_present_pending = false;
    s_wallpaper_selected = -1;

    s_sync_pending = false;
    s_font_sync_confirm = false; s_font_sync_path[0] = 0;
    s_config_confirm = false;
    s_batt_calibration_confirm = false;
    s_batt_calibration_next_ms = 0;
    s_panel_repair_pending = false;
    s_boot_pending = s_local_pending = s_upgrade_restart = false;
    s_upgrade_offer_pending = s_upgrade_confirm = false;
    s_upgrade_shortcut_back = false;
    (void)wifi_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved);
    settings_prepare_profile();
    // 每次进页先清掉上一次直达的返回目标，避免离开时未经过主列表而留下旧值。
    // Clear the previous direct-visit back target on every entry so a visit that
    // left without passing the main list cannot leave a stale target behind.
    s_ble_shortcut_back = false;
    s_haptics_shortcut_back = false;
    s_network_shortcut_back = false;
    s_memory_shortcut_back = false;
    s_memory_present_pending = false;
    s_memory_dirty = false;
    s_memory_last_present_ms = 0;
    s_memory_next_sample_ms = 0;
    network_service_get_status(&s_network_snapshot);
    // 快捷栏长按直达蓝牙子页：初始状态与主列表「蓝牙」行一致，消费后回到主列表。
    // Quick-bar shortcut straight to the Bluetooth sub-page: same initial state as
    // the main list's Bluetooth row, falling back to the main list after one use.
    if (s_enter_page == SETTINGS_BLUETOOTH) {
        s_page = SETTINGS_BLUETOOTH;
        settings_bluetooth_open();
        // 从其他页面直达时，左上角返回回到来源页；设置页内部重进仍回主列表。
        // A direct visit from another page sends the top-left back to that origin;
        // re-entering from inside Settings keeps the main-list back.
        s_ble_shortcut_back = s_enter_ble_from_quick;
    }
    if (s_enter_page == SETTINGS_HAPTIC) {
        s_page = SETTINGS_HAPTIC;
        s_haptics_shortcut_back = s_enter_haptics_from_quick;
    }
    if (s_enter_page == SETTINGS_NETWORK) {
        s_page = SETTINGS_NETWORK;
        s_network_notice = NULL;
        s_network_shortcut_back = s_enter_network_from_quick;
    }
    if (s_enter_page == SETTINGS_MEMORY) {
        s_page = SETTINGS_MEMORY;
        s_memory_shortcut_back = s_enter_memory_from_quick;
        memory_inventory_snapshot(ctx ? ctx->hl : NULL, &s_memory_snapshot);
        mm_get_clean_report(&s_memory_report);
        s_memory_next_sample_ms = ctx ? ctx->now_ms : 0;
        s_memory_last_present_ms = ctx ? ctx->now_ms : 0;
    }
    if (s_enter_page == SETTINGS_UPGRADE) {
        upgrade_open();
        s_upgrade_confirm = s_upgrade_online = s_update.state == PICO_UPDATE_AVAILABLE && !s_update.busy;
        s_upgrade_shortcut_back = true;
    }
    s_enter_page = SETTINGS_MAIN;
    s_enter_ble_from_quick = false;
    s_enter_haptics_from_quick = false;
    s_enter_network_from_quick = false;
    s_enter_memory_from_quick = false;
}

static app_redraw_t profile_editor_paint(app_ctx_t *ctx, bool field);
static app_redraw_t on_tick(app_ctx_t *ctx) {
    if (s_page == SETTINGS_BATT_CALIBRATION && ctx->now_ms >= s_batt_calibration_next_ms) {
        device_power_batt_calibration_status_t next;
        device_power_get_batt_calibration(&next);
        s_batt_calibration_next_ms = ctx->now_ms + 1000;
        const bool changed = next.enabled != s_batt_calibration_status.enabled ||
            next.applied != s_batt_calibration_status.applied || next.save_error != s_batt_calibration_status.save_error ||
            next.sampled_bins != s_batt_calibration_status.sampled_bins ||
            next.trusted_bins != s_batt_calibration_status.trusted_bins;
        s_batt_calibration_status = next;
        if (changed) return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_MEMORY) {
        if (ctx->now_ms >= s_memory_next_sample_ms) {
            mm_snapshot_t next;
            mm_clean_report_t report;
            memory_inventory_snapshot(ctx->hl, &next);
            mm_get_clean_report(&report);
            const bool state_changed = next.clean_state != s_memory_snapshot.clean_state ||
                next.clean_request_id != s_memory_snapshot.clean_request_id ||
                report.state != s_memory_report.state || report.request_id != s_memory_report.request_id;
            const bool changed = memory_snapshot_changed(&next, &s_memory_snapshot) ||
                memcmp(&report, &s_memory_report, sizeof(report)) != 0;
            s_memory_snapshot = next;
            s_memory_report = report;
            s_memory_next_sample_ms = ctx->now_ms + 500;
            if (changed) s_memory_dirty = true;
            const bool present_due = s_memory_last_present_ms == 0 ||
                ctx->now_ms - s_memory_last_present_ms >= 1000;
            if (s_memory_dirty && (state_changed || present_due)) {
                s_memory_present_pending = true;
                s_memory_dirty = false;
                s_memory_last_present_ms = ctx->now_ms;
                return APP_REDRAW_AREA;
            }
        }
        return APP_REDRAW_NONE;
    }
    // 屏幕修复：先画最终结果，再在 tick 中整屏恢复；成功后不重复推页。
    // Paint the final notice before the tick's full restoration; do not push the page twice on success.
    if (s_panel_repair_pending) {
        s_panel_repair_pending = false;
        if (s_page != SETTINGS_MAIN) return APP_REDRAW_NONE;
        s_scroll_present_pending = false;
        snprintf(s_notice, sizeof(s_notice), "屏幕修复完成；若条纹仍在可再做一次");
        render(ctx, ctx->fb);
        if (display_panel_repair(ctx->hl) != EPD_DRAW_SUCCESS) {
            snprintf(s_notice, sizeof(s_notice), "屏幕输出未完成，请稍后重试");
            return APP_REDRAW_FULL;
        }
        return APP_REDRAW_DONE;
    }

    if (s_page==SETTINGS_MAIN && s_avatar_pending && !(ctx->touch && ctx->touch->touched)) {
        ui_avatar_state_t state=ui_avatar_poll(s_avatar_path,82,s_avatar_pixels);
        s_avatar_pending=state==UI_AVATAR_PENDING;
        s_avatar_ready=state==UI_AVATAR_READY;
        if (s_avatar_ready && s_main_scroll < 180+settings_profile_height()-160) {
            render(ctx,ctx->fb);
            EpdRect area={57,180-s_main_scroll+(settings_profile_height()-82)/2,82,82};
            if (area.y<160) { area.height-=160-area.y; area.y=160; }
            if(area.height>0)guard_draw_result(ctx->hl,display_refresh_area(ctx->hl, DISPLAY_REFRESH_MAIN_DETAIL, area, DISPLAY_REFRESH_DEFAULT));
            return APP_REDRAW_DONE;
        }
    }
    if (s_page == SETTINGS_PIN_ENTRY) return pin_setup_tick(ctx);
    if (s_page == SETTINGS_LOCK_STYLE && !(ctx->touch && ctx->touch->touched) && ui_lock_style_tick(ctx->now_ms)) {
        return APP_REDRAW_FULL;
    }

    if (s_page == SETTINGS_READING_BG && reading_bg_thumbnail_tick(ctx->touch && ctx->touch->touched))
        return APP_REDRAW_PAGE;

    // 阅读背景预览：解码上限 50 MB，只能放 tick 里做，render 只画缓存。
    // Reading-background preview: decoding reads up to 50 MB, so it runs here; render paints
    // the cache only.
    if (s_reading_bg_pending && s_page == SETTINGS_READING_BG_PREVIEW) {
        s_reading_bg_pending = false;
        s_reading_bg_ready = s_wallpaper_selected >= 0 && s_wallpaper_selected < s_wallpaper_count &&
            ui_reader_background_prepare(s_wallpapers[s_wallpaper_selected].path);
        if (!s_reading_bg_ready)
            snprintf(s_reading_bg_notice, sizeof(s_reading_bg_notice), "%s", "图片无法解码或内存不足，请换一张");
        return APP_REDRAW_PAGE;
    }

    if (s_page == SETTINGS_TEXT_EDIT) {
        if (ctx->consumed) return APP_REDRAW_NONE;
        bool held = !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
        if (ui_keyboard_hold_tick(held, ctx->touch ? ctx->touch->x : 0,
            ctx->touch ? ctx->touch->y : 0, ctx->now_ms) == UI_KEYBOARD_CHANGED) return profile_editor_paint(ctx, false);
        return ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) == UI_KEYBOARD_CHANGED
            ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BLUETOOTH && !(ctx->touch && ctx->touch->touched) &&
        settings_bluetooth_tick(ctx->now_ms)) return APP_REDRAW_PAGE;
    if ((s_page == SETTINGS_NETWORK || s_page == SETTINGS_MAIN) &&
        !(ctx->touch && ctx->touch->touched) && settings_network_poll()) return APP_REDRAW_PAGE;

    (void)ctx;
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
    // 等待已离页的接收任务完成 TF 交还；不阻塞设置页轮询。
    // Defer restart until an exiting receiver returns TF handles without blocking settings ticks.
    if ((s_boot_pending || s_upgrade_restart) && !cloudzao_quiesce(0)) return APP_REDRAW_NONE;
#endif
    if (s_boot_pending) {
        s_boot_pending = false;
        pmu_selftest_prepare_powerdown();
        uint8_t req[2] = {0, 0};
        esp_err_t error = device_power_cmd(PMU_CMD_HOST_REQUEST_RESET, req, sizeof(req));
        if (error != ESP_OK) ESP_LOGW(TAG, "BOOT PMU notice: %s", esp_err_to_name(error));
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        // FORCE_DOWNLOAD_BOOT 是 S3 RTC 寄存器；S31 只能靠按住 BOOT 或 esptool 进入下载。
        // FORCE_DOWNLOAD_BOOT is an S3 RTC register; S31 enters download via BOOT/esptool.
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
#endif
        esp_restart(); return APP_REDRAW_NONE;
    }
    if (s_upgrade_restart) { s_upgrade_restart = false; esp_restart(); return APP_REDRAW_NONE; }
    if (s_page == SETTINGS_UPGRADE) {
        if (s_upgrade_job != UPGRADE_JOB_NONE) return upgrade_start_pending();
        if (s_local_pending) {
            s_local_pending = false;
            esp_err_t error = pico_ota_install(PICO_OTA_UPDATE_PATH, s_upgrade_notice, sizeof(s_upgrade_notice));
            s_upgrade_restart = error == ESP_OK;
            return APP_REDRAW_PAGE;
        }
        pico_online_get_status(&s_update);
        if (s_update.state == PICO_UPDATE_READY && !s_update.busy) {
#ifdef CONFIG_MOGO_CLOUDZAO_TRANSFER
            if (!cloudzao_quiesce(0)) return APP_REDRAW_NONE;
#endif
            if (pico_online_commit() == ESP_OK) { esp_restart(); return APP_REDRAW_NONE; }
            pico_online_get_status(&s_update);
        }
        uint32_t now = esp_timer_get_time() / 1000;
        app_redraw_t redraw = upgrade_status_redraw(now);
        if (redraw == APP_REDRAW_AREA) draw_upgrade_progress_delta(ctx->fb);
        return redraw;
    }
    if (s_page != SETTINGS_TIME) {
        s_sync_pending = false;
        return APP_REDRAW_NONE;
    }
    app_time_sync_result_t result = {0};
    if (s_sync_pending) {
        s_sync_pending = false;
        result.error = pico_online_busy() ? MEMORY_PEAK_BUSY : app_time_service_start_manual();
        if (result.error == ESP_OK) {
            s_sync_waiting = true;
            return APP_REDRAW_NONE;
        }
    } else if (!s_sync_waiting || !app_time_service_take_manual_result(&result)) {
        return APP_REDRAW_NONE;
    }
    s_sync_waiting = false;
    esp_err_t err = result.error;
    bool network_time_received = result.network_time_received;
    const char *hint = memory_peak_hint(err);
    const char *notice = err == ESP_OK ? "网络对时完成，内部时钟已接管" :
        network_time_received ? "已获取网络时间，内部时钟写入失败" :
        hint ? hint :
        err == ESP_ERR_NO_MEM ? "内存不足，请稍后重试" :
        err == ESP_ERR_NOT_FOUND ? "所选网络不可用，请检查网络设置" :
        err == ESP_ERR_INVALID_STATE ? "网络尚未连接，请稍后再试" :
        err == ESP_ERR_TIMEOUT ? "网络对时超时，请检查网络后重试" :
        "网络对时失败，请检查网络后重试";
    snprintf(s_notice, sizeof(s_notice), "%s", notice);
    return APP_REDRAW_PAGE;
}

static EpdRect s_input_area;
static bool s_input_layout, s_input_settle;
static app_redraw_t profile_editor_paint(app_ctx_t *ctx, bool field) {
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, 560, (EpdRect){36, 243, 612, 82}, 28, false, NULL, field);
    s_input_area = update.area; s_input_layout = update.layout; s_input_settle = update.settle;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static app_redraw_t profile_editor_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type == UI_GESTURE_PRESS)
        return ui_keyboard_press(ev->x0, ev->y0, 560, ctx->now_ms) ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
    bool feedback = ev->type != UI_GESTURE_LONG_PRESS && ui_keyboard_release();
    if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R) {
        bool page = ui_keyboard_page(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1);
        return (page || feedback) ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (ev->type != UI_GESTURE_TAP) return feedback ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
    int x = ev->x0, y = ev->y0;
    if (y < 160) {
        if (x < 160) { ui_keyboard_end(); s_page = s_editor_signature ? SETTINGS_MAIN : SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
        if (x > 510) goto save_text;
    }
    if (ui_text_input_tap(&s_editor_input, (EpdRect){36, 243, 612, 82}, 28,
                          false, NULL, x, y)) return profile_editor_paint(ctx, true);
    ui_keyboard_result_t result = ui_keyboard_tap(x, y, 560, esp_timer_get_time() / 1000);
    if (result == UI_KEYBOARD_DONE) goto save_text;
    return result == UI_KEYBOARD_CHANGED ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
save_text:
    if (ui_keyboard_pending()) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "请先选择候选字");
        return APP_REDRAW_PAGE;
    }
    if (s_editor_signature) app_settings_set_status_signature(s_editor);
    else if (s_editor[0]) app_settings_set_device_name(s_editor);
    else { snprintf(s_editor_notice, sizeof(s_editor_notice), "设备名称不能为空"); return APP_REDRAW_PAGE; }
    const char *saved = s_editor_signature ? app_settings_status_signature() : app_settings_device_name();
    if (strcmp(saved, s_editor)) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "保存失败，请检查设置存储空间");
        return APP_REDRAW_PAGE;
    }
    ui_keyboard_end();
    s_page = s_editor_signature ? SETTINGS_MAIN : SETTINGS_PROFILE;
    return APP_REDRAW_PAGE;
}

// 越过阈值按段推进，一次手势只更新一次，松手不再播放惯性。
// Advance one discrete step after crossing the threshold, with one update per gesture and no release inertia.
static app_redraw_t scroll_gesture(const ui_gesture_event_t *ev, int *offset, int limit, bool *handled) {
    *handled = ev->type != UI_GESTURE_TAP || s_scroll_drag_consumed;
    if (ev->type == UI_GESTURE_PRESS) {
        s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_TAP || ev->type == UI_GESTURE_CANCEL) {
        s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    if (ev->type != UI_GESTURE_MOVE && ev->type != UI_GESTURE_SWIPE_U && ev->type != UI_GESTURE_SWIPE_D)
        return APP_REDRAW_NONE;
    if (ev->y0 < (s_page == SETTINGS_MAIN ? 160 : 190) || ev->y0 >= UI_NAV_TOP)
        return APP_REDRAW_NONE;
    if (s_scroll_drag_consumed) {
        if (ev->type != UI_GESTURE_MOVE) s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    int dy = (int)ev->y - ev->y0, dx = (int)ev->x - ev->x0;
    int distance = abs(dy);
    if (ev->type == UI_GESTURE_MOVE && (distance < 108 || distance <= abs(dx))) return APP_REDRAW_NONE;
    int step = distance < 204 ? 204 : distance > 450 ? 450 : distance;
    bool up = ev->type == UI_GESTURE_SWIPE_U || (ev->type == UI_GESTURE_MOVE && dy < 0);
    int next = *offset + (up ? step : -step);
    if (limit < 0) limit = 0;
    if (next < 0) next = 0;
    if (next > limit) next = limit;
    if (ev->type == UI_GESTURE_MOVE) s_scroll_drag_consumed = true;
    if (next == *offset) return APP_REDRAW_NONE;
    *offset = next;
    s_scroll_present_pending = true;
    return APP_REDRAW_AREA;
}

static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (s_page == SETTINGS_QUICK_BAR) return quick_settings_gesture(ctx, ev);
    if (s_page == SETTINGS_LOCK_STYLE) {
        if (ev->type==UI_GESTURE_TAP) {
            int tab=ui_nav_hit(ev->x0,ev->y0);
            if(tab>=0){ui_lock_style_close();s_page=SETTINGS_MAIN;ui_nav_request(ctx,tab);return APP_REDRAW_PAGE;}
        }
        if (ev->type==UI_GESTURE_TAP && ev->y0<140 && ev->x0<170) {
            if (ui_lock_style_back()) return APP_REDRAW_PAGE;
            ui_lock_style_close();s_page=SETTINGS_MAIN;settings_prepare_profile();return APP_REDRAW_PAGE;
        }
        return ui_lock_style_gesture(ev)?APP_REDRAW_PAGE:APP_REDRAW_NONE;
    }
    // 快捷栏直达的返回只属于这次访问：回到设置主列表后恢复普通返回。
    // Quick-bar back targets belong to this visit only: once the main list
    // is shown again the ordinary back returns.
    if (s_ble_shortcut_back && s_page == SETTINGS_MAIN) s_ble_shortcut_back = false;
    if (s_haptics_shortcut_back && s_page == SETTINGS_MAIN) s_haptics_shortcut_back = false;
    if (s_page==SETTINGS_SYSTEM_FONT && s_font_sync_confirm) {
        if (ev->type!=UI_GESTURE_TAP) return APP_REDRAW_NONE;
        for (int i=0;i<2;++i) {
            if (!ui_rect_hit(system_font_sync_button(i),ev->x0,ev->y0)) continue;
            if (i) {
                app_settings_set_font_path(s_font_sync_path);
                if (strcmp(app_settings_font_path(),s_font_sync_path)) {
                    snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");return APP_REDRAW_PAGE;
                }
            }
            s_notice[0]=0;s_font_sync_confirm=false;return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_PIN_ENTRY) {
        EpdRect dirty; ui_pin_result_t result = ui_pinpad_handle(&s_pinpad, ev, &dirty);
        if (result == UI_PIN_CANCEL) { pin_setup_close(); s_page = SETTINGS_LOCK_PIN; return APP_REDRAW_PAGE; }
        if (result == UI_PIN_COMPLETE) s_pin_pending = true;
        if (result == UI_PIN_NONE) return APP_REDRAW_NONE;
        ui_pinpad_paint_input(ctx->fb, &s_pinpad);
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_FEEDBACK, dirty, DISPLAY_REFRESH_DEFAULT));
        return APP_REDRAW_DONE;
    }
    if (s_page == SETTINGS_LOCK_PIN && ev->type == UI_GESTURE_TAP && lock_pin_available() &&
        (ui_rect_hit((EpdRect){36,406,612,70},ev->x0,ev->y0) ||
         (lock_pin_enabled() && ui_rect_hit((EpdRect){36,512,612,70},ev->x0,ev->y0)))) {
        if (ctx->now_ms < s_pin_retry_at) { snprintf(s_notice, sizeof(s_notice), "请等待后重试"); return APP_REDRAW_PAGE; }
        pin_setup_open(ctx, ev->y0 >= 512); return APP_REDRAW_PAGE;
    }

    if (s_page == SETTINGS_BOOT) {
        if (ev->type != UI_GESTURE_TAP || s_boot_pending) return APP_REDRAW_NONE;
        if (ev->y0 < 190 && ev->x0 < 170) { s_page = SETTINGS_MAIN; (void)wifi_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
        if (ui_rect_hit((EpdRect){60, 438, 564, 78}, ev->x0, ev->y0)) { s_boot_pending = true; return APP_REDRAW_PAGE; }
        int tab = ui_nav_hit(ev->x0, ev->y0);
        if (tab >= 0) ui_nav_request(ctx, tab);
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_UPGRADE) {
        if (ev->type != UI_GESTURE_TAP || s_local_pending || s_upgrade_restart) return APP_REDRAW_NONE;
        int y = ev->y0;
        if (y < 190 && ev->x0 < 170) {
            s_upgrade_job = UPGRADE_JOB_NONE;
            s_upgrade_offer_pending = false;
            if (s_upgrade_shortcut_back) { ctx->request_return = true; return APP_REDRAW_NONE; }
            if (s_upgrade_confirm) s_upgrade_confirm = false;
            else { pico_online_cancel_join(); s_page = SETTINGS_MAIN; }
            return APP_REDRAW_PAGE;
        }
        int tab = ui_nav_hit(ev->x0, y);
        if (tab >= 0) { s_upgrade_job = UPGRADE_JOB_NONE; pico_online_cancel_join(); ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
        if (s_upgrade_confirm) {
            bool accept = ui_rect_hit(upgrade_confirm_button(s_upgrade_online, true), ev->x0, y);
            if (accept || ui_rect_hit(upgrade_confirm_button(s_upgrade_online, false), ev->x0, y)) {
                s_upgrade_confirm = false;
                s_upgrade_offer_pending = false;
                if (accept) {
                    if (s_upgrade_online) {
                        upgrade_schedule(true, esp_timer_get_time() / 1000);
                    } else s_local_pending = true;
                } else if (s_upgrade_shortcut_back) { ctx->request_return = true; return APP_REDRAW_NONE; }
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        if (ev->x0 < 60 || ev->x0 >= 624) return APP_REDRAW_NONE;
        if (y >= 550 && y < 612) {
            s_upgrade_notice[0] = 0;
            if (s_update.busy) { s_upgrade_job = UPGRADE_JOB_NONE; s_upgrade_offer_pending = false; pico_online_cancel_join(); }
            else if (s_update.state == PICO_UPDATE_AVAILABLE) { s_upgrade_confirm = true; s_upgrade_online = true; }
            else {
                upgrade_schedule(false, esp_timer_get_time() / 1000);
                return APP_REDRAW_PAGE;
            }
            pico_online_get_status(&s_update);
            upgrade_remember(esp_timer_get_time() / 1000);
            return APP_REDRAW_PAGE;
        }
        if (y >= 662 && y < 788) {
            app_settings_set_auto_ota(!app_settings_auto_ota());
            pico_ota_auto_poll(false);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_TEXT_EDIT) return profile_editor_gesture(ctx, ev);
    if (s_page == SETTINGS_BLUETOOTH) {
        if (ev->type == UI_GESTURE_TAP) {
            int tab = ui_nav_hit(ev->x0, ev->y0);
            if (tab >= 0) {
                settings_bluetooth_close(); s_page = SETTINGS_MAIN; s_ble_shortcut_back = false;
                ui_nav_request(ctx, tab); return APP_REDRAW_PAGE;
            }
            if (ev->x0 < 170 && ev->y0 < 140) {
                if (settings_bluetooth_back()) return APP_REDRAW_PAGE;
                settings_bluetooth_close();
                if (s_ble_shortcut_back) { s_ble_shortcut_back = false; ctx->request_return = true; return APP_REDRAW_NONE; }
                s_page = SETTINGS_MAIN; settings_prepare_profile(); return APP_REDRAW_PAGE;
            }
        }
        return settings_bluetooth_gesture(ev) ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_MAIN) {
        bool handled;
        app_redraw_t redraw = scroll_gesture(ev, &s_main_scroll, SETTINGS_SCROLL_MAX, &handled);
        if (handled) return redraw;
    }

    if (s_page == SETTINGS_AVATAR && (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int pages = (s_wallpaper_count + 6) / 7;
        int old = s_wallpaper_page;
        if (ev->type == UI_GESTURE_SWIPE_U && s_wallpaper_page + 1 < pages) ++s_wallpaper_page;
        if (ev->type == UI_GESTURE_SWIPE_D && s_wallpaper_page > 0) --s_wallpaper_page;
        if (s_wallpaper_page == old) return APP_REDRAW_NONE;
        s_scroll_present_pending = true;
        return APP_REDRAW_AREA;
    }

    if (s_page == SETTINGS_READING_BG_PREVIEW && !s_reading_bg_confirm &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int next = s_wallpaper_selected + (ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
        if (next < 0 || next >= s_wallpaper_count) return APP_REDRAW_NONE;
        s_wallpaper_selected = next;
        s_wallpaper_page = next / READING_BG_THUMB_SLOTS;
        // 换图要重新解码：ready 只对上一张有效。
        // A different image needs a fresh decode; ready was only valid for the previous one.
        s_reading_bg_ready = false;
        s_reading_bg_pending = true;
        s_reading_bg_notice[0] = 0;
        return APP_REDRAW_PAGE;
    }
    if ((s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_READING_BG) &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int *page = s_page == SETTINGS_SYSTEM_FONT ? &s_font_page : &s_wallpaper_page;
        int count = s_page == SETTINGS_SYSTEM_FONT ? ttf_font_count() + 1 : s_wallpaper_count;
        int slots = s_page == SETTINGS_SYSTEM_FONT ? system_font_layout().slots : READING_BG_THUMB_SLOTS;
        int pages = count > 0 ? (count + slots - 1) / slots : 1;
        int old = *page;
        if (ev->type == UI_GESTURE_SWIPE_U && *page + 1 < pages) ++*page;
        if (ev->type == UI_GESTURE_SWIPE_D && *page > 0) --*page;
        if (*page == old) return APP_REDRAW_NONE;
        s_scroll_present_pending = true;
        return APP_REDRAW_AREA;
    }
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;

    if (s_page == SETTINGS_TIME_EDIT) {
        if (ev->y0 < 190) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
        if (ev->y0 >= 354 && ev->y0 < 597) {
            int field = (ev->x0 - 36) / 122;
            if (ev->x0 >= 36 && field >= 0 && field < 5 && (ev->x0 - 36) % 122 < 106) {
                if (ev->y0 < 416) time_adjust(field, 1);
                else if (ev->y0 >= 535) time_adjust(field, -1);
                else return APP_REDRAW_NONE;
                return APP_REDRAW_PAGE;
            }
        }
        if (ev->y0 >= 767 && ev->y0 < 845) { (void)time_save(); return APP_REDRAW_PAGE; }
        if (ev->y0 >= 874 && ev->y0 < 952) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_MEMORY) {
        const int tab = ui_nav_hit(ev->x0, ev->y0);
        if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
        const int y = ev->y0;
        if (y < 190) {
            if (s_memory_shortcut_back) {
                s_memory_shortcut_back = false;
                ctx->request_return = true;
                return APP_REDRAW_NONE;
            }
            s_page = SETTINGS_MAIN;
            (void)wifi_transfer_get_saved_wifi(s_main_wifi, &s_main_wifi_saved);
            settings_prepare_profile();
            return APP_REDRAW_PAGE;
        }
        if (y >= 882 && y < 958 && ev->x0 >= 36 && ev->x0 < 648) {
            if (mm_request_clean(MM_CLEAN_MEMORY_PAGE, NULL) == ESP_OK) {
                memory_inventory_snapshot(ctx->hl, &s_memory_snapshot);
                s_memory_present_pending = false;
                s_memory_dirty = false;
                s_memory_last_present_ms = ctx->now_ms;
            }
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    int tab = ui_nav_hit(ev->x0, ev->y0);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    int y = ev->y0;
    if (s_page == SETTINGS_PROFILE) {
        if (y < 190) { s_page = SETTINGS_MAIN; (void)wifi_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
        if (y >= 266 && y < 350) { profile_editor_open(false); return APP_REDRAW_PAGE; }
        if (y >= 366 && y < 450) { wallpaper_scan(); s_page = SETTINGS_AVATAR; return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_AVATAR) {
        if (y < 190) { s_page = SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
        if (y >= 270 && y < 354) { app_settings_set_avatar_path(""); s_page = SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
        if (y >= 374 && y < 1046) {
            int i = (y - 374) / 96, index = s_wallpaper_page * 7 + i;
            if (index >= 0 && index < s_wallpaper_count && (y - 374) % 96 < 80) {
                app_settings_set_avatar_path(s_wallpapers[index].path);
                s_page = SETTINGS_PROFILE;
                return APP_REDRAW_PAGE;
            }
        }
        return APP_REDRAW_NONE;
    }

    if (s_page == SETTINGS_READING_BG_PREVIEW) {
        if (y < 190) { s_page = SETTINGS_READING_BG; return APP_REDRAW_PAGE; }
        if (s_reading_bg_confirm) {
            if (y >= 643 && y < 711 && ev->x0 >= 358 && ev->x0 < 590) {
                if (app_settings_set_book_background_path(s_wallpapers[s_wallpaper_selected].path))
                    snprintf(s_reading_bg_notice, sizeof(s_reading_bg_notice), "%s", "已设为阅读背景，下次打开书籍生效");
            }
            // 其余点按一律视为取消。
            // Any other tap cancels the pending background choice.
            s_reading_bg_confirm = false;
            return APP_REDRAW_PAGE;
        }
        if (s_reading_bg_ready && y >= 194 && y < 1032) {
            s_reading_bg_confirm = true;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_READING_BG) {
        if (y < 190) { s_page = SETTINGS_MAIN; (void)wifi_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
        if (ui_rect_hit(reading_bg_restore_rect(), ev->x0, y)) {
            if (!app_settings_set_book_background_path("")) return APP_REDRAW_NONE;
            snprintf(s_reading_bg_notice, sizeof(s_reading_bg_notice), "%s", "已恢复白底");
            return APP_REDRAW_PAGE;
        }
        for (int slot = 0; slot < READING_BG_THUMB_SLOTS; ++slot) {
            EpdRect card = reading_bg_card_rect(slot);
            if (!ui_rect_hit(card, ev->x0, y)) continue;
            int index = s_wallpaper_page * READING_BG_THUMB_SLOTS + slot;
            if (index < 0 || index >= s_wallpaper_count) return APP_REDRAW_NONE;
            s_wallpaper_selected = index;
            s_reading_bg_confirm = false;
            s_reading_bg_ready = false;
            s_reading_bg_pending = true;
            s_reading_bg_notice[0] = 0;
            reading_bg_thumbnails_release();
            s_page = SETTINGS_READING_BG_PREVIEW;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_SHELF_STYLE) {
        if (y < 190) { s_page = SETTINGS_MAIN; s_notice[0] = 0; (void)wifi_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
        for (int i = 0; i < SHELF_STYLE_CARD_COUNT; ++i) {
            EpdRect card = shelf_style_card(i);
            if (!ui_rect_hit(card, ev->x0, y)) continue;
            const uint8_t style = shelf_style_id(i);
            if (i < 2 && ui_rect_hit(shelf_style_titles_row(card), ev->x0, y)) {
                const bool saved = app_settings_set_shelf_show_titles(style, !app_settings_shelf_show_titles(style));
                snprintf(s_notice, sizeof(s_notice), "%s", saved ? "" : "保存失败，请重试");
                return APP_REDRAW_PAGE;
            }
            app_settings_set_shelf_style(style);
            s_notice[0] = 0;
            s_page = SETTINGS_MAIN;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_HAPTIC && s_haptics_shortcut_back && y < 190) {
        // 快捷栏直达子页：左上角返回直接回到来源页；阅读正文由主循环按离开时
        // 的书与全屏状态重开。
        // A direct quick-bar sub-page visit sends the top-left back
        // to the origin page; the loop reopens the reading body with the book and
        // full-screen state captured on the way out.
        s_haptics_shortcut_back = false;
        ctx->request_return = true;
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_NETWORK && s_network_shortcut_back && y < 190) {
        s_network_shortcut_back = false;
        ctx->request_return = true;
        return APP_REDRAW_NONE;
    }

    if (s_page != SETTINGS_MAIN && y < 190) { s_page = SETTINGS_MAIN; (void)wifi_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_LOCK_PIN) return APP_REDRAW_NONE;
    if (s_page == SETTINGS_AUTO_LOCK) {
        const uint8_t values[] = {1, 5, 10, 0};
        for (int i = 0; i < 4; ++i) if (ui_rect_hit((EpdRect){36, 272 + i * 112, 612, 92}, ev->x0, y)) {
            app_settings_set_auto_lock_minutes(values[i]);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_HAPTIC) {
        if (!haptics_available()) return APP_REDRAW_NONE;
        if (ui_rect_hit((EpdRect){36, 272, 612, 126}, ev->x0, y)) {
            // 打开时震一下作预览；关闭不再震。
            // / Enabling previews one pulse; disabling stays silent.
            bool on = !app_settings_touch_haptics();
            app_settings_set_touch_haptics(on);
            if (on) haptics_pulse();
            return APP_REDRAW_PAGE;
        }
        for (int i = 0; i < 3; ++i) if (ui_rect_hit((EpdRect){36, 482 + i * 112, 612, 92}, ev->x0, y)) {
            app_settings_set_haptic_intensity((uint8_t)i);
            haptics_pulse();
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_NETWORK) {
        network_status_t network;
        network_service_get_status(&network);
        if (network.cellular_capable && y >= NETWORK_TOGGLE_Y &&
            y < NETWORK_TOGGLE_Y + NETWORK_TOGGLE_H) {
            const esp_err_t err = network_service_set_enabled(NETWORK_BEARER_CELLULAR, !network.cellular_enabled);
            s_network_notice = memory_peak_hint(err);
            if (err != ESP_OK && !s_network_notice) {
                s_network_notice = network.cellular_enabled ? "4G 未能关闭，请稍后重试" :
                    err == ESP_ERR_NO_MEM ? "内存不足，无法启动 4G" : "4G 未能启动，请稍后重试";
            }
            return APP_REDRAW_PAGE;
        }
        if (network.cellular_capable && y >= NETWORK_ROUTE_ROW_Y &&
            y < NETWORK_ROUTE_ROW_Y + NETWORK_ROW_H) {
            network_route_t next = network.route == NETWORK_ROUTE_WIFI ? NETWORK_ROUTE_CELLULAR :
                network.route == NETWORK_ROUTE_CELLULAR ? NETWORK_ROUTE_AUTO : NETWORK_ROUTE_WIFI;
            if (next != NETWORK_ROUTE_CELLULAR || network.cellular_capable)
                (void)network_service_set_route(next, network.auto_priority);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BATT_CALIBRATION) return batt_calibration_touch(ev->x0, y);
    if (s_page == SETTINGS_CHARGE_CURRENT) return charge_current_touch(ev->x0, y);
    if (s_page == SETTINGS_POWER_LOG) {
        if (ui_rect_hit((EpdRect){36, 272, 612, 126}, ev->x0, y)) {
            app_settings_set_batt_log(!app_settings_batt_log());
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }

    if (s_page == SETTINGS_WIFI) {
        if (y >= 408 && y < 478) {
            extern const app_desc_t app_transfer;
            app_transfer_request_wifi_setup();
            ctx->request_app = &app_transfer;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_SYSTEM_FONT) {
        system_font_layout_t l=system_font_layout();
        if (ui_rect_hit(l.size,ev->x0,y)) {
            unsigned prior=app_settings_system_font_size();
            unsigned next=prior>=200?100:prior+10;
            app_settings_set_system_font_size(next);
            if (app_settings_system_font_size()!=next) {
                snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");return APP_REDRAW_PAGE;
            }
            s_font_page=0;
            if (s_main_scroll>SETTINGS_SCROLL_MAX) s_main_scroll=SETTINGS_SCROLL_MAX;
            ui_text_set_system_scale(true);s_notice[0]=0;return APP_REDRAW_PAGE;
        }
        for (int slot=0;slot<l.slots;++slot) {
            if (!ui_rect_hit(system_font_item_rect(slot),ev->x0,y)) continue;
            int index=s_font_page*l.slots+slot;
            if (index>ttf_font_count()) return APP_REDRAW_NONE;
            const ttf_font_item_t *item=index?ttf_font_item(index-1):NULL;
            if (index && !item) return APP_REDRAW_NONE;
            snprintf(s_font_sync_path,sizeof(s_font_sync_path),"%s",item?item->path:"");
            app_settings_set_system_font_path(s_font_sync_path);
            if (strcmp(app_settings_system_font_path(),s_font_sync_path)) {
                snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");return APP_REDRAW_PAGE;
            }
            app_font_activate_system();s_notice[0]=0;
            s_font_sync_confirm=strcmp(app_settings_font_path(),s_font_sync_path)!=0;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_MAIN_REFRESH) {
        for (int i = 0; i < MAIN_REFRESH_CHOICE_COUNT; ++i) {
            if (!ui_rect_hit((EpdRect){36, 298 + i * 158, 612, 132}, ev->x0, y)) continue;
            app_settings_set_main_refresh_mode(s_main_refresh_choices[i]);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_SYSTEM_SIZE) {
        for (int index = 0; index < 11; ++index) {
            EpdRect box = {36 + (index % 2) * 316, 298 + (index / 2) * 122, 296, 102};
            if (!ui_rect_hit(box, ev->x0, y)) continue;
            app_settings_set_system_font_size((uint8_t)(100 + index * 10));
            ui_text_set_system_scale(true);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_SYSTEM_CONTRAST) {
        if (y < 298 || y >= 878) return APP_REDRAW_NONE;
        int index = (y - 298) / 116;
        app_settings_set_system_contrast((uint8_t)(100 + index * 10));
        return APP_REDRAW_PAGE;
    }

    if (s_page == SETTINGS_TIME) {
        if (y >= TIME_ROW_AUTO_Y && y < TIME_ROW_AUTO_Y + TIME_ROW_HIT_H) {
            app_settings_set_auto_time_sync(!app_settings_auto_time_sync());
            s_notice[0] = 0;
            return APP_REDRAW_PAGE;
        }
        if (y >= TIME_ROW_WIFI_Y && y < TIME_ROW_WIFI_Y + TIME_ROW_HIT_H) {
            if (s_sync_pending || s_sync_waiting) return APP_REDRAW_NONE;
            s_notice[0] = 0; s_sync_pending = true;
            return APP_REDRAW_PAGE;
        }
        if (y >= TIME_ROW_MANUAL_Y && y < TIME_ROW_MANUAL_Y + TIME_ROW_HIT_H) {
            if (s_sync_pending || s_sync_waiting) return APP_REDRAW_NONE;
            time_open(); return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_POWER_SLEEP) {
        bool timed = app_settings_shutdown_minutes() != 0;
        if (timed) {
            for (unsigned i = 0; i < 3; ++i) {
                if (!ui_rect_hit(standby_duration_rect(i), ev->x0, y)) continue;
                app_settings_set_shutdown_style((app_shutdown_style_t)(APP_SHUTDOWN_LIGHT_10 + i));
                return APP_REDRAW_PAGE;
            }
        }
        for (int i = 0; i < 3; ++i) {
            EpdRect box = standby_mode_rect((unsigned)i, timed);
            if (!ui_rect_hit(box, ev->x0, y)) continue;
            if (i < 2)
                app_settings_set_shutdown_style((app_shutdown_style_t)(APP_SHUTDOWN_STAGED + i));
            else if (!timed)
                app_settings_set_shutdown_style(APP_SHUTDOWN_LIGHT_10);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_CONFIG) {
        if (y >= 561 && y < 644) {
            s_config_confirm = false;
            esp_err_t err = app_settings_backup_save();
            const char *bad = app_settings_backup_bad_bookmark();
            if (err != ESP_OK && *bad) {
                snprintf(s_notice, sizeof(s_notice), "书签损坏：%s", bad);
                return APP_REDRAW_PAGE;
            }
            snprintf(s_notice, sizeof(s_notice), "%s",
                     err == ESP_OK ? "已保存到 TF 卡根目录" :
                     err == ESP_ERR_INVALID_STATE ? "未识别到 TF 卡，请插卡后重试" :
                     "保存失败，请检查 TF 卡剩余空间");
            return APP_REDRAW_PAGE;
        }
        if (y >= 681 && y < 764) {
            if (!s_config_confirm) {
                s_config_confirm = true;
                snprintf(s_notice, sizeof(s_notice), "恢复将覆盖设置、合并 WiFi，请再次确认");
                return APP_REDRAW_PAGE;
            }
            s_config_confirm = false;
            esp_err_t err = app_settings_backup_restore();
            if (err == ESP_OK) {
                ttf_font_scan();
                app_font_activate_system();
                ui_text_set_system_scale(true);
                if (frontlight_ready())
                    (void)frontlight_apply(app_settings_frontlight_on(), app_settings_frontlight_brightness(),
                                           app_settings_frontlight_cct());
                // 阅读记录恢复后令首页和书架重新取进度、时长与排序。
                // Rebuild home and shelf caches after restoring reading records.
                book_store_notify_changed();
            }
            char saved_ssid[33] = {0};
            bool wifi_saved = false;
            if (err == ESP_OK)
                (void)wifi_transfer_get_saved_wifi(saved_ssid, &wifi_saved);
            snprintf(s_notice, sizeof(s_notice), "%s",
                     err == ESP_OK ? (wifi_saved ? "配置与阅读记录已恢复；WiFi 请重新连接" :
                                                   "配置与阅读记录已恢复") :
                     err == ESP_ERR_INVALID_STATE ? "未识别到 TF 卡，请插卡后重试" :
                     err == ESP_ERR_NOT_FOUND ? "未找到配置备份文件" :
                     err == ESP_ERR_INVALID_RESPONSE ? "配置文件损坏或版本不兼容" :
                     err == ESP_ERR_INVALID_SIZE ? "WiFi 合并超过 8 组，请先遗忘部分网络" :
                     "恢复未完成，请重试");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (y<160 || y>=UI_NAV_TOP || ev->x0<36 || ev->x0>=648) return APP_REDRAW_NONE;
    y+=s_main_scroll;
    if (y>=180 && y<180+settings_profile_height()) { s_page=SETTINGS_PROFILE;return APP_REDRAW_PAGE; }
    int row;
    const int wireless_rows = settings_wireless_rows();
    if (y>=SETTINGS_WIRELESS_Y && y<SETTINGS_WIRELESS_Y+wireless_rows*SETTINGS_ROW_H) {
        row=(y-SETTINGS_WIRELESS_Y)/SETTINGS_ROW_H;
        if(row==0) {
            extern const app_desc_t app_transfer;app_transfer_request_wifi_setup();ctx->request_app=&app_transfer;return APP_REDRAW_NONE;
        }
        network_status_t network;
        network_service_get_status(&network);
        if(network.cellular_capable && row==1) {
            s_page=SETTINGS_NETWORK;s_network_notice=NULL;return APP_REDRAW_PAGE;
        }
        const int bluetooth_row = network.cellular_capable ? 2 : 1;
        if(row==bluetooth_row) {
            s_page=SETTINGS_BLUETOOTH;settings_bluetooth_open();return APP_REDRAW_PAGE;
        }
        s_page=SETTINGS_TIME;return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_DISPLAY_Y && y<SETTINGS_DISPLAY_Y+SETTINGS_DISPLAY_ROWS*SETTINGS_ROW_H) {
        row=(y-SETTINGS_DISPLAY_Y)/SETTINGS_ROW_H;
        if(row==0) { ttf_font_scan();s_font_page=0;s_notice[0]=0;s_page=SETTINGS_SYSTEM_FONT; }
        else if(row==1)s_page=SETTINGS_SYSTEM_CONTRAST;
        else if(row==2)s_page=SETTINGS_MAIN_REFRESH;
        else if(row==3)app_settings_set_home_full_refresh(!app_settings_home_full_refresh());
        else if(row==4)s_page=SETTINGS_AUTO_LOCK;
        else if(row==5)s_page=SETTINGS_POWER_SLEEP;
        return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_PERSONAL_Y && y<SETTINGS_PERSONAL_Y+SETTINGS_PERSONAL_ROWS*SETTINGS_ROW_H) {
        row=(y-SETTINGS_PERSONAL_Y)/SETTINGS_ROW_H;
        if(row==0) {s_page=SETTINGS_SHELF_STYLE;s_notice[0]=0;}
        else if(row==1){ui_lock_style_open(ctx->fb);s_page=SETTINGS_LOCK_STYLE;}
        else if(row==2){s_page=SETTINGS_LOCK_PIN;s_notice[0]=0;}
        else if(row==3)profile_editor_open(true);
        else if(row==4){
            if(!haptics_available())return APP_REDRAW_NONE;
            s_page=SETTINGS_HAPTIC;
        }
        else if(row==5){
            reading_bg_scan();
            s_reading_bg_confirm=s_reading_bg_ready=s_reading_bg_pending=false;
            s_page=SETTINGS_READING_BG;
        }
        else if(row==6){app_components_open(ctx,false);return APP_REDRAW_NONE;}
        else if(row==7){s_page=SETTINGS_QUICK_BAR;s_quick_notice[0]=0;}
        return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_MAINTENANCE_Y && y<SETTINGS_MAINTENANCE_Y+SETTINGS_MAINTENANCE_ROWS*SETTINGS_ROW_H) {
        row=(y-SETTINGS_MAINTENANCE_Y)/SETTINGS_ROW_H;
        if(row==0){s_page=SETTINGS_CONFIG;s_config_confirm=false;s_notice[0]=0;}
        else if(row==1){
            // 屏幕在刷新中途断电会停在半帧，常规差分刷新永远盖不掉那道条纹；点一下就整屏盲刷
            // 一遍，然后自己把当前页面（含底栏）推回面板。
            // A power cut mid-refresh leaves the panel half-driven, and an ordinary differential
            // refresh can never cover that stripe. One tap runs the blind full-screen pass, which
            // then pushes the current page -- navigation bar included -- back onto the panel.
            s_panel_repair_pending=true;
            snprintf(s_notice,sizeof(s_notice),"屏幕修复中…");
            return APP_REDRAW_PAGE;
        }
        else if(row==2) upgrade_open();
        return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_POWER_Y && y<SETTINGS_POWER_Y+SETTINGS_POWER_ROWS*SETTINGS_ROW_H) {
        row=(y-SETTINGS_POWER_Y)/SETTINGS_ROW_H;
        if(row==0) {
            s_page=SETTINGS_BATT_CALIBRATION;
            s_batt_calibration_confirm=false;
            s_notice[0]=0;
            device_power_get_batt_calibration(&s_batt_calibration_status);
            s_batt_calibration_next_ms=ctx->now_ms+1000;
        }
        else if(row==1) s_page=SETTINGS_POWER_LOG;
        else if(row==2) { s_page=SETTINGS_CHARGE_CURRENT; s_notice[0]=0; }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    s_batt_calibration_confirm = false;
    if (s_ble_shortcut_back && s_page == SETTINGS_MAIN) s_ble_shortcut_back = false;
    if (s_haptics_shortcut_back && s_page == SETTINGS_MAIN) s_haptics_shortcut_back = false;
    if(s_font_sync_confirm) {
        s_font_sync_confirm=false;s_notice[0]=0;
        if(key!=1)return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_PIN_ENTRY) { pin_setup_close(); s_page = SETTINGS_LOCK_PIN; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_UPGRADE) {
        s_upgrade_job = UPGRADE_JOB_NONE;
        if (s_local_pending || s_upgrade_restart) return APP_REDRAW_NONE;
        pico_online_cancel_join(); s_upgrade_confirm = false;
        if (s_upgrade_shortcut_back && key != 1) { ctx->request_return = true; return APP_REDRAW_NONE; }
    }
    if (s_page == SETTINGS_BLUETOOTH) {
        if (key != 1 && settings_bluetooth_back()) return APP_REDRAW_PAGE;
        settings_bluetooth_close();
        if (key != 1 && s_ble_shortcut_back) { s_ble_shortcut_back = false; ctx->request_return = true; return APP_REDRAW_NONE; }
    }
    if (s_page==SETTINGS_LOCK_STYLE) {
        if(key!=1 && ui_lock_style_back())return APP_REDRAW_PAGE;
        ui_lock_style_close();s_page=SETTINGS_MAIN;
        if(key==1){ui_nav_request(ctx,0);return APP_REDRAW_NONE;}
        settings_prepare_profile();return APP_REDRAW_PAGE;
    }
    if (key == 1) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_page == SETTINGS_HAPTIC && s_haptics_shortcut_back) {
        s_haptics_shortcut_back = false;
        ctx->request_return = true;
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_MEMORY && s_memory_shortcut_back) {
        s_memory_shortcut_back = false;
        ctx->request_return = true;
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_MEMORY) {
        s_page = SETTINGS_MAIN;
        (void)wifi_transfer_get_saved_wifi(s_main_wifi, &s_main_wifi_saved);
        settings_prepare_profile();
        return APP_REDRAW_PAGE;
    }

    if (s_page == SETTINGS_READING_BG_PREVIEW) { s_reading_bg_confirm = false; s_page = SETTINGS_READING_BG; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_READING_BG) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_AVATAR) { s_page = SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_TEXT_EDIT) { ui_keyboard_end(); s_page = s_editor_signature ? SETTINGS_MAIN : SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_PROFILE) { s_page = SETTINGS_MAIN; (void)wifi_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_TIME_EDIT) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
    if (s_page != SETTINGS_MAIN) { s_page = SETTINGS_MAIN; (void)wifi_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
    ui_nav_request(ctx, 0);
    return APP_REDRAW_NONE;
}
static void settings_exit(app_ctx_t *ctx) {
    s_batt_calibration_confirm = false;
    ui_lock_style_close();

    // 释放预览内存；阅读器按当前路径读取TF成品，缓存失效时才重新解码。
    // Release preview RAM; the reader loads prepared SD art and decodes only stale/missing caches.
    ui_reader_background_release();
    reading_bg_thumbnails_release();
    free(s_wallpapers); s_wallpapers = NULL; s_wallpaper_capacity = 0;
    s_reading_bg_confirm=s_reading_bg_ready=s_reading_bg_pending=false;s_reading_bg_notice[0]=0;
    s_font_sync_confirm=false;s_font_sync_path[0]=0;
    pin_setup_close();
    s_upgrade_job = UPGRADE_JOB_NONE;
    (void)ctx; ui_keyboard_end(); pico_online_cancel_join(); settings_bluetooth_close();
}
static void on_before_lock(app_ctx_t *ctx) {
    s_batt_calibration_confirm = false;
    if(s_page==SETTINGS_LOCK_STYLE){ui_lock_style_close();s_page=SETTINGS_MAIN;}
    s_font_sync_confirm=false;s_font_sync_path[0]=0;
    // 锁屏唤醒后 tick 不会再补解码，预览页会停在白框：锁屏前退回主列表并释放缓存。
    // After waking from lock no tick re-runs the decode, so the preview page would sit on a
    // white frame: drop back to the main list and release the cache before locking.
    if (s_page == SETTINGS_READING_BG || s_page == SETTINGS_READING_BG_PREVIEW) {
        s_page = SETTINGS_MAIN;
        s_reading_bg_confirm=s_reading_bg_ready=s_reading_bg_pending=false;s_reading_bg_notice[0]=0;
        ui_reader_background_release();
        reading_bg_thumbnails_release();
    }
    if (s_page == SETTINGS_PIN_ENTRY) { pin_setup_close(); s_page = SETTINGS_LOCK_PIN; }
    s_upgrade_job = UPGRADE_JOB_NONE;
    (void)ctx; pico_online_cancel_join(); settings_bluetooth_close();
    (void)ble_pt_stop(2000);
}
static bool no_menu_handle(app_ctx_t *ctx) { (void)ctx; return false; }
static void settings_media_lost(app_ctx_t *ctx) {
    ui_lock_style_media_lost();
    // 拔卡后背景图片和列表都失效；退回主列表，避免停留在一个空预览。
    // Losing the card invalidates the image and the list; fall back to the main list instead
    // of staying on an empty preview.
    if (s_page == SETTINGS_READING_BG || s_page == SETTINGS_READING_BG_PREVIEW) {
        s_page = SETTINGS_MAIN;
        s_reading_bg_confirm=s_reading_bg_ready=s_reading_bg_pending=false;s_reading_bg_notice[0]=0;
        ui_reader_background_release();
        reading_bg_thumbnails_release();
    }
    (void)ctx; s_avatar_ready=s_avatar_pending=false; s_avatar_path[0]=0;
}
static void settings_media_ready(app_ctx_t *ctx) { (void)ctx; ui_lock_style_media_ready(); settings_prepare_profile(); }
static bool main_page_visible(app_ctx_t *ctx) { (void)ctx; return s_page == SETTINGS_MAIN; }
static bool settings_present(app_ctx_t *ctx, app_redraw_t redraw) {
    if (s_page == SETTINGS_PIN_ENTRY && (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL)) {
        display_main_transition_cancel(); render(ctx, ctx->fb);
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_DETAIL, ui_pinpad_full(), DISPLAY_REFRESH_DEFAULT));
        return true;
    }
    if (redraw == APP_REDRAW_AREA && s_page == SETTINGS_TEXT_EDIT) {
        if (s_input_settle) {
            guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_SETTLE, s_input_area, DISPLAY_REFRESH_DEFAULT));
            s_input_settle = false;
            return true;
        }
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, s_input_layout ? DISPLAY_REFRESH_DETAIL : DISPLAY_REFRESH_FEEDBACK, s_input_area, DISPLAY_REFRESH_DEFAULT));
        return true;
    }
    if (redraw == APP_REDRAW_AREA && s_page == SETTINGS_MEMORY && s_memory_present_pending) {
        s_memory_present_pending = false;
        memory_render_content(ctx->fb);
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_DETAIL,
            display_rect_to_epd(memory_area_hint(ctx)), DISPLAY_REFRESH_DEFAULT));
        return true;
    }
    if (redraw == APP_REDRAW_AREA && s_scroll_present_pending &&
        (s_page == SETTINGS_MAIN ||
         s_page == SETTINGS_SHELF_STYLE || s_page == SETTINGS_SYSTEM_FONT ||
         s_page == SETTINGS_AVATAR ||
         s_page == SETTINGS_READING_BG)) {
        s_scroll_present_pending = false;
        render(ctx, ctx->fb);
        int top = s_page == SETTINGS_MAIN ? 160 : s_page == SETTINGS_SHELF_STYLE ? 242 : 190;
        EpdRect area = {0, top, UI_LOCK_WIDTH, UI_NAV_REFRESH_END - top};
        // 只驱动变化像素，滚动次数不触发黑白清屏；题头与底栏保持稳定。
        // 脏区向外扩 1 行/1 字节，让差分跳过的白底邻像素重新走 GL16 白推，擦掉旧分隔线残影。
        // Drive changed pixels only; scroll counts never trigger a wipe, and header/nav stay stable.
        // Dilate the dirty masks by one row/byte so skipped white neighbours run the GL16
        // white tick again and old divider residue is erased.
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_MAIN_SCROLL, area, DISPLAY_REFRESH_DEFAULT));
        return true;
    }
    if (s_page != SETTINGS_UPGRADE) return false;
    if (redraw == APP_REDRAW_PAGE) {
        display_main_transition_cancel();
        render(ctx, ctx->fb);
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_DETAIL, (EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_REFRESH_END}, DISPLAY_REFRESH_DEFAULT));
        return true;
    }
    if (redraw == APP_REDRAW_AREA) {
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_FEEDBACK, upgrade_progress_area(), DISPLAY_REFRESH_DEFAULT));
        return true;
    }
    return false;
}

const app_desc_t app_device_settings = {
    .title = "设置 Settings", .detail = "显示、连接与设备", .enter_full = false,
    .owns_keys = true, .menu_handle_enabled = no_menu_handle,
    .main_page_visible = main_page_visible,
    .on_enter = on_enter, .on_exit = settings_exit, .on_before_lock = on_before_lock, .render = render, .on_tick = on_tick, .on_gesture = on_gesture, .on_key = on_key,
    .present = settings_present,
    .on_media_lost=settings_media_lost, .on_media_ready=settings_media_ready,
};
