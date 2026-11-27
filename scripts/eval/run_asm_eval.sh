#!/usr/bin/env bash
# 스택 없는 중재자 (gap H): 기능 확인 2 빌드 -> 사이클 24 빌드 -> 집계. 먼저 ~/st-link.sh 실행.
#   scripts/eval/run_asm_eval.sh            전부
#   SKIP_FUNC=1 / SKIP_CYC=1                단계 생략
set -u
cd "$(dirname "$0")/../.."
if [ -z "${SKIP_FUNC:-}" ]; then
  for v in "asm P1" "c P1 MEDIATOR_ASM=0"; do
    set -- $v; n=$1; shift
    scripts/eval/build_variant.sh asm_func $n P1 "$@" >/dev/null || { echo "BUILD FAIL $n"; exit 1; }
    DIR=results/asm_func/$n timeout 120 gdb-multiarch -batch -x scripts/eval/check_asm_mediator.py 2>&1 | grep -vE "^(Loading|Start address|Transfer rate|Breakpoint|Hardware)" | cut -c1-200
  done
fi
if [ -z "${SKIP_CYC:-}" ]; then
  PHASE=mediator_asm scripts/eval/run_all.sh
  python3 scripts/eval/analyze_asm.py results/mediator_asm > results/mediator_asm/summary.md
  cat results/mediator_asm/summary.md
fi
