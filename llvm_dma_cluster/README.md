# DMA Cluster 자동 배치 + S-MPU / MPCBB Guard 자동 설정 (LLVM pass 적용 가이드)

대상: STM32L562 (Cortex-M33, TrustZone), FreeRTOS ARMv8-M(MPU+TZ) NS 이미지, LLVM 18.
근거 문서: `LLVM 입문 DMA 버퍼 자동 배치까지.md`, `LLVM Introduction for 내연구.pdf`(1–6단계), `LLVM 실습 튜토리얼 for 내연구.pdf`(Lab 1–6, 요약: `manual_text/LLVM_tutorial_digest.md`).

> 이 폴더의 코드는 이 PC(Windows, Python·LLVM 미설치)에서 **컴파일·실행 검증을 하지 않았다.** Debian 빌드 머신에서 §6의 순서대로 확인하면서 적용할 것.
>
> **LLVM 을 잘 모르면 먼저 [EXPLAINED.md](EXPLAINED.md) 를 읽을 것.**

## 실행 (2026-11-27 실검증 경로 — 자세한 기록은 [VERIFY_LOG.md](VERIFY_LOG.md))

```bash
cd ~/dma-tz && ./build.sh                       # GCC baseline (⓪ 의 import library 를 만든다)
cd llvm_dma_cluster
bash scripts/build_all.sh                       # 플러그인(LLVM 18) → NS 2-pass → Secure ②③④⑤ → check_pair
bash tests/run_tests.sh                         # 단위·음성·양성 시험 (build/t_*)
```
- LLVM 18 이 시스템에 없으면 `scripts/env_llvm18.sh` 가 `~/opt/root18`(apt download + dpkg -x) 을 쓴다.
- `~/dma-tz` 의 프로젝트 파일은 수정하지 않는다. 바뀐 파일은 `src/<같은 상대 경로>` 사본이고 빌드 스크립트가 사본을 우선한다.
- 산출물: `build/ns/ns.elf`, `build/secure/secure.elf` (+ `.map`, `secure_nsclib.o`). 보드 절차: [BOARD_TEST.md](BOARD_TEST.md).
- 아래 §3–§6 의 경로·변수 예시(`project/NonSecure/Core/Src`, `OUT=$ROOT/build/ns` 등)는 초안 기준이다. 실제 값은 스크립트를 따른다.

## Secure 데이터를 guard 블록에 배치 (storage binding)

| 파일 | 역할 |
|---|---|
| `pass/SecureGuardSelect.cpp` | Secure IR 을 분석한다. 0 초기화, ≤256 B, 부팅 경로 미사용, GCC 섹션 이름 대응 조건을 확인해 guard 로 옮겨도 되는 전역을 고른다 |
| `config/secure_guard_config.json` | 모드(`annotated`/`auto`), boot 함수, 부팅 후에만 켜지는 핸들러, 어셈블리 전용 전역 제외 목록 |
| `gen/pack_guards.py` | 흩어진 guard 블록(256 B 칸)에 First-Fit Decreasing 으로 나눠 담는다. GCC 용 `secure_guard_mem.ld`, `secure_guard_sec.ld`, `guard_bind.h` 를 생성한다 |
| `secure/secure_guard_bind.c` | 부팅 시 기대 guard 주소와 manifest 를 비교한다(불일치면 정지). 일치하면 guard 의 Secure 데이터를 0 으로 초기화한다 |
| `scripts/build_secure_guard.sh` | ② 분석(clang → llvm-link → opt), ③ 배치. ④ GCC 최종 빌드는 생성된 스크립트를 INCLUDE 하고 `-Wl,--in-implib` 로 veneer 를 고정한다 |

빌드 순서는 ⓪ Secure 0차 → ① NS 2-pass → ② 분석 → ③ 배치 → ④ Secure 최종 → ⑤ veneer 비교 순이다. 이유는 EXPLAINED.md §6 에 있다.

---

## 0. 무엇을 자동화하나

| 자동화 대상 | 기존 (하드코딩) | 자동화 후 |
|---|---|---|
| DMA 버퍼 식별 | 사람이 `dma_task_mem_t` 에 넣음 | **pass**가 DMA sink 호출 인자를 역추적 |
| 태스크별 cluster 배치 | `DMA_TASK_A_CLUSTER_BASE 0x2001b700` 등 | **링커**가 생성된 스크립트로 배치 (주소는 링크 결과) |
| 공유 DMA 버퍼 | 수동 | owner 집합별 **shared cluster** 자동 생성 |
| guard 블록 위치 | `DMA_TASK_A_GUARD_BASE` | cluster 끝(256 B 경계) 바로 뒤 256 B — 링커가 예약 |
| S-MPU 태그 region | `SMPU_SetupOneTaskRegion(4, …)` 하드코딩 | **Secure boot**가 manifest 를 읽어 cluster 마다 자동 설정 |
| MPCBB guard 설정 | `HAL_GTZC_MPCBB_ConfigMemAttributes(…GUARD_BASE…)` | Secure boot 가 manifest 로 자동 설정 |
| 태스크 NS-MPU region | `xRegions` 수동 | 생성된 `dma_regions.h` 매크로 (TTAT 권한 판정과 일치) |

```
 NS 소스 (.c) ──clang -flto──▶ bitcode .o ──┐
                                            ▼
                        ld.lld (Full LTO) + DmaCluster.so  ──▶ dma_layout.json
                                            │                       │
                                            │              gen_layout.py
                                            │      ┌────────┼───────────────┐
                                            │ dma_clusters.ld  dma_manifest.c  dma_regions.h
                                            ▼      ▼               ▼              ▼
                                  2차 링크 ◀───────┴───────────────┴──── app 재컴파일
                                            │
                                         ns.elf  (.dma_manifest @ 0x08040400)
                                            │  (부팅)
 Secure boot: dma_boot_config() ──읽기──▶ manifest → S-MPU 태그 + MPCBB guard (→ 잠금)
```

## 1. 현재 설계 결정과 학습 자료 대비 변경점

| 항목 | 학습 자료 (Introduction / Tutorial) | 이 구현 | 이유 |
|---|---|---|---|
| guard 블록 수 N | 4단계: Secure 스택 WCSD 로 N 계산 | **항상 1** (`--guard-blocks 1`) | Mediator 가 SVC 경로(Handler mode, asm, Secure 스택 미사용) → guard 를 Secure 스택으로 쓰지 않음. **4단계(stack-size, wcsd.py) 불필요** |
| 다중 owner 버퍼 | 오류 (또는 shared 정책) | **shared cluster** (`.dma.s.<k>`) 로 자동 분리 | 공유 DMA 버퍼가 실제로 존재. TTT 는 "DMA 영역 소속", TTAT 는 "그 태스크 권한" → 공유 cluster 를 owner 태스크들의 NS-MPU 에만 넣으면 됨 |
| section 이름 | `.dma.<task>` | `.dma.t.<task>` / `.dma.s.<k>` | 태스크 전용·공유를 패턴으로 구분 |
| cluster 크기 | 1차 ELF 에서 측정 | pass 가 **상한(크기+정렬 패딩)** 계산 → 256 B 올림 고정 용량 | 용량이 상수가 되어 NS-MPU region 길이를 컴파일 시점 상수로 쓸 수 있음 |
| manifest 사용 | 부팅 시 S-MPU/MPCBB/스택 표 | 부팅 시 **S-MPU/MPCBB 만**, 런타임에는 사용하지 않음 | 런타임 정책은 잠긴 하드웨어 설정(S-MPU 태그, MPCBB)만으로 유지 |
| guard 0 초기화 | 언급 없음 | **cluster 만 0 초기화, guard 제외** (`dma_zero.c`) | guard 는 이미 Secure → NS 가 쓰면 TZIC illegal access |
| 설정 파일 | `config/dma_config.json` | 같은 이름, `args` 배열 | Tutorial 과 동일 |

**S-MPU region 예산:** STM32L5 Secure MPU 는 8 region. DMA 태그 = cluster 수(태스크 전용 + 공유) ≤ `8 − TAG_RNR0`. 기본값 `TAG_RNR0=4` → 최대 4 개. DMA 태스크 8개를 모두 쓰려면 `TAG_RNR0=0` 으로 내리고 Secure 쪽이 다른 S-MPU region 을 쓰지 않아야 한다 (`PRIVDEFENA` 로 기본 맵 사용). `gen_layout.py --max-tags` 와 `secure_dma_boot.c` 가 모두 검사한다.

## 2. 파일 구성

```
llvm_dma_cluster/
├─ pass/DmaCluster.cpp, CMakeLists.txt      LLVM 18 New PM 플러그인 (opt / ld.lld)
├─ config/dma_config.json                   sink 함수·버퍼 인자 위치, 출력 경로
├─ gen/gen_layout.py                        JSON → dma_clusters.ld, dma_manifest.c, dma_regions.h
├─ ns/dma_clusters_pass1.ld                 1차 링크용 임시 조각
├─ ns/dma_manifest_stub.c                   1차 링크용 빈 manifest
├─ ns/dma_zero.c                            cluster 0 초기화 (guard 제외)
├─ common/dma_manifest.h                    NS·Secure 공용 manifest 형식
├─ secure/secure_dma_boot.c                 Secure boot: manifest → S-MPU 태그 + MPCBB guard
└─ scripts/build_ns.sh, check_layout.py     2-pass 빌드, 정적 검증
```

## 3. 툴체인 전환 전략: **NS 만 clang, Secure 는 GCC 유지**

| 이미지 | 컴파일러 | 이유 |
|---|---|---|
| NS (FreeRTOS + app + HAL) | **clang + ld.lld + Full LTO + pass** | DMA 버퍼 정의·sink 호출·태스크 생성이 서로 다른 파일 → 링크 시점 whole-program 분석 필요 |
| Secure (mediator, GTZC, boot) | **GCC (CubeIDE 그대로)** | pass 가 필요 없음. `secure_dma_boot.c` 만 추가. CMSE import library(`--cmse-implib --out-implib`)도 기존 GCC 흐름 유지 |

NS 링크는 GCC 가 만든 `secure_nsclib.o`(veneer 주소의 absolute 심볼만 가진 ELF 오브젝트)를 그대로 입력으로 받는다. lld 가 이를 일반 오브젝트로 링크한다 **[미확인: 실제 링크로 확인]**.

### 3.1 설치 (Debian, 빌드 머신)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3 python3-pip wget lsb-release gnupg
wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 18
sudo apt install -y llvm-18-dev clang-18 lld-18            # 플러그인 헤더 + lld
pip3 install --user pyelftools
export PATH=/usr/lib/llvm-18/bin:$PATH                     # ~/.bashrc 에 추가
# newlib/libgcc 는 기존 arm-none-eabi-gcc (CubeIDE 번들 또는 apt gcc-arm-none-eabi) 것을 빌려 쓴다
arm-none-eabi-gcc --version
```

확인:

```bash
clang --version | head -1        # clang version 18.x
ld.lld --version                 # LLD 18.x
ls /usr/lib/llvm-18/lib/cmake/llvm/LLVMConfig.cmake
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -print-multi-directory
# → thumb/v8-m.main+fp/hard (이 디렉터리의 libc_nano.a 를 링크)
```

> **버전 일치 필수:** 플러그인은 그것을 로드하는 `ld.lld`/`opt` 와 같은 LLVM 버전·빌드로 빌드해야 한다. Arm Toolchain for Embedded(ATfE) 의 clang 을 쓰려면 개발 헤더가 없으므로 같은 버전 LLVM 을 소스에서 빌드해야 한다 → apt LLVM 18 + GCC newlib 조합이 가장 간단하다.

### 3.2 GCC 프로젝트 → clang 으로 옮길 때의 차이 (NS)

| 항목 | GCC (CubeIDE) | clang 에서 |
|---|---|---|
| 타깃 | `-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16` | 같은 플래그 + `--target=arm-none-eabi` |
| libc 헤더 | 자동 | `--sysroot=$GCC_ROOT/arm-none-eabi` |
| `--specs=nano.specs` | 사용 | **제거.** 대신 링크 시 `libc_nano.a libnosys.a libm.a libgcc.a` 를 직접 지정 (`-nostdlib -nostartfiles`) |
| `_init`/`_fini` (`__libc_init_array`) | crti.o 자동 | `-print-file-name=crti.o / crtn.o` 를 링크에 직접 넣음 (또는 빈 `_init`/`_fini` 정의) |
| GCC 전용 옵션 | `-fstack-usage`, `-fcyclomatic-complexity` | 제거 (clang 은 `-fstack-usage` 미지원, 필요 시 `-fstack-size-section`) |
| startup `.s` | GAS | clang 통합 어셈블러로 `-x assembler-with-cpp`. `.fpu softvfp` 가 있으면 `.fpu fpv5-sp-d16` 로 바꾸거나 삭제 |
| 링커 스크립트 | GNU ld | lld 가 대부분 지원. `(READONLY)` 키워드(최신 CubeIDE 생성)가 있으면 lld 가 거부할 수 있음 → 제거 |
| GlobalMerge | — | Arm 백엔드가 작은 전역을 합쳐 section 이 섞임 → **링크 시** `-Wl,-mllvm,-arm-global-merge=false` |
| LTO | — | **Full LTO 만** (`-flto`). ThinLTO(`-flto=thin`)는 모듈이 나뉘어 이 분석에 부적합 |
| `-fno-common` | GCC 10+ 기본 | clang 11+ 기본. 명시 권장 (common 심볼은 section 을 가질 수 없음) |
| FreeRTOS 포트 | `portable/GCC/ARM_CM33/non_secure` | 같은 포트를 clang 으로 컴파일 (inline asm 은 통합 어셈블러가 처리) |

## 4. NS 프로젝트 수정

### 4.1 링커 스크립트 (`STM32L562QEIXQ_FLASH.ld`)

```ld
SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

  /* (추가) Secure boot 가 읽는 manifest: 고정 주소 (dma_manifest.h 의 DMA_MANIFEST_NS_ADDR) */
  .dma_manifest 0x08040400 : { KEEP(*(.dma_manifest)) } > FLASH
  ASSERT(ADDR(.dma_manifest) == 0x08040400, "manifest address moved")

  .text : { ... } > FLASH
  ...

  /* (추가) DMA cluster + guard + zero table — .data/.bss 규칙보다 "앞"에 (first-match) */
  INCLUDE dma_clusters.ld

  .data : { ... } > RAM AT> FLASH
  .bss  : { ... } > RAM
  ...
}
```

- `INCLUDE` 파일은 `-L` 경로에서 찾는다 → 빌드 스크립트가 `-L build/ns/gen` 을 넘긴다.
- `RAM` 은 SAU 상 NS 구간(0x20018000~) 이어야 한다 (`partition_stm32l562xx.h`).
- 기존 `s_dmaTaskMem` 고정 배치(`0x2001b500`, `check_layout.sh`)와 `dma_task_mem_t` 의 cluster/guard 필드는 **삭제**한다. 버퍼는 일반 `static` 전역으로 선언하면 pass 가 찾는다.

### 4.2 startup (`startup_stm32l562qeixq.s`)

`.bss` 0 초기화 루프 다음, `bl __libc_init_array` 앞에 한 줄:

```asm
    bl  dma_zero_clusters       /* cluster 만 0 초기화, guard 는 건드리지 않음 */
```

### 4.3 앱 코드 (`app_freertos.c`)

```c
#include "FreeRTOS.h"
#include "task.h"
#if defined(DMA_HAVE_REGIONS)          /* 2차 빌드: 생성된 헤더 */
#  include "dma_regions.h"
#else                                  /* 1차 빌드: 자리만 차지 (분석에 영향 없음) */
#  define DMA_TASK_REGIONS_vDmaTaskA  { 0, 0, 0 }
#  define DMA_TASK_REGIONS_vDmaTaskB  { 0, 0, 0 }
#endif

static uint8_t s_dmaDstA[64];          /* 그냥 전역으로 선언 → pass 가 .dma.t.vDmaTaskA 로 */
static uint8_t s_dmaDstB[64];
static uint8_t s_dmaShared[128];       /* A, B 둘 다 DMA 에 쓰면 → .dma.s.0 (shared) */

static const TaskParameters_t xDmaTaskAParams = {
    .pvTaskCode = vDmaTaskA, .pcName = "DMA_A", /* ... 기존 필드 ... */
    .xRegions = { DMA_TASK_REGIONS_vDmaTaskA, /* 기존 태스크 데이터 region 등 */ },
};
```

- 자동 판정을 덮어쓰려면: `static uint8_t buf[32] __attribute__((annotate("dma:vDmaTaskA")));` 또는 `annotate("dma:shared:uart")`.
- 지역 배열, 힙 버퍼, 0 이 아닌 초기값을 가진 버퍼는 pass 가 **error** 로 보고한다(`DMA_CLUSTER_STRICT=1` 이면 링크 실패).

### 4.4 sink 설정 (`config/dma_config.json`)

```json
{ "out": "build/ns/dma_layout.json",
  "sinks": [ { "func": "SECURE_dma_m2m_start", "args": [1] },
             { "func": "App_DmaM2M",           "args": [2] } ] }
```

- `args` 는 DMA 가 **쓰는** 버퍼 인자 위치(0부터). `SECURE_dma_m2m_start(src, dst, len)` 이면 `[1]`.
- NS 래퍼(`App_DmaM2M(g, src, dst, len)`)가 있으면 래퍼도 sink 로 넣거나, 래퍼 안의 `SECURE_dma_m2m_start` 인자가 `Argument` 이므로 pass 가 호출자(태스크)로 자동 역추적한다.

## 5. Secure 프로젝트 수정 (GCC 그대로)

1. `common/dma_manifest.h` 를 Secure include 경로에, `secure/secure_dma_boot.c` 를 소스에 추가 (CubeIDE `subdir.mk`/`objects.list` 규칙에 맞게).
2. `main.c` 에서 `SMPU_SetupTaskRegions();` 와 guard 하드코딩 상수를 다음으로 교체:

```c
if (dma_boot_config() != 0) {
    Error_Handler();              /* 또는 mediator 비활성 상태로 NS 진입 */
}
```

3. `HAL_GTZC_MPCBB_ConfigMemAttributes` 의 size 인자 규약은 **기존 동작하던 코드와 동일하게** (`GUARD_HAL_SIZE_ARG`). HAL 버전 문서로 단위를 확인.
4. (선택) 설정 후 잠금: `SYSCFG->CSLCKR |= SYSCFG_CSLCKR_LOCKSMPU;`, MPCBB `LCK`/`LCKVTR`. LOCKSMPU 가 `MPU_RLAR`·alias 까지 고정하는지는 RM0438 에 명시가 없어 **실측 확인**.
5. Mediator 는 프로젝트의 **SVC 경로를 그대로** 쓴다 (2026-11-28 전환, VERIFY_LOG #28–#31).
   - NS 태스크: `svc #110` (`App_DmaStartSvc`, r0=src r1=dst r2=len) → NS `SVC_Handler` fast path (portasm.c) →
     Handler mode 에서 `SECURE_dma_m2m_start_svc` (secure_nsc.c, asm, 원자성 = SVC 예외 안).
   - Secure 실행 중 예외 프레임은 MSP_S 에 쌓이므로 DMA 태스크에 Secure context/스택이 필요 없다.
     landing pad, `secure_dma_entry.S`, stock Secure context 패치는 **삭제**했다 (secure_context*.c 원본 사용).
   - TTT 가 보는 S-MPU 태그는 manifest 로 자동 설정된다. DmaCluster pass 는 inline asm `svc #110` 도 sink 로 본다
     (`config/dma_config.json` 의 `{"svc": 110, "args": [1]}`, args = 레지스터 번호).

## 6. 빌드와 적용 순서

### 6.1 플러그인 빌드 + 단독 확인

```bash
cd llvm_dma_cluster
cmake -S pass -B build/pass -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
      -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build/pass                 # → build/pass/DmaCluster.so

# opt 로 파일 하나 확인 (입문 문서 실습 5–6 과 같은 방식)
clang --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -O1 -g -S -emit-llvm app_freertos.c -o app.ll
DMA_CLUSTER_CONFIG=config/dma_config.json \
  opt -load-pass-plugin=build/pass/DmaCluster.so -passes=dma-cluster app.ll -S -o app_placed.ll
grep 'section ".dma' app_placed.ll
```

### 6.2 NS 전체 2-pass 빌드

```bash
ROOT=~/dma-tz NSDIR=~/dma-tz/project/NonSecure ./scripts/build_ns.sh
```

`build_ns.sh` 가 하는 일 (변수 `ROOT`, `NSDIR`, `LDS`, `IMPLIB`, `INCS`, `SRCS` 를 실제 레포에 맞춘다):

| 단계 | 명령 | 산출물 |
|---|---|---|
| 1 | 모든 `.c` 를 `clang -flto` 로 컴파일 (bitcode), startup 은 어셈블 | `obj/*.o` |
| 2 | 1차 링크: `-fuse-ld=lld -flto -Wl,--load-pass-plugin=DmaCluster.so -Wl,-mllvm,-arm-global-merge=false`, 임시 `dma_clusters.ld`, stub manifest | `ns_pass1.elf`, `dma_layout.json` |
| 3 | `gen_layout.py` | `gen/dma_clusters.ld`, `gen/dma_manifest.c`, `gen/dma_regions.h`, `gen/layout_final.json` |
| 4 | `dma_regions.h` 를 쓰는 파일만 `-DDMA_HAVE_REGIONS` 로 재컴파일, manifest 컴파일 | |
| 5 | 2차 링크 (같은 플러그인, 생성 스크립트) | `ns.elf`, `ns.elf.map`, `ns.hex` |
| 6 | 1차/2차 JSON 동일성 검사 + `check_layout.py` | OK / FAIL |

> 2차 링크에서도 pass 가 다시 실행되어 같은 section 을 부여한다. 4단계 재컴파일은 `xRegions` 상수만 바꾸므로 DMA 분석 결과가 같아야 하며, 6단계가 이를 확인한다. 2차 링크가 `dma_layout.json` 을 덮어쓰므로 생성에 쓴 입력은 `layout_final.json` 으로 보존한다(Tutorial Lab 4 주의사항과 동일).

### 6.3 정적 확인

```bash
llvm-readelf -S build/ns/ns.elf | grep -E 'dma_c|dma_manifest|dma_zero'
llvm-nm -n build/ns/ns.elf | grep -E '__dmac|s_dma'
python3 scripts/check_layout.py build/ns/ns.elf build/ns/gen/layout_final.json
llvm-objcopy -O binary --only-section=.dma_manifest build/ns/ns.elf manifest.bin && xxd manifest.bin
```

기대: `__dmac<i>_start` 32 B 정렬, `__dmac<i>_end` 256 B 정렬, 각 버퍼 심볼이 자기 cluster 안, cluster/guard 구간이 서로 겹치지 않음, manifest 값 = 링커 심볼.

### 6.4 보드 확인 (GDB)

```gdb
x/24wx 0x08040400                              # manifest: 0x444d414d 1 count ...
b dma_boot_config
finish                                         # 반환값 0
# S-MPU 태그 덤프 (Secure 상태에서)
set $i = 4
while $i < 8
  set *(unsigned*)0xE000ED98 = $i              # MPU_RNR
  printf "rnr %d: RBAR=%08x RLAR=%08x\n", $i, *(unsigned*)0xE000ED9C, *(unsigned*)0xE000EDA0
  set $i = $i + 1
end
p/x ((GTZC_MPCBB_TypeDef*)GTZC_MPCBB1_S)->VCTR[ (guard-0x20000000)/256/32 ]   # guard 비트 = 1
```

- 기대 동작(기존과 동일): DMA_Task_A/B 전송이 계속 완료(10 s 당 29–30 회), Plain task 정상.
- 공격 시나리오: (a-1)(a-2) 의 요청 거부, (b) 전송 중 경계 초과 시 guard 에서 TZIC 이벤트 → 기존 결과와 같아야 한다.
- `x/64bx __dmac0_end` 를 NS 디버그로 읽으면 0 (MPCBB 차단) — guard 가 Secure 인지 확인.

## 7. 문제 해결

| 증상 | 원인 / 대처 |
|---|---|
| `Could not load library ... DmaCluster.so` / undefined symbol | 플러그인과 lld 의 LLVM 버전 불일치. `ld.lld --version` 과 `LLVM_DIR` 일치시키기 |
| pass 가 아무것도 출력하지 않음 | `DMA_CLUSTER_CONFIG` 미설정, 또는 ThinLTO 로 링크됨 (`-flto` = Full) |
| `.dma.*` 가 `.bss` 에 섞임 | GlobalMerge → `-Wl,-mllvm,-arm-global-merge=false` |
| `orphan .dma.* input section` ASSERT | 생성기 입력과 2차 링크 분석 결과 불일치 → 6단계 diff 확인 |
| `dma cluster cN overflow` | pass 의 크기 상한 계산보다 실제가 큼(정렬 특이 케이스) → 버그 보고, 임시로 `bytes_bound` 수동 증가 |
| 부팅 직후 TZIC 인터럽트 | NS 가 guard 를 씀 → `.bss` 루프가 cluster 영역까지 지우는지, `dma_zero_clusters` 테이블에 guard 가 들어갔는지 확인 |
| lld: `READONLY` 구문 오류 | CubeIDE 신형 스크립트의 `(READONLY)` 제거 |
| `undefined symbol: _init` | `crti.o`/`crtn.o` 링크 또는 빈 `_init`/`_fini` 정의 |
| clang: `.fpu softvfp` 오류 | startup 의 `.fpu` 를 `fpv5-sp-d16` 로 |
| stack-buffer / heap-buffer error | 지역 배열 → `static` 승격, 힙 → cluster 안 정적 pool |
| unresolved-load error | 포인터 변수(지역/구조체)로 전달 → `annotate("dma:<task>")` 또는 SVF 단계(Tutorial Lab 6) |

## 8. 한계와 다음 단계

- **간접 호출**: 같은 함수 타입의 address-taken 함수 전부로 연결(conservative) → owner 가 넓어져 불필요한 shared cluster 가 생길 수 있음. SVF(Tutorial Lab 6)로 좁힌다.
- **포인터 변수 경유**: 전역 포인터 변수는 모든 store 값을 후보로 사용(flow-insensitive). 지역 포인터·구조체 필드 포인터는 error → annotate/SVF.
- **S-MPU region 수**: cluster 수 ≤ 8 − `TAG_RNR0`. 공유 cluster 도 1 개씩 소모.
- **NS-MPU region 수**: 태스크당 `portNUM_CONFIGURABLE_REGIONS`(보통 4) 안에 전용+공유 cluster 와 기존 region 이 모두 들어가야 함 (`--max-task-regions`).
- **0 이 아닌 초기값 버퍼** 미지원 (NOLOAD). 필요하면 런타임에 채운다.
- **cluster 용량 낭비**: 256 B 올림으로 cluster 당 최대 255 B. 1차 ELF 실측 크기로 줄이는 최적화 가능.
- **manifest 무결성**: NS 이미지는 논문 위협 모델상 TCB(부트 포함)이므로 형식·범위 검증만 수행. 서명 부트(MCUboot/TF-M BL2/SBSFU)를 쓰면 manifest 가 서명 범위에 포함된다.
