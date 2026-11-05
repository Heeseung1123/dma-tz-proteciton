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
#define BENCH_SRC    ((uint8_t *)0x20038000u)

#define BENCH_ID_NS_CALIB      1u   /* NS 판독 비용 */
#define BENCH_ID_EMPTY_NSC     2u   /* 빈 NSC 호출~복귀 */
#define BENCH_ID_START         3u   /* 열: start 전체, 내부(T4-T1) */
#define BENCH_ID_INNER_CALIB   4u   /* T1, T4 기록 비용 */
#define BENCH_ID_VERIFY        5u   /* verify_inst */
#define BENCH_ID_VERIFY_CALIB  6u   /* verify_inst 측정의 판독 비용 */
#define BENCH_ID_END           0xFFu

extern int      SECURE_nop3(void *a, void *b, uint32_t c);
extern uint32_t SECURE_bench_inner(void);
extern uint32_t SECURE_bench_inner_calib(void);
extern uint32_t SECURE_bench_verify(void *dst, uint32_t len);
extern uint32_t SECURE_bench_verify_calib(void);
extern void     SECURE_bench_irq_mask(int mask);
extern void     SECURE_bench_cold(void);

/* gdb hbreak 지점 */
__attribute__((noinline)) void bench_hit(void) { __asm volatile ("nop"); }

static void bench_table_done(uint32_t id)
{
    BENCH_HDR->magic = 0xBE4C0002u;
    BENCH_HDR->id = id;
    BENCH_HDR->n = BENCH_N;
    bench_hit();
}

/* cold 조건이면 매 회 측정 직전에 ICACHE 를 비운다 */
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

static void bench_start_once(uint8_t *dst, dma_task_globals_t *g, uint32_t *total)
{
    int r;
    g->dmaDone = 0;
    SECURE_bench_irq_mask(1);
    BENCH_MEASURE_NS(*total, r = SECURE_dma_m2m_start(BENCH_SRC, dst, BENCH_LEN));
    SECURE_bench_irq_mask(0);         /* 여기서 ISR 이 돌아 전송 완료를 처리한다 */
    configASSERT(r == 0);
    while (!g->dmaDone) { }
}

/* DMA_Task_A 가 호출하는 진입점 */
static void Bench_Main(void)
{
    dma_task_globals_t *g = &s_dmaTaskMem[DMA_TASK_A].globals.v;
    uint8_t *dst = s_dmaTaskMem[DMA_TASK_A].cluster.dst;
    uint32_t i, a;

    for (i = 0; i < BENCH_LEN; i++) { BENCH_SRC[i] = (uint8_t)i; }

    /* warm 조건 예열 겸 첫 호출 비용 제외 */
    bench_start_once(dst, g, &a);

    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_MEASURE_NS(a, ;);
        BENCH_ROW(i)[0] = a;
    }
    bench_table_done(BENCH_ID_NS_CALIB);

    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_MEASURE_NS(a, (void)SECURE_nop3(NULL, NULL, 0));
        BENCH_ROW(i)[0] = a;
    }
    bench_table_done(BENCH_ID_EMPTY_NSC);

    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        bench_start_once(dst, g, &a);
        BENCH_ROW(i)[0] = a;
        BENCH_ROW(i)[1] = SECURE_bench_inner();
    }
    bench_table_done(BENCH_ID_START);

    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_ROW(i)[0] = SECURE_bench_inner_calib();
    }
    bench_table_done(BENCH_ID_INNER_CALIB);

    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_ROW(i)[0] = SECURE_bench_verify(dst, BENCH_LEN);
    }
    bench_table_done(BENCH_ID_VERIFY);

    for (i = 0; i < BENCH_N; i++)
    {
        bench_before_iter();
        BENCH_ROW(i)[0] = SECURE_bench_verify_calib();
    }
    bench_table_done(BENCH_ID_VERIFY_CALIB);

    bench_table_done(BENCH_ID_END);
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
