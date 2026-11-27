/* secure_guard_bind.c — guard 블록에 배치된 Secure 데이터의 부팅 시 검증과 초기화
 *
 * pack_guards.py 가 Secure 데이터를 "NS 링크 결과의 guard 주소"에 고정 배치했다.
 * NS 이미지만 다시 빌드되어 guard 위치가 바뀌면, Secure 데이터가 NS 메모리에 놓이게
 * 된다 (= 유출). 그래서 부팅 때 manifest 의 실제 guard 와 링크 때 기대한 주소를
 * 비교해, 하나라도 다르면 실패를 반환한다 (호출 측에서 정지).
 *
 * 호출 순서: dma_boot_config() 안, MPCBB guard 를 Secure 로 설정한 "직후".
 *            이 함수가 성공하기 전에는 guard 에 놓인 Secure 데이터를 쓰면 안 된다.
 */
#include <stdint.h>
#include "dma_manifest.h"
#include "guard_bind.h"           /* 생성물: GUARD_BIND_N, k_guard_bind_ns[] */

extern const uint32_t __gbind_zero_table_start[];
extern const uint32_t __gbind_zero_table_end[];

#if GUARD_BIND_N > 0
static int prvIsGuardBlock(const struct dma_manifest_entry *e, uint32_t n, uint32_t ns)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t g = (uint32_t)e[i].end;
        for (uint32_t k = 0; k < e[i].guard_blocks; k++) {
            uint32_t b = g + 256u * k;
            if (i == 0u && k == 0u)
                continue;                       /* landing pad 는 데이터용이 아님 */
            if (b == ns)
                return 1;
        }
    }
    return 0;
}
#endif

int guard_bind_init(const struct dma_manifest_entry *e, uint32_t n)
{
#if GUARD_BIND_N > 0
    for (uint32_t k = 0; k < GUARD_BIND_N; k++)
        if (!prvIsGuardBlock(e, n, k_guard_bind_ns[k]))
            return -1;                          /* NS/Secure 이미지 불일치 → fail-closed */
#else
    (void)e; (void)n;
#endif
    /* guard 의 Secure 데이터는 NOLOAD 이므로 Secure 가 직접 0 으로 초기화 */
    for (const uint32_t *p = __gbind_zero_table_start; p < __gbind_zero_table_end; p += 2) {
        uint32_t *q = (uint32_t *)p[0], *end = (uint32_t *)p[1];
        while (q < end)
            *q++ = 0u;
    }
    return 0;
}
