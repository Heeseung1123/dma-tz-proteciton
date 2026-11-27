#!/usr/bin/env bash
# build_all.sh — ⓪ → ① → ② → ③ → ④ → ⑤ 전체 (VERIFY_LOG.md 의 1–3단계)
#   OUT_ROOT=build/<이름> 로 변형 빌드를 본 빌드와 분리할 수 있다.
#   EXTRA_DEFS (NS -D), S_EXTRA_DEFS (Secure -D), PACK_ARGS (pack_guards.py 추가 인자) 를 넘길 수 있다.
set -euo pipefail
TOOLS=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${ROOT:-$(cd "$TOOLS/.." && pwd)}
OUT_ROOT=${OUT_ROOT:-$TOOLS/build}
source "$TOOLS/scripts/env_llvm18.sh"
export LLVM LLVM_DIR

mkdir -p "$TOOLS/build"
echo "#### 1) 플러그인 (LLVM 18)"
cmake -S "$TOOLS/pass" -B "$TOOLS/build/pass" -G Ninja -DCMAKE_CXX_COMPILER="$LLVM/clang++" \
      -DLLVM_DIR="$LLVM_DIR" > "$TOOLS/build/pass.cmake.log" 2>&1 || { cat "$TOOLS/build/pass.cmake.log"; exit 1; }
cmake --build "$TOOLS/build/pass"

echo "#### ⓪ Secure 0차 (baseline GCC 빌드의 import library)"
S0="$ROOT/Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/STM32CubeIDE/Secure/Debug/secure_nsclib.o"
[ -f "$S0" ] || { echo "baseline 미빌드: $ROOT/build.sh 먼저"; exit 1; }
mkdir -p "$OUT_ROOT/secure0"; cp "$S0" "$OUT_ROOT/secure0/secure_nsclib.o"
export IMPLIB="$OUT_ROOT/secure0/secure_nsclib.o" S0_IMPLIB="$OUT_ROOT/secure0/secure_nsclib.o"

echo "#### 2) NS 2-pass"
OUT="$OUT_ROOT/ns" bash "$TOOLS/scripts/build_ns.sh"

echo "#### 3) Secure ②③④⑤"
export S_OUT="$OUT_ROOT/secure" NS_ELF="$OUT_ROOT/ns/ns.elf"
bash "$TOOLS/scripts/build_secure_guard.sh"
bash "$TOOLS/scripts/build_secure.sh"
python3 "$TOOLS/scripts/check_pair.py" "$OUT_ROOT/secure/secure.elf" "$OUT_ROOT/ns/ns.elf"
echo "#### 전체 OK: $OUT_ROOT"
