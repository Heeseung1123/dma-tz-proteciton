/* secure_dma_entry_min.S — (선택안, 기본 아님) landing pad mediator 의 최소 명령 판.
 *
 * = baseline SECURE_dma_m2m_start_asm (project/Secure/Src/secure_nsc.c, MEDIATOR_ASM) 의 본문 그대로
 *   + 진입 직후 pad 점유 검사 6 명령 (secure_dma_entry.S 와 같은 검사).
 * baseline 이 뺀 명령(len==0 검사, CCR.EN=0, IFCR, 성공 경로 레지스터/APSR 정리)의 근거는 secure_nsc.c 주석
 * (docs/gap-analysis.md H) 을 따른다. 성공 경로: baseline 34 → 40 명령 (secure_dma_entry.S 는 59).
 * 거부(pad 점유) 경로만 r3(= PSP_S 값, Secure 주소)를 지운다 — 성공 경로 비용 없음.
 *
 * 선택: build_secure.sh 에 MEDIATOR_S=min  (기본 MEDIATOR_S=full → secure_dma_entry.S)
 * 레지스터: r0=src r1=dst r2=len, 임시 r3, r12. 스택 사용 0.
 */
    .syntax unified
    .thumb
    .text
    .global SECURE_dma_m2m_start, __acle_se_SECURE_dma_m2m_start
    .type   SECURE_dma_m2m_start, %function
    .type   __acle_se_SECURE_dma_m2m_start, %function
    .thumb_func
SECURE_dma_m2m_start:
__acle_se_SECURE_dma_m2m_start:
    cpsid  i                            @ PRIMASK_S: 이후 PSP_S 에 아무것도 쌓이지 않음
    mrs    r3, psp                      @ ── pad 점유 검사 (+6): 비어 있으면 PSP_S == g_pad_top
    movw   r12, #:lower16:g_pad_top
    movt   r12, #:upper16:g_pad_top
    ldr    r12, [r12]
    cmp    r3, r12
    bne    4f                           @ 다른 태스크 프레임이 pad 에 정지 중 / Secure context 로드됨 → 거부
    movw   r12, #:lower16:s_busy        @ ── 이하 baseline SECURE_dma_m2m_start_asm 과 동일
    movt   r12, #:upper16:s_busy
    ldrb   r3, [r12]
    cbnz   r3, 3f                       @ busy -> reject, nothing to release
    movs   r3, #1
    strb   r3, [r12]                    @ s_busy = 1: channel owned
    movw   r3, #0x0008
    movt   r3, #0x5002                  @ r3 = DMA1_Channel1 (Secure alias), EN = 0
    str    r0, [r3, #8]                 @ CPAR  = src
    str    r1, [r3, #12]                @ CM0AR = dst
    str    r2, [r3, #4]                 @ CNDTR = len
    subs   r2, r2, #1
    adds   r2, r2, r1                   @ r2 = dst+len-1; C = overflow (also len == 0)
    bcs    2f
    ttat   r0, r1                       @ NS-MPU(base), unprivileged
    ttat   r12, r2                      @ NS-MPU(end)
    cmp    r0, r12
    bne    2f
    tst    r12, #0x80000                @ bit19 readwrite_ok
    beq    2f
    ttt    r0, r1                       @ S-MPU(base)
    ttt    r12, r2                      @ S-MPU(end)
    cmp    r0, r12
    bne    2f
    tst    r12, #0x200000               @ bit21 nonsecure_readwrite_ok
    beq    2f
    ldr    r12, [r3]
    orr    r12, r12, #1
    str    r12, [r3]                    @ CCR.EN = 1
    movs   r0, #0
    mov    r12, lr
    cpsie  i                            @ 보류 예외는 여기서 받아지고 프레임은 pad 로
    bxns   lr
2:                                      @ reject after the lock: release, EN stays 0
    movw   r12, #:lower16:s_busy
    movt   r12, #:upper16:s_busy
    movs   r0, #0
    strb   r0, [r12]
    b      3f
4:
    mov    r3, lr                       @ pad 거부 경로: PSP_S 값(Secure 주소) 지움
3:
    mov    r0, #-1
    mov    r12, lr
    msr    APSR_nzcvq, lr
    cpsie  i
    bxns   lr
    .size SECURE_dma_m2m_start, . - SECURE_dma_m2m_start
