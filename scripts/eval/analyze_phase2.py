#!/usr/bin/env python3
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from stats import *
root='results/phase2'
def s3(x): return f"{x['min']}/{x['avg']}/{x['max']}/{x['std']}"
print('## 무계측(ts0): min/avg/max/stdev, N=1000 (표본에 틱 선점 포함)\n')
print('| 변형 | calib NS | calib S | 빈 NSC | 중재자(T0-T5) | 중재자 IRQ 차단 | 왕복(T0-T6) | verify_inst | TTT×2 사본 | 거부 수 |')
print('|'+'---|'*10)
for p in ['P0','P1','P2']:
  for c in ['off','warm','cold']:
    d=f'{root}/{p}_{c}_ts0'
    if not os.path.exists(d+'/t7.csv'): print(f'| {p}_{c} | 미측정 (배치 게이트 실패, 승인 대기) |'); continue
    t4=load(d+'/t4.csv'); t5=load(d+'/t5.csv')
    rej=sum(1 for r in t4 if r['n4']!='0')+sum(1 for r in t5 if r['n4']!='0')
    print(f"| {p}_{c} | {s3(summ(delta(load(d+'/t1.csv'),'n0','n1')))} | {s3(summ(delta(load(d+'/t2.csv'),'s0','s1')))} | {s3(summ(delta(load(d+'/t3.csv'),'n0','n1')))} | {s3(summ(delta(t4,'n0','n1')))} | {s3(summ(delta(t5,'n0','n1')))} | {s3(summ(delta(t4,'n0','n2')))} | {s3(summ(delta(load(d+'/t6.csv'),'s0','s1')))} | {s3(summ(delta(load(d+'/t7.csv'),'s0','s1')))} | {rej} |")
print('\n## 구간 분해(ts1): 구간별 중앙값 (min/avg/max/stdev 는 CSV 참조). 합계는 타임스탬프 비용 포함\n')
seg=[('T0→T1 진입','n0','s1'),('T1→T2 검증','s1','s2'),('T2→T3 레지스터','s2','s3'),('T3→T4 EN','s3','s4'),('T4→ISR 진입','s4','s5'),('ISR 처리','s5','s6'),('콜백 전→NS 콜백','s6','n3'),('NS 콜백→ISR 복귀','n3','s7'),('ISR 복귀→T5','s7','n1'),('T5→T6','n1','n2')]
print('| 변형 | '+' | '.join(x[0] for x in seg)+' | 합계 T0→T5 |')
print('|'+'---|'*(len(seg)+2))
for p in ['P0','P1','P2']:
  for c in ['off','warm','cold']:
    d=f'{root}/{p}_{c}_ts1'
    if not os.path.exists(d+'/t4.csv'): print(f'| {p}_{c} | 미측정 (배치 게이트 실패, 승인 대기) |'); continue
    r=load(d+'/t4.csv')
    sg=lambda v:[x-(1<<32) if x>=(1<<31) else x for x in v]
    print(f"| {p}_{c} | "+' | '.join(str(int(summ(sg(delta(r,a,b)))['med'])) for _,a,b in seg)+f" | {int(summ(delta(r,'n0','n1'))['med'])} |")
print('\n## 계측 왜곡: 같은 조건 T0→T5 중앙값 ts1 - ts0\n')
print('| 변형 | ts0 | ts1 | 차 |\n|---|---|---|---|')
for p in ['P0','P1','P2']:
  for c in ['off','warm','cold']:
    try:
      a=summ(delta(load(f'{root}/{p}_{c}_ts0/t4.csv'),'n0','n1'))['med']; b=summ(delta(load(f'{root}/{p}_{c}_ts1/t4.csv'),'n0','n1'))['med']
      print(f'| {p}_{c} | {a} | {b} | {b-a} |')
    except Exception: print(f'| {p}_{c} | 미측정 | | |')
print('\n## 중앙값 요약: ISR 몫 = 중재자 - 차단값\n')
print('| 변형 | 빈 NSC | 차단값 | 중재자 | ISR 몫 | 차단값-빈 NSC |\n|---|---|---|---|---|---|')
for p in ['P0','P1','P2']:
  for c in ['off','warm','cold']:
    d=f'{root}/{p}_{c}_ts0'
    try:
      e=summ(delta(load(d+'/t3.csv'),'n0','n1'))['med'];m=summ(delta(load(d+'/t5.csv'),'n0','n1'))['med'];f=summ(delta(load(d+'/t4.csv'),'n0','n1'))['med']
      print(f'| {p}_{c} | {e} | {m} | {f} | {f-m} | {m-e} |')
    except Exception: print(f'| {p}_{c} | 미측정 |||||')

print('\n## 구간별 전체 통계(ts1, N=1000): min/avg/max/stdev\n')
print('| 변형 | 구간 | min | avg | max | stdev |\n|---|---|---|---|---|---|')
for p in ['P1','P2']:
  for c in ['off','warm','cold']:
    r=load(f'{root}/{p}_{c}_ts1/t4.csv')
    sg=lambda v:[x-(1<<32) if x>=(1<<31) else x for x in v]
    for n,a,b in seg:
        x=summ(sg(delta(r,a,b))); print(f"| {p}_{c} | {n} | {x['min']} | {x['avg']} | {x['max']} | {x['std']} |")
