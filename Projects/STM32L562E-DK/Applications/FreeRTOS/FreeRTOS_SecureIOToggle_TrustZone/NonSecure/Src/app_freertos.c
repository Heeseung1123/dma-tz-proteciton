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

/* ---------------------------------------------------------------------------
 * 평가용 Application (docs/gap-analysis.md 구현 순서 참고)
 *
 * 태스크 구성 (총 3개, 전부 FreeRTOS-MPU xTaskCreateRestricted 로 생성):
 *   - DMA_Task_A, DMA_Task_B : 각자 자신만의 M2M DMA 목적지 버퍼(Cluster)를 가지며,
 *     그 Cluster 는 Secure 쪽에서 S-MPU 태그 + MPCBB 가드로 보호된다(§3.2, §3.3).
 *     두 태스크는 서로 다른 NS-MPU 사용자 영역을 가지므로, 한쪽이 다른 쪽의 버퍼를
 *     가리키도록 중재자를 호출해도 (1) NS-MPU 쓰기 권한이 없어 Secure_nsc.c 의
 *     verify_inst() 에서 거부되어야 한다.
 *   - Plain_Task : DMA 를 전혀 사용하지 않는 일반 FreeRTOS-MPU 태스크(기존 LED 예제
 *     그대로). Secure 쪽에 어떤 S-MPU 태그도 없으므로 비교군 역할을 한다.
 *
 * 메모리 배치 (§3.1, [그림 2]). 자동 배치/추가 메모리 섹션 없이, 태스크마다 구조체
 * 하나(dma_task_mem_t)로 선언해 멤버 순서 = 주소 증가 순서를 강제한다.
 * (별개 static 변수의 선언 순서는 GCC 가 역순으로 배치하므로[실측: nm] 쓰지 않는다.)
 *
 *   주소 증가(= DMA 주소 증가 방향) ↓   (s_dmaTaskMem[] 배열, 태스크당 768B)
 *   +0x000 A.globals 256B : Task A 전역 데이터. DMA 대상 아님   ┐ Task A NS-MPU 사용자 영역 1개
 *   +0x100 A.cluster 256B : Task A DMA Cluster                    ┘ (512B)
 *                           -> Secure 가 이 구간만 S-MPU 영역(TTT 태그)으로 설정
 *   +0x200 A.guard   256B : MPCBB Secure 가드 블록. NS-MPU/S-MPU 어디에도 넣지 않고
 *                           NS 데이터를 두지 않는다. Cluster 바로 위(높은 주소)이므로
 *                           Cluster 를 넘는 DMA 쓰기/읽기는 가장 먼저 여기에 닿는다.
 *   +0x300 B.globals 256B : Task B 전역 데이터 = Task A 가드 블록 바로 위   ┐ Task B NS-MPU 영역
 *   +0x400 B.cluster 256B : Task B DMA Cluster                               ┘
 *   +0x500 B.guard   256B : Task B 가드 블록
 *
 * Cluster 가 256B 이고 guard 가 256B 경계여야 하므로 globals 도 256B 블록으로 둔다
 * (globals 32B 면 Cluster 와 guard 사이에 틈이 생겨 경계 초과를 바로 탐지하지 못함).
 *
 * Cluster 는 가드를 포함하지 않는다. guard 멤버는 가드 블록의 메모리를 예약하기 위한
 * 자리일 뿐이다. globals 와 Cluster 가 같은 NS-MPU 영역에 있으므로 NS-MPU 만으로는
 * DMA 가 globals 를 덮어쓰는 것을 막을 수 없고, 이를 S-MPU 태그(Cluster 에만 존재)로
 * 버퍼 단위에서 거부하는 것이 §3.2 의 요지다.
 *
 * Secure 쪽 주소는 기존 SMPU_SetupProbeRegion() 패턴처럼 .map/nm 으로 읽어
 * Secure/Src/main.c 에 하드코딩한다(자동 테이블/링커 섹션 없음,
 * docs/impl-notes-2026-10-31.md 참고).
 * ------------------------------------------------------------------------- */

/* 사이클 측정 결과 (태스크 전역 데이터의 예). GDB 에서 조회:
 *   p s_dmaTaskMem[DMA_TASK_A].globals.v
 *   medStat  : SECURE_dma_m2m_start() 호출~복귀 1회 비용(= 중재자 검증 + 레지스터 설정).
 *   rtStat   : 같은 호출 시작부터 Secure ISR(TZIC/DMA 완료 인터럽트)을 거쳐
 *              NS 콜백이 실행되는 시점까지의 왕복 비용.
 * 논문 §4 와 동일하게 32회 반복, 측정 전 더미 전송 1회로 캐시를 채운다.
 * 검증이 거부되면(SECURE_dma_m2m_start 가 0이 아닌 값을 반환) 그 회차는 통계에서
 * 제외한다 — 모든 회차가 거부되면 min 필드가 초기값 0xFFFFFFFF 로 남는다. */
typedef struct { uint32_t min, max, avg; } cycle_stat_t;

typedef struct
{
    volatile cycle_stat_t statMediator;
    volatile cycle_stat_t statRoundtrip;
    /* DMA 채널 동기화용 (비특권 태스크가 읽어야 하므로 자기 NS-MPU 영역 안에 둔다) */
    SemaphoreHandle_t     dmaMutex;    /* s_dmaChannelMutex 사본. vCreateAppTasks()(특권)가 채움 */
    volatile uint8_t      dmaDone;     /* App_DmaDoneCallback()(특권)이 1로 설정 */
    volatile uint8_t      dmaStatus;   /* 마지막 전송 결과: 0 = TC, 1 = TEIF */
} dma_task_globals_t;

#define DMA_TASK_GLOBALS_SIZE   256u   /* Cluster 를 256B 경계에 두기 위해 256B 블록 */
#define DMA_CLUSTER_SIZE        256u
#define DMA_GUARD_SIZE          256u   /* MPCBB 블록 1개 */

typedef struct
{
    union
    {
        dma_task_globals_t v;
        uint8_t            raw[DMA_TASK_GLOBALS_SIZE];
    } globals;                       /* 태스크 전역 데이터 (DMA 대상 아님). 이전 태스크 guard 바로 위 */
    struct
    {
        uint8_t dst[DMA_CLUSTER_SIZE];   /* M2M DMA 목적지 */
    } cluster;                       /* DMA Cluster: S-MPU 태그 대상 */
    uint8_t guard[DMA_GUARD_SIZE];   /* 가드 블록 예약. NS 코드는 절대 읽거나 쓰지 않는다 */
} __attribute__((aligned(256))) dma_task_mem_t;

_Static_assert(sizeof(dma_task_globals_t) <= DMA_TASK_GLOBALS_SIZE, "globals overflow");
_Static_assert(offsetof(dma_task_mem_t, cluster) % 32u == 0u, "cluster: S-MPU 32B 정렬");
_Static_assert(offsetof(dma_task_mem_t, guard) % 256u == 0u, "guard: MPCBB 256B 블록 정렬");
_Static_assert(sizeof(dma_task_mem_t) == offsetof(dma_task_mem_t, guard) + DMA_GUARD_SIZE,
               "guard 뒤 패딩 없음 -> 다음 태스크 globals 가 guard 바로 위");

/* NS-MPU 사용자 영역 하나 = globals + Cluster (guard 제외) */
#define DMA_TASK_NSMPU_SIZE     offsetof(dma_task_mem_t, guard)

/* 배열 하나로 선언해 [A.globals|A.cluster|A.guard|B.globals|B.cluster|B.guard] 순서를
 * 보장한다([그림 2]: Task1 data -> Cluster1 -> Guard -> Task2 data -> ...).
 * 별개 static 변수는 GCC 가 역순 배치하므로 쓰지 않는다. */
enum { DMA_TASK_A = 0, DMA_TASK_B = 1, DMA_TASK_NUM };
static dma_task_mem_t s_dmaTaskMem[DMA_TASK_NUM];

static uint8_t s_dmaSrcA[32] __attribute__((aligned(4)));
static uint8_t s_dmaSrcB[32] __attribute__((aligned(4)));

/* 하나뿐인 Secure DMA 채널(DMA1_Channel1, M2M)과 콜백 슬롯을 두 태스크가 번갈아
 * 쓰도록 직렬화한다. NS-MPU/S-MPU 보호 자체와는 무관한, 이 PoC 응용 코드 수준의
 * 동시성 처리일 뿐이다.
 *
 * 비특권 태스크는 사용자 영역 밖 SRAM 을 읽을 수 없으므로(PRIVDEFENA 배경 영역 =
 * 특권 전용), 이 핸들 자체는 특권 코드(vCreateAppTasks, 콜백)만 쓰고 태스크는 자기
 * globals 의 사본(dmaMutex)과 완료 플래그(dmaDone)만 쓴다. 추가 NSC 호출은 없다. */
static SemaphoreHandle_t s_dmaChannelMutex;

/* Secure DMA ISR 이 BLXNS 로 부르는 NS 콜백. NS Handler 모드 = 특권이므로 모든
 * 태스크의 globals 에 쓸 수 있다. 어느 태스크의 전송인지 알 수 없으므로 전부에 기록한다:
 * 채널은 mutex 로 한 번에 한 태스크만 쓰고, 각 태스크는 mutex 를 잡은 뒤 자기 dmaDone 을
 * 0 으로 지우고 시작하므로 소유자가 아닌 태스크의 플래그가 1 이 되어도 영향이 없다.
 * (Secure IRQ 우선순위에서 실행되므로 FreeRTOS API 는 호출하지 않는다.) */
static void App_DmaDoneCallback(uint32_t status)
{
    for (uint32_t i = 0; i < DMA_TASK_NUM; i++)
    {
        s_dmaTaskMem[i].globals.v.dmaStatus = (uint8_t)status;
        s_dmaTaskMem[i].globals.v.dmaDone   = 1u;
    }
}

#define DMA_BENCH_XFER_LEN   8u
#define DMA_BENCH_ITERS      32u

/* 사이클 측정(논문 §4, 표 2) 스위치.
 * DWT->CYCCNT 는 PPB(0xE0001004)에 있어 비특권 접근 시 BusFault 가 나므로
 * (NS-MPU 로 허용 불가) [아키텍처 도출], 측정할 때는 DMA 태스크를 특권
 * (portPRIVILEGE_BIT)으로 만든다. NS-MPU 영역 구성(xRegions)은 그대로이고,
 * verify_inst() 는 호출자 권한과 무관하게 TTAT(비특권 기준)로 조회하므로
 * 중재자 판정과 경로 비용은 비특권 호출과 같다 [아키텍처 도출].
 * 표 2 와 같은 NS->NS 왕복(SG/BXNS 상태 전환 포함) 기준이다.
 *   1: DMA 태스크 특권 + 벤치마크 실행
 *   0: DMA 태스크 비특권(격리 실증) + 벤치마크 생략 */
#ifndef DMA_BENCH_ENABLE
#define DMA_BENCH_ENABLE     1
#endif

#if DMA_BENCH_ENABLE
#define DMA_TASK_PRIV_BIT    portPRIVILEGE_BIT
#else
#define DMA_TASK_PRIV_BIT    0u
#endif

#if DMA_BENCH_ENABLE

static void DmaTask_RunBenchmark(uint8_t *srcBuf, uint8_t *dstBuf, dma_task_globals_t *g)
{
    volatile cycle_stat_t *medStat = &g->statMediator;
    volatile cycle_stat_t *rtStat  = &g->statRoundtrip;
    uint32_t medMin = 0xFFFFFFFFu, medMax = 0u, medSum = 0u;
    uint32_t rtMin  = 0xFFFFFFFFu, rtMax  = 0u, rtSum  = 0u;
    uint32_t okCount = 0u;

    medStat->min = 0xFFFFFFFFu; medStat->max = 0u; medStat->avg = 0u;
    rtStat->min  = 0xFFFFFFFFu; rtStat->max  = 0u; rtStat->avg  = 0u;

    /* 캐시 워밍 (논문 §4 "캐시가 채워진 상태"): 측정 전 더미 전송 1회 */
    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    g->dmaDone = 0;
    if (SECURE_dma_m2m_start(srcBuf, dstBuf, DMA_BENCH_XFER_LEN) == 0)
    {
        while (!g->dmaDone) { }   /* 거부 시 콜백이 오지 않으므로 대기하지 않는다 */
    }
    xSemaphoreGive(g->dmaMutex);

    for (uint32_t i = 0; i < DMA_BENCH_ITERS; i++)
    {
        xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
        g->dmaDone = 0;

        __DSB(); __ISB();
        uint32_t t0 = DWT->CYCCNT;
        int r = SECURE_dma_m2m_start(srcBuf, dstBuf, DMA_BENCH_XFER_LEN);
        __DSB();
        uint32_t t1 = DWT->CYCCNT;   /* 중재자 호출(검증 + 레지스터 설정) 1회 비용 */

        if (r != 0)
        {
            /* 채널이 사용 중이거나 검증이 거부됨: 이번 회차는 통계에서 제외 */
            xSemaphoreGive(g->dmaMutex);
            continue;
        }

        while (!g->dmaDone) { }
        uint32_t t2 = DWT->CYCCNT;   /* 전송 + Secure ISR 까지 포함한 왕복 비용 */
        xSemaphoreGive(g->dmaMutex);

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
#endif /* DMA_BENCH_ENABLE */

#if 0
/* ---------------------------------------------------------------------------
 * 메모리 침범(공격) 시나리오 — CLAUDE.md §4 "공격 시나리오" 재현용.
 * 일단 주석 처리만 해 둔다 (지침: "메모리 침범 시나리오를 가정하기 위한 코드는
 * 일단 주석으로"). 실행하려면 해당 태스크 본문 안에서 이 블록의 호출부를 살려
 * 벤치마크 루프 이후에 호출하면 된다.
 *
 * (a) 요청 인자 조작:
 *   (a-1) Task A 가 Task B 의 Cluster 를 가리키며 중재자를 호출. NS-MPU 상 Task A 는
 *         s_dmaTaskMem[DMA_TASK_B] 에 쓰기 권한이 없으므로 verify_inst() 의 TTAT 단계에서 거부.
 *   (a-2) Task A 가 자기 태스크 전역 데이터(globals)를 가리키며 중재자를 호출.
 *         globals 는 Cluster 와 같은 NS-MPU 영역이라 TTAT 단계는 통과하지만, S-MPU
 *         태그가 Cluster 에만 있으므로 TTT 단계에서 거부 — 논문이 말하는 "같은 MPU
 *         영역 안의 보호 객체를 향한 DMA" 차단.
 *   두 경우 모두 SECURE_dma_m2m_start 가 0 이 아닌 값을 반환하고 전송은 시작되지 않는다.
 *
 * (b) 검증 통과 후 길이를 Cluster 너머로 변조: len 을 일부러 Cluster 크기(256B) 보다
 *     크게 주어 end 주소(base+len-1)가 guard 안으로 들어가게 한다. 1차 방어선인
 *     request-time 검증(TTT 로 양 끝점 S-MPU 영역 비교)이 base 와 end 의 영역 번호가
 *     달라진 것을 보고 거부해야 한다. 만약(설계상 있으면 안 되지만) 1차 방어선이
 *     뚫리더라도, 실제 DMA 가 guard 블록에 처음 접근하는 순간 TZIC 인터럽트가 발생해
 *     GTZC_IRQHandler 가 채널을 중단시키는 2차 방어선이 있다 (§3.3). 두 경우 모두
 *     guard 너머(= 다음 Cluster/다른 태스크 데이터)는 변조되지 않아야 한다.
 */
static void AttackScenario_ArgTamper_TaskA(void)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;

    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    g->dmaDone = 0;
    int r = SECURE_dma_m2m_start(s_dmaSrcA, s_dmaTaskMem[DMA_TASK_B].cluster.dst, DMA_BENCH_XFER_LEN);
    configASSERT(r != 0);  /* (a-1) 반드시 거부되어야 함 */
    r = SECURE_dma_m2m_start(s_dmaSrcA, s_dmaTaskMem[DMA_TASK_A].globals.raw, DMA_BENCH_XFER_LEN);
    configASSERT(r != 0);  /* (a-2) 반드시 거부되어야 함 */
    xSemaphoreGive(g->dmaMutex);
}

static void AttackScenario_OverflowAfterVerify_TaskA(void)
{
    const uint32_t oversizedLen = sizeof(s_dmaTaskMem[DMA_TASK_A].cluster.dst) + 32u; /* Cluster 를 넘어 guard 까지 */

    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;

    xSemaphoreTake(g->dmaMutex, portMAX_DELAY);
    g->dmaDone = 0;
    int r = SECURE_dma_m2m_start(s_dmaSrcA, s_dmaTaskMem[DMA_TASK_A].cluster.dst, oversizedLen);
    if (r == 0)
    {
        while (!g->dmaDone) { }   /* 2차 방어선(가드 블록+TZIC)이 채널을 중단시킬 때까지 대기 */
    }
    xSemaphoreGive(g->dmaMutex);
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

  dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;

#if DMA_BENCH_ENABLE
  DmaTask_RunBenchmark(s_dmaSrcA, s_dmaTaskMem[DMA_TASK_A].cluster.dst, g);
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

#if DMA_BENCH_ENABLE
  DmaTask_RunBenchmark(s_dmaSrcB, s_dmaTaskMem[DMA_TASK_B].cluster.dst, g);
#endif

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
  .uxPriority =  (tskIDLE_PRIORITY + 1 ) | DMA_TASK_PRIV_BIT,   /* DMA_BENCH_ENABLE 참고 */
  .puxStackBuffer = xDmaTaskAStack,
  .xRegions =
  {
    /* Region 0: 태스크 전역 데이터 + DMA Cluster 를 영역 하나로 (guard 제외).
     *           S-MPU 태그는 Secure 쪽에서 이 중 Cluster 에만 건다. */
    { &s_dmaTaskMem[DMA_TASK_A], DMA_TASK_NSMPU_SIZE, portMPU_REGION_READ_WRITE },
    /* Region 1: 이 태스크 전용 DMA 소스 (테스트용, Cluster 밖, 검증 대상 아님) */
    { s_dmaSrcA, sizeof( s_dmaSrcA ), portMPU_REGION_READ_WRITE },
    /* Region 2: 미사용. s_dmaTaskMem[DMA_TASK_A].guard 는 어떤 NS-MPU 영역에도 포함하지 않는다. */
    { 0, 0, 0 }
  }
};

static const TaskParameters_t xDmaTaskBParams =
{
  .pvTaskCode = vDmaTaskB,
  .pcName = "DMA_Task_B",
  .usStackDepth = STACK_SIZE_WORDS,
  .pvParameters = NULL,
  .uxPriority =  (tskIDLE_PRIORITY + 1 ) | DMA_TASK_PRIV_BIT,   /* DMA_BENCH_ENABLE 참고 */
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

  /* 태스크가 읽을 수 있도록 각자의 globals 에 핸들 사본을 둔다 (특권, 스케줄러 시작 전) */
  s_dmaTaskMem[DMA_TASK_A].globals.v.dmaMutex = s_dmaChannelMutex;
  s_dmaTaskMem[DMA_TASK_B].globals.v.dmaMutex = s_dmaChannelMutex;

  SECURE_dma_m2m_init((void *)App_DmaDoneCallback);

  TaskHandle_t hDmaTaskA = NULL, hDmaTaskB = NULL;
//  xTaskCreateRestricted( &xDmaTaskAParams, &hDmaTaskA );
//  xTaskCreateRestricted( &xDmaTaskBParams, &hDmaTaskB );
  /* configENABLE_ACCESS_CONTROL_LIST = 1: 권한을 주지 않으면 비특권 태스크의
   * xSemaphoreTake/Give 가 MPU wrapper 에서 즉시 pdFAIL 로 끝난다. */
//  vGrantAccessToSemaphore( hDmaTaskA, s_dmaChannelMutex );
//  vGrantAccessToSemaphore( hDmaTaskB, s_dmaChannelMutex );
  xTaskCreateRestricted( &xPlainTaskParams, NULL );
}

/* USER CODE END Application */
