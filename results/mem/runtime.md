# 런타임 SRAM 사용량 (scripts/eval/mem_runtime_report.py 생성)

보드 STM32L562E-DK, `run_runtime.py` (HowmemUsageWork.md §11). 단위 바이트. 스택은 최대 사용 깊이(high-water).
값은 반복 측정의 최댓값, 괄호는 반복 간 범위 (다를 때만).

## P1

반복: S 2 회, I 2 회. 측정 구간 60 초 (xTickCount S [60661, 60664], I [60668, 60657]).

### 힙과 MSP

| 항목 | S | I | Δ (S − I) |
|---|---|---|---|
| Secure 힙 최대 사용 (10240 중) | 4176 | 2096 | +2080 |
| NS 힙 최대 사용 (8192 중) | 2792 | 2792 | +0 |
| Secure MSP 사용 (NS 시작 후) | 112 | 216 | -104 |
| NS MSP 사용 (NS 시작 후) | 320 | 320 | +0 |

### Secure context 스택 (Secure 힙 안, 각 1024 B)

| Task | S | I | Δ |
|---|---|---|---|
| `DMA_Task_A` | 208 | 할당 없음 | — |
| `DMA_Task_B` | 208 | 할당 없음 | — |
| `IDLE` | 4 | 4 | +0 |
| `Plain_Task` | 56 | 56 | +0 |

### NS Task 스택

| Task | 크기 | S | I | Δ |
|---|---|---|---|---|
| `DMA_Task_A` | 1024 | 128 | 64 | +64 |
| `DMA_Task_B` | 1024 | 128 | 64 | +64 |
| `Plain_Task` | 1024 | 120 | 120 | +0 |
| `IDLE` | 2048 | 72 | 72 | +0 |
| `Tmr Svc` | 2048 | 120 | 120 | +0 |

### 기능 확인 (스택 측정 뒤 따로 센 구간)

| 항목 | S | I |
|---|---|---|
| 완료 ISR 횟수 / 초 / tick | 29 / 10 / 8606; 30 / 10 / 8654 | 29 / 10 / 8602; 29 / 10 / 8643 |
| Task A Cluster 8 B | 0x2001b600: [0, 1, 2, 3, 4, 5, 6, 7]; 0x2001b600: [0, 1, 2, 3, 4, 5, 6, 7] | 0x2001b4a0: [0, 1, 2, 3, 4, 5, 6, 7]; 0x2001b4a0: [0, 1, 2, 3, 4, 5, 6, 7] |
| Task B Cluster 8 B | 0x2001b900: [0, 1, 2, 3, 4, 5, 6, 7]; 0x2001b900: [0, 1, 2, 3, 4, 5, 6, 7] | 0x2001b5c0: [0, 1, 2, 3, 4, 5, 6, 7]; 0x2001b5c0: [0, 1, 2, 3, 4, 5, 6, 7] |
| dmaStatus A / B | 0 / 0; 0 / 0 | 0 / 0; 0 / 0 |
| DMA1_CH1 CCR (NS alias 로 읽음) | 0x20000; 0x20000 | 0x60ca; 0x60cb |
| Secure MPU 영역 4 / 5 (RBAR, RLAR) | ['0x2001b603', '0x2001b6e1'] / ['0x2001b903', '0x2001b9e1'] | — |
| MPCBB1 VCTR13 | 0x4800000 | — |

### 정적 프레임 (`-fstack-usage`, 함수 하나의 프레임, 호출 깊이 미포함)

| 함수 | 세계 | S | I |
|---|---|---|---|
| `SECURE_dma_m2m_start` | Secure | 32 (static) | 없음 |
| `verify_inst` | Secure | 0 (static,ignoring_inline_asm) | 없음 |
| `SECURE_dma_m2m_init` | Secure | 24 (static) | 없음 |
| `DMA1_Channel1_IRQHandler` | Secure | 16 (static) | 없음 |
| `GTZC_IRQHandler` | Secure | 4 (static,ignoring_inline_asm) | 없음 |
| `DFSDM1_FLT3_IRQHandler` | NonSecure | 24 (static) | 없음 |
| `DMA1_Channel1_IRQHandler` | NonSecure | 없음 | 24 (static) |
| `vDmaTaskA` | NonSecure | 16 (static) | 24 (static) |
| `vDmaTaskB` | NonSecure | 16 (static) | 24 (static) |

## P2

반복: S 2 회, I 2 회. 측정 구간 60 초 (xTickCount S [60650, 60658], I [60654, 60656]).

### 힙과 MSP

| 항목 | S | I | Δ (S − I) |
|---|---|---|---|
| Secure 힙 최대 사용 (10240 중) | 4168 | 2088 | +2080 |
| NS 힙 최대 사용 (8192 중) | 2792 | 2792 | +0 |
| Secure MSP 사용 (NS 시작 후) | 208 | 208 | +0 |
| NS MSP 사용 (NS 시작 후) | 320 | 320 | +0 |

### Secure context 스택 (Secure 힙 안, 각 1024 B)

| Task | S | I | Δ |
|---|---|---|---|
| `DMA_Task_A` | 208 | 할당 없음 | — |
| `DMA_Task_B` | 208 | 할당 없음 | — |
| `IDLE` | 4 | 4 | +0 |
| `Plain_Task` | 16 | 16 | +0 |

### NS Task 스택

| Task | 크기 | S | I | Δ |
|---|---|---|---|---|
| `DMA_Task_A` | 1024 | 128 | 64 | +64 |
| `DMA_Task_B` | 1024 | 128 | 64 | +64 |
| `Plain_Task` | 1024 | 120 | 120 | +0 |
| `IDLE` | 2048 | 72 | 72 | +0 |
| `Tmr Svc` | 2048 | 120 | 120 | +0 |

### 기능 확인 (스택 측정 뒤 따로 센 구간)

| 항목 | S | I |
|---|---|---|
| 완료 ISR 횟수 / 초 / tick | 29 / 10 / 8636; 30 / 10 / 8642 | 30 / 10 / 8646; 30 / 10 / 8644 |
| Task A Cluster 8 B | 0x2001b600: [0, 1, 2, 3, 4, 5, 6, 7]; 0x2001b600: [0, 1, 2, 3, 4, 5, 6, 7] | 0x2001b4a0: [0, 1, 2, 3, 4, 5, 6, 7]; 0x2001b4a0: [0, 1, 2, 3, 4, 5, 6, 7] |
| Task B Cluster 8 B | 0x2001b900: [0, 1, 2, 3, 4, 5, 6, 7]; 0x2001b900: [0, 1, 2, 3, 4, 5, 6, 7] | 0x2001b5c0: [0, 1, 2, 3, 4, 5, 6, 7]; 0x2001b5c0: [0, 1, 2, 3, 4, 5, 6, 7] |
| dmaStatus A / B | 0 / 0; 0 / 0 | 0 / 0; 0 / 0 |
| DMA1_CH1 CCR (NS alias 로 읽음) | 0x20000; 0x20000 | 0x60cb; 0x60cb |
| Secure MPU 영역 4 / 5 (RBAR, RLAR) | ['0x2001b603', '0x2001b6e1'] / ['0x2001b903', '0x2001b9e1'] | — |
| MPCBB1 VCTR13 | 0x4800000 | — |

### 정적 프레임 (`-fstack-usage`, 함수 하나의 프레임, 호출 깊이 미포함)

| 함수 | 세계 | S | I |
|---|---|---|---|
| `SECURE_dma_m2m_start` | Secure | 16 (static) | 없음 |
| `verify_inst` | Secure | 0 (static,ignoring_inline_asm) | 없음 |
| `SECURE_dma_m2m_init` | Secure | 16 (static,ignoring_inline_asm) | 없음 |
| `DMA1_Channel1_IRQHandler` | Secure | 0 (static) | 없음 |
| `GTZC_IRQHandler` | Secure | 0 (static,ignoring_inline_asm) | 없음 |
| `DFSDM1_FLT3_IRQHandler` | NonSecure | 24 (static) | 없음 |
| `DMA1_Channel1_IRQHandler` | NonSecure | 없음 | 24 (static) |
| `vDmaTaskA` | NonSecure | 16 (static) | 24 (static) |
| `vDmaTaskB` | NonSecure | 16 (static) | 24 (static) |

## 기본 빌드 (Task B 수정 확인, chk_default_P1)

| 항목 | 값 |
|---|---|
| 완료 ISR 횟수 / 초 | 30 / 10 |
| Task A Cluster | 0x2001b700: [0, 1, 2, 3, 4, 5, 6, 7], status 0 |
| Task B Cluster | 0x2001ba00: [0, 1, 2, 3, 4, 5, 6, 7], status 0 |
| Secure MPU 영역 4 / 5 | ['0x2001b703', '0x2001b7e1'] / ['0x2001ba03', '0x2001bae1'] |
| MPCBB1 VCTR13 | 0x9000000 |

