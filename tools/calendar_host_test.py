#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：穷举百年离线公农历、干支、宜忌与时区边界，并检查生成表一致。
English: Exhaustively check a century of lunar/almanac data and timezone bounds plus reproducible tables.
"""
import ctypes as c
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import tempfile
from run_reader_baseline import toolchain
ROOT = Path(__file__).resolve().parents[1]

class Date(c.Structure):
    _fields_ = [(key,c.c_int) for key in ('year','month','day','weekday','days','first_weekday','lunar_year','lunar_month','lunar_day','year_cycle','month_cycle','day_cycle')] + [(key,c.c_char_p) for key in ('yi','ji','wealth','clash','sha','joy','fortune','auspicious','inauspicious','officer','guardian','guardian_type')]

def main():
    with tempfile.TemporaryDirectory(prefix='calendar-model-') as folder:
        work=Path(folder); oracle=work/'oracle.json'; library=work/'calendar.so'
        subprocess.run(['node',str(ROOT/'tools/generate_calendar_data.cjs'),'--check','--oracle',str(oracle)],check=True)
        env, compiler = toolchain(work)
        subprocess.run(compiler+['-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'main'),str(ROOT/'main/time/calendar.c'),'-o',str(library)],env=env,check=True)
        lib=c.CDLL(str(library));lib.calendar_date.argtypes=[c.c_int,c.c_int,c.c_int,c.POINTER(Date)];lib.calendar_date.restype=c.c_bool
        lib.calendar_from_utc.argtypes=[c.c_int64,c.POINTER(Date)];lib.calendar_from_utc.restype=c.c_bool
        lib.calendar_term.argtypes=[c.c_int,c.c_int,c.c_int,c.POINTER(c.c_int)];lib.calendar_term.restype=c.c_char_p
        data=json.loads(oracle.read_text());d=Date()
        for row in data:
            y,m,day,ly,lm,ld,mc,dc,yi,ji,wealth,clash,sha,*folk=row
            assert lib.calendar_date(y,m,day,c.byref(d)),row
            assert (d.lunar_year,d.lunar_month,d.lunar_day,d.month_cycle,d.day_cycle)==(ly,lm,ld,mc,dc),row
            assert d.year_cycle==(ly-4)%60
            assert [getattr(d,key).decode() for key in ('yi','ji','wealth','clash','sha')]==[yi,ji,wealth,clash[-1],sha],row
            assert [getattr(d,key).decode() for key in ('joy','fortune','auspicious','inauspicious','officer','guardian','guardian_type')]==folk,row
            assert d.weekday==(datetime(y,m,day).weekday()+1)%7 and d.first_weekday==(datetime(y,m,1).weekday()+1)%7
        for y,m,day,ly,lm,ld in [(2000,2,5,2000,1,1),(2024,2,10,2024,1,1),(2025,1,29,2025,1,1),(2025,7,25,2025,-6,1),(2026,11,9,2026,10,1),(2026,11,10,2026,10,2)]:
            assert lib.calendar_date(y,m,day,c.byref(d)) and (d.lunar_year,d.lunar_month,d.lunar_day)==(ly,lm,ld)
        for y,m,day in [(1999,12,31),(2100,1,1),(2026,2,29),(2000,0,1),(2026,13,1),(2026,1,0)]:
            assert not lib.calendar_date(y,m,day,c.byref(d)) and not d.year
        def utc(y,m,day,h=0,minute=0): return int(datetime(y,m,day,h,minute,tzinfo=timezone.utc).timestamp()*1000)
        assert lib.calendar_from_utc(utc(2026,11,9,15,59),c.byref(d)) and d.day==9
        assert lib.calendar_from_utc(utc(2026,11,9,16),c.byref(d)) and d.day==10
        assert not lib.calendar_from_utc(0,c.byref(d)) and not d.year
        assert lib.calendar_from_utc(4102415999999,c.byref(d)) and (d.year,d.month,d.day)==(2099,12,31)
        assert not lib.calendar_from_utc(4102416000000,c.byref(d)) and not d.year
        for month,names,days in [(11,('立冬','小雪'),(7,22)),(12,('大雪','冬至'),(7,22))]:
            for i in range(2):
                value=c.c_int();assert lib.calendar_term(2026,month,i,c.byref(value)).decode()==names[i] and value.value==days[i]
        print(f'PASS: {len(data)} dates (2000–2099), lunar new years/leap month, Ganzhi/Yi/Ji/folk data, 2026 terms, invalid dates and Beijing midnight/range boundaries')

if __name__=='__main__': main()
