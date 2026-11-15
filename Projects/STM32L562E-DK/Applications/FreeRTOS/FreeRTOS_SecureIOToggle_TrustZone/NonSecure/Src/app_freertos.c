/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2020 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "main.h"
#include <stddef.h>

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
/* USER CODE BEGIN Variables */
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* Evaluation app (paper §3-4): DMA_Task_A/B each own a DMA Cluster protected by an
 * S-MPU tag + MPCBB guard; Plain_Task uses no DMA (baseline). Per-task layout, in
 * address order: [globals 256B | Cluster 256B | guard 256B]. One struct per task
 * fixes the order (GCC reverses separate statics). Secure addresses are hardcoded. */

/* Cycle stats (32 runs after 1 warm-up). medStat: SECURE_dma_m2m_start() call cost;
 * rtStat: call -> Secure ISR -> NS callback. Rejected runs are excluded
 * (min stays 0xFFFFFFFF if all are rejected). GDB: p s_dmaTaskMem[0].globals.v */
typedef struct { uint32_t min, max, avg; } cycle_stat_t;

typedef struct
{
    volatile cycle_stat_t statMediator;
    volatile cycle_stat_t statRoundtrip;
    /* Kept inside the task's own NS-MPU region so the unprivileged task can read them */
    SemaphoreHandle_t     dmaMutex;    /* copy of s_dmaChannelMutex, set by vCreateAppTasks() */
    volatile uint8_t      dmaDone;     /* set to 1 by App_DmaDoneCallback() */
    volatile uint8_t      dmaStatus;   /* last result: 0 = TC, 1 = TEIF */
} dma_task_globals_t;

#define DMA_TASK_GLOBALS_SIZE   256u   /* 256B so the Cluster starts on a 256B boundary */
#define DMA_CLUSTER_SIZE        256u
#define DMA_GUARD_SIZE          256u   /* one MPCBB block */

typedef struct
{
    union
    {
        dma_task_globals_t v;
        uint8_t            raw[DMA_TASK_GLOBALS_SIZE];
    } globals;                       /* task globals (not a DMA target) */
    struct
    {
        uint8_t dst[DMA_CLUSTER_SIZE];   /* M2M DMA destination */
    } cluster;                       /* DMA Cluster: S-MPU tagged */
    uint8_t guard[DMA_GUARD_SIZE];   /* guard block reservation; never touched by NS code */
} __attribute__((aligned(256))) dma_task_mem_t;

_Static_assert(sizeof(dma_task_globals_t) <= DMA_TASK_GLOBALS_SIZE, "globals overflow");
_Static_assert(offsetof(dma_task_mem_t, cluster) % 32u == 0u, "cluster: S-MPU 32B alignment");
_Static_assert(offsetof(dma_task_mem_t, guard) % 256u == 0u, "guard: MPCBB 256B block alignment");
_Static_assert(sizeof(dma_task_mem_t) == offsetof(dma_task_mem_t, guard) + DMA_GUARD_SIZE,
               "no padding after guard -> next task's globals follow directly");

/* One NS-MPU user region = globals + Cluster (guard excluded) */
#define DMA_TASK_NSMPU_SIZE     offsetof(dma_task_mem_t, guard)

/* Single array guarantees [A.globals|A.cluster|A.guard|B.globals|B.cluster|B.guard] */
enum { DMA_TASK_A = 0, DMA_TASK_B = 1, DMA_TASK_NUM };
static dma_task_mem_t s_dmaTaskMem[DMA_TASK_NUM];

static uint8_t s_dmaSrcA[32] __attribute__((aligned(4)));
static uint8_t s_dmaSrcB[32] __attribute__((aligned(4)));

/* Serializes the single Secure DMA channel between tasks (app-level only).
 * Only privileged code uses this handle; tasks use their globals copy. */
static SemaphoreHandle_t s_dmaChannelMutex;

/* NS callback called by the Secure DMA ISR via BLXNS (privileged). Owner is unknown,
 * so all tasks' flags are set; harmless since each clears its own under the mutex.
 * Runs at Secure IRQ priority: no FreeRTOS API calls. */
static void App_DmaDoneCallback(uint32_t status)
{
    for (uint32_t i = 0; i < DMA_TASK_NUM; i++)
    {
        s_dmaTaskMem[i].globals.v.dmaStatus = (uint8_t)status;
        s_dmaTaskMem[i].globals.v.dmaDone   = 1u;
    }
}

#define DMA_BENCH_XFER_LEN   8u
/* DWT->CYCCNT (PPB) faults when unprivileged, so DMA tasks are privileged
 * only in BENCH_PHASE builds; the default build keeps them unprivileged. */
#if defined(BENCH_PHASE)
#define DMA_TASK_PRIV_BIT    portPRIVILEGE_BIT
#else
#define DMA_TASK_PRIV_BIT    0u
#endif

#if 0
/* Attack scenarios (disabled). (a) Bad args: B's Cluster -> rejected by TTAT;
 * own globals -> rejected by TTT. (b) Length past the Cluster: rejected at request
 * time, or else the guard triggers TZIC and the channel is aborted.
 * Memory beyond the guard must remain unmodified. */
static void AttackScenario_ArgTamper_TaskA(void)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;

    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    g->dmaDone = 0;
    int r = SECURE_dma_m2m_start(s_dmaSrcA, s_dmaTaskMem[DMA_TASK_B].cluster.dst, DMA_BENCH_XFER_LEN);
    configASSERT(r != 0);  /* (a-1) must be rejected */
    r = SECURE_dma_m2m_start(s_dmaSrcA, s_dmaTaskMem[DMA_TASK_A].globals.raw, DMA_BENCH_XFER_LEN);
    configASSERT(r != 0);  /* (a-2) must be rejected */
    xSemaphoreGive(g->dmaMutex);
}

static void AttackScenario_OverflowAfterVerify_TaskA(void)
{
    const uint32_t oversizedLen = sizeof(s_dmaTaskMem[DMA_TASK_A].cluster.dst) + 32u; /* past Cluster into guard */

    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;

    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    g->dmaDone = 0;
    int r = SECURE_dma_m2m_start(s_dmaSrcA, s_dmaTaskMem[DMA_TASK_A].cluster.dst, oversizedLen);
    if (r == 0)
    {
        while (!g->dmaDone) { }   /* wait for guard + TZIC to abort the channel */
    }
    xSemaphoreGive(g->dmaMutex);
}
#endif

#ifdef BENCH_PHASE
#include "bench_ns.inc"
#endif

/* Task stacks (alignment required by the port) */
#define STACK_SIZE_WORDS 256
static StackType_t xDmaTaskAStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));
static StackType_t xDmaTaskBStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));
static StackType_t xPlainTaskStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));

static void vDmaTaskA( void *pvParameters )
{
  (void)pvParameters;
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);

  for (uint32_t i = 0; i < sizeof(s_dmaSrcA); i++) { s_dmaSrcA[i] = (uint8_t)i; }

  dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;

#if defined(BENCH_PHASE)
  Bench_Main();
#endif

  for( ;; )
  {
    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    g->dmaDone = 0;
    if (SECURE_dma_m2m_start(s_dmaSrcA, s_dmaTaskMem[DMA_TASK_A].cluster.dst, DMA_BENCH_XFER_LEN) == 0)
    {
      while (!g->dmaDone) { }
    }
    xSemaphoreGive(g->dmaMutex);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void vDmaTaskB( void *pvParameters )
{
  (void)pvParameters;
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);

  for (uint32_t i = 0; i < sizeof(s_dmaSrcB); i++) { s_dmaSrcB[i] = (uint8_t)i; }

  dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_B].globals.v;

  for( ;; )
  {
    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    g->dmaDone = 0;
    if (SECURE_dma_m2m_start(s_dmaSrcB, s_dmaTaskMem[DMA_TASK_B].cluster.dst, DMA_BENCH_XFER_LEN) == 0)
    {
      while (!g->dmaDone) { }
    }
    xSemaphoreGive(g->dmaMutex);

    vTaskDelay(pdMS_TO_TICKS(700));
  }
}

/* Example data the task is allowed to see */
static uint8_t ucPrivateBuffer[256];
static const uint32_t ulLookupTable[128];

/* Baseline task without DMA (no S-MPU tag on the Secure side) */
static void vPlainTask( void *pvParameters )
{
  (void)pvParameters;
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);

  for( ;; )
  {
    SECURE_LEDToggle();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

/* Parameters for xTaskCreateRestricted */
static const TaskParameters_t xDmaTaskAParams =
{
  .pvTaskCode = vDmaTaskA,
  .pcName = "DMA_Task_A",
  .usStackDepth = STACK_SIZE_WORDS,
  .pvParameters = NULL,
  .uxPriority =  (tskIDLE_PRIORITY + 1 ) | DMA_TASK_PRIV_BIT,   /* see DMA_TASK_PRIV_BIT */
  .puxStackBuffer = xDmaTaskAStack,
  .xRegions =
  {
    /* Region 0: globals + Cluster (guard excluded); S-MPU tags only the Cluster */
    { &s_dmaTaskMem[DMA_TASK_A], DMA_TASK_NSMPU_SIZE, portMPU_REGION_READ_WRITE },
    /* Region 1: task-private DMA source (outside the Cluster) */
    { s_dmaSrcA, sizeof( s_dmaSrcA ), portMPU_REGION_READ_WRITE },
    /* Region 2: unused; the guard is never in any NS-MPU region */
    { 0, 0, 0 }
  }
};

static const TaskParameters_t xDmaTaskBParams =
{
  .pvTaskCode = vDmaTaskB,
  .pcName = "DMA_Task_B",
  .usStackDepth = STACK_SIZE_WORDS,
  .pvParameters = NULL,
  .uxPriority =  (tskIDLE_PRIORITY + 1 ) | DMA_TASK_PRIV_BIT,   /* see DMA_TASK_PRIV_BIT */
  .puxStackBuffer = xDmaTaskBStack,
  .xRegions =
  {
    { &s_dmaTaskMem[DMA_TASK_B], DMA_TASK_NSMPU_SIZE, portMPU_REGION_READ_WRITE },
    { s_dmaSrcB, sizeof( s_dmaSrcB ), portMPU_REGION_READ_WRITE },
    { 0, 0, 0 }
  }
};

static const TaskParameters_t xPlainTaskParams =
{
  .pvTaskCode = vPlainTask,
  .pcName = "Plain_Task",
  .usStackDepth = STACK_SIZE_WORDS,
  .pvParameters = NULL,
  .uxPriority =  (tskIDLE_PRIORITY + 1 ),
  .puxStackBuffer = xPlainTaskStack,
  .xRegions =
  {
    { ucPrivateBuffer, sizeof( ucPrivateBuffer ), portMPU_REGION_READ_WRITE },
    { ulLookupTable, sizeof( ulLookupTable ), portMPU_REGION_READ_ONLY },
    { 0, 0, 0 }
  }
};

void vCreateAppTasks( void )
{
  s_dmaChannelMutex = xSemaphoreCreateMutex();
  configASSERT(s_dmaChannelMutex != NULL);

  /* Copy the handle into each task's globals (privileged, before scheduler start) */
  s_dmaTaskMem[DMA_TASK_A].globals.v.dmaMutex = s_dmaChannelMutex;
  s_dmaTaskMem[DMA_TASK_B].globals.v.dmaMutex = s_dmaChannelMutex;

  SECURE_dma_m2m_init((void *)App_DmaDoneCallback);

  TaskHandle_t hDmaTaskA = NULL, hDmaTaskB = NULL;
  xTaskCreateRestricted( &xDmaTaskAParams, &hDmaTaskA );
  xTaskCreateRestricted( &xDmaTaskBParams, &hDmaTaskB );
  /* With configENABLE_ACCESS_CONTROL_LIST = 1, ungranted take/give fails (pdFAIL) */
  vGrantAccessToSemaphore( hDmaTaskA, s_dmaChannelMutex );
  vGrantAccessToSemaphore( hDmaTaskB, s_dmaChannelMutex );
#if defined(BENCH_PHASE)
  /* EVAL: measure Task A only; create all tasks to keep .bss layout, suspend B/Plain */
  TaskHandle_t hPlainTask = NULL;
  xTaskCreateRestricted( &xPlainTaskParams, &hPlainTask );
  vTaskSuspend( hDmaTaskB );
  vTaskSuspend( hPlainTask );
#else
  xTaskCreateRestricted( &xPlainTaskParams, NULL );
#endif
}

/* USER CODE END Application */
