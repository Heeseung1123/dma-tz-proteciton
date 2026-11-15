#!/usr/bin/env python3
"""results/table 의 CSV 로 S-MPU 태그(verify_inst) 대 Flash 테이블(verify_table) 비교 표를 만든다.
값은 중앙값, 괄호는 min/avg/max (N=1000). 같은 빌드(같은 ELF) 안에서 두 경로를 잰 값이다.
  verify      = VERIFY(_TABLE) - min(VERIFY_CALIB)                 (ts0)
  바깥(NSC 포함) = START(_TABLE) 열0 - min(NS_CALIB)                  (ts0)
  안쪽(NSC 제외) = START(_TABLE) 열1 - min(INNER_CALIB)               (ts1)
사용: python3 scripts/eval/analyze_table.py [results/table]"""
import csv, os, sys, statistics as st
R = sys.argv[1] if len(sys.argv) > 1 else 'results/table'
def col(v, f, c=None):
    with open(f'{R}/{v}/{f}.csv') as h:
        return [int(r[c or f]) for r in csv.DictReader(h)]
def med(x): return st.median(x)
def mma(x): return f"{min(x)}/{round(st.mean(x),1)}/{max(x)}"
def cell(x): return f"{med(x):.0f} ({mma(x)})"

rows = []
for p in ['P1', 'P2']:
  for clk in ['110', '32']:
    for c in ['off', 'warm', 'cold']:
      a, b = f'{p}_{clk}M_{c}_ts0', f'{p}_{clk}M_{c}_ts1'
      if not all(os.path.exists(f'{R}/{v}/verify_table.csv') for v in (a, b)):
        rows.append((p, clk, c, None)); continue
      vcal = min(col(a, 'verify_calib')); ncal = min(col(a, 'ns_calib')); ical = min(col(b, 'inner_calib'))
      d = dict(
        v_smpu=[x - vcal for x in col(a, 'verify')],
        v_tbl=[x - vcal for x in col(a, 'verify_table')],
        o_smpu=[x - ncal for x in col(a, 'start_total')],
        o_tbl=[x - ncal for x in col(a, 'start_table_total')],
        i_smpu=[x - ical for x in col(b, 'start_total', 'inner_t4_t1')],
        i_tbl=[x - ical for x in col(b, 'start_table_total', 'inner_table_t4_t1')],
        cal=(ncal, ical, vcal))
      rows.append((p, clk, c, d))

def table(title, ks, kt):
  print(f'\n### {title}\n')
  print('| 프로파일 | 클럭 | 캐시 | S-MPU (verify_inst 경로) | 테이블 (verify_table 경로) | 차이 (중앙값) | 배율 |')
  print('|---|---|---|---|---|---|---|')
  for p, clk, c, d in rows:
    if d is None: print(f'| {p} | {clk} | {c} | 미측정 | | | |'); continue
    s, t = d[ks], d[kt]
    print(f'| {p} | {clk} | {c} | {cell(s)} | {cell(t)} | {med(t)-med(s):+.0f} | {med(t)/med(s):.1f}x |')

table('검증 함수 단독 (verify_inst vs verify_table, 판독 비용 제외)', 'v_smpu', 'v_tbl')
table('중재자 안쪽 (Secure 진입 후 ~ DMA EN, T4-T1, 기록 비용 제외)', 'i_smpu', 'i_tbl')
table('중재자 바깥 (NS 호출 ~ 복귀, NSC 포함, NS 판독 비용 제외)', 'o_smpu', 'o_tbl')
print('\n보정값 (min): NS 판독, T1·T4 기록, verify 판독\n')
print('| 프로파일 | 클럭 | 캐시 | 보정값 |\n|---|---|---|---|')
for p, clk, c, d in rows:
  if d: print(f'| {p} | {clk} | {c} | {", ".join(map(str, d["cal"]))} |')

# 최선(항목 0 적중, results/table_best) vs 최악(항목 7 적중, results/table) 비교.
# 사용: python3 scripts/eval/analyze_table.py --best   (두 디렉터리 모두 필요)
if '--best' in sys.argv:
  W, B = 'results/table', 'results/table_best'
  def c2(root, v, f, k=None):
    with open(f'{root}/{v}/{f}.csv') as h:
      return [int(r[k or f]) for r in csv.DictReader(h)]
  def paths(root, p, clk, c):
    a, b = f'{p}_{clk}M_{c}_ts0', f'{p}_{clk}M_{c}_ts1'
    vcal = min(c2(root, a, 'verify_calib')); ncal = min(c2(root, a, 'ns_calib')); ical = min(c2(root, b, 'inner_calib'))
    return dict(
      v_smpu=med([x - vcal for x in c2(root, a, 'verify')]),
      v_tbl=med([x - vcal for x in c2(root, a, 'verify_table')]),
      i_smpu=med([x - ical for x in c2(root, b, 'start_total', 'inner_t4_t1')]),
      i_tbl=med([x - ical for x in c2(root, b, 'start_table_total', 'inner_table_t4_t1')]),
      o_smpu=med([x - ncal for x in c2(root, a, 'start_total')]),
      o_tbl=med([x - ncal for x in c2(root, a, 'start_table_total')]))
  for title, ks, kt in [('검증 함수만', 'v_smpu', 'v_tbl'), ('중재자 안쪽 (T4-T1)', 'i_smpu', 'i_tbl'),
                        ('중재자 바깥 (NSC 포함)', 'o_smpu', 'o_tbl')]:
    print(f'\n### 최선/최악 적중: {title} (중앙값)\n')
    print('| 프로파일 | 클럭 | 캐시 | S-MPU (최악 빌드) | S-MPU (최선 빌드) | 테이블 최선 (항목 0) | 테이블 최악 (항목 7) | 최선−S-MPU | 최악−S-MPU | 최악−최선 |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    for p in ['P1', 'P2']:
      for clk in ['110', '32']:
        for c in ['off', 'warm', 'cold']:
          try: w, b = paths(W, p, clk, c), paths(B, p, clk, c)
          except FileNotFoundError: print(f'| {p} | {clk} | {c} | 미측정 |'); continue
          print(f'| {p} | {clk} | {c} | {w[ks]:.0f} | {b[ks]:.0f} | {b[kt]:.0f} | {w[kt]:.0f} | '
                f'{b[kt]-b[ks]:+.0f} | {w[kt]-w[ks]:+.0f} | {w[kt]-b[kt]:+.0f} |')
