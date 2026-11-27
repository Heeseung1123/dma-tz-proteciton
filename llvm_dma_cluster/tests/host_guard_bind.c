/* host_guard_bind.c — secure/secure_guard_bind.c 의 guard_bind_init() 을 호스트에서 "그대로" 실행한다.
 * 사용: host_guard_bind <start0> <end0> <gb0> [<start1> <end1> <gb1> ...]   (ns.elf manifest 값, tests/run_tests.sh 가 넘김)
 * guard_bind.h 는 시험할 Secure 빌드의 생성물(-I) → k_guard_bind_ns[] 가 그 빌드가 기대한 guard 주소.
 * 출력/종료 코드: guard_bind_init 반환값 (0 = 일치, -1 = 불일치 → dma_boot_config() 는 -7).
 * 0 초기화 표는 비워 둔다 (호스트 주소에 쓰지 않도록). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "dma_manifest.h"

__asm__(".section .rodata\n.globl __gbind_zero_table_start\n.globl __gbind_zero_table_end\n"
        "__gbind_zero_table_start:\n__gbind_zero_table_end:\n.long 0\n.text\n");

int guard_bind_init(const struct dma_manifest_entry *e, uint32_t n);

int main(int argc, char **argv)
{
    struct dma_manifest_entry e[DMA_MANIFEST_MAX] = { 0 };
    uint32_t n = (uint32_t)(argc - 1) / 3u;
    if (n == 0 || n > DMA_MANIFEST_MAX || (argc - 1) % 3) { fprintf(stderr, "args\n"); return 2; }
    for (uint32_t i = 0; i < n; i++) {
        e[i].id = i;
        e[i].start = (const void *)(uintptr_t)strtoul(argv[1 + 3 * i], NULL, 0);
        e[i].end = (const void *)(uintptr_t)strtoul(argv[2 + 3 * i], NULL, 0);
        e[i].guard_blocks = (uint32_t)strtoul(argv[3 + 3 * i], NULL, 0);
    }
    int rc = guard_bind_init(e, n);
    printf("guard_bind_init = %d\n", rc);
    return rc == 0 ? 0 : 1;
}
