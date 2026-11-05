/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/NonSecure/Src/main.c
  * @author  MCD Application Team
  * @brief   Main program body (non-secure)
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
#include "cmsis_os.h"

/* 
#define DMA_BLK        256U
#define DMA_ROUND(n) (((n) + DMA_BLK - 1U) & ~(DMA_BLK - 1U))

#define DMA_CAT_(a,b)  a##b
#define DMA_CAT(a,b)   DMA_CAT_(a,b)
#define DMA_STR_(x)  #x
#define DMA_STR(x)   DMA_STR_(x)
typedef struct {
    uint32_t pad_upper;
    uint32_t pad_lower;
} DMA_Guard_t;

#define DMA_ALLOCATE_BUFFER(name, sz)                                        \
    DMA_ALLOC_G_IMPL(name, sz, DMA_CAT(_dma_slot_, __COUNTER__))

#define DMA_ALLOC_G_IMPL(name, sz, uniq)     \
    static struct {                           \                              
        uint8_t pad_upper[DMA_BLK];            \                            
        uint8_t body[DMA_ROUND(sz)];             \                       
        uint8_t pad_lower[DMA_BLK];             \                            
    } __attribute__((aligned(DMA_BLK))) uniq;	\                          
    static const DMA_Guard_t			\			
    __attribute__((section(".guard_table." DMA_STR(uniq)))) \
    __attribute__((used))					\
    __attribute__((aligned(4)))					        \
    DMA_CAT(uniq, _desc) = {                                            \    
        .pad_upper = (uint32_t)(uintptr_t)uniq.pad_upper,             \
        .pad_lower  = (uint32_t)(uintptr_t)uniq.pad_lower,              \
    };                                                                   \   
    uint8_t * const name = uniq.body
*/ 

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* Definitions for LEDThreadHandle */
osThreadId_t LEDThreadHandleHandle;
const osThreadAttr_t LEDThreadHandle_attributes = {
  .name = "LEDThreadHandle",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};

#if 0 
osThreadId_t DMATestHandle;
const osThreadAttr_t DMATestHandle_attributes = {
	.name = "DMATestHandle",
	.priority = (osPriority_t) osPriorityNormal,
	.stack_size = 512 
};
#endif 

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void LED_Thread(void *argument);
//void dma_test(void *argument);

/* USER CODE BEGIN PFP */
void SecureFault_Callback(void);
void SecureError_Callback(void);
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
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* Register SecureFault callback defined in non-secure and to be called by secure handler */
  SECURE_RegisterCallback(SECURE_FAULT_CB_ID, (void *)SecureFault_Callback);

  /* Register SecureError callback defined in non-secure and to be called by secure handler */
  SECURE_RegisterCallback(GTZC_ERROR_CB_ID, (void *)SecureError_Callback);

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */
  /* Initialize PD.03 to drive external LED (LED9) */
  BSP_LED_Init(LED9);
  /* USER CODE END 2 */
#if 0 
  //DWT write protection of specifc memory region 
  #define DWT_COMP_REG(n)  (*(volatile uint32_t *)(0xE0001020u + 0x10u * (n)))
  #define DWT_FUNC_REG(n)  (*(volatile uint32_t *)(0xE0001028u + 0x10u * (n)))

  DWT_FUNC_REG(0) = 0;
  DWT_FUNC_REG(1) = 0;
  DWT_COMP_REG(0) = 0x20018500u;   // lower
  DWT_COMP_REG(1) = 0x200185FFu;   // upper (limit)
  DWT_FUNC_REG(0) = (0x1u << 4) | 0x5u;
  DWT_FUNC_REG(1) = (0x1u << 4) | 0x7u;
  __DSB(); __ISB(); 
#endif 
  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of LEDThreadHandle */
//  LEDThreadHandleHandle = osThreadNew(LED_Thread, NULL, &LEDThreadHandle_attributes);

//  DMATestHandle = osThreadNew(dma_test, NULL, &DMATestHandle_attributes); 

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  vCreateAppTasks();
	
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
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
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
#if defined(BENCH_CLK_32M)
  RCC_OscInitStruct.PLL.PLLN = 16;   /* EVAL: D-Box 와 같은 32MHz (4MHz MSI * 16 / 2) */
#else
  RCC_OscInitStruct.PLL.PLLN = 55;
#endif
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

#if defined(BENCH_CLK_32M)
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
#else
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
#endif
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
  * @brief  Callback called by secure code following a secure fault interrupt
  * @note   This callback is called by secure code thanks to the registration
  *         done by the non-secure application with non-secure callable API
  *         SECURE_RegisterCallback(SECURE_FAULT_CB_ID, (void *)SecureFault_Callback);
  * @retval None
  */
void SecureFault_Callback(void)
{
  /* Go to error infinite loop when Secure fault generated by IDAU/SAU check */
  /* because of illegal access */
  Error_Handler();
}


/**
  * @brief  Callback called by secure code following a GTZC TZIC secure interrupt (GTZC_IRQn)
  * @note   This callback is called by secure code thanks to the registration
  *         done by the non-secure application with non-secure callable API
  *         SECURE_RegisterCallback(GTZC_ERROR_CB_ID, (void *)SecureError_Callback);
  * @retval None
  */
void SecureError_Callback(void)
{
  /* Go to error infinite loop when Secure error generated by GTZC check */
  /* because of illegal access */
  Error_Handler();
}
/* USER CODE END 4 */


/* USER CODE BEGIN Header_LED_Thread */
/**
  * @brief  Function implementing the LEDThreadHandle thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_LED_Thread */
void LED_Thread(void *argument)
{
  /* USER CODE BEGIN 5 */
  (void) argument;

  portALLOCATE_SECURE_CONTEXT (configMINIMAL_SECURE_STACK_SIZE);

  for (;;)
  {
    /* Toggle secure LED4 (LED_GREEN) */
    SECURE_LEDToggle();
    osDelay(1000);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* LED9 on */
  BSP_LED_On(LED9);

  /* Infinite loop */
  while (1)
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

//static uint8_t src_buf[512] __attribute__((aligned(256)));
//static uint8_t dst_buf[512] __attribute__((aligned(256)));
//DMA_ALLOCATE_BUFFER(dst_buf, 256);


#if 0 
static volatile uint8_t done;

void Secure_dma_done(uint32_t status)
{
    done = (status == 0) ? 1 : 2;
}

static volatile uint32_t t0, t1, result, overhead; 
void dma_test(void *argument)
{
	(void) argument;
    portALLOCATE_SECURE_CONTEXT (configMINIMAL_SECURE_STACK_SIZE);

    SECURE_dma_m2m_init((void*)Secure_dma_done);

    for (int i = 0; i < 512; i++)
    {
	    src_buf[i] = (uint8_t)i;
    }

    done = 0;

    __DSB(); __ISB();
    uint32_t t0= DWT->CYCCNT;
    __DSB();
    uint32_t t1= DWT->CYCCNT;
    overhead = t1- t0;

    __DSB(); __ISB();
    t0 = DWT->CYCCNT;
    int r = SECURE_dma_m2m_start(src_buf, dst_buf, 256);
    __DSB();
    t1 = DWT->CYCCNT;
    result = t1 - t0;
    __asm volatile ("BKPT #0");


#if 0 
    if (SECURE_dma_m2m_start(src_buf, dst_buf, 256) != 0) { 
	    done = 0;
	    osThreadTerminate(osThreadGetId());
    }
#endif 
	if(dst_buf[1] == 0) {
		done = 0;
		
	}
    while(!done){

    }
    osThreadTerminate(osThreadGetId());
}
#endif 




#if 0 
#define BENCH_N   64u      /* 라운드 수 */
#define N_ITER    32u      /* 라운드당 호출 수 */
#define XFER_SZ   8u

typedef struct { uint32_t min, max, avg; } stat_t;

static stat_t stat_of(volatile const uint32_t *a, uint32_t n)
{
    stat_t s = { 0xFFFFFFFFu, 0u, 0u };
    uint64_t sum = 0u;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = a[i];
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
        sum += v;
    }
    s.avg = (uint32_t)(sum / n);
    return s;
}
volatile uint32_t overhead;
volatile stat_t   st_nop;        /* 빈 veneer */
volatile stat_t   st_call;       /* 8B 호출 1회 */
volatile stat_t   st_round;      /* 32회 합 */
volatile uint32_t wall_burst, wall_single;
volatile uint32_t err_code;

static volatile uint32_t s_call[N_ITER];
static volatile uint32_t s_round[BENCH_N];

void dma_test(void *argument)
{
//	int a = dst_buf[0];
	dst_buf[1] = 4; 
	dst_buf[257] = 10;

    (void)argument;
    portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);
    SECURE_dma_m2m_init((void *)Secure_dma_done);

    for (int i = 0; i < 512; i++) src_buf[i] = (uint8_t)i;

    /* ---- 측정 오버헤드 ---- */
    {
        uint32_t best = 0xFFFFFFFFu;
        for (uint32_t i = 0; i < 64u; i++) {
            __DSB(); __ISB();
            uint32_t a = DWT->CYCCNT;
            __DSB();
            uint32_t b = DWT->CYCCNT;
            if ((b - a) < best) best = b - a;
        }
        overhead = best;
    }

    /* ---- 기준선: 빈 veneer ---- */
    {
        for (uint32_t i = 0; i < 32u; i++) (void)SECURE_nop3(src_buf, dst_buf, XFER_SZ);

        for (uint32_t i = 0; i < N_ITER; i++) {
            taskENTER_CRITICAL();
            __DSB(); __ISB();
            uint32_t t0 = DWT->CYCCNT;
            (void)SECURE_nop3(src_buf, dst_buf, XFER_SZ);
            __DSB();
            uint32_t t1 = DWT->CYCCNT;
            taskEXIT_CRITICAL();
            s_call[i] = t1 - t0;
        }
        st_nop = stat_of(s_call, N_ITER);
    }

    for (uint32_t i = 0; i < 32u; i++) {
        done = 0;
        if (SECURE_dma_m2m_start(&src_buf[0], &dst_buf[0], XFER_SZ) != 0) {
            err_code = 0xDEAD0001u; __asm volatile("BKPT #0");
            osThreadTerminate(osThreadGetId());
        }
        while (!done) { }
    }

    {
        uint32_t w0 = DWT->CYCCNT;

        for (uint32_t k = 0; k < BENCH_N; k++) {
            uint32_t sum = 0u;

            for (uint32_t i = 0; i < N_ITER; i++) {
                uint32_t off = i * XFER_SZ;
                done = 0;

                taskENTER_CRITICAL();
                __DSB(); __ISB();
                uint32_t t0 = DWT->CYCCNT;
                int r = SECURE_dma_m2m_start(&src_buf[off], &dst_buf[off], XFER_SZ);
                __DSB();
                uint32_t t1 = DWT->CYCCNT;
                taskEXIT_CRITICAL();

                uint32_t d = t1 - t0;
                sum += d;
                if (k == 0u) s_call[i] = d;      /* 첫 라운드만 호출별 보관 */

                if (r != 0) { err_code = 0xDEAD0000u | (uint32_t)(-r);
                              __asm volatile("BKPT #0");
                              osThreadTerminate(osThreadGetId()); }

                while (!done) { }
            }
            s_round[k] = sum;
        }

        wall_burst = (DWT->CYCCNT - w0) / BENCH_N;

        st_call  = stat_of(s_call,  N_ITER);
        st_round = stat_of(s_round, BENCH_N);
    }

    {
        uint32_t best = 0xFFFFFFFFu;
        for (uint32_t i = 0; i < 32u; i++) {
            done = 0;
            uint32_t s0 = DWT->CYCCNT;
            if (SECURE_dma_m2m_start(src_buf, dst_buf, 256u) != 0) {
                err_code = 0xDEAD0002u; __asm volatile("BKPT #0");
                osThreadTerminate(osThreadGetId());
            }
            uint32_t d = DWT->CYCCNT - s0;
            while (!done) { }
            if (d < best) best = d;
        }
        wall_single = best;
    }

//    __asm volatile ("BKPT #0");
    osThreadTerminate(osThreadGetId());
}
#endif 
