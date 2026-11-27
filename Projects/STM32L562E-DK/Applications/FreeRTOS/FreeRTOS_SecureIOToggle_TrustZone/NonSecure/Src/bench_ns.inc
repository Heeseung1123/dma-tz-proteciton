/* 중재자 사이클 측정: NS 쪽 (app_freertos.c 에서 BENCH_PHASE 일 때만 include).
 *
 * 측정 구간 (8바이트 M2M, DMA IRQ 차단):
 *
 *   NS   T0 ---- SECURE_dma_m2m_start() 호출 ----------------------------- T5
 *   S          SG, prologue | T1 인자 확인, verify_inst, 레지스터, EN T4 | 복귀, FP 클리어
 *
 *   start 전체   = T5 - T0     (BENCH_TS 를 끈 빌드의 값을 쓴다)
 *   내부         = T4 - T1     (BENCH_TS 를 켠 빌드에서만 기록된다)
 *   verify_inst  = Secure 안에서 verify_inst 호출 전후를 잰 값
 *   빈 NSC       = 아무 일도 안 하는 SECURE_nop3 의 호출~복귀
 *
 * [요약] 중재자 사이클 구하는 법 (빌드 2번, 각 표의 열 번호는 BENCH_ROW(i)[열]):
 *   바깥(NSC 포함, NS 호출~복귀):
 *     빌드: BENCH_PHASE                (BENCH_TS 끔 -> 중재자 안에 계측 코드 없음)
 *     값  : START 열0 - NS_CALIB
 *   안쪽(NSC 제외, Secure 진입 직후 ~ DMA EN 직후):
 *     빌드: BENCH_PHASE + BENCH_TS     (중재자 안 T1/T4 기록 켬)
 *     값  : START 열1 - INNER_CALIB
 *     (이 빌드의 START 열0 은 T1/T4 코드 때문에 커지므로 쓰지 않는다)
 *   NSC 진입·복귀 비용 ≈ 바깥 - 안쪽  (교차 확인: EMPTY_NSC - NS_CALIB)
 *   검증만(verify_inst): VERIFY - VERIFY_CALIB  (어느 빌드 값이든 됨. 코드 배치 차이로 수 사이클 변동)
 *
 * 테스트마다 결과 표를 SRAM2 에 쓰고 bench_hit() 를 부른다. gdb 가 여기서 멈춰
 * 표를 CSV 로 옮긴다 (scripts/eval/run_bench.py).
 * SRAM2 를 쓰는 이유: .bss 를 늘리면 s_dmaTaskMem 주소가 움직여 Secure 의 하드코딩
 * 주소와 어긋난다. */
#ifndef BENCH_N
#define BENCH_N      1000u
#endif

#define BENCH_LEN    8u
#define BENCH_COLS   2u

typedef struct { volatile uint32_t magic, id, n; } bench_hdr_t;
#define BENCH_HDR    ((bench_hdr_t *)0x20030000u)
#define BENCH_ROW(i) ((volatile uint32_t *)(0x20030040u + (i) * BENCH_COLS * 4u))

/* DMA 소스. app_freertos.c 의 s_dmaSrcA(SRAM1 NS .bss)와는 다른 주소다.
 * 0x20038000 은 SRAM2 이며 SAU region 2(0x20018000-0x2003FFFF) NS, MPCBB2 전체 NS
 * (Secure/Src/main.c GTZC 설정) [코드]. verify_inst 는 dst 만 검사하고 중재자는 src 를
 * CPAR 에 쓰기만 하므로 측정 사이클(start/내부/verify)에는 영향이 없다 [코드].
 * 단 EN 이후 8B 전송이 Secure 에필로그와 겹치므로, SRAM1 소스(s_dmaSrcA)를 쓰면
 * 버스 경합이 수 사이클 달라질 수 있다 [추정]. */
#define BENCH_SRC    ((uint8_t *)0x20038000u)

#define BENCH_ID_NS_CALIB      1u   /* NS 판독 비용 */
#define BENCH_ID_EMPTY_NSC     2u   /* 빈 NSC 호출~복귀 */
#define BENCH_ID_START         3u   /* 열: start 전체, 내부(T4-T1) */
#define BENCH_ID_INNER_CALIB   4u   /* T1, T4 기록 비용 */
#define BENCH_ID_VERIFY        5u   /* verify_inst */
#define BENCH_ID_VERIFY_CALIB  6u   /* verify_inst 측정의 판독 비용 */
#define BENCH_ID_START_TABLE   7u   /* 열: start_table 전체, 내부(T4-T1). START 와 같은 방법 */
#define BENCH_ID_VERIFY_TABLE  8u   /* verify_table (VERIFY_CALIB 를 뺀다) */
#define BENCH_ID_START_ASM    13u   /* 열: start_asm 전체, 내부(T4-T1). START 와 같은 방법 (9-12 는 ISR 벤치) */
#define BENCH_ID_INNER_CALIB_ASM 14u /* start_asm 의 T1, T4 기록 비용 (movw/movt 형식) */
#define BENCH_ID_NSC_ASM0     16u   /* BENCH_DECOMP: 빈 NSC (asm) */
#define BENCH_ID_NSC_FPCLR    17u   /* BENCH_DECOMP: 빈 NSC (asm) + GCC FP 정리 */
#define BENCH_ID_START_M1     18u   /* BENCH_DECOMP: asm + C 의 DMA 레지스터 작업 */
#define BENCH_ID_START_M2     19u   /* BENCH_DECOMP: M1 + FP 정리 */
#define BENCH_ID_CTX_V0       20u   /* BENCH_DECOMP: 전환, context 없음 + asm NSC */
#define BENCH_ID_CTX_V1       21u   /* BENCH_DECOMP: 전환, context 있음 + asm NSC (Save/Load 만) */
#define BENCH_ID_CTX_V2       22u   /* BENCH_DECOMP: 전환, context 있음 + C NSC (Save/Load + FP 프레임) */
#define BENCH_ID_ALLOC        15u   /* BENCH_ALLOC 빌드: portALLOCATE_SECURE_CONTEXT 1회 (C 중재자 Task 의 1회 비용) */
#define BENCH_ID_END           0xFFu

extern int      SECURE_nop3(void *a, void *b, uint32_t c);
extern uint32_t SECURE_bench_inner(void);
extern uint32_t SECURE_bench_inner_calib(void);
extern uint32_t SECURE_bench_inner_calib_asm(void);
extern uint32_t SECURE_bench_verify(void *dst, uint32_t len);
extern uint32_t SECURE_bench_verify_calib(void);
extern uint32_t SECURE_bench_verify_table(void *dst, uint32_t len);
extern void     SECURE_bench_irq_mask(int mask);
extern void     SECURE_bench_cold(void);
extern int      SECURE_bench_dma_pending(void);

/* gdb hbreak 지점 */
__attribute__((noinline)) void bench_hit(void) { __asm volatile ("nop"); }

static void bench_table_done(uint32_t id)
{
    BENCH_HDR->magic = 0xBE4C0002u;
    BENCH_HDR->id = id;
    BENCH_HDR->n = BENCH_N;
    bench_hit();
}

/* cold 조건이면 매 회 측정 직전에 ICACHE 를 비운다.
 * SECURE_bench_cold: ICACHE_CR.CACHEINV -> SR.BUSYF 해제 대기 -> FCR.CBSYENDF 로 종료 플래그
 * 지움. 전체 무효화가 맞다 (RM0438 ICACHE 장, 절 번호 미확인).
 * - BENCH_ICACHE 없이 BENCH_COLD 만 켜면 캐시가 꺼져 있어 의미가 없다 (run_all.sh 는 둘 다 켬).
 * - cold = "중재자 경로가 cold" 라는 뜻이다. bench_cold 복귀 시 NS 루프 코드는 다시 채워지고,
 *   측정 구간 안의 NS long-branch veneer, SG, Secure 코드는 cold 상태로 남는다 [코드].
 * - ICACHE 는 S/NS 공용이므로 양쪽 라인이 모두 비워진다 [추정]. */
static inline void bench_before_iter(void)
{
#ifdef BENCH_COLD
    SECURE_bench_cold();
#endif
}

/* T5 - T0 을 재는 공통 경계 */
#define BENCH_MEASURE_NS(result, call)   \
    do {                                 \
        __DSB(); __ISB();                \
        uint32_t t0_ = DWT->CYCCNT;      \
        call;                            \
        __DSB();                         \
        (result) = DWT->CYCCNT - t0_;    \
    } while (0)


/* start 1회 측정. 측정 구간 동안 DMA IRQ(NVIC) 를 막아 전송 완료 ISR 과 NS 콜백이 T0-T5 에
 * 섞이지 않게 한다. 막지 않으면 기존 statMediator 828 처럼 ISR 비용이 섞인다.
 * 틱/태스크 선점은 막지 않으므로 max(일부 avg)에는 섞일 수 있다.
 * start / start_table 두 버전을 같은 매크로로 만들어 호출 대상 외에는 코드가 같게 한다
 * (함수 포인터 간접 호출을 쓰면 측정 구간 안 명령이 달라지므로 쓰지 않는다). */
/* B: the doorbell ISR only completes a task with dmaPending set. The bench still spins on
 * dmaDone (the notification count it leaves behind is never consumed). */
#if defined(DMA_NOTIFY_BLXNS)
#define BENCH_DMA_PENDING(g)   ((void)0)
#else
#define BENCH_DMA_PENDING(g)   ((g)->dmaPending = 1u)
#endif
#define BENCH_DEFINE_START_ONCE(name, nsc_fn)                                  \
static void name(uint8_t *dst, dma_task_globals_t *g, uint32_t *total)         \
{                                                                              \
    int r;                                                                     \
    g->dmaDone = 0;                                                            \
    BENCH_DMA_PENDING(g);                                                      \
    SECURE_bench_irq_mask(1);                                                  \
    BENCH_MEASURE_NS(*total, r = nsc_fn(BENCH_SRC, dst, BENCH_LEN));           \
    SECURE_bench_irq_mask(0);   /* 여기서 ISR 이 돌아 전송 완료를 처리한다 */   \
    configASSERT(r == 0);                                                      \
    while (!g->dmaDone) { }                                                    \
}
BENCH_DEFINE_START_ONCE(bench_start_once,       SECURE_dma_m2m_start)
BENCH_DEFINE_START_ONCE(bench_start_table_once, SECURE_dma_m2m_start_table)
#if MEDIATOR_ASM
BENCH_DEFINE_START_ONCE(bench_start_asm_once,   SECURE_dma_m2m_start_asm)
#endif

#if defined(BENCH_ISR_TS)
/* ---- DMA 완료 ISR 비용: Secure (CH1, 중재자 + doorbell) vs Insecure (CH2, NS ISR) ----
 * cmpISRoverhead.md §12. 구간: TC 가 이미 pending 인 상태에서 T0 -> cpsie -> HW 진입 -> ISR(들)
 * -> HW 복귀 -> t1. 전송 시간은 T0 전에 끝난다. 열0 = t1-T0, 열1 = Tk-T0 (BENCH_ISR_TS=k 의 스탬프,
 * 0 이면 스탬프 없음). 계측 비용은 같은 코드를 pending 없이 돌린 CAL 표의 열0 을 빼서 없앤다. */
#define BENCH_ID_ISR_CAL_S   9u    /* Secure 경로 코드, DMA 없음 (보정) */
#define BENCH_ID_ISR_S      10u
#define BENCH_ID_ISR_CAL_N  11u    /* Insecure 경로 코드, DMA 없음 (보정) */
#define BENCH_ID_ISR_N      12u

#define ISR_SLOT  ((volatile uint32_t *)0x20030010u)   /* [0] T0, [1] Tk (ISR_TS_ASM), [2] t1 */
/* T0/t1 use the same instructions as the ISR stamp, so their head/tail cancel against CAL */
#define TS_STAMP(addr)  __asm volatile ("ldr r12, =0xE0001004 \n ldr r12, [r12] \n ldr r0, =%c0 \n str r12, [r0] \n" \
                                        :: "i"(addr) : "r0", "r12", "memory")

#if BENCH_ISR_TS == 3
__attribute__((naked)) void DMA1_Channel2_IRQHandler(void) { __asm volatile (ISR_TS_ASM "b InsecureDma_IRQHandler \n"); }
#define DMA1_Channel2_IRQHandler  InsecureDma_IRQHandler
#endif

/* Insecure 기준선 ISR: Secure ISR 과 같은 레지스터 작업(원인 판정, EN=0, IFCR) + 같은 통지 본문.
 * s_busy, 상태 워드, doorbell pend 는 채널이 Secure 라서 생기는 일이므로 여기에는 없다. */
void DMA1_Channel2_IRQHandler(void)
{
    ISR_TS(4);   /* Tc: after prologue */
    uint32_t isr = DMA1->ISR;
    uint32_t status;

    if (isr & DMA_ISR_TEIF2)      { status = 1u; }
    else if (isr & DMA_ISR_TCIF2) { status = 0u; }
    else { DMA1->IFCR = DMA_IFCR_CGIF2; return; }

    DMA1_Channel2->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF2;
    App_DmaNotifyOwner(status);
    ISR_TS(5);   /* Td: before epilogue */
}

/* 표 하나 (워밍 1회 + BENCH_N 회). secure: CH1 중재자 경로 / 아니면 CH2. dma=0 이면 보정 */
static void bench_isr_table(uint32_t id, int secure, int dma)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;

    for (uint32_t i = 0; i <= BENCH_N; i++)          /* i == 0: warm-up, not stored */
    {
        g->dmaDone = 0u;
        ISR_SLOT[1] = 0u;
        if (secure) { SECURE_bench_irq_mask(1); } else { NVIC_DisableIRQ(DMA1_Channel2_IRQn); }

        if (dma)                                      /* run the transfer to TC, IRQ masked */
        {
            g->dmaPending = 1u;
            if (secure)
            {
                int r = SECURE_dma_m2m_start(BENCH_SRC, s_dmaTaskMem[DMA_TASK_A].cluster.dst, BENCH_LEN);
                configASSERT(r == 0);
                while (!SECURE_bench_dma_pending()) { }
            }
            else
            {
                DMA1_Channel2->CNDTR = BENCH_LEN;
                DMA1_Channel2->CCR  |= DMA_CCR_EN;
                while (!NVIC_GetPendingIRQ(DMA1_Channel2_IRQn)) { }
            }
        }

        __disable_irq();                              /* PRIMASK_NS also holds the Secure IRQ (PRIS=0) */
        if (secure) { SECURE_bench_irq_mask(0); }    /* ends with DSB+ISB */
        else        { NVIC_EnableIRQ(DMA1_Channel2_IRQn); __DSB(); __ISB(); }   /* same barrier */
        TS_STAMP(0x20030010u);                        /* T0 */
        __enable_irq();                               /* the pending TC interrupt is taken here */
        TS_STAMP(0x20030018u);                        /* t1 */

        configASSERT(!dma || g->dmaDone);
        (void)ulTaskNotifyTake(pdTRUE, 0);            /* drop the self-notification */
        if (i == 0u) { continue; }
        BENCH_ROW(i - 1u)[0] = ISR_SLOT[2] - ISR_SLOT[0];
        BENCH_ROW(i - 1u)[1] = ISR_SLOT[1] ? (ISR_SLOT[1] - ISR_SLOT[0]) : 0u;
    }
    bench_table_done(id);
}
#endif /* BENCH_ISR_TS */

#if defined(BENCH_ALLOC)
/* ---- Secure context 할당 비용 (C 중재자를 쓰는 DMA Task 가 시작할 때 한 번 내는 비용) ----
 * 구간: portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE) 호출 ~ 복귀 =
 *   SVC 진입 -> vPortSVCHandler_C -> SecureContext_AllocateContext (NSC, Secure pvPortMalloc 1040 B 포함)
 *   -> SecureContext_LoadContext (NSC) -> 예외 복귀  [코드: port.c portSVC_ALLOCATE_SECURE_CONTEXT].
 * AllocateContext 는 PSPLIM_S == 0 (로드된 context 없음) 일 때만 할당한다 [코드: secure_context.c].
 * 그래서 매 회 Secure context 가 없는 새 Task 를 만들어 그 안에서 한 번 재고, Task 는 스스로 지운다.
 * Bench Task 에서 helper 로 전환될 때 PendSV 의 SaveContext 가 PSPLIM_S = 0 으로 만든다 [코드].
 * 지운 Task 의 TCB 와 Secure context 는 IDLE 이 해제한다 (portCLEAN_UP_TCB) -> 다음 회 전에 vTaskDelay 로 IDLE 을 돌린다.
 * cold 조건은 지원하지 않는다: SECURE_bench_cold 는 C NSC 라 Secure 스택이 필요한데, helper 는 측정 전에 context 가 없다. */
static volatile uint32_t s_benchAlloc;
static void bench_alloc_helper(void *pv)
{
    (void)pv;
    uint32_t a;
    BENCH_MEASURE_NS(a, portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE));
    s_benchAlloc = a;
    vTaskDelete(NULL);
}

static void bench_alloc_main(void)
{
    uint32_t i, a;
    for (i = 0; i < BENCH_N; i++)
    {
        BENCH_MEASURE_NS(a, ;);
        BENCH_ROW(i)[0] = a;
    }
    bench_table_done(BENCH_ID_NS_CALIB);

    for (i = 0; i <= BENCH_N; i++)                   /* i == 0: warm-up, not stored */
    {
        s_benchAlloc = 0u;
        /* 우선순위가 Bench Task 보다 높아 생성 즉시 실행되고, 지워진 뒤 여기로 돌아온다 */
        BaseType_t ok = xTaskCreate(bench_alloc_helper, "alloc", 256, NULL,
                                    (tskIDLE_PRIORITY + 2) | portPRIVILEGE_BIT, NULL);
        configASSERT(ok == pdPASS);
        configASSERT(s_benchAlloc != 0u);
        vTaskDelay(2);                                /* IDLE 이 TCB 와 Secure context 를 해제 */
        if (i > 0u) { BENCH_ROW(i - 1u)[0] = s_benchAlloc; }
    }
    bench_table_done(BENCH_ID_ALLOC);
    bench_table_done(BENCH_ID_END);
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
#endif /* BENCH_ALLOC */

#if defined(BENCH_DECOMP) && MEDIATOR_ASM
/* ---- 분해 실험과 문맥 전환 비용 (cmpAsmMediator.md §6, §7) ---- */
extern int SECURE_bench_nsc_asm0(void *, void *, uint32_t);
extern int SECURE_bench_nsc_asm_fpclr(void *, void *, uint32_t);
extern int SECURE_bench_start_m1(void *, void *, uint32_t);
extern int SECURE_bench_start_m2(void *, void *, uint32_t);
BENCH_DEFINE_START_ONCE(bench_start_m1_once, SECURE_bench_start_m1)
BENCH_DEFINE_START_ONCE(bench_start_m2_once, SECURE_bench_start_m2)

/* 문맥 전환: 같은 우선순위의 두 Task 가 taskYIELD 로 번갈아 돈다. 구간 = 나가는 Task 의 t0 (yield 직전)
 * ~ 들어오는 Task 의 t1 (yield 복귀 직후). SVC/PendSV, Secure Save/Load (있으면), FP 저장/복원 (있으면),
 * 다음 Task 선택, MPU 영역 재설정이 들어간다. NSC 호출은 t0 앞에 있어 구간 밖이다.
 *   mode 0: Secure context 없음, 매 회 asm 중재자 호출 (asm 중재자 Task 와 같은 상태)
 *   mode 1: context 있음, asm 중재자 호출 (FPCA 가 서지 않음) -> mode 0 과의 차이 = Secure Save + Load NSC
 *   mode 0/1 의 호출은 SECURE_dma_m2m_start_asm(NULL, NULL, 0): len = 0 이라 바로 거부되고 DMA 는 시작하지 않는다.
 *   처음에는 SECURE_bench_nsc_asm0 (cpsid 없음) 을 썼는데, context 없는 Task 에서 그 함수 안으로 인터럽트가 들어오면
 *   PSP_S = 0 에 프레임이 쌓여 멈췄다 [실측: PSP_S 0xFFFFFFE0, EXC_RETURN 0xFFFFFFFD, cmpAsmMediator.md §7]. 
 *   mode 2: context 있음, C 빈 NSC (SECURE_nop3: vmov 로 FPCA = 1) -> C 중재자 Task 와 같은 상태
 * 처음 4 표본(Task 첫 시작 포함)은 버린다 (예전 f262d8d^ 하니스와 같음). */
typedef struct { volatile uint32_t valid, i, n, t0, done; } bench_cx_t;
static bench_cx_t s_cx;
static void bench_cx_task(void *p)
{
    uint32_t mode = (uint32_t)p;
    if (mode != 0u) { portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE); }
    for (;;)
    {
        uint32_t t1 = DWT->CYCCNT;
        if (s_cx.valid && s_cx.i < s_cx.n)
        {
            if (s_cx.i >= 4u) { BENCH_ROW(s_cx.i - 4u)[0] = t1 - s_cx.t0; }
            s_cx.i++;
        }
        if (s_cx.i >= s_cx.n) { s_cx.done++; vTaskSuspend(NULL); }
        s_cx.valid = 1u;
        if (mode == 2u) { (void)SECURE_nop3(NULL, NULL, 0); }
        else            { (void)SECURE_dma_m2m_start_asm(NULL, NULL, 0); }
        __DSB(); __ISB();
        s_cx.t0 = DWT->CYCCNT;
        taskYIELD();
    }
}

static void bench_cx_run(uint32_t id, uint32_t mode)
{
    TaskHandle_t h0 = NULL, h1 = NULL;
    s_cx.valid = 0u; s_cx.i = 0u; s_cx.n = BENCH_N + 4u; s_cx.done = 0u;
    vTaskSuspendAll();   /* 하나가 생성 즉시 돌면 짝이 없으므로 둘 다 만든 뒤 시작 */
    BaseType_t ok0 = xTaskCreate(bench_cx_task, "cx0", 256, (void *)mode, (tskIDLE_PRIORITY + 4) | portPRIVILEGE_BIT, &h0);
    BaseType_t ok1 = xTaskCreate(bench_cx_task, "cx1", 256, (void *)mode, (tskIDLE_PRIORITY + 4) | portPRIVILEGE_BIT, &h1);
    (void)xTaskResumeAll();
    configASSERT(ok0 == pdPASS && ok1 == pdPASS);
    configASSERT(s_cx.done == 2u);   /* 두 Task 가 모두 suspend 된 뒤에야 여기로 온다 */
    bench_table_done(id);
    vTaskDelete(h0); vTaskDelete(h1);
    vTaskDelay(5);       /* IDLE 이 TCB 와 Secure context 를 해제 */
}

#define BENCH_LOOP_NSC(id, call) do {                                   \
        (void)(call);                                                    \
        for (i = 0; i < BENCH_N; i++)                                    \
        { BENCH_MEASURE_NS(a, (void)(call)); BENCH_ROW(i)[0] = a; }      \
        bench_table_done(id); } while (0)
#define BENCH_LOOP_START(id, fn) do {                                   \
        fn(dst, g, &a);                                                  \
        for (i = 0; i < BENCH_N; i++)                                    \
        { fn(dst, g, &a); BENCH_ROW(i)[0] = a; }                         \
        bench_table_done(id); } while (0)

static void bench_decomp_main(void)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;
    uint8_t *dst = s_dmaTaskMem[DMA_TASK_A].cluster.dst;
    uint32_t i, a;

    for (i = 0; i < BENCH_N; i++) { BENCH_MEASURE_NS(a, ;); BENCH_ROW(i)[0] = a; }
    bench_table_done(BENCH_ID_NS_CALIB);

    BENCH_LOOP_NSC(BENCH_ID_EMPTY_NSC,  SECURE_nop3(NULL, NULL, 0));
    BENCH_LOOP_NSC(BENCH_ID_NSC_ASM0,   SECURE_bench_nsc_asm0(NULL, NULL, 0));
    BENCH_LOOP_NSC(BENCH_ID_NSC_FPCLR,  SECURE_bench_nsc_asm_fpclr(NULL, NULL, 0));
    BENCH_LOOP_START(BENCH_ID_START,     bench_start_once);
    BENCH_LOOP_START(BENCH_ID_START_ASM, bench_start_asm_once);
    BENCH_LOOP_START(BENCH_ID_START_M1,  bench_start_m1_once);
    BENCH_LOOP_START(BENCH_ID_START_M2,  bench_start_m2_once);

    bench_cx_run(BENCH_ID_CTX_V0, 0u);
    bench_cx_run(BENCH_ID_CTX_V1, 1u);
    bench_cx_run(BENCH_ID_CTX_V2, 2u);

    bench_table_done(BENCH_ID_END);
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
#endif /* BENCH_DECOMP */

/* DMA_Task_A 가 호출하는 진입점 */
static void Bench_Main(void)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;
    uint8_t *dst = s_dmaTaskMem[DMA_TASK_A].cluster.dst;
    uint32_t i, a;

    for (i = 0; i < BENCH_LEN; i++) { BENCH_SRC[i] = (uint8_t)i; }

#if defined(BENCH_ALLOC)
    bench_alloc_main();
#endif
#if defined(BENCH_DECOMP) && MEDIATOR_ASM
    bench_decomp_main();
#endif

#if defined(BENCH_ISR_TS)
    /* ISR 빌드는 ISR 표만 잰다. CH2 는 Secure main.c 가 SECM=0, ITNS=1 로 풀어 두었다.
     * 설정은 SECURE_dma_m2m_init 의 CH1 과 같다 (M2M, 8-bit, PL high, TCIE/TEIE). */
    DMA1_Channel2->CPAR  = (uint32_t)BENCH_SRC;
    DMA1_Channel2->CM0AR = (uint32_t)dst;
    DMA1_Channel2->CCR  |= DMA_CCR_MEM2MEM | DMA_CCR_MINC | DMA_CCR_PINC | DMA_CCR_PL_1
                         | DMA_CCR_TCIE | DMA_CCR_TEIE;
    NVIC_SetPriority(DMA1_Channel2_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);

    bench_isr_table(BENCH_ID_ISR_CAL_S, 1, 0);
    bench_isr_table(BENCH_ID_ISR_S,     1, 1);
    bench_isr_table(BENCH_ID_ISR_CAL_N, 0, 0);
    bench_isr_table(BENCH_ID_ISR_N,     0, 1);
    bench_table_done(BENCH_ID_END);
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
#endif

    /* warm 조건 예열 겸 첫 호출 비용 제외 */
    bench_start_once(dst, g, &a);

    /* NS_CALIB: 사이에 아무것도 없는 BENCH_MEASURE_NS 의 판독 비용 (NS 측 보정값) */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_MEASURE_NS(a, ;);
        BENCH_ROW(i)[0] = a;
    }
    bench_table_done(BENCH_ID_NS_CALIB);

    /* EMPTY_NSC: 아무 일도 안 하는 SECURE_nop3 의 호출~복귀. 중재자와 같은 경로
     * (NS veneer, SG + b.w, 16x vmov.f32 + FPSCR 클리어 + BXNS)를 거치므로 NSC 진입·복귀 비용이다.
     * 프롤로그만 다르다 (start: push {r7,lr}+sub #24, nop3: push {r7}+sub #20) [코드]. */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_MEASURE_NS(a, (void)SECURE_nop3(NULL, NULL, 0));
        BENCH_ROW(i)[0] = a;
    }
    bench_table_done(BENCH_ID_EMPTY_NSC);

    /* START: 열0 = T5-T0 (start 전체), 열1 = T4-T1 (내부: Secure 진입 후 ~ EN 직후).
     * BENCH_T1()/BENCH_T4() 는 BENCH_TS 빌드에서만 코드가 생긴다 (secure_nsc.c).
     *  - ts0 빌드: 중재자가 기본 빌드와 같다. 열0 이 깨끗한 start 전체 값. 열1 은 항상 0
     *    (SECURE_bench_inner 호출은 측정 구간 밖이라 결과에 영향 없음).
     *  - ts1 빌드: 훅이 명령 8개(DWT 주소 literal 로드 2, CYCCNT 판독 2, 저장 2 등)를 넣어
     *    열0 이 부풀려진다 (P1/110MHz/off 374 -> 423 [실측]). 열1 - INNER_CALIB 가 내부 값.
     * 즉 start 전체는 ts0 열0, 내부는 ts1 열1 - INNER_CALIB 를 쓴다. ts1 열0, ts0 열1 은 버린다.
     * 계측 없는 교차 확인: ts0 열0 - EMPTY_NSC ("차분"). */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        bench_start_once(dst, g, &a);
        BENCH_ROW(i)[0] = a;
        BENCH_ROW(i)[1] = SECURE_bench_inner();
    }
    bench_table_done(BENCH_ID_START);

    /* INNER_CALIB: T1, T4 기록 자체의 비용. bench_s.inc 에서 같은 코드
     * (ldr lit; ldr CYCCNT; str) 로 사이에 아무것도 없이 기록하므로 ts1 열1 에서 정확히 상쇄된다 [코드]. */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_ROW(i)[0] = SECURE_bench_inner_calib();
    }
    bench_table_done(BENCH_ID_INNER_CALIB);

    /* VERIFY: Secure 안에서 verify_inst 호출 전후를 잰 값. 인자 로드(ldr x2) + BL + 본문 + BX 포함 */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_ROW(i)[0] = SECURE_bench_verify(dst, BENCH_LEN);
    }
    bench_table_done(BENCH_ID_VERIFY);

    /* VERIFY_CALIB: 위 VERIFY 측정의 판독 비용 (DSB; ISB; 판독; DSB; 판독, 사이에 호출 없음).
     * 필요하다: Secure 코드는 -O0, 플래시 위치와 캐시 라인이 NS 와 달라 NS_CALIB 로 대신할 수 없다.
     * verify_inst 순수 값 = VERIFY - VERIFY_CALIB. */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_ROW(i)[0] = SECURE_bench_verify_calib();
    }
    bench_table_done(BENCH_ID_VERIFY_CALIB);

    /* ---- 테이블 방식 비교 대상 (secure_nsc.c SECURE_dma_m2m_start_table / verify_table) ----
     * NS-MPU 확인(TTAT x2)은 verify_inst 와 같고, S-MPU 태그(TTT x2) 대신 8항목 Flash 테이블 선형 탐색. dst 는 Task A Cluster = 항목 7 이므로 마지막 항목에서 적중
     * (최악 적중 경로) [코드]. 보정값은 S-MPU 경로와 공유한다:
     *   start_table 전체 = ts0 빌드 START_TABLE 열0 - NS_CALIB
     *   내부            = ts1 빌드 START_TABLE 열1 - INNER_CALIB
     *   verify_table    = VERIFY_TABLE - VERIFY_CALIB
     *     (SECURE_bench_verify_table 은 SECURE_bench_verify 와 같은 판독 순서 [코드]) */

    /* 예열: verify_table/start_table 코드와 g_dma_tbl 을 캐시에 올리고 첫 호출 비용 제외 */
    bench_start_table_once(dst, g, &a);

    /* START_TABLE: 열 의미는 START 와 같다 (start_table 에도 BENCH_T1/T4 가 있다) */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        bench_start_table_once(dst, g, &a);
        BENCH_ROW(i)[0] = a;
        BENCH_ROW(i)[1] = SECURE_bench_inner();
    }
    bench_table_done(BENCH_ID_START_TABLE);

    /* VERIFY_TABLE: Secure 안에서 verify_table 호출 전후를 잰 값 */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_ROW(i)[0] = SECURE_bench_verify_table(dst, BENCH_LEN);
    }
    bench_table_done(BENCH_ID_VERIFY_TABLE);

#if MEDIATOR_ASM
    /* ---- 스택 없는 중재자 (secure_nsc.c SECURE_dma_m2m_start_asm, gap H) ----
     * 같은 빌드에서 C 중재자(START)와 비교한다. 보정:
     *   start_asm 전체 = ts0 빌드 START_ASM 열0 - NS_CALIB
     *   내부          = ts1 빌드 START_ASM 열1 - INNER_CALIB_ASM
     *                   (T1 = cpsid 직후, T4 = EN 직후. C 쪽 T1 은 프롤로그 뒤라 구간 정의가 조금 다르다)
     * 이 빌드의 DMA Task 는 BENCH_PHASE 라서 Secure context 가 있다. asm 경로는 PSP_S 를 쓰지 않으므로
     * 사이클에는 영향이 없다 [아키텍처 도출]. */
    bench_start_asm_once(dst, g, &a);   /* 예열 */
    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        bench_start_asm_once(dst, g, &a);
        BENCH_ROW(i)[0] = a;
        BENCH_ROW(i)[1] = SECURE_bench_inner();
    }
    bench_table_done(BENCH_ID_START_ASM);

    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_ROW(i)[0] = SECURE_bench_inner_calib_asm();
    }
    bench_table_done(BENCH_ID_INNER_CALIB_ASM);
#endif

    bench_table_done(BENCH_ID_END);
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
