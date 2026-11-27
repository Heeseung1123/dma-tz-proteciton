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
| 빠른 원인 분해, 문맥 전환 비용 | **8 조건 완료** (§6, §7). P1/P2 × 110/32 MHz × off/warm |
| **안전성** | **문제 발견 (§8).** context 없는 Task 에서 asm 중재자를 반복 호출하면, 인터럽트 프레임이 PSP_S = 0 에 쌓여 멈춘다 [실측]. 기본 빌드(`MEDIATOR_ASM=1`)도 오래 돌리면 같은 문제가 날 수 있다 [추정] |
| 후보 A (SVC 경로) | **구현, 보드 기능 확인 통과, 사이클 8 조건 완료** (§9). 기본 빌드가 이제 A 경로를 쓴다 (`configDMA_MEDIATOR_SVC`) |

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

## 6. asm 이 빠른 원인 [실측, N=1000, 바깥 중앙값 cycle]

**방법.** 같은 빌드(`BENCH_PHASE BENCH_DECOMP`, `results/decomp/*`) 안에서 asm 중재자(M0)에 C 경로가 하는 일을 하나씩 되돌려 넣고, 단계마다 바깥 값을 쟀다.
- **M1 = M0 + DMA 레지스터 작업 2개.** C 경로에만 있는 `CCR.EN=0` 읽기·수정·쓰기와 `IFCR=CGIF1` 쓰기를 넣었다.
- **M2 = M1 + FP 정리.** GCC 가 `cmse_nonsecure_entry` 복귀 때 만드는 정리 코드를 그대로 넣었다: `vmov.f32 s0–s15, #1.0` 16개, FPSCR 마스크 (`vmrs/and/vmsr`, `push/pop {r4}`).
  M2 는 `cbnz` 대신 `cmp + bne` 를 쓴다. FP 정리가 끼면 `cbnz` 의 전방 범위(126 B)를 넘기 때문이다. 이 때문에 M2 에 약 +1 cycle 이 더 들어간다.
- **C − M2.** 남은 차이는 컴파일러가 만든 코드 자체의 몫이다: 프롤로그/에필로그, 인자 스택 spill, literal pool 적재, `BL verify_inst`, 같은 레지스터 반복 적재.
- **교차 확인.** 빈 NSC 세 가지를 같이 쟀다.
  - L0: asm 빈 NSC
  - L1: L0 + 같은 FP 정리
  - nop3: C 빈 NSC `SECURE_nop3`

| 프로파일 | 클럭 | 캐시 | asm (M0) | M1 (+DMA 레지스터 2) | M2 (+FP 정리) | C | M1-M0 | M2-M1 | C-M2 | 빈 asm L0 | L1 (+FP 정리) | 빈 C nop3 | L1-L0 | nop3-L1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| P1 | 110 | off | 145 | 160 | 252 | 367 | +15 | +92 | +115 | 60 | 141 | 156 | +81 | +15 |
| P1 | 110 | warm | 64 | 71 | 100 | 186 | +7 | +29 | +86 | 23 | 47 | 57 | +24 | +10 |
| P1 | 32 | off | 69 | 77 | 117 | 180 | +8 | +40 | +63 | 29 | 61 | 72 | +32 | +11 |
| P1 | 32 | warm | 60 | 67 | 96 | 158 | +7 | +29 | +62 | 23 | 47 | 57 | +24 | +10 |
| P2 | 110 | off | 141 | 160 | 251 | 273 | +19 | +91 | +22 | 60 | 137 | 137 | +77 | +0 |
| P2 | 110 | warm | 72 | 64 | 93 | 139 | -8 | +29 | +46 | 23 | 47 | 47 | +24 | +0 |
| P2 | 32 | off | 67 | 77 | 113 | 135 | +10 | +36 | +22 | 29 | 62 | 62 | +33 | +0 |
| P2 | 32 | warm | 60 | 64 | 93 | 117 | +4 | +29 | +24 | 23 | 47 | 47 | +24 | +0 |

실행 명령 추적 [실측: `scripts/eval/trace_mediator.py`, 110 MHz off 빌드, NS veneer ~ NS 복귀를 stepi 로 따라감, `results/decomp/*/trace_{c,asm}.txt`]:

| 빌드 | 경로 | 실행 명령 | literal 적재 (ldr [pc]) | FP 정리 (vmov/vmrs/vmsr) | 스택·프레임 (push/pop/[sp]/[r7]) | 호출/복귀 (bl/bx, verify_inst) | TT 계열 | 나머지 |
|---|---|---|---|---|---|---|---|---|
| P1 | C | 99 | 11 | 18 | 19 | 2 | 4 | 45 |
| P1 | asm | 37 | 1 | 0 | 0 | 0 | 4 | 32 |
| P2 | C | 78 | 4 | 18 | 4 | 2 | 4 | 46 |
| P2 | asm | 37 | 1 | 0 | 0 | 0 | 4 | 32 |

- "literal 적재" 1 개는 두 경로 모두 NS long-branch veneer 의 `ldr.w pc, [pc]` 다. asm 쪽 Secure 코드에는 literal 적재가 없다 (`movw/movt` 사용).
- "나머지" 에는 DMA 레지스터 접근이 들어 있다. C 는 CCR 읽기 2, CCR 쓰기 2, CPAR, CM0AR, CNDTR, IFCR 이고, asm 은 CCR 읽기 1, CCR 쓰기 1, CPAR, CM0AR, CNDTR 이다 [코드].

### 6.1 결론

- **원인 1: FP 정리 (모든 조건, 가장 큰 단일 항목).** off 에서 +36 ~ +92, warm 에서 +29 다.
  같은 정리를 빈 NSC 에 넣은 L1 − L0 도 +24 ~ +81 로 거의 같다. asm 은 FP 명령을 쓰지 않으므로 이 정리가 필요 없다 [아키텍처 도출].
  정리 명령 18 개는 110 MHz off 에서 명령당 약 5 cycle 이 든다. 32-bit 명령 16 개를 Flash wait state 5 에서 가져와야 하기 때문으로 보인다 [추정].
- **원인 2: 컴파일러 코드 (-O0 에서 큼).** P1 은 C − M2 = +62 ~ +115 이고, P2(-O1) 는 +22 ~ +46 이다.
  P1 의 C 경로는 실행 명령 99 개 중 스택·프레임 19 개, literal 적재 11 개다. P2 는 각각 4 개, 4 개로 줄어든다 [실측: 추적].
  P2 에서도 남는 +22 ~ +46 은 다음에서 나온다: `BL/BX verify_inst` 왕복, literal 적재 3 개, `s_busy` 를 두 번 적재, 끝 주소를 레지스터에 남기려고 하는 재배치 [코드: 추적].
- **원인 3: DMA 레지스터 작업 2 개 (작음).** +4 ~ +19 다. asm 은 `s_busy == 0` 이면 EN == 0 이라는 사실을 이용해 이 두 작업을 뺐다 (gap-analysis H-3).
  P2/110/warm 은 −8 로 나왔다. 기계어가 같은 M0 의 이 조건 값이 P1 보다 높아 생긴 것으로, ICACHE 충돌로 보인다 [추정] (§4.1).
- **프롤로그 비용.** P1 빈 NSC 의 nop3 − L1 = +10 ~ +15 이고, P2 는 0 이다. -O1 에서는 빈 함수의 프롤로그가 없어진다 [실측].
- **요약 (P1/110/off).** C 367 − asm 145 = 222 는 다음처럼 나뉜다.

  | 항목 | cycle |
  |---|---|
  | DMA 레지스터 작업 | 15 |
  | FP 정리 | 92 |
  | 컴파일러 코드 | 115 |

  P2/110/off 는 132 = 19 + 91 + 22 로, FP 정리가 차이의 70 % 다.

## 7. 문맥 전환 비용 (Save/Load) [실측, N=1000, 중앙값 cycle]

**방법.** 같은 우선순위의 두 특권 Task 가 `taskYIELD()` 로 번갈아 돈다 (예전 `f262d8d^` 하니스와 같은 방식).
측정 구간은 나가는 Task 의 yield 직전(t0)부터 들어오는 Task 의 yield 복귀 직후(t1)까지다.
여기에는 PendSV, Secure Save/Load NSC (context 가 있을 때), FP 저장/복원 (FPCA = 1 일 때), 다음 Task 선택, MPU 영역 재설정이 들어간다.
매 회 yield 앞에서 Secure 호출을 한 번 한다. 이 호출은 구간 밖이다.

| 변형 | Secure context | yield 앞 호출 | 대응하는 실제 Task |
|---|---|---|---|
| V0 | 없음 | asm 중재자 (len 0, 즉시 거부) | asm 중재자를 쓰는 DMA Task |
| V1 | 있음 | 같음 (FPCA 0) | (분해용) |
| V2 | 있음 | C 빈 NSC `SECURE_nop3` (FP 정리 `vmov` 로 FPCA = 1) | C 중재자를 쓰는 DMA Task |

두 Task 방향(A→B, B→A)의 값이 번갈아 나온다 (예: 679 / 717). 그래서 연속한 두 표본의 평균을 내고 그 중앙값을 썼다.
처음 4 표본은 Task 첫 시작이 섞이므로 버렸다. 틱이 겹친 이상값(예: 1664)은 1 % 미만이다.

| 프로파일 | 클럭 | 캐시 | V0 context 없음 (asm) | V1 context + asm | V2 context + C NSC (FPCA) | V1-V0 (Save+Load) | V2-V1 (FP) | V2-V0 (C 경로 Task 추가분) | 두 방향 값 V0 / V2 |
|---|---|---|---|---|---|---|---|---|---|
| P1 | 110 | off | 698 | 1242 | 1345 | +544 | +103 | +647 | 679/717 / 1364/1326 |
| P1 | 110 | warm | 320 | 602 | 694 | +282 | +92 | +374 | 325/325 / 699/690 |
| P1 | 32 | off | 362 | 633 | 758 | +271 | +125 | +396 | 369/369 / 765/765 |
| P1 | 32 | warm | 306 | 546 | 666 | +241 | +120 | +361 | 310/301 / 671/671 |
| P2 | 110 | off | 698 | 1218 | 1321 | +520 | +103 | +623 | 717/679 / 1340/1340 |
| P2 | 110 | warm | 346 | 648 | 748 | +302 | +99 | +401 | 342/351 / 752/743 |
| P2 | 32 | off | 362 | 600 | 725 | +238 | +125 | +363 | 355/369 / 732/732 |
| P2 | 32 | warm | 310 | 522 | 648 | +213 | +126 | +339 | 314/310 / 653/653 |

- **Secure Save + Load (V1 − V0).** 전환 1 회에 +213 ~ +544 다. P1/110/off +544 는 예전 측정(983a749, `results/summary_phase45.md`) +544 와 같다 [실측].
- **FP 확장 프레임 (V2 − V1).** C NSC 를 쓴 Task 는 FPCA = 1 이 되어 전환마다 +92 ~ +126 이 더 든다.
  FP 확장 프레임과 PendSV 의 `vstmdb/vldmia s16–s31` 때문이다 [코드: portasm.c `tst lr, #0x10`].
  예전 메모리 측정의 "NS DMA Task 스택 +64 B" 와 같은 원인이다 (cmpMemUsage_result.md §5.6).
- **C 중재자 Task 의 전환당 추가 비용 (V2 − V0).** +339 ~ +647 이다. 요청 1 회의 차이(§4, 49 ~ 222)보다 크다.
  예를 들어 이 앱의 DMA Task 는 500/700 ms 마다 한 번 요청하고, 그 사이 delay 와 깨어남으로 최소 2 번 전환된다. 그러면 요청당 전환 비용이 요청 자체 비용보다 커진다 [추정: 전환 횟수는 세지 않음].

### 7.1 C 경로의 전체 비용 정리 (P1/110/off, cycle)

| 항목 | C 중재자 | asm 중재자 | 근거 |
|---|---|---|---|
| Secure context 할당 (Task 당 1 회) | 2229 | 0 | §5 |
| 요청 1 회 | 367 | 145 | §4 |
| 문맥 전환 1 회 | 1345 | 698 | §7 (V2, V0) |
| Secure heap | 1040 B / Task | 0 | §2 |

## 8. 안전성 문제: context 없는 Task 의 Secure 실행 중 예외 [실측 + 추정]

**관찰 [실측].** §7 측정 중 V0 (context 없음) 에서 측정이 자주 멈췄다. 8 조건에서 재시도가 여러 번 필요했다. 멈춘 시점의 레지스터는 다음과 같았다.
- PC: Secure `GTZC_IRQHandler`
- xPSR: 예외 24 (GTZC IRQ)
- `EXC_RETURN`: `0xFFFFFFFD` (Secure Thread, PSP 로 복귀)
- **PSP_S: `0xFFFFFFE0`**
- 현재 Task: `cx0`

즉 PSP_S = 0 인 상태에서 Secure Thread 실행 중에 예외가 들어왔다. 하드웨어가 0 − 32 에 프레임을 쌓았고, 복귀할 때 쓰레기 값을 꺼낸 것이다.
처음 V0 에 `cpsid` 가 전혀 없는 빈 asm NSC 를 썼을 때 763 회째에서 멈췄다. 실제 asm 중재자로 바꾼 뒤에도 108 회째 등에서 멈췄다.

**원인 [추정, 근거 있음].** PRIMASK_S 는 위험을 없애지 못하고 복귀 지점으로 미룰 뿐이다.
1. 본문을 실행하는 동안(마스킹 중) 들어온 인터럽트는 pending 으로 남는다.
2. 그 인터럽트는 `cpsie i` 직후, 아직 `bxns` 로 NS 로 돌아가기 전에, Secure 상태에서 받아들여질 수 있다.
3. 그러면 프레임이 PSP_S 에 쌓인다.

`cpsie` 뒤 몇 번째 명령 경계에서 pending 예외를 받는지는 확인하지 않았다 [아키텍처 근거 절 미확인].

**발생 빈도 추정.**
- SG ~ `cpsid` 창(2 ~ 3 cycle)만 위험하다면, 1000 회 호출 중 실패 확률은 약 3 % 다 (SysTick 1 kHz).
- 실제 관찰은 약 50 % 였고, 32 MHz 에서 더 잦았다. 틱 주기가 cycle 로 짧아지기 때문이다.
- "마스킹된 전체 시간 동안 들어온 인터럽트가 복귀 직전에 처리된다" 고 보면 실패율은 호출당 (마스킹 시간 / 틱 주기) ≈ 0.07 % 이고, 1000 회 기준 약 50 % 로 관찰과 맞는다 [추정].

**영향.**
- 기본 빌드(`MEDIATOR_ASM=1`)의 DMA Task 는 context 가 없다. 요청이 초당 약 3 회이므로, 위 확률이면 수십 분 안에 같은 방식으로 멈출 수 있다 [추정: 장시간 실행은 하지 않음].
- §2 기능 확인은 15 초 실행이라 이 문제를 잡지 못했다.
- 사이클 측정(§4 ~ §7)은 영향이 없다. bench Task 는 context 가 있어 PSP_S 가 유효하다.
- gap-analysis H-2 의 "알려진 구멍: SG ~ cpsid 창" 기술은 범위가 좁았다. 복귀 쪽 창이 더 크다.

**해결 후보 (결정 필요).**

| 후보 | 방식 | 장점 | 단점 |
|---|---|---|---|
| A | NS 가 SVC 를 거쳐 호출해 Secure Handler mode 에서 실행 | MSP_S 를 쓰므로 할당 0 이고 창이 없다. PendSV 는 SVC 가 끝난 뒤에 돈다 [아키텍처 도출] | NS SVC 경로 수정이 필요하고, SVC 진입·복귀 비용이 더해진다 |
| B | DMA Task 에 작은 stock context (예: 128 B) | 수정이 적다 | Save/Load 전환 비용 (§7 V1 − V0) 이 다시 생긴다. FP 비용은 asm 이라 없다 |
| D | guard-as-stack (`guardasstack.md`) | 가드 256 B 를 Secure 스택으로 쓰므로 할당이 0 이다 | PendSV 분기와 Suspend/Resume NSC 가 필요하다 |

## 9. 후보 A: SVC 경로 [구현 + 보드 실측]

### 9.1 구조

```
비특권 DMA Task ── svc #110 (r0-r2 = src, dst, len) ─▶ NS SVC_Handler (Handler mode)
   fast path: cmp #110 → push {r0, lr} → ldm 프레임 {r0-r2} → bl SECURE_dma_m2m_start_svc (veneer → SG)
        ─▶ Secure (Handler mode, MSP_S): IPSR 검사 → 본문 (asm 중재자와 같고 cpsid/cpsie 없음) → bxns
   ◀─ pop {r1, lr} → stacked r0 = 결과 → bx lr (예외 복귀)
```

| 파일 | 변경 |
|---|---|
| `portmacrocommon.h` | `portSVC_DMA_M2M_START` (110), `portSVC_BENCH_EMPTY` (111, BENCH_DECOMP 전용) |
| `portasm.c` | MPU v2 `SVC_Handler` 맨 앞에 fast path 를 넣었다. `configDMA_MEDIATOR_SVC == 1` 일 때만 생긴다. 다른 SVC 에는 `cmp/beq` 2 명령이 더해진다 |
| `NonSecure/Inc/FreeRTOSConfig.h` | `configDMA_MEDIATOR_SVC`. 기본 1 이고, `MEDIATOR_INSECURE` 와 `MEDIATOR_ASM=0` 에서는 0 이다 |
| `Secure/Src/secure_nsc.c` | `SECURE_dma_m2m_start_svc`: naked NSC 이고 r0-r3, r12 만 쓴다. Thread mode (IPSR = 0) 호출은 거부한다 |
| `NonSecure/Src/app_freertos.c` | `App_DmaStartSvc()` (inline `svc`). `App_DmaM2M` 이 이 함수를 쓴다. Thread mode asm/C 경로는 그대로 두었다 |

**왜 안전한가 [아키텍처 도출].**
- NS Handler mode 에서 SG 를 지나면 Secure 도 Handler mode 다. 그래서 Secure 실행 중에 들어온 예외 프레임은 PSP_S 가 아니라 MSP_S (Secure 부팅 스택)에 쌓인다.
- 이렇게 쌓인 프레임은 LIFO 로 중첩된다. PendSV 는 우선순위가 가장 낮아 이 SVC 가 끝난 뒤에 돌기 때문에, Secure 프레임이 남은 채로 Task 가 바뀌지 않는다.
- 원자성: SVC 는 다시 발생할 수 없고, DMA ISR 은 `s_busy` 를 1 → 0 으로만 바꾼다. 그래서 PRIMASK 가 필요 없다 [코드].

**남는 한계 [추정].** 악의적인 NS Task 가 SVC 를 거치지 않고 `SECURE_dma_m2m_start_svc` 를 Thread mode 에서 직접 부르면, IPSR 검사 자체가 Thread mode Secure 실행이다. 그래서 §8 과 같은 PSP_S 문제로 시스템을 멈출 수 있다 (DoS).
context 가 없는 Task 가 아무 NSC 나 부를 때 생기는 문제이고, stock Secure context NSC 도 같다. 기밀성·무결성 문제는 아니다.

### 9.2 기능 확인 [실측, `results/asm_func/svc/func_check.txt`]

| 확인 | 결과 |
|---|---|
| 정상 호출 | 21 회 전부 0 |
| doorbell 수 | 21 |
| Cluster | A/B 모두 `00..07` |
| Secure heap | 8152 (DMA Task context 할당 0) |
| 변조 6 종 | 전부 −1, `s_busy` = 0 |
| §8 문제 재현 여부 | §7 V0 (context 없는 Task, 매 회 중재자 호출, 1000 회 이상 전환) 이 8 조건 모두 끝났다. 남은 3 조건은 재시도도 timeout 도 없었다 |

Thread mode asm 경로에서는 같은 측정이 약 절반 확률로 멈췄다.
P2/110/warm 재시도는 gdbserver 연결 오류(`vMustReplyEmpty` timeout → 이후 "Target is not responding")였고, 타깃이 멈춘 것은 아니다.

### 9.3 요청 1 회 비용 [실측, N=1000, 바깥 중앙값 cycle, `results/svc/*`]

모두 같은 빌드(`BENCH_PHASE BENCH_DECOMP`)에서 쟀다. "빈 SVC" 는 fast path 에서 바로 `bx lr` 하는 SVC #111 이다 (SVC 진입·복귀 + 비교 4 개).

| 프로파일 | 클럭 | 캐시 | C (Thread) | asm (Thread, §8 문제) | **SVC 경로 (A)** | 빈 SVC | SVC − asm | SVC − 빈 SVC (Handler mode 중재자) | asm − 빈 asm NSC (Thread mode 중재자) | A / C | SVC 최대 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| P1 | 110 | off | 367 | 145 | **228** | 62 | +83 | 166 | 85 | 62 % | 649 |
| P1 | 110 | warm | 167 | 75 | **157** | 34 | +82 | 123 | 52 | 94 % | 362 |
| P1 | 32 | off | 180 | 69 | **115** | 36 | +46 | 79 | 40 | 64 % | 321 |
| P1 | 32 | warm | 154 | 61 | **117** | 34 | +56 | 83 | 38 | 76 % | 299 |
| P2 | 110 | off | 273 | 141 | **228** | 62 | +87 | 166 | 77 | 84 % | 644 |
| P2 | 110 | warm | 173 | 57 | **160** | 34 | +103 | 126 | 34 | 92 % | 370 |
| P2 | 32 | off | 135 | 67 | **115** | 36 | +48 | 79 | 39 | 85 % | 310 |
| P2 | 32 | warm | 125 | 57 | **118** | 34 | +61 | 84 | 34 | 94 % | 298 |

문맥 전환 1회 (V0 = context 없음 + SVC 경로 호출, V1 = context + asm, V2 = context + C NSC)

| 프로파일 | 클럭 | 캐시 | V0 (A Task) | V1 | V2 (C Task) | V2 − V0 |
|---|---|---|---|---|---|---|
| P1 | 110 | off | 709 | 1253 | 1356 | +647 |
| P1 | 110 | warm | 300 | 616 | 672 | +371 |
| P1 | 32 | off | 366 | 637 | 762 | +396 |
| P1 | 32 | warm | 300 | 558 | 664 | +364 |
| P2 | 110 | off | 709 | 1198 | 1301 | +592 |
| P2 | 110 | warm | 300 | 570 | 644 | +344 |
| P2 | 32 | off | 366 | 597 | 722 | +356 |
| P2 | 32 | warm | 300 | 510 | 622 | +322 |

- **A 는 C 보다 빠르고 Thread asm 보다 느리다.** C 의 62 ~ 94 % 다.
  P1/off 는 62 ~ 64 % 이고, P2(-O1) 와 warm 은 84 ~ 94 % 로 차이가 작다.
  - off: 110 MHz 에서 C 367 → A 228, 32 MHz 에서 180 → 115.
  - Thread asm 보다 +46 ~ +87 이 더 든다.
- **SVC 를 거치는 비용 (off, P1/110).** SVC − asm = +83 은 다음 둘로 나뉜다.
  1. 빈 SVC 62: SVC 진입과 예외 복귀, 그리고 비교 4 개.
  2. 나머지 21: fast path 명령(`push/ldm/bl/pop/str`)과 NS veneer.

  교차 확인하면 같은 값이 나온다. SVC − 빈 SVC = 166 (NSC 진입부터 중재자 본문, 복귀까지)이고, Thread mode asm 전체 145 (빈 asm NSC 60 + 본문 85)보다 21 많다.
  Handler mode 라서 중재자 본문이 느려지지는 않는다 [실측 값 + 추정 분해].
- **warm 에서 차이가 큰 이유 [추정].** P1/110/warm 은 SVC − 빈 SVC = 123 으로, Thread mode 75 보다 48 많다.
  NS `SVC_Handler` 가 `.privileged_functions` 에 있어 다른 코드와 1-way ICACHE 줄이 겹치는 것으로 보인다. `icache_mon.py` 로 확인하지는 않았다.
- **bench 조건 주의.** bench Task A 는 이전 C NSC 호출로 FPCA = 1 이다. 그래서 SVC 진입이 FP 확장 프레임이 된다 (빈 SVC 에도 같이 들어감).
  실제 A 경로 DMA Task 는 FP 명령을 실행하지 않으므로 FPCA = 0 이고, SVC 진입이 조금 더 쌀 것이다 [추정: 따로 재지 않음].
- **최댓값.** A 는 본문을 마스킹하지 않으므로, 틱 같은 더 높은 우선순위 예외가 끼면 최댓값이 커진다 (예: 649).

### 9.4 문맥 전환과 전체 비용

V0 는 이제 A 경로 Task 다 (context 없음, 매 회 SVC 경로 호출). V1 과 V2 의 정의는 §7 과 같다.
`portYIELD` 도 SVC(#105)라서 fast path 비교 2 명령이 더해진다. 그래서 V0 가 §7 의 698 에서 709 로 늘었다 (P1/110/off).

전체 비용 (P1/110/off, cycle):

| 항목 | C 중재자 | asm (Thread, §8 문제) | **A (SVC)** |
|---|---|---|---|
| Secure context 할당 (Task 당 1 회) | 2229 | 0 | **0** |
| 요청 1 회 | 367 | 145 | **228** |
| 문맥 전환 1 회 | 1356 | 698 | **709** |
| Secure heap / Task | 1040 B | 0 | **0** |
| PSP_S 문제 (§8) | 없음 | 있음 | **없음** (직접 호출하는 악성 Task 의 DoS 는 남음) |

### 9.5 정리 (8 조건, 중앙값 cycle)

| | 요청 1 회: A − C | 문맥 전환 1 회: A Task − C Task (V0 − V2) | context 할당 (§5) |
|---|---|---|---|
| P1 | −10 ~ −139 | −364 ~ −647 | −1086 ~ −2229 (Task 당 1 회) |
| P2 | −7 ~ −45 | −322 ~ −592 | −548 ~ −1281 |

- **요청 1 회만 보면 A 가 아끼는 양은 작다.** 특히 P2(-O1)에서 −7 ~ −45 다.
  A 는 FP 정리와 컴파일러 코드(§6)를 없앤 대신 SVC 비용(빈 SVC 34 ~ 62 + fast path)을 새로 낸다.
- **A 의 큰 이득은 Task 쪽 비용이다.**
  - 문맥 전환마다 Secure Save/Load 와 FP 프레임이 없다 (−322 ~ −647).
  - Secure context 할당이 없다.
  - Secure heap 을 Task 당 1040 B 아낀다.
  - 요청보다 전환이 훨씬 잦은 Task 일수록 차이가 커진다 [추정].
- **Thread mode asm (§4) 은 요청 1 회로는 가장 싸다.** 하지만 §8 문제 때문에 context 없는 Task 에서는 쓸 수 없다. A 는 그 대가로 요청당 +46 ~ +103 을 낸다.
