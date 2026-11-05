# PoC 구현 현황 (논문 §3, §4 대응)

조사일: 2026-10-31. 대상: `git status` 기준 워킹트리 (커밋 0a6687a + 미추적 변경 없음 — 아래 코드는 전부 이미 커밋된 상태).
완성도 기준: **동작확인**(빌드되고 의도대로 실행되는 것이 코드상 분명) / **부분**(일부 로직만 구현, 핵심 누락) / **스켈레톤**(틀만 있고 내용 없음·하드코딩 테스트용) / **없음**.

## 0. 빌드 상태 [실측]

`./build.sh clean && ./build.sh` 실행 결과 **Secure, NonSecure 모두 빌드 성공** (exit 0).

```
FreeRTOS_SecureIOToggle_TrustZone_Secure.elf    : text=10288  data=40    bss=11976
FreeRTOS_SecureIOToggle_TrustZone_NonSecure.elf : text=34584  data=10240 bss=8816
```

경고(에러 아님):
- 두 ELF 모두 "LOAD segment with RWX permissions" — Cortex-M 베어메탈 단일 세그먼트 링크의 통상적 경고, 무시 가능.
- `app_freertos.c:80` unused variable `i` (vLEDTestTask 안의 더미 카운터, 예제 코드 잔재).
- `app_freertos.c:115` `const` qualifier discard — `xRegions[1]`에 `ulLookupTable`(const) 주소를 `TaskParameters_t.xRegions[].pvBaseAddress`(non-const 멤버)에 대입하는 ST 샘플 코드의 기존 경고. 동작에는 영향 없음.

결론: **보드 실측 전 단계에서 코드는 문제없이 빌드된다.** (CLAUDE.md "설정 시점 빌드 테스트 통과" 상태 유지)

## 1. §3.1 메모리 배치 (DMA Cluster + 가드 블록 레이아웃)

| 위치 | 내용 | 완성도 |
|---|---|---|
| `project/NonSecure/Src/main.c:24-55` | `DMA_ALLOCATE_BUFFER`/`DMA_ALLOC_G_IMPL` 매크로: pad_upper(256B)+body+pad_lower(256B) 구조체를 만들고 `.guard_table.<uniq>` 섹션에 설명자를 심는 설계. **전체가 블록 주석(`/* ... */`)으로 비활성화**, 아무 태스크도 사용하지 않음. | 스켈레톤 (미사용) |
| `project/STM32CubeIDE/NonSecure/STM32L562QEIXQ_FLASH.ld:209-216` | `.guard_table` 출력 섹션(Flash, `KEEP(SORT_BY_NAME(.guard_table.*))`)만 존재. 이를 채우는 코드가 없으므로 링크해도 빈 섹션. | 스켈레톤 |
| 링커 스크립트 전체 | "태스크 전역 데이터 → Cluster → 가드 블록" 순서를 강제하는 섹션 구분이 **없음**. `.data`/`.bss`는 모든 태스크가 뒤섞인 단일 섹션(통상 FreeRTOS-MPU 배치)이고 Cluster 전용 출력 섹션이 정의되어 있지 않음. | 없음 |
| `project/NonSecure/Src/app_freertos.c:70-119` | 실제 생성되는 태스크는 `vLEDTestTask` 하나뿐이며 `ucPrivateBuffer`/`ulLookupTable` 두 NS-MPU 영역만 가짐. DMA 버퍼/Cluster 개념이 전혀 없는 일반 FreeRTOS-MPU 예제. | 없음 |
| `project/Secure/Src/main.c:388-407` `SMPU_SetupProbeRegion()` | S-MPU 영역 4번(region 4)을 고정 주소 `0x20018500`, 크기 256B로 1개만 설정. Cluster 테이블을 순회하며 여러 개를 설정하는 로직이 아니라 **TTT 사이클 측정용으로 하드코딩된 단일 probe 영역**. | 스켈레톤 |
| `project/Secure/Src/main.c:190-258` `MX_GTZC_S_Init()` | MPCBB를 SRAM1/SRAM2 전체에 대해 설정하지만, 이는 ST 원본 데모의 "앞쪽 다수 워드=NS 전부, 뒤쪽 소수 워드=Secure 전부" 고정 분할이다. 태스크별 가드 블록(256B)을 주소에 맞춰 개별 지정하는 로직이 아님. | 부분(원본 그대로, Cluster 연동 없음) |

**요약**: §3.1의 "수동 배치 + 빌드 시 테이블 저장" 설계는 매크로/링커 섹션의 **뼈대만 작성되고 주석 처리된 상태**이며, 실제로 연결되어 동작하는 코드는 없다.

## 2. §3.2 요청 시점 검증 (Secure DMA 중재자)

| 위치 | 내용 | 완성도 |
|---|---|---|
| `project/Secure/Src/main.c:333-358` `Secure_DMA_LockAllChannels()` | DMA1/DMA2 채널 0~6에 대해 `CCR.SECM=1`, `CCR.SSEC=CCR.DSEC=0` 설정 → 각 채널을 Secure 전용으로 고정, NS가 직접 설정 불가. | 동작확인 (단, TZSC 레지스터가 아니라 DMA 채널 자체의 CCR 보안 비트 사용 — §gap-analysis 참고) |
| `project/Secure/Src/secure_nsc.c:139-154` `SECURE_dma_m2m_init()` | DMA1 채널1을 M2M 모드로 초기화, NS 콜백 포인터를 `cmse_nsfptr_create`로 안전하게 변환. | 동작확인 |
| `project/Secure/Src/secure_nsc.c:200-218` `verify_inst()` (naked inline asm) | **TTT 명령 2회**로 `base`, `base+len-1` 각각의 TT 결과를 얻어 (a) 덧셈 캐리로 주소 오버플로 검출, (b) 두 결과의 하위 바이트(= S-MPU 영역 번호) 동일성 비교, (c) `base` 쪽 TT 결과 비트21(`nonsecure_readwrite_ok`)만 확인. | **부분** — 아래 2개 핵심 누락 발견(§gap-analysis 1,2) |
| `project/Secure/Src/secure_nsc.c:220-238` `SECURE_dma_m2m_start()` | `verify_inst` 통과 시에만 `DMA_CH->CM0AR/CPAR/CNDTR` 설정 후 `CCR|=EN`으로 전송 시작. 검증 실패 시 `-1` 반환, 전송 안 함. | 동작확인 (verify_inst의 한계는 그대로 전파) |
| `project/Secure/Src/secure_nsc.c:161-183` `g_dma_table[8]`, `verify_table()` | Flash 테이블 방식 대안 구현. 8개 고정 엔트리, 선형 탐색. **CLAUDE.md는 "주석 처리"를 요구했으나 현재 코드에서 `#if 0`로 감싸져 있지 않고 그냥 미사용(dead code) 상태.** `SECURE_dma_m2m_start`는 이 경로를 호출하지 않음. | 스켈레톤(미사용, 지침과 다른 처리 상태) |
| `project/Secure/Src/secure_nsc.c:93-124` (`#if 0`) `SECURE_dma_buffer_request`, `mpcbb_mark_block_secure` | 런타임에 arena에서 버퍼를 할당하며 동적으로 가드 블록을 마킹하는 초기 설계 스케치. `DMA_ARENA_*` 매크로/변수가 어디에도 정의되어 있지 않아 **컴파일 불가능한 폐기된 초안**(그래서 `#if 0`로 막아둔 것으로 보임). CLAUDE.md의 "구현하지 말 것" 목록(동적 재설정류)과도 상충하는 방향이라 폐기가 맞아 보임. | 없음(의도적 폐기) |

**요약**: 호출자 NS-MPU 검증(TTAT/TTA)은 전혀 구현되어 있지 않고, S-MPU 태그 검증도 "유효성(mpu_region_valid)"은 확인하지 않는다 — 자세한 내용은 gap-analysis.md 참고.

## 3. §3.3 전송 시점 보호 (MPCBB 가드 블록 + TZIC)

| 위치 | 내용 | 완성도 |
|---|---|---|
| `project/Secure/Src/main.c:361-380` `Secure_TZIC_Init()` | `GTZC_TZIC_S->FCR1/2/3`를 전부 클리어(0xFFFFFFFF 라이트-클리어), `IER3 |= 0b10000`로 **SRAM1F(비트4) 인터럽트만 unmask**, `NVIC_EnableIRQ(GTZC_IRQn)`. | 동작확인 (CLAUDE.md의 "MPCBB는 bus error 없이 무시, SR3.SRAM1F로 통지" 사실과 일치) |
| `project/Secure/Src/secure_nsc.c:267-275` `GTZC_IRQHandler()` | `DMA_CH->CCR &= ~EN`으로 채널 중단 후 `GTZC_TZIC_S->FCR3 = 0b10000`로 플래그 클리어. | 동작확인 (단일 채널 `DMA_CH`=DMA1_Channel1 고정) |
| `project/Secure/Src/stm32l5xx_it.c:230-253` | 원본 ST 템플릿의 `GTZC_IRQHandler`(HAL 콜백 방식)는 **`#if 0`로 비활성화**되어 secure_nsc.c의 버전과 충돌하지 않음. | 없음(의도적 비활성화, 올바름) |
| 가드 블록이 실제 태스크 Cluster 경계에 적용되는 경로 | §1에서 보듯 Cluster/가드 블록 배치 자체가 없으므로, TZIC 핸들러가 "막아주는 대상"이 `SMPU_SetupProbeRegion`의 **고정 테스트 영역 하나**뿐이다. | 부분 (메커니즘은 있으나 실제 보호 대상과 미연결) |

**요약**: 전송 시점 탐지·차단의 하드웨어 배선(TZIC 설정, 핸들러, 채널 중단)은 **그 자체로는 완성**되어 있으나, §3.1이 비어있는 탓에 "무엇을 지키는지"가 아직 실제 Cluster가 아니다.

## 4. 커널 (FreeRTOS, Middlewares/Third_Party/FreeRTOS/Source)

- 저장소 내 `git diff baseline-original..HEAD -- Middlewares/`는 **빈 diff** (경로 정규화 커밋 이후 변경 없음, 즉 import 시점 그대로).
- 커널 트리 전체에서 `DMA|TZIC|GTZC|MPCBB|Cluster` 키워드 검색 → **0건**. DMA 보호 기법을 위한 커널 자체 수정(예: 신규 syscall, MPU 래퍼 확장)은 찾지 못했다.
- `configUSE_MPU_WRAPPERS_V1=0`(→ v2 사용), `configTOTAL_MPU_REGIONS=8`, 포트는 `portable/GCC/ARM_CM33/{non_secure,secure}` — 모두 표준 V11.3.1 MPU wrappers v2 설정과 일치.
- **문제**: 비교 기준으로 쓸 순정 V11.3.1 트리를 이 머신에서 찾지 못했다. `/home/sysop/STM32CubeL5/Middlewares/Third_Party/FreeRTOS`(symlink 원본, 사용자의 fork)에는 `vendor-baseline`라는 태그가 있지만 커밋 메시지가 "pristine vendor copy: FreeRTOS-Kernel **10.3.1**"이라 버전이 다르고, 그 태그 대비 diff는 862개 파일 변경(차세대 포트 추가 등 버전업 때문)으로 의미 있는 비교가 안 된다.
- **결론**: 현재 가진 증거로는 "커널이 V11.3.1 대비 DMA 보호를 위해 수정됐다"는 CLAUDE.md의 서술을 **확인도 반증도 하지 못했다** (근거: [실측] grep 결과 없음 / [미확인] 정확한 upstream diff). gap-analysis.md 질문 목록에 포함.

## 5. §4 평가 재현 (표 2 사이클 측정, 공격 시나리오 2개)

| 항목 | 위치 | 완성도 |
|---|---|---|
| 8B M2M, 32회 반복, DWT 사이클 카운터, nop veneer 기준선 측정 | `project/NonSecure/Src/main.c:438-575` (`dma_test`, `BENCH_N=64`, `N_ITER=32`, `XFER_SZ=8`) | **스켈레톤** — 로직은 표 2 방법론과 합치하나 **전체가 `#if 0`**이고, 이 함수를 호출하는 태스크 생성 코드도 없음(`DMATestHandle` 관련 코드 전부 주석/`#if 0`). |
| 빈 NSC 진입·복귀 측정(`SECURE_nop3`) | `project/Secure/Src/secure_nsc.c:242-244`, 호출부 `main.c:495,501` | 함수 자체는 살아있는 코드(활성)이나, 호출하는 벤치마크 루프가 `#if 0`라 **미실행**. |
| 검증 포함 측정(S-MPU 태그 / 테이블) 2개 행 비교 | 같은 `dma_test` 내부 | `SECURE_dma_m2m_start`가 `verify_inst`(S-MPU 경로)만 호출하므로, 지금 상태로 전부 켜도 **테이블 방식 행은 측정 불가**(코드 전환 필요). |
| UART DMA 태스크 + 다른 태스크들로 구성된 Application (CLAUDE.md 평가 재현 섹션 요구사항) | 없음 | **없음** — `app_freertos.c`에는 LED 토글 태스크 하나뿐, UART/USART 코드가 어디에도 없음(HAL 설정 헤더의 기본 include 외). |
| 공격 시나리오 (a) 인자 조작 | 없음 | **없음** |
| 공격 시나리오 (b) 검증 통과 후 길이 변조(Overflow) | 없음 | **없음** |

**요약**: §4 재현에 필요한 코드는 사실상 전무하다. 유일한 자산은 비활성화된 벤치마크 스켈레톤 하나뿐이고, 공격 시나리오 2개는 코드로도 설계로도 아직 존재하지 않는다.
