/* dma_manifest.h — NS(생성기 출력)와 Secure(boot 설정) 공용 */
#ifndef DMA_MANIFEST_H
#define DMA_MANIFEST_H
#include <stdint.h>

#define DMA_MANIFEST_MAGIC    0x444D414Du        /* 'DMAM' */
#define DMA_MANIFEST_VERSION  1u
#define DMA_MANIFEST_MAX      8u                 /* DMA 태스크 ≤ 8 (S-MPU region 수 상한) */
/* NS 링커 스크립트가 .dma_manifest 를 이 주소에 고정한다 (NS vector table 바로 뒤).
 * 실제 NS FLASH ORIGIN 과 vector table 크기에 맞게 정할 것. */
#define DMA_MANIFEST_NS_ADDR  0x08040400u

#define DMA_KIND_TASK    1u
#define DMA_KIND_SHARED  2u

struct dma_manifest_entry {
    uint32_t    id;
    uint32_t    kind;          /* DMA_KIND_* */
    const void *start;         /* cluster 시작 (NS alias, 32 B 정렬)  → S-MPU RBAR */
    const void *end;           /* cluster 끝 = guard 시작 (256 B 정렬) → S-MPU RLAR = end-1 */
    uint32_t    guard_blocks;  /* end 부터 256 B × guard_blocks 를 MPCBB Secure 로 */
};

struct dma_manifest {
    uint32_t magic, version, count;
    struct dma_manifest_entry e[DMA_MANIFEST_MAX];
};

#endif
