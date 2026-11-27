#!/usr/bin/env bash
# build_ns.sh — NS 이미지를 clang + ld.lld(Full LTO) + DmaCluster pass 로 2-pass 빌드
#
#   1) 전체 컴파일 (-flto, bitcode .o)            — dma_regions.h 없이, stub manifest
#   2) 1차 링크: pass 가 section 부여 + dma_layout.json 출력
#   3) gen_layout.py: dma_clusters.ld / dma_manifest.c / dma_regions.h 생성
#   4) dma_regions.h 를 쓰는 파일만 재컴파일 + 생성된 manifest 컴파일
#   5) 2차 링크: 확정 배치 → ns.elf
#   6) 결정성 검사(1차/2차 JSON 동일) + check_layout.py
#
# 환경에 맞게 아래 변수만 고치면 된다 (경로는 레포 구조 예시).
set -euo pipefail

TOOLS=$(cd "$(dirname "$0")/.." && pwd)                 # llvm_dma_cluster/
ROOT=${ROOT:-$HOME/dma-tz}                              # 프로젝트 레포
NSDIR=${NSDIR:-$ROOT/project/NonSecure}                 # NS 프로젝트 (CubeIDE)
OUT=${OUT:-$ROOT/build/ns}
LLVM=${LLVM:-/usr/lib/llvm-18/bin}
PLUGIN=${PLUGIN:-$TOOLS/build/pass/DmaCluster.so}
LDS=${LDS:-$NSDIR/STM32L562QEIXQ_FLASH.ld}              # INCLUDE dma_clusters.ld 를 넣은 NS 스크립트
IMPLIB=${IMPLIB:-$ROOT/project/Secure_nsclib/secure_nsclib.o}   # Secure(GCC) 빌드 산출물
REGION_USERS=${REGION_USERS:-"$NSDIR/Core/Src/app_freertos.c"}  # dma_regions.h 를 include 하는 파일

ARCH="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16"
GCC=arm-none-eabi-gcc                                   # newlib / libgcc 만 빌려 쓴다
GFLAGS="-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16"
GCC_ROOT=$(dirname "$(dirname "$(command -v $GCC)")")
MULTI=$($GCC $GFLAGS -print-multi-directory)            # 예: thumb/v8-m.main+fp/hard
LIBC_DIR=$GCC_ROOT/arm-none-eabi/lib/$MULTI
LIBGCC=$($GCC $GFLAGS -print-libgcc-file-name)
CRTI=$($GCC $GFLAGS -print-file-name=crti.o)
CRTN=$($GCC $GFLAGS -print-file-name=crtn.o)

# CubeIDE 의 Debug/**/subdir.mk 에 있는 -D / -I 를 그대로 옮긴다
DEFS="-DUSE_HAL_DRIVER -DSTM32L562xx -DDEBUG"
INCS="-I$NSDIR/Core/Inc -I$NSDIR/../Secure_nsclib -I$TOOLS/common \
 -I$ROOT/Drivers/STM32L5xx_HAL_Driver/Inc -I$ROOT/Drivers/CMSIS/Device/ST/STM32L5xx/Include \
 -I$ROOT/Drivers/CMSIS/Include \
 -I$ROOT/Middlewares/Third_Party/FreeRTOS/Source/include \
 -I$ROOT/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33/non_secure"
CFLAGS="$ARCH -O2 -g -std=gnu11 -ffunction-sections -fdata-sections -fno-common -flto \
 --sysroot=$GCC_ROOT/arm-none-eabi -Wno-unknown-attributes $DEFS $INCS"

SRCS=( $(find "$NSDIR/Core/Src" "$ROOT/Drivers/STM32L5xx_HAL_Driver/Src" \
              "$ROOT/Middlewares/Third_Party/FreeRTOS/Source" -name '*.c' \
              ! -path '*/portable/*' ! -name '*_template.c') \
       $ROOT/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33/non_secure/*.c \
       $ROOT/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c \
       "$TOOLS/ns/dma_zero.c" )
ASMS=( "$NSDIR/Core/Startup/startup_stm32l562qeixq.s" )

cd "$ROOT"                     # pass 는 설정의 "out" 경로를 lld 의 현재 디렉터리 기준으로 쓴다
mkdir -p "$OUT/obj" "$OUT/gen"
export DMA_CLUSTER_CONFIG=${DMA_CLUSTER_CONFIG:-$TOOLS/config/dma_config.json}
export DMA_CLUSTER_STRICT=1
LAYOUT=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['out'])" "$DMA_CLUSTER_CONFIG")
case "$LAYOUT" in /*) ;; *) LAYOUT="$ROOT/$LAYOUT";; esac
mkdir -p "$(dirname "$LAYOUT")"

obj() { echo "$OUT/obj/$(echo "$1" | sed "s#^$ROOT/##; s#[/.]#_#g").o"; }

compile_all() {
  OBJS=()
  for s in "${SRCS[@]}"; do o=$(obj "$s"); "$LLVM/clang" $CFLAGS "$@" -c "$s" -o "$o"; OBJS+=("$o"); done
  for s in "${ASMS[@]}"; do o=$(obj "$s"); "$LLVM/clang" $ARCH -x assembler-with-cpp -c "$s" -o "$o"; OBJS+=("$o"); done
}

link() {   # $1 = 출력 elf, $2 = manifest 오브젝트
  "$LLVM/clang" $ARCH -fuse-ld=lld -flto -nostdlib -nostartfiles \
    -Wl,--load-pass-plugin="$PLUGIN" \
    -Wl,-mllvm,-arm-global-merge=false -Wl,--lto-O2 \
    -Wl,-L,"$OUT/gen" -Wl,-T,"$LDS" -Wl,-Map="$1.map" -Wl,--gc-sections \
    "$CRTI" "${OBJS[@]}" "$2" "$IMPLIB" \
    "$LIBC_DIR/libc_nano.a" "$LIBC_DIR/libnosys.a" "$LIBC_DIR/libm.a" "$LIBGCC" "$CRTN" \
    -o "$1"
}

echo "== 1) 컴파일 (pass 1)"
cp "$TOOLS/ns/dma_clusters_pass1.ld" "$OUT/gen/dma_clusters.ld"
compile_all
"$LLVM/clang" $CFLAGS -c "$TOOLS/ns/dma_manifest_stub.c" -o "$OUT/obj/manifest_stub.o"

echo "== 2) 1차 링크"
link "$OUT/ns_pass1.elf" "$OUT/obj/manifest_stub.o"
cp "$LAYOUT" "$OUT/dma_layout.pass1.json"

echo "== 3) 생성기"
python3 "$TOOLS/gen/gen_layout.py" --layout "$OUT/dma_layout.pass1.json" --out-dir "$OUT/gen" \
        --ram RAM --flash FLASH --max-tags 4 --max-task-regions 4 --guard-blocks 1

echo "== 4) dma_regions.h 사용 파일 재컴파일 + manifest"
for s in $REGION_USERS; do
  "$LLVM/clang" $CFLAGS -DDMA_HAVE_REGIONS -I"$OUT/gen" -c "$s" -o "$(obj "$s")"
done
"$LLVM/clang" $CFLAGS -c "$OUT/gen/dma_manifest.c" -o "$OUT/obj/dma_manifest.o"

echo "== 5) 2차 링크"
link "$OUT/ns.elf" "$OUT/obj/dma_manifest.o"

echo "== 6) 검증"
diff <(python3 -m json.tool --sort-keys "$OUT/dma_layout.pass1.json") \
     <(python3 -m json.tool --sort-keys "$LAYOUT") \
  || { echo "1차/2차 분석 결과가 다름 (비결정적 입력?)"; exit 1; }
python3 "$TOOLS/scripts/check_layout.py" "$OUT/ns.elf" "$OUT/gen/layout_final.json"
"$LLVM/llvm-objcopy" -O ihex "$OUT/ns.elf" "$OUT/ns.hex"
echo "완료: $OUT/ns.elf"
