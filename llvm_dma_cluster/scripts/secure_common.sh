# secure_common.sh — Secure 이미지의 소스 목록 / -D / -I (source 해서 쓴다)
# CubeIDE STM32CubeIDE/Secure/Debug/**/subdir.mk 와 같은 목록. ② clang 분석과 ④ GCC 최종 빌드가
# "같은 소스" 를 보게 하려고 한 곳에 둔다. ~/dma-tz 의 원본 대신 llvm_dma_cluster/src/ 사본이 있으면 그것을 쓴다.
TOOLS=${TOOLS:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
ROOT=${ROOT:-$(cd "$TOOLS/.." && pwd)}
PRJ_REL=Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone
S_OUT=${S_OUT:-$TOOLS/build/secure}
S0_IMPLIB=${S0_IMPLIB:-$TOOLS/build/secure0/secure_nsclib.o}   # ⓪ 의 import library (veneer 주소 기준)

ST_GCC_BIN=/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin
GCC=${GCC:-$( [ -x "$ST_GCC_BIN/arm-none-eabi-gcc" ] && echo "$ST_GCC_BIN/arm-none-eabi-gcc" || command -v arm-none-eabi-gcc )}
GCC_BIN=$(dirname "$GCC")

ov() {   # ROOT 기준 상대 경로 → 사본이 있으면 사본
  if [ -f "$TOOLS/src/$1" ]; then echo "$TOOLS/src/$1"; else echo "$ROOT/$1"; fi
}

_P=$PRJ_REL; _H=Drivers/STM32L5xx_HAL_Driver/Src; _FS=Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33/secure
S_SRCS_REL=(
  $_P/Secure/Src/main.c $_P/Secure/Src/secure_nsc.c $_P/Secure/Src/stm32l5xx_hal_msp.c
  $_P/Secure/Src/stm32l5xx_it.c $_P/Secure/Src/system_stm32l5xx_s.c
  $_P/STM32CubeIDE/Secure/Application/User/syscalls.c $_P/STM32CubeIDE/Secure/Application/User/sysmem.c
  Drivers/BSP/STM32L562E-DK/stm32l562e_discovery.c
  $_H/stm32l5xx_hal.c $_H/stm32l5xx_hal_cortex.c $_H/stm32l5xx_hal_dma.c $_H/stm32l5xx_hal_dma_ex.c
  $_H/stm32l5xx_hal_exti.c $_H/stm32l5xx_hal_flash.c $_H/stm32l5xx_hal_flash_ex.c
  $_H/stm32l5xx_hal_flash_ramfunc.c $_H/stm32l5xx_hal_gpio.c $_H/stm32l5xx_hal_gtzc.c
  $_H/stm32l5xx_hal_i2c.c $_H/stm32l5xx_hal_i2c_ex.c $_H/stm32l5xx_hal_icache.c
  $_H/stm32l5xx_hal_pwr.c $_H/stm32l5xx_hal_pwr_ex.c $_H/stm32l5xx_hal_rcc.c $_H/stm32l5xx_hal_rcc_ex.c
  $_H/stm32l5xx_hal_tim.c $_H/stm32l5xx_hal_tim_ex.c $_H/stm32l5xx_hal_uart.c $_H/stm32l5xx_hal_uart_ex.c
  $_FS/secure_context.c $_FS/secure_context_port.c $_FS/secure_heap.c $_FS/secure_init.c
)
# llvm_dma_cluster 가 추가하는 Secure 파일 (TOOLS 기준)
S_ADD_C=( secure/secure_dma_boot.c secure/secure_guard_bind.c )
S_ADD_ASM=( secure/secure_dma_entry.S )
S_STARTUP_REL=$_P/STM32CubeIDE/Secure/Application/Startup/startup_stm32l562qeixq.s
S_LDS=$(ov "$_P/STM32CubeIDE/Secure/STM32L562QEIXQ_FLASH.ld")

# DMA_MEDIATOR_PAD: secure_nsc.c 의 C SECURE_dma_m2m_start 를 빼고 secure_dma_entry.S 로 (s_busy extern)
# SECSTACK_STATIC=1: secure_context*.c landing pad 패치 (secure_context_patch.md)
S_DEFS="-DUSE_HAL_DRIVER -DSTM32L562xx -DDEBUG -DDMA_MEDIATOR_PAD -DSECSTACK_STATIC=1 ${S_EXTRA_DEFS:-}"
S_INCS="-I$ROOT/Drivers/BSP/STM32L562E-DK -I$ROOT/$_P/Secure/Inc -I$ROOT/$_P/Secure_nsclib \
 -I$ROOT/Drivers/STM32L5xx_HAL_Driver/Inc -I$ROOT/Drivers/CMSIS/Device/ST/STM32L5xx/Include \
 -I$ROOT/Drivers/STM32L5xx_HAL_Driver/Inc/Legacy -I$ROOT/$_FS \
 -I$ROOT/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33/non_secure \
 -I$ROOT/Drivers/CMSIS/Include -I$TOOLS/common -I$S_OUT/gen"
S_ARCH="-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16"
unset _P _H _FS
