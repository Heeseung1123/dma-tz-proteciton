/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/Secure/Src/main.c
  * @author  MCD Application Team
  * @brief   Main secure program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */


/* Non-secure Vector table to jump to (internal Flash Bank2 here)             */
/* Caution: address must correspond to non-secure internal Flash where is     */
/*          mapped in the non-secure vector table                             */
#define VTOR_TABLE_NS_START_ADDR  0x08040000UL
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void NonSecure_Init(void);
static void MX_ICACHE_Init(void);
static void MX_GTZC_S_Init(void);
static void Secure_DMA_LockAllChannels(void);
static void Secure_TZIC_Init(void);
static void SMPU_SetupTaskRegions(void);


/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* SAU/IDAU, FPU and interrupts secure/non-secure allocation setup done */
  /* in SystemInit() based on partition_stm32l562xx.h file's definitions. */
  /* USER CODE BEGIN 1 */
  /* Enable SecureFault handler (HardFault is default) */
  SCB->SHCSR |= SCB_SHCSR_SECUREFAULTENA_Msk;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* GTZC initialisation */
  MX_GTZC_S_Init();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_ICACHE_Init();
  /* USER CODE BEGIN 2 */
   /* Add your secure application code here prior to non-secure initialization
     */

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk | CoreDebug_DEMCR_MON_EN_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL	|= DWT_CTRL_CYCCNTENA_Msk; 


  /* All IOs are by default allocated to secure */
  /* Release them all to non-secure except PG.12 (LED10) kept as secure */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  HAL_GPIO_ConfigPinAttributes(GPIOA, GPIO_PIN_All, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOB, GPIO_PIN_All, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOC, GPIO_PIN_All, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOD, GPIO_PIN_All, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOE, GPIO_PIN_All, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_All, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOG, (GPIO_PIN_All & ~(GPIO_PIN_12)), GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOH, GPIO_PIN_All, GPIO_PIN_NSEC);

  Secure_DMA_LockAllChannels();
  Secure_TZIC_Init();

  /* Leave the GPIO clocks enabled to let non-secure having I/Os control */

  /* Initialize PG.12 to drive external LED (LED10) */
  BSP_LED_Init(LED10);

  /* Secure SysTick should rather be suspended before calling non-secure  */
  /* in order to avoid wake-up from sleep mode entered by non-secure      */
  /* The Secure SysTick shall be resumed on non-secure callable functions */
  HAL_SuspendTick();
  /* USER CODE END 2 */

  /*************** Setup and jump to non-secure *******************************/

  SMPU_SetupTaskRegions();
  NonSecure_Init();

  /* Non-secure software does not return, this code is not executed */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief  Non-secure call function
  *         This function is responsible for Non-secure initialization and switch
  *         to non-secure state
  * @retval None
  */
static void NonSecure_Init(void)
{
  funcptr_NS NonSecure_ResetHandler;

  SCB_NS->VTOR = VTOR_TABLE_NS_START_ADDR;

  /* Set non-secure main stack (MSP_NS) */
  __TZ_set_MSP_NS((*(uint32_t *)VTOR_TABLE_NS_START_ADDR));

  /* Get non-secure reset handler */
  NonSecure_ResetHandler = (funcptr_NS)(*((uint32_t *)((VTOR_TABLE_NS_START_ADDR) + 4U)));

  /* Start non-secure state software application */
  NonSecure_ResetHandler();
}

/**
  * @brief GTZC_S Initialization Function
  * @param None
  * @retval None
  */
static void MX_GTZC_S_Init(void)
{

  /* USER CODE BEGIN GTZC_S_Init 0 */

  /* USER CODE END GTZC_S_Init 0 */

  MPCBB_ConfigTypeDef MPCBB_NonSecureArea_Desc = {0};

  /* USER CODE BEGIN GTZC_S_Init 1 */

  /* USER CODE END GTZC_S_Init 1 */
  MPCBB_NonSecureArea_Desc.SecureRWIllegalMode = GTZC_MPCBB_SRWILADIS_ENABLE;
  MPCBB_NonSecureArea_Desc.InvertSecureState = GTZC_MPCBB_INVSECSTATE_NOT_INVERTED;

  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[0] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[1] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[2] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[3] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[4] =   0xFFFFFFFF;

  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[5] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[6] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[7] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[8] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[9] =   0xFFFFFFFF;

  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[10] =   0xFFFFFFFF;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[11] =   0xFFFFFFFF;

  //NonSecure 
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[12] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[13] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[14] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[15] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[16] =   0x00000000;

  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[17] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[18] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[19] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[20] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[21] =   0x00000000;

  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[22] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[23] =   0x00000000;


  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_LockConfig_array[0] =   0x00000000;
  if (HAL_GTZC_MPCBB_ConfigMem(SRAM1_BASE, &MPCBB_NonSecureArea_Desc) != HAL_OK)
  {
    Error_Handler();
  }
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[0] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[1] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[2] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[3] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[4] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[5] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[6] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_SecConfig_array[7] =   0x00000000;
  MPCBB_NonSecureArea_Desc.AttributeConfig.MPCBB_LockConfig_array[0] =   0x00000000;
  if (HAL_GTZC_MPCBB_ConfigMem(SRAM2_BASE, &MPCBB_NonSecureArea_Desc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN GTZC_S_Init 2 */

  /* USER CODE END GTZC_S_Init 2 */

}

/**
  * @brief ICACHE Initialization Function
  * @param None
  * @retval None
  */
static void MX_ICACHE_Init(void)
{

  /* USER CODE BEGIN ICACHE_Init 0 */

  /* USER CODE END ICACHE_Init 0 */

  /* USER CODE BEGIN ICACHE_Init 1 */

  /* USER CODE END ICACHE_Init 1 */

  /** Enable instruction cache in 1-way (direct mapped cache)
  */
  if (HAL_ICACHE_ConfigAssociativityMode(ICACHE_1WAY) != HAL_OK)
  {
    Error_Handler();
  }
#if 0 
  if (HAL_ICACHE_Enable() != HAL_OK)
  {
    Error_Handler();
  }
#endif 
  /* USER CODE BEGIN ICACHE_Init 2 */

  /* USER CODE END ICACHE_Init 2 */

}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while(1) 
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* Infinite loop */
  while (1)
  {
  }
  /* USER CODE END 6 */
}

#endif /* USE_FULL_ASSERT */

static void Secure_DMA_LockAllChannels(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    for (uint32_t ch = 0; ch < 7; ch++)
    {
        DMA_Channel_TypeDef *pChannel = (DMA_Channel_TypeDef *)
            (DMA1_Channel1_BASE + ch * (DMA1_Channel2_BASE - DMA1_Channel1_BASE));

        pChannel->CCR |= DMA_CCR_SECM;
        pChannel->CCR &= ~(DMA_CCR_SSEC | DMA_CCR_DSEC);
    }

    for (uint32_t ch = 0; ch < 7; ch++)
    {
        DMA_Channel_TypeDef *pChannel = (DMA_Channel_TypeDef *)
            (DMA2_Channel1_BASE + ch * (DMA2_Channel2_BASE - DMA2_Channel1_BASE));

        pChannel->CCR |= DMA_CCR_SECM;
        pChannel->CCR &= ~(DMA_CCR_SSEC | DMA_CCR_DSEC);
    }

    __DSB();
    __ISB();
}


void Secure_TZIC_Init(void)
{

    GTZC_TZIC_S->FCR1 = 0xFFFFFFFFU;
    GTZC_TZIC_S->FCR2 = 0xFFFFFFFFU;
    GTZC_TZIC_S->FCR3 = 0xFFFFFFFFU;

    /* 부팅 중에는 TZIC SRAM1 인터럽트를 꺼 둔다. NS .bss 0 채우기가 MPCBB Secure 가드에
     * 쓸 때 GTZC_IRQHandler 가 오발하기 때문이다. SRAM1IE 는 첫 DMA 전에
     * SECURE_dma_m2m_init() -> Secure_TZIC_ArmGuardIrqOnce() (secure_nsc.c) 가 켠다. */
    GTZC_TZIC_S->IER1 = 0U;
    GTZC_TZIC_S->IER2 = 0U;
    GTZC_TZIC_S->IER3 &= ~GTZC_TZIC_IER3_SRAM1IE_Msk;

    // NVIC 
    NVIC_ClearPendingIRQ(GTZC_IRQn);
    NVIC_SetPriority(GTZC_IRQn, 0);
    NVIC_EnableIRQ(GTZC_IRQn);

    __DSB();
    __ISB();
}


/*
 * 태스크별 DMA Cluster S-MPU 태그 + 가드 블록 MPCBB 설정.
 *
 * docs/gap-analysis.md 결정에 따라 이번 라운드에서는 §3.1 의 Cluster/가드 자동 배치는
 * 구현하지 않는다. 대신 기존 SMPU_SetupProbeRegion() 과 동일한 방식(수동 하드코딩)을
 * NonSecure 쪽 두 DMA 태스크(DMA_Task_A/B, project/NonSecure/Src/app_freertos.c 의
 * s_dmaTaskMem[DMA_TASK_A/B])에 맞춰 그대로 반복한다.
 *
 * NS 쪽 배치(§3.1, [그림 2], 주소 증가 순): 태스크 전역 데이터(256B) -> DMA Cluster(256B) -> 가드(256B),
 * Task A 가드 바로 위에 Task B 전역 데이터가 온다(배열 s_dmaTaskMem[2]).
 * 전역 데이터 + Cluster 는 NS-MPU 영역 하나, Cluster 는 S-MPU 영역 하나, 가드는 MPCBB.
 *
 * 주소는 NonSecure 쪽을 먼저 빌드한 뒤 아래 방법으로 직접 읽은 값이다(추후 버퍼
 * 선언이나 레이아웃이 바뀌면 다시 읽어 갱신해야 한다 — 자동 동기화 없음, 지침대로
 * 테이블/링커 섹션을 쓰지 않기로 했기 때문):
 *   arm-none-eabi-nm Projects/.../NonSecure/Debug/FreeRTOS_SecureIOToggle_TrustZone_NonSecure.elf \
 *       | grep -E " s_dmaTaskMem$"
 * 결과(2026-11-01, 재측정):
 *   2001ae00 00000600 b s_dmaTaskMem
 *     [A] globals 0x2001ae00-eff, cluster 0x2001af00-fff, guard 0x2001b000-0ff
 *     [B] globals 0x2001b100-1ff, cluster 0x2001b200-2ff, guard 0x2001b300-3ff
 *
 * S-MPU 영역 4,5 는 각 태스크의 Cluster(256B) 만 태그한다. 태스크 전역 데이터와 guard 는
 * 어떤 S-MPU 영역에도 포함하지 않는다 — Secure_nsc.c 의 verify_inst() 가 TTT 로 base 와
 * base+len-1 의 영역을 조회할 때, base 가 전역 데이터를 가리키거나(같은 NS-MPU 영역이라
 * TTAT 는 통과) len 이 Cluster 를 넘어가면(공격 시나리오 (b), end 가 guard 안) 영역이
 * 무효/불일치가 되어 거부되게 하기 위함이다.
 */
#define DMA_TASK_A_CLUSTER_BASE   0x2001af00u
#define DMA_TASK_A_CLUSTER_SIZE   0x100u                                      /* 256B */
#define DMA_TASK_A_CLUSTER_LIMIT  (DMA_TASK_A_CLUSTER_BASE + DMA_TASK_A_CLUSTER_SIZE - 1u)
#define DMA_TASK_A_GUARD_BASE     (DMA_TASK_A_CLUSTER_BASE + DMA_TASK_A_CLUSTER_SIZE) /* 0x2001b000 */
#define DMA_TASK_A_SMPU_REGION    4u

#define DMA_TASK_B_CLUSTER_BASE   0x2001b200u
#define DMA_TASK_B_CLUSTER_SIZE   0x100u                                      /* 256B */
#define DMA_TASK_B_CLUSTER_LIMIT  (DMA_TASK_B_CLUSTER_BASE + DMA_TASK_B_CLUSTER_SIZE - 1u)
#define DMA_TASK_B_GUARD_BASE     (DMA_TASK_B_CLUSTER_BASE + DMA_TASK_B_CLUSTER_SIZE) /* 0x2001b300 */
#define DMA_TASK_B_SMPU_REGION    5u

#if (DMA_TASK_A_GUARD_BASE & 0xFFu) || (DMA_TASK_B_GUARD_BASE & 0xFFu)
#error "guard base must be MPCBB 256B block aligned"
#endif

static void SMPU_SetupOneTaskRegion(uint32_t region, uint32_t base, uint32_t limit)
{
    MPU->RNR  = region;
    MPU->RBAR = (base & MPU_RBAR_BASE_Msk)
              | (0u << MPU_RBAR_SH_Pos)    /* Non-shareable */
              | (1u << MPU_RBAR_AP_Pos)    /* AP=01: RW, priv+unpriv */
              | (1u << MPU_RBAR_XN_Pos);   /* XN */
    MPU->RLAR = (limit & MPU_RLAR_LIMIT_Msk)
              | (0u << MPU_RLAR_AttrIndx_Pos)
              | MPU_RLAR_EN_Msk;
}

static void SMPU_SetupTaskRegions(void)
{
    uint32_t guardAttr = GTZC_MCPBB_BLOCK_SEC;

    __DMB();
    MPU->CTRL = 0;

    MPU->MAIR0 = 0xFFu;

    SMPU_SetupOneTaskRegion(DMA_TASK_A_SMPU_REGION, DMA_TASK_A_CLUSTER_BASE, DMA_TASK_A_CLUSTER_LIMIT);
    SMPU_SetupOneTaskRegion(DMA_TASK_B_SMPU_REGION, DMA_TASK_B_CLUSTER_BASE, DMA_TASK_B_CLUSTER_LIMIT);

    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();

    /* 가드 블록(각 256B, 1블록)을 MPCBB Secure 로 지정. MX_GTZC_S_Init() 이 NS 로
     * 설정해 둔 구간 위에 이 두 블록만 Secure 로 덮어쓴다(§3.1, §3.3). */
    if (HAL_GTZC_MPCBB_ConfigMemAttributes(DMA_TASK_A_GUARD_BASE, 1u, &guardAttr) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_GTZC_MPCBB_ConfigMemAttributes(DMA_TASK_B_GUARD_BASE, 1u, &guardAttr) != HAL_OK)
    {
        Error_Handler();
    }
}


