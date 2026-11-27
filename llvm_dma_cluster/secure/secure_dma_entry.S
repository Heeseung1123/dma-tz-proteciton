/* secure_dma_entry.S — DMA mediator: assembly + atomic, 자체 스택 사용 0
 *
 * 수정 내역 (Fable 검토 반영)
 *  (1) 분기 범위: CBZ/CBNZ 는 전방 0–126 B 만 가능. 먼 분기를 모두 cmp + b<cond> 로 교체.
 *  (2) 탈출 창: cpsid 동안 보류된 예외는 cpsie 직후 (bxns 전) Secure Thread 위에서 받아진다.
 *      PSP_S = 0 이면 그 순간 stacking fault → 사실상 Secure 체류 전체가 노출.
 *      → 쉬는 동안 PSP_S 를 항상 "landing pad"(guard 블록 하나, 링크 시점에 정해진 상수 주소)
 *        top 에 둔다. 창에서 받은 예외의 프레임은 pad 에 쌓이고, 그 태스크가 전환돼도
 *        PSP_S 가 프레임을 계속 가리킨다. 다른 태스크는 pad 가 비어 있을 때만 진입한다.
 *      → stub 은 PSP_S 를 설치/해제하지 않는다 (명령 추가 = pad 점유 검사 5개).
 *
 * 레지스터: r0=src r1=dst r2=len (보존), 임시 r3, r12. r4–r11 은 건드리지 않음.
 * 전제: g_pad_top = (guard 블록 Secure alias) + 248, dma_boot_config() 가 설정.
 */
    .syntax unified
    .thumb
    .text
    .equ  DMA1_S, 0x50020000            @ IFCR +4, CCR1 +8, CNDTR1 +0xC, CPAR1 +0x10, CM0AR1 +0x14
    .global SECURE_dma_m2m_start, __acle_se_SECURE_dma_m2m_start
    .type   SECURE_dma_m2m_start, %function
    .type   __acle_se_SECURE_dma_m2m_start, %function
    .thumb_func
SECURE_dma_m2m_start:
__acle_se_SECURE_dma_m2m_start:
    cpsid i                             @ ── 원자 구간 시작 (PRIMASK_S)
    mrs   r3, ipsr
    cmp   r3, #0
    bne   rej                           @ Handler mode 거부
    mrs   r3, psp                       @ ── pad 점유 검사: 비어 있으면 PSP_S == pad top
    movw  r12, #:lower16:g_pad_top
    movt  r12, #:upper16:g_pad_top
    ldr   r12, [r12]
    cmp   r3, r12
    bne   rej                           @ 다른 태스크의 프레임이 pad 에 정지 중 (또는 stock 스택 위) → 거부
    movw  r12, #:lower16:s_busy
    movt  r12, #:upper16:s_busy
    ldrb  r3, [r12]
    cmp   r3, #0
    bne   rej                           @ 전송 중 → 거부 (busy 건드리지 않음)
    movs  r3, #1
    strb  r3, [r12]                     @ busy 획득 (DMA 완료 ISR(Secure)이 0 으로)

    movw  r12, #:lower16:DMA1_S
    movt  r12, #:upper16:DMA1_S
    ldr   r3, [r12, #8]
    bic   r3, r3, #1
    str   r3, [r12, #8]                 @ CCR1.EN = 0
    str   r0, [r12, #0x10]              @ CPAR1 = src → r0 해방

    cmp   r2, #0
    beq   fail
    subs  r0, r2, #1
    adds  r0, r0, r1                    @ end, C = overflow
    bcs   fail
    ttat  r3, r1
    ttat  r12, r0
    cmp   r3, r12
    bne   fail
    tst   r12, #(1 << 19)               @ 권한: NS-MPU RW (호출 태스크, 비특권)
    beq   fail
    ttt   r3, r1
    ttt   r12, r0
    cmp   r3, r12
    bne   fail                          @ 소속: 같은 S-MPU region
    tst   r3, #(1 << 21)                @ NSRW
    beq   fail

    movw  r12, #:lower16:DMA1_S
    movt  r12, #:upper16:DMA1_S
    str   r1, [r12, #0x14]              @ CM0AR1 = dst
    str   r2, [r12, #0x0C]              @ CNDTR1 = len
    movs  r3, #1
    str   r3, [r12, #4]                 @ IFCR = CGIF1
    ldr   r3, [r12, #8]
    orr   r3, r3, #1
    str   r3, [r12, #8]                 @ CCR1.EN = 1
    movs  r0, #0
    b     out
fail:
    movw  r12, #:lower16:s_busy
    movt  r12, #:upper16:s_busy
    movs  r3, #0
    strb  r3, [r12]                     @ 내가 잡은 busy 만 해제
rej:
    mvn   r0, #0
out:
    mov   r1, lr                        @ Secure 값 정리
    mov   r2, lr
    mov   r3, lr
    mov   r12, lr
    msr   APSR_nzcvqg, lr
    cpsie i                             @ ── 원자 구간 끝. 보류 예외는 여기서 받아지며 프레임은 pad 로
    bxns  lr
    .size SECURE_dma_m2m_start, . - SECURE_dma_m2m_start
