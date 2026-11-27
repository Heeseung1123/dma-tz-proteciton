/* NS startup 보조: DMA cluster(NOLOAD) 를 0 으로 초기화한다.
 * - guard 블록은 Secure boot 가 이미 MPCBB Secure 로 만들었으므로 절대 건드리지 않는다
 *   (NS 가 쓰면 무시되고 TZIC illegal access 이벤트가 발생).
 * - startup_stm32l562xx.s 의 .bss 0 초기화 루프 다음, __libc_init_array 전에 호출:
 *       bl dma_zero_clusters
 */
#include <stdint.h>

extern const uint32_t __dma_zero_table_start[];
extern const uint32_t __dma_zero_table_end[];

void dma_zero_clusters(void)
{
    for (const uint32_t *p = __dma_zero_table_start; p < __dma_zero_table_end; p += 2) {
        uint32_t *q = (uint32_t *)p[0];
        uint32_t *e = (uint32_t *)p[1];
        while (q < e) {
            *q++ = 0u;
        }
    }
}
