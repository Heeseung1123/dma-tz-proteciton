#!/usr/bin/env python3
import sys, os, glob, re
sys.path.insert(0, os.path.dirname(__file__))
from stats import *
def s4(v):
    k,x=valid(v); s=summ(k); return f"{s['min']}/{s['avg']}/{s['max']}/{s['med']:.0f}"+(f" (wrap {x})" if x else '')
def med(v): return summ(valid(v)[0])['med']
print('## Phase 4 context switch / task creation: min/avg/max/median\n')
print('| 변형 | ctx 미할당 | ctx 할당 | 증가(중앙값) | 생성 256W | 생성 128W | secure ctx 할당 |\n|'+'---|'*7)
for v in ['P1_off_n10','P1_off_n1000','P1_warm_n10','P1_warm_n1000','P2_off_n10','P2_off_n1000','P2_warm_n10','P2_warm_n1000','P1_off_n1000_32M','P1_warm_n1000_32M','P2_off_n1000_32M','P2_warm_n1000_32M']:
    d=f'results/phase4/{v}'
    if not os.path.exists(d+'/t33.csv'): print(f'| {v} | 미측정 |'); continue
    a=delta(load(d+'/t30.csv')[4:],'n0','n1'); b=delta(load(d+'/t31.csv')[4:],'n0','n1')
    ma,mb=med(a),med(b)
    print(f"| {v} | {s4(a)} | {s4(b)} | +{mb-ma:.0f} ({(mb-ma)/ma*100:.1f}%) | {s4(delta(load(d+'/t32.csv'),'n0','n1'))} | {s4(delta(load(d+'/t34.csv'),'n0','n1'))} | {s4(delta(load(d+'/t33.csv'),'n0','n1'))} |")
print('\n## Phase 4 부팅 초기화 (BENCH_PHASE=4, g_bBoot 차이, MSI 클럭 부팅 구간)\n')
print('| 변형 | MX_GTZC_S_Init | Lock channels | Secure_TZIC_Init | SMPU_SetupTaskRegions | 합계 |\n|'+'---|'*6)
for v in ['P1_off_n1000','P1_warm_n1000','P2_off_n1000','P2_warm_n1000']:
    e=[l for l in open(f'results/phase4/{v}/env.txt') if l.startswith('g_bBoot')][0]
    b=eval(e.split('=')[1]); g=b[1]-b[0]; l=b[3]-b[2]; t=b[4]-b[3]; m=b[6]-b[5]
    print(f'| {v} | {g} | {l} | {t} | {m} | {g+l+t+m} |')
print('\n## Phase 4 DMA 검증: min/avg/max/median (S 내부 측정값은 calib S 미보정 원값)\n')
print('| 변형 | N | calib S | verify_inst | TTT×2 사본 | 빈 NSC | 중재자 IRQ 차단 | 중재자 | 왕복 T0-T6 |\n|'+'---|'*9)
L=[]
for p in ['P1','P2']:
  for c in ['off','warm']:
    L+= [(f'{p}_{c}','10',f'results/phase4/{p}_{c}_dma_n10'),(f'{p}_{c}','1000',f'results/phase2/{p}_{c}_ts0'),(f'{p}_{c}_32M','1000',f'results/phase2/{p}_{c}_ts0_32M')]
for name,n,d in L:
    if not os.path.exists(d+'/t7.csv'): print(f'| {name} | {n} | 미측정 |'); continue
    t4=load(d+'/t4.csv'); t5=load(d+'/t5.csv')
    print(f"| {name} | {n} | {s4(delta(load(d+'/t2.csv'),'s0','s1'))} | {s4(delta(load(d+'/t6.csv'),'s0','s1'))} | {s4(delta(load(d+'/t7.csv'),'s0','s1'))} | {s4(delta(load(d+'/t3.csv'),'n0','n1'))} | {s4(delta(t5,'n0','n1'))} | {s4(delta(t4,'n0','n1'))} | {s4(delta(t4,'n0','n2'))} |")
print('\n## 32MHz 변형 플래시 설정\n')
for d in sorted(glob.glob('results/phase2/*_32M')):
    print(os.path.basename(d),[l.strip() for l in open(d+'/env.txt') if l.startswith(('FLASH_ACR','SystemCoreClock'))])
print('\n## Phase 5 M2M: 10회 평균, 첫 예열 패스 제외\n')
for v in ['P1_off','P1_warm']:
    d=f'results/phase5/{v}'
    print(f'\n### {v}\n\n| B | Pool | Insecure DMA | 제안 | 제안-Insecure | 대 Pool % | 대 Insecure % |\n|'+'---|'*7)
    allp=[];alli=[]
    for f in sorted(glob.glob(d+'/t*.csv'),key=lambda x:int(os.path.basename(x)[1:-4])):
        r=load(f); a=summ(delta(r,'n0','n1'))['avg']; b=summ(delta(r,'n2','n3'))['avg']; c=summ(delta(r,'n4','n5'))['avg']
        L_=int(r[0]['arg'])
        if L_<=100: allp.append((c-a)/a*100); alli.append((c-b)/b*100)
        print(f"| {L_} | {a} | {b} | {c} | {c-b:.1f} | {(c-a)/a*100:.1f} | {(c-b)/b*100:.1f} |")
    print(f'\n1~100B 오버헤드 % min/avg/max: Pool {min(allp):.1f}/{sum(allp)/len(allp):.1f}/{max(allp):.1f}, Insecure {min(alli):.1f}/{sum(alli)/len(alli):.1f}/{max(alli):.1f}')
