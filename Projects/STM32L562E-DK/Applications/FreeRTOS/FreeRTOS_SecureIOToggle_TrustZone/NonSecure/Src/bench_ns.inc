/* EVAL.md 평가 전용 NS 하니스. app_freertos.c 에서 BENCH_PHASE 일 때만 include 한다.
 * s_dmaTaskMem 주소를 움직이지 않도록 .bss 를 쓰지 않고 SRAM2 고정 주소만 쓴다.
 * 테스트 하나가 끝날 때마다 bench_done() 에서 gdb 가 멈춰 결과를 회수한다. */
#define BENCH_NS_BASE   0x20030000u
#define BENCH_NS_K      6u
#ifndef BENCH_N
#define BENCH_N         1000u
#endif

typedef struct { volatile uint32_t magic, id, n, k_ns, k_s, arg; } bench_hdr_t;
#define BENCH_HDR    ((bench_hdr_t *)BENCH_NS_BASE)
#define BENCH_ROW(i) ((volatile uint32_t *)(BENCH_NS_BASE + 0x40u + (i) * BENCH_NS_K * 4u))

/* SRAM2 뒤쪽: 공유 변수, 소스 버퍼, 벤치마크 태스크 스택 */
#define BENCH_SHARED ((bench_shared_t *)0x20038000u)
#define BENCH_SRC    ((uint8_t *)0x20038100u)
#define BENCH_STK(i) ((StackType_t *)(0x20039000u + (i) * 0x800u))
#define BENCH_STK_WORDS 256u   /* 프로젝트 태스크(STACK_SIZE_WORDS)와 동일 */

typedef struct {
    volatile uint32_t t0, valid, i, n;
    volatile uint32_t irqDone;
} bench_shared_t;

extern void SECURE_bench_reset(void);
extern void SECURE_bench_next(void);
extern void SECURE_bench_irq_mask(int mask);
extern void SECURE_bench_cold(void);
extern void SECURE_bench_env(void);
extern void SECURE_bench_calib(void);
extern void SECURE_bench_nop_s(int which);
extern int  SECURE_bench_verify(void *dst, uint32_t len);
extern int  SECURE_bench_verify_ttt2(void *dst, uint32_t len);

/* gdb hbreak 지점: 헤더를 다 쓴 뒤 호출된다 */
__attribute__((noinline)) void bench_hit(void) { __asm volatile ("nop"); }

__attribute__((noinline)) void bench_done(uint32_t id, uint32_t n, uint32_t k_ns, uint32_t k_s, uint32_t arg)
{
    BENCH_HDR->magic = 0xBE4C0001u;
    BENCH_HDR->id = id; BENCH_HDR->n = n; BENCH_HDR->k_ns = k_ns; BENCH_HDR->k_s = k_s; BENCH_HDR->arg = arg;
    bench_hit();
}

#define CC()  (DWT->CYCCNT)

static inline void bench_pre_iter(void)
{
#ifdef BENCH_COLD
    SECURE_bench_cold();
#endif
}

/* id=1 NS 연속 판독 보정 */
static void t_calib_ns(void)
{
    for (uint32_t i = 0; i < BENCH_N; i++)
    {
        bench_pre_iter();
        __DSB(); __ISB();
        uint32_t t0 = CC();
        __DSB();
        uint32_t t1 = CC();
        BENCH_ROW(i)[0] = t0; BENCH_ROW(i)[1] = t1;
    }
    bench_done(1, BENCH_N, 2, 0, 0);
}

/* id=2 Secure 연속 판독 보정 */
static void t_calib_s(void)
{
    SECURE_bench_reset();
    for (uint32_t i = 0; i < BENCH_N; i++) { bench_pre_iter(); SECURE_bench_calib(); SECURE_bench_next(); }
    bench_done(2, BENCH_N, 0, 2, 0);
}

#define BENCH_NOPFN_NS(name, n) \
    __attribute__((naked, noinline)) static void name(void) { __asm volatile (".rept " #n "\n nop\n .endr\n bx lr\n"); }
BENCH_NOPFN_NS(bench_ns_nop256, 256)
BENCH_NOPFN_NS(bench_ns_nop1024, 1024)
BENCH_NOPFN_NS(bench_ns_nop4096, 4096)

/* id=10+w NS NOP 블록, id=20+w Secure NOP 블록 (w: 0=256, 1=1024, 2=4096)
 * id=20+w 는 Secure 내부 경계(s0,s1) 와 NS 에서 본 경계(t0,t1) 를 같이 기록해
 * CYCCNT 가 Secure 구간을 세는지 교차 검증한다. */
static void t_nop(void)
{
    for (int w = 0; w < 3; w++)
    {
        for (uint32_t i = 0; i < 100u; i++)
        {
            bench_pre_iter();
            void (*fn)(void) = (w == 0) ? bench_ns_nop256 : ((w == 1) ? bench_ns_nop1024 : bench_ns_nop4096);
            __DSB(); __ISB();
            uint32_t t0 = CC();
            fn();
            __DSB();
            uint32_t t1 = CC();
            BENCH_ROW(i)[0] = t0; BENCH_ROW(i)[1] = t1;
        }
        bench_done(10u + (uint32_t)w, 100u, 2, 0, 0);

        SECURE_bench_reset();
        for (uint32_t i = 0; i < 100u; i++)
        {
            bench_pre_iter();
            __DSB(); __ISB();
            uint32_t t0 = CC();
            SECURE_bench_nop_s(w);
            __DSB();
            uint32_t t1 = CC();
            BENCH_ROW(i)[0] = t0; BENCH_ROW(i)[1] = t1;
            SECURE_bench_next();
        }
        bench_done(20u + (uint32_t)w, 100u, 2, 2, 0);
    }
}

/* id=3 빈 NSC 진입·복귀 */
static void t_empty_nsc(uint32_t n)
{
    extern int SECURE_nop3(void *a, void *b, uint32_t c);
    (void)SECURE_nop3(NULL, NULL, 0);
    for (uint32_t i = 0; i < n; i++)
    {
        bench_pre_iter();
        __DSB(); __ISB();
        uint32_t t0 = CC();
        (void)SECURE_nop3(NULL, NULL, 0);
        __DSB();
        uint32_t t1 = CC();
        BENCH_ROW(i)[0] = t0; BENCH_ROW(i)[1] = t1;
    }
    bench_done(3, n, 2, 0, 0);
}

/* id=4 중재자 전체 (기존 DmaTask_RunBenchmark 와 같은 경계)
 * NS 열: t0, t5(복귀), t6(완료 감지), tcb(NS 콜백, BENCH_TS 일 때만)
 * S  열: [1]진입 [2]verify 후 [3]EN 전 [4]EN 후 [5]ISR 진입 [6]콜백 전 [7]콜백 후 */
static void t_mediator(uint32_t n, uint32_t len, uint32_t id)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;
    uint8_t *dst = s_dmaTaskMem[DMA_TASK_A].cluster.dst;

    /* 예열 전송도 타임스탬프를 쓰므로 먼저 행 포인터를 되돌린다 (배열 끝 초과 방지) */
    SECURE_bench_reset();
    g->dmaDone = 0;
    if (SECURE_dma_m2m_start(BENCH_SRC, dst, len) == 0) { while (!g->dmaDone) { } }

    SECURE_bench_reset();
    for (uint32_t i = 0; i < n; i++)
    {
        bench_pre_iter();
        g->dmaDone = 0;
        __DSB(); __ISB();
        uint32_t t0 = CC();
        int r = SECURE_dma_m2m_start(BENCH_SRC, dst, len);
        __DSB();
        uint32_t t5 = CC();
        if (r == 0) { while (!g->dmaDone) { } }
        uint32_t t6 = CC();
        BENCH_ROW(i)[0] = t0; BENCH_ROW(i)[1] = t5; BENCH_ROW(i)[2] = t6;
        BENCH_ROW(i)[3] = BENCH_NS_CB_VALUE();
        BENCH_ROW(i)[4] = (uint32_t)r;
        if (i + 1u < n) { SECURE_bench_next(); }
    }
    bench_done(id, n, 5, 8, len);
}

/* id=5 중재자, DMA IRQ 마스크: 전송·ISR 이 복귀 경계에 섞이지 않은 값 */
static void t_mediator_masked(uint32_t n, uint32_t len)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;
    uint8_t *dst = s_dmaTaskMem[DMA_TASK_A].cluster.dst;

    SECURE_bench_reset();
    for (uint32_t i = 0; i < n; i++)
    {
        bench_pre_iter();
        g->dmaDone = 0;
        SECURE_bench_irq_mask(1);
        __DSB(); __ISB();
        uint32_t t0 = CC();
        int r = SECURE_dma_m2m_start(BENCH_SRC, dst, len);
        __DSB();
        uint32_t t5 = CC();
        SECURE_bench_irq_mask(0);
        if (r == 0) { while (!g->dmaDone) { } }
        BENCH_ROW(i)[0] = t0; BENCH_ROW(i)[1] = t5; BENCH_ROW(i)[4] = (uint32_t)r;
        if (i + 1u < n) { SECURE_bench_next(); }
    }
    bench_done(5, n, 5, 8, len);
}

/* id=6 verify_inst 단독, id=7 TTT×2 사본 단독 */
static void t_verify(uint32_t n)
{
    uint8_t *dst = s_dmaTaskMem[DMA_TASK_A].cluster.dst;
    SECURE_bench_reset();
    for (uint32_t i = 0; i < n; i++)
    {
        bench_pre_iter();
        BENCH_ROW(i)[0] = (uint32_t)SECURE_bench_verify(dst, 8u);
        SECURE_bench_next();
    }
    bench_done(6, n, 1, 2, 8);

    SECURE_bench_reset();
    for (uint32_t i = 0; i < n; i++)
    {
        bench_pre_iter();
        BENCH_ROW(i)[0] = (uint32_t)SECURE_bench_verify_ttt2(dst, 8u);
        SECURE_bench_next();
    }
    bench_done(7, n, 1, 2, 8);
}

/* ---- Phase 4: context switch / task creation ---- */
static void ctx_task(void *p)
{
    bench_shared_t *sh = BENCH_SHARED;
    if ((uint32_t)p != 0u) { portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE); }
    for (;;)
    {
        uint32_t t1 = CC();
        if (sh->valid != 0u)
        {
            uint32_t i = sh->i;
            if (i < sh->n) { BENCH_ROW(i)[0] = sh->t0; BENCH_ROW(i)[1] = t1; sh->i = i + 1u; }
        }
        if (sh->i >= sh->n) { vTaskSuspend(NULL); }
        sh->valid = 1u;
        __DSB(); __ISB();
        sh->t0 = CC();
        taskYIELD();
    }
}

static void bench_params(TaskParameters_t *tp, TaskFunction_t fn, const char *name, void *arg, UBaseType_t prio, int stk)
{
    uint8_t *raw = (uint8_t *)tp;
    for (uint32_t i = 0; i < sizeof(*tp); i++) { raw[i] = 0u; }
    tp->pvTaskCode = fn;
    tp->pcName = name;
    tp->usStackDepth = BENCH_STK_WORDS;
    tp->pvParameters = arg;
    tp->uxPriority = prio | portPRIVILEGE_BIT;
    tp->puxStackBuffer = BENCH_STK(stk);
}

/* id=30 secure context 미할당, id=31 할당. 처음 4샘플은 분석에서 버린다. */
static void t_ctxsw(uint32_t n, uint32_t withSecure)
{
    bench_shared_t *sh = BENCH_SHARED;
    TaskParameters_t tp;
    TaskHandle_t h0 = NULL, h1 = NULL;

    sh->valid = 0u; sh->i = 0u; sh->n = n + 4u;
    /* 생성 즉시 선점되면 짝 태스크가 만들어지지 않으므로 스케줄러를 멈추고 둘 다 만든다 */
    vTaskSuspendAll();
    bench_params(&tp, ctx_task, "cx0", (void *)withSecure, tskIDLE_PRIORITY + 4u, 0);
    xTaskCreateRestricted(&tp, &h0);
    bench_params(&tp, ctx_task, "cx1", (void *)withSecure, tskIDLE_PRIORITY + 4u, 1);
    xTaskCreateRestricted(&tp, &h1);
    (void)xTaskResumeAll();
    /* 두 태스크가 모두 suspend 된 뒤에야 이 낮은 우선순위 태스크가 돌아온다 */
    bench_done(30u + withSecure, n + 4u, 2, 0, 0);
    vTaskDelete(h0);
    vTaskDelete(h1);
}

static void idle_body(void *p) { (void)p; for (;;) { vTaskSuspend(NULL); } }

/* id=32(256워드), id=34(128워드) xTaskCreateRestricted 비용
 * (생성된 태스크는 낮은 우선순위라 실행되지 않음). 스택 크기 의존성을 보려고 둘 다 잰다. */
static void t_create(uint32_t n, uint32_t words, uint32_t id)
{
    TaskParameters_t tp;
    bench_params(&tp, idle_body, "cr", NULL, tskIDLE_PRIORITY, 2);
    tp.usStackDepth = (configSTACK_DEPTH_TYPE)words;
    for (uint32_t i = 0; i < n; i++)
    {
        TaskHandle_t h = NULL;
        __DSB(); __ISB();
        uint32_t t0 = CC();
        xTaskCreateRestricted(&tp, &h);
        __DSB();
        uint32_t t1 = CC();
        BENCH_ROW(i)[0] = t0; BENCH_ROW(i)[1] = t1;
        vTaskDelete(h);
    }
    bench_done(id, n, 2, 0, words);
}

static void alloc_body(void *p)
{
    (void)p;
    __DSB(); __ISB();
    uint32_t t0 = CC();
    portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);
    __DSB();
    uint32_t t1 = CC();
    bench_shared_t *sh = BENCH_SHARED;
    BENCH_ROW(sh->i)[0] = t0; BENCH_ROW(sh->i)[1] = t1;
    vTaskSuspend(NULL);
    for (;;) { }
}

/* id=33 portALLOCATE_SECURE_CONTEXT 비용 */
static void t_alloc(uint32_t n)
{
    bench_shared_t *sh = BENCH_SHARED;
    TaskParameters_t tp;
    bench_params(&tp, alloc_body, "al", NULL, tskIDLE_PRIORITY + 4u, 2);
    for (uint32_t i = 0; i < n; i++)
    {
        TaskHandle_t h = NULL;
        sh->i = i;
        xTaskCreateRestricted(&tp, &h);
        vTaskDelete(h);
    }
    bench_done(33, n, 2, 0, 0);
}

/* ---- Phase 5: M2M 길이별 방식 비교 ---- */
#if defined(BENCH_INSECURE_DMA)
void DMA1_Channel2_IRQHandler(void)
{
    DMA1_Channel2->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF2;
    BENCH_SHARED->irqDone = 1u;
    __DSB();
}

static uint32_t insecure_once(uint8_t *dst, uint32_t len)
{
    BENCH_SHARED->irqDone = 0u;
    DMA1_Channel2->CCR = 0u;
    DMA1_Channel2->CPAR = (uint32_t)BENCH_SRC;
    DMA1_Channel2->CM0AR = (uint32_t)dst;
    DMA1_Channel2->CNDTR = len;
    DMA1->IFCR = DMA_IFCR_CGIF2;
    DMA1_Channel2->CCR = DMA_CCR_MEM2MEM | DMA_CCR_MINC | DMA_CCR_PINC | DMA_CCR_PL_1
                       | DMA_CCR_TCIE | DMA_CCR_TEIE | DMA_CCR_EN;
    while (BENCH_SHARED->irqDone == 0u) { }
    return 0u;
}
#endif

static const uint16_t s_benchLens[] = { 1, 2, 4, 8, 16, 25, 32, 50, 64, 75, 100, 128, 200, 256 };

/* id=50+k: arg=길이. NS 열: pool t0,t1 / insecure t0,t1 / 제안 t0,t6 */
static void t_macro(void)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;
    uint8_t *dst = s_dmaTaskMem[DMA_TASK_A].cluster.dst;

#if defined(BENCH_INSECURE_DMA)
    NVIC_SetPriority(DMA1_Channel2_IRQn, 5);
    NVIC_EnableIRQ(DMA1_Channel2_IRQn);
#endif

    for (uint32_t k = 0; k < sizeof(s_benchLens) / sizeof(s_benchLens[0]); k++)
    {
        uint32_t len = s_benchLens[k];
        for (int warm = 1; warm >= 0; warm--)
        {
            for (uint32_t i = 0; i < 10u; i++)
            {
                volatile uint8_t *d = dst;
                const volatile uint8_t *s = BENCH_SRC;
                __DSB(); __ISB();
                uint32_t a0 = CC();
                for (uint32_t b = 0; b < len; b++) { d[b] = s[b]; }
                __DSB();
                uint32_t a1 = CC();

                __DSB(); __ISB();
                uint32_t b0 = CC();
#if defined(BENCH_INSECURE_DMA)
                (void)insecure_once(dst, len);
#endif
                uint32_t b1 = CC();

                g->dmaDone = 0;
                __DSB(); __ISB();
                uint32_t c0 = CC();
                int r = SECURE_dma_m2m_start(BENCH_SRC, dst, len);
                if (r == 0) { while (!g->dmaDone) { } }
                uint32_t c1 = CC();

                if (warm == 0)
                {
                    BENCH_ROW(i)[0] = a0; BENCH_ROW(i)[1] = a1;
                    BENCH_ROW(i)[2] = b0; BENCH_ROW(i)[3] = b1;
                    BENCH_ROW(i)[4] = c0; BENCH_ROW(i)[5] = c1;
                }
            }
        }
        bench_done(50u + k, 10u, 6, 0, len);
    }
}

/* DMA_Task_A 가 호출하는 진입점 */
static void Bench_Main(void)
{
    for (uint32_t i = 0; i < 256u; i++) { BENCH_SRC[i] = (uint8_t)i; }
    SECURE_bench_env();
#if BENCH_PHASE == 1
    t_calib_ns();
    t_calib_s();
    t_nop();
#elif BENCH_PHASE == 2
    t_calib_ns();
    t_calib_s();
    t_empty_nsc(BENCH_N);
    t_mediator(BENCH_N, 8u, 4u);
    t_mediator_masked(BENCH_N, 8u);
    t_verify(BENCH_N);
#elif BENCH_PHASE == 4
    t_ctxsw(BENCH_N, 0u);
    t_ctxsw(BENCH_N, 1u);
    t_create(BENCH_N, 256u, 32u);
    t_create(BENCH_N, 128u, 34u);
    t_alloc(BENCH_N);
#elif BENCH_PHASE == 5
    t_macro();
#elif BENCH_PHASE == 100
    /* 디버거 비연결 검증: 멈출 곳 없이 실행하고 RAM 에 남긴다 */
    t_nop();
#endif
    bench_done(0xFFu, 0, 0, 0, 0);
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
