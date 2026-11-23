/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实边缘手势的阈值、取消、圆键与前光滑杆事件检查。
 * English: Real edge-gesture thresholds, cancellation, circle keys and frontlight slider events.
 */
#include "ui_quick_menu.h"
#include <assert.h>
#include <stdio.h>
int ui_key_hit_test(uint16_t x,uint16_t y){(void)x;return y>1216?0:-1;}
static ui_quick_menu_t menu;
static int64_t time_ms;
static int screen_width=684;

static int control_y(ui_quick_control_t control) {
 EpdRect track=ui_quick_menu_frontlight_track(&menu,control);
 return track.y+track.height/2;
}
static int control_hit_y(ui_quick_control_t control) {
 EpdRect hit=ui_quick_menu_frontlight_hit(&menu,control);
 return hit.y+hit.height/2;
}

static ui_quick_event_t feed(int x,int y,int count,bool pressed,bool released,bool allowed,bool owned){
 app_touch_t touch={.x=x,.y=y,.count=count,.touched=count!=0};
 app_ctx_t ctx={.touch=&touch,.now_ms=time_ms,.pressed=pressed,.released=released};
 ui_quick_event_t event;
 assert(ui_quick_menu_feed(&menu,&ctx,allowed,false,&event)==owned);return event;
}
static void open(void){ui_quick_menu_reset(&menu);time_ms=0;
 assert(feed(300,32,1,true,false,true,true).action==UI_QUICK_NONE);
 time_ms=1500;assert(feed(300,104,1,false,false,true,true).action==UI_QUICK_OPEN&&menu.open);
 assert(feed(300,104,1,false,false,true,true).action==UI_QUICK_NONE);
 assert(feed(300,104,0,false,true,true,true).action==UI_QUICK_NONE);
}
int main(void){
 ui_quick_menu_reset(&menu);assert(feed(300,33,1,true,false,true,false).action==UI_QUICK_NONE);
 open();assert(feed(300,500,1,true,false,true,true).action==UI_QUICK_NONE);
 assert(feed(300,500,0,false,true,true,true).action==UI_QUICK_CLOSE&&!menu.open);
 assert(feed(300,500,0,false,false,true,false).action==UI_QUICK_NONE);
 ui_quick_menu_reset(&menu);time_ms=0;feed(300,20,1,true,false,true,true);time_ms=1501;
 assert(feed(300,92,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.open);feed(300,92,0,false,true,true,true);
 ui_quick_menu_reset(&menu);time_ms=0;feed(300,20,1,true,false,true,true);
 assert(feed(350,92,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.open);feed(350,92,0,false,true,true,true);
 open();feed(85,156,1,true,false,true,true);assert(feed(85,156,2,false,false,true,true).action==UI_QUICK_NONE);
 assert(feed(85,156,0,false,true,true,true).action==UI_QUICK_NONE&&menu.open);
 feed(85,156,1,true,false,true,true);ui_quick_menu_cancel_input(&menu);
 assert(feed(85,156,0,false,true,true,true).action==UI_QUICK_NONE&&menu.open);
 feed(85,156,1,true,false,true,true);assert(feed(150,156,1,false,false,true,true).action==UI_QUICK_NONE);
 assert(feed(150,156,0,false,true,true,true).action==UI_QUICK_NONE);
 assert(feed(85,156,1,true,false,true,true).action==UI_QUICK_NONE&&ui_quick_menu_pressed(&menu)==0);
 assert(feed(85,156,0,false,true,true,true).action==UI_QUICK_WIFI&&ui_quick_menu_pressed(&menu)==-1);
 feed(300,220,1,true,false,true,true);assert(feed(300,148,1,false,false,true,true).action==UI_QUICK_CLOSE);
 assert(feed(300,148,0,false,true,true,true).action==UI_QUICK_NONE);
 open();assert(feed(0,1250,1,true,false,true,true).action==UI_QUICK_CLOSE);
 feed(0,1250,0,false,true,true,true);
 open();assert(feed(0,0,0,false,false,false,true).action==UI_QUICK_CLOSE&&!menu.open);
 puts("PASS: 32/33px edge, 72px/1500ms limits, diagonal rejection, upward/outside/back dismissal, multitouch/error/drag cancellation and consumed release");

 // WiFi / 蓝牙圆键按住 0.5 秒：到阈值即关层并给出对应设置页事件，抬手不再点按；
 // 手指在圆键命中半径内可轻微漂移，移出圆键或按住其他圆键都不触发。
 // Holding the WiFi / Bluetooth circle for 0.5 s closes the layer and emits the
 // matching settings action at the threshold; the release no longer taps. The
 // finger may drift inside the circle's hit radius, but moving off the circle or
 // holding another circle never fires it.
 open();
 assert(feed(85,156,1,true,false,true,true).action==UI_QUICK_NONE&&ui_quick_menu_pressed(&menu)==0);
 time_ms+=UI_QUICK_HOLD_SETTINGS_MS-1;
 assert(feed(85,156,1,false,false,true,true).action==UI_QUICK_NONE&&menu.open);
 time_ms+=1;
 assert(feed(110,170,1,false,false,true,true).action==UI_QUICK_WIFI_SETTINGS&&!menu.open);
 assert(feed(110,170,0,false,true,true,true).action==UI_QUICK_NONE);
 open();
 feed(256,156,1,true,false,true,true);
 time_ms+=UI_QUICK_HOLD_SETTINGS_MS;
 assert(feed(256,156,1,false,false,true,true).action==UI_QUICK_BLUETOOTH_SETTINGS&&!menu.open);
 assert(feed(256,156,0,false,true,true,true).action==UI_QUICK_NONE);
 open();
 feed(85,156,1,true,false,true,true);
 time_ms+=UI_QUICK_HOLD_SETTINGS_MS;
 assert(feed(160,156,1,false,false,true,true).action==UI_QUICK_NONE&&menu.open);
 assert(feed(160,156,0,false,true,true,true).action==UI_QUICK_NONE);
 open();
 feed(427,156,1,true,false,true,true);
 time_ms+=UI_QUICK_HOLD_SETTINGS_MS;
 assert(feed(427,156,1,false,false,true,true).action==UI_QUICK_NONE&&menu.open);
 assert(feed(427,156,0,false,true,true,true).action==UI_QUICK_FULL&&menu.open);
 puts("PASS: 0.5s holds on WiFi/Bluetooth emit their settings pages and never double as taps");

 // 第四枚圆钮是前光开关：开灯后胶囊行才展开、层高变 480，熄灯收回 264。
 // The fourth circle is the frontlight switch: the capsule rows expand with a
 // 480px layer only while the light is on and collapse back to 264px when off.
 ui_quick_menu_set_frontlight(&menu,true,false,40,50);
 open();
 ui_quick_event_t e=feed(598,156,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_NONE&&!menu.fl_on);
 e=feed(598,156,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_ON&&e.value==1&&e.commit&&menu.fl_on);
 // 开灯拖亮度：无极定位，松手提交。
 // With the light on, dragging brightness positions continuously and commits on release.
 e=feed(399,control_y(UI_QUICK_CTL_BRIGHTNESS),1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==59&&!e.commit&&menu.fl_on);
 e=feed(512,control_y(UI_QUICK_CTL_BRIGHTNESS),1,false,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==78&&!e.commit);
 e=feed(512,control_y(UI_QUICK_CTL_BRIGHTNESS),0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==78&&e.commit&&menu.fl_bri==78&&menu.fl_on);
 // 整行可点：胶囊上方整行也能直接定位到档位。
 // The whole row is a hit target, so taps above the capsule still position it directly.
 e=feed(300,control_hit_y(UI_QUICK_CTL_BRIGHTNESS),1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==42&&menu.fl_on);
 e=feed(300,control_hit_y(UI_QUICK_CTL_BRIGHTNESS),0,false,true,true,true);
 assert(e.commit&&menu.fl_bri==42&&menu.fl_on);
 // 色温照常提交。
 // CCT commits as usual.
 e=feed(399,control_y(UI_QUICK_CTL_CCT),1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.value==59&&!e.commit&&menu.fl_on);
 e=feed(399,control_y(UI_QUICK_CTL_CCT),0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.value==59&&e.commit&&menu.fl_cct==59);
 // 按住滑杆向上拖不应把整层收起；松手仍提交最后一档。
 // Dragging a slider upward must not dismiss the layer; release still commits the last step.
 e=feed(399,control_y(UI_QUICK_CTL_CCT),1,true,false,true,true);
 e=feed(399,control_y(UI_QUICK_CTL_CCT)-68,1,false,false,true,true);
 assert(e.action==UI_QUICK_NONE&&menu.open);
 e=feed(399,330,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.commit&&menu.open);
 // 熄灯：胶囊行收回，卡片区域不再命中，点在圆钮区以下直接收层。
 // Switching off collapses the rows; the card area stops matching and taps below the circles dismiss.
 e=feed(598,156,1,true,false,true,true);
 e=feed(598,156,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_ON&&e.value==0&&e.commit&&!menu.fl_on);
 e=feed(399,control_y(UI_QUICK_CTL_BRIGHTNESS),1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_NONE);
 e=feed(399,control_y(UI_QUICK_CTL_BRIGHTNESS),0,false,true,true,true);
 assert(e.action==UI_QUICK_CLOSE&&!menu.open);
 puts("PASS: fourth circle toggles the frontlight, capsule rows expand only while on, full-row hit areas, 1%-step commit and drag protection");

 // 灯灭时继续下拉临时展开胶囊：同一次下拉要「停顿后再拉」才算继续，开层后的
 // 再次下拉也展开；只属本次开层，重开或熄灯点按即收起，滑杆在灯灭时只排队档位
 // 不开灯。
 // With the light off, the capsules expand for this opening only when the same
 // pull pauses and then resumes, or via a fresh drag on the open layer: reopening
 // or the light-off tap collapses them, and rows dragged while off only queue
 // levels without lighting.
 ui_quick_menu_set_frontlight(&menu,true,false,40,50);
 // 一次连续快滑只开层不展开：总行程再大也不算「继续下拉」。
 // One continuous fast swipe only opens: any total travel stays short of a "continuation".
 ui_quick_menu_reset(&menu);time_ms=0;
 feed(300,32,1,true,false,true,true);
 assert(feed(300,104,1,false,false,true,true).action==UI_QUICK_OPEN&&menu.open&&menu.fl_pull);
 assert(feed(300,200,1,false,false,true,true).action==UI_QUICK_NONE);
 assert(feed(300,260,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.fl_expanded&&!menu.fl_pull_settled);
 assert(feed(300,260,0,false,true,true,true).action==UI_QUICK_NONE&&menu.open&&!menu.fl_expanded);
 // 停在圆钮区外（滑过头）解除继续下拉，之后怎么拉都不展开。
 // Resting below the circle band (overshoot) disarms the continuation for good.
 ui_quick_menu_reset(&menu);time_ms=0;
 feed(300,32,1,true,false,true,true);
 assert(feed(300,500,1,false,false,true,true).action==UI_QUICK_OPEN&&menu.open);
 time_ms=300;
 assert(feed(300,500,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.fl_pull);
 time_ms=350;
 assert(feed(300,600,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.fl_expanded);
 feed(300,600,0,false,true,true,true);
 // 停顿不足再拉过一程、或再拉行程不足，都不展开。
 // An insufficient pause, or a resumed pull shorter than the opening travel, never expands.
 ui_quick_menu_reset(&menu);time_ms=0;
 feed(300,32,1,true,false,true,true);
 assert(feed(300,150,1,false,false,true,true).action==UI_QUICK_OPEN&&menu.open);
 time_ms=299;
 assert(feed(300,170,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.fl_pull_settled);
 time_ms=300;
 assert(feed(300,170,1,false,false,true,true).action==UI_QUICK_NONE&&menu.fl_pull_settled);
 time_ms=350;
 assert(feed(300,200,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.fl_expanded);
 feed(300,200,0,false,true,true,true);
 // 停顿后继续下拉过开层行程：同一次手势完成「开层—停顿—展开」。
 // Pause, then pull the opening travel again: one contact completes open-pause-expand.
 ui_quick_menu_reset(&menu);time_ms=0;
 feed(300,32,1,true,false,true,true);
 assert(feed(300,150,1,false,false,true,true).action==UI_QUICK_OPEN&&menu.open);
 time_ms=300;
 assert(feed(300,150,1,false,false,true,true).action==UI_QUICK_NONE&&menu.fl_pull_settled);
 time_ms=350;
 assert(feed(300,250,1,false,false,true,true).action==UI_QUICK_EXPAND&&menu.fl_expanded&&!menu.fl_on);
 assert(feed(300,250,0,false,true,true,true).action==UI_QUICK_NONE&&menu.open);
 // 展开后灯仍灭：整行可命中，拖动提交档位但不开灯。
 // Still off after expansion: full rows hit and drags commit levels without lighting.
 e=feed(399,control_y(UI_QUICK_CTL_BRIGHTNESS),1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==59&&!e.commit&&!menu.fl_on);
 e=feed(399,control_y(UI_QUICK_CTL_BRIGHTNESS),0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.commit&&e.value==59&&!menu.fl_on&&menu.fl_bri==59);
 // 熄灯点按收起临时展开：开灯再熄灯后卡片区域不再命中。
 // The light-off tap collapses the session expansion: after on then off the card area stops matching.
 e=feed(598,156,1,true,false,true,true);
 e=feed(598,156,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_ON&&e.value==1&&e.commit&&menu.fl_on&&!menu.fl_expanded);
 e=feed(598,156,1,true,false,true,true);
 e=feed(598,156,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_ON&&e.value==0&&!menu.fl_on&&!menu.fl_expanded);
 e=feed(399,500,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_NONE);
 e=feed(399,500,0,false,true,true,true);
 assert(e.action==UI_QUICK_CLOSE&&!menu.open&&!menu.fl_expanded);
 // 重开不记忆：开层后的再次下拉也能展开；收层再开只剩圆钮区。
 // Reopening forgets: a fresh drag on the open layer also expands; after closing
 // and reopening only the circle band remains.
 open();
 assert(feed(300,200,1,true,false,true,true).action==UI_QUICK_NONE&&!menu.fl_expanded);
 assert(feed(300,300,1,false,false,true,true).action==UI_QUICK_EXPAND&&menu.fl_expanded);
 assert(feed(300,300,0,false,true,true,true).action==UI_QUICK_NONE&&menu.open);
 feed(300,100,1,true,false,true,true);
 assert(feed(300,20,1,false,false,true,true).action==UI_QUICK_CLOSE);
 feed(300,20,0,false,true,true,true);
 open();
 assert(!menu.fl_expanded);
 e=feed(399,500,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_NONE&&!menu.fl_expanded);
 e=feed(399,500,0,false,true,true,true);
 assert(e.action==UI_QUICK_CLOSE&&!menu.open);
 // 层下缘起手的再次下拉不展开（起手必须抓在遮罩上）。
 // A fresh drag started below the band never expands (the grip must be on the shade).
 ui_quick_menu_set_frontlight(&menu,true,false,40,50);
 open();
 assert(feed(300,500,1,true,false,true,true).action==UI_QUICK_NONE);
 assert(feed(300,600,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.fl_expanded);
 assert(feed(300,600,0,false,true,true,true).action==UI_QUICK_NONE&&menu.open);
 feed(300,500,1,true,false,true,true);
 assert(feed(300,500,0,false,true,true,true).action==UI_QUICK_CLOSE&&!menu.open);
 // 灯亮开的层不受下拉影响：滑杆本就展开，无 EXPAND 事件。
 // A layer opened with the light on ignores further pulls: the rows are already
 // visible and no EXPAND event fires.
 ui_quick_menu_set_frontlight(&menu,true,true,40,50);
 open();
 assert(feed(300,300,1,false,false,true,true).action==UI_QUICK_NONE&&menu.fl_on&&!menu.fl_expanded);
 feed(300,300,0,false,true,true,true);
 e=feed(300,200,1,true,false,true,true);
 assert(e.action==UI_QUICK_NONE&&menu.fl_on&&!menu.fl_expanded);
 e=feed(300,300,1,false,false,true,true);
 assert(e.action==UI_QUICK_NONE&&menu.fl_on);
 feed(300,300,0,false,true,true,true);
 puts("PASS: light-off expansion needs a paused continuation or a fresh drag, never chains inside one fast swipe, forgets on reopen and never lights up");

 // 没有前光的板：第四枚仍是锁屏，卡片区域不命中，层高仍是 404，下拉延续与
 // 再次下拉都不会产生 EXPAND。
 // Boards without a frontlight keep the lock on the fourth circle, ignore the
 // card area and stay 404px tall; neither continued nor fresh pulls expand.
 ui_quick_menu_set_frontlight(&menu,false,true,40,50);
 open();
 assert(feed(300,200,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.fl_expanded);
 feed(300,200,0,false,true,true,true);
 feed(300,200,1,true,false,true,true);
 assert(feed(300,300,1,false,false,true,true).action==UI_QUICK_NONE&&!menu.fl_expanded);
 feed(300,300,0,false,true,true,true);
 assert(feed(598,156,1,true,false,true,true).action==UI_QUICK_NONE);
 assert(feed(598,156,0,false,true,true,true).action==UI_QUICK_ROTATION);
 int lock=ui_quick_menu_lock_button(&menu);
 int lx=ui_quick_menu_button_x(&menu,lock,screen_width),ly=ui_quick_menu_button_y(&menu,lock);
 feed(lx,ly,1,true,false,true,true);
 assert(feed(lx,ly,0,false,true,true,true).action==UI_QUICK_LOCK);
 assert(feed(300,450,1,true,false,true,true).control==UI_QUICK_CTL_NONE);
 assert(feed(300,450,0,false,true,true,true).action==UI_QUICK_CLOSE);
 puts("PASS: no-frontlight boards keep the lock key, the 404px layer and ignore card hits");
 // 竖屏滑杆在下方、横屏滑杆在右侧；方向键仍在第二排，按钮/间隙/滑杆独立命中。
 // Portrait sliders sit below and landscape sliders sit right; orientation stays in row two with disjoint key/gap/slider hits.
 for(screen_width=684;screen_width<=1216;screen_width+=532){
  menu.rotation_available=true;menu.rotation_enabled=true;ui_quick_menu_set_frontlight(&menu,false,false,40,50);open();
  assert(ui_quick_menu_button_count(&menu)==6&&menu.rotation_enabled);
  const ui_quick_action_t actions[]={UI_QUICK_WIFI,UI_QUICK_BLUETOOTH,UI_QUICK_FULL,UI_QUICK_ROTATION,UI_QUICK_MEMORY_CLEAN,UI_QUICK_LOCK};
  for(int i=0;i<6;++i){
   int x=ui_quick_menu_button_x(&menu,i,screen_width);
   int y=ui_quick_menu_button_y(&menu,i);
   const bool row_two=i>=4;
   assert(x>=UI_QUICK_RADIUS&&x+UI_QUICK_RADIUS<=screen_width);
   assert(y==(row_two?296:156));
   assert(feed(x,y,1,true,false,true,true).action==UI_QUICK_NONE&&ui_quick_menu_pressed(&menu)==i);
   assert(feed(x,y,0,false,true,true,true).action==actions[i]);
  }
  int rotation=ui_quick_menu_rotation_button(&menu);
  int x=ui_quick_menu_button_x(&menu,rotation,screen_width);
  int ry=ui_quick_menu_button_y(&menu,rotation);
  feed(x,ry,1,true,false,true,true);time_ms+=600;
  assert(feed(x,ry,1,false,false,true,true).action==UI_QUICK_NONE&&menu.open);
  assert(feed(x,ry,0,false,true,true,true).action==UI_QUICK_ROTATION);
  assert(ui_quick_menu_height(&menu)==404);
  ui_quick_menu_set_frontlight(&menu,true,true,40,50);
  assert(ui_quick_menu_button_count(&menu)==7);
  assert(ui_quick_menu_height(&menu)==(screen_width>684?404:620));
  x=ui_quick_menu_button_x(&menu,ui_quick_menu_lock_button(&menu),screen_width);
  assert(feed(x,296,1,true,false,true,true).action==UI_QUICK_NONE);
  assert(feed(x,296,0,false,true,true,true).action==UI_QUICK_LOCK);
  ui_quick_menu_set_frontlight(&menu,true,true,40,50);
  x=ui_quick_menu_button_x(&menu,ui_quick_menu_rotation_button(&menu),screen_width);
  feed(x,296,1,true,false,true,true);
  assert(feed(x,296,0,false,true,true,true).action==UI_QUICK_ROTATION);
  for(int ctl=UI_QUICK_CTL_BRIGHTNESS;ctl<=UI_QUICK_CTL_CCT;++ctl){
   EpdRect track=ui_quick_menu_frontlight_track(&menu,(ui_quick_control_t)ctl);
   int cy=track.y+track.height/2;
   e=feed(track.x+track.width/2,cy,1,true,false,true,true);
   assert(e.control==(ui_quick_control_t)ctl&&e.value==50&&!e.commit);
   e=feed(track.x-8,cy,1,false,false,true,true);
   assert(e.control==(ui_quick_control_t)ctl&&e.value==(ctl==UI_QUICK_CTL_BRIGHTNESS?1:0));
   e=feed(track.x+track.width+7,cy,1,false,false,true,true);
   assert(e.control==(ui_quick_control_t)ctl&&e.value==100);
   assert(feed(track.x+track.width+7,cy,0,false,true,true,true).commit);
  }
  if(screen_width==1216){
   assert(feed(610,156,1,true,false,true,true).control==UI_QUICK_CTL_NONE&&ui_quick_menu_pressed(&menu)==-1);
   assert(feed(610,156,0,false,true,true,true).action==UI_QUICK_NONE&&menu.open);
   assert(feed(300,424,1,true,false,true,true).control==UI_QUICK_CTL_NONE);
   assert(feed(300,424,0,false,true,true,true).action==UI_QUICK_CLOSE&&!menu.open);open();
   for(int key=0;key<2;++key){
    int kx=102+140*key;
    feed(kx,156,1,true,false,true,true);time_ms+=UI_QUICK_HOLD_SETTINGS_MS;
    assert(feed(kx,156,1,false,false,true,true).action==(key==0?UI_QUICK_WIFI_SETTINGS:UI_QUICK_BLUETOOTH_SETTINGS));
    feed(kx,156,0,false,true,true,true);open();
   }
   ui_quick_menu_set_frontlight(&menu,true,false,40,50);open();
   assert(ui_quick_menu_height(&menu)==404);
   assert(feed(910,156,1,true,false,true,true).control==UI_QUICK_CTL_NONE);
   assert(feed(910,156,0,false,true,true,true).action==UI_QUICK_NONE);
   feed(610,190,1,true,false,true,true);
   assert(feed(610,270,1,false,false,true,true).action==UI_QUICK_EXPAND&&menu.fl_expanded);
   feed(610,270,0,false,true,true,true);
   assert(ui_quick_menu_height(&menu)==404);
   assert(feed(914,156,1,true,false,true,true).control==UI_QUICK_CTL_BRIGHTNESS);
   assert(feed(914,156,0,false,true,true,true).commit&&!menu.fl_on);
   assert(feed(522,156,1,true,false,true,true).control==UI_QUICK_CTL_NONE);
   assert(feed(522,156,0,false,true,true,true).control==UI_QUICK_CTL_ON&&menu.fl_on);
   assert(feed(522,156,1,true,false,true,true).control==UI_QUICK_CTL_NONE);
   assert(feed(522,156,0,false,true,true,true).control==UI_QUICK_CTL_ON&&!menu.fl_on);
   assert(ui_quick_menu_height(&menu)==404&&!menu.fl_expanded);
  }
  ui_quick_menu_set_frontlight(&menu,false,false,40,50);
  menu.rotation_available=false;ui_quick_menu_reset(&menu);assert(ui_quick_menu_button_count(&menu)==6);
 }
 puts("PASS: responsive two-column landscape and stacked portrait keep controls, slider endpoints, holds and light expansion distinct");
 // 震动键在普通页及阅读页、横竖屏中都独立命中；长按不补点按，滑杆编号不冲突。
 // Haptics has its own hit in normal/reader and portrait/landscape layouts; holds swallow release and never alias a slider.
 for(screen_width=684;screen_width<=1216;screen_width+=532)for(int reader=0;reader<2;++reader){
  menu.haptics_available=menu.haptics_enabled=true;menu.rotation_available=reader!=0;
  ui_quick_menu_set_frontlight(&menu,true,true,40,50);open();
  const int key=ui_quick_menu_haptics_button(&menu);
  const int x=ui_quick_menu_button_x(&menu,key,screen_width),y=296;
  assert(ui_quick_menu_button_count(&menu)==8);
  assert(ui_quick_menu_height(&menu)==(screen_width==684?620:404));
  assert(feed(x,y,1,true,false,true,true).action==UI_QUICK_NONE&&ui_quick_menu_pressed(&menu)==key);
  e=feed(x,y,0,false,true,true,true);
  assert(e.action==UI_QUICK_HAPTICS&&e.control==UI_QUICK_CTL_NONE&&menu.open);
  feed(x,y,1,true,false,true,true);time_ms+=UI_QUICK_HOLD_SETTINGS_MS-1;
  assert(feed(x,y,1,false,false,true,true).action==UI_QUICK_NONE);
  ++time_ms;assert(feed(x+8,y+8,1,false,false,true,true).action==UI_QUICK_HAPTICS_SETTINGS&&!menu.open);
  assert(feed(x+8,y+8,0,false,true,true,true).action==UI_QUICK_NONE);
  open();feed(x,y,1,true,false,true,true);time_ms+=UI_QUICK_HOLD_SETTINGS_MS;
  assert(feed(x,y,2,false,false,true,true).action==UI_QUICK_NONE);
  assert(feed(x,y,0,false,true,true,true).action==UI_QUICK_NONE&&menu.open);
  feed(x,y,1,true,false,true,true);ui_quick_menu_cancel_input(&menu);
  assert(feed(x,y,0,false,true,true,true).action==UI_QUICK_NONE);
  feed(x,y,1,true,false,true,true);time_ms+=UI_QUICK_HOLD_SETTINGS_MS;
  assert(feed(x+80,y,1,false,false,true,true).action==UI_QUICK_NONE&&menu.open);
  assert(feed(x+80,y,0,false,true,true,true).action==UI_QUICK_NONE);
  for(int ctl=UI_QUICK_CTL_BRIGHTNESS;ctl<=UI_QUICK_CTL_CCT;++ctl){
   EpdRect track=ui_quick_menu_frontlight_track(&menu,(ui_quick_control_t)ctl);
   int sx=track.x+track.width/2,sy=track.y+track.height/2;
   e=feed(sx,sy,1,true,false,true,true);assert(e.control==(ui_quick_control_t)ctl&&e.action==UI_QUICK_NONE);
   assert(feed(sx,sy,0,false,true,true,true).commit);
  }
 }
 puts("PASS: haptics short/hold threshold, release swallowing, cancellation and disjoint sliders in normal/reader portrait/landscape layouts");
 // 穷举能力组合；每个圆键命中区彼此分离，也不与滑杆相交。
 // Enumerate capabilities; every circle hit zone stays disjoint from its neighbours and the sliders.
 for(screen_width=684;screen_width<=1216;screen_width+=532)for(unsigned flags=0;flags<16;++flags){
  menu=(ui_quick_menu_t){.rotation_available=(flags&1)!=0,.haptics_available=(flags&2)!=0,
      .cellular_available=(flags&4)!=0};
  ui_quick_menu_set_frontlight(&menu,(flags&8)!=0,true,40,50);open();
  for(int i=0;i<ui_quick_menu_button_count(&menu);++i){
   const int x=ui_quick_menu_button_x(&menu,i,screen_width),y=ui_quick_menu_button_y(&menu,i);
   const int radius=ui_quick_menu_button_layout(&menu,i,screen_width).hit_radius;
   assert(x-radius>=0&&x+radius<screen_width);
   for(int j=0;j<i;++j){
    int dx=x-ui_quick_menu_button_x(&menu,j,screen_width),dy=y-ui_quick_menu_button_y(&menu,j);
    int sum=radius+ui_quick_menu_button_layout(&menu,j,screen_width).hit_radius;
    assert(dx*dx+dy*dy>sum*sum);
   }
   if(menu.frontlight)for(int ctl=UI_QUICK_CTL_BRIGHTNESS;ctl<=UI_QUICK_CTL_CCT;++ctl){
    EpdRect track=ui_quick_menu_frontlight_hit(&menu,(ui_quick_control_t)ctl);
    assert(x+radius<track.x||x-radius>=track.x+track.width||y+radius<track.y||y-radius>=track.y+track.height);
   }
  }
  const int key=ui_quick_menu_memory_button(&menu),x=ui_quick_menu_button_x(&menu,key,screen_width),y=ui_quick_menu_button_y(&menu,key);
  feed(x,y,1,true,false,true,true);
  assert(feed(x,y,0,false,true,true,true).action==UI_QUICK_MEMORY_CLEAN);
  feed(x,y,1,true,false,true,true);time_ms+=499;
  assert(feed(x,y,1,false,false,true,true).action==UI_QUICK_NONE);++time_ms;
  assert(feed(x,y,1,false,false,true,true).action==UI_QUICK_MEMORY_SETTINGS&&!menu.open);
  assert(feed(x,y,0,false,true,true,true).action==UI_QUICK_NONE);
  open();feed(x,y,1,true,false,true,true);time_ms+=500;
  assert(feed(x+30,y,1,false,false,true,true).action==UI_QUICK_NONE&&menu.open);
  assert(feed(x,y,0,false,true,true,true).action==UI_QUICK_NONE);
  feed(x,y,1,true,false,true,true);time_ms+=500;
  assert(feed(x,y,2,false,false,true,true).action==UI_QUICK_NONE);
  assert(feed(x,y,0,false,true,true,true).action==UI_QUICK_NONE);
 }
 puts("PASS: memory tap/500ms hold, release swallowing and move/multitouch cancellation across every capability/orientation; disjoint key and slider hit zones");
 // 每个项目都可隐藏，其他八项可跨两排排列；事件始终按项目而非位置触发。
 // Hide each item and rotate the other eight through both rows; events follow item IDs rather than positions.
 const ui_quick_action_t item_actions[APP_QUICK_ITEM_COUNT]={UI_QUICK_WIFI,UI_QUICK_BLUETOOTH,UI_QUICK_FULL,
     UI_QUICK_NONE,UI_QUICK_CELLULAR,UI_QUICK_ROTATION,UI_QUICK_HAPTICS,UI_QUICK_MEMORY_CLEAN,UI_QUICK_LOCK};
 for(screen_width=684;screen_width<=1216;screen_width+=532)
 for(unsigned hidden=0;hidden<APP_QUICK_ITEM_COUNT;++hidden)for(unsigned shift=0;shift<8;++shift){
  menu=(ui_quick_menu_t){.frontlight=true,.haptics_available=true,.cellular_available=true};
  menu.layout.count=8;unsigned n=0;
  for(unsigned item=0;item<APP_QUICK_ITEM_COUNT;++item)if(item!=hidden)menu.layout.order[(n++ + shift)%8]=item;
  menu.layout.order[8]=hidden;open();
  assert(ui_quick_menu_button_count(&menu)==8&&ui_quick_menu_find_button(&menu,(app_quick_item_t)hidden)==-1);
  for(int i=0;i<8;++i){
   int x=ui_quick_menu_button_x(&menu,i,screen_width),y=ui_quick_menu_button_y(&menu,i);
   assert(y==UI_QUICK_CIRCLE_Y+(i/4)*UI_QUICK_ROTATION_ROW_STEP);
   if(i>=4)assert(x==ui_quick_menu_button_x(&menu,i-4,screen_width));
   feed(x,y,1,true,false,true,true);e=feed(x,y,0,false,true,true,true);
   app_quick_item_t item=ui_quick_menu_item(&menu,i);
   if(item==APP_QUICK_FRONTLIGHT)assert(e.control==UI_QUICK_CTL_ON&&e.commit);
   else assert(e.action==item_actions[item]&&e.control==UI_QUICK_CTL_NONE);
   ui_quick_action_t hold=item==APP_QUICK_WIFI?UI_QUICK_WIFI_SETTINGS:
       item==APP_QUICK_BLUETOOTH?UI_QUICK_BLUETOOTH_SETTINGS:item==APP_QUICK_CELLULAR?UI_QUICK_CELLULAR_SETTINGS:
       item==APP_QUICK_HAPTICS?UI_QUICK_HAPTICS_SETTINGS:item==APP_QUICK_MEMORY?UI_QUICK_MEMORY_SETTINGS:UI_QUICK_NONE;
   if(hold!=UI_QUICK_NONE){
    feed(x,y,1,true,false,true,true);time_ms+=500;
    assert(feed(x,y,1,false,false,true,true).action==hold&&!menu.open);
    assert(feed(x,y,0,false,true,true,true).action==UI_QUICK_NONE);open();
   }
  }
 }
 menu.layout.count=0;open();assert(ui_quick_menu_button_count(&menu)==0);
 puts("PASS: every hidden item and all cyclic positions preserve taps/holds across portrait and landscape; zero selected items is valid");
 return 0;
}

int epd_rotated_display_width(void) { return screen_width; }
int epd_rotated_display_height(void) { return screen_width==684?1216:684; }
