# 메모리 자동 배치 결과: 보드 확인 + 메모리 오버헤드와 원인

대상: `scripts/build_all.sh` 산출물 (커밋 `d7e0fa3`).
- Task A: `s_taskA = { priv; dst[256] }`
- Task B: `s_dmaDstB`
- 공유: `s_dmaShared` 64 B, Task B cluster 끝에 놓임
- guard 2개

비교 기준은 프로젝트의 수동 배치(GCC baseline, `s_dmaTaskMem[2]`)입니다.
배치 설명은 `explainMemoryAuto.md` 를 보세요.
아래 숫자는 모두 ELF 심볼과 섹션 크기에서 읽은 값입니다 [빌드 실측].

## 1. 보드 확인 (2026-11-28, STM32L562E-DK + STLINK-V3) [실측]

스크립트는 `tests/board/*.gdb`, 로그는 `build/board/*.log` 입니다.
처음에는 STLINK 가 USB 통신 오류(`DEV_USB_COMM_ERR`)를 내서 붙지 못했습니다. 사용자가 USB 를 다시 꽂은 뒤 모든 단계를 실행했습니다.

| 단계 | 결과 | 판정 |
|---|---|---|
| 1 부팅 | `dma_boot_config()` = 0. RNR4 `20018103/200181e1`, RNR5 `200183c3/200184e1`, MPCBB1 VCTR[12] = `0x24` (블록 386, 389) | 기대와 같음 |
| 2 정상 DMA (60 s) | okCount A 122, B 87. `s_taskA.dst` 00..07, `s_dmaDstB` 80..87, `s_dmaShared` 00..07, `+32` 80..87. dmaStatus 0 | 같음 |
| 3 요청 시점 공격 | `attack = {0,-1,-1,-1,-1,-1,0,-1}`. **[2] 같은 struct 의 globals (`s_taskA.priv`) 거부** (NS-MPU 는 허용, S-MPU 태그 밖). SFSR/CFSR 0 | 같음 |
| 5 SVC 경로 | mediator 첫 명령에서 IPSR 0xb, MSP_S 0x30017f58, PSP_S 0, 호출자 `SVC_Handler`. SFSR/CFSR 0 | 같음 |
| 4 쓰기 guard A | CNDTR1 8 → 528, CPAR1 = NS flash. `s_privB` (guard 너머 16 B) 가 flash 값(0xffffffff)으로 바뀌지 않음 → guard A 에서 멈춤. Secure 에서 본 guard A 도 flash 값 아님. s_busy 0. **okCount A 7→13→19, B 6→10→14 로 계속 증가** (복구 전 이미지는 7/6 에서 멈췄음) | 같음 (D8 복구 동작) |
| 4 읽기 guard A | 정지점이 부팅 시 `attack[0]` 요청에 걸림 → CPAR1 = guard A, CNDTR1 16. dst 에 0 이 10 B 쓰이고 멈춤 (RAZ), ISR = 0 (**TEIF 없음**), `attack[0]` = **−2** (status 2 가 NS 까지 전달), 이후 계속 실행 | 보호·복구는 같음. TEIF 없음은 D7 그대로 |
| 4c 공유 버퍼 → guard B | 정지점이 `attack[6]` (`s_dmaShared`) 에 걸림 → CNDTR1 8 → 336. guard B 너머 `0x20018600` (xKernelObjectPool) 무변조, guard B 무변조, s_busy 0, `attack[6]` = **−2**, A/B 계속 실행 | 같음 |
| 6 stale 짝 | t_bind Secure + t_stale NS: `Error_Handler`, `g_dma_boot_status` = −7 | 같음 |

마지막에 보드에는 본 이미지(`build/secure/secure.elf` + `build/ns/ns.elf`)를 다시 올려 두었습니다.

## 2. 메모리 오버헤드 요약

"오버헤드" 는 DMA 보호 때문에 생기는 메모리만 셉니다. 버퍼 자체와 태스크 globals 의 내용은 넣지 않습니다.

| 항목 | 수동 배치 (baseline) | LLVM 자동 배치 | 차이 |
|---|---|---|---|
| guard (MPCBB 256 B 블록) | 512 B (2개) | 512 B (2개) | 0 |
| globals 를 256 B 로 채운 패딩 | 488 B (2 × 244) | 0 | −488 |
| `aligned(256)` 배열 앞 정렬 구멍 | 152 B | 0 | −152 |
| cluster 앞 빈 칸 중 못 채운 부분 | — | 123 B (c0 27 + c1 96) | +123 |
| cluster 크기 32 B 올림 낭비 | 0 | 0 | 0 |
| **NS SRAM 합계** | **1152 B** | **635 B** | **−517 B (−45 %)** |
| NS Flash | — | +264 B | +264 |
| Secure Flash (`.text`, GCC -O0) | 11208 B | 11208 + 808 B (배치용 부팅 코드) | +808 |
| Secure SRAM | `.data` 24 B | `.data` 32 B | +8 |
| S-MPU region | 2 | 2 | 0 |
| NS-MPU configurable region (DMA 태스크) | 태스크당 1 | A 2, B 2 | 태스크당 +1 (이유는 §3.4) |

Secure `.text` 는 실제로 12080 B 입니다. 그중 64 B 는 `GTZC_IRQHandler` 복구 코드(D8)라서 배치 오버헤드에서 뺐습니다.

## 3. 원인별 설명

### 3.1 NS SRAM 이 줄어든 원인: 정렬 패딩이 없어짐
수동 배치는 `[globals | cluster | guard]` 를 struct 하나로 묶고 `aligned(256)` 을 붙였습니다 (`app_freertos.c` 원본 `dma_task_mem_t`). 그래서 두 가지 낭비가 생깁니다.
1. guard 를 256 B 경계에 맞추려면 cluster 가 256 B 경계에서 시작해야 합니다. 그래서 그 앞의 globals(12 B)를 **256 B 로 채웁니다** (`raw[256]` union). 태스크마다 244 B, 합계 488 B 입니다.
2. `aligned(256)` 배열이 일반 `.bss` 중간에 놓이므로 앞 객체(`s_dmaSrcA` 끝 0x2001b568) 뒤로 **0x2001b600 까지 152 B 구멍**이 생깁니다.

자동 배치는 두 가지를 바꿨습니다.
- cluster 를 guard 쪽에 붙이고(오른쪽 정렬), 앞 칸은 일반 `.bss` 로 채웁니다 (`gen_layout.py` 의 fill).
- cluster 영역을 NS SRAM 맨 앞(0x20018000, 원래 256 B 경계)에 둡니다.

그래서 1번과 2번이 모두 0 이 되었습니다.

### 3.2 남은 NS SRAM 낭비 123 B 의 원인: 앞 칸을 채울 작은 객체가 부족함
- c0 앞 칸 160 B 에는 133 B 가 들어갔습니다 (`htim6` 76, `KernelState`, `uwTick` 등). 27 B 는 크기가 맞는 객체가 없어 남았습니다.
- c1 앞 칸 192 B 에는 `s_privB` 96 B 만 들어가고 96 B 가 남았습니다.
- 채울 수 있는 후보는 1차 링크 map 의 `.bss.<이름>` 입력 섹션 16개뿐입니다. 첫 채우기 뒤 남은 8개는 모두 256 B 이상(태스크 스택 1024 B, `Idle_TCB` 888 B, `ucPrivateBuffer` 256 B 등)이라 96 B 칸에 들어가지 않습니다.
- **후보가 적은 이유:** FreeRTOS-MPU 커널 변수는 `.bss` 가 아니라 `privileged_data` 입력 섹션에 모여 있습니다. 1차 map 에서 0x26bc B 와 0x118 B 짜리 큰 묶음 두 개입니다. 이 묶음은 객체별로 나뉘지 않아 칸에 넣을 수 없습니다.
- **해석 [추정]:** 이 낭비는 앱 크기에 따라 달라집니다. 작은 `.bss` 객체가 많은 앱이면 0 에 가까워지고, 최악은 cluster 하나당 256 B − 32 B 입니다.

### 3.3 guard 는 같음, 공유 버퍼는 guard 를 늘리지 않음
- 공유 버퍼를 따로 cluster 로 두면 guard 256 B 와 S-MPU region 1개가 더 듭니다 (이전 D3 방식은 SRAM 816 B).
- 지금은 Task B cluster 끝(guard B 바로 앞)에 붙였으므로 guard 는 수동 배치와 같은 2개입니다.
- 대가: B 가 `s_dmaDstB` 에서 넘치면 guard 가 아니라 공유 버퍼로 들어갑니다. B 는 원래 공유 버퍼에 쓸 권한이 있습니다.

### 3.4 NS-MPU region 이 태스크당 1개씩 늘어난 원인
- **Task A 2개:** 하나는 `s_taskA` (priv + dst, struct 라서 연속), 하나는 공유 버퍼 칸입니다. 공유 버퍼가 Task B 블록에 있어 A 의 struct 와 떨어져 있기 때문입니다. 수동 배치에는 공유 버퍼가 없었으므로 이 1개는 "공유 버퍼 기능의 비용" 입니다.
- **Task B 2개:** `s_privB` 하나, dstB + 공유 하나입니다. `s_privB` 는 c1 앞 칸 맨 앞(0x20018300)에 들어가고, cluster 는 0x200183C0 에서 시작합니다. 둘 사이 96 B 가 비어 있어서 region 하나로 합칠 수 없습니다.
  - 수동 배치는 globals 를 struct 로 cluster 앞에 붙였기 때문에 1개였습니다.
  - 자동 배치에서도 Task A 처럼 `s_privB` 와 `s_dmaDstB` 를 struct 로 묶으면 1개가 됩니다. 앱 코드 구조의 문제이고 도구의 한계는 아닙니다.
- 두 태스크 모두 `portNUM_CONFIGURABLE_REGIONS` = 3 안에 들어갑니다 (`_Static_assert`).

### 3.5 NS Flash +264 B 의 원인

| 항목 | 크기 | 설명 |
|---|---|---|
| `g_dma_manifest` (0x08040400) | 172 B | 머리 12 B + 8칸 × 20 B. **최대 8칸 고정 크기**라서 cluster 2개만 써도 172 B (빈 6칸 120 B) |
| `.dma_zero_table` | 16 B | cluster 2개 × (시작, 끝) |
| `dma_zero_clusters()` | 76 B | NOLOAD cluster 를 0 으로 채우는 startup 보조 함수 |

- 수동 배치에서는 cluster 가 일반 `.bss` 안에 있어 startup 이 같이 지웠습니다. 주소는 Secure 코드의 상수였습니다. 그래서 이 세 가지가 필요 없었습니다.
- NS 전체 Flash 는 비교하지 않았습니다. 컴파일러가 다르기 때문입니다 (GCC -O1 대 clang -O2 LTO).

### 3.6 Secure Flash +808 B 의 원인: 주소를 "상수" 대신 "manifest 검증" 으로 받음
- 수동 배치의 `SMPU_SetupTaskRegions` 는 cluster 주소를 상수로 적어 두고, S-MPU 2번과 MPCBB 2번 호출만 하는 212 B 코드입니다.
- 자동 배치에서는 주소를 NS 빌드가 정하므로, Secure 부팅 코드 `dma_boot_config()` 가 NS flash 의 manifest 를 읽습니다. NS 가 만든 데이터는 믿을 수 없으므로 먼저 Secure 쪽에 복사하고 검증해야 합니다.

`dma_boot_config` 802 B 의 줄 단위 내역 (`objdump -dl`):

| 부분 | 크기 |
|---|---|
| 엔트리 복사 + 정렬·범위 검사 | 272 B |
| MPCBB guard 이중 반복문 | 142 B |
| cluster 끼리 겹침 검사 | 134 B |
| S-MPU 태그 반복문 | 116 B |
| magic, version, count 검사 | 64 B |
| 스택 프레임 | 50 B |
| `guard_bind_init` 호출 | 24 B |

- 여기에 `guard_bind_init` 88 B, `prvSetTag` 64 B, `prvMaxTags` 44 B 가 더해집니다.
- **-O0 영향이 큽니다.** Secure 는 GCC -O0 으로 빌드합니다. 같은 `secure_dma_boot.c` 를 -O1 로 빌드하면 456 B, -Os 로는 408 B 입니다 [실측: 오브젝트 크기].
- 이 코드는 부팅 때 한 번만 돕니다. 요청마다 쓰는 mediator 와 context 전환 명령열은 baseline 과 같습니다 (`tests/run_tests.sh` T5: 44 / 7 / 11 명령).

### 3.7 Secure SRAM +8 B
`.data` 가 24 B 에서 32 B 가 되었습니다. `g_dma_boot_status` (부팅 결과, GDB 확인용) 등이 늘어난 것입니다.

## 4. 결론
- 같은 보호(태스크당 guard 1개, S-MPU 태그 1개)를 유지하면서 **NS SRAM 오버헤드는 1152 B → 635 B 로 줄었습니다.** 수동 배치의 정렬 패딩(640 B)이 없어졌고, 앞 칸을 다 못 채운 123 B 가 새로 생겼습니다.
- 그 대신 **Flash 가 늡니다.** NS +264 B (manifest, 0 초기화 표와 함수), Secure +808 B (manifest 검증 부팅 코드, -O0 기준. -Os 면 약 400 B 대 [추정]).
- 런타임 비용은 바뀌지 않습니다. 요청마다 쓰는 mediator 와 context 전환 명령열이 baseline 과 같습니다. 사이클은 보드 측정이 필요합니다.
