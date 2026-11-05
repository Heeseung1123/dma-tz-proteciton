#!/usr/bin/env bash
# run_variant.sh <phase> <variant> <P0|P1|P2> [매크로...]: 클린 빌드 -> flash -> 측정 -> CSV
set -euo pipefail
cd "$(dirname "$0")/../.."
PHASE="$1"; VARIANT="$2"; PROFILE="$3"; shift 3
scripts/eval/build_variant.sh "$PHASE" "$VARIANT" "$PROFILE" "$@"
DIR="results/$PHASE/$VARIANT" TMO="${TMO:-60}" timeout "${WALL:-500}" gdb-multiarch -batch -x scripts/eval/run_bench.py 2>&1 | grep -vE "^(Loading section|Start address|Transfer rate|add symbol|Reset_Handler|[0-9]+\s)" | tail -25
tail -1 "results/$PHASE/$VARIANT/run.log"
