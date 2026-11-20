# DMA 완료 ISR 오버헤드 측정 결과: Secure DMA (CH1 + doorbell) vs Insecure DMA (CH2)

측정일 2026-11-20. 보드 STM32L562E-DK, ST-LINK V3, gdbserver `~/st-link.sh`. 커밋 `a836b4c` (빌드 manifest `dirty=` 없음).
방법과 스탬프 위치는 `cmpISRoverhead.md` §12. 이 문서는 **결과**다.

표기: [실측] 보드 측정 / [코드] 소스·리스팅 / [도출] 아키텍처 규칙에서 유도 / [추정] 근거 부족 / 미확인.
값은 모두 CPU 사이클(110 MHz), N = 1000 (워밍 1 회 제외), 특별한 말이 없으면 **중앙값**.

---

## 0. 요약

| 조건 | S (Secure DMA) | N (Insecure DMA) | **S − N** | 비율 |
|---|---|---|---|---|
| P2 / 110 MHz / **ICACHE warm** (헤드라인) | 273 | 241 | **+32** | 1.13x |
| P2 / 110 MHz / **ICACHE off** (대조) | 573 | 505 | **+68** | 1.13x |

- 구간: TC 인터럽트가 이미 pending 인 상태에서 `cpsie` → HW 진입 → ISR(들) → HW 복귀까지. 전송 시간과 계측 비용은 뺐다 [코드, §12.1–12.2].
- **Secure DMA 경로가 완료 처리 1 회에 더하는 비용은 warm 에서 32 사이클(0.29 µs), off 에서 68 사이클(0.62 µs)** 이다 [실측].
  두 조건 모두 N 대비 13 % 증가다.
- 더해지는 비용은 Secure ISR 본문과 Secure 퇴장 + tail-chain 에서 생긴다. 그 일부는 N 이 NS ISR 에서 하는
  DMA 레지스터 작업(ISR 판독, EN=0, IFCR)이 Secure 로 옮겨간 것이라 상쇄된다 (§2) [실측].
- 두 경로 모두 비용의 대부분(warm 200/241, off 434/462)은 **공통 NS 통지 본문** (`App_DmaNotifyOwner` →
  `MPU_vTaskGenericNotifyGiveFromISR`)이다 [실측]. 보안 경로에 따른 차이보다 RTOS 통지 비용이 훨씬 크다.
- **주의 (§4):** ICACHE 가 1-way (direct-mapped) 라서 warm 값에는 iteration 당 4–5 회의 conflict miss 가 들어 있다.
  N 경로는 코드 배치에 민감해서, 스탬프를 넣은 빌드(k=3–5)는 miss 가 2–4 회 늘어난다. 따라서 **warm 의 N 구간 분해는 신뢰할 수 없다.**
  warm 의 S 분해와 두 경로의 총합(k=0), 그리고 off 의 전체 분해는 검증을 통과했다.

---

## 1. 측정 구성

| 항목 | 값 |
|---|---|
| 빌드 | `scripts/eval/build_variant.sh isr P2_110M_{warm,off}_its{k} P2 BENCH_PHASE=1 [BENCH_ICACHE] BENCH_ISR_TS=k`, k = 0..5, 모두 layout ok |
| 프로파일 | P2 (Secure -O1, NS -O1), arm-none-eabi-gcc 14.3.1 |
| 환경 레지스터 [실측 `env.txt`] | SystemCoreClock = 110000000, FLASH_ACR = 0x5 (LATENCY 5 WS, 프리페치 없음), ICACHE_CR = 0x1 (warm: EN=1, WAYSEL=0 → 1-way) / 0x0 (off), DWT_CTRL = 0x40000001, DAUTHSTATUS = 0xff |
| 경로 S | DMA1_CH1, SECM=1, 중재자 `SECURE_dma_m2m_start` 로 시작, IRQ 29 Secure → Secure ISR → doorbell pend (IRQ 105) → tail-chain → NS doorbell ISR |
| 경로 N | DMA1_CH2, SECM=0 (BENCH_ISR_TS 빌드에서만), NS 가 직접 설정, IRQ 30 NS → NS ISR |
| 공통 | 8 B M2M, src 0x20038000 → Task A Cluster dst. 두 NS ISR 은 같은 통지 본문 `App_DmaNotifyOwner()` 를 쓴다 [코드] |
| 실행 | `PHASE=isr scripts/eval/run_all.sh` (warm), off 는 같은 루프에서 `BENCH_ICACHE` 만 뺐다 |
| 집계 | `python3 scripts/eval/analyze_isr.py results/isr [P2_110M_warm|P2_110M_off]` → `results/isr/summary.md`, `summary_off.md` |

---

## 2. 결과

### 2.1 구간 전체 (k=0 빌드, 스탬프 없음)

| 조건 | 경로 | ISR = t1 − T0 − CAL: 중앙값 (min / avg / max) | CAL | 중앙값 표본 수 |
|---|---|---|---|---|
| warm | S | **273** (263 / 274.7 / 537) | 9 | 985 / 1000 |
| warm | N | **241** (241 / 242.1 / 450) | 9 | 990 / 1000 |
| off | S | **573** (573 / 579.3 / 991) | 26 | 983 / 1000 |
| off | N | **505** (505 / 508.7 / 923) | 26 | 990 / 1000 |

- 분포가 매우 좁다. 표본의 98–99 % 가 중앙값과 같은 값이다. max 는 SysTick 이 섞인 소수 표본이다 [실측 / 추정: 원인].
- CAL 은 S, N 이 같다 (warm 9 / 9, off 26 / 26). 구간 명령열이 같다는 확인이다 [실측].
- 재현성: k=1, 2 빌드는 Secure 코드만 바뀌고 NS ELF 는 같다. 이 빌드들의 N 총합도 241 로 같다 [실측 summary.md §4].

### 2.2 구간 분해: ICACHE off (모든 검증 통과, 깨끗한 분해)

| 구간 | S | N | S − N | 스탬프 |
|---|---|---|---|---|
| HW 진입 | 20 (NS→S) | 14 (NS→NS) | +6 | T0 → Ta (N: T0 → Tn) |
| Secure ISR 본문 | 86 | — | +86 | Ta → Tb |
| Secure 퇴장 + tail-chain | 14 | — | +14 | Tb → Tn |
| NS prologue | 7 | 12 | −5 | Tn → Tc |
| NS 본문 | 434 | 462 | −28 | Tc → Td |
| NS epilogue + HW 복귀 | 12 | 17 | −5 | Td → t1 |
| **합** | **573** | **505** | **+68** | = 구간 전체 |

### 2.3 구간 분해: ICACHE warm (S 만 신뢰, N 은 참고)

| 구간 | S | N (참고, §4) | S − N (참고) |
|---|---|---|---|
| HW 진입 | 12 | 12 | +0 |
| Secure ISR 본문 | 38 | — | +38 |
| Secure 퇴장 + tail-chain | 7 | — | +7 |
| NS prologue | 3 | 4 | −1 |
| NS 본문 | 200 | 241 | −41 |
| NS epilogue + HW 복귀 | 13 | **−16** | +29 |
| **합** | **273** | **241** | **+32** |

- N 의 마지막 구간이 음수다. 스탬프 지점 Td(257)가 k=0 총합(241)보다 크기 때문이다. 원인은 §4 의 배치 효과다.
  `analyze_isr.py` 검증 표에서 "N: P_k 증가 < ISR" 이 **확인 필요**로 나온 것도 같은 원인이다 [실측].
- S 의 스탬프 빌드는 총합이 k=0 대비 +8 – +12 다. 이는 스탬프 1 개의 비용(CAL ≈ 9)과 맞는다. 즉 S 쪽 배치는 안정적이다 [실측 summary.md §4].

### 2.4 S − N 의 해석

- **Secure 가 새로 하는 일:** HW 진입 차이 + Secure ISR 본문 + Secure 퇴장/tail-chain.
  off 에서 +6 + 86 + 14 = **+106**, warm 에서 0 + 38 + 7 = **+45** [실측].
  - Secure ISR 본문: 원인 판정, EN=0, IFCR, `s_busy=0`, NS 상태 워드 store, ISPR3 store (TC 경로 21 명령, §12.4) [코드].
  - 퇴장 + tail-chain: `bx lr`(EXC_RETURN) → 같은 우선순위의 pending doorbell 로 tail-chain → NS 벡터.
    warm 7 사이클은 상태 컨텍스트 push 없이 tail-chain 된 것과 맞는다 [도출: 배경이 NS thread 이고 Secure ISR → NS ISR 로 tail-chain].
- **Secure 로 옮겨가서 NS 에서 사라진 일:** N 의 NS ISR 은 DMA 레지스터 작업(원인 판정, EN=0, IFCR)을 직접 한다.
  S 의 doorbell ISR 은 상태 워드 하나만 읽는다. 그래서 NS 본문은 S 가 더 짧다. off 에서 −28 사이클 [실측].
  (warm 의 −41 은 §4 때문에 과대 추정일 수 있다.)
- 순 비용 = (Secure 고유분) − (옮겨간 NS 작업) ≈ off 106 − 38 = **68**.

---

## 3. 검증

| 항목 | warm | off |
|---|---|---|
| S: 0 < P_k 단조 증가 < ISR | OK (12, 50, 57, 60, 260 < 273) | OK (20, 106, 120, 127, 561 < 573) |
| N: 0 < P_k 단조 증가 < ISR | **실패** (12, 16, 257 > 241) → §4 | OK (14, 26, 488 < 505) |
| CAL 표 열1 = 0 (보정 중 ISR 미실행) | OK | OK |
| k=1, 2 의 N 열1 = 0 (N 은 Secure ISR 을 안 거침) | OK | OK |
| CAL S = N | 9 = 9 | 26 = 26 |
| 스탬프 1 개 추가 시 총합 증가 ≈ CAL | S: +8 – +12 OK / **N: +33 – +51 실패** | S: +20 – +31, N: +17 – +28 OK |

---

## 4. ICACHE 배치 효과 (warm 의 N 이상치 원인)

### 4.1 현상
warm 에서 N 경로에 스탬프 1 개를 넣으면 총합이 +33 (k=3), +51 (k=4), +45 (k=5) 늘었다.
스탬프 비용(≈ 9)의 4–5 배다. 반면 같은 빌드의 S 경로는 +8 – +12 였다.
off 에서는 두 경로 모두 스탬프 비용만큼(+17 – +31)만 늘었다 [실측]. 따라서 원인은 캐시다.

### 4.2 ICACHE 모니터로 확인 [실측]
`scripts/eval/icache_mon.py`: `bench_hit` 사이(표 하나 = 1001 iteration, 표 안에서는 멈추지 않음)의
ICACHE_HMONR/MMONR. 모니터 enable/reset 은 CR 비트 16–19 다 (`stm32l562xx.h`, HAL `HAL_ICACHE_Monitor_Reset`) [코드]. RM0438 ICACHE 장, 절 번호 미확인.

| 빌드 k | 표 10 isr_s miss / iter | 표 11 cal_n miss / iter | 표 12 isr_n miss / iter |
|---|---|---|---|
| 0 | 10.2 | 0.01 | **4.07** |
| 1 | 10.2 | 0.01 | 4.07 |
| 2 | 10.2 | 0.01 | 4.07 |
| 3 | 10.2 | 0.01 | **6.03** |
| 4 | 10.2 | 0.02 | **8.07** |
| 5 | 10.2 | 0.01 | **8.07** |

- 보정 루프(cal_n)는 miss 가 0 이다. 즉 "warm" 은 보정 경로에서는 성립한다.
- DMA + ISR 을 도는 iteration 은 1-way 캐시에서 스스로 충돌한다. S 표(중재자 호출 포함)는 iteration 당 약 10 회, N 표는 약 4 회다.
- N 은 스탬프 빌드에서 miss 가 +2 (k=3) / +4 (k=4, 5) 늘었다. 초과 사이클과 나누면 **miss 당 약 11–12 사이클**이다:
  k=3 (33 − 9) / 2 = 12, k=4 (51 − 9) / 4 ≈ 10.5 [실측 기반 추정. FLASH 5 WS 와 맞는 크기].
- S 표의 miss 는 k 에 따라 변하지 않는다. S 분해가 검증을 통과한 것과 맞는다.

### 4.3 측정 구간(T0 → t1) 안의 miss [실측, 보조]
`scripts/eval/icache_win.py`: `cpsie`(T0 직후)와 t1 판독 직후에 hbreak 를 걸고, 그 사이 miss 를 센다.
SysTick 은 끄고, `set trust-readonly-sections on` 으로 GDB 가 코드를 ELF 에서 읽게 했다. 표당 40 회 중 앞 5 회는 버렸다.

| k | S 구간 miss | N 구간 miss | CAL_N 구간 miss |
|---|---|---|---|
| 0 | 5 (드물게 8, 17) | 4 | 0 |
| 1, 2 | 5 | 4 | 0 |
| 3 | 6–7 | 6–7 | 0 |
| 4, 5 | 7–9 | 10 | 0 |

- k=0–3 에서는 N 의 구간 miss 가 표 단위 iteration 당 miss 와 거의 같다. **N 의 miss 는 거의 다 측정 구간 안에서 생긴다.**
- 이 방법은 측정 대상을 교란한다. CAL_N 구간에서도 hit 가 168 회 세어진다 (디버거 접근 [추정]).
  k=4, 5 의 N 은 10 회로 표 단위 값(8.07)보다 크다. 그래서 4.2 를 1 차 근거로 쓰고, 이 표는 보조로만 쓴다.

### 4.4 결과에 미치는 영향
- warm 총합(k=0)은 S 약 5 회, N 약 4 회의 miss 를 포함한다. 이 둘은 **특정 배치 하나에서의 값**이다.
- miss 를 모두 뺀 "순수 hit" 추정: S ≈ 273 − 5 × 11 ≈ 218, N ≈ 241 − 4 × 11 ≈ 197, S − N ≈ **+21** [추정].
- 비율 S/N 은 warm, off 모두 1.13 이다. 배치에 덜 민감한 대표값은 이 비율과 off 의 절대 분해다.
- ICACHE 1-way 설정은 Secure `MX_ICACHE_Init` 의 `HAL_ICACHE_ConfigAssociativityMode(ICACHE_1WAY)` 다 [코드].
  2-way 로 바꾸면 conflict 가 줄 것으로 본다 [추정]. 이번 측정에서는 바꾸지 않았다 (기존 중재자/테이블 측정과 조건을 같게 둠).

---

## 5. 결론

1. Secure DMA 경로(Secure 채널 + Secure ISR + NS doorbell)의 완료 처리 비용은 Insecure DMA 기준선보다 **warm +32 사이클(+13 %),
   off +68 사이클(+13 %)** 크다 [실측]. 110 MHz 에서 0.3–0.6 µs 다.
2. Secure 가 새로 더하는 일은 Secure ISR 본문과 Secure → NS tail-chain 이다. 하드웨어 tail-chain 자체는 warm 7 / off 14 사이클로 작다 [실측].
   DMA 레지스터 작업이 Secure 로 옮겨가서 NS ISR 은 짧아진다. 그래서 순 비용이 Secure 고유분보다 작다.
3. 전체 비용은 RTOS 통지(`vTaskGenericNotifyGiveFromISR`, MPU wrapper 경유)가 지배한다. warm 에서 S 의 73 %, N 은 그 이상이다 [실측].
4. warm 의 절대값은 1-way ICACHE 의 conflict miss(miss 당 약 11 사이클)를 포함하므로 배치에 따라 수십 사이클 달라진다.
   논문/보고용 수치는 warm k=0 총합과 비율 1.13x 를 쓰고, 구간 분해는 off 표(§2.2)를 쓰기를 권한다.

---

## 6. 산출물과 재현

| 경로 | 내용 |
|---|---|
| `results/isr/P2_110M_warm_its{0..5}/` | `isr_cal_s/isr_s/isr_cal_n/isr_n.csv`, `env.txt`, `manifest.txt`, `icmon.txt` (§4.2), `icwin.txt` (§4.3) |
| `results/isr/P2_110M_off_its{0..5}/` | CSV, `env.txt`, `manifest.txt` |
| `results/isr/summary.md`, `summary_off.md` | `analyze_isr.py` 출력 |
| `scripts/eval/analyze_isr.py` | 집계. 두 번째 인자로 변형 접두사 지정 (기본 `P2_110M_warm`) |
| `scripts/eval/icache_mon.py`, `icache_win.py` | ICACHE 모니터 측정 (GDB) |

```
~/st-link.sh &                                  # gdbserver :61234
PHASE=isr scripts/eval/run_all.sh               # warm k=0..5
for k in 0 1 2 3 4 5; do v=P2_110M_off_its$k
  scripts/eval/build_variant.sh isr $v P2 BENCH_PHASE=1 BENCH_ISR_TS=$k
  DIR=results/isr/$v gdb-multiarch -batch -x scripts/eval/run_bench.py; done
python3 scripts/eval/analyze_isr.py results/isr               > results/isr/summary.md
python3 scripts/eval/analyze_isr.py results/isr P2_110M_off   > results/isr/summary_off.md
DIR=results/isr/P2_110M_warm_its0 gdb-multiarch -batch -x scripts/eval/icache_mon.py
```

## 7. 남은 일 / 미확인
- 2-way ICACHE 조건에서 같은 측정 (배치 민감도 확인). 하려면 Secure `MX_ICACHE_Init` 를 바꿔야 해서 기존 측정과 조건이 달라진다. 결정이 필요하다.
- P1, 32 MHz, cold 조건은 재지 않았다 (`run_all.sh` 의 isr 단계는 P2/110/warm 만 정의).
- A (BLXNS) 방식, 스택/정적 메모리(§4.2–4.3 of `cmpISRoverhead.md`)는 보류 상태 그대로다.
- max 표본(warm S 537, N 450)의 원인이 SysTick 인지는 EXCCNT 로 확인하지 않았다 [추정].
