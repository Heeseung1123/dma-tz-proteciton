/* EVAL.md 평가 전용 Secure 계측. secure_nsc.c 끝에서 BENCH_PHASE 일 때만 include 한다.
 * 기존 보안 로직(verify_inst, S-MPU/MPCBB, TZIC)은 호출만 하고 바꾸지 않는다. */
#define BENCH_S_K     8u
#ifndef BENCH_N
#define BENCH_N       1000u
#endif

volatile uint32_t g_bT[BENCH_N * BENCH_S_K];
volatile uint32_t *g_bCur = g_bT;
volatile uint32_t g_bEnv[4];     /* DWT_CTRL, DAUTHSTATUS, DHCSR, ICACHE_CR */
volatile uint32_t g_bBoot[10];   /* main.c 부팅 타임스탬프 */

#define BENCH_CC()    (DWT->CYCCNT)

secureportNON_SECURE_CALLABLE void SECURE_bench_reset(void) { g_bCur = g_bT; }
secureportNON_SECURE_CALLABLE void SECURE_bench_next(void)  { g_bCur += BENCH_S_K; }

/* 전송·복귀 중첩(가설 e) 분리: DMA IRQ 를 막아 ISR 이 측정 구간 밖에서 돌게 한다. */
secureportNON_SECURE_CALLABLE void SECURE_bench_irq_mask(int mask)
{
    if (mask) { NVIC_DisableIRQ(DMA_CH_IRQn); }
    else      { NVIC_EnableIRQ(DMA_CH_IRQn); }
    __DSB(); __ISB();
}

/* cold 조건: ICACHE 전체 무효화. 무효화 완료까지 대기한다. */
secureportNON_SECURE_CALLABLE void SECURE_bench_cold(void)
{
    ICACHE->CR |= ICACHE_CR_CACHEINV;
    while (ICACHE->SR & ICACHE_SR_BUSYF) { }
    ICACHE->FCR = ICACHE_FCR_CBSYENDF;
    __DSB(); __ISB();
}

secureportNON_SECURE_CALLABLE void SECURE_bench_env(void)
{
    g_bEnv[0] = DWT->CTRL;
    g_bEnv[1] = *(volatile uint32_t *)0xE000EFB8u;
    g_bEnv[2] = *(volatile uint32_t *)0xE000EDF0u;
    g_bEnv[3] = ICACHE->CR;
}

/* Secure 쪽 연속 판독 보정값: NS 경계와 같은 DSB/ISB 순서 */
secureportNON_SECURE_CALLABLE void SECURE_bench_calib(void)
{
    __DSB(); __ISB();
    uint32_t t0 = BENCH_CC();
    __DSB();
    uint32_t t1 = BENCH_CC();
    g_bCur[0] = t0; g_bCur[1] = t1;
}

/* NOP 블록은 리터럴 풀 범위 때문에 별도 함수로 둔다. BL/BX 는 길이와 무관한 상수 비용이다. */
#define BENCH_NOPFN(name, n) \
    __attribute__((naked, noinline)) static void name(void) { __asm volatile (".rept " #n "\n nop\n .endr\n bx lr\n"); }
BENCH_NOPFN(bench_s_nop256, 256)
BENCH_NOPFN(bench_s_nop1024, 1024)
BENCH_NOPFN(bench_s_nop4096, 4096)

secureportNON_SECURE_CALLABLE void SECURE_bench_nop_s(int which)
{
    void (*fn)(void) = (which == 0) ? bench_s_nop256 : ((which == 1) ? bench_s_nop1024 : bench_s_nop4096);
    __DSB(); __ISB();
    uint32_t t0 = BENCH_CC();
    fn();
    __DSB();
    uint32_t t1 = BENCH_CC();
    g_bCur[0] = t0; g_bCur[1] = t1;
}

/* verify_inst 단독 비용 (Secure 내부 경계, BL/BX 포함) */
secureportNON_SECURE_CALLABLE int SECURE_bench_verify(void *dst, uint32_t len)
{
    __DSB(); __ISB();
    uint32_t t0 = BENCH_CC();
    int r = verify_inst((uint32_t)(uintptr_t)dst, len);
    __DSB();
    uint32_t t1 = BENCH_CC();
    g_bCur[0] = t0; g_bCur[1] = t1;
    return r;
}

/* TTAT 추가분(가설 d)을 분리하기 위한 사본: verify_inst 에서 TTAT 블록만 뺀 TTT×2 구성 */
__attribute__((naked, noinline))
static int bench_verify_ttt2(uint32_t addr, uint32_t len)
{
    __asm volatile (
        "SUBS   r2, r1, #1              \n"
        "ADDS   r2, r2, r0              \n"
        "BCS    1f                      \n"
        "TTT    r3, r0                  \n"
        "TTT    r2, r2                  \n"
        "CMP    r3, r2                  \n"
        "BNE    1f                      \n"
        "UBFX   r0, r3, #21, #1         \n"
        "BX     lr                      \n"
        "1:                             \n"
        "MOVS   r0, #0                  \n"
        "BX     lr                      \n"
    );
}

secureportNON_SECURE_CALLABLE int SECURE_bench_verify_ttt2(void *dst, uint32_t len)
{
    __DSB(); __ISB();
    uint32_t t0 = BENCH_CC();
    int r = bench_verify_ttt2((uint32_t)(uintptr_t)dst, len);
    __DSB();
    uint32_t t1 = BENCH_CC();
    g_bCur[0] = t0; g_bCur[1] = t1;
    return r;
}
