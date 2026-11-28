# 보드 확인 절차 (4단계) — 2026-11-28 실행, 결과: VERIFY_LOG.md "보드 확인", 스크립트: tests/board/

대상 이미지: `build/secure/secure.elf` + `build/ns/ns.elf` (`scripts/build_all.sh` 산출물).
Mediator 는 프로젝트와 같은 SVC 경로다: 태스크 `svc #110` → NS `SVC_Handler` fast path (`dma_m2m_start_svc`
레이블) → Handler mode 에서 `SECURE_dma_m2m_start_svc` (secure_nsc.c, asm). landing pad / Secure 스택 패치 없음.
아래 주소·기대값은 그 빌드에서 읽은 값이다. 다시 빌드하면 `llvm-nm` 으로 바뀐 것이 없는지 먼저 확인한다
(현재 값: c0 0x200180c0–0x20018100 (앞 칸 0x20018000–0x200180c0 = s_privA, s_privB) / c1 0x20018200 / c2 0x20018400,
guard 0x20018100 / 0x20018300 / 0x20018500).
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
# S-MPU 태그 (Secure 상태에서)
set $i = 4
while $i < 7
  set *(unsigned*)0xE000ED98 = $i
  printf "rnr %d: RBAR=%08x RLAR=%08x\n", $i, *(unsigned*)0xE000ED9C, *(unsigned*)0xE000EDA0
  set $i = $i + 1
end
# 기대: rnr 4: 200180c3 200180e1 / rnr 5: 20018203 200182e1 / rnr 6: 20018403 200184e1
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
LED10 은 Plain_Task 가 200 ms 마다 토글 (Secure context 를 가진 태스크의 stock Load/Save 경로).

## 3. 요청 시점 공격 (논문 §4 (a)) — Task A 시작 시 1 회 자동 실행
```gdb
p s_privA.attack               # {0, -1, -1, -1, -1, -1}
```
[0] 정상, [1] Task B cluster (TTAT: A 의 NS-MPU 에 없음), [2] A 자신의 globals (TTT: 태그 없음. s_privA 는 c0 바로
앞 칸 0x20018000 에 있으므로 "cluster 앞 칸은 DMA 대상이 아님" 도 함께 확인된다),
[3] 길이 257 (끝이 guard), [4] 끝이 cluster 밖, [5] Secure 주소 0x30000000.

## 4. 전송 시점 guard (논문 §4 (b)) — 검증 통과 뒤 길이 변조
```gdb
b *0x0c000f20                  # CCR.EN = 1 (str.w r12,[r3]) 직전, __acle_se_SECURE_dma_m2m_start_svc
c
p/x $r1                        # dst. 0x20018200 (Task A) 인 정지점까지 c 반복
set *(unsigned*)0x5002000C = 0x210     # CNDTR1: 8 → 528 B (cluster 256 + guard 256 + 16 B 너머)
x/4wx 0x20018400               # (전) Task B cluster 앞부분 기록
delete
c   (몇 초) → Ctrl-C
p/x *(unsigned*)0x50032818     # TZIC SR3: bit4 SRAM1F (GTZC_IRQHandler 가 지우기 전이면 1)
x/4wx 0x20018400               # (후) 위와 같아야 함 = guard 너머 무변조
x/4wx 0x30018300               # Secure 에서 본 guard: NS DMA 쓰기 무시 → 이전 값 그대로
p s_privA.okCount              # [실측 2026-11-28] 정지: GTZC_IRQHandler 는 채널만 끄고 NS 통지/s_busy 해제가 없어 Task A 영구 대기
```
주의: s_privA(DMA source)가 c0 앞 칸에 있으므로 길이만 늘리면 source 읽기가 c0 guard 에 먼저 닿는다. 쓰기 방향을 보려면
같은 정지점에서 `set *(unsigned*)0x50020010 = 0x08040000` (CPAR1 = NS flash) 도 한다.
읽기 방향: 같은 정지점에서 `set *(unsigned*)0x50020010 = 0x20018300` (CPAR1 = guard), CNDTR1 = 16.
[실측 2026-11-28] TEIF 는 나지 않는다: 0 을 읽어(RAZ) 8 B 전송 뒤 GTZC_IRQHandler 가 EN=0, DMA ISR = 0x5 (GIF/HTIF), Task A 영구 대기.
(CLAUDE.md 의 "읽기 → TEIF" 와 다름, VERIFY_LOG D7)

## 5. SVC 경로 확인 (Secure context 없는 DMA 태스크)
```gdb
b *0x0c000ec6                  # __acle_se_SECURE_dma_m2m_start_svc 첫 명령 (mrs r3, ipsr)
c
p/x $xpsr & 0x1ff              # 0xb (SVCall): Handler mode 로 들어옴
p/x $msp_s                     # Secure 부팅 스택 안 (ST gdbserver 이름이 다르면 info registers)
bt                             # NS 쪽: dma_m2m_start_svc ← SVC_Handler (태스크의 svc #110)
delete
c   (몇 초) → Ctrl-C
p/x *(unsigned*)0xE000EDE4     # SFSR = 0
p/x *(unsigned*)0xE000ED28     # CFSR = 0
```
Thread mode 에서 NSC 를 직접 부르는 경로는 이 이미지에 없다 (tests/run_tests.sh T5: NS 의 mediator 호출은
`SVC_Handler` 안 한 곳뿐).

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
# 요청 1 회: vDmaTaskA 안 svc #110 과 다음 명령에 정지점 (disassemble vDmaTaskA 로 주소 확인)
# 컨텍스트 전환: PendSV_Handler(0x0804059c) 진입과 마지막 bx lr
```
정적 명령열 (`tests/run_tests.sh` T5 가 GCC baseline Secure ELF 와 비교):

| 경로 | baseline (GCC) | 이 빌드 |
|---|---|---|
| `__acle_se_SECURE_dma_m2m_start_svc` | 44 (성공 경로 35) | 동일 명령열 |
| `SecureContext_LoadContextAsm` / `SaveContextAsm` | 7 / 11 | 동일 명령열 |
| NS `SVC_Handler` → NSC 분기 | GCC veneer `ldr.w pc,[pc]` 1 명령 | lld thunk `movw/movt/bx r12` 3 명령 |

마지막 행만 다르다 (NS 링커 차이, 요청마다 1 회). 차이는 위 "요청 1 회" 측정으로 확인한다 [추정: ±1–2 cycle].
