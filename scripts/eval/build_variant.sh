#!/usr/bin/env bash
# 평가용 변형 빌드. makefile 을 고치지 않고 gcc 래퍼로 -O 치환 + -D 추가.
#   build_variant.sh <phase> <variant> <P0|P1|P2> [BENCH_매크로[=값] ...]
# 산출물: results/<phase>/<variant>/{*.elf,*.map,manifest.txt,flags.txt}
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"
PHASE="$1"; VARIANT="$2"; PROFILE="$3"; shift 3
case "$PROFILE" in
  P0) OS=-O0; ONS=-O0;;
  P1) OS=-O0; ONS=-O1;;
  P2) OS=-O1; ONS=-O1;;
  *) echo "unknown profile $PROFILE"; exit 2;;
esac
DEFS=""; for d in "$@"; do DEFS="$DEFS -D$d"; done
STBIN="/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin"
SHIM="$ROOT/.build-shim/eval"; mkdir -p "$SHIM"
cat > "$SHIM/arm-none-eabi-gcc" <<W
#!/usr/bin/env bash
sec=0; comp=0
for a in "\$@"; do [ "\$a" = "-mcmse" ] && sec=1; [ "\$a" = "-c" ] && comp=1; done
if [ \$sec = 1 ]; then O=\${EVAL_O_S}; else O=\${EVAL_O_NS}; fi
args=()
for a in "\$@"; do case "\$a" in -O0|-O1|-O2|-O3|-Os|-Og) args+=("\$O");; *) args+=("\$a");; esac; done
[ \$comp = 1 ] && args+=(\${EVAL_DEFS})
exec "$STBIN/arm-none-eabi-gcc" "\${args[@]}"
W
chmod +x "$SHIM/arm-none-eabi-gcc"
export EVAL_O_S="$OS" EVAL_O_NS="$ONS" EVAL_DEFS="$DEFS"
export PATH="$STBIN:$PATH" TOOLCHAIN_BIN="$SHIM"
./build.sh clean >/dev/null
OUT="results/$PHASE/$VARIANT"; mkdir -p "$OUT"
./build.sh > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
P=Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/STM32CubeIDE
cp $P/Secure/Debug/*_Secure.elf $P/Secure/Debug/*_Secure.map $P/Secure/Debug/*_Secure.list "$OUT/"
cp $P/NonSecure/Debug/*_NonSecure.elf $P/NonSecure/Debug/*_NonSecure.map $P/NonSecure/Debug/*_NonSecure.list "$OUT/"
{
  echo "variant=$VARIANT profile=$PROFILE S=$OS NS=$ONS defs=[$DEFS]"
  echo "git=$(git rev-parse HEAD) dirty=$(git status --porcelain -- '*.c' '*.h' | tr '\n' ';')"
  "$STBIN/arm-none-eabi-gcc" --version | head -1
  (cd "$OUT" && sha256sum *.elf)
  "$STBIN/arm-none-eabi-size" "$OUT"/*.elf
} > "$OUT/manifest.txt"
scripts/eval/check_layout.sh "$OUT"/*_NonSecure.elf
