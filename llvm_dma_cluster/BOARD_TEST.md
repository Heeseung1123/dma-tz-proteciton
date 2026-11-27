# 보드 확인 절차 (4단계) — 아직 보드에서 실행하지 않았음

대상 이미지: `build/secure/secure.elf` + `build/ns/ns.elf` (`scripts/build_all.sh` 산출물, MEDIATOR_S=full).
아래 주소·기대값은 그 빌드에서 읽은 값이다. 다시 빌드하면 `llvm-nm` 으로 바뀐 것이 없는지 먼저 확인한다
(현재 값: c0 0x20018000 / c1 0x20018200 / c2 0x20018400, guard 각 +0x100, pad_top 0x300181f8).
보드에 올리기 전: `python3 scripts/check_pair.py build/secure/secure.elf build/ns/ns.elf` → `예상 = 0`.

## 0. 올리기
```bash
~/st-link.sh &                                   # ST-LINK_gdbserver, :61234
arm-none-eabi-gdb build/secure/secure.elf
(gdb) target remote :61234
(gdb) monitor reset halt
(gdb) load build/secure/secure.elf
(gdb) load build/ns/ns.elf
(gdb) add-symbol-file build/ns/ns.elf
(gdb) monitor reset halt
```
(CubeIDE 디버그 구성으로 올려도 된다. NS 는 lld 산출물이지만 일반 ELF 이다.)

## 1. 부팅: `dma_boot_config()` == 0
```gdb
b dma_boot_config
c
finish                         # Value returned = 0
p g_dma_boot_status            # 0   (1 이면 아직 실행 전, <0 이면 Error_Handler 에서 정지)
p/x g_pad_top                  # 0x300181f8
p/x g_idle_psp                 # 0x300181f8
# S-MPU 태그 (Secure 상태에서)
set $i = 4
while $i < 7
  set *(unsigned*)0xE000ED98 = $i
  printf "rnr %d: RBAR=%08x RLAR=%08x\n", $i, *(unsigned*)0xE000ED9C, *(unsigned*)0xE000EDA0
  set $i = $i + 1
end
# 기대: rnr 4: 20018003 200180e1 / rnr 5: 20018203 200182e1 / rnr 6: 20018403 200184e1
p/x *(unsigned*)0x50032D30     # MPCBB1 VCTR[12] = 0x0000002a (블록 385/387/389 = guard 3 개만 Secure)
```

## 2. 정상 DMA (1–2 분 실행 후 Ctrl-C)
```gdb
p s_privA.okCount              # 약 2 회/초 증가
p s_privB.okCount              # 약 1.4 회/초 증가
x/8bx &s_dmaDstA               # 00 01 02 03 04 05 06 07
x/8bx &s_dmaDstB               # 80 81 82 83 84 85 86 87
x/8bx &s_dmaShared             # 00..07 (A),   x/8bx ((char*)&s_dmaShared)+32 → 80..87 (B)
p/x s_privA.v.dmaStatus        # 0 (TC)
```
LED10 은 Plain_Task 가 200 ms 마다 토글 (Secure context 를 가진 태스크의 Load/Save 패치 경로가 살아 있음).

## 3. 요청 시점 공격 (논문 §4 (a)) — Task A 시작 시 1 회 자동 실행
```gdb
p s_privA.attack               # {0, -1, -1, -1, -1, -1}
```
[0] 정상, [1] Task B cluster (TTAT: A 의 NS-MPU 에 없음), [2] A 자신의 globals (TTT: 태그 없음),
[3] 길이 257 (끝이 guard), [4] 끝이 cluster 밖, [5] Secure 주소 0x30000000.

## 4. 전송 시점 guard (논문 §4 (b)) — 검증 통과 뒤 길이 변조
```gdb
b *0x0c0002d0                  # CCR.EN = 1 직전 (__acle_se_SECURE_dma_m2m_start), 정상 요청이 검증을 통과한 상태
c
p/x $r1                        # dst. 0x20018200 (Task A) 인 정지점까지 c 반복
set *(unsigned*)0x5002000C = 0x210     # CNDTR1: 8 → 528 B (cluster 256 + guard 256 + 16 B 너머)
x/4wx 0x20018400               # (전) Task B cluster 앞부분 기록
delete
c   (몇 초) → Ctrl-C
p/x *(unsigned*)0x50032818     # TZIC SR3: bit4 SRAM1F (GTZC_IRQHandler 가 지우기 전이면 1)
x/4wx 0x20018400               # (후) 위와 같아야 함 = guard 너머 무변조
x/4wx 0x30018300               # Secure 에서 본 guard: NS DMA 쓰기 무시 → 이전 값 그대로
p s_privA.okCount              # 계속 증가 (채널이 중단·복구되고 다음 요청이 처리됨)
```
읽기 방향: 같은 정지점에서 `set *(unsigned*)0x50020010 = 0x20018300` (CPAR1 = guard) → TEIF →
`p/x s_privA.v.dmaStatus` = 1.

## 5. 탈출 창의 보류 인터럽트 → landing pad (T1)
```gdb
b *0x0c0002f6                  # out: 의 cpsie i (원자 구간 끝)
c
set *(unsigned*)0xE002ED04 = (1<<26)   # ICSR_NS.PENDSTSET: NS SysTick 을 보류
p/x $psp_s                     # (ST gdbserver 이름이 다르면 info registers) = 0x300181f8
stepi                          # cpsie 직후 예외 진입
p/x $psp_s                     # < 0x300181f8 이고 >= 0x30018100 (프레임이 pad 안)
delete
c   (몇 초) → Ctrl-C
p s_privA.okCount              # 계속 증가, HardFault/SecureFault 없음
p/x *(unsigned*)0xE000EDE4     # SFSR = 0
p/x *(unsigned*)0xE000ED28     # CFSR = 0
```
T2 (pad 점유 중 다른 DMA 태스크 → −1) 는 정상 태스크가 mutex 로 직렬화되어 재현이 어렵다. 필요하면
위 stepi 직후 `set $pc` 로 Task B 의 호출을 강제해야 하므로 생략 가능.

## 6. 음성: stale NS → −7 (정지)
```bash
python3 scripts/check_pair.py build/t_bind/secure/secure.elf build/t_stale/ns/ns.elf --expect -7
```
`build/t_bind/secure/secure.elf` + `build/t_stale/ns/ns.elf` 를 올리고 `b Error_Handler`, `c` →
정지, `p g_dma_boot_status` = −7. (짝이 맞는 `build/t_bind/ns/ns.elf` 로는 0.)

## 7. 사이클 (baseline 983a749 와 같은 방법으로)
DMA 태스크가 비특권이라 DWT 를 코드에서 읽을 수 없으므로 GDB 정지점 사이의 CYCCNT 차이로 잰다
(정지 중에는 CYCCNT 가 멈춘다). 같은 스크립트를 baseline 이미지에도 적용해 비교한다.
```gdb
set *(unsigned*)0xE000EDFC |= (1<<24)  # DEMCR.TRCENA
set *(unsigned*)0xE0001000 |= 1        # DWT_CTRL.CYCCNTENA
# NSC 1 회: vDmaTaskA 안 SECURE_dma_m2m_start 호출(bl) 과 다음 명령에 정지점 (disassemble vDmaTaskA 로 주소 확인)
# 컨텍스트 전환: PendSV_Handler(0x0804059c) 진입과 마지막 bx lr
```
정적 명령 수 (실측 전 참고, `objdump`):

| 경로 | baseline | 이 빌드 (full) | 이 빌드 (min) |
|---|---|---|---|
| mediator 성공 경로 | 34 (`SECURE_dma_m2m_start_asm`) | 59 | 40 |
| `SecureContext_LoadContextAsm` | 9 | 13 | 13 |
| `SecureContext_SaveContextAsm` | 13 | 21 | 21 |

Load/Save 증가분은 Secure context 를 가진 태스크(Plain_Task)가 전환될 때만 든다.
