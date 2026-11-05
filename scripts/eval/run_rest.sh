#!/usr/bin/env bash
# Phase 4 / Phase 2 N=10 / 32MHz / Phase 5 측정 일괄 실행
cd "$(dirname "$0")/../.."
run() { echo "=== $* $(date +%T)"; TMO=90 WALL=400 scripts/eval/run_variant.sh "$@" 2>&1 | tail -1 | cut -c1-160; }
for prof in P1 P2; do
  for cache in off warm; do
    C=""; [ $cache = warm ] && C="BENCH_ICACHE"
    run phase4 ${prof}_${cache}_n1000 $prof BENCH_PHASE=4 $C
    run phase4 ${prof}_${cache}_n10 $prof BENCH_PHASE=4 BENCH_N=10 $C
    run phase4 ${prof}_${cache}_dma_n10 $prof BENCH_PHASE=2 BENCH_N=10 $C
    # 32MHz: D-Box 와 같은 클럭
    run phase2 ${prof}_${cache}_ts0_32M $prof BENCH_PHASE=2 BENCH_CLK_32M $C
    run phase4 ${prof}_${cache}_n1000_32M $prof BENCH_PHASE=4 BENCH_CLK_32M $C
  done
done
run phase5 P1_off P1 BENCH_PHASE=5 BENCH_INSECURE_DMA
run phase5 P1_warm P1 BENCH_PHASE=5 BENCH_INSECURE_DMA BENCH_ICACHE
echo ALLDONE
