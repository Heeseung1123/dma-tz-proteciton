#!/usr/bin/env bash
# build_secure.sh — ④ Secure 최종 빌드 (GCC, CubeIDE 와 같은 플래그) + ⑤ veneer 비교
#   - 소스: secure_common.sh (src/ 사본 우선) + secure_dma_boot.c / secure_guard_bind.c / secure_dma_entry.S
#   - 링커 스크립트: src/ 사본 (INCLUDE secure_guard_mem.ld / secure_guard_sec.ld, -L build/secure/gen)
#   - --in-implib=⓪ 로 기존 veneer 주소를 유지, --out-implib 는 build/secure/secure_nsclib.o
set -euo pipefail
TOOLS=$(cd "$(dirname "$0")/.." && pwd)
source "$TOOLS/scripts/secure_common.sh"
NM="$GCC_BIN/arm-none-eabi-nm"
SIZE="$GCC_BIN/arm-none-eabi-size"

# CubeIDE Secure/Debug/subdir.mk 그대로 (-fcyclomatic-complexity 는 ST GCC 전용이라 뺌, 산출물에 영향 없음)
CFLAGS="$S_ARCH -std=gnu11 -g3 -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -mcmse --specs=nano.specs \
 $S_DEFS $S_INCS"

[ -f "$S0_IMPLIB" ] || { echo "⓪ import library 없음: $S0_IMPLIB"; exit 1; }
[ -f "$S_OUT/gen/secure_guard_mem.ld" ] || { echo "③ 생성물 없음: build_secure_guard.sh 먼저"; exit 1; }
rm -rf "$S_OUT/obj"; mkdir -p "$S_OUT/obj"
OBJS=()
for r in "${S_SRCS_REL[@]}"; do
  o="$S_OUT/obj/$(echo "$r" | sed 's#[/.]#_#g').o"
  "$GCC" $CFLAGS -c "$(ov "$r")" -o "$o"; OBJS+=("$o")
done
for r in "${S_ADD_C[@]}"; do
  o="$S_OUT/obj/$(basename "$r" .c).o"; "$GCC" $CFLAGS -c "$TOOLS/$r" -o "$o"; OBJS+=("$o")
done
for r in "${S_ADD_ASM[@]}"; do
  o="$S_OUT/obj/$(basename "$r" .S).o"; "$GCC" $S_ARCH -g3 -c -x assembler-with-cpp "$TOOLS/$r" -o "$o"; OBJS+=("$o")
done
o="$S_OUT/obj/startup.o"
"$GCC" $S_ARCH -g3 -DDEBUG -c -x assembler-with-cpp "$(ov "$S_STARTUP_REL")" -o "$o"; OBJS+=("$o")

ELF="$S_OUT/secure.elf"
"$GCC" -o "$ELF" "${OBJS[@]}" $S_ARCH -T"$S_LDS" -L"$S_OUT/gen" --specs=nosys.specs -Wl,-Map="$ELF.map" \
  -Wl,--gc-sections -static -Wl,--cmse-implib -Wl,--in-implib="$S0_IMPLIB" -Wl,--out-implib="$S_OUT/secure_nsclib.o" \
  --specs=nano.specs -Wl,--start-group -lc -lm -Wl,--end-group
"$SIZE" "$ELF"

echo "== ⑤ veneer 비교 (⓪ vs ④)"
"$NM" -n "$S0_IMPLIB" > "$S_OUT/implib0.txt"
"$NM" -n "$S_OUT/secure_nsclib.o" > "$S_OUT/implib4.txt"
if diff "$S_OUT/implib0.txt" "$S_OUT/implib4.txt"; then
  echo "veneer 동일 ($(wc -l < "$S_OUT/implib0.txt") 개) → NS 재링크 불필요"
else
  echo "FAIL: veneer 주소/집합이 ⓪ 과 다름"; exit 1
fi
