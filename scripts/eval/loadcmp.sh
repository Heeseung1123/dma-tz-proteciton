#!/usr/bin/env bash
# loadcmp.sh a.elf b.elf : two ELFs load the same bytes and define the same symbols?
# Compares the contents of every alloc PROGBITS section (objdump -s) and the symbol table (nm -n -S).
# Debug info is ignored, so a source edit that only moves line numbers reports "same" (cmpMemUsage_result.md §7.1 M2).
set -euo pipefail
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin}"
export PATH="$TOOLCHAIN_BIN:$PATH"
f(){ for s in $(arm-none-eabi-readelf -SW "$1" | awk '/ PROGBITS / && $0 ~ / [WAX]+ /{for(i=1;i<=NF;i++) if($i ~ /^\./){print $i; break}}'); do
       echo "== $s"; arm-none-eabi-objdump -s -j "$s" "$1" | tail -n +4; done
     arm-none-eabi-nm -n -S "$1"; }
if diff <(f "$1") <(f "$2") >/dev/null; then echo "same: $1 $2"; else echo "DIFF: $1 $2"; exit 1; fi
