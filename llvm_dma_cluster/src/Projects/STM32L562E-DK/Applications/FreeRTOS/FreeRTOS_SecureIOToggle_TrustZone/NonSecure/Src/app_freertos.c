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

/* llvm_dma_cluster 판 평가 앱 (원본: project/NonSecure/Src/app_freertos.c).
 * 원본과 다른 점: DMA 버퍼를 "그냥 static 전역" 으로 선언한다. DmaCluster pass 가 DMA sink 호출
 * 인자를 역추적해 버퍼마다 소유 태스크를 정하고(.dma.t.<task> / 공유 .dma.s.<k>), 생성된 링커
 * 스크립트가 cluster(256B 정렬) + 바로 뒤 MPCBB guard 256B 로 배치한다. 주소 하드코딩 없음.
 *   - 태스크 전역(globals)과 DMA source 는 태스크 private 구조체 하나 = NS-MPU region 1 개
 *   - 태스크 전용 cluster / 공유 cluster 의 NS-MPU region 은 생성된 dma_regions.h 가 준다
 * 기존 원본의 MEDIATOR_INSECURE / DMA_NOTIFY_BLXNS / BENCH_PHASE 변형은 이 판에 없다.
 * Mediator = 원본과 같은 SVC 경로: svc #portSVC_DMA_M2M_START → NS SVC_Handler fast path (portasm.c)
 * → Handler mode 에서 SECURE_dma_m2m_start_svc (secure_nsc.c, asm). Secure 실행 중 예외 프레임은 MSP_S 에
 * 쌓이므로 DMA 태스크에 Secure context(Secure 스택)가 필요 없다. */

#include "secure_nsc.h"
#if defined(DMA_HAVE_REGIONS)          /* 2차 빌드: gen_layout.py 가 만든 헤더 */
#  include "dma_regions.h"
#else                                  /* 1차 빌드: 자리만 차지 (분석 결과에 영향 없음) */
#  define DMA_TASK_REGIONS_vDmaTaskA  { 0, 0, 0 }
#  define DMA_TASK_REGIONS_vDmaTaskB  { 0, 0, 0 }
#  define DMA_TASK_NREGIONS_vDmaTaskA 1
#  define DMA_TASK_NREGIONS_vDmaTaskB 1
#endif

typedef struct
{
    SemaphoreHandle_t     dmaMutex;    /* copy of s_dmaChannelMutex, set by vCreateAppTasks() */
    TaskHandle_t          hTask;       /* doorbell ISR notifies this task */
    volatile uint8_t      dmaPending;  /* 1 while this task's transfer is in flight */
    volatile uint8_t      dmaDone;
    volatile uint8_t      dmaStatus;   /* last result: 0 = TC, 1 = TEIF */
} dma_task_globals_t;

#define DMA_XFER_LEN        8u
#define DMA_ATTACK_N        6u

/* 태스크 private = globals + DMA source + 공격 시험 결과. DMA 목적지가 아니므로 cluster 밖. */
typedef struct
{
    dma_task_globals_t v;
    uint8_t            src[32];
    volatile int32_t   attack[DMA_ATTACK_N];  /* DMA_ATTACK_TESTS 결과 (GDB: p s_privA.attack) */
    volatile uint32_t  okCount;               /* 정상 전송 완료 횟수 */
} __attribute__((aligned(32))) dma_task_priv_t;

static dma_task_priv_t s_privA, s_privB;

/* DMA 목적지 버퍼: 평범한 static 전역. 배치는 pass + 생성 링커 스크립트가 정한다. */
static uint8_t s_dmaDstA[256];        /* Task A 만 → .dma.t.vDmaTaskA */
static uint8_t s_dmaDstB[256];        /* Task B 만 → .dma.t.vDmaTaskB */
static uint8_t s_dmaShared[64];       /* A, B 둘 다 → 공유 cluster .dma.s.0 */

/* ---- 빌드 시험용 변형 (tests/run_tests.sh 가 -D 로 켠다. 기본 빌드에는 없음) ---- */
#if defined(DMA_TEST_SHIFT)        /* 음성: NS 배치만 바뀐 "stale" 이미지 → Secure 의 guard 결합 불일치 (-7).
                                    * cluster 0 (공유 {A,B}, key = 소유자 목록) 을 키워 모든 guard 를 옮긴다. */
static uint8_t s_testShift[256] __attribute__((used, annotate("dma:shared:vDmaTaskA,vDmaTaskB")));
#endif
#if defined(DMA_TEST_BAD_INIT)     /* 음성: 초기값 있는 annotate 버퍼 → NOLOAD cluster 에 둘 수 없음 */
static uint8_t s_testBadInit[16] __attribute__((used, annotate("dma:vDmaTaskA"))) = { 1 };
#endif
#if defined(DMA_TEST_BAD_TASK)     /* 음성: annotate 의 태스크 이름이 없음 */
static uint8_t s_testBadTask[16] __attribute__((used, annotate("dma:vNoSuchTask")));
#endif
#if defined(DMA_TEST_TOO_MANY)     /* 음성: cluster 7 개 > S-MPU 태그 4 개 */
static uint8_t s_testMany1[8] __attribute__((used, annotate("dma:shared:m1")));
static uint8_t s_testMany2[8] __attribute__((used, annotate("dma:shared:m2")));
static uint8_t s_testMany3[8] __attribute__((used, annotate("dma:shared:m3")));
static uint8_t s_testMany4[8] __attribute__((used, annotate("dma:shared:m4")));
#endif
#if defined(DMA_TEST_OVERSIZE)     /* 음성: NS RAM(96K) 에 들어가지 않는 cluster → 링크 실패 */
static uint8_t s_testHuge[128 * 1024] __attribute__((used, annotate("dma:vDmaTaskA")));
#endif
#if defined(DMA_TEST_ANNOTATE_OK)  /* 양성: sink 사용처 없이 annotate 만으로 Task B cluster 에 */
static uint8_t s_testAnnB[48] __attribute__((used, annotate("dma:vDmaTaskB")));
#endif
#if defined(DMA_TEST_OVERRIDE)     /* 양성: Task A 가 쓰지만 annotate 는 Task B → owner = B 만 (경고) */
static uint8_t s_testOvr[32] __attribute__((annotate("dma:vDmaTaskB")));
#endif

#define DMA_NSMPU_REGIONS(t)  (1 + DMA_TASK_NREGIONS_##t)
_Static_assert(DMA_NSMPU_REGIONS(vDmaTaskA) <= portNUM_CONFIGURABLE_REGIONS, "Task A NS-MPU regions");
_Static_assert(DMA_NSMPU_REGIONS(vDmaTaskB) <= portNUM_CONFIGURABLE_REGIONS, "Task B NS-MPU regions");

/* Serializes the single Secure DMA channel between tasks (app-level only). */
static SemaphoreHandle_t s_dmaChannelMutex;

/* Secure DMA ISR writes the result here and pends SECURE_DMA_DOORBELL_IRQn. */
static volatile uint32_t s_dmaStatusWord;
static dma_task_priv_t * const s_privs[] = { &s_privA, &s_privB };

void DFSDM1_FLT3_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t status = s_dmaStatusWord;

    for (uint32_t i = 0; i < sizeof(s_privs) / sizeof(s_privs[0]); i++)
    {
        dma_task_globals_t *g = &s_privs[i]->v;
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

#if configDMA_MEDIATOR_SVC != 1
#error "llvm_dma_cluster: SVC mediator 경로 (configDMA_MEDIATOR_SVC = 1) 가 필요"
#endif
/* 원본 app_freertos.c 의 App_DmaStartSvc 그대로 (cmpAsmMediator.md §9). */
static inline int App_DmaStartSvc(void *src, void *dst, uint32_t len)
{
    register uint32_t r0 __asm("r0") = (uint32_t)src;
    register uint32_t r1 __asm("r1") = (uint32_t)dst;
    register uint32_t r2 __asm("r2") = len;
    __asm volatile ("svc %[n]" : "+r"(r0) : "r"(r1), "r"(r2), [n] "i"(portSVC_DMA_M2M_START) : "memory");
    return (int)r0;
}

/* One M2M transfer through the Secure mediator; returns after completion (0) or on reject (-1).
 * Caller holds the DMA mutex. (DMA sink: config/dma_config.json 의 App_DmaM2M args [2]) */
static int App_DmaM2M(dma_task_globals_t *g, void *src, void *dst, uint32_t len)
{
    g->dmaDone    = 0u;
    g->dmaPending = 1u;          /* set before start: the ISR may run right after EN */
    int r = App_DmaStartSvc(src, dst, len);            /* SVC -> Handler mode NSC */
    if (r == 0) { (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
    else        { g->dmaPending = 0u; }
    return r;
}

#if defined(DMA_ATTACK_TESTS)
/* 요청 시점 공격 시험 (논문 §4 (a)). Task A 가 시작할 때 한 번 실행, 결과를 s_privA.attack[] 에 남긴다.
 * 기대: [0] 0 (정상), [1]~[5] -1 (거부).
 * 이 함수의 sink 호출은 의도적으로 "남의 버퍼" 를 넘기므로 pass 분석에서 제외한다
 * (config/dma_config.json "exclude_callers"). noinline 필수: 인라인되면 태스크 함수의 호출로 보인다. */
__attribute__((noinline)) static void App_DmaAttackTests(dma_task_priv_t *p)
{
    dma_task_globals_t *g = &p->v;
    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    p->attack[0] = App_DmaM2M(g, p->src, s_dmaDstA, DMA_XFER_LEN);                 /* 정상 */
    p->attack[1] = App_DmaM2M(g, p->src, s_dmaDstB, DMA_XFER_LEN);                 /* (a-1) 남의 cluster: TTAT */
    p->attack[2] = App_DmaM2M(g, p->src, p, DMA_XFER_LEN);                         /* (a-2) 자기 globals: TTT */
    p->attack[3] = App_DmaM2M(g, p->src, s_dmaDstA, sizeof(s_dmaDstA) + 1u);       /* (a-3) 길이가 guard 로 */
    p->attack[4] = App_DmaM2M(g, p->src, s_dmaDstA + sizeof(s_dmaDstA) - 4u, 8u);  /* (a-4) 끝이 cluster 밖 */
    p->attack[5] = App_DmaM2M(g, p->src, (void *)0x30000000u, DMA_XFER_LEN);       /* (a-5) Secure 주소 */
    xSemaphoreGive(g->dmaMutex);
}
#endif

/* Task stacks (alignment required by the port) */
#define STACK_SIZE_WORDS 256
static StackType_t xDmaTaskAStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));
static StackType_t xDmaTaskBStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));
static StackType_t xPlainTaskStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));

/* DMA 태스크는 Secure context 를 만들지 않는다: mediator 는 SVC (Handler mode) 로 불려 MSP_S 를 쓴다. */
static void vDmaTaskA( void *pvParameters )
{
  (void)pvParameters;
  dma_task_priv_t *p = &s_privA;
  for (uint32_t i = 0; i < sizeof(p->src); i++) { p->src[i] = (uint8_t)i; }

#if defined(DMA_ATTACK_TESTS)
  App_DmaAttackTests(p);
#endif

  for( uint32_t n = 0;; n++ )
  {
    xSemaphoreTake(p->v.dmaMutex, portMAX_DELAY);
    if (App_DmaM2M(&p->v, p->src, s_dmaDstA, DMA_XFER_LEN) == 0) { p->okCount++; }
    if ((n & 3u) == 0u)
    {
      (void)App_DmaM2M(&p->v, p->src, s_dmaShared, DMA_XFER_LEN);
    }
#if defined(DMA_TEST_OVERRIDE)
    (void)App_DmaM2M(&p->v, p->src, s_testOvr, DMA_XFER_LEN);
#endif
    xSemaphoreGive(p->v.dmaMutex);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void vDmaTaskB( void *pvParameters )
{
  (void)pvParameters;
  dma_task_priv_t *p = &s_privB;
  for (uint32_t i = 0; i < sizeof(p->src); i++) { p->src[i] = (uint8_t)(0x80u + i); }

  for( uint32_t n = 0;; n++ )
  {
    xSemaphoreTake(p->v.dmaMutex, portMAX_DELAY);
    if (App_DmaM2M(&p->v, p->src, s_dmaDstB, DMA_XFER_LEN) == 0) { p->okCount++; }
    if ((n & 3u) == 2u)
    {
      (void)App_DmaM2M(&p->v, p->src, s_dmaShared + 32, DMA_XFER_LEN);
    }
#if defined(DMA_TEST_STACK_BUF)    /* 음성: 지역(스택) 배열을 DMA 목적지로 */
    {
      uint8_t local[8];
      (void)App_DmaM2M(&p->v, p->src, local, sizeof(local));
      p->okCount += local[0];
    }
#endif
    xSemaphoreGive(p->v.dmaMutex);

    vTaskDelay(pdMS_TO_TICKS(700));
  }
}

/* Example data the task is allowed to see */
static uint8_t ucPrivateBuffer[256] __attribute__((aligned(32)));
static const uint32_t ulLookupTable[128] __attribute__((aligned(32)));

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
  .uxPriority =  (tskIDLE_PRIORITY + 1 ),
  .puxStackBuffer = xDmaTaskAStack,
  .xRegions =
  {
    { &s_privA, sizeof( s_privA ), portMPU_REGION_READ_WRITE },   /* globals + src (cluster 밖) */
    DMA_TASK_REGIONS_vDmaTaskA                                     /* 전용 cluster + 공유 cluster */
  }
};

static const TaskParameters_t xDmaTaskBParams =
{
  .pvTaskCode = vDmaTaskB,
  .pcName = "DMA_Task_B",
  .usStackDepth = STACK_SIZE_WORDS,
  .pvParameters = NULL,
  .uxPriority =  (tskIDLE_PRIORITY + 1 ),
  .puxStackBuffer = xDmaTaskBStack,
  .xRegions =
  {
    { &s_privB, sizeof( s_privB ), portMPU_REGION_READ_WRITE },
    DMA_TASK_REGIONS_vDmaTaskB
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
    /* 원본은 portMPU_REGION_READ_ONLY(=6) → V11 port 는 tskMPU 플래그로 해석해 RW+XN 이 됨. 사본은 tskMPU_ 로 */
    { (void *)ulLookupTable, sizeof( ulLookupTable ), tskMPU_REGION_READ_ONLY },
    { 0, 0, 0 }
  }
};

void vCreateAppTasks( void )
{
  s_dmaChannelMutex = xSemaphoreCreateMutex();
  configASSERT(s_dmaChannelMutex != NULL);
  s_privA.v.dmaMutex = s_dmaChannelMutex;
  s_privB.v.dmaMutex = s_dmaChannelMutex;

  int initRes = SECURE_dma_m2m_init((void *)&s_dmaStatusWord);
  configASSERT(initRes == 0);
  (void)initRes;
  NVIC_SetPriority(SECURE_DMA_DOORBELL_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_EnableIRQ(SECURE_DMA_DOORBELL_IRQn);

  TaskHandle_t hDmaTaskA = NULL, hDmaTaskB = NULL;
  xTaskCreateRestricted( &xDmaTaskAParams, &hDmaTaskA );
  xTaskCreateRestricted( &xDmaTaskBParams, &hDmaTaskB );
  s_privA.v.hTask = hDmaTaskA;
  s_privB.v.hTask = hDmaTaskB;
  vGrantAccessToSemaphore( hDmaTaskA, s_dmaChannelMutex );
  vGrantAccessToSemaphore( hDmaTaskB, s_dmaChannelMutex );
  xTaskCreateRestricted( &xPlainTaskParams, NULL );
}

/* USER CODE END Application */
