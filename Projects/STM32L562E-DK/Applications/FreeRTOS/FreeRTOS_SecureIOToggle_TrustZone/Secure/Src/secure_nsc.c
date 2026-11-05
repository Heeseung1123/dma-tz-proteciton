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

/* 중재자 사이클 측정 훅 (BENCH_TS 빌드에서만 코드 생성, bench_s.inc 참고) */
#if defined(BENCH_PHASE) && defined(BENCH_TS)
extern volatile uint32_t g_benchT1, g_benchT4;
#define BENCH_T1()  (g_benchT1 = DWT->CYCCNT)
#define BENCH_T4()  (g_benchT4 = DWT->CYCCNT)
#else
#define BENCH_T1()  ((void)0)
#define BENCH_T4()  ((void)0)
#endif

#define DMA_CH        DMA1_Channel1
#define DMA_CH_IRQn   DMA1_Channel1_IRQn

//typedef void (*ns_callback_t)(uint32_t status);
typedef void (*ns_callback_t)(uint32_t status) __attribute__((cmse_nonsecure_call));
static ns_callback_t s_ns_callback;
static volatile uint8_t s_busy;



/*
 * TZIC SRAM1 illegal-access 인터럽트(IER3.SRAM1IE) 지연 활성화 (§3.3).
 *
 * 부팅 시가 아니라 여기서 켜는 이유: 가드 블록(NS s_dmaTaskMem[].guard)은 Secure 부팅 중
 * MPCBB Secure 로 설정된다(main.c SMPU_SetupTaskRegions). 이후 NS Reset_Handler 가 .bss 를
 * 0 으로 채우며 가드에 쓰면, 쓰기는 MPCBB 가 무시하지만 TZIC SR3.SRAM1F 가 서고 IER 이 켜져
 * 있으면 GTZC_IRQHandler 가 워드마다 재진입한다 [실측]. 그래서 부팅 중에는 IER3.SRAM1IE 를
 * 꺼 두고(main.c Secure_TZIC_Init), SECURE_dma_m2m_init() 의 첫 동작으로 여기서 켠다.
 * M2M 모드(CCR.MEM2MEM)·TCIE/TEIE·NS 콜백은 init 에서만 설정되므로 M2M 전송은 IER 이 켜진
 * 뒤에만 가능하다 [아키텍처 도출]. 측정 경로(verify_inst, SECURE_dma_m2m_start, SECURE_nop3)
 * 에는 분기/플래그 검사를 추가하지 않는다.
 *
 * 레지스터 근거:
 *   - TZIC_IER3/SR3/FCR3 오프셋 0x08/0x18/0x28, bit4 = SRAM1: CMSIS stm32l562xx.h
 *     GTZC_TZIC_TypeDef 및 GTZC_CFGR3_SRAM1_Pos(4). RM0438 GTZC 장 TZIC 레지스터 절 번호: 미확인.
 *   - IER 이 꺼져 있어도 SR 플래그는 선다: [실측](부팅 시 오발 관찰) / RM 근거 미확인.
 *   - CMSIS 함정: GTZC_TZIC_FCR3_MPCBB1FC_Msk 는 정의되지 않은 GTZC_CFGR3_MPCBB1_Msk 를
 *     참조한다(stm32l562xx.h). 그래서 FCR3 값은 GTZC_TZIC_FCR3_MPCBB1FC_Pos(= SRAM1_Pos)로 만든다.
 */
uint32_t g_tzic_sr3_at_arm;   /* 감사용: IER 을 켜기 직전의 SR3. 기대 bit4(SRAM1F)=1 */

static void Secure_TZIC_ArmGuardIrqOnce(void)
{
    /* IER 비트 자체가 1회 실행 판정. 재호출 시 위반 흔적(SR3)을 지우지 않는다. */
    if ((GTZC_TZIC_S->IER3 & GTZC_TZIC_IER3_SRAM1IE_Msk) == 0U)
    {
        /* 순서 고정: 기록 -> 지움 -> 켬. 지우기 전에 켜면 즉시 재진입한다. */
        g_tzic_sr3_at_arm = GTZC_TZIC_S->SR3;
        GTZC_TZIC_S->FCR3 = (1UL << GTZC_TZIC_FCR3_MPCBB1FC_Pos);   /* SRAM1F 지움 */
        __DSB();
        NVIC_ClearPendingIRQ(GTZC_IRQn);
        GTZC_TZIC_S->IER3 |= GTZC_TZIC_IER3_SRAM1IE_Msk;
        __DSB();
        __ISB();
    }
}

secureportNON_SECURE_CALLABLE int SECURE_dma_m2m_init(void *ns_cb)
{
    /* 첫 DMA 전에 TZIC SRAM1IE 를 켠다. CCR(MEM2MEM/TCIE/TEIE) 설정보다 먼저 실행되어야 한다. */
    Secure_TZIC_ArmGuardIrqOnce();
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
 * NS-MPU 검증(TTAT) 추가 근거: ARM "Armv8-M Security Extension User Guide" (107655_100_01_en) 4.4.4절
 *   "TTA  (Test Target Alternate domain)           : ... Non-Secure MPU setting ... for a
 *    Non-Secure access ..."
 *   "TTAT (Test Target Alternate domain Unprivileged): ... Non-Secure MPU setting ... for a
 *    Non-Secure UNPRIVILEGED access ..."
 * TTA는 "NS 특권" 접근 기준으로, TTAT는 "NS 비특권" 접근 기준으로 NS-MPU를 조회한다.
 * 호출자(DMA_Task_A/B 등, project/NonSecure/Src/app_freertos.c 의 xTaskCreateRestricted 로
 * 생성된 태스크)는 항상 비특권으로 실행되므로, "권한 구분 없이 하나만 쓴다"는 지침은
 * "매번 호출자 권한을 분기하지 말고 비특권 변형(TTAT)으로 고정한다"는 뜻으로 적용했다
 * (TTA 는 특권 접근 기준이라 실제 호출자와 맞지 않아 제외).
 *
 * 필드 선택: GCC/LLVM 의 cmse_address_info_t 레이아웃(이 레포 툴체인
 * .../lib/gcc/arm-none-eabi/14.3.1/include/arm_cmse.h 에서 직접 확인) 기준으로
 *   bit[0:7]   mpu_region       - 이 TT 변형이 실제로 조회한 MPU(=S-MPU 또는 NS-MPU)의 영역 번호
 *   bit[16]    mpu_region_valid
 *   bit[18:19] read_ok/readwrite_ok   - 그 MPU 기준 권한 (TT/TTT 는 S-MPU, TTA/TTAT 는 NS-MPU)
 *   bit[20:21] nonsecure_read_ok/nonsecure_readwrite_ok, bit[22] secure, bit[23:31] idau_*
 *              - __ARM_FEATURE_CMSE&2 전용, SAU/IDAU 레벨의 별개 개념(어떤 TT 변형을 썼든
 *                상수적으로 존재하는 필드이지 TTA/TTAT 전용 "NS 권한" 필드가 아니다)
 * 즉 mpu_region/readwrite_ok(bit19) 쌍이 "이 변형이 조회한 MPU" 기준의 일반 필드이고,
 * nonsecure_readwrite_ok(bit21)는 SAU 레벨의 별개 필드다. 아래 NS-MPU 블록은 TTAT 결과의
 * bit[0:7](영역 번호)을 기존 S-MPU 블록과 동일한 방식(EORS+LSLS#24+BNE)으로 먼저 비교하고,
 * 그 다음에야 bit19(readwrite_ok)를 확인한다 — 양 끝점이 각각 독립적으로 쓰기 가능하더라도
 * 서로 다른 영역이면(= 연속된 한 영역이 아니면) 거부해야 하기 때문에, 권한 비트만 보는 것으로는
 * 부족하고 기존 S-MPU 체크와 같은 "영역 번호 동일성" 검사가 반드시 필요하다.
 *
 * 참고: 아래쪽 TTT 경로의 "UBFX r0, r3, #21, #1"(bit21=nonsecure_readwrite_ok)은 기존 코드를
 * 그대로 둔 것이고, 사용자가 이미 보드에서 정상 동작을 확인한 바 있다. [추정] dst_buf 는 항상
 * SAU 상 NS 주소이므로, TTA/TTAT 의 "Alternate domain"이 하는 일(SAU 상 Secure 로 보이는 주소를
 * 강제로 NS 취급해 조회)이 애초에 필요 없는 경우다 — 이미 SAU 가 NS 로 판정한 주소라면 A 플래그
 * 없이도 TT/TTT 가 그 판정을 반영해 bit21 에 NS-MPU 기준 권한(TTT 이므로 비특권)을 채울 가능성이
 * 있다. 맞다면 이 줄과 위에서 새로 추가한 TTAT/bit19 체크는 같은 권한을 서로 다른 경로로 중복
 * 확인하는 셈이 되고, 새로 추가한 부분의 실질적 기여는 permission 비트가 아니라 "영역 번호
 * 동일성(EORS/LSLS#24/BNE)" 검사 쪽이다 — 기존 TTT 경로에는 NS-MPU 쪽 영역 비교가 없었다.
 * 이 가설은 Armv8-M 명령 수준 pseudocode 로 재확인한 것은 아니므로 [추정]으로 남긴다.
 * docs/gap-analysis.md, docs/impl-notes-2026-10-31.md 참고.
 */
__attribute__((naked, noinline))
int verify_inst(uint32_t addr, uint32_t len)
{
    __asm volatile (
        "SUBS   r2, r1, #1              \n"
        "ADDS   r2, r2, r0              \n"   /* r2 = base+len-1; C = 주소 오버플로 */
        "BCS    1f                      \n"   /* 오버플로면 거부 (TT 계열은 플래그를 바꾸지 않으므로 먼저 검사해도 무방) */
        "TTAT   r1, r0                  \n"   /* [신규] r1 = NS-MPU(base),  비특권 기준 */
        "TTAT   r3, r2                  \n"   /* [신규] r3 = NS-MPU(end),   비특권 기준 (r2=end 보존됨) */
	"CMP	r1, r3			\n" 
        "BNE    1f                      \n"
        "UBFX   r1, r3, #19, #1         \n"   /* [신규] readwrite_ok (NS-MPU 쓰기 가능?) */
        "CBZ    r1, 1f                  \n"   /* [신규] NS-MPU 쓰기 불가 -> 거부 */
        "TTT    r3, r0                  \n"   /* r3 = S-MPU(base) (기존) */
        "TTT    r2, r2                  \n"   /* r2 = S-MPU(end)  (기존) */
	"CMP	r3, r2			\n"	
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
    BENCH_T1();
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
    BENCH_T4();

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

#ifdef BENCH_PHASE
#include "bench_s.inc"
#endif
