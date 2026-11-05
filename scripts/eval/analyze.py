#!/usr/bin/env python3
"""results/mediator 의 CSV 를 집계한다. 값은 min/avg/max (N=1000).
start 전체와 verify_inst 는 타임스탬프 없는 빌드(ts0), 내부(T4-T1)는 타임스탬프 빌드(ts1) 값이다."""
import csv, os, statistics as st
R = 'results/mediator'
def col(v, f, c=None):
    with open(f'{R}/{v}/{f}.csv') as h:
        return [int(r[c or f]) for r in csv.DictReader(h)]
def fmt(x): return f"{min(x)} / {round(st.mean(x),1)} / {max(x)}"
print('| 프로파일 | 클럭 | 캐시 | start 전체 | 빈 NSC | 내부 T4-T1 (기록 비용 제외) | 차분 start-빈 NSC (중앙값) | verify_inst (판독 비용 제외) | 비용: NS 판독, T1·T4 기록, verify 판독 |')
print('|'+'---|'*9)
for p in ['P1','P2']:
  for clk in ['110','32']:
    for c in ['off','warm','cold']:
      a=f'{p}_{clk}M_{c}_ts0'; b=f'{p}_{clk}M_{c}_ts1'
      if not (os.path.exists(f'{R}/{a}/verify_calib.csv') and os.path.exists(f'{R}/{b}/verify_calib.csv')):
        print(f'| {p} | {clk} | {c} | 미측정 |'); continue
      total=col(a,'start_total'); empty=col(a,'empty_nsc'); 
      icalib=min(col(b,'inner_calib')); inner=[x-icalib for x in col(b,'start_total','inner_t4_t1')]
      vcal=min(col(a,'verify_calib')); ver=[x-vcal for x in col(a,'verify')]
      print(f"| {p} | {clk} | {c} | {fmt(total)} | {fmt(empty)} | {fmt(inner)} | {st.median(total)-st.median(empty):.0f} | {fmt(ver)} | {min(col(a,'ns_calib'))}, {icalib}, {vcal} |")
print('\n실측 환경 (env.txt)\n\n| 조건 | SystemCoreClock | FLASH_ACR | ICACHE_CR |\n|---|---|---|---|')
for clk in ['110','32']:
  for c in ['off','warm']:
    e=dict(l.strip().split(' = ') for l in open(f'{R}/P1_{clk}M_{c}_ts0/env.txt'))
    print(f"| {clk}MHz {c} | {e['SystemCoreClock']} | {e['FLASH_ACR']} | {e['ICACHE_CR']} |")
