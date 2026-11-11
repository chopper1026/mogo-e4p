/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: Apache-2.0
 * 中文：只替换蓝牙/设置/网络服务，真实控制器与原生绘图由 Python 入口编译。
 * English: Stub BLE/settings/network services; Python compiles the real controller and native drawing.
 */
#include <strings.h>
static bool enabled, running, connected, connecting, scanning, wifi_busy, save_error, scan_error;
static int connect_error, bind_error, mutations, scan_starts, scan_stops, disconnects, connects, forgets, binds, learn_begins;
static int failure_resets, recent_cancels, raw_count, raw_at, key_count;
static char address[18], peer_name[32], target[18], forgotten[18], failure[96];
static ble_pt_bond_t bonds[BLE_PT_MAX_BONDS];
static ble_pt_device_t devices[BLE_PT_MAX_DEVICES];
static ble_pt_raw_t reports[8];
static uint8_t bond_count, device_count;
static bool bindings[2];
enum {WIFI_TRANSFER_STOPPED, WIFI_TRANSFER_ACTIVE};
typedef struct {int state;} wifi_transfer_status_t;
static void wifi_transfer_get_status(wifi_transfer_status_t *out){out->state=wifi_busy?WIFI_TRANSFER_ACTIVE:WIFI_TRANSFER_STOPPED;}
static bool app_settings_ble_turner(void){return enabled;}
static void app_settings_set_ble_turner(bool value){++mutations;if(!save_error)enabled=value;}
bool ble_pt_running(void){return running;}
bool ble_pt_connected(void){return connected;}
bool ble_pt_connecting(void){return connecting;}
bool ble_pt_scanning(void){return scanning;}
const char *ble_pt_connected_addr(void){return connected?address:"";}
const char *ble_pt_connected_name(void){return connected?peer_name:"";}
uint8_t ble_pt_bond_count(void){return bond_count;}
const ble_pt_bond_t *ble_pt_bond(uint8_t i){return i<bond_count?&bonds[i]:NULL;}
uint8_t ble_pt_device_count(void){return device_count;}
const ble_pt_device_t *ble_pt_device(uint8_t i){return i<device_count?&devices[i]:NULL;}
void ble_pt_scan_start(uint32_t duration){assert(duration==BLE_PT_SCAN_FOREVER&&!connected&&!connecting);++mutations;++scan_starts;if(!scan_error){scanning=true;device_count=0;}}
void ble_pt_scan_stop(void){++mutations;++scan_stops;/* completion remains asynchronous */}
void ble_pt_disconnect(void){++mutations;++disconnects;/* link remains until callback */}
void ble_pt_cancel_recent(void){++mutations;++recent_cancels;}
void ble_pt_reset_failure(void){++mutations;++failure_resets;}
esp_err_t ble_pt_connect(const char *addr){++mutations;++connects;snprintf(target,sizeof(target),"%s",addr);if(!connect_error)connecting=true;return connect_error;}
void ble_pt_forget(const char *addr){
    ++mutations;++forgets;snprintf(forgotten,sizeof(forgotten),"%s",addr);
    for(int i=0;i<bond_count;++i)if(!strcmp(addr,bonds[i].addr)){
        memmove(&bonds[i],&bonds[i+1],sizeof(bonds[0])*(bond_count-i-1));--bond_count;break;
    }
}
bool ble_pt_take_start_failure(char *out,size_t cap){(void)out;(void)cap;return false;}
bool ble_pt_take_failure(char *out,size_t cap){if(!failure[0])return false;snprintf(out,cap,"%s",failure);failure[0]=0;++mutations;return true;}
bool ble_pt_pop_key(ble_pt_event_t *out){if(!key_count)return false;memset(out,0,sizeof(*out));--key_count;++mutations;return true;}
bool ble_pt_pop_raw(ble_pt_raw_t *out){if(raw_at==raw_count)return false;*out=reports[raw_at++];++mutations;return true;}
void ble_pt_learn_begin(void){assert(raw_at==raw_count&&!key_count);++mutations;++learn_begins;}
esp_err_t ble_pt_bind(ble_pt_action_t action,const ble_pt_frame_t *frame){
    assert(connected&&frame->len&&action>=BLE_PT_ACTION_PREV&&action<=BLE_PT_ACTION_NEXT);
    ++mutations;++binds;if(!bind_error)bindings[action-1]=true;return bind_error;
}
bool ble_pt_binding(ble_pt_action_t action,ble_pt_frame_t *out){(void)out;return bindings[action-1];}

// PRODUCTION_CONTROLLER
static void seed_bonds(int n){
    bond_count=n;memset(bonds,0,sizeof(bonds));
    const char *names[]={"Page Turner","阅读翻页器","Mini Remote","Bluetooth Remote"};
    for(int i=0;i<n;++i){snprintf(bonds[i].addr,sizeof(bonds[i].addr),"7C:91:22:35:A0:%02d",18+i);snprintf(bonds[i].name,sizeof(bonds[i].name),"%s",names[i]);}
}
static void seed_devices(int n){
    device_count=n;memset(devices,0,sizeof(devices));
    const char *names[]={"Page Turner","Bluetooth Remote","阅读翻页器","BT Page Turner","Mini Remote","Wireless Keyboard"};
    for(int i=0;i<n;++i){snprintf(devices[i].addr,sizeof(devices[i].addr),"AA:BB:CC:DD:EE:%02d",i);snprintf(devices[i].name,sizeof(devices[i].name),"%s",names[i%6]);devices[i].has_name=true;devices[i].rssi=-48-i*2;devices[i].bonded=i<3;}
}
static void link_peer(int i){connected=true;connecting=false;snprintf(address,sizeof(address),"%s",bonds[i].addr);snprintf(peer_name,sizeof(peer_name),"%s",bonds[i].name);}
static void reset(void){
    enabled=running=true;connected=connecting=scanning=wifi_busy=save_error=scan_error=false;
    connect_error=bind_error=mutations=scan_starts=scan_stops=disconnects=connects=forgets=binds=learn_begins=0;
    failure_resets=recent_cancels=raw_count=raw_at=key_count=device_count=0;
    address[0]=peer_name[0]=target[0]=forgotten[0]=failure[0]=0;memset(bindings,0,sizeof(bindings));
    seed_bonds(3);settings_bluetooth_open();
    s_page=SETTINGS_BLUETOOTH;s_ble_shortcut_back=false;requested_tab=-1;
}
static bool tap(int x,int y){ui_gesture_event_t ev={.type=UI_GESTURE_TAP,.x0=x,.y0=y};return settings_bluetooth_gesture(&ev);}
static bool press(ble_control_t id){EpdRect r=control_rect(id);return tap(r.x+r.width/2,r.y+r.height/2);}
static void queue_press(void){raw_count=1;raw_at=0;reports[0]=(ble_pt_raw_t){.pressed=true,.frame={.len=2,.bytes={6,0xb1}}};key_count=1;}
static void enter_scan(void){assert(press(CONTROL_ADD));assert(s_view==BLE_SCAN);assert(settings_bluetooth_tick(1));assert(scan_starts==1);}
static void check_workflows(void){
    reset();save_error=true;assert(press(CONTROL_TOGGLE)&&enabled&&strstr(s_ble_notice,"保存失败"));
    assert(!failure_resets);save_error=false;assert(press(CONTROL_TOGGLE)&&!enabled&&bond_count==3&&failure_resets==1&&recent_cancels==1);
    assert(!press(CONTROL_ADD)&&!press(CONTROL_LEARN_PREV));assert(press(CONTROL_TOGGLE)&&enabled&&failure_resets==1);
    wifi_busy=true;assert(settings_bluetooth_tick(20));assert(!press(CONTROL_ADD));

    reset();link_peer(0);settings_bluetooth_tick(1);assert(press(CONTROL_ADD)&&s_view==BLE_SCAN_CONFIRM&&!disconnects&&!scan_starts);
    assert(press(CONTROL_CANCEL)&&s_view==BLE_HOME&&!disconnects);
    press(CONTROL_ADD);press(CONTROL_CONFIRM);assert(s_view==BLE_SCAN&&disconnects==1&&!scan_starts);
    settings_bluetooth_tick(2);settings_bluetooth_tick(3);assert(disconnects==1&&!scan_starts);
    connected=false;settings_bluetooth_tick(4);assert(scan_starts==1);
    seed_devices(24);devices[2].has_name=false;settings_bluetooth_tick(1005);assert(s_model.device_count==23);
    assert(!press(CONTROL_SCAN_PREV));press(CONTROL_SCAN_NEXT);press(CONTROL_SCAN_NEXT);press(CONTROL_SCAN_NEXT);
    assert(s_scan_page==3&&!press(CONTROL_SCAN_NEXT));
    // Same-name peers remain distinct; a late scan callback cannot change what the user tapped.
    char chosen[18];snprintf(chosen,sizeof(chosen),"%s",s_model.devices[18].addr);
    seed_devices(1);assert(tap(300,474)&&s_view==BLE_HOME&&connects==1&&scan_stops==1&&!strcmp(target,chosen));
    reset();seed_devices(2);enter_scan();assert(!s_model.device_count);assert(settings_bluetooth_back()&&scan_stops==1);
    assert(!settings_bluetooth_back());settings_bluetooth_close();assert(scan_stops==1);
    reset();scan_error=true;press(CONTROL_ADD);settings_bluetooth_tick(100);settings_bluetooth_tick(500);settings_bluetooth_tick(2099);assert(scan_starts==1);
    settings_bluetooth_tick(2100);assert(scan_starts==2&&strstr(s_ble_notice,"扫描未启动"));
    scan_error=false;settings_bluetooth_tick(4100);assert(scan_starts==3&&scanning);
    seed_devices(1);settings_bluetooth_tick(5100);devices[0].rssi=-49;assert(!settings_bluetooth_tick(6100));
    devices[0].rssi=-70;assert(settings_bluetooth_tick(7100));assert(!settings_bluetooth_tick(8100));
    settings_bluetooth_close();assert(scan_stops==1);

    reset();snprintf(bonds[1].name,sizeof(bonds[1].name),"%s",bonds[0].name);link_peer(0);settings_bluetooth_tick(1);
    tap(300,580);assert(s_view==BLE_DETAIL&&!strcmp(s_selected.addr,bonds[1].addr));
    press(CONTROL_FORGET);assert(s_view==BLE_FORGET_CONFIRM&&!forgets);press(CONTROL_CANCEL);assert(s_view==BLE_DETAIL&&!forgets);
    char old[18];snprintf(old,sizeof(old),"%s",bonds[1].addr);press(CONTROL_FORGET);press(CONTROL_CONFIRM);
    assert(forgets==1&&!disconnects&&!strcmp(forgotten,old)&&bond_count==2);
    tap(300,492);press(CONTROL_FORGET);press(CONTROL_CONFIRM);assert(forgets==2&&disconnects==1);
    reset();link_peer(0);settings_bluetooth_tick(1);tap(300,580);press(CONTROL_CONNECT);
    assert(connects==1&&!disconnects&&!strcmp(target,bonds[1].addr));

    reset();link_peer(0);settings_bluetooth_tick(1);queue_press();assert(press(CONTROL_LEARN_PREV)&&learn_begins==1&&s_ble_learning==1&&!binds);
    assert(!press(CONTROL_LEARN_NEXT));queue_press();settings_bluetooth_tick(2);assert(binds==1&&!s_ble_learning&&s_model.bound[0]);
    assert(strstr(s_ble_feedback,"已绑定上一页")&&!key_count&&raw_at==raw_count);
    press(CONTROL_LEARN_NEXT);bind_error=ESP_FAIL;queue_press();settings_bluetooth_tick(3);assert(binds==2&&!bindings[1]&&strstr(s_ble_feedback,"保存失败"));
    bind_error=0;press(CONTROL_LEARN_NEXT);link_peer(1);queue_press();settings_bluetooth_tick(4);assert(!s_ble_learning&&binds==2&&strstr(s_ble_feedback,"连接已变化"));
    press(CONTROL_LEARN_NEXT);connected=false;queue_press();settings_bluetooth_tick(5);assert(!s_ble_learning&&binds==2);
    link_peer(1);settings_bluetooth_tick(6);press(CONTROL_LEARN_NEXT);assert(settings_bluetooth_back()&&!s_ble_learning);
    // A new connection error is retained even when the link flag rises on the same tick.
    connected=false;settings_bluetooth_tick(7);link_peer(0);snprintf(failure,sizeof(failure),"连接失败，请重试");settings_bluetooth_tick(8);assert(strstr(s_ble_notice,"连接失败"));
    queue_press();settings_bluetooth_tick(9);assert(s_ble_feedback[0]);link_peer(1);settings_bluetooth_tick(10);assert(!s_ble_feedback[0]);
    press(CONTROL_DISCONNECT);assert(strstr(s_ble_notice,"正在断开"));connected=false;settings_bluetooth_tick(11);assert(!s_ble_notice[0]);
    reset();link_peer(0);settings_bluetooth_tick(1);tap(300,492);press(CONTROL_CONNECT);connected=false;settings_bluetooth_tick(2);assert(!s_ble_notice[0]);

    for(int shortcut=0;shortcut<2;++shortcut)for(int back=0;back<3;++back){
        reset();s_ble_shortcut_back=shortcut;enter_scan();app_ctx_t ctx={0};
        ui_gesture_event_t ev={.type=UI_GESTURE_TAP,.x0=58,.y0=114};
        app_redraw_t result=back==0?owner_gesture(&ctx,&ev):owner_key(&ctx,back==1?0:2);
        assert(result==APP_REDRAW_PAGE&&s_view==BLE_HOME&&s_page==SETTINGS_BLUETOOTH&&scan_stops==1&&!ctx.request_return);
        result=back==0?owner_gesture(&ctx,&ev):owner_key(&ctx,back==1?0:2);
        assert(result==(shortcut?APP_REDRAW_NONE:APP_REDRAW_PAGE)&&ctx.request_return==(shortcut!=0));
        assert(!shortcut?s_page==SETTINGS_MAIN:!s_ble_shortcut_back);
    }
    reset();enter_scan();app_ctx_t ctx={0};owner_key(&ctx,1);assert(requested_tab==0&&scan_stops==1);
    reset();enter_scan();ui_gesture_event_t nav={.type=UI_GESTURE_TAP,.x0=600,.y0=1150};
    owner_gesture(&ctx,&nav);assert(requested_tab==3&&scan_stops==1&&s_page==SETTINGS_MAIN);
    puts("PASS: asynchronous scan/disconnect, named 24-device paging, stable peer identity, learning/save faults, WiFi/off guards and Back/Home/origin lifecycle");
}
int main(int argc,char **argv){
    assert(argc==2);check_workflows();assert(ttf_font_open_builtin()==ESP_OK);
    uint8_t *fb=malloc(684*1216/2);assert(fb);char path[2048];
    const char *names[]={"01-connected","02-scan","03-learning","04-off","05-empty","06-scan-empty",
        "07-max-bonds","08-wifi-paused","09-learned","10-device-detail","11-scan-confirm","12-forget-confirm",
        "13-long-name","14-next-learning","15-connection-error","16-starting","17-scan-last-page","18-max-learning"};
    const unsigned scales[]={100,120,200};
    for(unsigned scale=0;scale<sizeof(scales)/sizeof(scales[0]);++scale){
        font_percent=scales[scale];
        for(int state=0;state<18;++state){
            reset();link_peer(0);settings_bluetooth_tick(1);
            switch(state){
                case 1: case 5: case 16:
                    connected=false;settings_bluetooth_tick(2);enter_scan();seed_devices(state==5?0:state==16?24:8);settings_bluetooth_tick(1002);
                    if(state==16){press(CONTROL_SCAN_NEXT);press(CONTROL_SCAN_NEXT);press(CONTROL_SCAN_NEXT);}break;
                case 2:press(CONTROL_LEARN_PREV);break;
                case 3:enabled=running=connected=false;settings_bluetooth_tick(2);break;
                case 4:connected=false;seed_bonds(0);settings_bluetooth_tick(2);break;
                case 6:seed_bonds(4);settings_bluetooth_tick(2);break;
                case 7:wifi_busy=true;running=connected=false;settings_bluetooth_tick(2);break;
                case 8:press(CONTROL_LEARN_PREV);queue_press();settings_bluetooth_tick(2);break;
                case 9:tap(300,492);break;
                case 10:press(CONTROL_ADD);break;
                case 11:tap(300,492);press(CONTROL_FORGET);break;
                case 12:snprintf(bonds[0].name,sizeof(bonds[0].name),"Very Long Bluetooth Turner Name");link_peer(0);settings_bluetooth_tick(2);break;
                case 13:press(CONTROL_LEARN_NEXT);break;
                case 14:connected=false;snprintf(failure,sizeof(failure),"连接失败，请靠近设备后重试");settings_bluetooth_tick(2);break;
                case 15:connected=running=false;settings_bluetooth_tick(2);break;
                case 17:seed_bonds(4);settings_bluetooth_tick(2);press(CONTROL_LEARN_NEXT);break;
                default:break;
            }
            int before=mutations;ble_model_t model=s_model;
            settings_bluetooth_render(fb);assert(mutations==before&&!memcmp(&model,&s_model,sizeof(model)));
            assert(ble_layout().footer+32<=UI_NAV_TOP-4);
            snprintf(path,sizeof(path),"%s/%s-%u.raw",argv[1],names[state],font_percent);save(path,fb);
        }
    }
    free(fb);ttf_font_unload();printf("PASS: 18 production states at 100/120/200%%, pure render, %u glyph/bounds checks\n",text_checks);return 0;
}
