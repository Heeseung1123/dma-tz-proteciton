#!/usr/bin/env bash
# DmaCluster pass 단위 시험: clang -O1 -fno-inline → opt -passes=dma-cluster → JSON 검사
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); TOOLS=$(cd "$T/../.." && pwd)
[ -n "${LLVM:-}" ] || source "$TOOLS/scripts/env_llvm18.sh"
O=$TOOLS/build/t_pass; mkdir -p "$O"
"$LLVM/clang" --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -O1 -fno-inline -g -S -emit-llvm \
  "$T/precision.c" -o "$O/precision.ll"
DMA_CLUSTER_CONFIG=$T/config.json DMA_CLUSTER_OUT=$O/precision.json \
  "$LLVM/opt" -load-pass-plugin="$TOOLS/build/pass/DmaCluster.so" -passes=dma-cluster \
  "$O/precision.ll" -S -o "$O/precision_placed.ll" 2> "$O/precision.log"
python3 "$T/check_precision.py" "$O/precision.json"
grep -q 'section ".dma.t.taskA"' "$O/precision_placed.ll" && echo "ok   IR 에 section .dma.t.taskA 부여"
