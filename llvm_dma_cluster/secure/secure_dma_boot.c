/* secure_dma_boot.c — Secure boot 에서 NS manifest 를 읽어
 *   (1) DMA cluster 마다 S-MPU 태그 region 설정
 *   (2) cluster 바로 뒤 guard 블록을 MPCBB Secure 로 설정
 * 을 자동으로 수행한다. 기존 main.c 의 SMPU_SetupTaskRegions() 를 대체한다.
 *
 * 호출 위치: MX_GTZC_S_Init() 다음, NS 로 점프하기 전 (NS 가 아직 실행되지 않으므로
 *            manifest 를 읽는 동안 NS 가 바꿀 수 없다).
 * 이 파일은 Secure 프로젝트(GCC 그대로)에 추가한다. LLVM 이 필요 없다.
 */
#include "main.h"                 /* stm32l5xx_hal.h, MPU, GTZC HAL */
#include "dma_manifest.h"

#define TAG_RNR0        4u        /* DMA 태그용 첫 S-MPU region (기존 코드: 4, 5) */
#define NS_SRAM_LO      0x20018000u   /* SAU 상 NS SRAM 시작 (partition_stm32l562xx.h) */
#define NS_SRAM_HI      0x20040000u   /* SRAM1+SRAM2 끝 */
/* 기존 main.c 의 HAL_GTZC_MPCBB_ConfigMemAttributes(guard, 1u, &attr) 와 같은 인자 규약.
 * HAL 버전의 size 단위(바이트/블록)를 문서로 확인하고 기존 동작과 동일하게 유지할 것. */
#define GUARD_HAL_SIZE_ARG(nblk)  (nblk)

/* landing pad: 쉬는 동안 PSP_S 가 가리키는 곳 = cluster 0 의 guard 블록 top (Secure alias).
 * 링크 결과로 정해지는 상수이며 런타임에 호출자별로 조회하지 않는다.
 * secure_dma_entry.S 의 점유 검사와 SecureContext_Init/Save/Load 가 사용한다. */
uint32_t g_pad_top;
uint32_t g_idle_psp;              /* stock Load 가 보존, Save 가 복원하는 쉼 PSP_S */

static uint32_t prvMaxTags(void)
{
    uint32_t dregion = (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;  /* 8 */
    return (dregion > TAG_RNR0) ? (dregion - TAG_RNR0) : 0u;
}

static void prvSetTag(uint32_t rnr, uint32_t base, uint32_t limit)
{
    MPU->RNR  = rnr;
    MPU->RBAR = (base & MPU_RBAR_BASE_Msk)
              | (0u << MPU_RBAR_SH_Pos)     /* Non-shareable */
              | (1u << MPU_RBAR_AP_Pos)     /* AP=01: RW, priv+unpriv → TTT(unpriv) NSRW 판정 */
              | (1u << MPU_RBAR_XN_Pos);    /* XN */
    MPU->RLAR = (limit & MPU_RLAR_LIMIT_Msk)
              | (0u << MPU_RLAR_AttrIndx_Pos)
              | MPU_RLAR_EN_Msk;
}

/* 반환: 0 성공, <0 manifest 오류 (호출 측에서 NS 진입 중단 또는 mediator 비활성) */
int dma_boot_config(void)
{
    const struct dma_manifest *m = (const struct dma_manifest *)DMA_MANIFEST_NS_ADDR;
    struct dma_manifest_entry e[DMA_MANIFEST_MAX];
    uint32_t n = m->count;

    if (m->magic != DMA_MANIFEST_MAGIC || m->version != DMA_MANIFEST_VERSION) return -1;
    if (n == 0u || n > DMA_MANIFEST_MAX || n > prvMaxTags())                 return -2;

    for (uint32_t i = 0; i < n; i++) {          /* Secure 쪽 사본으로 검증·사용 */
        e[i] = m->e[i];
        uint32_t s = (uint32_t)e[i].start, g = (uint32_t)e[i].end;
        uint32_t ge = g + 256u * e[i].guard_blocks;
        if ((s & 31u) || (g & 255u) || s >= g || e[i].guard_blocks == 0u)  return -3;
        if (s < NS_SRAM_LO || ge > NS_SRAM_HI || ge < g)                    return -4;
        for (uint32_t j = 0; j < i; j++) {       /* cluster+guard 구간끼리 겹침 금지 */
            uint32_t s2 = (uint32_t)e[j].start, ge2 = (uint32_t)e[j].end + 256u * e[j].guard_blocks;
            if (s < ge2 && s2 < ge)                                          return -5;
        }
    }

    /* (1) S-MPU 태그 */
    __DMB();
    MPU->CTRL  = 0;
    MPU->MAIR0 = 0xFFu;                          /* AttrIndx 0: Normal WB (기존 코드와 동일) */
    for (uint32_t i = 0; i < n; i++)
        prvSetTag(TAG_RNR0 + i, (uint32_t)e[i].start, (uint32_t)e[i].end - 1u);
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();

    /* (2) MPCBB guard: 각 블록을 Secure 로 (MX_GTZC_S_Init 이 NS 로 둔 구간 위에 덮어씀) */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t attr = GTZC_MCPBB_BLOCK_SEC;
        for (uint32_t k = 0; k < e[i].guard_blocks; k++) {
            uint32_t gb = (uint32_t)e[i].end + 256u * k;
            if (HAL_GTZC_MPCBB_ConfigMemAttributes(gb, GUARD_HAL_SIZE_ARG(1u), &attr) != HAL_OK)
                return -6;
        }
    }

    /* (3) landing pad = e[0] 의 첫 guard 블록 (frame 최대 212 B < 248 B).
     *     guard 는 Secure 이므로 NS CPU/NS DMA 가 pad 위 프레임을 읽거나 바꿀 수 없다.
     *     SecureContext_Init() 보다 먼저 실행되어야 한다 (NS 진입 전이므로 성립). */
    g_pad_top  = (uint32_t)e[0].end + 0x10000000u + 248u;
    g_idle_psp = g_pad_top;

    /* (3-1) guard 에 배치된 Secure 데이터: 주소 일치 확인 + 0 초기화 (secure_guard_bind.c) */
    {
        extern int guard_bind_init(const struct dma_manifest_entry *e, uint32_t n);
        if (guard_bind_init(e, n) != 0)
            return -7;                           /* 이미지 불일치: Secure 데이터 사용 금지 */
    }

    /* (4) (선택) 설정 잠금: 런타임에 manifest 없이도 정책이 하드웨어에 고정되게 한다.
     *     SYSCFG->CSLCKR |= SYSCFG_CSLCKR_LOCKSMPU;   (RLAR/alias 포함 여부는 실측 확인)
     *     MPCBB 잠금은 GTZC_MPCBBx_CR.LCK / LCKVTR (superblock) — RM0438 ch.5 참고 */
    return 0;
}
