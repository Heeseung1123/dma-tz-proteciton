/**
  ******************************************************************************
  * @file    Secure/Src/tzic_arm.c
  * @brief   TZIC SRAM1 illegal-access interrupt (IER3.SRAM1IE) 지연 활성화 (§3.3)
  ******************************************************************************
  *
  * 왜 부팅 시가 아니라 여기서 켜는가:
  *   가드 블록(s_dmaTaskMem[].guard, NS .bss)은 Secure 부팅 중 MPCBB Secure 로 설정된다
  *   (main.c SMPU_SetupTaskRegions). 이후 NS Reset_Handler 가 .bss 를 0 으로 채우며 가드에
  *   쓰면, 쓰기 자체는 MPCBB 가 무시하지만 TZIC SR3.SRAM1F 가 서고 IER 이 켜져 있으면
  *   GTZC_IRQHandler 가 워드마다 재진입한다 [실측]. 그래서 부팅 중에는 IER3.SRAM1IE 를
  *   꺼 두고(main.c Secure_TZIC_Init), 첫 DMA 전송 전에 여기서 켠다.
  *
  * 호출 위치: SECURE_dma_m2m_init() 의 첫 동작 (secure_nsc.c). M2M 모드(CCR.MEM2MEM),
  *   TCIE/TEIE, NS 콜백은 init 에서만 설정되므로, M2M 전송은 이 함수가 IER 을 켠 뒤에만
  *   가능하다 [아키텍처 도출]. 측정 경로(SECURE_nop3, verify_inst, SECURE_dma_m2m_start)에는
  *   아무 것도 추가하지 않는다.
  *
  * 링크 위치: 이 파일은 Secure objects.list 의 맨 마지막에 둔다. 신규 코드/변수가 기존
  *   심볼 뒤에만 놓여 기존 함수·전역 변수 주소가 바뀌지 않게 하기 위함이다.
  *
  * 레지스터 근거:
  *   - TZIC_IER3/SR3/FCR3 오프셋 0x08/0x18/0x28, bit4 = SRAM1:
  *     CMSIS stm32l562xx.h GTZC_TZIC_TypeDef 및 GTZC_CFGR3_SRAM1_Pos(4).
  *     RM0438 GTZC 장 TZIC 레지스터 절 번호: 미확인 (레포에 RM0438 없음).
  *   - IER 이 꺼져 있어도 SR 플래그는 선다: [실측](부팅 시 오발 관찰) / RM 근거 미확인.
  *   - CMSIS 함정: GTZC_TZIC_FCR3_MPCBB1FC_Msk 는 정의되지 않은 GTZC_CFGR3_MPCBB1_Msk 를
  *     참조한다(stm32l562xx.h). 그래서 FCR3 은 GTZC_TZIC_FCR3_MPCBB1FC_Pos(= SRAM1_Pos)로 만든다.
  */
#include "main.h"

/* 감사용: IER 을 켜기 직전의 TZIC SR3 값. 기대: bit4(SRAM1F)=1 (.bss 0 채우기 흔적) */
uint32_t g_tzic_sr3_at_arm;

void Secure_TZIC_ArmGuardIrqOnce(void)
{
    /* IER 비트 자체가 1회 실행 판정. 재호출 시 위반 흔적(SR3)을 지우지 않는다. */
    if ((GTZC_TZIC_S->IER3 & GTZC_TZIC_IER3_SRAM1IE_Msk) == 0U)
    {
        /* 순서 고정: 기록 -> 지움 -> 켬. 지우기 전에 켜면 즉시 재진입한다. */
        g_tzic_sr3_at_arm = GTZC_TZIC_S->SR3;
        GTZC_TZIC_S->FCR3 = (1UL << GTZC_TZIC_FCR3_MPCBB1FC_Pos);   /* SRAM1F 지움 */
        __DSB();
        NVIC_ClearPendingIRQ(GTZC_IRQn);
        GTZC_TZIC_S->IER3 |= GTZC_TZIC_IER3_SRAM1IE_Msk;
        __DSB();
        __ISB();
    }

    /* SECURE_dma_m2m_init() 첫 줄에 있던 것을 그대로 옮겨 왔다 (init 크기 보존용, secure_nsc.c 참고) */
    __HAL_RCC_DMA1_CLK_ENABLE();
}
