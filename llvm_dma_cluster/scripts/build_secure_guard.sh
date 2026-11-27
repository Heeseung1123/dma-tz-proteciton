#!/usr/bin/env bash
# build_secure_guard.sh — Secure 데이터를 guard 블록에 배치하기 위한 분석 + 생성 (②, ③)
#
# 전체 빌드 순서 (scripts/build_all.sh)
#   ⓪ Secure 0차 빌드 (GCC, CubeIDE baseline)  → secure_nsclib.o (veneer 주소)
#   ① NS 빌드 (build_ns.sh, 2-pass)            → ns.elf (guard 주소 확정)
#   ② Secure 분석 (clang → IR → opt, 분석만)   → secure_guard_candidates.json
#   ③ 배치 (pack_guards.py)                    → secure_guard_mem.ld / _sec.ld / guard_bind.h
#   ④ Secure 최종 빌드 (build_secure.sh, GCC)  → 생성 ld 를 INCLUDE, --in-implib 로 veneer 고정
#   ⑤ veneer 비교 (⓪과 ④의 implib 심볼이 같아야 NS 를 다시 링크하지 않아도 됨)
set -euo pipefail
TOOLS=$(cd "$(dirname "$0")/.." && pwd)
source "$TOOLS/scripts/secure_common.sh"
[ -n "${LLVM:-}" ] || source "$TOOLS/scripts/env_llvm18.sh"
NS_ELF=${NS_ELF:-$TOOLS/build/ns/ns.elf}
PLUGIN=${PLUGIN:-$TOOLS/build/pass/SecureGuardSelect.so}
SYSROOT=$(realpath "$("$GCC" -print-sysroot)")
S_FLASH=${S_FLASH:-ROM}                                  # Secure 링커 스크립트의 Flash MEMORY 이름

# 분석은 GCC 빌드(-O0)와 같은 소스·매크로로. -O1 은 IR 을 읽기 쉽게 할 뿐 판정에 영향 없음 (부팅 경로는 호출 그래프).
CFLAGS="--target=arm-none-eabi $S_ARCH -mcmse -O1 -g -std=gnu11 -fno-common -ffunction-sections -fdata-sections \
 --sysroot=$SYSROOT -Wno-unknown-attributes $S_DEFS $S_INCS"

cd "$TOOLS"
rm -rf "$S_OUT/ir"; mkdir -p "$S_OUT/ir" "$S_OUT/gen"
[ -f "$S_OUT/gen/guard_bind.h" ] || printf '#define GUARD_BIND_N 0u\nstatic const unsigned k_guard_bind_ns[1]={0};\n' > "$S_OUT/gen/guard_bind.h"

echo "== ② Secure 소스 → LLVM IR (분석 전용, 실제 바이너리는 GCC 가 만든다)"
BCS=()
for r in "${S_SRCS_REL[@]}"; do
  b="$S_OUT/ir/$(echo "$r" | sed 's#[/.]#_#g').bc"
  "$LLVM/clang" $CFLAGS -emit-llvm -c "$(ov "$r")" -o "$b"
  BCS+=("$b")
done
for r in "${S_ADD_C[@]}"; do
  b="$S_OUT/ir/$(basename "$r" .c).bc"
  "$LLVM/clang" $CFLAGS -emit-llvm -c "$TOOLS/$r" -o "$b"
  BCS+=("$b")
done
"$LLVM/llvm-link" "${BCS[@]}" -o "$S_OUT/secure.bc"        # 파일들을 하나의 IR 모듈로

export SECURE_GUARD_CONFIG=${SECURE_GUARD_CONFIG:-$TOOLS/config/secure_guard_config.json}
export SECURE_GUARD_OUT=${SECURE_GUARD_OUT:-$S_OUT/secure_guard_candidates.json}
CAND=$SECURE_GUARD_OUT
mkdir -p "$(dirname "$CAND")"; rm -f "$CAND"
"$LLVM/opt" -load-pass-plugin="$PLUGIN" -passes=secure-guard-select -disable-output "$S_OUT/secure.bc"
[ -f "$CAND" ] || { echo "pass 가 $CAND 를 쓰지 않음"; exit 1; }

echo "== ③ guard 블록에 나눠 담기"
python3 "$TOOLS/gen/pack_guards.py" --candidates "$CAND" --ns-elf "$NS_ELF" --out-dir "$S_OUT/gen" \
        --flash "$S_FLASH" ${PACK_ARGS:-}
