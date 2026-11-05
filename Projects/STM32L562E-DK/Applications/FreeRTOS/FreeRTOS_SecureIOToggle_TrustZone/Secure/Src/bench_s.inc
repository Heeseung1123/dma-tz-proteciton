/* 중재자 사이클 측정: Secure 쪽 (secure_nsc.c 끝에서 BENCH_PHASE 일 때만 include).
 * 측정 흐름은 NonSecure/Src/bench_ns.inc 맨 위 설명 참고.
 * verify_inst 와 중재자 로직은 호출만 하고 바꾸지 않는다. */

/* SECURE_dma_m2m_start 안의 T1, T4 기록 위치 (BENCH_TS 빌드에서만 기록됨) */
volatile uint32_t g_benchT1;
volatile uint32_t g_benchT4;

/* 직전 start() 의 T4 - T1 을 NS 로 돌려준다 */
secureportNON_SECURE_CALLABLE uint32_t SECURE_bench_inner(void)
{
    return g_benchT4 - g_benchT1;
}

/* T1, T4 기록 자체의 비용: 사이에 아무것도 없이 같은 코드로 기록한다 */
secureportNON_SECURE_CALLABLE uint32_t SECURE_bench_inner_calib(void)
{
    g_benchT1 = DWT->CYCCNT;
    g_benchT4 = DWT->CYCCNT;
    return g_benchT4 - g_benchT1;
}

/* verify_inst 단독 비용 (BL/BX 포함) */
secureportNON_SECURE_CALLABLE uint32_t SECURE_bench_verify(void *dst, uint32_t len)
{
    __DSB(); __ISB();
    uint32_t t0 = DWT->CYCCNT;
    (void)verify_inst((uint32_t)(uintptr_t)dst, len);
    __DSB();
    uint32_t t1 = DWT->CYCCNT;
    return t1 - t0;
}

/* 위 측정의 판독 비용: verify_inst 호출만 뺀 같은 순서 */
secureportNON_SECURE_CALLABLE uint32_t SECURE_bench_verify_calib(void)
{
    __DSB(); __ISB();
    uint32_t t0 = DWT->CYCCNT;
    __DSB();
    uint32_t t1 = DWT->CYCCNT;
    return t1 - t0;
}

/* 측정 구간 동안 DMA IRQ 를 막아 전송 완료 ISR 이 구간에 섞이지 않게 한다 */
secureportNON_SECURE_CALLABLE void SECURE_bench_irq_mask(int mask)
{
    if (mask) { NVIC_DisableIRQ(DMA_CH_IRQn); }
    else      { NVIC_EnableIRQ(DMA_CH_IRQn); }
    __DSB(); __ISB();
}

/* cold 조건: ICACHE 전체 무효화 */
secureportNON_SECURE_CALLABLE void SECURE_bench_cold(void)
{
    ICACHE->CR |= ICACHE_CR_CACHEINV;
    while (ICACHE->SR & ICACHE_SR_BUSYF) { }
    ICACHE->FCR = ICACHE_FCR_CBSYENDF;
    __DSB(); __ISB();
}
