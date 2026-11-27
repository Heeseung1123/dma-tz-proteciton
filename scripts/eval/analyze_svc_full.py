#!/usr/bin/env python3
"""results/svc P2 전체표 (cmpAsmMediator.md §9.7). 같은 빌드 안의 모든 표, off/warm/cold x 110/32 MHz.
  표 1: 깨끗한 빌드 (BENCH_ICMON 없음), NS 판독 보정 뺀 중앙값 (min / avg / max 는 표 2).
  표 3: BENCH_ICMON 진단 빌드의 측정 구간 ICACHE hit/miss (중앙값 사이클 표본의 최빈값) 와 사이클 회귀.
  python3 scripts/eval/analyze_svc_full.py [results/svc]"""
import csv, os, statistics as st, sys
import numpy as np
R = sys.argv[1] if len(sys.argv) > 1 else 'results/svc'
T = [('empty_nsc', '빈 C NSC (nop3)'), ('nsc_asm0', '빈 asm NSC (L0)'), ('nsc_fpclr', 'L0 + FP 정리 (L1)'),
     ('start_total', 'C 중재자'), ('start_asm_total', 'asm 중재자 (Thread)'), ('start_m1', 'M1 (asm + DMA 레지스터 2)'),
     ('start_m2', 'M2 (M1 + FP 정리)'), ('start_svc', '**SVC 경로 (A)**'), ('svc_empty', '빈 SVC')]
COND = [(c, k) for c in ['110', '32'] for k in ['off', 'warm', 'cold']]
def rows(v, f): return [[int(x) for x in r] for r in list(csv.reader(open(f'{R}/{v}/{f}.csv')))[1:]]
def col(v, f): return [r[0] for r in rows(v, f)]

print('### 표 1. 같은 빌드 안의 모든 값 (P2, 중앙값 cycle, NS 판독 보정 뺌, N=1000)\n')
print('| 항목 | ' + ' | '.join(f'{c}/{k}' for c, k in COND) + ' |')
print('|---|' + '---|' * len(COND))
cal = {}
for c, k in COND: cal[(c, k)] = min(col(f'P2_{c}M_{k}_ts0', 'ns_calib'))
print('| NS 판독 보정 (min, 뺀 값) | ' + ' | '.join(str(cal[x]) for x in COND) + ' |')
med = {}
for f, name in T:
    out = []
    for c, k in COND:
        m = st.median(col(f'P2_{c}M_{k}_ts0', f)) - cal[(c, k)]; med[(f, c, k)] = m; out.append(f'{m:.0f}')
    print(f'| {name} | ' + ' | '.join(out) + ' |')
print('\n### 표 2. min / avg / max (같은 값들)\n')
print('| 항목 | ' + ' | '.join(f'{c}/{k}' for c, k in COND) + ' |')
print('|---|' + '---|' * len(COND))
for f, name in T:
    out = []
    for c, k in COND:
        x = [y - cal[(c, k)] for y in col(f'P2_{c}M_{k}_ts0', f)]
        out.append(f'{min(x)} / {st.mean(x):.1f} / {max(x)}')
    print(f'| {name} | ' + ' | '.join(out) + ' |')

print('\n### 표 3. 측정 구간 ICACHE hit / miss (BENCH_ICMON 진단 빌드, 사이클은 이 빌드 값)\n')
print('| 항목 | 110 warm h/m | 110 warm cyc | 110 cold h/m | 110 cold cyc | 110 off cyc (표 1) | 32 warm h/m | 32 warm cyc | 32 cold h/m | 32 cold cyc | 32 off cyc (표 1) |')
print('|---|' + '---|' * 10)
hm = {}
for f, name in T:
    out = []
    for c in ['110', '32']:
        for k in ['warm', 'cold']:
            v = f'P2_{c}M_{k}_icmon'; n = min(col(v, 'ns_calib')); r = rows(v, f)
            m = st.median([x[0] for x in r]); w = st.mode([x[1] for x in r])
            hm[(f, c, k)] = (m - n, w >> 16, w & 0xFFFF)
            out += [f'{w >> 16} / {w & 0xFFFF}', f'{m - n:.0f}']
        out.append(f'{med[(f, c, "off")]:.0f}')
    print(f'| {name} | ' + ' | '.join(out) + ' |')
print('\n회귀 (최소제곱, 9 항목). 캐시 켜짐: cyc = a + h·hit + p·miss (warm + cold 18 점). off: cyc = a + q·조회 수 (조회 = cold 의 hit + miss).\n')
print('| 클럭 | 캐시 켜짐 a | hit 1 회 h | **miss 1 회 p** | 최대 오차 | off a | **off 조회 1 회 q** | 최대 오차 |')
print('|---|' + '---|' * 8)
for c in ['110', '32']:
    A = [[1, hm[(f, c, k)][1], hm[(f, c, k)][2]] for f, _ in T for k in ['warm', 'cold']]
    b = [hm[(f, c, k)][0] for f, _ in T for k in ['warm', 'cold']]
    co = np.linalg.lstsq(np.array(A, float), np.array(b, float), rcond=None)[0]
    e1 = max(abs(np.array(A, float) @ co - b))
    Ao = [[1, hm[(f, c, 'cold')][1] + hm[(f, c, 'cold')][2]] for f, _ in T]; bo = [med[(f, c, 'off')] for f, _ in T]
    co2 = np.linalg.lstsq(np.array(Ao, float), np.array(bo, float), rcond=None)[0]
    e2 = max(abs(np.array(Ao, float) @ co2 - bo))
    print(f'| {c} | {co[0]:.1f} | {co[1]:.2f} | **{co[2]:.2f}** | {e1:.1f} | {co2[0]:.1f} | **{co2[1]:.2f}** | {e2:.1f} |')
print('\n### 표 4. 항목별 cold 의 조회/miss (= 한 줄(16 B)에서 쓴 조회 수) 와 off 대비 miss 1 회당 초과 사이클:\n')
print('| 항목 | 조회 수 (110) | miss (110) | 조회/miss | cold − off (표 1, 110) | miss 당 초과 (110) | 조회 수 (32) | miss (32) | 조회/miss | cold − off (표 1, 32) | miss 당 초과 (32) |')
print('|---|' + '---|' * 10)
for f, name in T:
    out = []
    for c in ['110', '32']:
        _, h, m = hm[(f, c, 'cold')]
        d = med[(f, c, "cold")] - med[(f, c, "off")]
        out += [str(h + m), str(m), f'{(h + m) / m:.1f}', f'{d:+.0f}', f'{d / m:+.1f}']
    print(f'| {name} | ' + ' | '.join(out) + ' |')
