#!/usr/bin/env bash
# 전체 측정: {P1,P2} x {off,warm,cold} x {110,32MHz} x {타임스탬프 없음/있음}. 먼저 ~/st-link.sh 실행.
cd "$(dirname "$0")/../.."
PHASE=${PHASE:-mediator}   # 결과 디렉터리 results/$PHASE (예: PHASE=table, PHASE=table_best EXTRA_DEFS=BENCH_TBL_BEST)
for prof in P1 P2; do for clk in 110 32; do for cache in off warm cold; do for ts in 0 1; do
  D="BENCH_PHASE=1 ${EXTRA_DEFS:-}"   # EXTRA_DEFS: 추가 매크로 (예: BENCH_TBL_BEST)
  [ $clk = 32 ] && D="$D BENCH_CLK_32M"
  [ $cache != off ] && D="$D BENCH_ICACHE"
  [ $cache = cold ] && D="$D BENCH_COLD"
  [ $ts = 1 ] && D="$D BENCH_TS"
  v=${prof}_${clk}M_${cache}_ts$ts
  scripts/eval/build_variant.sh $PHASE $v $prof $D >/dev/null || { echo "BUILD/LAYOUT FAIL $v"; continue; }
  DIR=results/$PHASE/$v timeout 240 gdb-multiarch -batch -x scripts/eval/run_bench.py 2>&1 | grep -E "^done|Error|TIMEOUT" | cut -c1-200
done; done; done; done
echo ALLDONE
