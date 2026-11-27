#!/usr/bin/env python3
"""문맥 전환 1회 비교 (cmpAsmMediator.md §11).
Phase 4 (커밋 983a749 results/phase4, yield 앞 Secure 호출 없음) 와
§7 (results/decomp, asm 호출) / §9 (results/svc, SVC 경로) 의 V0/V1/V2 를 같은 통계로 나란히 놓는다.
통계: 앞 4 표본 제외, 연속 두 표본(두 방향) 평균의 중앙값. Phase 4 원시 중앙값도 함께 낸다(예전 summary 대조용)."""
import csv, io, statistics as st, subprocess

OLD = '983a749'

def s32(x): return x - 2**32 if x >= 2**31 else x
def pairmed(x): return st.median([(x[i] + x[i + 1]) / 2 for i in range(0, len(x) - 1, 2)])

def old(v, t):
    txt = subprocess.run(['git', 'show', f'{OLD}:results/phase4/{v}/t{t}.csv'],
                         capture_output=True, text=True, check=True).stdout
    d = [s32((int(r['n1']) - int(r['n0'])) & 0xffffffff) for r in csv.DictReader(io.StringIO(txt))][4:]
    return pairmed(d), st.median(d)

def new(R, v, f):
    with open(f'{R}/{v}/{f}.csv') as h:
        return pairmed([s32(int(list(r.values())[0])) for r in csv.DictReader(h)])

conds = [(p, c, k) for p in ['P1', 'P2'] for c in ['110', '32'] for k in ['off', 'warm']]
rows = []
for p, c, k in conds:
    ov = f'{p}_{k}_n1000' + ('_32M' if c == '32' else '')
    nv = f'{p}_{c}M_{k}_ts0'
    (b0, b0r), (b1, b1r) = old(ov, 30), old(ov, 31)
    d = [new('results/decomp', nv, f) for f in ['ctx_v0', 'ctx_v1', 'ctx_v2']]
    s = [new('results/svc', nv, f) for f in ['ctx_v0', 'ctx_v1', 'ctx_v2']]
    rows.append((p, c, k, b0, b1, b0r, b1r, d, s))

print('| 프로파일 | 클럭 | 캐시 | **P4 순수** (ctx 없음, 호출 없음) | P4 ctx (호출 없음) | §7 V0 (asm 호출) | §7 V1 | §7 V2 | §9 V0 (A Task) | §9 V1 | §9 V2 (C Task) | P4 원시 중앙값 순수 / ctx |')
print('|' + '---|' * 12)
for p, c, k, b0, b1, b0r, b1r, d, s in rows:
    print(f'| {p} | {c} | {k} | **{b0:.0f}** | {b1:.0f} | {d[0]:.0f} | {d[1]:.0f} | {d[2]:.0f} | {s[0]:.0f} | {s[1]:.0f} | {s[2]:.0f} | {b0r:.0f} / {b1r:.0f} |')

print('\n순수 전환(P4) 대비 차이\n')
print('| 프로파일 | 클럭 | 캐시 | P4 ctx − 순수 (Save/Load) | §7 V1 − V0 (Save/Load) | §9 V0 − 순수 (A Task) | §9 V2 − 순수 (C Task) | §7 V0 − 순수 |')
print('|' + '---|' * 8)
for p, c, k, b0, b1, b0r, b1r, d, s in rows:
    print(f'| {p} | {c} | {k} | {b1-b0:+.0f} | {d[1]-d[0]:+.0f} | {s[0]-b0:+.0f} | {s[2]-b0:+.0f} | {d[0]-b0:+.0f} |')
