#!/usr/bin/env bash
# build_secure_guard.sh — Secure 데이터를 guard 블록에 배치하기 위한 분석 + 생성
#
# 전체 빌드 순서 (이 스크립트는 ②, ③ 담당)
#   ⓪ Secure 0차 빌드 (GCC, CubeIDE)          → secure_nsclib.o (veneer 주소)
#   ① NS 빌드 (build_ns.sh, 2-pass)            → ns.elf (guard 주소 확정)
#   ② Secure 분석 (clang → IR → opt, 분석만)   → secure_guard_candidates.json
#   ③ 배치 (pack_guards.py)                    → secure_guard_mem.ld / _sec.ld / guard_bind.h
#   ④ Secure 최종 빌드 (GCC, CubeIDE)          → 생성 ld 를 INCLUDE, -Wl,--in-implib 로 veneer 고정
#   ⑤ veneer 비교 (⓪과 ④의 implib 심볼이 같아야 NS 를 다시 링크하지 않아도 됨)
set -euo pipefail

TOOLS=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${ROOT:-$HOME/dma-tz}
SDIR=${SDIR:-$ROOT/project/Secure}                     # Secure 프로젝트 (CubeIDE)
OUT=${OUT:-$ROOT/build/secure}
NS_ELF=${NS_ELF:-$ROOT/build/ns/ns.elf}
LLVM=${LLVM:-/usr/lib/llvm-18/bin}
PLUGIN=${PLUGIN:-$TOOLS/build/pass/SecureGuardSelect.so}
GCC_ROOT=$(dirname "$(dirname "$(command -v arm-none-eabi-gcc)")")

# GCC 빌드와 같은 -D / -I (CubeIDE Secure/Debug/**/subdir.mk 에서 옮겨 적기)
DEFS="-DUSE_HAL_DRIVER -DSTM32L562xx -DDEBUG"
INCS="-I$SDIR/Core/Inc -I$ROOT/project/Secure_nsclib -I$TOOLS/common -I$OUT/gen \
 -I$ROOT/Drivers/STM32L5xx_HAL_Driver/Inc -I$ROOT/Drivers/CMSIS/Device/ST/STM32L5xx/Include \
 -I$ROOT/Drivers/CMSIS/Include \
 -I$ROOT/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33/secure"
CFLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 \
 -mcmse -O1 -g -fno-common --sysroot=$GCC_ROOT/arm-none-eabi $DEFS $INCS"

cd "$ROOT"
mkdir -p "$OUT/ir" "$OUT/gen"
[ -f "$OUT/gen/guard_bind.h" ] || printf '#define GUARD_BIND_N 0u\nstatic const unsigned k_guard_bind_ns[1]={0};\n' > "$OUT/gen/guard_bind.h"

echo "== ② Secure 소스 → LLVM IR (분석 전용, 실제 바이너리는 GCC 가 만든다)"
SRCS=( $(find "$SDIR/Core/Src" "$ROOT/Drivers/STM32L5xx_HAL_Driver/Src" -name '*.c' ! -name '*_template.c') \
       $ROOT/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33/secure/*.c \
       "$TOOLS/secure/secure_dma_boot.c" "$TOOLS/secure/secure_guard_bind.c" )
BCS=()
for s in "${SRCS[@]}"; do
  b="$OUT/ir/$(basename "$s" .c).bc"
  "$LLVM/clang" $CFLAGS -emit-llvm -c "$s" -o "$b"
  BCS+=("$b")
done
"$LLVM/llvm-link" "${BCS[@]}" -o "$OUT/secure.bc"        # 파일들을 하나의 IR 모듈로

export SECURE_GUARD_CONFIG=${SECURE_GUARD_CONFIG:-$TOOLS/config/secure_guard_config.json}
"$LLVM/opt" -load-pass-plugin="$PLUGIN" -passes=secure-guard-select -disable-output "$OUT/secure.bc"

echo "== ③ guard 블록에 나눠 담기"
CAND=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['out'])" "$SECURE_GUARD_CONFIG")
python3 "$TOOLS/gen/pack_guards.py" --candidates "$CAND" --ns-elf "$NS_ELF" --out-dir "$OUT/gen" --flash FLASH

cat <<EOF

== 다음: ④ Secure 최종 빌드 (CubeIDE/GCC)
  - Secure 링커 스크립트에 추가:
      INCLUDE $OUT/gen/secure_guard_mem.ld      (최상위, MEMORY 다음)
      INCLUDE $OUT/gen/secure_guard_sec.ld      (SECTIONS 안, .bss 규칙보다 앞)
  - 컴파일 옵션: -fdata-sections (CubeIDE 기본값), include 경로에 $OUT/gen 추가
  - 링크 옵션 : -Wl,--in-implib=<⓪의 secure_nsclib.o>  (veneer 주소 유지)
  - 빌드 후 ⑤: arm-none-eabi-nm -n <⓪ implib> > a.txt; arm-none-eabi-nm -n <④ implib> > b.txt; diff a.txt b.txt
EOF
