#!/usr/bin/env python3
"""中文：Metalio 电量滤波状态机对拍：抗抖、单向、限速、判满、开机恢复与负载补偿。
English: Metalio battery filter replay: noise immunity, one-direction stepping, rate
limit, charge-done snap, boot restore and load compensation."""
from pathlib import Path
import subprocess, tempfile, re
root=Path(__file__).resolve().parents[1]
source=(root/'components/read_pico_pmu/read_pico_pmu_metalio.c').read_text()
def extract(name):
    m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*?\)\s*\{',source,re.M)
    assert m, name
    n=m.end();level=1
    while level:
        if source[n]=='{':level+=1
        elif source[n]=='}':level-=1
        n+=1
    return source[m.start():n]
defines='\n'.join(re.findall(r'^#define BATT_.*$',source,re.M))
curve=re.search(r'^static const int32_t s_soc_curve\[.*?\};',source,re.M|re.S).group(0)
typedef=re.search(r'typedef struct \{[^}]*\} batt_filter_t;',source,re.S).group(0)
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    code='#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n'
    code+=defines+'\n'+curve+'\n'+typedef+'\n'
    code+='\n'.join(extract(f) for f in ['batt_curve_soc','batt_filter_step'])
    code+='''
static batt_filter_t f;
static int32_t s_mv;
static int32_t s_pm;
static void reset(void) { memset(&f, 0, sizeof(f)); }
static void step(int64_t t, int mv, int ma, bool have_i, bool chg, bool done, int saved) {
 batt_filter_step(&f, t, (uint16_t)mv, have_i, (int16_t)ma, chg, done, saved, &s_mv, &s_pm);
}
int main(void) {
 // 1) 瞬时 ±8mV 抖动整段不动显示 / instantaneous +-8mV noise never moves the display
 reset();
 int first=-1;
 for (int i=0;i<600;i++) {
  step((int64_t)i*200000, 3900+((i&1)?8:-8), 30, true, false, false, -1);
  if (first<0) first=s_pm; else assert(s_pm==first);
 }
 // 2) 放电单调不升 / discharge never rises
 reset();
 int prev=1<<30;
 for (int i=0;i<1400;i++) {
  step((int64_t)i*200000, 4000-i/2, 40, true, false, false, -1);
  assert(s_pm<=prev);
  prev=s_pm;
 }
 assert(s_pm<560);
 // 3) 充电单调不降 / charge never falls
 reset();
 prev=0;
 for (int i=0;i<1400;i++) {
  step((int64_t)i*200000, 3700+i/2, 500, true, true, false, -1);
  assert(s_pm>=prev);
  prev=s_pm;
 }
 // 4) 限速：可见变化至少隔 10s、单步最多 1% / rate limit: >=10s apart, <=1% per step
 reset();
 step(0, 4000, 0, false, false, false, -1);
 assert(s_pm==560);
 int64_t last_t=0;
 int changes=0;
 prev=560;
 for (int i=1;i<=2100;i++) {
  step((int64_t)i*200000, 3700, 0, false, false, false, -1);
  if (s_pm!=prev) {
   assert((int64_t)i*200000-last_t>=10000000);
   assert(prev-s_pm>0 && prev-s_pm<=10);
   changes++;
   last_t=(int64_t)i*200000;
   prev=s_pm;
  }
 }
 assert(changes>=10);
 // 5) 判满对齐 100%，电压门槛挡误判 / done snaps to 100%, voltage gate guards misreads
 reset();
 step(0, 4380, 0, false, true, true, -1);
 assert(s_pm==1000);
 reset();
 step(0, 4000, 0, false, true, true, -1);
 assert(s_pm==560);
 // 6) 开机恢复容差 / boot restore tolerance
 reset(); step(0, 3900, 0, false, false, false, 430); assert(s_pm==430);
 reset(); step(0, 3900, 0, false, false, false, 200); assert(s_pm==450);
 reset(); step(0, 3900, 0, false, false, false, -1);  assert(s_pm==450);
 // 7) EMA 收敛到补偿后电压 / EMA converges on the compensated voltage
 reset();
 step(0, 3950, 0, false, false, false, -1);
 for (int i=1;i<900;i++) step((int64_t)i*200000, 3900, 0, false, false, false, -1);
 assert(s_mv>=3899 && s_mv<=3901);
 // 8) 负载补偿方向与封顶 / load compensation direction and cap
 reset(); step(0, 3800, 3000, true, false, false, -1); assert(s_mv==3950);
 reset(); step(0, 3800, 800, true, false, false, -1);  assert(s_mv==3880);
 reset(); step(0, 3800, 800, true, true, false, -1);   assert(s_mv==3720);
 puts("PASS: battery filter noise immunity, monotonic one-direction stepping, rate limit, charge-done snap, boot restore tolerance, EMA convergence and load compensation");
}
'''
    (p/'test.c').write_text(code)
    # 本机 CommandLineTools 27.0 SDK 的 tbd 损坏时逐个旧 SDK 回退。 / Fall back through the
    # older SDKs when the local CommandLineTools 27.0 tbd files are broken.
    cands=[['cc']]
    sdkdir=Path('/Library/Developer/CommandLineTools/SDKs')
    if sdkdir.is_dir():
        cands+= [['cc','-isysroot',str(s)] for s in sorted(sdkdir.glob('MacOSX*.sdk'),reverse=True)]
    err=None
    for cmd in cands:
        # 本机 ASan 主机测试会挂（见 IDF 构建环境记忆），只用基础告警编译。
        # ASan host tests hang on this machine (see the IDF build-env note); plain warnings only.
        r=subprocess.run(cmd+['-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],capture_output=True)
        if r.returncode==0 and subprocess.run([str(p/'test')],capture_output=True).returncode==0:
            print(subprocess.run([str(p/'test')],capture_output=True,text=True).stdout.strip())
            break
        err=r
    else:
        raise RuntimeError(err.stderr.decode() if err else 'no working compiler')
