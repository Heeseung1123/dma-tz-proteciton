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

/* Unused since 0ae8cde (never written): mediator cost is in bench_ns.inc tables 1-8,
 * ISR cost in the planned ISR tables. Kept commented; globals stay a 256B union.
typedef struct { uint32_t min, max, avg; } cycle_stat_t;
*/

typedef struct
{
    /* volatile cycle_stat_t statMediator;  */
    /* volatile cycle_stat_t statRoundtrip; */
    /* Kept inside the task's own NS-MPU region so the unprivileged task can read them */
    SemaphoreHandle_t     dmaMutex;    /* copy of s_dmaChannelMutex, set by vCreateAppTasks() */
    TaskHandle_t          hTask;       /* doorbell ISR notifies this task, set by vCreateAppTasks() */
    volatile uint8_t      dmaPending;  /* 1 while this task's transfer is in flight (doorbell owner) */
    volatile uint8_t      dmaDone;     /* set to 1 by the completion handler */
    volatile uint8_t      dmaStatus;   /* last result: 0 = TC, 1 = TEIF */
} dma_task_globals_t;

#if defined(MEDIATOR_INSECURE)
/* Memory baseline (cmpMemUsage.md §2.2 C7): no guard, so no 256B alignment is needed.
 * NS-MPU (ARMv8-M) only needs 32B base/limit granularity. */
#define DMA_TASK_GLOBALS_SIZE   ((sizeof(dma_task_globals_t) + 31u) & ~31u)
#define DMA_CLUSTER_SIZE        256u
#else
#define DMA_TASK_GLOBALS_SIZE   256u   /* 256B so the Cluster starts on a 256B boundary */
#define DMA_CLUSTER_SIZE        256u
#define DMA_GUARD_SIZE          256u   /* one MPCBB block */
#endif

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
#if defined(MEDIATOR_INSECURE)
} __attribute__((aligned(32))) dma_task_mem_t;

_Static_assert(sizeof(dma_task_globals_t) <= DMA_TASK_GLOBALS_SIZE, "globals overflow");
_Static_assert(sizeof(dma_task_mem_t) % 32u == 0u, "NS-MPU 32B granularity");

/* One NS-MPU user region = globals + DMA buffer */
#define DMA_TASK_NSMPU_SIZE     sizeof(dma_task_mem_t)
#else
    uint8_t guard[DMA_GUARD_SIZE];   /* guard block reservation; never touched by NS code */
} __attribute__((aligned(256))) dma_task_mem_t;

_Static_assert(sizeof(dma_task_globals_t) <= DMA_TASK_GLOBALS_SIZE, "globals overflow");
_Static_assert(offsetof(dma_task_mem_t, cluster) % 32u == 0u, "cluster: S-MPU 32B alignment");
_Static_assert(offsetof(dma_task_mem_t, guard) % 256u == 0u, "guard: MPCBB 256B block alignment");
_Static_assert(sizeof(dma_task_mem_t) == offsetof(dma_task_mem_t, guard) + DMA_GUARD_SIZE,
               "no padding after guard -> next task's globals follow directly");

/* One NS-MPU user region = globals + Cluster (guard excluded) */
#define DMA_TASK_NSMPU_SIZE     offsetof(dma_task_mem_t, guard)
#endif

/* Single array guarantees [A.globals|A.cluster|A.guard|B.globals|B.cluster|B.guard] */
enum { DMA_TASK_A = 0, DMA_TASK_B = 1, DMA_TASK_NUM };
static dma_task_mem_t s_dmaTaskMem[DMA_TASK_NUM];

static uint8_t s_dmaSrcA[32] __attribute__((aligned(4)));
static uint8_t s_dmaSrcB[32] __attribute__((aligned(4)));

/* Serializes the single Secure DMA channel between tasks (app-level only).
 * Only privileged code uses this handle; tasks use their globals copy. */
static SemaphoreHandle_t s_dmaChannelMutex;

#if defined(DMA_NOTIFY_BLXNS)
/* A (comparison target): NS callback called by the Secure DMA ISR via BLXNS (privileged).
 * Owner is unknown, so all tasks' flags are set; harmless since each clears its own under
 * the mutex. Runs at Secure IRQ priority: no FreeRTOS API calls, the task spins. */
static void App_DmaDoneCallback(uint32_t status)
{
    for (uint32_t i = 0; i < DMA_TASK_NUM; i++)
    {
        s_dmaTaskMem[i].globals.v.dmaStatus = (uint8_t)status;
        s_dmaTaskMem[i].globals.v.dmaDone   = 1u;
    }
}
#else
#if !defined(MEDIATOR_INSECURE)
/* B (default): the Secure DMA ISR writes the result here and pends SECURE_DMA_DOORBELL_IRQn.
 * Checked by SECURE_dma_m2m_init() (NS, writable by the caller). */
static volatile uint32_t s_dmaStatusWord;
#endif

/* Completion -> owner task. Shared by the doorbell ISR and the bench's Insecure DMA ISR
 * (bench_ns.inc), so both paths run the same notify code. */
static inline __attribute__((always_inline)) void App_DmaNotifyOwner(uint32_t status)
{
    BaseType_t woken = pdFALSE;

    for (uint32_t i = 0; i < DMA_TASK_NUM; i++)
    {
        dma_task_globals_t *g = &s_dmaTaskMem[i].globals.v;
        if (g->dmaPending)
        {
            g->dmaPending = 0u;
            g->dmaStatus  = (uint8_t)status;
            g->dmaDone    = 1u;
            vTaskNotifyGiveFromISR(g->hTask, &woken);
        }
    }
    portYIELD_FROM_ISR(woken);
}

#if defined(MEDIATOR_INSECURE)
/* ---- Insecure mediator (memory baseline, cmpMemUsage.md §4, pattern 3) ----
 * NS programs DMA1_CH1 (SECM=0, released by Secure main.c) directly. Same register sequence as
 * SECURE_dma_m2m_start without verify_inst and s_busy; the ISR body is the bench's Insecure
 * baseline ISR (bench_ns.inc) on CH1. The unprivileged DMA tasks reach these registers through
 * one NS-MPU region (xRegions[2], DMA1 ISR/IFCR + CH1 registers, 32B). */
#define INSECURE_DMA_NSMPU_BASE   ((void *)DMA1_BASE)   /* 0x40020000: ISR, IFCR, CH1 CCR..CM1AR */
#define INSECURE_DMA_NSMPU_SIZE   32u
_Static_assert(DMA1_Channel1_BASE + sizeof(DMA_Channel_TypeDef) <= DMA1_BASE + INSECURE_DMA_NSMPU_SIZE,
               "CH1 registers inside the NS-MPU region");

/* Privileged, before the scheduler (vCreateAppTasks): same CCR bits as SECURE_dma_m2m_init */
static void Insecure_dma_m2m_init(void)
{
    DMA1_Channel1->CCR |= (DMA_CCR_MEM2MEM
                        | DMA_CCR_MINC | DMA_CCR_PINC
                        | DMA_CCR_PL_1
                        | DMA_CCR_TCIE | DMA_CCR_TEIE);
    NVIC_SetPriority(DMA1_Channel1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

/* Called by the unprivileged task. No argument check beyond len (no protection) */
static int Insecure_dma_m2m_start(void *src_buf, void *dst_buf, uint32_t len)
{
    if (len == 0) return -1;

    DMA1_Channel1->CCR  &= ~DMA_CCR_EN;
    DMA1_Channel1->CPAR  = (uint32_t)src_buf;
    DMA1_Channel1->CM0AR = (uint32_t)dst_buf;
    DMA1_Channel1->CNDTR = len;
    DMA1->IFCR           = DMA_IFCR_CGIF1;
    DMA1_Channel1->CCR  |= DMA_CCR_EN;
    return 0;
}

void DMA1_Channel1_IRQHandler(void)
{
    uint32_t isr = DMA1->ISR;
    uint32_t status;

    if (isr & DMA_ISR_TEIF1)      { status = 1u; }
    else if (isr & DMA_ISR_TCIF1) { status = 0u; }
    else { DMA1->IFCR = DMA_IFCR_CGIF1; return; }

    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF1;
    App_DmaNotifyOwner(status);
}
#else /* !MEDIATOR_INSECURE */
#if BENCH_ISR_TS == 3
/* ISR bench stamp 3 (Tn): vector entry, before the compiler prologue of the real handler */
__attribute__((naked)) void DFSDM1_FLT3_IRQHandler(void) { __asm volatile (ISR_TS_ASM "b Doorbell_IRQHandler \n"); }
#define DFSDM1_FLT3_IRQHandler  Doorbell_IRQHandler
#endif

/* NS doorbell ISR (vector of SECURE_DMA_DOORBELL_IRQn). A real NS IRQ at
 * configMAX_SYSCALL priority, so FreeRTOS FromISR API is allowed: wake the owner task. */
void DFSDM1_FLT3_IRQHandler(void)
{
    ISR_TS(4);   /* Tc: after prologue */
    App_DmaNotifyOwner(s_dmaStatusWord);
    ISR_TS(5);   /* Td: before epilogue */
}
#endif /* MEDIATOR_INSECURE */
#endif

#if configDMA_MEDIATOR_SVC
/* DMA 요청을 SVC #portSVC_DMA_M2M_START 로 보낸다 (cmpAsmMediator.md §9). NS SVC_Handler fast path 가
 * Handler mode 에서 SECURE_dma_m2m_start_svc 를 부르고, 결과를 stacked r0 에 써 준다. 비특권 Task 에서도 쓸 수 있다. */
static inline int App_DmaStartSvc(void *src, void *dst, uint32_t len)
{
    register uint32_t r0 __asm("r0") = (uint32_t)src;
    register uint32_t r1 __asm("r1") = (uint32_t)dst;
    register uint32_t r2 __asm("r2") = len;
    __asm volatile ("svc %[n]" : "+r"(r0) : "r"(r1), "r"(r2), [n] "i"(portSVC_DMA_M2M_START) : "memory");
    return (int)r0;
}
#endif

/* One M2M transfer through the Secure mediator; returns after completion (0) or on reject (-1).
 * Caller holds the DMA mutex. B blocks on a task notification, A spins on dmaDone. */
static int App_DmaM2M(dma_task_globals_t *g, void *src, void *dst, uint32_t len)
{
    g->dmaDone = 0u;
#if defined(DMA_NOTIFY_BLXNS)
    int r = SECURE_dma_m2m_start(src, dst, len);
    if (r == 0) { while (!g->dmaDone) { } }
#else
    g->dmaPending = 1u;          /* set before start: the ISR may run right after EN */
#if defined(MEDIATOR_INSECURE)
    int r = Insecure_dma_m2m_start(src, dst, len);
#elif configDMA_MEDIATOR_SVC
    int r = App_DmaStartSvc(src, dst, len);            /* SVC -> Handler mode NSC (후보 A) */
#elif MEDIATOR_ASM
    int r = SECURE_dma_m2m_start_asm(src, dst, len);   /* stackless mediator (Thread mode, §8 문제 있음) */
#else
    int r = SECURE_dma_m2m_start(src, dst, len);
#endif
    if (r == 0) { (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
    else        { g->dmaPending = 0u; }
#endif
    return r;
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
#if !defined(MEDIATOR_INSECURE) && !(MEDIATOR_ASM && !defined(BENCH_PHASE) && !defined(DMA_NOTIFY_BLXNS))
  /* Not with the stackless mediator: it is this task's only Secure entry (BENCH_PHASE also calls the
   * C bench entries, DMA_NOTIFY_BLXNS uses the C mediator) */
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);
#endif

  for (uint32_t i = 0; i < sizeof(s_dmaSrcA); i++) { s_dmaSrcA[i] = (uint8_t)i; }

  dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;

#if defined(BENCH_PHASE)
  Bench_Main();
#endif

  for( ;; )
  {
    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    (void)App_DmaM2M(g, s_dmaSrcA, s_dmaTaskMem[DMA_TASK_A].cluster.dst, DMA_BENCH_XFER_LEN);
    xSemaphoreGive(g->dmaMutex);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void vDmaTaskB( void *pvParameters )
{
  (void)pvParameters;
#if !defined(MEDIATOR_INSECURE) && !(MEDIATOR_ASM && !defined(BENCH_PHASE) && !defined(DMA_NOTIFY_BLXNS))
  /* Not with the stackless mediator: it is this task's only Secure entry (BENCH_PHASE also calls the
   * C bench entries, DMA_NOTIFY_BLXNS uses the C mediator) */
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);
#endif

  for (uint32_t i = 0; i < sizeof(s_dmaSrcB); i++) { s_dmaSrcB[i] = (uint8_t)i; }

  dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_B].globals.v;

  for( ;; )
  {
    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    (void)App_DmaM2M(g, s_dmaSrcB, s_dmaTaskMem[DMA_TASK_B].cluster.dst, DMA_BENCH_XFER_LEN);
    xSemaphoreGive(g->dmaMutex);

    vTaskDelay(pdMS_TO_TICKS(700));
  }
}

#if !defined(EVAL_MEM)   /* never read or written by code; not built for the memory evaluation */
/* Example data the task is allowed to see */
static uint8_t ucPrivateBuffer[256];
static const uint32_t ulLookupTable[128];
#endif

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
#if defined(MEDIATOR_INSECURE)
    /* Region 2 (baseline only): DMA1 ISR/IFCR + CH1 registers for Insecure_dma_m2m_start */
    { INSECURE_DMA_NSMPU_BASE, INSECURE_DMA_NSMPU_SIZE,
      portMPU_REGION_READ_WRITE | tskMPU_REGION_DEVICE_MEMORY | tskMPU_REGION_EXECUTE_NEVER }
#else
    /* Region 2: unused; the guard is never in any NS-MPU region */
    { 0, 0, 0 }
#endif
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
#if defined(MEDIATOR_INSECURE)
    { INSECURE_DMA_NSMPU_BASE, INSECURE_DMA_NSMPU_SIZE,
      portMPU_REGION_READ_WRITE | tskMPU_REGION_DEVICE_MEMORY | tskMPU_REGION_EXECUTE_NEVER }
#else
    { 0, 0, 0 }
#endif
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
#if !defined(EVAL_MEM)
    { ucPrivateBuffer, sizeof( ucPrivateBuffer ), portMPU_REGION_READ_WRITE },
    { ulLookupTable, sizeof( ulLookupTable ), portMPU_REGION_READ_ONLY },
#else
    { 0, 0, 0 },
    { 0, 0, 0 },
#endif
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

#if defined(DMA_NOTIFY_BLXNS)
  int initRes = SECURE_dma_m2m_init((void *)App_DmaDoneCallback);
  configASSERT(initRes == 0);
#elif defined(MEDIATOR_INSECURE)
  Insecure_dma_m2m_init();
#else
  int initRes = SECURE_dma_m2m_init((void *)&s_dmaStatusWord);
  configASSERT(initRes == 0);
  /* Secure set ITNS for the doorbell in init; only now can NS program its NVIC bits */
  NVIC_SetPriority(SECURE_DMA_DOORBELL_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_EnableIRQ(SECURE_DMA_DOORBELL_IRQn);
#endif

  TaskHandle_t hDmaTaskA = NULL, hDmaTaskB = NULL;
  xTaskCreateRestricted( &xDmaTaskAParams, &hDmaTaskA );
  xTaskCreateRestricted( &xDmaTaskBParams, &hDmaTaskB );
  s_dmaTaskMem[DMA_TASK_A].globals.v.hTask = hDmaTaskA;
  s_dmaTaskMem[DMA_TASK_B].globals.v.hTask = hDmaTaskB;
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
