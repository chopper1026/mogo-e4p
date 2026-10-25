/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实主循环，用确定性触摸和任务延时退出验证调度。
 * English: Run the real loop with deterministic touch and task-delay exit.
 */
#include "app_loop.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ui_power_dialog.h"
#include "ui_quick_menu.h"
#include "boot_state.h"
uint8_t app_settings_system_contrast(void) {return 100;}
void pico_boot_ready(void) {}
void app_book_cover_mode_changed(void) {}
void app_home_cover_mode_changed(void) {}
esp_err_t pico_boot_save_resume(const pico_resume_t *resume) { (void)resume; return ESP_OK; }
void pico_boot_clear_resume(void) {}
bool app_book_resume_context(char *p,size_t c,bool *f) { (void)p;(void)c;(void)f;return false; }
#include "read_pico_transfer.h"
#include "ble_page_turner.h"
#include "ui_nav_layout.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

void touch_sampler_test_enable(bool active);
void touch_sampler_test_push(uint32_t t_ms, uint16_t x, uint16_t y, uint8_t count);

const app_desc_t app_book = {.title="book"};
const app_desc_t app_transfer = {.title="transfer"};
static int power_open_draws, power_controls_draws, power_final_draws;
static int power_trigger_step = -1, shutdowns, restarts;
static bool staged_shutdown;
int64_t esp_timer_get_time(void);
static bool power_tap_during_draw;
static bool power_drag_during_draw;
static bool power_commit_during_draw;
static EpdRect last_du_area;
EpdRect ui_power_dialog_rect(void) {return (EpdRect){0,0,684,1216};}
void ui_power_dialog_draw(uint8_t* fb,const ui_power_dialog_t *dialog) {
 (void)fb;(void)dialog;++power_open_draws;
 // 电源弹窗展开的整屏 GL16 阻塞期间，用户快速点按“重启”；两帧都在队列里。
 // While the dialog's opening full-screen GL16 blocks, the user taps Restart fast.
 if (power_tap_during_draw) {
    power_tap_during_draw = false;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    touch_sampler_test_push(now, UI_POWER_RESTART_X, UI_POWER_BUTTON_Y, 1);
    touch_sampler_test_push(now + 10, 0, 0, 0);
 }
 // 展开期间积压一段快速拖动：按住拇指连扫三帧后在低位松手（不足提交位，且让
 // 下一场景从抬手状态开始）。
 // A fast drag buffered while the dialog opens: three sweeping frames on the thumb, released
 // below the commit line (and leaving the next scenario starting from a lifted finger).
 if (power_drag_during_draw) {
    power_drag_during_draw = false;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    touch_sampler_test_push(now, UI_POWER_THUMB_X, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 10, UI_POWER_THUMB_X + 30, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 20, UI_POWER_THUMB_X + 70, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 30, UI_POWER_THUMB_X + 120, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 40, UI_POWER_THUMB_X + 120, UI_POWER_THUMB_Y, 0);
 }
 // 展开期间积压"拖过提交位再松手"整段。/ A full drag-past-commit-and-release buffered while opening.
 if (power_commit_during_draw) {
    power_commit_during_draw = false;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    touch_sampler_test_push(now, UI_POWER_THUMB_X, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 10, UI_POWER_THUMB_X + 200, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 20, UI_POWER_THUMB_X + UI_POWER_SLIDE_RANGE, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 30, UI_POWER_THUMB_X + UI_POWER_SLIDE_RANGE, UI_POWER_THUMB_Y, 0);
 }
}
void ui_power_dialog_draw_controls(uint8_t* fb,const ui_power_dialog_t *dialog,EpdRect dirty) {(void)fb;(void)dialog;(void)dirty;++power_controls_draws;}
void ui_power_dialog_end(void) {}
void ui_power_final_draw(uint8_t* fb,bool restarting) {(void)fb;(void)restarting;++power_final_draws;}
// 与 ui_kit.c 同实现（桩里没有 clip_refresh_rect，宿主无刷新区裁剪）。
// Same shape as ui_kit.c (no clip_refresh_rect in the stub; the host has no refresh-area clip).
EpdRect ui_rect_union(EpdRect a,EpdRect b) {
 if(a.width<=0||a.height<=0)return b;
 if(b.width<=0||b.height<=0)return a;
 int x0=a.x<b.x?a.x:b.x,y0=a.y<b.y?a.y:b.y;
 int x1=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width;
 int y1=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;
 return (EpdRect){x0,y0,x1-x0,y1-y0};
}

static jmp_buf done;
typedef struct { int x, y, count, error; } sample_t;
static sample_t samples[32];
static int sample_count, step, ticks, enters, exits, touch_calls, keys[3], events[16];
static int menus, highlights, restores, fulls, mode, tick_consumed[32];
static int long_keys;
static uint8_t idle_minutes;
static int idle_locks, before_locks, lock_feedback_calls, lock_badges, wake_clean_requests;
static bool feedback_enabled;
static bool online_busy;
static bool ble_enabled, saved_wifi, upload_busy, click_enabled, click_active;
static read_pico_transfer_status_t transfer_status;
static int quick_draws, quick_gl, quick_du, wifi_starts, wifi_stops, ble_resets, click_presses, click_restores;

static app_redraw_t long_key(app_ctx_t* c, int k) { assert(k==UI_KEY_2); ++long_keys; c->request_menu=true; return APP_REDRAW_NONE; }
static app_redraw_t response;
static bool request_on_touch, request_on_tick, menu_on_tick, menu_on_touch, cancel_clobber;
static bool home_on_touch, home_on_tick, home_on_key, start_second;
static int home_enters, home_renders, last_menu_leaf, rendered_leaf;
static const app_desc_t* menu_background;
static app_desc_t first, second;
int ui_nav_hit(uint16_t x,uint16_t y){return y>=UI_NAV_TOUCH_TOP&&y<1216&&x<684?x/171:-1;}
void ui_nav_request(app_ctx_t *ctx,int index){ctx->request_app=index==0?&first:&second;}
static bool full_tick, lock_due, font_due, shake_activity;
static int64_t time_offset, time_step;
static int du_areas, gl_areas;
static int quick_button_draws,quick_status_draws,quick_press_draws,quick_recent_requests,base_renders;
static bool main_fast, main_water;
int app_settings_main_refresh_mode(void) {return main_water?APP_MAIN_REFRESH_WATER:main_fast?APP_MAIN_REFRESH_FAST:APP_MAIN_REFRESH_NORMAL;}
static int shelf_exit_arms;
static int nav_arms, nav_draws, nav_full_arms, nav_area_draws;
static const void *nav_armed;
static bool main_source_visible, main_target_visible;
static bool media_test, sd_font, saved_sd_font;
static bool mounted_steps[32], present_steps[32];
static int media_lost, media_ready, builtin_opens, font_opens, probes, remounts, loss_step;
static bool fl_ready, fl_on;
static uint8_t fl_bri=40, fl_cct=50;
static int fl_applies, fl_control_draws, fl_sweep_draws, fl_drag_pushes, fl_white_pushes, fl_gray_pushes, fl_key_gl, quick_gl_height;
int E0470_WAVEFORM;
int E0470_FOLLOW_WAVEFORM;
int E0470_CAPSULE_DRAG_WAVEFORM;
int E0470_CAPSULE_WHITE_WAVEFORM;
int E0470_CAPSULE_GRAY_WAVEFORM;
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {
    if(a.height==108&&a.width==108){
        assert(a.width==108&&a.y==102);
        if(m==MODE_GL16)++fl_key_gl;
        else {assert(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU);++quick_du;}
    }
    if(a.height==76)assert(a.x==0&&a.y==0&&a.width==684&&w==&E0470_WAVEFORM&&m==MODE_GL16);
    if (a.height==UI_QUICK_HEIGHT||a.height==UI_QUICK_FRONTLIGHT_HEIGHT) {assert(a.x==0&&a.y==0&&a.width==684);++quick_du;}
    if (a.y==1120) assert(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU&&a.height==48&&a.width==50);
    return update_display_area_with(h,w,m,a);
}
enum EpdDrawError update_display_area_diff_padded(EpdiyHighlevelState*h,const void*w,int m,EpdRect a,
                                                  int pad_lines,int pad_col_bytes) {
    if(a.width==604&&(a.y==UI_QUICK_FL_BRI_CY-UI_QUICK_FL_ROW_H/2||a.y==UI_QUICK_FL_CCT_CY-UI_QUICK_FL_ROW_H/2)){
        // 胶囊行只允许三张专用表：拖动近似、定稿洗白、定稿落灰；阈值 DU 把 to=9
        // 当纯白，绝不能出现。
        // Capsule rows only allow the three dedicated tables: approximate drag,
        // settle whitening and settle gray; the threshold DU treats to=9 as pure
        // white and must never appear.
        assert(m==MODE_DU&&pad_lines==1&&pad_col_bytes==1);
        if(w==&E0470_CAPSULE_DRAG_WAVEFORM)++fl_drag_pushes;
        else if(w==&E0470_CAPSULE_WHITE_WAVEFORM)++fl_white_pushes;
        else {assert(w==&E0470_CAPSULE_GRAY_WAVEFORM&&m==MODE_DU);++fl_gray_pushes;}
    }
    return update_display_area_diff_with(h,w,m,a);
}
enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState* hl,EpdRect area) {
 const void *owner=nav_armed;nav_armed=NULL;
 enum EpdDrawError result=update_display_area_diff_with(hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
 nav_armed=owner;return result;
}
static bool tap_during_present;
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {
    assert(a.x==0&&a.y==0&&a.width==684&&
           (a.height==UI_QUICK_HEIGHT||a.height==UI_QUICK_FRONTLIGHT_HEIGHT));++quick_gl;
    quick_gl_height=a.height;
    // 快捷层展开刷新阻塞期间，用户抬手并快速点按 Wi-Fi 圆键；三帧都在队列里。
    // While the quick layer's opening refresh blocks, the user lifts and taps Wi-Fi fast.
    if (tap_during_present) {
        tap_during_present = false;
        touch_sampler_test_push(30,0,0,0);
        touch_sampler_test_push(40,85,156,1);
        touch_sampler_test_push(50,0,0,0);
    }
    return update_display_area_with(h,w,m,a);
}
void ui_quick_menu_draw(uint8_t *fb,const ui_quick_menu_t *menu,bool wifi,bool bluetooth) {(void)fb;(void)menu;(void)wifi;(void)bluetooth;++quick_draws;}
void ui_quick_menu_end(void) {}
void ui_quick_menu_draw_status(uint8_t *fb) {(void)fb;++quick_status_draws;}
EpdRect ui_quick_menu_rect(const ui_quick_menu_t *menu) {
    return (EpdRect){0,0,684,menu->frontlight&&menu->fl_on?UI_QUICK_FRONTLIGHT_HEIGHT:UI_QUICK_HEIGHT};
}
bool ui_quick_menu_draw_control(uint8_t *fb,const ui_quick_menu_t *menu,
                                ui_quick_control_t control,EpdRect *dirty) {
    (void)fb;(void)menu;++fl_control_draws;
    if((control!=UI_QUICK_CTL_BRIGHTNESS&&control!=UI_QUICK_CTL_CCT)||!menu->fl_on)return false;
    *dirty=(EpdRect){UI_QUICK_FL_ROW_X, control==UI_QUICK_CTL_BRIGHTNESS
                     ?UI_QUICK_FL_BRI_CY-UI_QUICK_FL_ROW_H/2:UI_QUICK_FL_CCT_CY-UI_QUICK_FL_ROW_H/2,
                     UI_QUICK_FL_ROW_W, UI_QUICK_FL_ROW_H};
    return true;
}
bool ui_quick_menu_draw_control_white(uint8_t *fb,const ui_quick_menu_t *menu,
                                      ui_quick_control_t control,EpdRect *dirty) {
    (void)fb;(void)menu;++fl_sweep_draws;
    if((control!=UI_QUICK_CTL_BRIGHTNESS&&control!=UI_QUICK_CTL_CCT)||!menu->fl_on)return false;
    *dirty=(EpdRect){UI_QUICK_FL_ROW_X, control==UI_QUICK_CTL_BRIGHTNESS
                     ?UI_QUICK_FL_BRI_CY-UI_QUICK_FL_ROW_H/2:UI_QUICK_FL_CCT_CY-UI_QUICK_FL_ROW_H/2,
                     UI_QUICK_FL_ROW_W, UI_QUICK_FL_ROW_H};
    return true;
}
bool ui_quick_menu_draw_button(uint8_t *fb,const ui_quick_menu_t *menu,int button,bool active,bool pressed,EpdRect *dirty) {
 (void)fb;(void)menu;(void)active;assert(button>=0&&button<4);++quick_button_draws;quick_press_draws+=pressed;
 *dirty=(EpdRect){(button*2+1)*684/8-54,102,108,108};return true;
}
void ble_pt_reconnect_recent(void) {assert(ble_enabled);++quick_recent_requests;}
void ble_pt_cancel_recent(void) {assert(!ble_enabled);}
bool app_settings_ble_turner(void) {return ble_enabled;}
void app_settings_set_ble_turner(bool enabled) {ble_enabled=enabled;}
bool app_settings_frontlight_on(void) {return fl_on;}
void app_settings_set_frontlight_on(bool on) {fl_on=on;}
uint8_t app_settings_frontlight_brightness(void) {return fl_bri;}
void app_settings_set_frontlight_brightness(uint8_t percent) {fl_bri=percent;}
uint8_t app_settings_frontlight_cct(void) {return fl_cct;}
void app_settings_set_frontlight_cct(uint8_t percent) {fl_cct=percent;}
bool frontlight_ready(void) {return fl_ready;}
esp_err_t frontlight_apply(bool on,uint8_t brightness,uint8_t cct) {
    fl_on=on;fl_bri=brightness;fl_cct=cct;++fl_applies;return ESP_OK;
}
void ble_pt_reset_failure(void) {++ble_resets;}
bool ble_pt_pop_key(ble_pt_event_t *e) {(void)e;return false;}
bool ble_pt_pop_raw(ble_pt_raw_t *e) {(void)e;return false;}
void read_pico_transfer_get_status(read_pico_transfer_status_t *s) {*s=transfer_status;}
int read_pico_transfer_get_saved_wifi(char *ssid,bool *configured) {strcpy(ssid,"saved");*configured=saved_wifi;return ESP_OK;}
int read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg) {assert(cfg->mode==READ_PICO_TRANSFER_MODE_STA&&cfg->network_only);++wifi_starts;transfer_status.mode=cfg->mode;transfer_status.state=1;return ESP_OK;}
bool read_pico_transfer_try_stop_if_idle(void) {if(upload_busy)return false;++wifi_stops;transfer_status.state=0;return true;}
static int pulse_tab=-1,pulse_requests,pulse_draws;
void ui_click_feedback_reset(void) {click_active=false;pulse_tab=-1;}
void ui_click_feedback_begin(uint8_t *fb) {(void)fb;click_active=false;}
void ui_click_feedback_nav_request(int tab) {pulse_tab=tab;++pulse_requests;}
bool ui_click_feedback_nav_finish(uint8_t *fb,EpdRect *r) {(void)fb;(void)r;pulse_tab=-1;return false;}
bool ui_click_feedback_nav_pending(void) {return pulse_tab>=0;}
bool ui_click_feedback_nav_tick(uint8_t *fb,int64_t now_ms,EpdRect *r) {
 (void)fb;(void)now_ms;
 if(pulse_tab<0)return false;
 // 首姿态只要求当前画面存在，不依赖目标页面进入。/ A first pose requires the current frame, never destination entry.
 ++pulse_draws;pulse_tab=-1;
 *r=(EpdRect){60,1120,50,48};return true;
}
bool ui_click_feedback_active(void) {return click_active;}
bool ui_click_feedback_press(uint8_t *fb,int x,int y,EpdRect *r) {(void)fb;if(!click_enabled||y!=1141)return false;(void)x;*r=(EpdRect){60,1120,50,48};click_active=true;++click_presses;return true;}
bool ui_click_feedback_release(uint8_t *fb,EpdRect *r) {(void)fb;if(!click_active)return false;click_active=false;++click_restores;*r=(EpdRect){60,1120,50,48};return true;}
bool ui_click_feedback_cancel_at(int x,int y) {return x>120||y!=1141;}
int cst836u_read(void* h, cst836u_touch_t* t) {
    (void)h;
    sample_t s = samples[step];
    *t = (cst836u_touch_t){ .touched=s.count>0, .count=s.count, .x=s.x, .y=s.y };
    t->points[0].active=s.count>0;
    return s.error;
}
int cst836u_get_info(void* h,cst836u_info_t* i) {(void)h;(void)i;return 0;}
const char* esp_err_to_name(int e) {(void)e;return "test";}
int64_t esp_timer_get_time(void) {return time_offset+(int64_t)(step+1)*time_step;}
void vTaskDelay(int ms) {(void)ms;if (++step>=sample_count) longjmp(done,1);}
bool continuous_du_init(void) {return true;}
void display_request_full_refresh(void) {++wake_clean_requests;}
void guard_draw_result(EpdiyHighlevelState* h,enum EpdDrawError e) {(void)h;assert(e==0);}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {(void)h;(void)w;last_du_area=a;nav_area_draws+=nav_armed!=NULL;nav_armed=NULL;if(m==MODE_DU)du_areas++;else if(m==MODE_GL16)gl_areas++;return 0;}
enum EpdDrawError update_display_full(EpdiyHighlevelState*h) {(void)h;fulls++;nav_full_arms+=nav_armed!=NULL;nav_armed=NULL;return 0;}
enum EpdDrawError update_display_mode(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState*h) {(void)h;mode++;nav_draws+=nav_armed!=NULL;nav_armed=NULL;return 0;}
enum EpdDrawError update_display_white(EpdiyHighlevelState*h) {(void)h;return 0;}
bool display_take_white_exit(void) {return false;}
void display_main_transition_cancel(void) {nav_armed=NULL;}
void display_main_transition_arm(const void* owner, bool shelf_exit, bool changing_page) {(void)changing_page;display_main_transition_cancel();nav_armed=owner;++nav_arms;shelf_exit_arms+=shelf_exit;}
void display_main_transition_disarm(void) {nav_armed=NULL;}
void rails_idle_check(int64_t n) {(void)n;}
bool read_pico_pmu_ready(void) {return lock_due||step==power_trigger_step;}
read_pico_pmu_key_action_t read_pico_pmu_take_key_action(void) {return step==power_trigger_step?READ_PICO_PMU_KEY_LONG:READ_PICO_PMU_KEY_SHORT;}
void enter_lock_and_sleep(EpdiyHighlevelState*h,int64_t*t,void*a,void*tp,bool reader,bool light_only) {(void)tp;(void)h;(void)t;(void)a;(void)reader;(void)light_only;assert(!nav_armed);++idle_locks;lock_due=false;time_offset+=1000000;}
void app_lock_feedback(EpdiyHighlevelState*h) {(void)h;++lock_feedback_calls;if(feedback_enabled){assert(!before_locks&&!idle_locks);++lock_badges;}}
void app_lock_wait_key_idle(int ms) {(void)ms;}
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {assert(mode==APP_SLEEP_OFF);++shutdowns;longjmp(done,1);}
void app_restart_host(void) {++restarts;longjmp(done,1);}
void epd_poweroff(void) {}
const char* app_settings_font_path(void) {return "builtin";}
bool app_settings_reader_power_turn(void) {return false;}
app_shutdown_style_t app_settings_shutdown_style(void) {return staged_shutdown ? APP_SHUTDOWN_STAGED : APP_SHUTDOWN_FULL;}
bool ttf_font_path_is_builtin(const char*p) {(void)p;return !font_due&&!saved_sd_font;}
bool ttf_font_ready(void) {return true;}
bool ttf_font_is_builtin(void) {return !sd_font;}
const char* ttf_font_path(void) {return "other";}
int ttf_font_open(const char*p) {(void)p;font_opens++;sd_font=true;return 0;}
int ttf_font_open_builtin(void) {assert(media_lost>0);builtin_opens++;sd_font=font_due=false;return 0;}
int read_pico_sd_get_info(read_pico_sd_info_t*i) {i->present=media_test?present_steps[step]:font_due;i->mounted=media_test?mounted_steps[step]:font_due;return media_test&&!i->mounted?ESP_ERR_INVALID_STATE:0;}
void read_pico_sd_start_probe(void) {probes++;}
// 失效后卡回位的自动重挂：宿主用不到真实挂载，只记录调用。
// Auto-remount once a latched card returns; the host keeps the count without a real mount.
int read_pico_sd_remount(void) {++remounts;return ESP_ERR_NOT_FINISHED;}
static void lost(app_ctx_t*c) {(void)c;media_lost++;loss_step=step;}
static void ready(app_ctx_t*c) {(void)c;media_ready++;}
EpdRect ui_content_refresh_area(void) {return (EpdRect){0,0,684,1000};}
const app_desc_t* app_home_page(void) {return &first;}
const app_desc_t* app_at(int i) {return i==0?&first:i==1?&second:NULL;}
int app_index_of(const app_desc_t* app) {return app==&first?0:app==&second?1:-1;}
int ui_key_hit_test(uint16_t x,uint16_t y) {return y>=1300 && x<480?x/160:-1;}
bool ui_menu_handle_hit_test(uint16_t x,uint16_t y) {return x>600 && y>1100 && y<1300;}
int ui_menu_leaf_for_app(const app_desc_t*a) {return a==&second?1:0;}
int ui_menu_leaf_count(void) {return 2;}
void ui_draw_menu_page(uint8_t*f,const app_desc_t*a,int l) {(void)f;menu_background=a;last_menu_leaf=l;menus++;}
int ui_menu_hit_test(uint16_t x,uint16_t y,int l) {(void)l;if(x==550&&y==500)return UI_MENU_HIT_NEXT;return x<500&&y>=100&&y<300?(y-100)/100:-1;}
bool ui_menu_row_rect(int l,int r,EpdRect*out) {(void)l;*out=(EpdRect){0,r*100+100,500,100};return r>=0&&r<2;}
void ui_draw_menu_row_pressed(uint8_t*f,const app_desc_t*a,int l,int r,bool p) {(void)f;(void)a;(void)l;(void)r;if(p)highlights++;else restores++;}
static void enter(app_ctx_t*c) {(void)c;enters++;}
static void leave(app_ctx_t*c) {(void)c;exits++;}
static void render(app_ctx_t*c,uint8_t*f) {(void)c;(void)f;++base_renders;}
static void home_enter(app_ctx_t*c) {c->leaf=0;enters++;home_enters++;}
static void home_render(app_ctx_t*c,uint8_t*f) {(void)f;rendered_leaf=c->leaf;home_renders++;}
static app_redraw_t touch(app_ctx_t*c,const cst836u_touch_t*t) {
    (void)t;touch_calls++;assert(c->pressed&&c->consumed);
    if(request_on_touch)c->request_app=&second;
    if(menu_on_touch)c->request_menu=true;
    if(home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    events[e->type]++;
    if(e->type==UI_GESTURE_CANCEL&&cancel_clobber) {c->request_app=NULL;c->request_menu=true;c->request_return=false;}
    if(e->type==UI_GESTURE_PRESS&&request_on_touch)c->request_app=&second;
    if(e->type==UI_GESTURE_PRESS&&home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t key(app_ctx_t*c,int k) {keys[k]++;if(home_on_key)c->request_return=true;return APP_REDRAW_NONE;}
static app_redraw_t tick(app_ctx_t*c) {
    ticks++;tick_consumed[step]=c->consumed;
    if (shake_activity && step == 0) c->user_activity = true;
    if(request_on_tick)c->request_app=&second;
    if(menu_on_tick)c->request_menu=true;
    if(home_on_tick){c->request_return=true;home_on_tick=false;}
    return full_tick ? APP_REDRAW_FULL : APP_REDRAW_NONE;
}
static app_redraw_t consume_power(app_ctx_t*c) {(void)c;lock_due=false;return APP_REDRAW_DONE;}
static void reset(void) {
    pulse_tab=-1;pulse_requests=pulse_draws=0;
    quick_button_draws=quick_status_draws=quick_press_draws=quick_recent_requests=base_renders=0;
    power_open_draws=power_controls_draws=power_final_draws=shutdowns=restarts=0;power_trigger_step=-1;staged_shutdown=false;
    last_du_area=(EpdRect){0};
    ble_enabled=saved_wifi=upload_busy=click_enabled=click_active=false;
    transfer_status=(read_pico_transfer_status_t){0};
    quick_draws=quick_gl=quick_du=wifi_starts=wifi_stops=ble_resets=click_presses=click_restores=0;
    fl_ready=fl_on=false;fl_bri=40;fl_cct=50;fl_applies=fl_control_draws=fl_sweep_draws=fl_drag_pushes=fl_white_pushes=fl_gray_pushes=fl_key_gl=quick_gl_height=0;
    main_fast=main_water=false;shelf_exit_arms=0;idle_minutes=0;idle_locks=before_locks=lock_feedback_calls=lock_badges=wake_clean_requests=0;feedback_enabled=false;online_busy=false;
    nav_arms=nav_draws=nav_full_arms=nav_area_draws=0;nav_armed=NULL;
    main_source_visible=main_target_visible=true;
    media_test=sd_font=saved_sd_font=false;media_lost=media_ready=builtin_opens=font_opens=probes=remounts=0;loss_step=-1;
    memset(mounted_steps,0,sizeof(mounted_steps));memset(present_steps,0,sizeof(present_steps));
    long_keys=0;
    home_on_touch=home_on_tick=home_on_key=start_second=false;
    home_enters=home_renders=0;rendered_leaf=-1;last_menu_leaf=-1;menu_background=NULL;
    full_tick=lock_due=font_due=shake_activity=false;time_offset=0;time_step=10000;du_areas=gl_areas=0;
    memset(samples,0,sizeof(samples));memset(keys,0,sizeof(keys));memset(events,0,sizeof(events));memset(tick_consumed,0,sizeof(tick_consumed));
    sample_count=step=ticks=enters=exits=touch_calls=menus=highlights=restores=fulls=mode=0;
    request_on_touch=request_on_tick=menu_on_tick=menu_on_touch=cancel_clobber=false;response=APP_REDRAW_NONE;
    first=(app_desc_t){.title="first",.render=render,.on_enter=enter,.on_exit=leave,.on_touch=touch,.on_tick=tick,.on_key=key};
    second=(app_desc_t){.title="second",.render=render,.on_enter=enter};
}
static void add(int x,int y,int count,int error) {samples[sample_count++]=(sample_t){x,y,count,error};}
static void run(void) {app_loop_config_t cfg={.first_app=start_second?&second:&first};if(!setjmp(done))app_loop_run(&cfg);}
static app_redraw_t consumed_return_tick(app_ctx_t*c) { if(step==0){c->leaf=7;c->request_app=&second;}else c->request_return=true;return APP_REDRAW_NONE; }
static app_redraw_t return_tick(app_ctx_t*c) { c->request_return=true; return APP_REDRAW_NONE; }
static app_redraw_t origin_tick(app_ctx_t*c) { c->leaf=7; if(step==0 && request_on_tick)c->request_app=&second; return APP_REDRAW_NONE; }
static void home_case(void) {
    reset();start_second=true;
    second=first;second.title="transfer";
    first.on_enter=home_enter;first.render=home_render;first.on_tick=NULL;first.on_touch=NULL;
}
static bool product_menu_disabled(app_ctx_t *ctx) {(void)ctx;return false;}
static void before_lock(app_ctx_t *ctx) {(void)ctx;++before_locks;}
static bool main_first(app_ctx_t* ctx) {(void)ctx;return main_source_visible;}
static bool main_second(app_ctx_t* ctx) {(void)ctx;return main_target_visible;}
static void enter_subview(app_ctx_t* ctx) {enter(ctx);main_target_visible=false;}
static int root_key_taps;
static bool root_key_tap(app_ctx_t *ctx,int key,app_redraw_t *r){(void)ctx;assert(key==UI_KEY_2);++root_key_taps;*r=APP_REDRAW_NONE;return true;}
static app_redraw_t root_disappears(app_ctx_t *ctx){(void)ctx;if(step==2)main_source_visible=false;return APP_REDRAW_NONE;}
static void enable_main_tabs(void) {main_fast=true;first.main_page_visible=main_first;second.main_page_visible=main_second;}
static app_redraw_t nav_return_tick(app_ctx_t* ctx) {
    if(step==0)ctx->request_app=&second;
    else if(step==1)ctx->request_return=true;
    return APP_REDRAW_NONE;
}
static void enter_with_pending_tab(app_ctx_t* ctx){enter(ctx);ctx->request_app=&second;}
static app_redraw_t main_late_tick(app_ctx_t* ctx) {(void)ctx;return step == 1 ? response : APP_REDRAW_NONE;}
int main(void) {
    // 第一帧报点立即切页，尚未抬手也能响应；同次停留和释放不重复。
    // The first contact switches before lift; its subsequent hold and release never repeat navigation.
    for(int policy=0;policy<3;++policy) {
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,UI_NAV_TOUCH_TOP,1,0);run();assert(nav_draws==1&&enters==2&&!touch_calls&&pulse_requests==1&&pulse_draws==1);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,1080,1,0);add(232,1092,1,0);add(232,1092,1,0);add(0,0,0,0);run();
        assert(nav_draws==1&&enters==2&&!touch_calls&&pulse_requests==1&&pulse_draws==1);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,1080,1,0);add(0,0,0,0);add(85,1080,1,0);add(0,0,0,0);run();
        assert(nav_draws==2&&enters==3&&!touch_calls&&pulse_requests==2&&pulse_draws==2);
    }
    reset();enable_main_tabs();add(85,1080,1,0);add(0,0,0,0);run();assert(!nav_draws&&enters==1&&pulse_requests==1&&pulse_draws==1);
    reset();enable_main_tabs();first.on_gesture=gesture;add(200,1080,2,0);add(0,0,0,0);run();assert(!nav_draws);
    reset();enable_main_tabs();add(200,1080,1,-1);add(0,0,0,0);run();assert(!nav_draws);
    reset();enable_main_tabs();main_source_visible=false;first.on_gesture=gesture;
    add(200,1080,1,0);run();assert(!nav_draws&&enters==1);
    for(int policy=0;policy<3;++policy){
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(85,1080,1,0);add(0,0,0,0);add(85,1080,1,0);add(0,0,0,0);run();
        assert(!nav_draws&&enters==1&&pulse_requests==2&&pulse_draws==2);
    }
    // 待切另一页时点击当前页也必须反馈，并取消旧待切请求。
    // Tapping the active page while another tab is pending must give feedback and replace that pending request.
    for(int policy=0;policy<3;++policy){
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;first.on_enter=enter_with_pending_tab;
        add(85,1080,1,0);run();assert(enters==1&&!nav_draws&&pulse_requests==1&&pulse_draws==1);
    }
    puts("PASS: root tabs act on first contact in all modes, enlarged top edge, one-shot holds, rapid taps, active-tab no-op, subview/multitouch/read-error guards");
    reset();add(10,1141,1,0);
    app_ctx_t loading={.tp=(void*)1};
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=10,.y=1141,.count=1};
    assert(app_poll_main_navigation(&loading)&&loading.request_app==&first&&loading.user_activity);
    loading.request_app=NULL;samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading)); // current shelf, not a cancellation
    samples[0]=(sample_t){.x=10,.y=1141,.count=2};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=10,.y=1141,.count=1,.error=-1};assert(!app_poll_main_navigation(&loading));

    // 冷封面加载的轮询中，点当前书架也能反馈，保持不取消；同一接触不重复。
    // During cold-cover polling even the current shelf responds without cancelling; one contact never repeats.
    uint8_t loading_fb=0;EpdiyHighlevelState loading_hl={0};
    loading.fb=&loading_fb;loading.hl=&loading_hl;loading.request_app=NULL;
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    int loading_pulses=pulse_draws;
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading)&&pulse_draws==loading_pulses+1);
    assert(!app_poll_main_navigation(&loading)&&pulse_draws==loading_pulses+1);
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading)&&pulse_draws==loading_pulses+2);

    reset();add(100,400,1,0);add(110,400,1,0);add(0,0,0,0);run();
    assert(touch_calls==1&&ticks==3&&!tick_consumed[0]);
    reset();response=APP_REDRAW_AREA;add(100,400,1,0);run();assert(touch_calls==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;response=APP_REDRAW_AREA;add(100,400,1,0);add(0,0,0,0);run();
    assert(!touch_calls&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;add(100,400,1,0);add(0,0,0,9);add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();assert(enters==2&&exits==1&&ticks==0&&!menus);
    reset();request_on_tick=true;menu_on_tick=true;add(0,0,0,0);run();assert(enters==2&&exits==1&&ticks==1&&!menus);
    reset();first.on_gesture=gesture;request_on_touch=true;cancel_clobber=true;add(100,400,1,0);run();
    assert(enters==2&&events[UI_GESTURE_CANCEL]==1&&!menus);
    reset();menu_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();assert(menus==1&&ticks==1);
    reset();first.owns_keys=true;for(int k=0;k<3;k++){add(k*160+80,1500,1,0);add(0,0,0,0);}run();
    assert(keys[0]==1&&keys[1]==1&&keys[2]==1&&!menus&&fulls==1);
    reset();add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();assert(!keys[1]&&!keys[2]&&menus==1&&fulls==2);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==2&&exits==1);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==1&&!exits);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,9);run();assert(restores==1&&enters==1);
    reset();first.on_gesture=gesture;full_tick=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();first.on_gesture=gesture;font_due=true;time_offset=4000000;add(100,400,1,0);add(0,0,0,0);run();
    // 开机首帧前已加载保存字体，因此首个手势无需再被延迟字体切换取消。
    // The saved font is ready before the first frame, so the first gesture is no longer cancelled.
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&font_opens==1);
    reset();first.owns_keys=true;add(650,1150,1,0);add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(!keys[1]&&!keys[2]&&fulls==2&&menus==2);
    reset();add(650,1150,1,0);add(0,0,0,0);
    for(int i=0;i<3;i++){add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);}run();
    assert(du_areas==6&&gl_areas==1);
    reset();time_step=1000000;add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(du_areas==2&&gl_areas==1);
    reset();first.on_gesture=gesture;time_step=1000000;lock_due=true;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(!lock_due&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]&&tick_consumed[2]);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(keys[1]==1&&long_keys==1&&menus==1);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(0,0,0,0);run();assert(keys[1]==1&&!long_keys&&!menus);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(80,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(0,0,0,9);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=1000000;lock_due=true;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(!lock_due&&!long_keys);
    home_case();home_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&last_menu_leaf==1&&menu_background==&second);
    assert(home_renders==0&&ticks==1&&fulls==1&&mode==1);
    home_case();home_on_tick=true;add(0,0,0,0);add(400,1500,1,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0);
    home_case();home_on_touch=true;request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0&&ticks==0);
    home_case();second.on_gesture=gesture;home_on_touch=true;cancel_clobber=true;response=APP_REDRAW_PAGE;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&events[UI_GESTURE_CANCEL]==1&&home_renders==0);
    home_case();second.owns_keys=true;second.on_key_long=long_key;home_on_key=true;time_step=300000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&!long_keys&&ticks==0);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&first&&home_renders==1);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&rendered_leaf==7&&home_renders==2);
    reset();request_on_tick=true;first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&!menus&&home_renders==2&&rendered_leaf==7);
    reset();first.render=home_render;first.on_enter=home_enter;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==4&&enters==5&&last_menu_leaf==1&&menu_background==&first&&home_renders==2&&rendered_leaf==7);
    reset();start_second=true;second.on_tick=origin_tick;second.on_exit=leave;second.on_enter=home_enter;second.render=home_render;first.on_tick=return_tick;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(100,120,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&second&&home_renders==2&&rendered_leaf==7);
    reset();first.on_tick=consumed_return_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&menus==1&&last_menu_leaf==0&&menu_background==&first);
    // 拔卡先关资源再回退字体，取消本轮按键，持续无卡不重复通知。
    // Removal closes consumers before fallback, drops the current key and notifies only once.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(80,1500,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==1&&builtin_opens==1&&!keys[0]&&fulls==2);
    // 后台页即使被菜单遮盖也释放资源，仍保留菜单视图。
    // A menu-covered page still releases resources and retains the menu view.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(650,1150,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&menus==2&&ticks==0);
    // 延迟挂载后拔卡同样被捕获，使用内置字体无需再打开一次。
    // Removal after a late mount is detected without reopening an already built-in font.
    reset();media_test=true;mounted_steps[1]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==2&&!builtin_opens);
    // 首次无卡时后插卡应触发探测，挂载完成只通知页面一次。
    // First insertion after an empty boot probes, then notifies the active page once.
    reset();media_test=true;time_step=600000;first.on_media_ready=ready;
    present_steps[1]=present_steps[2]=present_steps[3]=true;
    mounted_steps[2]=mounted_steps[3]=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(probes>=1&&media_ready==1&&!media_lost);
    // 手势和长按在拔卡边界取消，不能把旧按住状态变成新的动作。
    // Removal cancels gestures and holds without turning an old contact into another action.
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.on_gesture=gesture;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.owns_keys=true;first.on_key_long=long_key;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(media_lost==1&&keys[1]==1&&!long_keys);
    // 失效后不自动探测；显式恢复的挂载可重新加载保存字体并再次检测拔卡。
    // No automatic probing after invalidation; an explicit remount restores saved-font loading and loss detection.
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&!probes&&!font_opens);
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=mounted_steps[2]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==2&&builtin_opens==2&&!probes&&font_opens==1);
    // 失效后卡回到位自动重挂一次，挂载完成后只通知页面一次。
    // A latched card that returns remounts once; completion notifies the page exactly once.
    reset();media_test=sd_font=true;mounted_steps[0]=true;present_steps[2]=present_steps[3]=true;
    mounted_steps[3]=true;time_step=1000000;first.on_media_lost=lost;first.on_media_ready=ready;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&remounts==1&&media_ready==1);
    reset();idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&before_locks==1&&wake_clean_requests==1); // Real scheduler flushes before automatic lock.
    reset();idle_minutes=1;time_step=60000000;shake_activity=true;
    add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=60000000;shake_activity=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(idle_locks==1&&wake_clean_requests==1); /* Activity is consumed once, not latched. */
    reset();idle_minutes=0;time_step=600000000;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=600000000;online_busy=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=600000000;first.holds_pmu=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=30000000;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();first.owns_keys=true;first.on_gesture=gesture;first.menu_handle_enabled=product_menu_disabled;
    add(636,1150,1,0);add(0,0,0,0);run();
    assert(!menus&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1);
    // 首帧保持全刷；切页一次完成，按住、松开、空闲都不安排第二次刷新。
    // Retain the initial full update; finish navigation once without another draw on hold, release or idle.
    reset();enable_main_tabs();add(0,0,0,0);run();assert(!nav_arms&&!nav_draws&&!nav_full_arms);
    reset();enable_main_tabs();request_on_touch=true;add(100,400,1,0);add(100,400,1,0);run();
    assert(nav_arms==1&&nav_draws==1&&!nav_armed);
    reset();enable_main_tabs();request_on_touch=true;add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_arms==1&&nav_draws==1&&!nav_armed&&!nav_full_arms);
    reset();enable_main_tabs();first.on_tick=nav_return_tick;add(0,0,0,0);add(0,0,0,0);second.on_tick=return_tick;add(0,0,0,0);run();
    assert(nav_arms==2&&nav_draws==2&&!nav_armed&&shelf_exit_arms==1);
    // 目录延后完成重绘和同页局部更新也获得固定底栏许可，没有额外 idle 推屏。
    // Deferred directory completion and local repaint receive frozen-navigation permission without an idle redraw.
    reset();enable_main_tabs();first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_PAGE;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==2&&nav_arms==2&&!nav_armed&&!nav_full_arms);
    reset();enable_main_tabs();first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_AREA;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==1&&nav_area_draws==1&&nav_arms==2&&!nav_armed&&!nav_full_arms);
    // 子视图、非主页面保留普通出口，显式全刷优先。
    // Subviews and non-main pages retain ordinary output; explicit full refresh has priority.
    reset();enable_main_tabs();main_source_visible=false;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.on_enter=enter_subview;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.main_page_visible=NULL;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.enter_full=true;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(nav_arms==1&&nav_full_arms==1&&!nav_draws);
    reset();enable_main_tabs();main_fast=false;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(nav_arms==1&&nav_draws==1&&!shelf_exit_arms&&!nav_full_arms);
    reset();enable_main_tabs();main_fast=false;first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_AREA;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==1&&nav_area_draws==1&&nav_arms==2&&!nav_full_arms);
    reset();enable_main_tabs();lock_due=true;time_step=3000000;add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&!nav_arms&&!nav_draws);
    puts("main-tab scheduler: single immediate draw, no hold/release/idle redraw, rapid return, root/subview boundaries and full-output priority passed");
    // 跑真实边缘识别和主循环，验证不会漏给底层页面或按住重复动作。
    // Run the real edge recognizer and loop to reject underlay leakage and repeated hold actions.
    reset();first.on_gesture=gesture;
    add(300,20,1,0);add(300,110,1,0);add(300,120,1,0);add(0,0,0,0);
    add(300,500,1,0);add(0,0,0,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(quick_draws==1&&quick_gl==2&&!quick_du);
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1);
    reset();main_fast=true;saved_wifi=true;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(85,156,1,0);add(85,156,1,0);add(0,0,0,0);
    add(85,156,1,0);add(0,0,0,0);run();
    assert(wifi_starts==1&&wifi_stops==1&&quick_gl==1&&quick_du==4&&quick_draws==1&&base_renders==2&&quick_press_draws==2&&!touch_calls);
    reset();upload_busy=true;transfer_status=(read_pico_transfer_status_t){.state=1,.mode=READ_PICO_TRANSFER_MODE_STA};
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);add(0,0,0,0);run();
    assert(!wifi_starts&&!wifi_stops&&transfer_status.state==1);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);add(0,0,0,0);run();
    assert(!wifi_starts&&!wifi_stops&&!touch_calls);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(256,156,1,0);add(0,0,0,0);add(256,156,1,0);add(0,0,0,0);run();
    assert(!ble_enabled&&quick_recent_requests==1&&!ble_resets&&quick_gl==1&&quick_draws==1&&quick_status_draws==2&&quick_press_draws==2&&base_renders==2&&!touch_calls);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(427,156,1,0);add(0,0,0,0);run();
    assert(fulls==2&&base_renders==2&&quick_draws==1&&quick_du==1&&quick_press_draws==1);
    reset();first.on_before_lock=before_lock;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(427,156,1,0);add(0,0,0,0);add(598,156,1,0);add(0,0,0,0);run();
    assert(idle_locks==1&&before_locks==1&&fulls>=2&&!touch_calls);
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;first.on_gesture=gesture;click_enabled=true;
        add(85,1141,1,0);add(85,1141,1,0);add(0,0,0,0);run();
        assert(click_presses==1&&click_restores==1&&!click_active&&ticks==1&&du_areas==2&&!gl_areas);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;click_enabled=true;request_on_touch=true;
        add(200,1141,1,0);add(0,0,0,0);run();
        assert(!click_presses&&!click_restores&&!click_active&&du_areas==1&&!gl_areas&&nav_draws==1&&pulse_requests==1&&pulse_draws==1);
    }
    reset();click_enabled=true;add(85,1141,1,0);add(85,1141,2,0);add(0,0,0,0);run();
    assert(click_presses==1&&click_restores==1&&!click_active);
    reset();click_enabled=true;add(85,1141,1,0);add(0,0,0,1);add(0,0,0,0);run();
    assert(click_presses==1&&click_restores==1&&!click_active);
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;saved_wifi=true;
        add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);run();
        assert(quick_press_draws==1&&quick_du==1&&quick_gl==1&&quick_draws==1&&base_renders==2&&!wifi_starts);
        reset();first.on_gesture=gesture;
        add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);
        add(85,156,2,0);add(0,0,0,0);run();
        assert(quick_press_draws==1&&quick_du==2&&quick_gl==1&&quick_draws==1&&!wifi_starts&&!touch_calls);
        reset();first.on_gesture=gesture;
        add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);
        add(85,156,0,1);add(0,0,0,0);run();
        assert(quick_press_draws==1&&quick_du==2&&quick_gl==1&&!wifi_starts&&!touch_calls);
    }
    puts("quick layer/click scheduler: modal ownership, local BW/gray, immediate local presses without underlay re-render/glass regeneration, one saved-network connect/disconnect, upload/no-credential guards, BLE on/recent request and off before connection, full/lock and release/multitouch/read-error restoration passed");
    // 第四枚圆钮是前光开关：开灯才展开胶囊滑杆并整层重排到 480，拖动帧与松手
    // 提交都走胶囊拖动表（不再两段定稿，白表/灰表零推送），熄灯收回 264，收层
    // 按 480 高度擦除；开关点按不再单独重画圆钮。
    // The fourth circle is the frontlight switch: the capsules expand with a
    // whole-layer re-composite to 480, drag frames and the release commit both
    // use the capsule drag table (no two-stage settle; zero white/gray pushes),
    // switching off returns to 264 and closing erases by 480px; the switch tap
    // no longer repaints its circle alone.
    reset();fl_ready=true;fl_on=false;fl_bri=40;fl_cct=50;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(598,156,1,0);add(0,0,0,0);
    add(399,300,1,0);add(512,300,1,0);add(0,0,0,0);
    add(399,398,1,0);add(0,0,0,0);
    add(300,500,1,0);add(300,300,1,0);add(0,0,0,0);run();
    assert(fl_on&&fl_bri==78&&fl_cct==59);
    assert(quick_gl_height==UI_QUICK_FRONTLIGHT_HEIGHT);
    assert(!fl_key_gl&&fl_control_draws==5&&fl_sweep_draws==0&&fl_drag_pushes==5&&fl_white_pushes==0&&fl_gray_pushes==0);
    // 展开与开关点按各重排一次并重画一层；收起时层已关闭，只重排底层页面。
    // Opening and the switch tap each re-composite and redraw the layer; on close the
    // layer is already gone, so only the underlay page is re-rendered.
    assert(quick_draws==2&&quick_gl==3&&base_renders==4);
    puts("frontlight: fourth-circle switch expands the capsule card, slider rows, DU drag, drag-table commit on release and 480px close passed");
    // 采样回放：快捷层展开的整页刷新期间快速点按，按下沿不能丢；一次点按只切换一次。
    // Sampler replay: a fast tap during the quick layer's opening refresh keeps its
    // press edge and toggles exactly once.
    reset();saved_wifi=true;first.on_gesture=gesture;
    touch_sampler_test_enable(true);
    touch_sampler_test_push(10,300,20,1);
    touch_sampler_test_push(20,300,110,1);
    tap_during_present=true;
    run();
    assert(wifi_starts==1&&!wifi_stops&&!touch_calls&&quick_gl==1&&quick_draws==1);
    touch_sampler_test_enable(false);tap_during_present=false;
    puts("quick layer replay: a tap buffered during the opening refresh keeps its press edge");
    // 反馈必须早于页面保存/后台收尾；下一页与自动休眠不显示主动锁屏徽标。
    // Acknowledgement precedes saves/joins; consumed page turns and idle sleep omit it.
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;feedback_enabled=true;
        first.on_before_lock=before_lock;lock_due=true;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);run();
        assert(lock_badges==1&&lock_feedback_calls==1&&before_locks==1&&idle_locks==1);
        reset();feedback_enabled=true;first.on_power_short=consume_power;lock_due=true;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);run();assert(!lock_badges&&!lock_feedback_calls&&!before_locks&&!idle_locks);
        reset();feedback_enabled=true;idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
        add(0,0,0,0);add(0,0,0,0);run();assert(idle_locks==1&&before_locks==1&&!lock_feedback_calls&&!lock_badges);
    }
    puts("PASS: manual lock acknowledgement precedes page save/reader joins in all three modes; consumed power turns and auto sleep omit the badge");
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_THUMB_X,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+112,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,1,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&power_controls_draws==2&&shutdowns==1&&!restarts&&power_final_draws==1&&!touch_calls&&ticks==1&&du_areas==2&&gl_areas==1);
    /* 「彻底关机」档不再改变「关机」按钮：任何档位都立即断电。/ The full-off style no longer rebinds 关机: every style powers off immediately. */
    reset();power_trigger_step=1;time_step=3000000;staged_shutdown=true;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_THUMB_X,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&shutdowns==1&&power_final_draws==1&&!restarts&&!idle_locks&&!wake_clean_requests);
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_RESTART_X,UI_POWER_BUTTON_Y,1,0);add(0,0,0,0);run();
    assert(restarts==1&&!shutdowns&&power_final_draws==1);
    reset();enable_main_tabs();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_CANCEL_X,UI_POWER_BUTTON_Y,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!shutdowns&&!restarts&&!power_final_draws&&nav_arms==0&&mode==1);
    for(int fault=0;fault<2;++fault){
        reset();power_trigger_step=1;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(UI_POWER_THUMB_X,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,fault?2:0,fault?0:1);add(0,0,0,0);run();
        assert(!shutdowns&&!restarts&&power_controls_draws==2&&!touch_calls);
    }
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(UI_POWER_THUMB_X,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,1,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&!power_controls_draws&&!shutdowns&&!restarts);
    puts("PASS: real loop and power recognizer: DU-only contact, release commits off/restart once in every shutdown style, cancel restores full navigation, touch/multitouch errors and entry-held suppression");
    // 采样回放：电源弹窗展开的整屏刷新期间快速点按“重启”，按下沿不能丢。
    // Sampler replay: a fast Restart tap during the dialog's opening refresh keeps its press edge.
    reset();power_trigger_step=1;time_step=3000000;
    touch_sampler_test_enable(true);power_tap_during_draw=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(restarts==1&&!shutdowns&&power_final_draws==1&&!touch_calls);
    touch_sampler_test_enable(false);power_tap_during_draw=false;
    puts("power dialog replay: a tap buffered during the opening refresh keeps its press edge");
    // 采样回放：拖动期间积压的多帧必须合并成一次控件重绘和一次 DU 推屏，合并区覆盖整段
    // 轨迹；逐帧推屏（旧行为）会在 5ms 采样对 55–90ms 扫描下积压，滑块越拖越落后。
    // Sampler replay: frames buffered during a drag must merge into one controls redraw and one
    // DU push whose area spans the whole sweep; per-frame pushes (old behavior) backlog at 5 ms
    // sampling against 55-90 ms scans and the slider falls further behind.
    reset();power_trigger_step=1;time_step=3000000;
    touch_sampler_test_enable(true);power_drag_during_draw=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    touch_sampler_test_enable(false);
    assert(power_open_draws==1&&gl_areas==1&&power_controls_draws==1&&du_areas==1&&
           !shutdowns&&!restarts&&power_final_draws==0&&
           last_du_area.x==UI_POWER_THUMB_X-UI_POWER_THUMB_R-1&&
           last_du_area.width==120+2*UI_POWER_THUMB_R+3);
    puts("power dialog replay: buffered drag frames merge into one push covering the sweep");
    // 拖动到提交位后松手也积压在回放里：动作延迟执行一次，不再为中途轨迹浪费推屏。
    // A release past the commit line buffered in the same replay: the action runs once and no
    // intermediate trajectory push is wasted.
    reset();power_trigger_step=1;time_step=3000000;
    touch_sampler_test_enable(true);power_commit_during_draw=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    touch_sampler_test_enable(false);
    assert(shutdowns==1&&power_final_draws==1&&!restarts&&du_areas==0&&gl_areas==1&&power_controls_draws==0);
    puts("power dialog replay: buffered release past the commit line still powers off once");
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;root_key_taps=0;time_step=100000;
    add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==1&&!keys[1]);
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;root_key_taps=0;time_step=300000;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==0&&fulls==2&&!keys[1]);
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;first.on_tick=root_disappears;root_key_taps=0;time_step=100000;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==0&&!keys[1]);
    puts("app_loop: 50 scheduler scenarios passed, including automatic lock, save callback, input reset and busy guards");
    return 0;
}

/* 中文：硬件 BLE 与联网任务替身。/ English: Hardware BLE and online worker shims. */
void ble_pt_sync(bool enabled) { (void)enabled; }
void ble_pt_poll(void) {}
bool pico_online_busy(void) { return online_busy; }

uint8_t app_settings_auto_lock_minutes(void){return idle_minutes;}
uint32_t ble_pt_input_serial(void){return 0;}


void crash_note_log(void){}

void app_time_service_poll(bool idle) { (void)idle; }
void app_time_service_cancel(void) {}

// 开机分段计时只写崩溃日志，替身忽略；本页不验证它。/ Boot-phase marks only feed the crash log; the shim ignores them.
void rp_boot_mark(const char *name) { (void)name; }

void ui_avatar_invalidate(void) {}
bool book_cover_jobs_quiesce(unsigned timeout){(void)timeout;return true;}
void book_lock_collage_invalidate(void) {}

bool ble_pt_connected(void) { return false; }
