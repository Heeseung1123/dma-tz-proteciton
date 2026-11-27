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
# 소스 목록과 -D/-I 는 CubeIDE 의 STM32CubeIDE/NonSecure/Debug/**/subdir.mk 와 같다.
# ~/dma-tz 의 파일은 수정하지 않는다: llvm_dma_cluster/src/<같은 상대 경로> 에 사본이 있으면 그것을 쓴다.
# 산출물은 모두 llvm_dma_cluster/build/ns 아래.
set -euo pipefail

TOOLS=$(cd "$(dirname "$0")/.." && pwd)                 # llvm_dma_cluster/
ROOT=${ROOT:-$(cd "$TOOLS/.." && pwd)}                  # 프로젝트 레포 (~/dma-tz)
PRJ_REL=Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone
OUT=${OUT:-$TOOLS/build/ns}
# shellcheck source=env_llvm18.sh
[ -n "${LLVM:-}" ] || source "$TOOLS/scripts/env_llvm18.sh"
PLUGIN=${PLUGIN:-$TOOLS/build/pass/DmaCluster.so}
IMPLIB=${IMPLIB:-$TOOLS/build/secure0/secure_nsclib.o} # ⓪ Secure 0차 빌드의 import library
EXTRA_DEFS=${EXTRA_DEFS:-"-DDMA_ATTACK_TESTS"}

# GCC 는 newlib / libgcc / crt 만 빌려 쓴다. build.sh 와 같은 ST 툴체인을 우선 사용.
ST_GCC_BIN=/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin
GCC=${GCC:-$( [ -x "$ST_GCC_BIN/arm-none-eabi-gcc" ] && echo "$ST_GCC_BIN/arm-none-eabi-gcc" || command -v arm-none-eabi-gcc )}
GFLAGS="-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16"
SYSROOT=$("$GCC" -print-sysroot)
[ -d "$SYSROOT/include" ] || SYSROOT=/usr/lib/arm-none-eabi        # Debian gcc-arm-none-eabi
SYSROOT=$(realpath "$SYSROOT")
LIBC_DIR=$(dirname "$("$GCC" $GFLAGS -print-file-name=libc_nano.a)")
LIBGCC=$("$GCC" $GFLAGS -print-libgcc-file-name)
CRTI=$("$GCC" $GFLAGS -print-file-name=crti.o)
CRTN=$("$GCC" $GFLAGS -print-file-name=crtn.o)

ov() {   # $1 = ROOT 기준 상대 경로 → 사본이 있으면 사본, 없으면 원본 (절대 경로)
  if [ -f "$TOOLS/src/$1" ]; then echo "$TOOLS/src/$1"; else echo "$ROOT/$1"; fi
}

P=$PRJ_REL; F=Middlewares/Third_Party/FreeRTOS/Source; H=Drivers/STM32L5xx_HAL_Driver/Src
NS_LDS=$(ov "$P/STM32CubeIDE/NonSecure/STM32L562QEIXQ_FLASH.ld")
APP=$P/NonSecure/Src/app_freertos.c
NS_RAM=${NS_RAM:-RAM}; NS_FLASH=${NS_FLASH:-ROM}            # NS 링커 스크립트 MEMORY 영역 이름
REGION_USERS=${REGION_USERS:-"$APP"}                    # dma_regions.h 를 include 하는 파일 (상대 경로)

SRCS_REL=(
  "$APP" $P/NonSecure/Src/main.c $P/NonSecure/Src/stm32l5xx_hal_msp.c
  $P/NonSecure/Src/stm32l5xx_hal_timebase_tim.c $P/NonSecure/Src/stm32l5xx_it.c
  $P/NonSecure/Src/system_stm32l5xx_ns.c
  $P/STM32CubeIDE/NonSecure/Application/User/syscalls.c $P/STM32CubeIDE/NonSecure/Application/User/sysmem.c
  Drivers/BSP/STM32L562E-DK/stm32l562e_discovery.c
  $H/stm32l5xx_hal.c $H/stm32l5xx_hal_cortex.c $H/stm32l5xx_hal_dma.c $H/stm32l5xx_hal_dma_ex.c
  $H/stm32l5xx_hal_exti.c $H/stm32l5xx_hal_flash.c $H/stm32l5xx_hal_flash_ex.c
  $H/stm32l5xx_hal_flash_ramfunc.c $H/stm32l5xx_hal_gpio.c $H/stm32l5xx_hal_i2c.c
  $H/stm32l5xx_hal_i2c_ex.c $H/stm32l5xx_hal_icache.c $H/stm32l5xx_hal_pwr.c $H/stm32l5xx_hal_pwr_ex.c
  $H/stm32l5xx_hal_rcc.c $H/stm32l5xx_hal_rcc_ex.c $H/stm32l5xx_hal_tim.c $H/stm32l5xx_hal_tim_ex.c
  $H/stm32l5xx_hal_uart.c $H/stm32l5xx_hal_uart_ex.c
  $F/CMSIS_RTOS_V2/cmsis_os2.c $F/croutine.c $F/event_groups.c $F/list.c
  $F/portable/Common/mpu_wrappers_v2.c $F/portable/GCC/ARM_CM33/non_secure/mpu_wrappers_v2_asm.c
  $F/portable/GCC/ARM_CM33/non_secure/portasm.c $F/portable/GCC/ARM_CM33/non_secure/port.c
  $F/portable/MemMang/heap_4.c $F/queue.c $F/stream_buffer.c $F/tasks.c $F/timers.c
)
# inline asm 에서만 참조하는 심볼(MPU_*Impl, vSystemCallExit, pxCurrentTCB …)은 LTO 가 볼 수 없어
# 지워지거나 내부화된다. 이 두 파일은 LTO 밖의 일반 오브젝트로 컴파일 → lld 가 참조를 LTO 에 알려 준다.
# (DMA 버퍼/sink 호출이 없는 커널 asm 파일이라 pass 분석 범위에는 영향 없음)
NOLTO_REL=( $F/portable/GCC/ARM_CM33/non_secure/portasm.c $F/portable/GCC/ARM_CM33/non_secure/mpu_wrappers_v2_asm.c )
ASMS_REL=( $P/STM32CubeIDE/NonSecure/Application/Startup/startup_stm32l562qeixq.s )

ARCH="--target=arm-none-eabi $GFLAGS"
DEFS="-DUSE_HAL_DRIVER -DSTM32L562xx -DDEBUG $EXTRA_DEFS"
INCS="-I$ROOT/$P/NonSecure/Inc -I$ROOT/Drivers/BSP/STM32L562E-DK -I$ROOT/$P/Secure_nsclib \
 -I$ROOT/Drivers/STM32L5xx_HAL_Driver/Inc -I$ROOT/Drivers/CMSIS/Device/ST/STM32L5xx/Include \
 -I$ROOT/Drivers/STM32L5xx_HAL_Driver/Inc/Legacy -I$ROOT/$F/include -I$ROOT/$F/CMSIS_RTOS_V2 \
 -I$ROOT/$F/portable/GCC/ARM_CM33/non_secure -I$ROOT/$F/portable/GCC/ARM_CM33/secure \
 -I$ROOT/Drivers/CMSIS/Include -I$TOOLS/common"
CFLAGS="$ARCH -O2 -g -std=gnu11 -ffunction-sections -fdata-sections -fno-common -flto \
 --sysroot=$SYSROOT $DEFS $INCS"

[ -f "$PLUGIN" ] || { echo "플러그인 없음: $PLUGIN (cmake --build build/pass)"; exit 1; }
[ -f "$IMPLIB" ] || { echo "import library 없음: $IMPLIB (scripts/build_all.sh 가 ⓪ 에서 만든다)"; exit 1; }

cd "$TOOLS"                    # pass 는 설정의 "out" 경로를 lld 의 현재 디렉터리 기준으로 쓴다
rm -rf "$OUT/gen" "$OUT/obj"; mkdir -p "$OUT/obj" "$OUT/gen"
export DMA_CLUSTER_CONFIG=${DMA_CLUSTER_CONFIG:-$TOOLS/config/dma_config.json}
export DMA_CLUSTER_STRICT=1
LAYOUT=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['out'])" "$DMA_CLUSTER_CONFIG")
case "$LAYOUT" in /*) ;; *) LAYOUT="$TOOLS/$LAYOUT";; esac
mkdir -p "$(dirname "$LAYOUT")"
rm -f "$LAYOUT"

obj() { echo "$OUT/obj/$(echo "$1" | sed 's#[/.]#_#g').o"; }

compile_all() {
  OBJS=()
  for r in "${SRCS_REL[@]}"; do
    o=$(obj "$r"); f=$CFLAGS
    for n in "${NOLTO_REL[@]}"; do [ "$r" = "$n" ] && f=${CFLAGS/ -flto / }; done
    "$LLVM/clang" $f -c "$(ov "$r")" -o "$o"; OBJS+=("$o")
  done
  for r in "${ASMS_REL[@]}"; do o=$(obj "$r"); "$LLVM/clang" $ARCH -x assembler-with-cpp -c "$(ov "$r")" -o "$o"; OBJS+=("$o"); done
  o="$OUT/obj/dma_zero.o"; "$LLVM/clang" $CFLAGS -c "$TOOLS/ns/dma_zero.c" -o "$o"; OBJS+=("$o")
}

link() {   # $1 = 출력 elf, $2 = manifest 오브젝트
  "$LLVM/clang" $ARCH -fuse-ld=lld -flto -nostdlib -nostartfiles \
    -Wl,--load-pass-plugin="$PLUGIN" \
    -Wl,-mllvm,-arm-global-merge=false -Wl,--lto-O2 \
    -Wl,-L,"$OUT/gen" -Wl,-T,"$NS_LDS" -Wl,-Map="$1.map" -Wl,--gc-sections \
    "$CRTI" "${OBJS[@]}" "$2" "$IMPLIB" \
    -Wl,--start-group "$LIBC_DIR/libc_nano.a" "$LIBC_DIR/libnosys.a" "$LIBC_DIR/libm.a" "$LIBGCC" -Wl,--end-group \
    "$CRTN" -o "$1"
}

echo "== 1) 컴파일 (pass 1)"
sed "s/@RAM@/$NS_RAM/g; s/@FLASH@/$NS_FLASH/g" "$TOOLS/ns/dma_clusters_pass1.ld" > "$OUT/gen/dma_clusters.ld"
compile_all
"$LLVM/clang" $CFLAGS -c "$TOOLS/ns/dma_manifest_stub.c" -o "$OUT/obj/manifest_stub.o"

echo "== 2) 1차 링크"
link "$OUT/ns_pass1.elf" "$OUT/obj/manifest_stub.o"
[ -f "$LAYOUT" ] || { echo "pass 가 $LAYOUT 을 쓰지 않음 (플러그인 미실행?)"; exit 1; }
cp "$LAYOUT" "$OUT/dma_layout.pass1.json"

echo "== 3) 생성기"
python3 "$TOOLS/gen/gen_layout.py" --layout "$OUT/dma_layout.pass1.json" --out-dir "$OUT/gen" \
        --ram "$NS_RAM" --flash "$NS_FLASH" --max-tags 4 --max-task-regions 2 --guard-blocks 1

echo "== 4) dma_regions.h 사용 파일 재컴파일 + manifest"
for r in $REGION_USERS; do
  "$LLVM/clang" $CFLAGS -DDMA_HAVE_REGIONS -I"$OUT/gen" -c "$(ov "$r")" -o "$(obj "$r")"
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
