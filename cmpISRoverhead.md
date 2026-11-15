# DMA 완료 통지: A (BLXNS 콜백) vs B (Doorbell) — 자원 사용량 비교 측정 계획

작성일 2026-11-15. **계획 문서**다. 측정 결과는 §9 에 측정 후 기입한다.

관련 문서: `docs/plan-isr-overhead-measurement.md` (ISR 경로 분석), `docs/plan-isr-secure-context-overhead.md`
(Secure context 비용), `Mediator.md`, `cmpTable.md` (같은 측정 하니스).

표기: [실측] 보드 측정 / [논문] docs/paper/paper.txt / [코드] 소스·디스어셈블 확인 / [도출] 아키텍처 규칙에서 유도 /
[추정] 근거 부족 / 미확인 = 근거 없음.

---

## 1. 목적과 범위

| 항목 | 내용 |
|---|---|
| 목적 1 | 방식 A, B 각각에서 **Secure DMA 경로**와 **비보안(Insecure) NS DMA 경로**의 자원 사용량 차이(보안 비용)를 잰다 |
| 목적 2 | **A 와 B 사이**의 자원 사용량 차이를 잰다. 어느 통지 방식이 보안 비용을 덜 늘리는지 본다 |
| 자원 | 사이클(지연, CPU 점유), 스택(Secure/NS, MSP/PSP), 정적 메모리(Flash, RAM), 인터럽트 자원(IRQ 라인), Secure context |
| 범위 밖 | 요청 경로(중재자 검증)는 `Mediator.md`/`cmpTable.md` 에서 이미 쟀다. 여기서는 **DMA 완료 이후**만 다룬다 |
| 제약 | 측정 코드는 BENCH_PHASE 빌드에만 넣는다. 기본 빌드는 바뀌지 않는다. B 는 논문에 없는 설계다. 구현 전에 `docs/gap-analysis.md` 에 기록하고 승인받는다 (CLAUDE.md 규칙 2) |

---

## 2. 두 방식의 동작

### 2.1 실제 STM32 HAL 의 DMA ISR 이 하는 일 (재현 기준)

근거 [코드]: `Drivers/STM32L5xx_HAL_Driver/Src/stm32l5xx_hal_dma.c:738` `HAL_DMA_IRQHandler`,
`stm32l5xx_hal_uart.c` `UART_DMATransmitCplt`(3609) / `UART_DMAReceiveCplt`(3661) / `UART_DMAError`(3760) /
`UART_EndTransmit_IT`(4138) / `HAL_UART_TxCpltCallback`(2542) / `HAL_UART_RxCpltCallback`(2572).

#### 2.1.1 호출 사슬 (UART + DMA, 일반 NS 펌웨어)
```
DMA1_ChannelX_IRQHandler            (stm32l5xx_it.c, CubeMX 생성)
 └ HAL_DMA_IRQHandler(&hdma)        ── 레이어 1: DMA 드라이버
     ├ HT : IFCR(HTIF), (비순환이면 HTIE 끔) → hdma->XferHalfCpltCallback
     ├ TC : (비순환이면 TCIE·TEIE 끔, State=READY) → IFCR(TCIF) → __HAL_UNLOCK
     │      → hdma->XferCpltCallback
     └ TE : TC·HT·TE IT 끔 → IFCR(GIF) → ErrorCode=TE, State=READY → UNLOCK
            → hdma->XferErrorCallback          (EN 은 하드웨어가 이미 0 으로 만듦)
          │
          ▼  hdma->XferCpltCallback = UART_DMAxxxCplt   ── 레이어 2: 주변장치 드라이버
     Rx: RxXferCount=0, PEIE/EIE/DMAR 끔, RxState=READY → HAL_UART_RxCpltCallback
     Tx: TxXferCount=0, DMAT 끔, **USART TCIE 켬** (사용자 콜백은 아직 호출하지 않음)
          → 나중에 USART IRQ → UART_EndTransmit_IT → HAL_UART_TxCpltCallback
          │
          ▼  HAL_UART_xxCpltCallback (weak, 사용자 구현)  ── 레이어 3: 애플리케이션
     xSemaphoreGiveFromISR / vTaskNotifyGiveFromISR(&woken) ; portYIELD_FROM_ISR(woken)
          │
          ▼  PendSV → 대기 Task 실행
```
- 핵심: HAL DMA ISR 의 일은 **3 개 레이어**로 나뉜다. ① DMA 채널 관리, ② 주변장치 마무리, ③ Task 통지.
- UART **송신**은 DMA TC 에서 끝나지 않는다. USART TC 인터럽트에서 최종 콜백을 부른다 (마지막 바이트가 선로에서 나간 시점).
  즉 송신 완료 통지는 **NS 주변장치 IRQ** 가 하고, DMA ISR 은 레이어 1–2 의 일부만 한다 [코드].

#### 2.1.2 HAL 단계별로 A, B 가 어떻게 다르게 수행하는가
| # | HAL 단계 (레이어) | HAL 에서의 위치 | **A (BLXNS 콜백)** | **B (Doorbell)** | 측정 관점 |
|---|---|---|---|---|---|
| 1 | 원인 판정 ISR 읽기 (L1) | `flag_it = ISR; source_it = CCR` | Secure ISR (`DMA1->ISR`) | Secure ISR (같음) | A/B 동일 |
| 2 | HT 처리 (L1) | `IFCR(HTIF)`, HT 콜백 | Secure 가 IFCR. NS 통지가 필요하면 **HT 마다 BLXNS 1 회** | Secure 가 IFCR. **HT 마다 doorbell 1 회**, 상태 워드에 이벤트 종류(HT) 기록 | 현재 구현은 HT 미사용. 이벤트당 비용은 TC 와 같은 식으로 계산 |
| 3 | TC: IT 끄기, 플래그 정리 (L1) | `DISABLE_IT(TC\|TE)`, `IFCR(TCIF)` | Secure: EN=0, `IFCR(CGIF1)` (TCIE 는 유지) | Secure (같음) | 동일 |
| 4 | 드라이버 상태, 잠금 (L1) | `State=READY`, `__HAL_UNLOCK` | Secure: `s_busy=0` | Secure: `s_busy=0` | 동일 |
| 5 | TE 처리 (L1) | `ErrorCode=TE`, 오류 콜백 | Secure: status=1 → 콜백 인자로 전달 | Secure: `*ns_status = TE` → doorbell | 전달 수단만 다름 |
| 6 | 상위 콜백 디스패치 (L1→L2) | `hdma->XferCpltCallback(hdma)` (NS 함수 포인터, 같은 보안 상태 안의 일반 호출) | **Secure 가 저장한 NS 함수 포인터를 BLXNS 로 호출.** cmse 정리(r1–r3, s0–s15), 스텁의 24w 저장/복원, FPSCR 정리, 상태 전환 2 회 | **함수 포인터 호출이 없다.** NVIC pend 1 회 → Secure ISR 복귀 → NS ISR 로 tail-chain. 디스패치 대상은 NS 벡터 테이블이 정한다 | **A/B 의 주된 차이 1.** A: 스텁과 상태 전환. B: 예외 1 회 추가 |
| 7 | 주변장치 마무리 (L2) | `UART_DMAxxxCplt`: DMAR/DMAT·PEIE/EIE 끄기, Rx/TxState, (Tx) TCIE 켜기 | NS 콜백 안에서 수행 (Handler 모드, 특권이라 USART 레지스터 접근 가능) [추정: USART 가 NS 주변장치일 때] | NS doorbell ISR 안에서 수행 | 코드는 같고 실행 문맥만 다름 |
| 8 | Task 통지 (L3) | `HAL_UART_xxCpltCallback` → `...FromISR` + `portYIELD_FROM_ISR` | **불가.** Secure IRQ 문맥이라 `vPortValidateInterruptPriority` assert. 플래그만 쓰고 Task 는 폴링 | **가능.** 진짜 NS IRQ 이므로 HAL 사용자 콜백을 그대로 쓸 수 있음 | **A/B 의 주된 차이 2.** B 만 HAL 의미(블로킹 Task 깨우기)를 재현 |
| 9 | 문맥 전환 (L3 이후) | PendSV → Task | 없음 (spin) | PendSV + Task 가 가진 Secure context 의 `LoadContext` | B 는 블로킹의 대가로 PendSV 비용이 듦. A 는 폴링으로 CPU 를 씀 |
| 10 | Tx 최종 통지 (USART IRQ) | `UART_EndTransmit_IT` → `HAL_UART_TxCpltCallback` | NS USART IRQ 가 처리. Secure 무관 | 같음 | 송신은 Secure DMA ISR 이 통지할 필요가 없음. 레지스터 정리 (1, 3, 4) 만 Secure 에서 하면 됨 |

요약:
- 레이어 1 (1–5): 채널이 Secure 이므로 **A, B 모두 Secure ISR 이 한다.** 비용도 같다.
- 레이어 2–3 (6–8): **A 는 함수 호출(BLXNS)로, B 는 인터럽트(doorbell)로 NS 에 넘긴다.**
  HAL 의 `XferCpltCallback` 구조(함수 포인터 호출)와 모양이 닮은 것은 A 다.
  하지만 HAL 의 실제 결과(NS IRQ 문맥에서 RTOS 통지)를 재현하는 것은 B 뿐이다.
- 이 프로젝트의 M2M 벤치에는 레이어 2 (주변장치)가 없다. UART DMA 를 쓰는 평가 앱(CLAUDE.md §4)에서는
  7 과 10 이 NS 쪽 공통 비용으로 더해진다. 그래서 A/B gap 에는 영향이 없다 [도출].

#### 2.1.3 이 문서의 기호와의 대응
| 기호 (§2.2 이후) | HAL 단계 |
|---|---|
| ① 원인 판정 | 1, 2 |
| ② 플래그 정리 | 3 |
| ③ 채널 관리 | 3 |
| ④ 드라이버 상태 | 4, 5 |
| ⑤ 주변장치 마무리 | 7, 10 |
| ⑥ 소비자 통지 | 6, 8, 9 |

A 와 B 는 ⑥ 을 NS 에 넘기는 방식(HAL 단계 6, 8, 9)만 다르다.

### 2.2 방식 A: Secure ISR → BLXNS → NS 콜백 (현재 구현)
```
NS Task (spin: while(!dmaDone))
  ── IRQ 29 (ITNS=0) ──► Secure DMA1_Channel1_IRQHandler     [MSP_S]
                         ①–④, r1-r3·s0-s15 덮어쓰기
                         __gnu_cmse_nonsecure_call: push 8w + vpush d8-d15 16w, FPSCR 정리
                         BLXNS (Secure 스택에 2w) ──► App_DmaDoneCallback  [NS, Handler, MSP_NS]
                                                     dmaDone/dmaStatus 쓰기
                         FNC_RETURN ◄──
                         vpop, ldm, pop ─► EXC_RETURN
  ◄── Task 가 dmaDone 확인
```
- 근거 [코드]: `secure_nsc.c` DMA1_Channel1_IRQHandler, `app_freertos.c` App_DmaDoneCallback.
  디스어셈블 `results/mediator/P2_110M_warm_ts0/*_Secure.list` 0c000a7c, 0c000238.
- 콜백은 Secure IRQ 처리 중에 실행된다. 그래서 FreeRTOS `...FromISR` API 를 쓰면 `vPortValidateInterruptPriority`
  assert 에 걸린다 (port.c:2146–2171). NS 에서 Secure IRQ 의 IPR 이 0 으로 읽히기 때문이다 [도출, 보드 미확인].
  **⑥ 은 플래그 쓰기로만 가능하고, Task 는 폴링해야 한다.**

### 2.3 방식 B: Secure ISR → NS Doorbell IRQ pend → NS ISR
```
NS Task (block: ulTaskNotifyTake)        (다른 Task 실행 중)
  ── IRQ 29 (ITNS=0) ──► Secure DMA ISR                       [MSP_S]
                         ①–④
                         *ns_status = TC/TE    (init 때 cmse_check_address_range 로 검증한 NS 워드)
                         NVIC_SetPendingIRQ(DOORBELL_IRQn)   (ITNS=1, 미사용 IRQ)
                         EXC_RETURN ──tail-chain──► NS Doorbell ISR   [MSP_NS]
                                                    status 읽기
                                                    vTaskNotifyGiveFromISR(owner)
                                                    portYIELD_FROM_ISR
                                                    ──► PendSV: 실행 중 Task 저장, Task A 복원
                                                        (+ SecureContext_LoadContext(A))
  ◄── Task A 가 ulTaskNotifyTake 에서 복귀
```
- Secure ISR 은 NS 함수를 부르지 않는다. 따라서 cmse 콜 스텁, BLXNS/FNC_RETURN, s0–s15 덮어쓰기가 없다 [도출].
- 대가로 예외가 하나 더 생긴다 (Secure ISR → NS ISR tail-chain). 원래 배경이 NS thread 이면 추가 상태
  컨텍스트(r4–r11 + 서명)는 push 되지 않는다 [도출, 미확인 → EXCCNT 로 확인].
- Doorbell IRQ: 이 프로젝트의 Secure·NS 어느 쪽도 쓰지 않는 IRQ 1개. 구현할 때 벡터 테이블과 사용 주변장치를 확인해 고른다.
  NS 우선순위는 configMAX_SYSCALL(5) 이상 (수치 기준)으로 둔다.

### 2.4 A 와 B 의 차이 요약
| 항목 | A (BLXNS 콜백) | B (Doorbell) |
|---|---|---|
| 예외 수 | 1 (Secure) | 2 (Secure → NS tail-chain) + PendSV (블로킹이므로) |
| 보안 상태 전환 | 예외 진입/복귀 + BLXNS + FNC_RETURN | 예외 진입(S) → tail-chain(NS) → 복귀 |
| 컴파일러 cmse 코드 | s0–s15·r1–r3 덮어쓰기, 24w 저장/복원, FPSCR 정리 | 없음 |
| FP lazy 저장 유발 | 예 (Task 가 FPCA=1 일 때 Secure ISR 의 vmov) | Secure ISR 에 FP 명령이 없으면 아니요 [추정, 리스팅 확인] |
| NS 쪽 통지 코드 실행 문맥 | Secure IRQ 29 의 Handler 모드 (빌린 문맥) | 진짜 NS IRQ |
| FreeRTOS API | 불가 | 가능 |
| Task 대기 방식 | 폴링 (CPU 점유) | 블로킹 (다른 Task 실행) |
| 결과(TC/TE) 전달 | 콜백 인자 | NS 상태 워드 |
| 추가 자원 | 없음 | IRQ 라인 1, NS 상태 워드 1, Secure 측 포인터 1 |

---

## 3. 비교 구성 (4 가지)

| ID | 구성 | 채널 | 완료 ISR | Task 대기 | Task 의 Secure context |
|---|---|---|---|---|---|
| **A-S** | A, Secure | DMA1_CH1 (SECM=1, 중재자) | Secure ISR + BLXNS 콜백 | spin | 있음 (중재자 호출에 필요) |
| **A-N** | A 의 비보안 기준 | DMA1_CH2 (SECM=0, NS 직접 설정) | NS ISR + 같은 콜백 직접 호출 | spin | 없음 |
| **B-S** | B, Secure | DMA1_CH1 (중재자) | Secure ISR + doorbell → NS ISR (notify) | block | 있음 |
| **B-N** | B 의 비보안 기준 | DMA1_CH2 | NS ISR 하나 (①–④ + notify) | block | 없음 |

- A-N / B-N 의 NS ISR 은 Secure ISR 과 같은 C 본문(①–④)을 쓴다. 차이는 통지 부분뿐이다.
- 채널 2 를 NS 로 푸는 것은 BENCH_PHASE 빌드에서만 한다 (Secure main.c: `SECM=0`, `NVIC_SetTargetState`).
- **방식 내부 gap** = X-S − X-N (X ∈ {A, B}): 그 방식에서 보안이 더하는 비용.
- **방식 간 gap** = 두 가지를 본다.
  1. A-S − B-S: Secure 구현끼리의 절대 차이.
  2. (A-S − A-N) − (B-S − B-N): 보안 비용 자체의 차이.
- 참고: B-N − A-N = RTOS 블로킹 통지 의미 자체의 비용 (보안과 무관).

---

## 4. 측정 항목 (자원별)

### 4.1 사이클
| ID | 지표 | 구간 | 의미 |
|---|---|---|---|
| C1 | **완료 지연** | T0 → Task 가 완료를 인지한 첫 명령 (A: `while` 탈출, B: `ulTaskNotifyTake` 복귀) | 애플리케이션이 체감하는 지연 |
| C2 | **ISR 점유** | T0 → 인터럽트된 문맥으로 복귀 (A) / PendSV 진입 (B) | 매 전송마다 다른 일에서 빼앗는 CPU |
| C3 | 하드웨어 예외 오버헤드 | `DWT->EXCCNT` 차분 | 스태킹/언스태킹/tail-chain. A 대비 B 의 추가 예외 확인 |
| C4 | 분해 | 스탬프 Ta, Tb, Tc, Td, Tp (§5.3) | gap 이 어느 단계에서 생기는지 |
| C5 | FP 영향 | FPCA=0 (F0) / FPCA=1 (F1) 별 C1, C2 | A 의 lazy FP 저장 비용 |

### 4.2 스택 (동적, 1회 완료 처리당 최대 사용량)
| ID | 스택 | A-S | A-N | B-S | B-N |
|---|---|---|---|---|---|
| S1 | 인터럽트된 Task 의 PSP_NS (HW 프레임) | 32 B / FPCA=1 이면 104 B 예약 | 같음 | 같음 | 같음 |
| S2 | Secure MSP_S | 핸들러 프레임 + 32 B(8w) + 64 B(d8–d15) + 8 B(BLXNS) | 0 | 핸들러 프레임만 | 0 |
| S3 | NS MSP_NS | 콜백 프레임 (leaf, 0) | NS ISR + 콜백 | Doorbell ISR + FreeRTOS FromISR 호출 깊이 | NS ISR + FromISR 깊이 |
| S4 | Task 의 Secure 스택 (PSP_S) | 완료 경로는 사용하지 않음 [도출] | — | 같음 | — |

### 4.3 정적 메모리
| ID | 항목 | 측정 |
|---|---|---|
| M1 | Flash (코드): 완료 경로 함수 크기 | `arm-none-eabi-nm --size-sort -S`: Secure ISR, `__gnu_cmse_nonsecure_call`, NS 콜백, Doorbell ISR, NS DMA ISR |
| M2 | RAM (.data/.bss): 경로 전용 변수 | A: `s_ns_callback` 4 B / B: NS 상태 워드 포인터 4 B (Secure) + 상태 워드 4 B (NS) + owner 핸들 4 B |
| M3 | 전체 이미지 차이 | `arm-none-eabi-size` Secure/NS, 기본 빌드 대비 |

### 4.4 기타 자원
| ID | 항목 | A-S | A-N | B-S | B-N |
|---|---|---|---|---|---|
| R1 | IRQ 라인 | 1 (Secure) | 1 (NS) | 2 (Secure + NS doorbell) | 1 (NS) |
| R2 | Task 당 Secure context | 1 슬롯 / 8, Secure 힙 1040 B (1024 + seal 8 + 헤더) [코드] | 0 | 같음 (A-S) | 0 |
| R3 | 블로킹 복귀 시 PendSV 의 `SecureContext_LoadContext` | 해당 없음 (spin) | — | 있음 (C4 의 Tp→T5 에 포함) | 없음 |
| R4 | CPU 가용성 (대기 중) | spin 이라 0 | 0 | 다른 Task 실행 가능 | 같음 |

---

## 5. 측정 방법

### 5.1 공통 조건
- 기존 하니스를 그대로 쓴다: `bench_ns.inc` / `bench_s.inc`, SRAM2 표 (0x20030000), `run_bench.py`, `run_all.sh`.
- 8 B M2M, src `BENCH_SRC` (0x20038000), dst = Task A Cluster. N = 1000. 통계는 min / median / max.
- 조건: {P1, P2} × {110, 32 MHz} × {off, warm, cold}, `PHASE=isr`. 헤드라인은 P2 (양쪽 -O1)/110 MHz/warm.
- `.bss` 를 늘리지 않는다 (모든 벤치 상태는 SRAM2). `check_layout.sh`: `s_dmaTaskMem = 0x2001b600`.
- DWT: Secure main.c 의 BENCH_PHASE 블록에서 `DWT_CTRL.EXCEVTENA` 를 켠다.
  `DWT_CTRL = 0x40000001` 이므로 NOPRFCNT=0 이고 프로파일 카운터가 있다 [실측 env.txt].
  DAUTHSTATUS=0xff 로 Secure 비침습 카운팅도 허용된다 [실측].

### 5.2 트리거 (T0 를 결정적으로 만들기)
DMA 완료(TC) 순간은 직접 찍을 수 없다. 전송을 **IRQ 를 막은 채** 끝내고, 정해진 명령에서 IRQ 를 푼다.
PRIMASK_NS 는 AIRCR.PRIS=0 이므로 Secure 우선순위 5 IRQ 까지 막는다 [도출, partition_stm32l562xx.h:305].

**A (spin), 측정 Task 가 직접 트리거한다:**
```
cpsid i
start (A-S: SECURE_dma_m2m_start / A-N: ns_dma_start)
dst[7] 기록될 때까지 폴링 → CYCCNT 로 32 사이클 대기 (TC pending 보장)
FPCA 설정 (F0: CONTROL.FPCA 지움 / F1: vmov s0,s0)
T0 = CYCCNT ; E0 = EXCCNT
cpsie i
while (!done) {}
T5 = CYCCNT ; E5 = EXCCNT
```

**B (block), 저우선순위 트리거 Task 가 푼다:**
```
Task A (prio 2):  DMA IRQ 를 NVIC 에서 막음 (B-S: SECURE_bench_irq_mask(1) / B-N: NVIC_DisableIRQ)
                  start → ulTaskNotifyTake(portMAX_DELAY)   (블록)
Trigger (prio 1): dst[7] 폴링 + 32 사이클 대기
                  cpsid i ; NVIC 마스크 해제 (B-S: SECURE_bench_irq_mask(0) / B-N: NVIC_EnableIRQ)
                  FPCA 설정 ; T0 = CYCCNT ; E0 = EXCCNT ; cpsie i
                  → Secure ISR → doorbell → notify → PendSV → Task A
Task A:           T5 = CYCCNT ; E5 = EXCCNT  (ulTaskNotifyTake 복귀 직후)
```
- NVIC 마스크를 PRIMASK 안에서 푼다. 그래서 B-S 의 NSC 호출 중에 IRQ 가 들어오지 않는다
  (Secure 배경 + NS 핸들러에서 생기는 추가 상태 push 를 피한다).
- 트리거 Task 와 Task A 는 둘 다 특권으로 만든다. DWT 와 SRAM2 를 써야 하기 때문이다 (BENCH_PHASE 의 기존 방식).
  트리거 Task 는 Secure context 를 갖지 않게 만든다. B-S 의 마스크 해제 NSC 를 쓰려면 context 가 필요하므로,
  B-S 에서만 트리거 Task 에 context 를 할당한다. 이 비용은 측정 구간 밖이다 (PendSV 의 Save 는 구간 안. §8 참고).

### 5.3 분해 스탬프 (BENCH_TS 빌드에서만, 기존 BENCH_T1/T4 관례)
| 스탬프 | 위치 | A-S | A-N | B-S | B-N |
|---|---|---|---|---|---|
| Ta | 완료 ISR 첫 문장 | Secure ISR | NS ISR | Secure ISR | NS ISR |
| Tb | NS 통지 코드 진입 | 콜백 진입 | 콜백 진입 | Doorbell ISR 진입 | (= Ta 이후 notify 직전) |
| Tc | NS 통지 코드 끝 | 콜백 끝 | 콜백 끝 | `portYIELD_FROM_ISR` 직후 | 같음 |
| Td | Secure 복귀 지점 | BLXNS 다음 | — | — | — |
| Tp | PendSV 진입 | — | — | EXCCNT 와 T5 로 도출 | 같음 |

- Secure 스탬프는 Secure 전역 변수에 기록한다. 측정 구간 밖에서 getter NSC `SECURE_bench_isr_ts(k)` 로 읽는다.
- 스탬프 1개 비용(`ldr lit; ldr CYCCNT; str`)은 INNER_CALIB 방식으로 보정한다.
- 총합은 ts0 빌드 값을 쓴다. ts1 은 분해용이다. 검증 조건: ts1 의 Σ 구간 − ts0 총합 ≈ 스탬프 수 × 스탬프 비용.

### 5.4 보정
| ID | 내용 |
|---|---|
| CAL_SPIN | A 트리거와 같은 코드에서 done 을 미리 1 로 두고 DMA 없이 측정 (cpsie + 루프 1회 + 판독) |
| CAL_BLOCK | 트리거 Task 가 `xTaskNotifyGive(A)` + `taskYIELD()` 를 직접 실행 (ISR 없음). Task 전환 자체의 비용 |
| CAL_TS | 스탬프 1개 비용 (Secure / NS 각각) |

### 5.5 스택 측정
- **동적 (S2, S3):** 페인팅 방식.
  - 첫 `bench_hit` 에서 GDB 가 Secure MSP 와 NS MSP 의 미사용 영역을 패턴(0xDEADBEEF)으로 채운다.
    범위는 현재 SP 부터 `_estack − _Min_Stack_Size` (0x400) 까지다.
  - 각 ISR 표가 끝난 뒤의 `bench_hit` 에서 high-water 를 읽는다.
  - 표마다 다시 칠해서 구성별 최대값을 분리한다.
  - `run_bench.py` 에 추가한다. 주소는 각 ELF 의 `_estack`, `_Min_Stack_Size` 를 nm 으로 읽는다.
- **정적 교차 확인:** `.list` 의 push/vpush 개수에서 프레임 크기를 계산한다 (§4.2 표의 값) [코드].
  B 의 FreeRTOS FromISR 깊이는 `-fstack-usage` 없이 리스팅으로 추적한다.
- **S1 (HW 프레임):** FPCA 에 따라 32 / 104 B. 아키텍처 규칙에서 값을 정하고 [도출], F0/F1 의 PSP_NS 페인팅으로 확인한다.
- **R2:** Secure 힙 `xFreeBytesRemaining` 차이와 `xSecureContexts[]` 를 GDB 로 읽는다 (`docs/plan-isr-secure-context-overhead.md`).

### 5.6 정적 메모리 측정
- 4 구성을 각각 빌드하지 않는다. 한 ELF 에 모든 경로가 들어가므로 **함수 단위** 크기(M1)로 비교한다.
- 이미지 전체 차이(M3)는 기본 빌드 / BENCH_PHASE 빌드 / B 기능을 넣은 기본 빌드(예: `DMA_NOTIFY_DOORBELL`)를 비교한다.
- 스크립트: `scripts/eval/size_isr.sh <dir>` → `results/isr/<variant>/size.txt`.

---

## 6. 벤치 표 (SRAM2, BENCH_COLS 2 → 6)

| ID | 이름 | 열 (모두 T0 기준 상대값) |
|---|---|---|
| 9 | `cal_spin` | total |
| 10 | `cal_block` | total |
| 11, 12 | `a_n_f0`, `a_n_f1` | total, exccnt, Ta, Tb, Tc, — |
| 13, 14 | `a_s_f0`, `a_s_f1` | total, exccnt, Ta, Tb, Tc, Td |
| 15, 16 | `b_n_f0`, `b_n_f1` | total(C1), exccnt, Ta, Tb, Tc, C2 |
| 17, 18 | `b_s_f0`, `b_s_f1` | total(C1), exccnt, Ta, Tb, Tc, C2 |
| 19 | `cal_ts` | Secure 스탬프 비용, NS 스탬프 비용 |

1000 행 × 24 B = 24 KB (0x20030040 ~ 0x20036040). `BENCH_SRC` 0x20038000 아래에서 끝난다.
`run_bench.py` 는 COLS=6 과 NAMES 를 갱신하고, 스택 페인팅/판독 결과를 `stack.txt` 로 쓴다.

---

## 7. 결과 계산식

### 7.1 방식 내부 gap (보안 비용)
| 지표 | A | B |
|---|---|---|
| 완료 지연 C1 | `(a_s − cal_spin) − (a_n − cal_spin)` = `a_s − a_n` | `b_s − b_n` |
| ISR 점유 C2 | A 는 C1 과 같은 구간 | `b_s.C2 − b_n.C2` |
| HW 예외 C3 | `a_s.exccnt − a_n.exccnt` (기대: 0) | `b_s.exccnt − b_n.exccnt` (기대: tail-chain 1 회분) |
| 단계 분해 | Secure 고유분 = (Tb−Ta)\_S − (Tb−Ta)\_N + (Td−Tc)\_S | Secure 고유분 = (Tb−Ta)\_S − (Tb−Ta)\_N, PendSV LoadContext = (T5−Tc)\_S − (T5−Tc)\_N |
| FP | `a_s_f1 − a_s_f0` (lazy 저장) | `b_s_f1 − b_s_f0` (기대: ≈ 0) |
| 스택 | S2: Secure MSP 증가분, S3: NS MSP 차이 | 같음 |
| 정적 | M1: Secure 쪽 추가 함수 크기 | 같음 + Doorbell ISR |

### 7.2 방식 간 gap
| 비교 | 식 | 해석 |
|---|---|---|
| Secure 끼리 절대 차이 | `a_s − b_s` (C2 기준, 둘 다 ISR 점유) | 같은 보안 수준에서 어느 통지가 CPU 를 덜 쓰나 |
| 보안 비용 차이 | `(a_s − a_n) − (b_s − b_n)` | 통지 방식이 보안 오버헤드를 얼마나 바꾸나 |
| 의미 비용 | `b_n − a_n` | 블로킹 통지(notify + PendSV) 자체의 비용. 보안과 무관 |
| 스택 | A-S, B-S 의 S2·S3 비교 | BLXNS 스텁(104 B Secure)과 FromISR 깊이(NS)의 교환 |
| 자원 | R1–R4 표 | B 는 IRQ 1 개와 상태 워드를 더 쓰는 대신 CPU 가용성(R4)을 얻는다 |

### 7.3 예상 (검증 대상 가설) [추정]
- C3: A-S = A-N (같은 F). B-S 는 B-N 보다 tail-chain 1 회만큼 크다.
- A-S 의 Secure 고유분 ≈ 49 명령 + 24w 저장 + 26w 복원 + BLXNS/FNC_RETURN 전환 2 회. F1 이면 lazy FP 저장 17w 가 추가된다 [코드 기반 상한].
- B-S 의 Secure 고유분 ≈ 상태 워드 store 1 + NVIC pend store 1 + tail-chain + Secure 벡터 fetch.
- 보안 비용은 B 가 A 보다 작을 것으로 본다. 단 PendSV 의 LoadContext 를 포함하면 역전될 수 있다. 측정으로 판정한다.

---

## 8. 편향과 통제
| 편향 | 통제 |
|---|---|
| TC pending 이 마지막 바이트보다 늦게 섬 | 폴링 후 32 사이클 대기. `Ta − T0` 분포 폭으로 확인 |
| SysTick 이 샘플에 섞임 | min/median 사용. EXCCNT 증가로 식별해 제외 |
| P1 (Secure -O0) 이 Secure 코드를 부풀림 | 헤드라인은 P2. P1 은 참고 |
| ICACHE 1-way, S/NS 공용 충돌 | off/warm/cold 전 조건 |
| NS ISR 과 Secure ISR 본문 차이 (`s_busy`, NULL 검사) | 같은 C 본문 사용. 남는 차이는 리스팅으로 명령 수를 기록 |
| B-S 트리거 Task 의 Secure context | PendSV 에서 트리거 Task 의 SaveContext 가 C1 에 섞인다. B-N 트리거 Task 는 context 가 없다. 이 차이는 `docs/plan-isr-secure-context-overhead.md` 의 ctx_s − ctx_ns 의 절반 정도로 보정한다. 또는 두 경로 모두 트리거 Task 에 context 를 할당해 통일한다 (기본안: 통일) |
| A 는 spin, B 는 block 이라 대기 의미가 다름 | 의도된 차이 (각 방식의 자연스러운 사용). C2 (ISR 점유)로 같은 기준 비교를 함께 제시 |

---

## 9. 결과 (측정 후 기입)

### 9.1 사이클 — P2 / 110 MHz / warm, median (min)
| 지표 | A-S | A-N | gap A | B-S | B-N | gap B | A-S − B-S |
|---|---|---|---|---|---|---|---|
| C1 F1 | | | | | | | |
| C2 F1 | | | | | | | |
| C3 | | | | | | | |
| FP (F1 − F0) | | | | | | | |

### 9.2 스택 / 메모리 / 자원
| 항목 | A-S | A-N | B-S | B-N |
|---|---|---|---|---|
| S2 Secure MSP 최대 (B) | | | | |
| S3 NS MSP 최대 (B) | | | | |
| M1 Flash (B) | | | | |
| M2 RAM (B) | | | | |
| R1 IRQ | 1 | 1 | 2 | 1 |
| R2 Secure 힙 (B) | 1040 | 0 | 1040 | 0 |

### 9.3 전 조건 표
`results/isr/summary.md` (`scripts/eval/analyze_isr.py` 생성).

---

## 10. 구현 항목 (BENCH_PHASE 전용, 영어 주석 3 줄 이내)

| 파일 | 내용 | 신규/재사용 |
|---|---|---|
| `docs/gap-analysis.md` | B (doorbell, NS 상태 워드), NS 채널 기준선 기록 | 신규 항목 |
| `Secure/Src/main.c` | BENCH_PHASE: CH2 SECM=0 + ITNS, doorbell IRQ ITNS, EXCEVTENA | 기존 블록 확장 |
| `Secure/Src/secure_nsc.c` | B 경로: `SECURE_dma_m2m_init` 에 상태 워드 등록(`cmse_check_address_range`), ISR 에서 `*ns_status` 쓰기 + pend. A/B 선택은 init 인자로 (매크로 없이). `BENCH_TI(k)` 스탬프 | 기존 ISR 수정 |
| `Secure/Src/bench_s.inc` | `SECURE_bench_isr_ts(k)`, Secure 스탬프 보정 | 신규 함수 |
| `NonSecure/Src/bench_ns.inc` | `ns_dma_start`, NS DMA ISR (CH2), Doorbell ISR, 트리거 Task, `bench_isr_spin()` / `bench_isr_block()`, 표 9–19, `BENCH_COLS 6` | 신규 |
| `scripts/eval/run_bench.py` | COLS=6, NAMES, 스택 페인팅/판독 | 수정 |
| `scripts/eval/size_isr.sh`, `analyze_isr.py` | M1–M3, 요약 표 | 신규 |
| `docs/CHANGES.md` | 커밋 1 줄 | — |

---

## 11. 검증 절차

1. 빌드: `./build.sh clean && ./build.sh` (기본 빌드가 바뀌지 않았는지 확인).
   `scripts/eval/build_variant.sh isr P2_110M_warm_ts0 P2 BENCH_PHASE=1 BENCH_ICACHE` (layout ok 0x2001b600), ts1 도 같이 빌드.
2. 리스팅 확인:
   - `cpsie` 가 T0/E0 판독 직후에 있다.
   - B 의 Secure ISR 에 `vmov`/`blxns` 가 없다.
   - NS ISR 들에 FP 명령이 없다.
3. 보드 측정 전 GDB 확인:
   - `p/x *(uint32_t*)0xE0001000`: bit18 EXCEVTENA = 1
   - `p/x *(uint32_t*)0x40020008 & 0x40000`: CH2 SECM = 0
   - `p/x *(uint32_t*)0xE000E380`: ITNS0 bit30 (CH2) = 1, bit29 (CH1) = 0, doorbell 비트 = 1
   - T0 지점에서 `p/x $control`: FPCA (bit2) = 0 (F0) / 1 (F1)
4. 측정: `~/st-link.sh` 후 `PHASE=isr scripts/eval/run_all.sh`.
   산출물은 `results/isr/<variant>/{cal_*,a_*,b_*}.csv`, `stack.txt`, `size.txt`.
5. 기대값 확인 (§7.3):
   - C3: A-S = A-N
   - B-S − B-N ≈ tail-chain 1 회
   - `a_s_f1 > a_s_f0`
   - `b_s_f1 ≈ b_s_f0`
   - B-N, B-S 모두 Task A 가 매 샘플 깨어남 (표 행 수 = N)
