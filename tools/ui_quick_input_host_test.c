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

 // 前光卡片常驻：开关、亮度、色温都直接命中；滑杆 1% 步进，松手才提交。
 // The frontlight card is always visible: switch, brightness and CCT hit directly;
 // sliders step by 1% and only commit on release.
 ui_quick_menu_set_frontlight(&menu,true,false,40,50);
 open();
 ui_quick_event_t e=feed(584,253,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_NONE&&!menu.fl_on);
 e=feed(584,253,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_ON&&e.value==1&&e.commit&&menu.fl_on);
 e=feed(584,253,1,true,false,true,true);
 e=feed(584,253,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_ON&&e.value==0&&e.commit&&!menu.fl_on);
 e=feed(399,318,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==60&&!e.commit&&menu.fl_on);
 e=feed(512,318,1,false,false,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==80&&!e.commit);
 e=feed(512,318,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_BRIGHTNESS&&e.value==80&&e.commit&&menu.fl_bri==80);
 e=feed(399,382,1,true,false,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.value==60&&!e.commit);
 e=feed(399,382,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.value==60&&e.commit&&menu.fl_cct==60);
 // 按住滑杆向上拖不应把整层收起；松手仍提交最后一档。
 // Dragging a slider upward must not dismiss the layer; release still commits the last step.
 e=feed(399,382,1,true,false,true,true);
 e=feed(399,300,1,false,false,true,true);
 assert(e.action==UI_QUICK_NONE&&menu.open);
 e=feed(399,300,0,false,true,true,true);
 assert(e.control==UI_QUICK_CTL_CCT&&e.commit&&menu.open);
 e=feed(300,430,1,true,false,true,true);
 e=feed(300,300,1,false,false,true,true);
 assert(e.action==UI_QUICK_CLOSE&&!menu.open);
 puts("PASS: frontlight switch, 1%-step brightness/CCT sliders, auto-on, drag protection and release commit");

 // 没有前光的板：卡片区域不命中，层高仍是 264。
 // Boards without a frontlight ignore the card area and keep the 264px height.
 ui_quick_menu_set_frontlight(&menu,false,true,40,50);
 open();
 assert(feed(584,253,1,true,false,true,true).control==UI_QUICK_CTL_NONE);
 assert(feed(584,253,0,false,true,true,true).control==UI_QUICK_CTL_NONE&&menu.open);
 assert(feed(300,300,1,true,false,true,true).action==UI_QUICK_NONE);
 assert(feed(300,300,0,false,true,true,true).action==UI_QUICK_CLOSE);
 puts("PASS: no-frontlight boards keep the four-key 264px layer and ignore card hits");
 return 0;
}
