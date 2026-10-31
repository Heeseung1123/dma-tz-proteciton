#!/usr/bin/env bash
# 터미널 빌드. Secure 를 먼저 빌드해야 NonSecure 가 Secure_nsclib 의 import library 로 링크된다.
#   ./build.sh          빌드
#   ./build.sh clean    정리
#   TOOLCHAIN_BIN=/path/to/bin ./build.sh   툴체인 지정
set -euo pipefail
cd "$(dirname "$0")"
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin}"
[ -n "$TOOLCHAIN_BIN" ] && export PATH="$TOOLCHAIN_BIN:$PATH"
command -v arm-none-eabi-gcc >/dev/null || { echo "arm-none-eabi-gcc 없음: TOOLCHAIN_BIN=/path/to/bin ./build.sh"; exit 1; }

# ST GCC 가 아니면(-fcyclomatic-complexity 미지원) 그 옵션만 걸러내는 래퍼를 앞에 둔다
if ! echo 'int x;' | arm-none-eabi-gcc -fcyclomatic-complexity -x c -c - -o /dev/null 2>/dev/null; then
  SHIM="$PWD/.build-shim"; mkdir -p "$SHIM"
  for t in gcc g++; do
    REAL="$(command -v arm-none-eabi-$t || true)"; [ -z "$REAL" ] && continue
    cat > "$SHIM/arm-none-eabi-$t" <<EOF
#!/usr/bin/env bash
args=(); for a in "\$@"; do [ "\$a" = "-fcyclomatic-complexity" ] || args+=("\$a"); done
exec "$REAL" "\${args[@]}"
EOF
    chmod +x "$SHIM/arm-none-eabi-$t"
  done
  export PATH="$SHIM:$PATH"
  echo "(ST GCC 가 아님: -fcyclomatic-complexity 를 걸러서 빌드)"
fi

TARGET="${1:-all}"
for d in "Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/STM32CubeIDE/Secure/Debug" "Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/STM32CubeIDE/NonSecure/Debug"; do
  echo "=== $d ($TARGET)"
  make -C "$d" -j"$(nproc)" "$TARGET"
done
