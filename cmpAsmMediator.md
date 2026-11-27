# 스택 없는 중재자 `SECURE_dma_m2m_start_asm`: 기능 확인과 사이클 오버헤드

근거 태그: [실측] 보드/빌드 측정, [코드] 레포 코드로 확인, [아키텍처 도출], [추정], 미확인.
설계와 제거 근거: `docs/gap-analysis.md` H. 구현: `Secure/Src/secure_nsc.c` (`MEDIATOR_ASM`, 기본 1).

## 0. 상태 (2026-11-27)

| 항목 | 상태 |
|---|---|
| 빌드 (8 변형) | 통과. `MEDIATOR_ASM=0` 바이너리는 변경 전과 동일 [실측: cmp] |
| 보드 기능 확인 | **통과** (asm, c 두 빌드 모두, §2) [실측] |
| 사이클 측정 | **24 조건 전부 완료** (§4). 첫 실행 중 ST-LINK USB 재연결 문제가 있어 USB 를 다시 꽂고 이어서 측정함 |
| Secure context 할당 비용 | **8 조건 완료** (§5, off/warm. cold 는 지원 안 함) |

## 1. 정적 비교 (빌드에서 확인한 사실)

| | C 중재자 `SECURE_dma_m2m_start` | asm 중재자 `SECURE_dma_m2m_start_asm` |
|---|---|---|
| 함수 길이 (진입 ~ 첫 `bxns`, 거부 분기 포함) | P1(-O0) 93 명령, P2(-O1) 65 명령 + `verify_inst` 17 명령 (BL 호출) [실측: objdump] | 33 명령 (P1, P2 같음: naked asm) [실측] |
| 성공 경로 실행 명령 | 미집계 | 29 (`cpsid` ~ `bxns`) [코드] |
| 스택 사용 | `push {r7,lr}` + `sub sp,#24` (P1) | 없음 |
| FP 정리 | `vmov.f32 s0–s15, #1.0` 16개 + FPSCR | 없음 (FP 명령 없음) |
| Secure context | DMA Task 마다 heap 1040 B | 할당 안 함 (BENCH_PHASE·BLXNS 제외) |
| 인터럽트 | 선점 가능 | PRIMASK_S 로 전 구간 마스킹 (SG ~ `cpsid` 창은 알려진 구멍) |

## 2. 기능 확인 (`scripts/eval/check_asm_mediator.py`)

빌드: `asm` = 기본(MEDIATOR_ASM=1), `c` = MEDIATOR_ASM=0 (대조군). 둘 다 P1, BENCH 매크로 없음, DMA Task 비특권.

절차:
1. flash → reset → 3 s 실행 → 정지. A/B Cluster 앞 8 B 를 0 으로 지운다.
2. NS veneer 진입, 호출 지점 복귀, NS doorbell ISR 에 hbreak (멈추지 않고 센다). 진입 4, 6, 8, 10, 12, 14 회차에서 인자를 변조한다.
3. 12 s 더 실행 → 정지 → `results/asm_func/<빌드>/func_check.txt`.

결과 (`results/asm_func/{asm,c}/func_check.txt`) [실측]:

| 확인 | 기대 | asm | c |
|---|---|---|---|
| 정지 위치 | HardFault/SecureFault 가 아님 | `prvIdleTask+16` | `prvCheckTasksWaitingTermination+58` (IDLE 안) |
| 정상 호출 | 전부 r0 = 0 | 20 회 전부 0 | 21 회 전부 0 |
| doorbell 수 | 정상 성공 수와 같다 | 20 | 21 |
| 복귀 직후 `s_busy` | 아래 설명 | 20 회 전부 1 | 21 회 전부 0 |
| Cluster A/B [0:8] | `00 01 02 03 04 05 06 07` (지운 뒤 다시 쓰임) | 둘 다 일치 | 둘 다 일치 |
| Secure `xFreeBytesRemaining` | asm 이 c 보다 2080 B 많다 | 8152 | 6072 (차이 2080) |
| a-1 다른 Task 의 Cluster (0x2001b700) | r0 = -1, s_busy = 0 | 통과 | 통과 |
| a-2 자기 globals (0x2001b900) | r0 = -1, s_busy = 0 | 통과 | 통과 |
| a-3 len 0x120 (가드까지) | r0 = -1, s_busy = 0 | 통과 | 통과 |
| a-4 len = 0 | r0 = -1, s_busy = 0 | 통과 | 통과 |
| a-5 len = 0xFFFFFFFC (오버플로) | r0 = -1, s_busy = 0 | 통과 | 통과 |
| a-6 Secure 주소 0x30000040 | r0 = -1, s_busy = 0 | 통과 | 통과 |

- **`s_busy` 차이와 중복 중단점.** asm 은 복귀 직후 항상 `s_busy` = 1 이고, c 는 항상 0 이다.
  8 B 전송은 중재자가 끝나기 전에 완료된다. c 는 인터럽트를 막지 않으므로 DMA ISR 이 Secure 안에서 먼저 실행되어 `s_busy` 를 지운다.
  asm 은 PRIMASK_S 로 ISR 을 `cpsie` 까지 미룬다. 그래서 NS 복귀 지점의 중단점이 먼저 걸리고, gdb 가 그 중단점을 넘으려고 single-step 하는 순간 ISR 이 들어가
  같은 PC 의 중단점이 한 번 더 걸렸다 (첫 실행에서 정상 호출을 40 으로 셈. 스크립트에서 중복을 걸러 다시 실행함, `duplicate return hits ignored = 20`).
  둘 다 PRIMASK_S 가 ISR 을 막고 있다는 간접 증거다 [실측 + 추정].

공격 (b) (검증 뒤 CNDTR 변조 → 가드 → TZIC 중단)은 이 스크립트에 없다. asm 경로는 TZIC/GTZC 핸들러와 CCR 설정을 바꾸지 않았다 [코드].

## 3. 사이클 측정 방법 (기존 `Mediator.md` 하니스 재사용)

- 빌드: `PHASE=mediator_asm scripts/eval/run_all.sh` → P1/P2 × 110/32 MHz × off/warm/cold × ts0/ts1 = 24 빌드, N=1000.
- 같은 빌드 안에서 C 중재자(`START`, id 3)와 asm 중재자(`START_ASM`, id 13)를 모두 잰다. 코드 배치가 같은 조건에서 비교된다.
- 8 B M2M, 측정 구간 동안 DMA IRQ 를 NVIC 에서 막는다 (기존과 같음).
- 바깥 (NS 호출 ~ 복귀) = ts0 빌드 열0 − min(NS_CALIB).
- 안쪽 = ts1 빌드 열1 − min(INNER_CALIB_ASM) (id 14). T1 은 `cpsid` 직후, T4 는 EN 직후이고, 기록 코드는 `movw/movt` 형식이다.
  C 쪽 안쪽은 기존대로 T1 이 프롤로그 뒤라서, 구간 정의가 조금 다르다.
- BENCH_PHASE 에서는 DMA Task 가 특권이고 Secure context 가 있다 (C bench 진입 함수 때문). asm 경로는 PSP_S 를 쓰지 않으므로 사이클에는 영향이 없다 [아키텍처 도출].
- 집계: `python3 scripts/eval/analyze_asm.py results/mediator_asm` → `results/mediator_asm/summary.md`.
- 이번 실행은 ST-LINK USB 재연결 문제 때문에 `run_all.sh` 대신 같은 조건·매크로를 쓰는 재개 스크립트로 이어서 했다.
  조건마다 gdbserver 를 새로 띄우고, 실패하면 2회 재시도하며, 이미 끝난 조건은 건너뛴다.
  전체를 다시 재려면 `PHASE=mediator_asm scripts/eval/run_all.sh` 를 쓴다.

## 4. 결과: 요청 1회 비용 [실측, N=1000, 단위 cycle, min / avg / max]

바깥 = NS 호출 ~ 복귀 (ts0 빌드), 안쪽 = Secure 안 T1 ~ T4 (ts1 빌드). 차이는 중앙값끼리 비교했다. 빈 NSC = `SECURE_nop3`.
C 쪽 값에는 Secure context 할당이 들어 있지 않다. 할당은 §5 에 따로 적었다.

| 프로파일 | 클럭 | 캐시 | 바깥 C | 바깥 asm | 바깥 차이 (asm-C, 중앙값) | 안쪽 C | 안쪽 asm | 안쪽 차이 | 빈 NSC (-NS 판독) | 보정: NS 판독, T1·T4 C, T1·T4 asm |
|---|---|---|---|---|---|---|---|---|---|---|
| P1 | 110 | off | 367 / 370.4 / 806 | 145 / 145.7 / 571 | -222 | 200 / 202.0 / 640 | 81 / 81 / 81 | -119 | 154 / 155.5 / 591 | 8, 22, 16 |
| P1 | 110 | warm | 168 / 168.2 / 190 | 57 / 57.4 / 288 | -111 | 105 / 105.2 / 126 | 35 / 35.2 / 68 | -70 | 56 / 56.1 / 159 | 3, 8, 6 |
| P1 | 110 | cold | 345 / 368.4 / 876 | 174 / 174 / 174 | -189 | 179 / 179 / 179 | 87 / 87 / 87 | -92 | 189 / 189.5 / 696 | 10, 9, 17 |
| P1 | 32 | off | 180 / 182.2 / 402 | 69 / 69.0 / 70 | -111 | 101 / 102.3 / 321 | 38 / 38 / 38 | -63 | 69 / 70.9 / 290 | 4, 10, 8 |
| P1 | 32 | warm | 152 / 153.9 / 369 | 57 / 57.8 / 247 | -95 | 89 / 90.9 / 307 | 35 / 35.2 / 44 | -54 | 56 / 56.9 / 286 | 3, 8, 6 |
| P1 | 32 | cold | 201 / 208.7 / 485 | 90 / 90 / 90 | -111 | 101 / 101.5 / 612 | 47 / 47 / 47 | -54 | 89 / 94.2 / 364 | 6, 14, 9 |
| P2 | 110 | off | 273 / 275.3 / 716 | 141 / 142.6 / 581 | -132 | 138 / 139.2 / 576 | 81 / 81 / 81 | -57 | 135 / 135.4 / 554 | 8, 15, 16 |
| P2 | 110 | warm | 126 / 126.5 / 354 | 98 / 98 / 98 | -28 | 78 / 78.5 / 346 | 54 / 54 / 54 | -24 | 73 / 73.7 / 514 | 3, 7, 7 |
| P2 | 110 | cold | 301 / 304.5 / 815 | 187 / 188.5 / 694 | -114 | 114 / 116.7 / 627 | 87 / 87 / 87 | -27 | 167 / 169.3 / 675 | 10, 17, 17 |
| P2 | 32 | off | 135 / 136.5 / 356 | 67 / 67 / 67 | -68 | 65 / 65 / 65 | 38 / 38 / 38 | -27 | 60 / 60.7 / 281 | 4, 7, 8 |
| P2 | 32 | warm | 113 / 114.4 / 322 | 64 / 64.9 / 256 | -49 | 58 / 58.4 / 440 | 39 / 39 / 39 | -19 | 55 / 56.1 / 319 | 3, 6, 6 |
| P2 | 32 | cold | 159 / 162.2 / 445 | 89 / 89.3 / 370 | -70 | 68 / 68 / 68 | 42 / 42 / 42 | -26 | 80 / 81.0 / 81 | 6, 9, 8 |

### 4.1 요약

- **바깥 비용 (중앙값).** asm 이 C 보다 얼마나 드는지를 % 로 적었다.

  | 프로파일 | 110 MHz off / warm / cold | 32 MHz off / warm / cold |
  |---|---|---|
  | P1 | 40 / 34 / 48 % | 38 / 38 / 45 % |
  | P2 | 52 / 78 / 62 % | 50 / 57 / 56 % |

  P2 는 C 쪽도 -O1 이라 차이가 작다 (-28 ~ -132). P1 은 -95 ~ -222 다.
- **asm 중재자 전체 비용은 빈 NSC 와 비슷하다.** P1/110/off 는 145 vs 154, P2/32/off 는 67 vs 60 이다.
  빈 NSC 에도 C 프롤로그와 FP 정리(`vmov.f32` 16개 + FPSCR)가 들어 있다 [코드: bench_ns.inc 주석].
  asm 은 둘 다 없으므로, 그만큼 아낀 비용이 검증과 DMA 설정 비용을 거의 메운다 [추정].
- **asm 은 기계어가 같으면 값도 같다.** naked asm 이라 -O0/-O1 에 상관없이 기계어가 같다.
  안쪽 값이 P1 과 P2 에서 같다: 110 MHz off 81, cold 87, 32 MHz off 38.
  110 MHz 와 32 MHz 의 차이는 Flash wait state 때문으로 보인다 (FLASH_ACR LATENCY 5 vs 1, env.txt) [실측 값 + 추정 원인].
- **asm 은 최댓값이 퍼지지 않는다.** 안쪽 max 가 min 과 같다 (예: 81/81/81). 본문이 PRIMASK_S 아래에서 돌아 틱 선점이 끼지 않기 때문이다 [아키텍처 도출].
  바깥 max(예: 571) 는 NS 쪽 T0 ~ SG 구간이나 복귀 뒤에 들어온 선점으로 보인다 [추정].
- **이상값: P2/110/warm.** 기계어는 같은데 asm 이 P1 보다 느리다 (바깥 98 vs 57, 안쪽 54 vs 35).
  P2 빌드의 코드 배치에서 1-way ICACHE 충돌 miss 가 생긴 것으로 보인다 [추정]. `measureISR_overhead.md` 에서도 같은 현상이 있었다.
  이번에는 `icache_mon.py` 로 확인하지 않았다. P2/32/warm 은 64 로 P1/32/warm 57 과 가깝다.
- **C 중재자 값을 `Mediator.md` 와 비교.** 같은 조건에서 +6 ~ +21 차이가 난다 (예: P1/110/warm 168 vs 147).
  bench 코드(START_ASM 표)를 더해 NS 배치가 바뀐 영향으로 보인다 [추정]. 같은 빌드 안에서 C 와 asm 을 비교하는 데는 영향이 없다.
- **안쪽 구간 정의가 조금 다르다.** C 는 T1 이 프롤로그 뒤이고, asm 은 `cpsid` 뒤다. 그래서 두 방식은 바깥 값으로 비교한다.

## 5. Secure context 할당 비용 (C 중재자 Task 의 1회 비용) [실측, N=1000]

C 중재자를 쓰는 DMA Task 는 처음 Secure 에 들어가기 전에 `portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE)` 를 한 번 호출해야 한다.
asm 중재자를 쓰는 Task 는 이 호출이 없다 (`app_freertos.c`, `MEDIATOR_ASM`).

**측정 구간.** 호출 ~ 복귀까지다. 안에서 일어나는 일은 다음 순서다 [코드: port.c portSVC_ALLOCATE_SECURE_CONTEXT].
1. SVC 진입
2. `vPortSVCHandler_C`
3. `SecureContext_AllocateContext` (NSC, Secure `pvPortMalloc(1024+8)`)
4. `SecureContext_LoadContext` (NSC)
5. 예외 복귀

**측정 방법.** `AllocateContext` 는 로드된 context 가 없을 때(PSPLIM_S = 0)만 할당한다. 그래서 매 회 context 가 없는 새 Task 를 만들고, 그 안에서 한 번 잰 뒤 Task 가 스스로 지운다.
IDLE 이 TCB 와 context 를 해제한 뒤에 다음 회를 돈다. 빌드는 `BENCH_PHASE BENCH_ALLOC` 이고 결과는 `results/alloc/*` 에 있다. 값에서 NS 판독 비용을 뺐다.
cold 조건은 재지 않았다. cold 는 매 회 ICACHE 를 비우는 `SECURE_bench_cold` 를 불러야 하는데, 이 함수는 C NSC 라 Secure 스택이 필요하고 측정 직전의 helper Task 에는 context 가 없다.

| 프로파일 | 클럭 | 캐시 | 할당 (min / avg / max) | NS 판독 보정 | C 호출 1회 (중앙값) | C 첫 요청 = 할당 + 호출 | asm 호출 1회 | 할당 = asm 호출 몇 회분 |
|---|---|---|---|---|---|---|---|---|
| P1 | 110 | off | 2229 / 2229.0 / 2233 | 8 | 367 | 2596 | 145 | 15.4 |
| P1 | 110 | warm | 1425 / 1468.0 / 1469 | 3 | 168 | 1636 | 57 | 25.8 |
| P1 | 32 | off | 1121 / 1121 / 1121 | 4 | 180 | 1301 | 69 | 16.2 |
| P1 | 32 | warm | 1074 / 1083.0 / 1086 | 3 | 152 | 1235 | 57 | 19.0 |
| P2 | 110 | off | 1281 / 1281 / 1281 | 8 | 273 | 1554 | 141 | 9.1 |
| P2 | 110 | warm | 726 / 726 / 726 | 3 | 126 | 852 | 98 | 7.4 |
| P2 | 32 | off | 584 / 584 / 584 | 4 | 135 | 719 | 67 | 8.7 |
| P2 | 32 | warm | 548 / 548.0 / 550 | 3 | 113 | 661 | 64 | 8.6 |

- **할당 1회는 asm 요청 7 ~ 26 회분이다** (P1 15 ~ 26, P2 7 ~ 9). P1/110/off 기준으로 할당 2229 이고, C 의 첫 요청은 할당 2229 + 호출 367 = 2596, asm 의 첫 요청은 145 다.
- **asm 은 할당이 없고 요청마다도 싸다.** 그래서 손익분기점은 없다. 두 비용 모두 asm 이 작다.
- **분포.** 할당값은 거의 일정하다. 대부분의 조건에서 max − min ≤ 4 이고, P1/32/warm 은 1074 ~ 1086 이다.
  P1/110/warm 은 중앙값 1469 인데 드물게 1425 가 나왔다. 원인은 확인하지 않았다 [미확인].
  Secure heap 에 다른 할당이 없는 상태에서 1040 B 블록을 같은 자리에 반복해서 할당하기 때문에 값이 일정한 것으로 보인다 [추정]. 실제 앱에서는 heap 상태에 따라 달라질 수 있다.
- **이 표에 넣지 않은 C 쪽 비용.**
  1. Task 삭제 시 `SecureContext_FreeContext` (SVC + NSC). 이 앱에서는 Task 를 지우지 않는다.
  2. 문맥 전환마다 PendSV 가 부르는 `SecureContext_SaveContext` / `LoadContext` (NSC 2회). 예전 측정에서 전환 1회에 +184 ~ +544 였다 [실측, 983a749 `results/summary_phase45.md`].
     asm 중재자 Task 는 context 가 없으므로 이 비용도 없다 [코드: portasm.c `cbz r0, save_ns_context`]. 이번 측정에서는 다시 재지 않았다.
