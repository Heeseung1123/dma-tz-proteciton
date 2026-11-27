# 스택 없는 중재자 `SECURE_dma_m2m_start_asm`: 기능 확인과 사이클 오버헤드

근거 태그: [실측] 보드/빌드 측정, [코드] 레포 코드로 확인, [아키텍처 도출], [추정], 미확인.
설계와 제거 근거: `docs/gap-analysis.md` H. 구현: `Secure/Src/secure_nsc.c` (`MEDIATOR_ASM`, 기본 1).

## 0. 상태 (2026-11-27)

| 항목 | 상태 |
|---|---|
| 빌드 (8 변형) | 통과. `MEDIATOR_ASM=0` 바이너리는 변경 전과 동일 [실측: cmp] |
| 보드 기능 확인 | **통과** (asm, c 두 빌드 모두, §2) [실측] |
| 사이클 측정 | **24 조건 중 17 조건 완료** (§4). 남은 조건: P2_110M_cold_ts1, P2_32M 전체(6). ST-LINK 가 측정 도중 USB 를 반복 재연결하다 `DEV_USB_COMM_ERR` 상태가 되어 중단됨. USB 를 다시 꽂은 뒤 이어서 측정할 수 있다 (§3 끝) |

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
  남은 조건은 `PHASE=mediator_asm scripts/eval/run_all.sh` 로 다시 돌리면 된다 (완료된 조건도 다시 잰다).

## 4. 결과 [실측, N=1000, 단위 cycle, min / avg / max]

바깥 = NS 호출 ~ 복귀 (ts0 빌드), 안쪽 = Secure 안 T1 ~ T4 (ts1 빌드). 차이는 중앙값끼리. 빈 NSC = `SECURE_nop3`.

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
| P2 | 110 | cold | 미측정 |
| P2 | 32 | off | 미측정 |
| P2 | 32 | warm | 미측정 |
| P2 | 32 | cold | 미측정 |

### 4.1 요약

- **바깥 비용이 줄었다 (중앙값).** asm 이 C 의 몇 %인지로 보면 다음과 같다.
  - P1: 34 ~ 48 % (−95 ~ −222).
  - P2/110: off 52 % (273 → 141), warm 78 % (126 → 98), cold 62 % (301 → 187, −114).
  - P2/110/cold 는 ts0 빌드만 측정되어, 위 표에는 미측정으로 나온다. 바깥 값만 여기 적었다.
- **asm 중재자 전체 비용은 빈 NSC 와 비슷하다.** P1/110/off 기준 145 vs 154, warm 57 vs 56, P2/110/off 141 vs 135.
  빈 NSC 에도 C 프롤로그와 FP 정리(`vmov.f32` 16개 + FPSCR)가 있다 [코드: bench_ns.inc 주석]. asm 은 이 둘이 없으므로, 아낀 몫이 검증과 DMA 설정 비용을 거의 상쇄한다 [추정].
- **asm 은 코드가 같으면 값도 같다.** naked asm 이라 -O0/-O1 과 상관없이 기계어가 같고, 안쪽 off 값이 P1, P2 모두 81 이다.
  110 MHz 81 vs 32 MHz 38 의 차이는 Flash wait state 차이로 보인다 (FLASH_ACR LATENCY 5 vs 1, env.txt) [실측 값 + 추정 원인].
- **asm 은 최댓값이 퍼지지 않는다.** 안쪽 max 가 min 과 거의 같다 (예: P1/110/off 81/81/81, cold 87/87/87). 본문이 PRIMASK_S 아래에서 실행되어 틱 선점이 끼지 않기 때문이다 [아키텍처 도출].
  바깥 max(예: 571)는 NS 쪽 T0 ~ SG 구간이나 복귀 뒤에 들어온 선점이다 [추정].
- **이상값: P2/110/warm.** 기계어가 같은데 asm 이 P1 보다 느리다 (바깥 98 vs 57, 안쪽 54 vs 35). 그래서 차이가 −28 로 작다.
  P2 빌드의 코드 배치에서 1-way ICACHE 충돌 miss 가 생긴 것으로 보인다 [추정]. `measureISR_overhead.md` 에서도 같은 현상을 봤다. 이번에는 `icache_mon.py` 로 확인하지 않았다.
- **C 중재자 값과 `Mediator.md` 비교.** 같은 조건에서 +6 ~ +21 차이가 난다 (예: P1/110/warm 168 vs 147).
  bench 코드(START_ASM 표)를 추가해 NS 배치가 바뀐 영향으로 보인다 [추정]. 같은 빌드 안의 C 대 asm 비교에는 영향이 없다.
- **구간 정의 차이.** 안쪽 구간은 C 와 asm 이 조금 다르다. C 는 T1 이 프롤로그 뒤, asm 은 `cpsid` 뒤다. 두 방식을 비교할 때는 바깥 값을 기준으로 삼는다.
- **남은 조건.** 미측정 7 조건(P2/110/cold ts1, P2/32 전체)은 다시 연결한 뒤 채운다.
