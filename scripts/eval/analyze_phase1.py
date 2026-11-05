#!/usr/bin/env python3
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from stats import *
root = 'results/phase1'
print('| 조건 | NS 판독 차 min/avg/max | S 판독 차 min/avg/max | ICACHE_CR | ')
print('|---|---|---|---|')
for v in ['P1_off','P1_warm','P1_cold']:
    d=f'{root}/{v}'
    a=summ(delta(load(d+'/t1.csv'),'n0','n1')); b=summ(delta(load(d+'/t2.csv'),'s0','s1'))
    env=[l for l in open(d+'/env.txt') if l.startswith('ICACHE_CR')][0].strip()
    print(f"| {v} | {a['min']}/{a['avg']}/{a['max']} | {b['min']}/{b['avg']}/{b['max']} | {env} |")
print()
print('| 조건 | NOP | NS 경계 (NS 코드) | S 내부 (S 코드) | NS 에서 본 S 호출 |')
print('|---|---|---|---|---|')
for v in ['P1_off','P1_warm','P1_cold']:
    d=f'{root}/{v}'
    for w,n in enumerate([256,1024,4096]):
        a=summ(delta(load(f'{d}/t{10+w}.csv'),'n0','n1')); r=load(f'{d}/t{20+w}.csv')
        b=summ(delta(r,'s0','s1')); c=summ(delta(r,'n0','n1'))
        print(f"| {v} | {n} | {a['min']}/{a['avg']}/{a['max']} | {b['min']}/{b['avg']}/{b['max']} | {c['min']}/{c['avg']}/{c['max']} |")
