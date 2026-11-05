# 구현 메모 (2026-10-31) — §3.2 NS-MPU 검증 추가 + 평가용 Application

docs/gap-analysis.md 의 결정 사항(Q1~Q6, 하단 action 지시문)에 따라 진행한 1회차 구현.
범위는 의도적으로 좁게 잡았다: **DMA Cluster/가드 자동 배치(§3.1)는 구현하지 않고, 기존 코드처럼 S-MPU/MPCBB를
수동 하드코딩**하며, 중재자에는 **NS-MPU 검증 단계만** 추가한다. 되돌리려면 이 커밋만 revert하면 된다
(`git log` 에서 이 커밋 해시 확인 후 `git revert <hash>`).

## 1. `project/.../Secure/Src/secure_nsc.c` — `verify_inst()` 에 NS-MPU(TTAT) 검증 추가

### 무엇을 (최종, 1차 리뷰에서 교정됨)
기존 `verify_inst()`는 `TTT` 2회로 S-MPU(§3.2의 (2))만 확인했고, NS-MPU 쪽(§3.2의 (1))은 전혀 확인하지
않았다(docs/gap-analysis.md A.2). `base`, `base+len-1` 두 끝점에 대해 **`TTAT`**(비특권 alternate-domain)를
1회씩 추가했다. 처음에는 `TTA`(특권)로 구현했다가, 사용자 리뷰에서 다음 두 가지를 교정받아 다시 썼다:

1. **TTA 가 아니라 TTAT.** `DMA_Task_A`/`DMA_Task_B`는 `xTaskCreateRestricted`로 생성된 **비특권**
   태스크다. `TTA`는 "NS **특권** 접근이라면" 기준으로 NS-MPU를 조회하고, `TTAT`는 "NS **비특권**
   접근이라면" 기준으로 조회한다(ARM "Armv8-M Security Extension User Guide" 107655_100_01_en
   §4.4.4). 실제 호출자가 항상 비특권이므로 TTAT 가 맞다. CLAUDE.md의 "권한 구분 없이 하나만
   쓴다"는 "매번 분기하지 말고 비특권 변형으로 고정한다"는 뜻으로 적용했다.
2. **권한 비트만 보고 영역 번호 비교를 생략하면 안 됨.** 처음 구현은 `TTA(base)`와 `TTA(end)`의
   권한 비트를 AND로 묶어서만 확인했는데, 이러면 두 끝점이 각각 "따로" 쓰기 가능한 **서로 다른**
   NS-MPU 영역에 속해도 통과해 버린다(그 사이 구간이 실제로 연속/동일 영역이라는 보장이 없음).
   기존 S-MPU(`TTT`) 블록이 이미 `EORS`+`LSLS #24`+`BNE`로 영역 번호(결과 레지스터 bit[0:7])
   동일성을 먼저 확인하는 것과 똑같은 구조를 NS-MPU 쪽에도 그대로 적용했다: TTAT 영역 번호
   비교 → 같으면 그제서야 한쪽의 `readwrite_ok`(bit19) 확인.

필드 선택(`bit19` vs `bit21`)도 함께 교정했다: 이 레포 툴체인의
`arm_cmse.h`(`cmse_address_info_t`)를 직접 읽어 확인한 바, `mpu_region`(bit[0:7])/`readwrite_ok`
(bit19)는 "이 TT 변형이 실제로 조회한 MPU"(TT/TTT→S-MPU, TTA/TTAT→NS-MPU) 기준의 **공통** 필드이고,
`nonsecure_readwrite_ok`(bit21)/`secure`/`sau_*`/`idau_*`는 `__ARM_FEATURE_CMSE & 2` 로 게이트된
**SAU/IDAU 레벨의 별개 개념**이다(어떤 TT 변형을 썼든 존재하는 필드일 뿐, "TTA/TTAT 전용 NS 권한"
필드가 아니다). 그래서 새 NS-MPU 블록은 `TTAT` 결과의 **bit19**를 쓴다.

레지스터 r0-r3 만 사용해 push/pop 없이 끝낸다(naked 함수라 r4 이상을 쓰면 AAPCS상 직접
push/pop 해야 해서 명령 수가 늘어남 — "최대한 적은 instruction" 지침 때문에 피했다). 최종 asm은
`arm-none-eabi-objdump -d`로 디스어셈블해 `ttat`/`eors`/`ubfx #19`/`cbz`가 의도대로 나오는지, 분기
거리가 올바른지 직접 확인했다.

### 기존 TTT/bit21 줄에 대한 의심은 철회함 (사용자가 보드에서 정상 동작 확인)
1차 리뷰 때는 "이 줄이 항상 0을 반환해 상시 거부로 동작할 수 있다"고 적었는데, 사용자가 전에 이미
보드에서 이 경로(TTT + bit21)가 정상적으로 동작하는 것을 확인했다고 알려왔다. 그 근거로 들었던
ARM 문서 인용(§4.4.4, TTA/TTAT="Non-Secure MPU setting"을 본다는 서술)은 "TT/TTT 는 bit21을 전혀
채우지 않는다"는 뜻으로 단정할 근거가 아니었다 — 그 문서는 "TT/TTT가 bit21을 비운다"고 명시하지
않았고, 거기서부터는 내 추론이었다.

**[추정, pseudocode 재확인 안 됨]** 더 그럴듯한 설명: `TTA`/`TTAT`의 "Alternate domain"은 SAU 상
**Secure 로 보이는 주소**를 강제로 NS 취급해서 조회하기 위한 것이다. `dst_buf`는 애초에 SAU 상
**NS** 주소이므로 그 강제 전환이 필요 없는 경우다 — SAU 가 이미 NS 로 판정했다면, `A` 플래그 없이도
`TT`/`TTT` 가 그 판정을 그대로 반영해 bit21 에 NS-MPU 기준 권한(`TTT` 이므로 NS-비특권 권한)을
채울 수 있다. 이 설명이 맞다면, 기존 `TTT`/bit21 체크와 이번에 추가한 `TTAT`/bit19 체크는 **같은
권한을 두 개의 다른 공식 경로로 중복 확인**하는 셈이고, 이번 추가의 진짜 기여는 permission 비트가
아니라 **NS-MPU 쪽 영역 번호 동일성 검사**(`EORS`+`LSLS #24`+`BNE`, 기존 TTT 경로에는 없었음)다.
기존 줄은 **고치지 않았다** — 범위 밖이기도 하고, 이제는 틀렸다고 볼 근거도 약해졌다.

### 보드에서 추가로 확인하면 좋을 것
`g_statMediatorTaskA`/`g_statRoundtripTaskA`(및 TaskB 쌍)는 여전히 유용한 관찰 지점이다 — 이번에
추가한 `TTAT` 경로가 기존 `TTT` 경로와 실제로 같은 값을 주는지(= 위 가설이 맞는지) 보드에서 양쪽
결과 레지스터를 직접 비교해 보면 확인할 수 있다. `SECURE_dma_m2m_start()`→`verify_inst()`에
중단점을 걸고, `TTAT r1,r0` 직후의 `r1`과 `TTT r3,r0` 직후의 `r3`(둘 다 bit21 자리만 비교)를
비교하면 된다.

## 2. `project/.../NonSecure/Src/app_freertos.c` — 평가용 Application 전면 교체

### 무엇을
기존 LED 예제 태스크 1개를 아래 3개로 교체했다(전부 `xTaskCreateRestricted`, 기존 FreeRTOS-MPU 패턴
그대로 재사용):

- **`DMA_Task_A`, `DMA_Task_B`**: 각자 전용 `dma_cluster_t`(= `body[256]` + `guard[256]`, 하나의
  struct로 선언해 두 멤버가 반드시 연속이 되도록 강제 — 추가 링커 섹션 없이 "단순 코드 배치"로
  유도하라는 CLAUDE.md 지침을 그대로 따른 것) 전역 버퍼를 가진다. `body`만 해당 태스크의 NS-MPU
  사용자 영역에 포함되고, `guard`는 어떤 NS-MPU 영역에도 포함하지 않는다(실수로라도 NS 코드가
  그 256B를 건드리지 못하게). DMA 소스 버퍼(`s_dmaSrcA/B`)는 별도 32B 배열, 검증 대상 아님
  (Q4: UART DMA 모델에서는 소스를 신경 쓰지 않기로 했고, 이번 라운드는 그 모델의 중재자를 그대로
  재사용하는 M2M 태스크라 소스는 그대로 둠).
- **`Plain_Task`**: DMA를 전혀 쓰지 않는 기존 LED 예제 그대로(비교군). Secure 쪽에 어떤 S-MPU 태그도
  없다.

하나뿐인 Secure DMA 채널(기존 코드가 `DMA1_Channel1` 하나만 M2M으로 초기화해 둠)과 콜백 슬롯
(`s_ns_callback`, 함수 포인터 1개)을 두 태스크가 공유하므로, `xSemaphoreCreateMutex()`로 직렬화했다.
이건 DMA 보호 설계(§3.2/§3.3)와는 무관한, 이 PoC 응용 코드 수준의 동시성 처리다.

각 DMA 태스크는 시작 시 32회 반복 측정(캐시 워밍 1회 + 32회, 논문 §4 방법론과 동일)을 한 번 수행해
`g_statMediatorTask{A,B}`(중재자 호출 1회 비용)와 `g_statRoundtripTask{A,B}`(전송 + Secure ISR까지
왕복 비용)에 min/max/avg를 기록하고, 이후에는 500ms/700ms 주기로 계속 전송을 반복한다.

### 왜
- "FreeRTOS-MPU API로, DMA 버퍼는 전역으로" — 사용자 지시 3번 그대로.
- 두 DMA 태스크를 분리된 NS-MPU 영역으로 만든 이유: "MPU 격리 및 DMA protection이 정상적으로
  작동"을 확인하려면 적어도 2개의 서로 다른 영역이 있어야 "격리"를 테스트할 대상이 생긴다
  (한쪽이 다른 쪽 버퍼를 가리키면 거부되어야 함 — §3. 공격 시나리오 (a), 아래 참고).
- 사이클 측정을 "각 DMA 사용 태스크에" 넣은 것은 지시 문단 그대로이며, Q6(테이블 방식 벤치마크는
  사용자가 직접 작성)과는 범위가 다르다 — 이건 S-MPU 태그 경로(현재 유일하게 활성화된 경로)의
  오버헤드만 잰다.

### 커버하지 않은 것 (의도적)
- Cluster 자동 배치, `.guard_table` 섹션, `DMA_ALLOCATE_BUFFER` 매크로: 손대지 않았다(지침).
- `src_buf` 검증: 하지 않는다(Q4).
- Flash 테이블 방식(`g_dma_table`/`verify_table`): 손대지 않았다(Q6).

## 3. `project/.../Secure/Src/main.c` — `SMPU_SetupProbeRegion()` → `SMPU_SetupTaskRegions()`

### 무엇을
기존의 "사이클 측정용 하드코딩 probe 영역 1개"를 "실제 두 DMA 태스크 Cluster를 위한 S-MPU 영역 2개
+ MPCBB 가드 2개"로 교체했다. Cluster 자동 배치가 없으므로, 주소는 **NonSecure를 먼저 빌드한 뒤
`.elf`에서 직접 읽어 하드코딩**했다(아래 "주소를 직접 확인하는 방법" 참고). S-MPU 영역 번호는
기존에 쓰던 4번을 Task A에, 새로 5번을 Task B에 배정했다(S-MPU 8개 한도 중 2개 사용, 논문 §5의
한계 그대로).

가드 블록은 `HAL_GTZC_MPCBB_ConfigMemAttributes()`(기존 `MX_GTZC_S_Init()`이 쓰던
`HAL_GTZC_MPCBB_ConfigMem()`과는 다른, 블록 단위 전용 HAL 함수)로 256B(1블록)씩만 Secure로
표시했다. 비트 극성은 `Drivers/STM32L5xx_HAL_Driver/Inc/stm32l5xx_hal_gtzc.h`의 `MPCBB_SecConfig_array`
주석("0 means non-secure, 1 means secure")과 `GTZC_MCPBB_BLOCK_SEC`/`_NSEC` 매크로 정의로 직접
확인한 뒤 적용했다(추측하지 않음 — CLAUDE.md 규칙 3).

### 왜 body/guard를 하나의 struct로 묶었는가
가드 블록을 "body 바로 위 256B"로 보장하려면 NS 쪽에서 그 두 영역이 반드시 인접해야 하는데,
두 전역 변수를 따로 선언하면 컴파일러/링커가 사이에 다른 전역을 끼워 넣을 수 있다. 하나의
struct(`dma_cluster_t`)로 선언하면 멤버 순서가 보장되므로, 별도 링커 섹션 없이도 "가드가 바로
위에 있다"는 전제가 깨지지 않는다 — CLAUDE.md의 "추가적인 메모리 섹션없이 단순 코드 배치" 지침과
기존에 주석 처리돼 있던 `DMA_ALLOCATE_BUFFER` 매크로(`pad_upper+body+pad_lower`)가 쓰려던 것과
같은 발상이다.

### 주소를 직접 확인하는 방법 (사용자 요청 2번)
NonSecure를 빌드한 뒤, 둘 중 하나로 확인할 수 있다.

```bash
cd Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/STM32CubeIDE/NonSecure/Debug

# 방법 1: nm (가장 간단, 주소+크기 바로 보임)
arm-none-eabi-nm --size-sort -S FreeRTOS_SecureIOToggle_TrustZone_NonSecure.elf | grep s_dmaCluster
# 2001b900 00000200 b s_dmaClusterA
# 2001b700 00000200 b s_dmaClusterB

# 방법 2: .map 파일에서 같은 정보 (섹션 배치 전체 맥락을 보고 싶을 때)
grep -A1 "\.bss\.s_dmaCluster" FreeRTOS_SecureIOToggle_TrustZone_NonSecure.map
```
`b`는 `.bss`(미초기화 전역)에 있다는 뜻이고, 그 뒤 숫자(`00000200` = 0x200 = 512)가 크기다.
`body`는 그 주소부터 256B, `guard`는 그 주소+256부터 256B(struct 멤버 선언 순서 그대로)다.

**주의**: NS 쪽 `app_freertos.c`를 다시 고쳐서 두 버퍼의 선언 순서/크기/다른 전역 변수 배치가
바뀌면 이 주소도 바뀔 수 있다. 그럴 때마다 위 명령으로 다시 읽어서 `Secure/Src/main.c`의
`DMA_TASK_A_BODY_BASE`/`DMA_TASK_B_BODY_BASE`를 수동으로 갱신해야 한다(자동 동기화 없음 — 이번
라운드에서 Cluster 자동 배치를 구현하지 않기로 했기 때문에 생기는 당연한 제약이다).

## 4. 공격 시나리오 코드 (주석 처리, `app_freertos.c` 내 `#if 0` 블록)

- **(a) 인자 조작**: `AttackScenario_ArgTamper_TaskA()` — Task A가 자신의 `s_dmaSrcA`를 Task B의
  `s_dmaClusterB.body`로 보내며 중재자를 호출한다. Task A는 그 주소에 대한 NS-MPU 쓰기 권한이 없으므로
  새로 추가한 TTA 체크에서 거부되어야 한다.
- **(b) 검증 통과 후 길이 변조**: `AttackScenario_OverflowAfterVerify_TaskA()` — `len`을 `body`
  크기(256)보다 32바이트 크게 줘서 `base+len-1`이 `guard` 안으로 들어가게 한다. 1차 방어선(TTT
  영역번호 비교)이 걸러야 정상이고, 혹시 뚫리더라도 2차 방어선(가드 블록 MPCBB + TZIC, §3.3)이
  실제 전송이 `guard`에 처음 닿는 순간 채널을 중단시켜야 한다.

둘 다 "일단 주석으로"라는 지시대로 비활성 상태로만 작성했다. 활성화하려면 해당 태스크 본문에서
벤치마크 호출 다음 줄에 함수 호출을 추가하면 된다.

## 빌드 확인

```
./build.sh clean && ./build.sh
```
Secure: text=10928 data=40 bss=11976 (기존 대비 +640B text, TTA 체크 + SMPU_SetupTaskRegions 추가분)
NonSecure: text=35416 data=10288 bss=12112 (기존 대비 +832B text, +48B data, +3296B bss — 새 태스크
3개의 스택(256*4*3=3072B)과 두 Cluster(512*2=1024B) 등)
경고: 기존에 있던 것(`const` discard, RWX LOAD segment) 외 신규 경고/에러 없음.

## 아직 안 한 것 / 다음 라운드 후보
- 위 1번 항목의 "TTT/bit21 과 TTAT/bit19 가 같은 값을 주는가" 가설을 보드에서 직접 비교 확인.
- 공격 시나리오 코드 활성화(사용자가 원하는 시점에).
