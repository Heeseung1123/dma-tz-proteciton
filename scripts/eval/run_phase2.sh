#!/usr/bin/env bash
# Phase 2 전체 행렬: 프로파일 3 x 캐시 3 x 타임스탬프 유무
cd "$(dirname "$0")/../.."
for prof in P1 P0 P2; do
  for cache in off warm cold; do
    case $cache in off) C="";; warm) C="BENCH_ICACHE";; cold) C="BENCH_ICACHE BENCH_COLD";; esac
    for ts in 0 1; do
      T=""; [ $ts = 1 ] && T="BENCH_TS"
      v=${prof}_${cache}_ts$ts
      echo "=== $v $(date +%T)"
      TMO=90 WALL=400 scripts/eval/run_variant.sh phase2 $v $prof BENCH_PHASE=2 $C $T 2>&1 | tail -1 | cut -c1-160
    done
  done
done
echo ALLDONE
