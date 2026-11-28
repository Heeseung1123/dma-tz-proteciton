# DMA 메모리 자동 배치 설명 (LLVM 초보자용)

이 문서는 `llvm_dma_cluster/` 가 **DMA 버퍼를 어떻게 자동으로 찾아서, 어느 주소에 놓을지 정하는지**를 설명합니다.
모든 예시는 지금 빌드(`scripts/build_all.sh`, 커밋 `d7e0fa3` 이후)의 실제 산출물에서 그대로 옮겼습니다.

- 분석 결과 JSON: `build/ns/dma_layout.json`
- 생성된 링커 스크립트: `build/ns/gen/dma_clusters.ld`
- 그 밖의 생성물: `build/ns/gen/dma_regions.h`, `build/ns/gen/dma_manifest.c`

---

## 0. 먼저 알아야 할 용어 5개

| 용어 | 한 줄 설명 | 이 프로젝트에서 |
|---|---|---|
| **LLVM IR** | C 코드를 컴파일러가 분석하기 쉬운 중간 언어로 바꾼 것. 함수, 전역 변수, 호출이 모두 보인다 | pass 가 읽는 대상 |
| **LTO** (Link Time Optimization) | 파일마다 따로 기계어를 만들지 않고, 링크할 때 **모든 파일의 IR 을 하나로 합쳐** 처리한다 | 덕분에 pass 가 태스크 함수(`app_freertos.c`)와 커널 API 호출을 한 번에 본다 |
| **pass** | IR 을 읽거나 고치는 플러그인 | `DmaCluster.so`. 링커(lld)가 LTO 를 시작할 때 자동으로 실행된다 |
| **section** | 오브젝트 파일 안에서 코드와 데이터를 묶는 이름표 (`.text`, `.bss` …) | pass 가 DMA 버퍼에 `.dma.h.vDmaTaskA` 같은 이름표를 새로 붙인다 |
| **링커 스크립트 (.ld)** | "어떤 section 을 어느 주소에 놓아라"는 규칙 | `gen_layout.py` 가 그 규칙 조각(`dma_clusters.ld`)을 만든다 |

핵심은 이것 하나입니다. **pass 는 "무엇을 어디에 묶을지(이름표)"를 정하고, 링커 스크립트는 "그 묶음을 어느 주소에 둘지"를 정합니다.**

---

## 1. 전체 흐름

```
                          (1차 링크: 분석과 크기 측정)
 app_freertos.c ─┐
 tasks.c ...     ├─ clang -flto ─► IR 파일들 ─► lld ─┬─ LTO 시작 직전 DmaCluster pass 실행
 (NS 소스 전부)  ┘                                   │    ├─ ① DMA 버퍼 찾기
                                                     │    ├─ ② 버퍼마다 section 이름표 붙이기
                                                     │    └─ ③ dma_layout.json 기록
                                                     └─ ns_pass1.elf + ns_pass1.elf.map
                                                         (map = 모든 .bss 객체의 크기와 정렬)
                                   │
                                   ▼
             gen_layout.py  (dma_layout.json + ns_pass1.elf.map 를 읽음)
                ├─ dma_clusters.ld   ← 주소 배치 규칙 (이 문서의 핵심)
                ├─ dma_regions.h     ← 태스크별 NS-MPU region (xRegions)
                ├─ dma_manifest.c    ← Secure 부팅 코드가 읽을 cluster 표
                └─ layout_final.json ← 입력 사본
                                   │
                                   ▼
                          (2차 링크: 진짜 이미지)
 같은 오브젝트 + dma_regions.h 로 다시 컴파일한 app_freertos.o + dma_manifest.o
   ─► lld (NS 링커 스크립트가 INCLUDE dma_clusters.ld) ─► ns.elf
   ─► check_layout.py : 1차와 2차 JSON 이 같은지, 주소·region·manifest 가 맞는지 확인
```

**링크를 왜 두 번 하나요?**
`gen_layout.py` 는 "cluster 앞 빈 칸에 어떤 .bss 객체를 넣을지" 정해야 하는데, 객체 크기는 컴파일과 LTO 를 한 번 거쳐야 알 수 있습니다. 그래서 1차 링크는 **측정용**입니다. 1차 링크 때는 `dma_clusters.ld` 자리에 모든 `.dma.*` 를 한 곳에 모으기만 하는 임시 파일(`ns/dma_clusters_pass1.ld`)이 들어갑니다.

---

## 2. pass 가 하는 일: 소스 → JSON

### 2.1 시작점: 태스크 함수 찾기
pass 는 IR 에서 `xTaskCreateRestricted` 같은 태스크 생성 API 의 호출을 찾고, 거기에 넘긴 함수 포인터를 태스크로 기록합니다.

```json
"tasks": { "prvTimerTask": …, "vDmaTaskA": …, "vDmaTaskB": …, "vPlainTask": … }
```

### 2.2 DMA 요청 지점(sink) 찾기
설정 파일 `config/dma_config.json` 에 "이 모양의 코드가 DMA 요청이다"라고 적혀 있습니다.

```json
{ "svc": 110, "args": [1] }     // inline asm "svc #110" 이 DMA 요청이고, r1 이 목적지 주소
```

소스에서는 이 줄입니다.
```c
194:  __asm volatile ("svc %[n]" : "+r"(r0) : "r"(r1), "r"(r2), [n] "i"(portSVC_DMA_M2M_START) : "memory");
```

### 2.3 r1 을 거꾸로 따라가서 전역 변수 찾기
`App_DmaStartSvc`(194행)는 `App_DmaM2M`(205행) 안으로 인라인되고, 그것이 다시 태스크 함수 안으로 인라인됩니다. pass 는 r1 에 들어간 값을 거꾸로 따라가 **어떤 전역 변수의 주소인지** 찾습니다.

```
svc #110 의 r1 (194행)
   ◄─ App_DmaM2M 의 dst 인자 (205행)
        ◄─ vDmaTaskA 258행: s_taskA.dst      → 전역 s_taskA, 필드 1 (오프셋 96)
        ◄─ vDmaTaskA 261행: s_dmaShared
        ◄─ vDmaTaskB 281행: s_dmaDstB
        ◄─ vDmaTaskB 284행: s_dmaShared + 32
```

JSON 의 `"sites"` 가 바로 이 경로입니다. `194 <- 205 <- 258` 는 "258행에서 부른 것이 205행을 거쳐 194행의 svc 에 닿았다"는 뜻입니다.

### 2.4 버퍼마다 주인(owner) 정하기

| 버퍼 | 쓰는 태스크 | 판정 | 붙인 section |
|---|---|---|---|
| `s_taskA` (struct 의 `dst` 필드만 DMA) | A | A 전용, **head 있음** | `.dma.h.vDmaTaskA` |
| `s_dmaDstB` | B | B 전용 | `.dma.t.vDmaTaskB` |
| `s_dmaShared` | A, B | 공유. 설정 `shared_host` 에 따라 **B cluster 끝에 붙임** | `.dma.x.vDmaTaskB` |

section 이름의 가운데 글자 뜻은 이렇습니다.
- `.dma.t.<태스크>`: 그 태스크 전용 버퍼
- `.dma.h.<태스크>`: 앞부분(head)은 DMA 대상이 아닌 struct
- `.dma.x.<host>`: 다른 태스크와 공유하지만 host 태스크 cluster 의 끝에 놓는 버퍼

### 2.5 struct 의 "head" 란?
```c
116: typedef struct
117: {
118:     dma_task_priv_t priv;             /* globals: DMA 대상 아님 (96 B) */
119:     uint8_t         dst[256];         /* DMA 목적지 — 마지막 필드 (뒤에 guard) */
120: } dma_task_a_mem_t;
121: static dma_task_a_mem_t s_taskA;
```
C 에서 struct 는 쪼갤 수 없는 한 덩어리입니다. 그래서 `s_taskA` 전체가 한 자리에 놓입니다. 하지만 DMA 로 쓰는 것은 `dst` 뿐입니다.

pass 는 IR 의 주소 계산식(GEP)을 보고 "필드 1, 오프셋 96" 을 알아냅니다. 그리고 **앞 96 B(`priv`)는 head** 로 표시합니다. head 는 struct 와 함께 놓이지만 S-MPU 태그(DMA 허용 구역) 밖에 둡니다.

```
s_taskA (352 B)
 0        96                              352
 ├─ priv ─┼──────────── dst ──────────────┤
 │  head  │      cluster (S-MPU 태그)      │
 └ DMA 불가┘└────────── DMA 가능 ───────────┘
```

진단 메시지가 이 판정을 알려 줍니다.
```
warning struct-field: s_taskA: 필드 1 (오프셋 96) 부터 DMA 버퍼 → 앞 96 B 는 태그 밖 head
```

### 2.6 JSON 한 줄씩 읽기 (cluster c1 = Task B)
```jsonc
{
  "id": "t.vDmaTaskB",            // cluster 이름: Task B 전용
  "kind": "task",
  "owners": ["vDmaTaskB"],        // 이 cluster 전체를 NS-MPU region 으로 받는 태스크
  "head": 0,                      // head 없음
  "body_bound": 256,              // 전용 버퍼 합계 상한 (정렬 패딩 포함)
  "hosted_bound": 64,             // 끝에 붙일 공유 버퍼 합계
  "hosted_owners": ["vDmaTaskA"], // 공유 버퍼 칸만 NS-MPU region 으로 받는 다른 태스크
  "bytes_bound": 320,             // head + body + hosted
  "section": ".dma.t.vDmaTaskB",  "hosted_section": ".dma.x.vDmaTaskB",
  "buffers": [
    { "name": "s_dmaDstB",   "size": 256, "hosted": false, "users": ["vDmaTaskB"] },
    { "name": "s_dmaShared", "size": 64,  "hosted": true,  "users": ["vDmaTaskA","vDmaTaskB"] }
  ]
}
```
cluster c0 (Task A) 는 `"head": 96, "head_buffer": "s_taskA", "body_bound": 256, "bytes_bound": 352` 입니다.

---

## 3. gen_layout.py 가 하는 일: JSON → 크기 계산

규칙은 세 가지뿐입니다.
1. **S-MPU 와 NS-MPU 는 32 B 단위**이므로 cluster 크기(cap)는 32 B 단위로 올린다.
2. **MPCBB guard 는 256 B 블록 단위**이므로 cluster 의 끝은 256 B 경계에 맞춘다. 바로 뒤 256 B 가 guard 이다.
3. cluster 를 guard 쪽으로 붙이고(오른쪽 정렬), 블록 안에서 **앞에 남는 칸(pre)** 은 일반 NS 데이터로 채운다.

```
cap  = 32 단위로 올림(body) + 32 단위로 올림(hosted)
span = 256 단위로 올림(head + cap)      ← 블록 몇 개를 쓰는가
pre  = span − head − cap                ← 앞 빈 칸
```

| | head | body | hosted | cap | span | pre |
|---|---|---|---|---|---|---|
| c0 (A) | 96 | 256 | 0 | 256 | 512 | **160** |
| c1 (B) | 0 | 256 | 64 | 320 | 512 | **192** |

앞 빈 칸은 1차 링크 map 의 `.bss.<이름>` 객체로 채웁니다. 큰 것부터 넣어 보는 방식(First-Fit Decreasing)이고, 크기 우선과 정렬 우선 두 순서로 해 보고 더 많이 채운 쪽을 고릅니다.
- c0: 160 B 중 133 B (`htim6`, `KernelState`, `uwTick` …)
- c1: 192 B 중 96 B (`s_privB`)

---

## 4. 생성된 링커 스크립트 한 줄씩 읽기

### 4.1 들어가는 위치
프로젝트 NS 링커 스크립트(사본)의 `.data` 앞에 한 줄이 있습니다.
```ld
INCLUDE dma_clusters.ld      /* .data/.bss 규칙보다 앞 → .dma.* 가 .bss* 에 먼저 잡히지 않게 */
```
링커는 규칙을 위에서부터 적용합니다. 그래서 여기 적힌 `.bss.htim6` 같은 객체는 원래 `.bss` 로 가지 않고 이 자리로 옵니다. 결과적으로 cluster 는 NS SRAM 맨 앞(0x20018000)부터 놓입니다.

### 4.2 c0 (Task A)
```ld
.dma_c0 (NOLOAD) : ALIGN(256)            /* ① 출력 섹션 .dma_c0, 256 B 경계에서 시작. NOLOAD = 플래시에 내용 없음 */
{
  __dmac0_pre_start = .;                 /* ② "." = 현재 주소 (0x20018000). 이름을 붙여 둔다 */
  *(.bss.htim6)                          /* ③ 앞 칸 채우기: 1차 map 에서 고른 일반 .bss 객체들 */
  *(.bss.KernelState)
  *(.bss.s_dmaChannelMutex)
  *(.bss.s_dmaStatusWord)
  *(.bss.ulMaxPRIGROUPValue)
  *(.bss.uwTick)
  *(.bss.ucMaxSysCallPriority)
  __dmac0_pre_end = .;                   /*    = 0x20018085 (133 B 사용) */
  . = __dmac0_pre_start + 0xa0;          /* ④ 현재 주소를 앞 칸 끝(160 B)으로 강제 이동 */
  __dmac0_obj = .;                       /* ⑤ 0x200180A0: head 시작 = s_taskA 시작 = NS-MPU region 시작 */
  KEEP(*(.dma.h.vDmaTaskA))              /* ⑥ s_taskA 352 B 를 여기 놓는다. KEEP = 안 쓰는 것처럼 보여도 지우지 마라 */
  __dmac0_start = __dmac0_obj + 0x60;    /* ⑦ 0x20018100: head 96 B 를 건너뛴 곳 = S-MPU 태그 시작 */
  KEEP(*(.dma.t.vDmaTaskA))              /* ⑧ Task A 의 다른 전용 버퍼 (지금은 없음) */
  __dmac0_fill = .;                      /*    실제로 채운 끝 */
  . = __dmac0_start + 0x100;             /* ⑨ cluster 크기 256 B 만큼 이동 */
  __dmac0_end = .;                       /* ⑩ 0x20018200: cluster 끝 = guard 시작 (MPCBB Secure 로 만들 블록) */
  . = . + 0x100;                         /* ⑪ guard 256 B 는 비워 둔다 (NS 데이터 금지) */
  __dmac0_guard_end = .;                 /*    0x20018300 */
} > RAM
ASSERT(__dmac0_fill <= __dmac0_start + 0x100, "dma cluster c0 overflow")   /* 버퍼가 커져 guard 를 침범하면 링크 실패 */
ASSERT(__dmac0_fill >= __dmac0_start, "... head 구조체가 head 보다 작음")
ASSERT(__dmac0_pre_end - __dmac0_pre_start == 0x85, "... pre-fill 가 1차 링크와 다름")  /* 1·2차 크기가 다르면 실패 */
ASSERT((__dmac0_obj % 32) == 0, "... NS-MPU base not 32B aligned")
ASSERT((__dmac0_start % 32) == 0, "... S-MPU base not 32B aligned")
ASSERT((__dmac0_end % 256) == 0, "... guard not 256B aligned")
```

### 4.3 c1 (Task B + 공유 버퍼)
```ld
.dma_c1 (NOLOAD) : ALIGN(256)
{
  __dmac1_pre_start = .;                 /* 0x20018300 (guard A 바로 뒤) */
  *(.bss.s_privB)                        /* 앞 칸: Task B 의 globals (DMA 대상 아님) */
  __dmac1_pre_end = .;
  . = __dmac1_pre_start + 0xc0;          /* 앞 칸 192 B 끝 */
  __dmac1_obj = .;                       /* 0x200183C0 (head 없음 → obj = start) */
  __dmac1_start = .;                     /* S-MPU 태그 시작 */
  KEEP(*(.dma.t.vDmaTaskB))              /* s_dmaDstB 256 B */
  __dmac1_fill = .;
  . = __dmac1_start + 0x100;             /* 전용 칸(256 B) 끝으로 이동 */
  __dmac1_shr = .;                       /* 0x200184C0: 공유 버퍼 칸 시작 = A 의 두 번째 NS-MPU region 시작 */
  KEEP(*(.dma.x.vDmaTaskB))              /* s_dmaShared 64 B */
  __dmac1_shr_fill = .;
  . = __dmac1_start + 0x140;             /* cluster 끝 (320 B) */
  __dmac1_end = .;                       /* 0x20018500 = guard B 시작. 공유 버퍼가 guard 바로 앞 */
  . = . + 0x100;
  __dmac1_guard_end = .;                 /* 0x20018600 */
} > RAM
```

### 4.4 그 밖의 규칙
```ld
.dma_orphan (NOLOAD) : { KEEP(*(.dma.*)) } > RAM
ASSERT(SIZEOF(.dma_orphan) == 0, "orphan .dma.* input section")
```
위 규칙에 잡히지 않은 `.dma.*` 가 남으면 링크를 실패시킵니다. pass 와 생성물이 어긋났다는 뜻이기 때문입니다.

```ld
.dma_zero_table : ALIGN(4)
{
  __dma_zero_table_start = .;
    LONG(__dmac0_pre_start) LONG(__dmac0_end)     /* [0x20018000, 0x20018200) 를 0 으로 */
    LONG(__dmac1_pre_start) LONG(__dmac1_end)     /* [0x20018300, 0x20018500) 를 0 으로 */
  __dma_zero_table_end = .;
} > ROM
```
cluster 는 NOLOAD 라서 startup 의 `.bss` 초기화 루프가 지우지 않습니다. 그래서 startup 이 `bl dma_zero_clusters`(`ns/dma_zero.c`)로 이 표의 구간을 0 으로 채웁니다. **guard 는 표에서 뺍니다.** guard 는 이미 Secure 블록이라, NS 가 쓰면 TZIC 인터럽트가 납니다.

---

## 5. 최종 메모리 그림 (ns.elf 실측)

```
 주소          내용                                    S-MPU 태그   NS-MPU (태스크)          MPCBB
 0x20018000 ┌──────────────────────────────────┐
            │ htim6, KernelState, uwTick …     │      ─           (커널/드라이버)          NS
            │ (앞 칸 133/160 B, 27 B 빈칸)      │
 0x200180A0 ├──────────────────────────────────┤ ┐                ┐
            │ s_taskA.priv  (Task A globals)   │ │ ─              │ A region ①             NS
 0x20018100 ├──────────────────────────────────┤ │ ┐              │ {0x200180A0, 0x160}
            │ s_taskA.dst   256 B              │ │ │ RNR4         │                        NS
 0x20018200 ├──────────────────────────────────┤ ┘ ┘              ┘
            │ guard A       256 B              │      ─           ─                        Secure (블록 386)
 0x20018300 ├──────────────────────────────────┤
            │ s_privB (Task B globals 96 B)    │      ─           B region (s_privB)       NS
            │ 빈칸 96 B                         │
 0x200183C0 ├──────────────────────────────────┤   ┐              ┐
            │ s_dmaDstB     256 B              │   │ RNR5         │ B region               NS
 0x200184C0 ├──────────────────────────────────┤   │              │ {0x200183C0, 0x140}  ┐
            │ s_dmaShared    64 B              │   │              │                      │ A region ②
 0x20018500 ├──────────────────────────────────┤   ┘              ┘                      ┘ {0x200184C0, 0x40}
            │ guard B       256 B              │      ─           ─                        Secure (블록 389)
 0x20018600 ├──────────────────────────────────┤
            │ .data / .bss (나머지 NS 데이터)    │
            └──────────────────────────────────┘
 DMA 주소는 증가 방향 → 각 cluster 를 넘는 순간 바로 뒤 guard 에 닿는다
```

---

## 6. 나머지 생성물

### dma_regions.h: 태스크가 접근할 수 있는 NS-MPU region
```c
#define DMA_REGION_C0        { (void *)__dmac0_obj, 0x160u, RW | XN }  /* s_taskA 전체 (head + cluster) */
#define DMA_REGION_C1        { (void *)__dmac1_obj, 0x140u, RW | XN }  /* s_dmaDstB + s_dmaShared */
#define DMA_REGION_C1_SHARED { (void *)__dmac1_shr, 0x40u,  RW | XN }  /* s_dmaShared 만 */
#define DMA_TASK_REGIONS_vDmaTaskA  DMA_REGION_C0, DMA_REGION_C1_SHARED   /* A: 2 개 */
#define DMA_TASK_REGIONS_vDmaTaskB  DMA_REGION_C1                         /* B: 1 개 (+ s_privB 1 개) */
```
`app_freertos.c` 의 `xTaskCreateRestricted` 인자(`xRegions`)에 이 매크로가 들어갑니다. 주소는 링커 심볼이라 2차 링크 때 채워집니다.

### dma_manifest.c: Secure 부팅 코드가 읽는 표 (NS flash 0x08040400)
```c
{ 0u, DMA_KIND_TASK, __dmac0_start, __dmac0_end, 1u },   /* S-MPU RNR4 = [0x20018100, 0x20018200), guard 1 블록 */
{ 1u, DMA_KIND_TASK, __dmac1_start, __dmac1_end, 1u },   /* S-MPU RNR5 = [0x200183C0, 0x20018500), guard 1 블록 */
```
- manifest 의 시작 주소는 `__dmac0_start` 입니다. `__dmac0_obj` 가 아니므로 **head 는 S-MPU 태그에 들어가지 않습니다.**
- Secure 의 `dma_boot_config()` 는 이 표를 검증합니다 (정렬, 범위, 겹침). 그다음 S-MPU region 과 MPCBB guard 를 설정합니다.
- Secure 쪽 생성물(`build/secure/gen/secure_guard_mem.ld`, `secure_guard_sec.ld`)은 guard 블록을 Secure 메모리 영역으로 정의하고, 그 안에 TZIC 로그 객체를 놓습니다.
  - guard 의 Secure alias `0x30018200` 에 `g_tzic_log0` 와 `g_tzic_sum` 이, `0x30018500` 에 `g_tzic_log1` 이 들어갑니다.
  - 고정 방법: 소스의 `annotate("guard:<k>")` 를 `SecureGuardSelect` pass 가 `"pin": k` 로 기록하고, `pack_guards.py` 가 guard k 에 놓습니다.
  - NS 쪽에서 이 블록은 Secure 이므로, NS DMA 가 쓰면 무시되고 읽으면 0 입니다. 그래서 로그를 NS 가 바꾸거나 읽을 수 없습니다.

---

## 7. 이 배치로 무엇이 막히는가 (mediator 검사와 연결)

mediator 는 요청 주소의 양 끝을 두 번 검사합니다.
- TTAT 는 호출 태스크의 NS-MPU 로 "이 태스크가 쓸 수 있는 곳인가"를 봅니다.
- TTT 는 S-MPU 로 "DMA 허용 구역(태그)인가"를 봅니다.

| 요청 (Task A) | TTAT (NS-MPU) | TTT (S-MPU) | 결과 | 이유 |
|---|---|---|---|---|
| `s_taskA.dst` | ① 안 | RNR4 안 | 허용 | |
| `s_taskA.priv` (같은 struct 의 globals) | ① 안 | **태그 밖** | **거부** | head 를 태그 밖에 뒀기 때문 |
| `s_dmaDstB` | 밖 | — | 거부 | B 의 버퍼 |
| `s_dmaShared` | ② 안 | RNR5 안 | 허용 | |
| `s_dmaShared` 끝 + 4 B | 끝이 guard B | — | 거부 | |

전송 도중 길이를 변조하면 다음과 같이 막힙니다.
- A 가 `dst` 에서 넘치면 guard A 에 닿습니다.
- A 나 B 가 `s_dmaShared` 에서 넘치면 guard B 에 닿습니다.
- guard 에 닿으면 TZIC 인터럽트가 나고 `GTZC_IRQHandler` 가 채널을 멈춥니다.

**막지 않는 것이 하나 있습니다.** B 가 `s_dmaDstB` 에서 넘치면 guard 가 아니라 `s_dmaShared` 로 들어갑니다. B 는 원래 공유 버퍼에 쓸 권한이 있으므로 권한 밖 쓰기는 아닙니다. 공유 버퍼에 guard 를 따로 두지 않기로 한 데 따른 대가입니다.
