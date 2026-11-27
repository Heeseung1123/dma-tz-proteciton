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

#if MEDIATOR_ASM
/* SECURE_dma_m2m_start_asm 의 T1, T4 기록 비용: 같은 MED_ASM_TS 명령을 사이에 아무것도 없이 둔다.
 * BENCH_TS 가 없으면 MED_ASM_TS 는 비어 있어 의미 없는 값을 돌려준다 (ts1 빌드 값만 쓴다). */
secureportNON_SECURE_CALLABLE uint32_t SECURE_bench_inner_calib_asm(void)
{
    __asm volatile (MED_ASM_TS(g_benchT1) MED_ASM_TS(g_benchT4) ::: "r3", "r12", "memory");
    return g_benchT4 - g_benchT1;
}
#endif

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

/* ISR bench: is the DMA TC interrupt pending? (NS cannot see Secure IRQ pending bits) */
secureportNON_SECURE_CALLABLE int SECURE_bench_dma_pending(void)
{
    return (int)NVIC_GetPendingIRQ(DMA_CH_IRQn);
}

/* cold 조건: ICACHE 전체 무효화 */
secureportNON_SECURE_CALLABLE void SECURE_bench_cold(void)
{
    ICACHE->CR |= ICACHE_CR_CACHEINV;
    while (ICACHE->SR & ICACHE_SR_BUSYF) { }
    ICACHE->FCR = ICACHE_FCR_CBSYENDF;
    __DSB(); __ISB();
}

#if defined(BENCH_DECOMP) && MEDIATOR_ASM
/* ---- 분해 실험 (cmpAsmMediator.md §6): asm 중재자에 C 경로의 부가 작업을 하나씩 되돌려 넣은 변형 ----
 * 모두 naked NSC 이고, 이 빌드에서만 존재한다. 본문은 SECURE_dma_m2m_start_asm 과 같고 삽입 지점만 다르다.
 *   L0  빈 NSC (asm): r0 = 0, r1-r3/r12/APSR 정리, BXNS. 프롤로그와 FP 정리가 없다.
 *   L1  L0 + GCC 의 cmse FP 정리 (SECURE_nop3 에필로그를 그대로 복사: vmov.f32 s0-s15 #1.0, FPSCR 마스크, push/pop r4)
 *   M1  asm 중재자 + C 경로에만 있는 DMA 레지스터 작업 2개 (CCR.EN=0 RMW, IFCR=CGIF1)
 *   M2  M1 + L1 과 같은 FP 정리 (cpsie 뒤)
 * C - M2 = 컴파일러가 만든 코드 자체의 차이 (프롤로그/에필로그, 스택 spill, literal 적재, BL verify_inst 등). */
#define GCC_CMSE_FP_CLR \
    "vmov.f32 s0, #1.0 \n vmov.f32 s1, #1.0 \n vmov.f32 s2, #1.0 \n vmov.f32 s3, #1.0 \n" \
    "vmov.f32 s4, #1.0 \n vmov.f32 s5, #1.0 \n vmov.f32 s6, #1.0 \n vmov.f32 s7, #1.0 \n" \
    "vmov.f32 s8, #1.0 \n vmov.f32 s9, #1.0 \n vmov.f32 s10, #1.0 \n vmov.f32 s11, #1.0 \n" \
    "vmov.f32 s12, #1.0 \n vmov.f32 s13, #1.0 \n vmov.f32 s14, #1.0 \n vmov.f32 s15, #1.0 \n" \
    "msr APSR_nzcvqg, lr \n push {r4} \n vmrs r12, fpscr \n movw r4, #0xff60 \n movt r4, #0x0fff \n" \
    "and r12, r12, r4 \n vmsr fpscr, r12 \n pop {r4} \n"
#define CLR_INT_EXIT  "mov r1, lr \n mov r2, lr \n mov r3, lr \n"

__attribute__((cmse_nonsecure_entry, naked, used))
int SECURE_bench_nsc_asm0(void *a, void *b, uint32_t c)
{
    (void)a; (void)b; (void)c;
    __asm volatile ("movs r0, #0 \n" CLR_INT_EXIT "msr APSR_nzcvqg, lr \n mov r12, lr \n bxns lr \n");
}

__attribute__((cmse_nonsecure_entry, naked, used))
int SECURE_bench_nsc_asm_fpclr(void *a, void *b, uint32_t c)
{
    (void)a; (void)b; (void)c;
    __asm volatile ("movs r0, #0 \n" CLR_INT_EXIT GCC_CMSE_FP_CLR "mov r12, lr \n bxns lr \n");
}

/* SECURE_dma_m2m_start_asm 본문. PRE: 잠금 뒤 레지스터 쓰기 전, PREEN: EN 직전, OKX/REJX: 복귀 직전 */
/* BUSYBR: M2 는 FP 정리가 끼어 cbnz 의 전방 범위(126 B)를 넘으므로 cmp + bne 를 쓴다 (명령 1개, 약 +1 cycle) */
#define MED_VARIANT(BUSYBR, PRE, PREEN, OKX, REJX) \
        "cpsid  i \n movw r12, #:lower16:s_busy \n movt r12, #:upper16:s_busy \n" \
        "ldrb r3, [r12] \n" BUSYBR "movs r3, #1 \n strb r3, [r12] \n" \
        "movw r3, #0x0008 \n movt r3, #0x5002 \n" PRE \
        "str r0, [r3, #8] \n str r1, [r3, #12] \n str r2, [r3, #4] \n" \
        "subs r2, r2, #1 \n adds r2, r2, r1 \n bcs 2f \n" \
        "ttat r0, r1 \n ttat r12, r2 \n cmp r0, r12 \n bne 2f \n tst r12, #0x80000 \n beq 2f \n" \
        "ttt r0, r1 \n ttt r12, r2 \n cmp r0, r12 \n bne 2f \n tst r12, #0x200000 \n beq 2f \n" PREEN \
        "ldr r12, [r3] \n orr r12, r12, #1 \n str r12, [r3] \n" \
        "movs r0, #0 \n cpsie i \n" OKX "mov r12, lr \n bxns lr \n" \
        "2: \n movw r12, #:lower16:s_busy \n movt r12, #:upper16:s_busy \n movs r0, #0 \n strb r0, [r12] \n" \
        "3: \n mov r0, #-1 \n cpsie i \n" REJX "mov r12, lr \n msr APSR_nzcvq, lr \n bxns lr \n"
#define C_PERIPH_PRE    "ldr r12, [r3] \n bic r12, r12, #1 \n str r12, [r3] \n"   /* CCR.EN = 0 */
#define C_PERIPH_PREEN  "movs r12, #1 \n str r12, [r3, #-4] \n"                    /* IFCR = CGIF1 */

__attribute__((cmse_nonsecure_entry, naked, used))
int SECURE_bench_start_m1(void *a, void *b, uint32_t c)
{
    (void)a; (void)b; (void)c;
    __asm volatile (MED_VARIANT("cbnz r3, 3f \n", C_PERIPH_PRE, C_PERIPH_PREEN, "", ""));
}

__attribute__((cmse_nonsecure_entry, naked, used))
int SECURE_bench_start_m2(void *a, void *b, uint32_t c)
{
    (void)a; (void)b; (void)c;
    __asm volatile (MED_VARIANT("cmp r3, #0 \n bne 3f \n", C_PERIPH_PRE, C_PERIPH_PREEN, CLR_INT_EXIT GCC_CMSE_FP_CLR, ""));
}
#endif /* BENCH_DECOMP */
