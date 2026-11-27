#!/usr/bin/env python3
"""results/decomp 집계 (cmpAsmMediator.md §6, §7). 값은 중앙값 (N=1000), NS 판독 보정을 뺐다.
문맥 전환은 두 Task 방향(A->B, B->A)이 번갈아 나오므로 연속 두 표본 평균의 중앙값을 쓴다."""
import csv, os, statistics as st, sys
R = sys.argv[1] if len(sys.argv) > 1 else 'results/decomp'
def col(v, f):
    with open(f'{R}/{v}/{f}.csv') as h:
        return [int(list(r.values())[0]) for r in csv.DictReader(h)]
def s32(x): return x - 2**32 if x >= 2**31 else x
conds = [(p, c, k) for p in ['P1', 'P2'] for c in ['110', '32'] for k in ['off', 'warm']]
print('### 분해 (바깥, 중앙값 cycle)\n')
print('| 프로파일 | 클럭 | 캐시 | asm (M0) | M1 (+DMA 레지스터 2) | M2 (+FP 정리) | C | M1-M0 | M2-M1 | C-M2 | 빈 asm L0 | L1 (+FP 정리) | 빈 C nop3 | L1-L0 | nop3-L1 |')
print('|' + '---|' * 16)
for p, c, k in conds:
    v = f'{p}_{c}M_{k}_ts0'
    if not os.path.exists(f'{R}/{v}/ctx_v2.csv'): print(f'| {p} | {c} | {k} | 미측정 |'); continue
    n = min(col(v, 'ns_calib'))
    m = {f: st.median(col(v, f)) - n for f in ['start_asm_total', 'start_m1', 'start_m2', 'start_total', 'nsc_asm0', 'nsc_fpclr', 'empty_nsc']}
    a, m1, m2, cc = m['start_asm_total'], m['start_m1'], m['start_m2'], m['start_total']
    l0, l1, np3 = m['nsc_asm0'], m['nsc_fpclr'], m['empty_nsc']
    print(f'| {p} | {c} | {k} | {a:.0f} | {m1:.0f} | {m2:.0f} | {cc:.0f} | {m1-a:+.0f} | {m2-m1:+.0f} | {cc-m2:+.0f} | {l0:.0f} | {l1:.0f} | {np3:.0f} | {l1-l0:+.0f} | {np3-l1:+.0f} |')
print('\n### 문맥 전환 1회 (중앙값 cycle, 두 방향 평균)\n')
print('| 프로파일 | 클럭 | 캐시 | V0 context 없음 (asm) | V1 context + asm | V2 context + C NSC (FPCA) | V1-V0 (Save+Load) | V2-V1 (FP) | V2-V0 (C 경로 Task 추가분) | 두 방향 값 V0 / V2 |')
print('|' + '---|' * 10)
for p, c, k in conds:
    v = f'{p}_{c}M_{k}_ts0'
    if not os.path.exists(f'{R}/{v}/ctx_v2.csv'): print(f'| {p} | {c} | {k} | 미측정 |'); continue
    r = {}; modes = {}
    for f in ['ctx_v0', 'ctx_v1', 'ctx_v2']:
        x = [s32(y) for y in col(v, f)]
        pairs = [(x[i] + x[i + 1]) / 2 for i in range(0, len(x) - 1, 2)]
        r[f] = st.median(pairs)
        ev, od = st.median(x[0::2]), st.median(x[1::2]); modes[f] = f'{ev:.0f}/{od:.0f}'
    v0, v1, v2 = r['ctx_v0'], r['ctx_v1'], r['ctx_v2']
    print(f'| {p} | {c} | {k} | {v0:.0f} | {v1:.0f} | {v2:.0f} | {v1-v0:+.0f} | {v2-v1:+.0f} | {v2-v0:+.0f} | {modes["ctx_v0"]} / {modes["ctx_v2"]} |')
