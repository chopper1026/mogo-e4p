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

static ui_quick_event_t feed(int x,int y,int count,bool pressed,bool released,bool allowed,bool owned){
 cst836u_touch_t touch={.x=x,.y=y,.count=count,.touched=count!=0};
 app_ctx_t ctx={.touch=&touch,.now_ms=time_ms,.pressed=pressed,.released=released};
 ui_quick_event_t event;
 assert(ui_quick_menu_feed(&menu,&ctx,allowed,&event)==owned);return event;
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
 e=feed(399,300,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==59&&!e.commit&&menu.fl_on);
 e=feed(512,300,1,false,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==78&&!e.commit);
 e=feed(512,300,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==78&&e.commit&&menu.fl_bri==78&&menu.fl_on);
 // 整行可点：胶囊上方整行也能直接定位到档位。
 // The whole row is a hit target, so taps above the capsule still position it directly.
 e=feed(300,232,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==42&&menu.fl_on);
 e=feed(300,232,0,false,true,true,true);
 assert(e.commit&&menu.fl_bri==42&&menu.fl_on);
 // 色温照常提交。
 // CCT commits as usual.
 e=feed(399,398,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.value==59&&!e.commit&&menu.fl_on);
 e=feed(399,398,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.value==59&&e.commit&&menu.fl_cct==59);
 // 按住滑杆向上拖不应把整层收起；松手仍提交最后一档。
 // Dragging a slider upward must not dismiss the layer; release still commits the last step.
 e=feed(399,398,1,true,false,true,true);
 e=feed(399,330,1,false,false,true,true);
 assert(e.action==UI_QUICK_NONE&&menu.open);
 e=feed(399,330,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.commit&&menu.open);
 // 熄灯：胶囊行收回，卡片区域不再命中，点在圆钮区以下直接收层。
 // Switching off collapses the rows; the card area stops matching and taps below the circles dismiss.
 e=feed(598,156,1,true,false,true,true);
 e=feed(598,156,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_ON&&e.value==0&&e.commit&&!menu.fl_on);
 e=feed(399,300,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_NONE);
 e=feed(399,300,0,false,true,true,true);
 assert(e.action==UI_QUICK_CLOSE&&!menu.open);
 puts("PASS: fourth circle toggles the frontlight, capsule rows expand only while on, full-row hit areas, 1%-step commit and drag protection");

 // 没有前光的板：第四枚仍是锁屏，卡片区域不命中，层高仍是 264。
 // Boards without a frontlight keep the lock on the fourth circle, ignore the card area and stay 264px tall.
 ui_quick_menu_set_frontlight(&menu,false,true,40,50);
 open();
 assert(feed(598,156,1,true,false,true,true).action==UI_QUICK_NONE);
 assert(feed(598,156,0,false,true,true,true).action==UI_QUICK_LOCK);
 assert(feed(300,300,1,true,false,true,true).control==UI_QUICK_CTL_NONE);
 assert(feed(300,300,0,false,true,true,true).action==UI_QUICK_CLOSE);
 puts("PASS: no-frontlight boards keep the lock key, the 264px layer and ignore card hits");
 return 0;
}
