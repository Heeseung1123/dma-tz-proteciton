#!/usr/bin/env bash
# Secure main.c 의 하드코딩(Task A Cluster 0x2001b700)과 NS 배치가 일치하는지 확인.
set -euo pipefail
ELF="$1"; EXPECT=2001b600
GOT=$(arm-none-eabi-nm "$ELF" | awk '$3=="s_dmaTaskMem"{print $1}')
if [ "$GOT" != "$EXPECT" ]; then echo "LAYOUT FAIL: s_dmaTaskMem=$GOT expected=$EXPECT ($ELF)"; exit 3; fi
echo "layout ok: s_dmaTaskMem=$GOT"
