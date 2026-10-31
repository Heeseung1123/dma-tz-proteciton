/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Secure/Src/secure_nsc.c
  * @author  MCD Application Team
  * @brief   This file contains the non-secure callable APIs (secure world)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* USER CODE BEGIN Non_Secure_CallLib */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "secure_nsc.h"
#include <stdio.h>
#include "secure_port_macros.h"  
/** @addtogroup STM32L5xx_HAL_Examples
  * @{
  */

/** @addtogroup FreeRTOS_SecureIOToggle_TrustZone
  * @{
  */

/* Global variables ----------------------------------------------------------*/
void *pSecureFaultCallback = NULL;   /* Pointer to secure fault callback in Non-secure */
void *pSecureErrorCallback = NULL;   /* Pointer to secure error callback in Non-secure */




/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Secure registration of non-secure callback.
  * @param  CallbackId  callback identifier
  * @param  func        pointer to non-secure function
  * @retval None
  */
CMSE_NS_ENTRY void SECURE_RegisterCallback(SECURE_CallbackIDTypeDef CallbackId, void *func)
{
  if(func != NULL)
  {
    switch(CallbackId)
    {
      case SECURE_FAULT_CB_ID:           /* SecureFault Interrupt occurred */
        pSecureFaultCallback = func;
        break;
      case GTZC_ERROR_CB_ID:             /* GTZC Interrupt occurred */
        pSecureErrorCallback = func;
        break;
      default:
        /* unknown */
        break;
    }
  }
}

/**
  * @brief  Secure treatment of non-secure push button interrupt.
  * @retval None
  */
/*CMSE_NS_ENTRY*/secureportNON_SECURE_CALLABLE void SECURE_LEDToggle(void)
{
  BSP_LED_Toggle(LED10);
}

/**
  * @}
  */

/**
  * @}
  */
/* USER CODE END Non_Secure_CallLib */
	

#if 0 
static void mpcbb_mark_block_secure(uint32_t block_idx)
{
    DMA_ARENA_VCTR[block_idx / MPCBB_BLOCKS_PER_SB] |=
        1UL << (block_idx % MPCBB_BLOCKS_PER_SB);
}

CMSE_NS_ENTRY uint32_t SECURE_dma_buffer_request(uint32_t len)
{
    if (len == 0) return 0;

    uint32_t usable_len = (len + MPCBB_BLOCK_SIZE - 1) & ~(MPCBB_BLOCK_SIZE - 1);
    uint32_t total_len  = usable_len + 2 * MPCBB_BLOCK_SIZE;

    if (s_arena_next + total_len > DMA_ARENA_BASE + DMA_ARENA_SIZE)
        return 0;

    uint32_t usable_base = s_arena_next + MPCBB_BLOCK_SIZE;

    uint32_t first_block = (usable_base - DMA_ARENA_SRAM_BASE) / MPCBB_BLOCK_SIZE;
    uint32_t last_block  = first_block + usable_len / MPCBB_BLOCK_SIZE - 1;

    /* lower guard: usable 영역 바로 앞 block */
    mpcbb_mark_block_secure(first_block - 1);
    /* upper guard: usable 영역 바로 뒤 block */
    mpcbb_mark_block_secure(last_block + 1);

    s_arena_next += total_len;
    return usable_base;
}

#endif 

#include "stm32l5xx.h"
#include <arm_cmse.h>

#define DMA_CH        DMA1_Channel1
#define DMA_CH_IRQn   DMA1_Channel1_IRQn

//typedef void (*ns_callback_t)(uint32_t status);
typedef void (*ns_callback_t)(uint32_t status) __attribute__((cmse_nonsecure_call));
static ns_callback_t s_ns_callback;
static volatile uint8_t s_busy;



secureportNON_SECURE_CALLABLE int SECURE_dma_m2m_init(void *ns_cb)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    NVIC_ClearTargetState(DMA_CH_IRQn);
    NVIC_SetPriority(DMA_CH_IRQn, 5);
    NVIC_EnableIRQ(DMA_CH_IRQn);

    DMA_CH->CCR |= (DMA_CCR_MEM2MEM
                | DMA_CCR_MINC | DMA_CCR_PINC
                | DMA_CCR_PL_1                 /* priority high */
                | DMA_CCR_TCIE | DMA_CCR_TEIE);

    s_ns_callback = (ns_callback_t)cmse_nsfptr_create(ns_cb);
    s_busy = 0;
    return 0;
}
typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t  valid;
} dma_ent_t;

const dma_ent_t g_dma_table[8] = {
    { 0x20018500u, 0x100u, 1u},
    { 0x20018600u, 0x100u, 1u},
    { 0x20018700u, 0x200u, 1u},
    { 0x20018900u, 0x080u, 1u},
    { 0, 0, 0 }, { 0, 0, 0 },
    { 0, 0, 0 }, { 0, 0, 0 },
};


__attribute__((noinline))
int verify_table(uint32_t addr, uint32_t len)
{
    for (uint32_t i = 0u; i < 8u; i++) {
        const dma_ent_t *e = &g_dma_table[i];
        if (!e->valid) continue;
        uint32_t off = addr - e->base;
        if (off < e->size) {
            return (len <= e->size - off);   
        }
    }
    return 0;
}

#if 0 
__attribute__((naked))
int verify_inst(uint32_t addr)
{
	__asm volatile (
		"TTT	r0, r0		\n"
		"LSLS   r0, r0, #10	\n"
		"LSRS	r0, r0, #31	\n"
		"BX	lr		\n"
	);
}
#endif 



/*
 * NS-MPU 검증(TTA) 추가 근거: ARM "Armv8-M Security Extension User Guide" (107655_100_01_en) 4.4.4절
 *   "TTA (Test Target Alternate domain): Query ... the Non-Secure MPU setting to get access
 *    permissions of a memory location for a Non-Secure access to that location."
 * 즉 NS-MPU 조회는 TTA/TTAT 전용이고 TTT(아래쪽, S-MPU 조회용)는 NS-MPU를 보지 않는다.
 * [아키텍처 도출: Armv8-M Security Extension User Guide 4.4.4, RM0438에는 TT 계열 명령 자체가 없어 근거 없음]
 * CLAUDE.md 지침에 따라 호출자 특권/비특권 구분 없이 TTA 하나만 사용한다(TTAT 불필요).
 *
 * 주의: 아래쪽 TTT 경로의 "UBFX r0, r3, #21, #1"(bit21=nonsecure_readwrite_ok)은 기존 코드를
 * 그대로 둔 것이다. 같은 근거 문서에 따르면 그 필드는 TTA/TTAT 전용이라 일반 TTT 결과에서는
 * 채워지지 않을 가능성이 있다(즉 이 줄이 항상 0을 반환해 기존 경로가 상시 거부로 동작할 수 있음).
 * 이번 변경 범위는 "NS-MPU 검증 단계 추가"로 한정하기로 했으므로 이 줄은 고치지 않았다.
 * docs/gap-analysis.md 참고.
 */
__attribute__((naked, noinline))
int verify_inst(uint32_t addr, uint32_t len)
{
    __asm volatile (
        "SUBS   r2, r1, #1              \n"
        "ADDS   r2, r2, r0              \n"   /* r2 = base+len-1; C = 주소 오버플로 */
        "TTA    r1, r0                  \n"   /* r1 = NS-MPU(base)  [신규] */
        "TTA    r3, r2                  \n"   /* r3 = NS-MPU(end)   [신규] */
        "BCS    1f                      \n"   /* 오버플로면 거부 (ADDS의 C 플래그, TT 계열은 플래그 미변경) */
        "ANDS   r1, r1, r3              \n"   /* [신규] 양 끝점 NS-MPU 결과 AND */
        "UBFX   r1, r1, #21, #1         \n"   /* [신규] nonsecure_readwrite_ok, 둘 다 1이어야 통과 */
        "CBZ    r1, 1f                  \n"   /* [신규] NS-MPU 쓰기 불가 -> 거부 */
        "TTT    r3, r0                  \n"   /* r3 = S-MPU(base) (기존) */
        "TTT    r2, r2                  \n"   /* r2 = S-MPU(end)  (기존) */
        "EORS   r1, r3, r2              \n"   /* 기존: 두 S-MPU 영역 번호 비교 */
        "LSLS	r1, r1, #24             \n"
        "BNE    1f                      \n"
        "UBFX   r0, r3, #21, #1         \n"   /* 기존 그대로 (위 주석 참고) */
        "BX     lr                      \n"
        "1:                             \n"
        "MOVS   r0, #0                  \n"
        "BX     lr                      \n"
    );
}

secureportNON_SECURE_CALLABLE
int SECURE_dma_m2m_start(void *src_buf, void *dst_buf, uint32_t len)

{
    if (len == 0 || s_busy) return -1;
    uint32_t a = (uint32_t)(uintptr_t)dst_buf; 
    if (!verify_inst(a, len)) return -1;

    s_busy = 1;

    DMA_CH->CCR  &= ~DMA_CCR_EN;
    DMA_CH->CPAR  = (uint32_t)src_buf;
    DMA_CH->CM0AR  = (uint32_t)dst_buf;
    DMA_CH->CNDTR = len;             
    DMA1->IFCR    = DMA_IFCR_CGIF1;
    DMA_CH->CCR  |= DMA_CCR_EN;      

    return 0;
}

secureportNON_SECURE_CALLABLE int SECURE_dma_m2m_busy(void) { return s_busy; }

secureportNON_SECURE_CALLABLE
int SECURE_nop3(void *a, void *b, uint32_t c)
{ (void)a; (void)b; (void)c; return 0; }

void DMA1_Channel1_IRQHandler(void)
{
    uint32_t isr = DMA1->ISR;
    uint32_t status;

    if (isr & DMA_ISR_TEIF1) {
        status = 1;   
    } else if (isr & DMA_ISR_TCIF1) {
        status = 0;   
    } else {
        DMA1->IFCR = DMA_IFCR_CGIF1;
        return;   
    }

    DMA_CH->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR   = DMA_IFCR_CGIF1;
    s_busy = 0;

    if (s_ns_callback) s_ns_callback(status);
}

void GTZC_IRQHandler(void)
{
    DMA_CH->CCR &= ~DMA_CCR_EN;
    __DSB();

    GTZC_TZIC_S->FCR3 = 0b10000U;
    __DSB();

}
