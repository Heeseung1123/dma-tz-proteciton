# 메모리 측정 집계 (scripts/eval/mem_measure.py 생성)

장치 용량: Flash 512 KB, SRAM 256 KB. .data 는 SRAM 과 Flash(LMA) 양쪽에 센다.

## P1

### 영역별 (바이트)

| 영역 | S | I | Δ (S−I) | Δ% (대 I) |
|---|---|---|---|---|
| S-Flash | 11064 | 8512 | +2552 | +29.98 % |
| S-NSC | 96 | 96 | +0 | +0.00 % |
| S-SRAM | 12000 | 11992 | +8 | +0.07 % |
| NS-Flash | 44952 | 44984 | -32 | -0.07 % |
| NS-SRAM | 22592 | 21504 | +1088 | +5.06 % |
| Flash 합계 | 56112 | 53592 | +2520 | +4.70 % |
| SRAM 합계 | 34592 | 33496 | +1096 | +3.27 % |

### 섹션별 (바이트, 0 아닌 섹션)

| 세계 | 섹션 | S | I | Δ |
|---|---|---|---|---|
| Secure | `.isr_vector` | 504 | 504 | +0 |
| Secure | `.text` | 10440 | 7888 | +2552 |
| Secure | `.rodata` | 80 | 80 | +0 |
| Secure | `.init_array` | 8 | 8 | +0 |
| Secure | `.fini_array` | 8 | 8 | +0 |
| Secure | `.data` | 24 | 24 | +0 |
| Secure | `.gnu.sgstubs` | 96 | 96 | +0 |
| Secure | `.bss` | 10440 | 10432 | +8 |
| Secure | `._user_heap_stack` | 1536 | 1536 | +0 |
| NonSecure | `.isr_vector` | 504 | 504 | +0 |
| NonSecure | `.privileged_functions` | 25408 | 25408 | +0 |
| NonSecure | `.freertos_system_calls` | 1152 | 1152 | +0 |
| NonSecure | `.text` | 7264 | 7288 | -24 |
| NonSecure | `.rodata` | 384 | 392 | -8 |
| NonSecure | `.init_array` | 8 | 8 | +0 |
| NonSecure | `.fini_array` | 8 | 8 | +0 |
| NonSecure | `.data` | 10224 | 10224 | +0 |
| NonSecure | `.bss` | 10832 | 9744 | +1088 |
| NonSecure | `._user_heap_stack` | 1536 | 1536 | +0 |

### 구성요소별 (바이트, S 와 I 가 다른 입력 섹션만)

| # | 구성요소 | S Flash | S SRAM | I Flash | I SRAM | Δ Flash | Δ SRAM |
|---|---|---|---|---|---|---|---|
| C1 | 요청 검증 | 50 | 0 | 0 | 0 | +50 | +0 |
| C2 | 요청 진입점 | 240 | 0 | 0 | 0 | +240 | +0 |
| C3 | 초기화 | 964 | 0 | 0 | 0 | +964 | +0 |
| C4 | 완료 ISR + 통지 | 288 | 9 | 152 | 0 | +136 | +9 |
| C5 | 부팅 설정 | 1644 | 0 | 572 | 0 | +1072 | +0 |
| C6 | 전송 시점 보호 | 52 | 0 | 0 | 0 | +52 | +0 |
| C7 | 배치 (s_dmaTaskMem) | 0 | 1536 | 0 | 576 | +0 | +960 |
| C8 | NSC 관문 (sgstub + NS veneer) | 136 | 0 | 104 | 0 | +32 | +0 |
| C9 | NS 호출부 (inline 포함) | 112 | 0 | 116 | 0 | -4 | +0 |
| C10 | 정렬 fill | 46 | 166 | 68 | 39 | -22 | +127 |
| | **합계** | 3532 | 1711 | 1012 | 615 | +2520 | +1096 |

구성요소 합계 Δ Flash +2520 / 영역 합계 Δ Flash +2520, 구성요소 합계 Δ SRAM +1096 / 영역 합계 Δ SRAM +1096 (일치)

### 차이 나는 입력 섹션 전체 (맵 기준)

| 세계 | 출력 섹션 | 입력 섹션 | 오브젝트 | S | I | Δ | 구성요소 |
|---|---|---|---|---|---|---|---|
| NonSecure | `.bss` | `*fill*` |  | 163 | 39 | +124 | C10 |
| NonSecure | `.rodata` | `*fill*` |  | 28 | 36 | -8 | C10 |
| NonSecure | `.bss` | `.bss.s_dmaStatusWord` | app_freertos.o | 4 | 0 | +4 | C4 |
| NonSecure | `.text` | `.text.DFSDM1_FLT3_IRQHandler` | app_freertos.o | 116 | 0 | +116 | C4 |
| NonSecure | `.text` | `.text.DMA1_Channel1_IRQHandler` | app_freertos.o | 0 | 152 | -152 | C4 |
| NonSecure | `.bss` | `.bss.s_dmaTaskMem` | app_freertos.o | 1536 | 576 | +960 | C7 |
| NonSecure | `.text` | `.fini.__stub` | linker stubs | 40 | 24 | +16 | C8 |
| NonSecure | `.text` | `.text.vDmaTaskA` | app_freertos.o | 112 | 116 | -4 | C9 |
| Secure | `.text` | `.text.verify_inst` | secure_nsc.o | 50 | 0 | +50 | C1 |
| Secure | `.gnu.sgstubs` | `(ALIGN/문자열 병합, 맵 미표시)` |  | 0 | 16 | -16 | C10 |
| Secure | `.bss` | `*fill*` |  | 3 | 0 | +3 | C10 |
| Secure | `.text` | `*fill*` |  | 18 | 16 | +2 | C10 |
| Secure | `.text` | `.text.SECURE_dma_m2m_start` | secure_nsc.o | 240 | 0 | +240 | C2 |
| Secure | `.text` | `.text` | libgcc.a(cmse.o) | 216 | 0 | +216 | C3 |
| Secure | `.text` | `.text.NVIC_ClearTargetState` | secure_nsc.o | 112 | 0 | +112 | C3 |
| Secure | `.text` | `.text.NVIC_SetTargetState` | secure_nsc.o | 108 | 0 | +108 | C3 |
| Secure | `.text` | `.text.SECURE_dma_m2m_init` | secure_nsc.o | 256 | 0 | +256 | C3 |
| Secure | `.text` | `.text.Secure_TZIC_ArmGuardIrqOnce` | secure_nsc.o | 68 | 0 | +68 | C3 |
| Secure | `.text` | `.text.__NVIC_ClearPendingIRQ` | secure_nsc.o | 60 | 0 | +60 | C3 |
| Secure | `.text` | `.text.__NVIC_EnableIRQ` | secure_nsc.o | 60 | 0 | +60 | C3 |
| Secure | `.text` | `.text.__NVIC_SetPriority` | secure_nsc.o | 84 | 0 | +84 | C3 |
| Secure | `.bss` | `.bss.s_busy` | secure_nsc.o | 1 | 0 | +1 | C4 |
| Secure | `.bss` | `.bss.s_ns_status` | secure_nsc.o | 4 | 0 | +4 | C4 |
| Secure | `.text` | `.text.DMA1_Channel1_IRQHandler` | secure_nsc.o | 112 | 0 | +112 | C4 |
| Secure | `.text` | `.text.__NVIC_SetPendingIRQ` | secure_nsc.o | 60 | 0 | +60 | C4 |
| Secure | `.text` | `.text.HAL_GTZC_MPCBB_ConfigMemAttributes` | stm32l5xx_hal_gtzc.o | 492 | 0 | +492 | C5 |
| Secure | `.text` | `.text.NVIC_SetTargetState` | main.o | 0 | 108 | -108 | C5 |
| Secure | `.text` | `.text.SMPU_SetupOneTaskRegion` | main.o | 64 | 0 | +64 | C5 |
| Secure | `.text` | `.text.SMPU_SetupTaskRegions` | main.o | 148 | 0 | +148 | C5 |
| Secure | `.text` | `.text.Secure_DMA_LockAllChannels` | main.o | 212 | 0 | +212 | C5 |
| Secure | `.text` | `.text.Secure_TZIC_Init` | main.o | 92 | 0 | +92 | C5 |
| Secure | `.text` | `.text.__NVIC_ClearPendingIRQ` | main.o | 60 | 0 | +60 | C5 |
| Secure | `.text` | `.text.__NVIC_EnableIRQ` | main.o | 60 | 0 | +60 | C5 |
| Secure | `.text` | `.text.__NVIC_SetPriority` | main.o | 84 | 0 | +84 | C5 |
| Secure | `.text` | `.text.main` | main.o | 432 | 464 | -32 | C5 |
| Secure | `.text` | `.text.GTZC_IRQHandler` | secure_nsc.o | 52 | 0 | +52 | C6 |
| Secure | `.gnu.sgstubs` | `.gnu.sgstubs.__stub` | linker stubs | 96 | 80 | +16 | C8 |

### 검사

- S_P1: 통과
- I_P1: 통과
- 미분류 차이: 0 건

## P2

### 영역별 (바이트)

| 영역 | S | I | Δ (S−I) | Δ% (대 I) |
|---|---|---|---|---|
| S-Flash | 7488 | 5968 | +1520 | +25.47 % |
| S-NSC | 96 | 96 | +0 | +0.00 % |
| S-SRAM | 11992 | 11984 | +8 | +0.07 % |
| NS-Flash | 44952 | 44984 | -32 | -0.07 % |
| NS-SRAM | 22592 | 21504 | +1088 | +5.06 % |
| Flash 합계 | 52536 | 51048 | +1488 | +2.91 % |
| SRAM 합계 | 34584 | 33488 | +1096 | +3.27 % |

### 섹션별 (바이트, 0 아닌 섹션)

| 세계 | 섹션 | S | I | Δ |
|---|---|---|---|---|
| Secure | `.isr_vector` | 504 | 504 | +0 |
| Secure | `.text` | 6856 | 5336 | +1520 |
| Secure | `.rodata` | 96 | 96 | +0 |
| Secure | `.init_array` | 8 | 8 | +0 |
| Secure | `.fini_array` | 8 | 8 | +0 |
| Secure | `.data` | 16 | 16 | +0 |
| Secure | `.gnu.sgstubs` | 96 | 96 | +0 |
| Secure | `.bss` | 10440 | 10432 | +8 |
| Secure | `._user_heap_stack` | 1536 | 1536 | +0 |
| NonSecure | `.isr_vector` | 504 | 504 | +0 |
| NonSecure | `.privileged_functions` | 25408 | 25408 | +0 |
| NonSecure | `.freertos_system_calls` | 1152 | 1152 | +0 |
| NonSecure | `.text` | 7264 | 7288 | -24 |
| NonSecure | `.rodata` | 384 | 392 | -8 |
| NonSecure | `.init_array` | 8 | 8 | +0 |
| NonSecure | `.fini_array` | 8 | 8 | +0 |
| NonSecure | `.data` | 10224 | 10224 | +0 |
| NonSecure | `.bss` | 10832 | 9744 | +1088 |
| NonSecure | `._user_heap_stack` | 1536 | 1536 | +0 |

### 구성요소별 (바이트, S 와 I 가 다른 입력 섹션만)

| # | 구성요소 | S Flash | S SRAM | I Flash | I SRAM | Δ Flash | Δ SRAM |
|---|---|---|---|---|---|---|---|
| C1 | 요청 검증 | 46 | 0 | 0 | 0 | +46 | +0 |
| C2 | 요청 진입점 | 296 | 0 | 0 | 0 | +296 | +0 |
| C3 | 초기화 | 520 | 0 | 0 | 0 | +520 | +0 |
| C4 | 완료 ISR + 통지 | 196 | 9 | 152 | 0 | +44 | +9 |
| C5 | 부팅 설정 | 1156 | 0 | 616 | 0 | +540 | +0 |
| C6 | 전송 시점 보호 | 36 | 0 | 0 | 0 | +36 | +0 |
| C7 | 배치 (s_dmaTaskMem) | 0 | 1536 | 0 | 576 | +0 | +960 |
| C8 | NSC 관문 (sgstub + NS veneer) | 136 | 0 | 104 | 0 | +32 | +0 |
| C9 | NS 호출부 (inline 포함) | 112 | 0 | 116 | 0 | -4 | +0 |
| C10 | 정렬 fill | 44 | 166 | 66 | 39 | -22 | +127 |
| | **합계** | 2542 | 1711 | 1054 | 615 | +1488 | +1096 |

구성요소 합계 Δ Flash +1488 / 영역 합계 Δ Flash +1488, 구성요소 합계 Δ SRAM +1096 / 영역 합계 Δ SRAM +1096 (일치)

### 차이 나는 입력 섹션 전체 (맵 기준)

| 세계 | 출력 섹션 | 입력 섹션 | 오브젝트 | S | I | Δ | 구성요소 |
|---|---|---|---|---|---|---|---|
| NonSecure | `.bss` | `*fill*` |  | 163 | 39 | +124 | C10 |
| NonSecure | `.rodata` | `*fill*` |  | 28 | 36 | -8 | C10 |
| NonSecure | `.bss` | `.bss.s_dmaStatusWord` | app_freertos.o | 4 | 0 | +4 | C4 |
| NonSecure | `.text` | `.text.DFSDM1_FLT3_IRQHandler` | app_freertos.o | 116 | 0 | +116 | C4 |
| NonSecure | `.text` | `.text.DMA1_Channel1_IRQHandler` | app_freertos.o | 0 | 152 | -152 | C4 |
| NonSecure | `.bss` | `.bss.s_dmaTaskMem` | app_freertos.o | 1536 | 576 | +960 | C7 |
| NonSecure | `.text` | `.fini.__stub` | linker stubs | 40 | 24 | +16 | C8 |
| NonSecure | `.text` | `.text.vDmaTaskA` | app_freertos.o | 112 | 116 | -4 | C9 |
| Secure | `.text` | `.text.verify_inst` | secure_nsc.o | 46 | 0 | +46 | C1 |
| Secure | `.gnu.sgstubs` | `(ALIGN/문자열 병합, 맵 미표시)` |  | 0 | 16 | -16 | C10 |
| Secure | `.bss` | `*fill*` |  | 3 | 0 | +3 | C10 |
| Secure | `.text` | `*fill*` |  | 16 | 14 | +2 | C10 |
| Secure | `.text` | `.text.SECURE_dma_m2m_start` | secure_nsc.o | 296 | 0 | +296 | C2 |
| Secure | `.text` | `.text` | libgcc.a(cmse.o) | 216 | 0 | +216 | C3 |
| Secure | `.text` | `.text.SECURE_dma_m2m_init` | secure_nsc.o | 304 | 0 | +304 | C3 |
| Secure | `.bss` | `.bss.s_busy` | secure_nsc.o | 1 | 0 | +1 | C4 |
| Secure | `.bss` | `.bss.s_ns_status` | secure_nsc.o | 4 | 0 | +4 | C4 |
| Secure | `.text` | `.text.DMA1_Channel1_IRQHandler` | secure_nsc.o | 80 | 0 | +80 | C4 |
| Secure | `.text` | `.text.HAL_GTZC_MPCBB_ConfigMemAttributes` | stm32l5xx_hal_gtzc.o | 304 | 0 | +304 | C5 |
| Secure | `.text` | `.text.main` | main.o | 852 | 616 | +236 | C5 |
| Secure | `.text` | `.text.GTZC_IRQHandler` | secure_nsc.o | 36 | 0 | +36 | C6 |
| Secure | `.gnu.sgstubs` | `.gnu.sgstubs.__stub` | linker stubs | 96 | 80 | +16 | C8 |

### 검사

- S_P2: 통과
- I_P2: 통과
- 미분류 차이: 0 건

