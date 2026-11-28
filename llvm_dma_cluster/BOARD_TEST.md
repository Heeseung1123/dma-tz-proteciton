# 보드 확인 절차 (4단계) — 1차 실행 2026-11-28 (공유 cluster 별도 guard 이미지), 결과: VERIFY_LOG.md "보드 확인", 스크립트: tests/board/

**2026-11-28 개정 (D8, D9, D10): GTZC_IRQHandler 복구 + Task A struct(globals+dst) + Task B cluster 끝 공유 버퍼.**
아래 주소·기대값은 개정 이미지 기준이다. 2026-11-28 보드에서 모두 확인함 (결과: resultMemoryAuto.md §1).

대상 이미지: `build/secure/secure.elf` + `build/ns/ns.elf` (`scripts/build_all.sh` 산출물).
Mediator 는 프로젝트와 같은 SVC 경로다: 태스크 `svc #110` → NS `SVC_Handler` fast path (`dma_m2m_start_svc`
레이블) → Handler mode 에서 `SECURE_dma_m2m_start_svc` (secure_nsc.c, asm). landing pad / Secure 스택 패치 없음.
아래 주소·기대값은 그 빌드에서 읽은 값이다. 다시 빌드하면 `llvm-nm` 으로 바뀐 것이 없는지 먼저 확인한다.

| 구간 | 주소 | 내용 |
|---|---|---|
| c0 앞 칸 | 0x20018000–0x200180a0 | 일반 NS .bss (htim6, uwTick, …) |
| c0 head | 0x200180a0–0x20018100 | `s_taskA.priv` (Task A globals, 96 B) — NS-MPU 는 허용, S-MPU 태그 밖 |
| c0 태그 (RNR4) | 0x20018100–0x20018200 | `s_taskA.dst` 256 B |
| guard A | 0x20018200–0x20018300 | MPCBB Secure (블록 386) |
| c1 앞 칸 | 0x20018300–0x200183c0 | `s_privB` (Task B globals, 96 B) |
| c1 태그 (RNR5) | 0x200183c0–0x20018500 | `s_dmaDstB` 256 B + `s_dmaShared` 64 B @0x200184c0 |
| guard B | 0x20018500–0x20018600 | MPCBB Secure (블록 389) |

NS-MPU (DMA 태스크 configurable region): Task A = {0x200180a0, 0x160} (priv+dst) + {0x200184c0, 0x40} (공유),
Task B = {s_privB, 0x60} + {0x200183c0, 0x140} (dstB+공유). 둘 다 2/3.
보드에 올리기 전: `python3 scripts/check_pair.py build/secure/secure.elf build/ns/ns.elf` → `예상 = 0`.

## 0. 올리기
```bash
~/st-link.sh &                                   # ST-LINK_gdbserver, :61234
gdb-multiarch -nx -batch -x tests/board/s1_3_boot_dma_attack.gdb   # 1–3 단계
gdb-multiarch -nx -batch -x tests/board/s4_5_svc_write_guard.gdb   # 5, 4 (쓰기)
gdb-multiarch -nx -batch -x tests/board/s4b_read_guard.gdb         # 4 (읽기)
gdb-multiarch -nx -batch -x tests/board/s4c_shared_guard.gdb       # 4c (공유 버퍼)
```
(ST gdbserver 는 `monitor reset halt` 를 받지 않으므로 스크립트는 `monitor reset` 을 쓴다. 이미지 적재는
CubeIDE 또는 `load` 로 먼저.)

## 1. 부팅: `dma_boot_config()` == 0
```gdb
b dma_boot_config
c
finish                         # Value returned = 0
p g_dma_boot_status            # 0   (1 이면 아직 실행 전, <0 이면 Error_Handler 에서 정지)
set $i = 4
while $i < 6
  set *(unsigned*)0xE000ED98 = $i
  printf "rnr %d: RBAR=%08x RLAR=%08x\n", $i, *(unsigned*)0xE000ED9C, *(unsigned*)0xE000EDA0
  set $i = $i + 1
end
# 기대: rnr 4: 20018103 200181e1 / rnr 5: 200183c3 200184e1
p/x *(unsigned*)0x50032D30     # MPCBB1 VCTR[12] = 0x00000024 (블록 386/389 = guard 2 개만 Secure)
```

## 2. 정상 DMA (1–2 분 실행 후 Ctrl-C)
```gdb
p s_taskA.priv.okCount         # 약 2 회/초 증가
p s_privB.okCount              # 약 1.4 회/초 증가
x/8bx &s_taskA.dst             # 00 01 02 03 04 05 06 07
x/8bx &s_dmaDstB               # 80 81 82 83 84 85 86 87
x/8bx &s_dmaShared             # 00..07 (A)    x/8bx ((char*)&s_dmaShared)+32 → 80..87 (B)
p/x s_taskA.priv.v.dmaStatus   # 0 (TC)
```
LED10 은 Plain_Task 가 200 ms 마다 토글 (Secure context 를 가진 태스크의 stock Load/Save 경로).

## 3. 요청 시점 공격 (논문 §4 (a)) — Task A 시작 시 1 회 자동 실행
```gdb
p s_taskA.priv.attack          # {0, -1, -1, -1, -1, -1, 0, -1}
```
[0] 정상, [1] Task B cluster (TTAT), **[2] 같은 struct 의 globals `s_taskA.priv`: NS-MPU region 안이라 TTAT 는
통과하지만 S-MPU 태그 밖이라 TTT 가 거부**, [3] 길이 257 (끝이 guard A), [4] 끝이 cluster 밖, [5] Secure 주소,
[6] 공유 버퍼 정상, [7] 공유 버퍼 끝 + 4 B (끝이 guard B: TTAT).

## 4. 전송 시점 guard (논문 §4 (b)) — 검증 통과 뒤 길이 변조
```gdb
b *0x0c000f20                  # CCR.EN = 1 (str.w r12,[r3]) 직전, __acle_se_SECURE_dma_m2m_start_svc
c
p/x $r1                        # dst. 0x20018100 (Task A) 인 정지점까지 c 반복
set *(unsigned*)0x50020010 = 0x08040000   # CPAR1 = NS flash (source)
set *(unsigned*)0x5002000C = 0x210     # CNDTR1: 8 → 528 B (cluster 256 + guard 256 + 16 B 너머)
x/4wx 0x20018300               # (전) guard 너머 = s_privB 앞부분 기록
delete
c   (몇 초) → Ctrl-C
p/x *(unsigned*)0x50032818     # TZIC SR3: bit4 SRAM1F (GTZC_IRQHandler 가 지우기 전이면 1)
x/4wx 0x20018300               # (후) 위와 같아야 함 = guard 너머(Task B globals) 무변조
x/4wx 0x30018200               # Secure 에서 본 guard A: NS DMA 쓰기 무시 → 이전 값 그대로
p/x s_taskA.priv.v.dmaStatus   # 2 (guard 위반, GTZC_IRQHandler 가 통지)
p s_busy                       # 0 (채널 반납)
p s_taskA.priv.okCount         # 계속 증가 (위반된 1 회만 빠짐: App_DmaM2M = -2). 복구 전 이미지 [실측]: 영구 정지
p s_privB.okCount              # 계속 증가
```
읽기 방향 (s4b): 같은 정지점에서 CPAR1 = 0x20018200 (guard A), CNDTR1 = 16.
[실측 2026-11-28, 이전 이미지] TEIF 는 나지 않는다: 0 을 읽어(RAZ) 8 B 전송 뒤 GTZC_IRQHandler 가 EN=0, DMA ISR = 0x5 (GIF/HTIF), Task A 영구 대기.
개정 이미지 기대: 같은 지점에서 GTZC_IRQHandler 가 CGIF1 을 지우고 s_busy = 0, dmaStatus = 2, Task A 계속 실행.
(CLAUDE.md 의 "읽기 → TEIF" 와 다름, VERIFY_LOG D7)

### 4c. 공유 버퍼 → guard B (s4c)
Task A 의 `s_dmaShared` 요청 (r1 = 0x200184c0) 에서 CNDTR1 = 0x150 (336 B = 공유 64 + guard 256 + 16 B 너머).
첫 정지가 부팅 시 공격 시험 [6] 이면 attack[6] 은 −2 가 된다 (정상).
기대: guard B 너머 `x/4wx 0x20018600` 무변조, `x/4wx 0x30018500` (guard B) 그대로, s_busy = 0,
`s_taskA.priv.v.dmaStatus` = 2, A·B okCount 계속 증가. 공유 버퍼에서 넘친 쓰기는 guard 를 새로 쓰지 않고
Task B 의 guard 에서 멈춘다.

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
