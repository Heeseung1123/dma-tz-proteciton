#!/usr/bin/env python3
"""DMA 완료 ISR 비용: Secure(CH1 + doorbell) vs Insecure(CH2) 집계 (cmpISRoverhead.md §12).

입력: <R>/P2_110M_warm_its{k}/{isr_cal_s,isr_s,isr_cal_n,isr_n}.csv  (k = 0..5, BENCH_ISR_TS=k 빌드)
  열0 = t1 - T0 (구간 전체), 열1 = Tk - T0 (그 빌드의 스탬프 k, 없으면 0)
계산 (CAL = 같은 빌드, 같은 경로 CAL 표 열0 의 min):
  ISR  = 열0(k=0) - CAL                       구간 전체 (전송 시간·계측 비용 제외)
  P_k  = median(열1(k)) - CAL(k)              T0 -> 스탬프 k 까지 (k=1 Ta, 2 Tb, 3 Tn, 4 Tc, 5 Td)
  구간 = 인접 P_k 의 차, 마지막 = ISR - P_5   (합 = ISR, 구성상)
사용: python3 scripts/eval/analyze_isr.py [results/isr] [P2_110M_warm] > results/isr/summary.md
      (두 번째 인자 = 변형 접두사, 예 P2_110M_off: ICACHE off 대조)"""
import csv, os, sys, statistics as st

R = sys.argv[1] if len(sys.argv) > 1 else 'results/isr'
PFX = sys.argv[2] if len(sys.argv) > 2 else 'P2_110M_warm'
V = PFX + '_its{}'
NAME = {1: 'Ta', 2: 'Tb', 3: 'Tn', 4: 'Tc', 5: 'Td'}


def load(k, t):
    f = f'{R}/{V.format(k)}/{t}.csv'
    if not os.path.exists(f):
        return None
    with open(f) as h:
        rows = list(csv.reader(h))[1:]
    return [int(r[0]) for r in rows], [int(r[1]) for r in rows]


def cell(x):
    return f'{st.median(x):.0f} ({min(x)}/{st.mean(x):.1f}/{max(x)})'


data = {k: {t: load(k, t) for t in ('isr_cal_s', 'isr_s', 'isr_cal_n', 'isr_n')} for k in range(6)}
if any(v is None for v in data[0].values()):
    sys.exit(f'{R}/{V.format(0)} 의 CSV 가 없다 (k=0 빌드는 필수)')

checks = []   # (항목, 결과, 통과 여부)


def path(p):
    """p = 's' 또는 'n'. 반환: cal(k), isr 목록(k=0), 스탬프 지점 P_k(median)"""
    cal = {k: min(data[k][f'isr_cal_{p}'][0]) for k in range(6) if data[k][f'isr_cal_{p}']}
    isr = [x - cal[0] for x in data[0][f'isr_{p}'][0]]
    pts = {}
    for k in range(1, 6):
        d = data[k][f'isr_{p}']
        if d is None:
            continue
        ts = d[1]
        if all(v == 0 for v in ts):           # 이 경로에서는 실행되지 않는 스탬프 (N 의 Ta, Tb)
            continue
        if any(v == 0 for v in ts):
            checks.append((f'{p.upper()} k={k} 스탬프 누락 행', f'{sum(v == 0 for v in ts)} 행', False))
        pts[k] = st.median([v for v in ts if v]) - cal[k]
    return cal, isr, pts


cal_s, isr_s, pt_s = path('s')
cal_n, isr_n, pt_n = path('n')
m_s, m_n = st.median(isr_s), st.median(isr_n)

print(f'# DMA 완료 ISR 집계 ({PFX}, N=1000)\n')
print(f'원자료 `{R}/{V.format("{0..5}")}/*.csv`. 방법은 `cmpISRoverhead.md` §12. 값은 사이클, 중앙값 (min/avg/max).\n')

print('## 1. 구간 전체: TC pending 직후 ~ NS ISR 복귀 (k=0 빌드)\n')
print('| 경로 | ISR = t1−T0−CAL | CAL (min) |')
print('|---|---|---|')
print(f'| S (Secure DMA, CH1 + doorbell) | {cell(isr_s)} | {cal_s[0]} |')
print(f'| N (Insecure DMA, CH2) | {cell(isr_n)} | {cal_n[0]} |')
print(f'| **S − N** | **{m_s - m_n:+.0f}** ({m_s / m_n:.2f}x) | |\n')

# 구간 분해
SEG_S = [('HW 진입 (NS→S)', None, 1), ('Secure ISR 본문', 1, 2), ('Secure 퇴장 + tail-chain', 2, 3),
         ('NS prologue', 3, 4), ('NS 본문', 4, 5), ('NS epilogue + HW 복귀', 5, None)]
SEG_N = {'HW 진입 (NS→NS)': (None, 3), 'NS prologue': (3, 4), 'NS 본문': (4, 5), 'NS epilogue + HW 복귀': (5, None)}


def seg(pts, total, a, b):
    lo = 0 if a is None else pts.get(a)
    hi = total if b is None else pts.get(b)
    return None if lo is None or hi is None else hi - lo


def fmt(x):
    return '미측정' if x is None else f'{x:.0f}'


print('## 2. 구간 분해 (중앙값끼리의 차)\n')
print('| 구간 | S | N | S − N | 스탬프 |')
print('|---|---|---|---|---|')
sum_s = sum_n = 0
for name, a, b in SEG_S:
    vs = seg(pt_s, m_s, a, b)
    key = 'HW 진입 (NS→NS)' if name.startswith('HW 진입') else name
    vn = seg(pt_n, m_n, *SEG_N[key]) if key in SEG_N else 0
    sum_s += vs or 0
    sum_n += vn or 0
    diff = '' if vs is None or vn is None else f'{vs - vn:+.0f}'
    stamp = f'{NAME.get(a, "T0")} → {NAME.get(b, "t1")}'
    if key in SEG_N and name.startswith('HW 진입'):
        stamp += ' (N: T0 → Tn)'
    print(f'| {name} | {fmt(vs)} | {fmt(vn) if key in SEG_N else "— (없음)"} | {diff} | {stamp} |')
print(f'| **합** | **{sum_s:.0f}** | **{sum_n:.0f}** | **{sum_s - sum_n:+.0f}** | = 구간 전체 |\n')

print('스탬프 지점 P_k = median(Tk−T0) − CAL(k):\n')
print('| 지점 | S | N |')
print('|---|---|---|')
for k in range(1, 6):
    print(f'| k={k} {NAME[k]} | {fmt(pt_s.get(k))} | {"— (없음)" if k < 3 else fmt(pt_n.get(k))} |')

# 검증
print('\n## 3. 검증\n')
for p, pts, total in (('S', pt_s, m_s), ('N', pt_n, m_n)):
    seq = [pts[k] for k in sorted(pts)]
    ok = all(x < y for x, y in zip(seq, seq[1:])) and (not seq or (0 < seq[0] and seq[-1] < total))
    checks.append((f'{p}: 0 < P_k 증가 < ISR', ', '.join(f'{v:.0f}' for v in seq) + f' < {total:.0f}', ok))
cal_ts = [max(data[k][t][1]) for k in range(6) for t in ('isr_cal_s', 'isr_cal_n') if data[k][t]]
checks.append(('CAL 표 열1 = 0 (보정 중 ISR 미실행, 전 빌드)', f'최대 {max(cal_ts)}', max(cal_ts) == 0))
n_sec = [max(data[k]['isr_n'][1]) for k in (1, 2) if data[k]['isr_n']]
if n_sec:
    checks.append(('k=1,2 isr_n 열1 = 0 (N 경로는 Secure ISR 미실행)', f'최대 {max(n_sec)}', max(n_sec) == 0))
c = [cal_s[0], cal_n[0]]
checks.append(('CAL S ≈ N (같은 구간 명령)', f'{c[0]} vs {c[1]}', abs(c[0] - c[1]) <= 2))
print('| 항목 | 값 | 판정 |')
print('|---|---|---|')
for name, val, ok in checks:
    print(f'| {name} | {val} | {"OK" if ok else "**확인 필요**"} |')

# 스탬프 하나가 그 빌드의 전체 값에 더한 비용 (스탬프가 레지스터 할당을 바꾸지 않았는지 보는 참고값)
print('\n## 4. 빌드별 구간 전체 (스탬프 1 개의 영향, 참고)\n')
print('| k | S: ISR(k) | S: −ISR(0) | N: ISR(k) | N: −ISR(0) | CAL S / N |')
print('|---|---|---|---|---|---|')
for k in range(6):
    if not data[k]['isr_s'] or not data[k]['isr_n']:
        print(f'| {k} | 미측정 | | | | |')
        continue
    a = st.median([x - cal_s[k] for x in data[k]['isr_s'][0]])
    b = st.median([x - cal_n[k] for x in data[k]['isr_n'][0]])
    print(f'| {k} | {a:.0f} | {a - m_s:+.0f} | {b:.0f} | {b - m_n:+.0f} | {cal_s[k]} / {cal_n[k]} |')
print('\n스탬프가 실행되는 경로에서는 저장 2 명령 + literal 1 개만큼 커지고, 실행되지 않는 경로는 0 근처여야 한다.')
