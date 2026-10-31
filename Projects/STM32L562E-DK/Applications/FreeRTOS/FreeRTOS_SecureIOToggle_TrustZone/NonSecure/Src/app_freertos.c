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

/* ---------------------------------------------------------------------------
 * 평가용 Application (docs/gap-analysis.md 구현 순서 참고)
 *
 * 태스크 구성 (총 3개, 전부 FreeRTOS-MPU xTaskCreateRestricted 로 생성):
 *   - DMA_Task_A, DMA_Task_B : 각자 자신만의 M2M DMA 목적지 버퍼(dma_cluster_t.body)를
 *     가지며, 그 버퍼는 Secure 쪽에서 S-MPU 태그 + MPCBB 가드로 보호된다(§3.2, §3.3).
 *     두 태스크는 서로 다른 NS-MPU 사용자 영역을 가지므로, 한쪽이 다른 쪽의 버퍼를
 *     가리키도록 중재자를 호출해도 (1) NS-MPU 쓰기 권한이 없어 Secure_nsc.c 의
 *     verify_inst() 에서 거부되어야 한다.
 *   - Plain_Task : DMA 를 전혀 사용하지 않는 일반 FreeRTOS-MPU 태스크(기존 LED 예제
 *     그대로). Secure 쪽에 어떤 S-MPU 태그도 없으므로 비교군 역할을 한다.
 *
 * Cluster(가드 포함 메모리 배치)는 CLAUDE.md 지침에 따라 이번 라운드에서는 구현하지
 * 않는다. 대신 기존 SMPU_SetupProbeRegion() 패턴처럼, 아래 두 dma_cluster_t 를
 * 전역으로 선언해 build 한 뒤 .map 에서 실제 링크 주소를 읽어 Secure/Src/main.c 에
 * 하드코딩한다(자동 테이블/링커 섹션 없음, docs/impl-notes-2026-10-31.md 참고).
 * ------------------------------------------------------------------------- */

/* DMA 목적지(body) 256B + Secure 전용 가드 256B. 구조체 하나로 선언해 두 멤버가
 * 반드시 연속(= body 바로 위가 guard)이도록 강제한다. 이 struct 바깥의 다른 전역과
 * 섞이지 않도록 하는 것이 유일한 목적이며, 그 외 추가 메모리 섹션은 쓰지 않는다. */
typedef struct
{
    uint8_t body[256];   /* NS가 쓰는 DMA 목적지. Secure 가 S-MPU 영역으로 태그한다. */
    uint8_t guard[256];  /* Secure 전용 가드 블록. NS 코드는 절대 읽거나 쓰지 않는다. */
} __attribute__((aligned(256))) dma_cluster_t;

static dma_cluster_t s_dmaClusterA;
static dma_cluster_t s_dmaClusterB;

static uint8_t s_dmaSrcA[32] __attribute__((aligned(4)));
static uint8_t s_dmaSrcB[32] __attribute__((aligned(4)));

/* 하나뿐인 Secure DMA 채널(DMA1_Channel1, M2M)과 콜백 슬롯을 두 태스크가 번갈아
 * 쓰도록 직렬화한다. NS-MPU/S-MPU 보호 자체와는 무관한, 이 PoC 응용 코드 수준의
 * 동시성 처리일 뿐이다. */
static SemaphoreHandle_t s_dmaChannelMutex;
static volatile uint8_t  s_dmaDone;
static volatile uint32_t s_dmaCbStatus;

static void App_DmaDoneCallback(uint32_t status)
{
    s_dmaCbStatus = status;
    s_dmaDone = 1;
}

/* 사이클 측정 결과. GDB 에서 바로 조회할 수 있도록 전역 volatile 로 둔다.
 *   medStat  : SECURE_dma_m2m_start() 호출~복귀 1회 비용(= 중재자 검증 + 레지스터 설정).
 *   rtStat   : 같은 호출 시작부터 Secure ISR(TZIC/DMA 완료 인터럽트)을 거쳐
 *              NS 콜백이 실행되는 시점까지의 왕복 비용.
 * 논문 §4 와 동일하게 32회 반복, 측정 전 더미 전송 1회로 캐시를 채운다.
 * 검증이 거부되면(SECURE_dma_m2m_start 가 0이 아닌 값을 반환) 그 회차는 통계에서
 * 제외한다 — 만약 모든 회차가 거부되면 min 필드가 초기값 0xFFFFFFFF 로 남고 avg 는
 * 갱신되지 않으므로, docs/gap-analysis.md 에 적어둔 "bit21-of-TTT 상시 거부" 의심을
 * 보드에서 바로 확인할 수 있는 신호가 된다. */
typedef struct { uint32_t min, max, avg; } cycle_stat_t;

volatile cycle_stat_t g_statMediatorTaskA   = { 0xFFFFFFFFu, 0u, 0u };
volatile cycle_stat_t g_statRoundtripTaskA  = { 0xFFFFFFFFu, 0u, 0u };
volatile cycle_stat_t g_statMediatorTaskB   = { 0xFFFFFFFFu, 0u, 0u };
volatile cycle_stat_t g_statRoundtripTaskB  = { 0xFFFFFFFFu, 0u, 0u };

#define DMA_BENCH_XFER_LEN   8u
#define DMA_BENCH_ITERS      32u

static void DmaTask_RunBenchmark(uint8_t *srcBuf, uint8_t *dstBuf,
                                  volatile cycle_stat_t *medStat,
                                  volatile cycle_stat_t *rtStat)
{
    uint32_t medMin = 0xFFFFFFFFu, medMax = 0u, medSum = 0u;
    uint32_t rtMin  = 0xFFFFFFFFu, rtMax  = 0u, rtSum  = 0u;
    uint32_t okCount = 0u;

    /* 캐시 워밍 (논문 §4 "캐시가 채워진 상태"): 측정 전 더미 전송 1회 */
    xSemaphoreTake(s_dmaChannelMutex, portMAX_DELAY);
    s_dmaDone = 0;
    (void)SECURE_dma_m2m_start(srcBuf, dstBuf, DMA_BENCH_XFER_LEN);
    while (!s_dmaDone) { }
    xSemaphoreGive(s_dmaChannelMutex);

    for (uint32_t i = 0; i < DMA_BENCH_ITERS; i++)
    {
        xSemaphoreTake(s_dmaChannelMutex, portMAX_DELAY);
        s_dmaDone = 0;

        __DSB(); __ISB();
        uint32_t t0 = DWT->CYCCNT;
        int r = SECURE_dma_m2m_start(srcBuf, dstBuf, DMA_BENCH_XFER_LEN);
        __DSB();
        uint32_t t1 = DWT->CYCCNT;   /* 중재자 호출(검증 + 레지스터 설정) 1회 비용 */

        if (r != 0)
        {
            /* 채널이 사용 중이거나 검증이 거부됨: 이번 회차는 통계에서 제외 */
            xSemaphoreGive(s_dmaChannelMutex);
            continue;
        }

        while (!s_dmaDone) { }
        uint32_t t2 = DWT->CYCCNT;   /* 전송 + Secure ISR 까지 포함한 왕복 비용 */
        xSemaphoreGive(s_dmaChannelMutex);

        uint32_t dMed = t1 - t0;
        uint32_t dRt  = t2 - t0;

        if (dMed < medMin) medMin = dMed;
        if (dMed > medMax) medMax = dMed;
        medSum += dMed;

        if (dRt < rtMin) rtMin = dRt;
        if (dRt > rtMax) rtMax = dRt;
        rtSum += dRt;
        okCount++;
    }

    if (okCount > 0u)
    {
        medStat->min = medMin; medStat->max = medMax; medStat->avg = medSum / okCount;
        rtStat->min  = rtMin;  rtStat->max  = rtMax;  rtStat->avg  = rtSum  / okCount;
    }
}

#if 0
/* ---------------------------------------------------------------------------
 * 메모리 침범(공격) 시나리오 — CLAUDE.md §4 "공격 시나리오" 재현용.
 * 일단 주석 처리만 해 둔다 (지침: "메모리 침범 시나리오를 가정하기 위한 코드는
 * 일단 주석으로"). 실행하려면 해당 태스크 본문 안에서 이 블록의 호출부를 살려
 * 벤치마크 루프 이후에 호출하면 된다.
 *
 * (a) 요청 인자 조작: Task A 가 자신의 Cluster 가 아닌 Task B 의 버퍼를 가리키며
 *     중재자를 호출. NS-MPU 상 Task A 는 s_dmaClusterB 에 쓰기 권한이 없으므로
 *     verify_inst() 의 TTA 단계에서 거부되어야 한다 (SECURE_dma_m2m_start 가 -1 반환,
 *     실제 DMA 전송은 시작되지 않음).
 *
 * (b) 검증 통과 후 길이를 Cluster 너머로 변조: len 을 일부러 256(= body 크기) 보다
 *     크게 주어 end 주소(base+len-1)가 guard 안으로 들어가게 한다. 1차 방어선인
 *     request-time 검증(TTT 로 양 끝점 S-MPU 영역 비교)이 base 와 end 의 영역 번호가
 *     달라진 것을 보고 거부해야 한다. 만약(설계상 있으면 안 되지만) 1차 방어선이
 *     뚫리더라도, 실제 DMA 가 guard 블록에 처음 접근하는 순간 TZIC 인터럽트가 발생해
 *     GTZC_IRQHandler 가 채널을 중단시키는 2차 방어선이 있다 (§3.3). 두 경우 모두
 *     guard 너머(= 다음 Cluster/다른 태스크 데이터)는 변조되지 않아야 한다.
 */
static void AttackScenario_ArgTamper_TaskA(void)
{
    xSemaphoreTake(s_dmaChannelMutex, portMAX_DELAY);
    s_dmaDone = 0;
    int r = SECURE_dma_m2m_start(s_dmaSrcA, s_dmaClusterB.body, DMA_BENCH_XFER_LEN);
    xSemaphoreGive(s_dmaChannelMutex);
    configASSERT(r != 0);  /* 반드시 거부되어야 함 */
}

static void AttackScenario_OverflowAfterVerify_TaskA(void)
{
    const uint32_t oversizedLen = sizeof(s_dmaClusterA.body) + 32u; /* body 를 넘어 guard 까지 */

    xSemaphoreTake(s_dmaChannelMutex, portMAX_DELAY);
    s_dmaDone = 0;
    int r = SECURE_dma_m2m_start(s_dmaSrcA, s_dmaClusterA.body, oversizedLen);
    if (r == 0)
    {
        while (!s_dmaDone) { }   /* 2차 방어선(가드 블록+TZIC)이 채널을 중단시킬 때까지 대기 */
    }
    xSemaphoreGive(s_dmaChannelMutex);
}
#endif

/* Task 스택. 포트 요구에 따라 정렬 중요. */
#define STACK_SIZE_WORDS 256
static StackType_t xDmaTaskAStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));
static StackType_t xDmaTaskBStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));
static StackType_t xPlainTaskStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));

static void vDmaTaskA( void *pvParameters )
{
  (void)pvParameters;
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);

  for (uint32_t i = 0; i < sizeof(s_dmaSrcA); i++) { s_dmaSrcA[i] = (uint8_t)i; }

  DmaTask_RunBenchmark(s_dmaSrcA, s_dmaClusterA.body, &g_statMediatorTaskA, &g_statRoundtripTaskA);

  for( ;; )
  {
    xSemaphoreTake(s_dmaChannelMutex, portMAX_DELAY);
    s_dmaDone = 0;
    (void)SECURE_dma_m2m_start(s_dmaSrcA, s_dmaClusterA.body, DMA_BENCH_XFER_LEN);
    while (!s_dmaDone) { }
    xSemaphoreGive(s_dmaChannelMutex);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void vDmaTaskB( void *pvParameters )
{
  (void)pvParameters;
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);

  for (uint32_t i = 0; i < sizeof(s_dmaSrcB); i++) { s_dmaSrcB[i] = (uint8_t)i; }

  DmaTask_RunBenchmark(s_dmaSrcB, s_dmaClusterB.body, &g_statMediatorTaskB, &g_statRoundtripTaskB);

  for( ;; )
  {
    xSemaphoreTake(s_dmaChannelMutex, portMAX_DELAY);
    s_dmaDone = 0;
    (void)SECURE_dma_m2m_start(s_dmaSrcB, s_dmaClusterB.body, DMA_BENCH_XFER_LEN);
    while (!s_dmaDone) { }
    xSemaphoreGive(s_dmaChannelMutex);

    vTaskDelay(pdMS_TO_TICKS(700));
  }
}

/* Example data the task is allowed to see */
static uint8_t ucPrivateBuffer[256];
static const uint32_t ulLookupTable[128];

/* DMA 를 전혀 쓰지 않는 일반(비교군) 태스크. Secure 쪽에 어떤 S-MPU 태그도 없다. */
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
    /* Region 0: 이 태스크 전용 DMA 목적지(Cluster body). Secure 쪽 S-MPU 태그 대상. */
    { s_dmaClusterA.body, sizeof( s_dmaClusterA.body ), portMPU_REGION_READ_WRITE },
    /* Region 1: 이 태스크 전용 DMA 소스 (검증 대상 아님, Q4 참고) */
    { s_dmaSrcA, sizeof( s_dmaSrcA ), portMPU_REGION_READ_WRITE },
    /* Region 2: 미사용. s_dmaClusterA.guard 는 어떤 NS-MPU 영역에도 포함하지 않는다. */
    { 0, 0, 0 }
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
    { s_dmaClusterB.body, sizeof( s_dmaClusterB.body ), portMPU_REGION_READ_WRITE },
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

  SECURE_dma_m2m_init((void *)App_DmaDoneCallback);

  xTaskCreateRestricted( &xDmaTaskAParams, NULL );
  xTaskCreateRestricted( &xDmaTaskBParams, NULL );
  xTaskCreateRestricted( &xPlainTaskParams, NULL );
}

/* USER CODE END Application */
