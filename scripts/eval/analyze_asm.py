#!/usr/bin/env python3
"""results/mediator_asm 집계: C 중재자(START) vs 스택 없는 중재자(START_ASM), 같은 빌드. 값은 min / avg / max (N=1000).
바깥 = ts0 빌드 열0 - min(NS_CALIB), 안쪽 = ts1 빌드 열1 - min(INNER_CALIB[_ASM]). 차이는 중앙값끼리."""
import csv, os, statistics as st, sys
R = sys.argv[1] if len(sys.argv) > 1 else 'results/mediator_asm'
def col(v, f, c=None):
    with open(f'{R}/{v}/{f}.csv') as h:
        return [int(r[c or f]) for r in csv.DictReader(h)]
def fmt(x): return f"{min(x)} / {round(st.mean(x),1)} / {max(x)}"
print('| 프로파일 | 클럭 | 캐시 | 바깥 C | 바깥 asm | 바깥 차이 (asm-C, 중앙값) | 안쪽 C | 안쪽 asm | 안쪽 차이 | 빈 NSC (-NS 판독) | 보정: NS 판독, T1·T4 C, T1·T4 asm |')
print('|' + '---|' * 11)
for p in ['P1', 'P2']:
  for clk in ['110', '32']:
    for c in ['off', 'warm', 'cold']:
      a = f'{p}_{clk}M_{c}_ts0'; b = f'{p}_{clk}M_{c}_ts1'
      if not (os.path.exists(f'{R}/{a}/inner_asm_calib.csv') and os.path.exists(f'{R}/{b}/inner_asm_calib.csv')):
        print(f'| {p} | {clk} | {c} | 미측정 |'); continue
      nc = min(col(a, 'ns_calib'))
      oc = [x - nc for x in col(a, 'start_total')]
      oa = [x - nc for x in col(a, 'start_asm_total')]
      ic_c = min(col(b, 'inner_calib')); ic_a = min(col(b, 'inner_asm_calib'))
      ic = [x - ic_c for x in col(b, 'start_total', 'inner_t4_t1')]
      ia = [x - ic_a for x in col(b, 'start_asm_total', 'inner_asm_t4_t1')]
      en = [x - nc for x in col(a, 'empty_nsc')]
      print(f"| {p} | {clk} | {c} | {fmt(oc)} | {fmt(oa)} | {st.median(oa)-st.median(oc):+.0f} | {fmt(ic)} | {fmt(ia)} | "
            f"{st.median(ia)-st.median(ic):+.0f} | {fmt(en)} | {nc}, {ic_c}, {ic_a} |")
