# D-Box 비교 기준값

출처: A. Mera et al., "D-Box: DMA-enabled Compartmentalization for Embedded Applications", NDSS 2022.
https://www.ndss-symposium.org/wp-content/uploads/2022-53-paper.pdf (로컬: docs/paper/dbox-ndss22.pdf, 미커밋)

## 조건
| 항목 | D-Box | 근거 |
|---|---|---|
| 보드/MCU | NUCLEO-L152RE, STM32L152RE Cortex-M3 @32MHz | §VII |
| OS | FreeRTOS 10.4.1 | §VII |
| 측정 | DWT CYCCNT, 수동 계측, 10샘플 평균 | §VII-B1 |
| 최적화 | -O0, GNU Tools 7-2018-q2. §VII-D 메모리 분석용 이미지에 대한 서술이며 §VII-B 측정에 대해서는 명시 없음 | §VII-D |
| 검증 주체 | 특권 DMA 태스크. 요청은 FreeRTOS SVC 인터페이스(큐, 알림) | §VI-B |
| 검증 내용 | 태스크 영역 + 주변장치 capability 정책 | §VI, §VII-B1 |
| 캐시/플래시 대기 | 논문에 명시 없음 | 미확인 |
| "DMA validation" 측정 경계 | 시작·끝 지점 명시 없음 | 미확인 |
| "Context switch" 측정 경계 | "context switch routine" 이라고만 서술 | §VII-B1, 세부 미확인 |

## Table IV (cycles)
| 항목 | F-MPU | D-Box | 오버헤드 % |
|---|---|---|---|
| Context switch | 287 | 292 | 1.74 |
| Task creation 0 | 12730 | 13047 | 2.49 |
| Task creation 1 | 12730 | 13332 | 4.73 |
| Task creation 2 | 12730 | 13617 | 6.97 |
| Task creation 3 | 12730 | 13902 | 9.21 |
| DMA validation Min | n/a | 569 | n/a |
| DMA validation Avg | n/a | 657 | n/a |
| DMA validation Max | n/a | 792 | n/a |

검증 비용이 주변장치·동작마다 다른 원인은 off-chip 주소 체계(EAR)다: I2C 가 가장 높고 USART 가 가장 낮다 (§VII-B1).

## Table X (오버헤드 %, min/avg/max)
| 동작 | Pool | IT | Insecure DMA |
|---|---|---|---|
| USART-TX | 0.8 / 3.8 / 46.3 | 0.3 / 1.5 / 16.0 | 0.3 / 1.2 / 13.3 |
| USART-RX | -2.4 / -0.2 / 2.9 | -2.9 / -0.6 / 1.6 | -2.7 / -0.1 / 2.8 |
| SPI-TX | 2.6 / 10.4 / 134.5 | 1.0 / 4.6 / 73.2 | 0.9 / 4.3 / 72.8 |
| SPI-RX | -7.6 / 0.3 / 114.0 | 1.3 / 4.7 / 54.2 | 0.8 / 3.0 / 35.5 |
| I2C-TX | -10.0 / -1.1 / 103.3 | -0.5 / 1.7 / 38.6 | 10.9 / 12.3 / 45.6 |
| I2C-RX | 1.9 / 9.4 / 89.7 | -0.7 / 1.6 / 33.9 | 0.6 / 8.0 / 14.5 |
| ADC | -80.4 / -67.9 / 88.4 | -86.9 / -77.2 / 72.7 | 1.1 / 4.2 / 34.0 |
| Average | -13.60 / -6.48 / 82.73 | -12.63 / -9.12 / 41.46 | 1.70 / 4.71 / 31.21 |

매크로 조건: 1~100 바이트(ADC 는 샘플), 길이별 10회 평균 (§VII-B2). 길이 간격은 논문에 명시 없음(미확인). 25바이트 이상에서 오버헤드 10% 미만 (부록 C).
