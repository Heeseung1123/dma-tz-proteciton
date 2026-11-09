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
 * 틱/태스크 선점은 막지 않으므로 max(일부 avg)에는 섞일 수 있다. */
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

    bench_table_done(BENCH_ID_END);
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
