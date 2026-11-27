#!/usr/bin/env python3
"""results/svc 집계 (cmpAsmMediator.md §9). 중앙값 cycle, NS 판독 보정 뺌, N=1000.
SVC 경로 = App_DmaStartSvc (svc #110 -> SVC_Handler fast path -> SECURE_dma_m2m_start_svc).
문맥 전환은 연속 두 표본 평균의 중앙값 (analyze_decomp.py 와 같음)."""
import csv, os, statistics as st, sys
R = sys.argv[1] if len(sys.argv) > 1 else 'results/svc'
def col(v, f):
    with open(f'{R}/{v}/{f}.csv') as h:
        return [int(list(r.values())[0]) for r in csv.DictReader(h)]
def s32(x): return x - 2**32 if x >= 2**31 else x
def pairmed(x):
    x = [s32(y) for y in x]
    return st.median([(x[i] + x[i + 1]) / 2 for i in range(0, len(x) - 1, 2)])
conds = [(p, c, k) for p in ['P1', 'P2'] for c in ['110', '32'] for k in ['off', 'warm']]
print('| 프로파일 | 클럭 | 캐시 | C (Thread) | asm (Thread, §8 문제) | **SVC 경로 (A)** | 빈 SVC | SVC − asm | SVC − 빈 SVC (Handler mode 중재자) | asm − 빈 asm NSC (Thread mode 중재자) | A / C | SVC 최대 |')
print('|' + '---|' * 12)
rows = []
for p, c, k in conds:
    v = f'{p}_{c}M_{k}_ts0'
    if not os.path.exists(f'{R}/{v}/svc_empty.csv'): print(f'| {p} | {c} | {k} | 미측정 |'); continue
    n = min(col(v, 'ns_calib'))
    m = {f: st.median(col(v, f)) - n for f in ['start_total', 'start_asm_total', 'start_svc', 'svc_empty', 'nsc_asm0']}
    mx = max(col(v, 'start_svc')) - n
    C, A, S, E, L0 = m['start_total'], m['start_asm_total'], m['start_svc'], m['svc_empty'], m['nsc_asm0']
    print(f'| {p} | {c} | {k} | {C:.0f} | {A:.0f} | **{S:.0f}** | {E:.0f} | {S-A:+.0f} | {S-E:.0f} | {A-L0:.0f} | {100*S/C:.0f} % | {mx} |')
print('\n문맥 전환 1회 (V0 = context 없음 + SVC 경로 호출, V1 = context + asm, V2 = context + C NSC)\n')
print('| 프로파일 | 클럭 | 캐시 | V0 (A Task) | V1 | V2 (C Task) | V2 − V0 |')
print('|' + '---|' * 7)
for p, c, k in conds:
    v = f'{p}_{c}M_{k}_ts0'
    if not os.path.exists(f'{R}/{v}/ctx_v2.csv'): continue
    v0, v1, v2 = (pairmed(col(v, f)) for f in ['ctx_v0', 'ctx_v1', 'ctx_v2'])
    print(f'| {p} | {c} | {k} | {v0:.0f} | {v1:.0f} | {v2:.0f} | {v2-v0:+.0f} |')
