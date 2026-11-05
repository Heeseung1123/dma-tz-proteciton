# 버퍼 단위 DMA 보호 (ARMv8-M TrustZone, STM32L562E-DK)

논문 **"ARMv8-M TrustZone을 이용한 RTOS 환경에서의 버퍼 단위 DMA 보호 기법"**의 프로토타입 구현과 사이클 평가 코드입니다. Secure DMA 중재자와 MPCBB 가드 블록으로, 상용 MCU에서 DMA 접근을 버퍼 단위로 검증하고 전송 중 경계 초과를 차단합니다.

- 논문: [`docs/paper/paper.pdf`](docs/paper/paper.pdf), [`docs/paper/paper.txt`](docs/paper/paper.txt)
- 측정 보고서: [`results/report.md`](results/report.md)
- 대상: STM32L562E-DK (Cortex-M33, 110MHz). Secure는 베어메탈, NonSecure는 FreeRTOS V11.3.1 (MPU wrappers v2)

## 논문 요약

### 문제
DMA는 CPU와 독립된 버스 마스터라서 MPU와 DWT 워치포인트처럼 CPU 접근만 검사하는 보호를 우회합니다. MCU에는 IOMMU도 없습니다. 공격 경로는 두 가지입니다.

1. **요청 인자 조작:** 메모리 오염 취약점으로 DMA 요청의 주소와 길이를 조작합니다 (예: CVE-2025-5688).
2. **전송 중 경계 초과:** 인자는 정상이어도 드라이버 결함으로 DMA가 버퍼 경계를 넘습니다 (예: CVE-2026-10644).

선행 연구의 한계는 다음과 같습니다.

| 기법 | 타태스크·커널 | 영역 내부 보호 객체 | 전송 중 경계 초과 | 상용 MCU 적용 |
|---|---|---|---|---|
| MPU+DWT | X | X | X | O |
| D-Box + DWT | O | X | X | O |
| DMAAuth | O | O | O | X |
| **제안 기법** | **O** | **O** | **O** | **O** |

위협 모델은 Secure 펌웨어, NS 커널, 부트 과정을 신뢰 기반으로 둡니다. 물리 공격과 부채널 공격은 범위 밖입니다.

### 설계

**메모리 배치 (§3.1).** 태스크마다 DMA 버퍼를 하나의 연속 구간(DMA Cluster)으로 모으고, 다음 순서로 배치합니다. DMA 주소는 아래로 갈수록 증가합니다.

```
태스크 전역 데이터   (NS-MPU 영역 안, S-MPU 태그 없음)
DMA Cluster         (NS-MPU 영역 안, S-MPU 영역 1개 = TTT 태그)
가드 블록 256B       (MPCBB Secure, NS 데이터를 두지 않음)
```

**요청 시점 검증: Secure DMA 중재자 (§3.2).** DMA 컨트롤러와 채널은 Secure에 두어 NS 코드가 직접 설정할 수 없습니다. 요청 `(base, len)`은 NSC 진입점에서 다음과 같이 검사합니다.

1. `TTAT`/`TTA`로 NS-MPU를 조회해 `base`와 `base+len-1`을 호출자가 쓸 수 있는지 확인합니다.
2. `TTT`로 양 끝점의 S-MPU 영역을 조회해 둘 다 유효하고 같은 영역인지 확인합니다. 그러면 전 범위가 Cluster 안입니다.
3. 통과하면 DMA를 NS 트랜잭션으로 설정해 시작합니다. 그래야 가드 블록이 적용됩니다.

검증 비용은 등록 버퍼 수와 무관하게 TTT 2회지만, 태그 수는 S-MPU 영역 수(8개)로 제한됩니다.

**전송 시점 보호: MPCBB 가드 블록 (§3.3).** 경계를 넘은 NS DMA가 가드 블록에 닿으면 TZIC 인터럽트가 발생하고, 핸들러가 DMA 채널을 중단합니다.

**한계 (§5).** 태스크당 가드 블록 256B와 S-MPU 영역 1개를 추가로 씁니다. S-MPU가 부족한 환경에서는 Flash 테이블 방식으로 대체해야 합니다. NS-MPU 영역은 추가로 쓰지 않습니다.

### 논문의 평가 결과 (표 2)
8바이트 M2M, 캐시가 채워진 상태, 32회 반복, DWT 사이클 카운터.

| 항목 | 평균 (사이클) | 비고 |
|---|---|---|
| 빈 NSC 진입·복귀 | 66 | 상태 전환 약 58 |
| 검증 포함 (Flash 테이블 8항목) | 223 | 최대 525 |
| 검증 포함 (S-MPU 태그) | 160 | TTT×2, 31/32회 동일 |

S-MPU 태그 방식이 테이블 방식보다 평균 63 사이클(약 28%) 빠릅니다. 전송 길이를 Cluster 너머로 변조한 공격은 가드 블록 첫 접근 시 TZIC로 탐지되어 채널이 중단되었고, 가드 너머 메모리는 변조되지 않았습니다. 읽기와 쓰기 모두 같습니다.

## 구현 상태

프로토타입이므로 일부는 논문과 다르게 단순화했습니다. 상세 내용은 [`docs/gap-analysis.md`](docs/gap-analysis.md)에 있습니다.

| 항목 | 상태 |
|---|---|
| 중재자 `verify_inst` | 인라인 어셈블리. `TTAT`×2 + `TTT`×2. 호출자 권한 분기 없이 `TTAT`로 고정 |
| 메모리 배치 | 링커 섹션 없이 구조체 `dma_task_mem_t` 배열로 수동 배치 |
| S-MPU·MPCBB 설정 | `Secure/Src/main.c`에 주소를 하드코딩. 자동 동기화 없음 |
| 소스 버퍼 검증 | 목적지만 검증 |
| 애플리케이션 | M2M DMA 태스크 2개 + DMA 미사용 태스크 1개 |

**알려진 문제 (미수정)**
- Task B의 Secure 하드코딩 주소(0x2001c000)가 실제 Cluster(0x2001ba00)와 달라 B의 요청은 전부 거부됩니다. 평가는 Task A만 대상으로 합니다.
- `GTZC_IRQHandler`가 `s_busy`를 지우지 않고 콜백도 부르지 않습니다 [코드, 보드 미검증].
- `SECURE_dma_m2m_init`을 NS에서 다시 호출해 콜백을 바꿀 수 있어 보입니다 [코드, 보드 미검증].

## 측정 결과

**보안·보호 평가는 아직 수행하지 않았습니다.** 아래는 사이클 측정이고, 공격 시나리오 재현과 negative test는 다음 단계입니다.

### 중재자 사이클
8바이트 M2M, N=1000, Task A, DMA IRQ 차단. 값은 min / avg / max이고, max는 1ms 틱 인터럽트가 끼어든 표본입니다. 전체 표와 정의는 [`results/report.md`](results/report.md)에 있습니다.

| 조건 | `start()`~리턴 | 내부 (NSC 제외) | `verify_inst` | 빈 NSC |
|---|---|---|---|---|
| **P1, 110MHz, 캐시 off (현재 구성)** | **374 / 376.7 / 811** | **200 / 201.5 / 635** | **62 / 62.3 / 401** | **162 / 163.5 / 595** |
| P1, 110MHz, warm | 164 / 164.8 / 411 | 99 / 99.5 / 348 | 21 / 21.0 / 29 | 59 / 59.1 / 173 |
| P1, 32MHz, off | 182 / 185.0 / 405 | 101 / 102.8 / 325 | 28 / 28.2 / 247 | 73 / 75.1 / 294 |
| P2, 110MHz, off | 288 / 291.1 / 724 | 136 / 137.2 / 571 | 61 / 61 / 61 | 147 / 147.7 / 487 |
| P2, 110MHz, warm | 118 / 136.7 / 366 | 58 / 76.2 / 311 | 18 / 18.0 / 48 | 49 / 49.3 / 228 |
| P2, 32MHz, off | 140 / 142.4 / 361 | 61 / 61.9 / 280 | 24 / 25.7 / 245 | 63 / 63.9 / 284 |

P2 110MHz warm은 min이 대표값이 아닙니다. 대부분의 표본이 `start()` 136, 내부 76입니다.

**용어**
- **P1:** Secure -O0, NS -O1 (현재 프로젝트 설정). **P2:** Secure -O1, NS -O1.
- **off:** ICACHE 꺼짐 (현재 프로젝트 설정). **warm:** 켜짐, 채워진 상태. **cold:** 켜짐, 매 회 비움.
- **클럭별 차이:** 플래시 대기가 110MHz에서 5 WS, 32MHz에서 1 WS라서 캐시 off일 때 사이클 수가 다릅니다. warm에서는 거의 같습니다.

**해석**
- 기존 측정값 `statMediator` 828은 IRQ를 막지 않아 전송 완료 ISR과 NS 콜백(약 450 사이클)이 섞인 값입니다.
- P1 110MHz warm의 164와 59는 논문의 160과 66에 가깝습니다. 현재 코드는 논문의 TTT×2보다 TTAT 2개가 많습니다. 논문 측정 당시의 ICACHE·IRQ 조건은 미확인입니다.
- warm 값은 코드 배치에 민감합니다. 측정 코드가 달랐던 이전 측정에서는 같은 조건이 204였습니다.

### D-Box(NDSS'22)와 비교
D-Box 값은 [`docs/dbox_reference.md`](docs/dbox_reference.md)에 정리했습니다 (Cortex-M3, 32MHz). **코어, 측정 경계, 검증 내용이 달라 배수로 비교할 수 없습니다.** context switch와 macro는 이전 측정 코드의 결과로, 커밋 `983a749`의 `results/`에 원본이 있습니다.

| 항목 | D-Box | 제안 기법 | 주원인 |
|---|---|---|---|
| DMA 검증 (사이클) | 569 / 657 / 792 | `verify_inst` 28, 내부 101, `start()` 182 (32MHz, off, min) | TT 명령 기반 vs 소프트웨어 정책 대조 |
| Context switch 증가 | +5 (1.74%) | +271 (71.5%), 32MHz off | MPU 영역 추가 vs secure context 저장·복원 |
| 1~100B 오버헤드, 대 Insecure DMA (평균) | 4.71% | 254.9% (off), 133.2% (warm) | D-Box는 주변장치, 이쪽은 M2M. **% 비교 불가** |
| 전송당 추가 사이클 | 논문에 절대값 없음 | 518~720 (off), 224~385 (warm), 110MHz | 양쪽 모두 전송당 고정 비용 성격 |

context switch 증가는 secure context를 쓰는 태스크, 즉 NSC를 호출하는 DMA 태스크에 해당합니다.

### 미측정
P0(전부 -O0), 디버거 비연결 상태의 CYCCNT 검증, M3 대 M33 코어 차이, 실제 주변장치(USART, SPI, I2C, ADC) DMA, 메모리 오버헤드, CPU 사용률.

## 빌드

```bash
./build.sh                       # Secure -> NonSecure 순서로 빌드
./build.sh clean && ./build.sh   # 클린 빌드
```

툴체인 기본 경로는 STM32CubeIDE 2.2.0 번들입니다. 다른 위치라면 `TOOLCHAIN_BIN=/path/to/bin ./build.sh`로 지정합니다.

**주의:** `Secure/Src/main.c`의 Cluster 주소는 NS 빌드의 `s_dmaTaskMem` 주소를 손으로 옮긴 값입니다. NS `.bss`가 바뀌면 함께 갱신해야 합니다. `scripts/eval/check_layout.sh`가 Task A 기준으로 이를 확인합니다.

## 측정 재현

```bash
~/st-link.sh                                  # 별도 터미널: ST-LINK GDB 서버 (포트 61234)
scripts/eval/run_all.sh                       # 조건별 빌드 -> flash -> 측정 -> CSV
python3 scripts/eval/analyze.py               # 표 출력
```

보드 이미지를 덮어씁니다. `~/st-link.sh`는 개발 PC의 스크립트이므로 환경에 맞게 준비해야 합니다.

| 빌드 매크로 | 의미 |
|---|---|
| `BENCH_PHASE` | 측정 코드 포함 |
| `BENCH_TS` | 중재자 안의 T1, T4 기록 |
| `BENCH_ICACHE`, `BENCH_COLD` | 캐시 켬, 매 회 비움 |
| `BENCH_CLK_32M` | 32MHz |
| `BENCH_N` | 반복 수 (기본 1000) |

매크로를 모두 끈 기본 빌드는 측정 코드가 들어가지 않습니다. 원본 커밋 `9679590`과 `verify_inst`, 중재자, 두 핸들러의 디스어셈블 및 NS 코드가 같습니다.

## 레포 구조

```
project -> Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone
  Secure/Src/secure_nsc.c       verify_inst, 중재자, DMA·TZIC 핸들러
  Secure/Src/main.c             S-MPU, MPCBB, TZIC, DMA 잠금 설정
  Secure/Src/bench_s.inc        측정 코드 (Secure)
  NonSecure/Src/app_freertos.c  평가용 애플리케이션과 메모리 배치
  NonSecure/Src/bench_ns.inc    측정 코드 (NS). 측정 구간 설명은 파일 맨 위
Middlewares/Third_Party/FreeRTOS/Source   커널
Drivers/                                  CMSIS, HAL, BSP
scripts/eval/                             빌드, 측정, 집계
results/                                  측정 결과와 보고서
docs/                                     논문, gap 분석, 변경 이력, D-Box 기준값
```

| 문서 | 내용 |
|---|---|
| [`CLAUDE.md`](CLAUDE.md) | 설계 요약, 확정된 하드웨어 사실, 작업 규칙 |
| [`EVAL.md`](EVAL.md) | 평가 계획 |
| [`docs/gap-analysis.md`](docs/gap-analysis.md) | 논문과 코드의 차이 |
| [`docs/CHANGES.md`](docs/CHANGES.md) | 변경 이력 |
| [`docs/SETUP_REPORT.md`](docs/SETUP_REPORT.md) | 원본 위치, 복사 범위 |

Git 태그 `baseline-original`은 원본 그대로이고, `baseline`은 작업 시작점입니다.

## 라이선스
STMicroelectronics와 FreeRTOS 소스는 각 디렉터리의 라이선스를 따릅니다. 이 레포 전체에 적용되는 별도 라이선스 파일은 아직 없습니다.
