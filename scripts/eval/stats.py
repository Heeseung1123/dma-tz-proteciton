#!/usr/bin/env python3
"""results CSV 공통 통계. CYCCNT 는 32비트이므로 차이는 mod 2^32."""
import csv, statistics as st
def load(path):
    with open(path) as f: return list(csv.DictReader(f))
def delta(rows, a, b): return [(int(r[b]) - int(r[a])) & 0xFFFFFFFF for r in rows]
def summ(v):
    v = list(v)
    m = st.median(v)
    return dict(n=len(v), min=min(v), avg=round(st.mean(v), 1), max=max(v), std=round(st.pstdev(v), 1), med=m)
def clean(v, med=None, k=1.5):
    """틱 인터럽트 선점 표본 제외: 중앙값의 k 배를 넘는 표본 제외. 제외 수를 함께 보고한다."""
    med = st.median(v) if med is None else med
    keep = [x for x in v if x <= med * k]
    return keep, len(v) - len(keep)

def valid(v):
    """t1<t0 로 wrap 된 표본(틱이 판독과 기록 사이에 끼어든 경우) 제외. (표본, 제외 수)"""
    k=[x for x in v if x < 0x80000000]
    return k, len(v)-len(k)
