# 스택 없는 중재자 `SECURE_dma_m2m_start_asm`: 기능 확인과 사이클 오버헤드

근거 태그: [실측] 보드/빌드 측정, [코드] 레포 코드로 확인, [아키텍처 도출], [추정], 미확인.
설계와 제거 근거: `docs/gap-analysis.md` H. 구현: `Secure/Src/secure_nsc.c` (`MEDIATOR_ASM`, 기본 1).

## 0. 상태 (2026-11-27)

| 항목 | 상태 |
|---|---|
| 빌드 (8 변형) | 통과. `MEDIATOR_ASM=0` 바이너리는 변경 전과 동일 [실측: cmp] |
| 보드 기능 확인 | **미실행.** ST-LINK 가 USB 에 보이지 않는다. `lsusb` 에 `0483:5740 Virtual COM Port` 만 있고, `~/st-link.sh` 는 "No ST-LINK found" (exit 20) |
| 사이클 측정 | **미측정.** 같은 이유. 하니스와 집계 스크립트는 준비됨 (§3) |

보드를 연결한 뒤 `~/st-link.sh &` 를 띄우고 `scripts/eval/run_asm_eval.sh` 를 실행하면, §2·§4 의 표를 채울 결과가 생긴다.

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

합격 기준 (기대값):

| 확인 | 기대 | asm | c |
|---|---|---|---|
| 정지 위치 | HardFault/SecureFault 가 아님 (IDLE 등) | 미실행 | 미실행 |
| 정상 호출 | 전부 r0 = 0. 12 s 동안 A 약 24회 + B 약 17회, 변조 6회 제외 [추정: 500/700 ms 주기] | | |
| doorbell 수 | 정상 성공 수와 같다 | | |
| 복귀 직후 `s_busy` | 1 (전송 중, ISR 이 0 으로 지움) | | |
| Cluster A/B [0:8] | `00 01 02 03 04 05 06 07` (지운 뒤 다시 쓰임 = DMA 동작) | | |
| Secure `xFreeBytesRemaining` | asm 이 c 보다 2080 B 많다 (2 × 1040) | | |
| a-1 다른 Task 의 Cluster | r0 = -1, s_busy = 0 (NS-MPU, TTAT) | | |
| a-2 자기 globals | r0 = -1, s_busy = 0 (S-MPU, TTT) | | |
| a-3 len 이 가드까지 | r0 = -1, s_busy = 0 (끝점 NS-MPU 다름) | | |
| a-4 len = 0 | r0 = -1, s_busy = 0 (asm: BCS. C: `len == 0` 검사) | | |
| a-5 주소 오버플로 | r0 = -1, s_busy = 0 (BCS) | | |
| a-6 Secure 주소 | r0 = -1, s_busy = 0 (TTAT RW 없음) | | |

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

## 4. 결과

미측정 (§0). 측정 후 `results/mediator_asm/summary.md` 의 표를 여기에 옮기고, 차이 원인을 적는다.

표 열: 프로파일 | 클럭 | 캐시 | 바깥 C | 바깥 asm | 바깥 차이 (asm−C, 중앙값) | 안쪽 C | 안쪽 asm | 안쪽 차이 | 빈 NSC | 보정값.

참고 기준 (기존 측정, 같은 하니스): C 중재자 값은 `Mediator.md` / `results/mediator/*` 와 같아야 한다.
다르면 NS 배치 변경(1-way ICACHE 충돌)을 먼저 의심한다. `Mediator.md` 에서 이미 1~37 사이클 변동을 관찰했다 [실측].
