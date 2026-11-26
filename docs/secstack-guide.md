# DMA Task Secure 스택 static binding: 직접 구현 가이드

근거 태그: [실측] 보드/빌드 측정, [코드] 레포 코드로 확인, [아키텍처 도출] Armv8-M 규칙에서 도출, [추정], [미확인].
Armv8-M ARM(DDI0553) 과 RM0438 절 번호는 이 문서에서 확인하지 못했다. 주제 이름만 적는다 (미확인).

## 0. 전체 그림

### 0.1 왜 하나
- stock FreeRTOS 는 Secure context 가 있는 Task 가 전환될 때마다 PendSV 가 `SecureContext_SaveContext` 와
  `SecureContext_LoadContext` 를 NSC 로 부른다. 그 결과 전환 1회 비용이 +544 cyc (P1, cache off), +244 (warm) 늘어난다
  [실측, `git show 983a749:results/summary_phase45.md`].
- DMA Task 는 Secure 코드(중재자)를 아주 짧게만 쓴다. 그래서 DMA Task 는 자기 MPCBB guard(256 B)를 Secure 스택으로 고정하고,
  **Secure 안에서 선점된 경우에만** NSC 를 부르게 바꾼다.
- 덤으로 중재자를 FP 명령 없는 asm 으로 쓴다. 그러면 예외 프레임이 208 B 에서 72 B 로 줄고, NS Task 스택 +64 B 도 사라진다
  (이전 분석: `cmpMemUsage_result.md` §5.6 와 208 B 분해).

### 0.2 상태 (Secure Thread 스택 PSP_S / PSPLIM_S 가 어디를 가리키나)
| 상태 | PSP_S | PSPLIM_S | 언제 |
|---|---|---|---|
| park (쉼) | `P+248` (Secure 정적 256 B 블록) | 0 | Secure Thread 를 쓰는 Task 가 없을 때. SG 직후 창의 예외 프레임이 여기 쌓임 |
| heap (stock) | stock context 스택 | stock limit | Plain Task 등. 기존 그대로 |
| guard | `G+248` 이하 (G = guard Secure alias) | G | DMA Task 가 중재자 안에 있을 때 |

불변식: **Secure Thread 가 쓰이지 않는 동안에는 항상 park, PSPLIM=0.** stock Load/Alloc 의 전제 `PSPLIM == 0` 이 그대로 성립한다.

### 0.3 경우
- ① DMA Task 가 NS 에서 전환됨: NSC 0회 (이득)
- ② 요청: stub 이 guard 설치 → 중재 → park 복귀
- ③ 중재 중 선점: Suspend 1회 + Resume 1회
- ④ SG 직후(park 위) 또는 stock NSC 안에서 선점: Suspend 가 프레임을 guard 로 옮김. 평가에서 제외

### 0.4 되돌리기
- git: 시작 전에 `git switch impl/paper && git tag pre-secstack && git switch -c impl/secstack`.
  원본으로 가려면 `git switch impl/paper`. `baseline*` 태그는 건드리지 않는다.
- 빌드 스위치: `Secure_nsclib/secstack_cfg.h` 의 `SECSTACK_STATIC` (기본 1). 모든 변경을 `#if SECSTACK_STATIC` 안에 넣고,
  0 이면 원본과 같은 바이너리가 나오게 한다. 확인 방법은 §9.1.

---

## 1. 먼저 알아야 할 개념

| 개념 | 핵심 | 이 설계에서 쓰는 곳 | 볼 자료 |
|---|---|---|---|
| SG / NSC veneer | NS 는 `.gnu.sgstubs` 의 `SG; B.W __acle_se_X` 로만 Secure 에 들어온다. 링커가 `X` 와 `__acle_se_X` 두 심볼이 있으면 veneer 를 만든다 | §5 stub | Secure `.list` 의 `.gnu.sgstubs`, `STM32L562QEIXQ_FLASH.ld` 의 `.gnu.sgstubs` (ROM_NSC) |
| 뱅크된 SP | PSP/PSPLIM 은 Secure/NS 각각 있다. Secure Thread(CONTROL_S.SPSEL=1)의 SP 는 PSP_S | park/guard 전환 | `secure_context_port.c` 의 `msr psp`, `msr psplim` |
| 스택 limit 검사 | SP 가 PSPLIM 보다 아래로 내려가면 STKOF. **PSP 를 먼저 올리고 PSPLIM 을 나중에 올린다. 내릴 때는 PSPLIM=0 을 먼저** | stub, Suspend/Resume 의 순서 | Armv8-M ARM "stack limit checks" (절 번호 미확인). MSR PSP 가 limit 검사를 받는지는 [미확인] |
| 예외 프레임 | 기본 32 B. FPCA=1 이면 +72 (s0–s15, FPSCR). TS=1 이면 +64 (s16–s31). Secure→NS 예외면 추가 상태 40 B (서명, r4–r11) | guard 크기 계산: 8 + 208 + 정렬 ≤ 248 | `cmpMemUsage_result.md` §5.6, 이전 208 B 분해 |
| EXC_RETURN | bit6 S (프레임이 Secure 스택에 있음), bit5 DCRS, bit4 FType (0 = FP 프레임), bit0 ES | PendSV 의 `tst lr,#0x40` | `portasm.c` PendSV 의 `lsls r3,r3,#25 / bmi` 주석 |
| FPCA / lazy stacking | FP 명령을 실행하면 FPCA=1. 예외 때 FP 자리만 잡아 두고 나중에 채운다 (LSPEN=1). stock Save 는 `vstmdb/vldmia s0` 로 지연 저장을 끝낸다 | Suspend 에서 같은 관용구 | `secure_context_port.c SaveContextAsm`, `port.c prvSetupFPU` |
| CMSE 복귀 정리 | BXNS 전에 Secure 값이 남은 레지스터를 지운다. GCC 는 hard-float 에서 s0–s15 까지 지운다 (`vmov #1.0`) | stub 은 r1–r3, r12, APSR 만 정리 (FP 미사용) [아키텍처 도출] | 현재 `__acle_se_SECURE_dma_m2m_start` 디스어셈블 |
| SRAM alias | 같은 SRAM 이 0x2xxx (SAU 상 NS) 와 0x3xxx (Secure) 두 주소로 보인다. Secure 코드는 guard 를 **0x3xxx 로** 접근해야 한다 (0x2xxx 로 접근하면 NS 트랜잭션이 되어 MPCBB 에 막힘) | guard 주소 = NS 주소 + 0x10000000 | `partition_stm32l562xx.h` SAU2 (0x20018000–0x2003FFFF 만 NS) |
| MPCBB | 256 B 블록 단위 Secure 속성. NS DMA 가 쓰면 무시, 읽으면 bus error | NS 는 guard 스택을 보거나 바꿀 수 없다 | CLAUDE.md "확정된 하드웨어 사실", `main.c SMPU_SetupTaskRegions` |
| TT 계열 | TTAT = NS-MPU 비특권 기준, TTT = S-MPU. bit16 MRVALID, bit19 RW, bit21 NS RW | stub 안의 검증 | `secure_nsc.c verify_inst` 주석, `arm_cmse.h` |
| LDREX/STREX | 예외 복귀가 monitor 를 지우므로 선점되면 STREX 가 실패한다 | `g_dma_busy` 획득 | Armv8-M ARM "exclusive monitor" (절 번호 미확인) |
| stock Secure context | handle = index+1 (1..8). Load/Alloc 전제 `PSPLIM==0`, Save 후 PSP=PSPLIM=0. CONTROL_S 는 항상 0x02 (수정본) | 인코딩과 park | `secure_context.c` 전체, 특히 `AllocateContext`, `LoadContext`, `SaveContext` |

자가 점검 (구현 전에 보드에서):
1. 현재 빌드에서 `b __acle_se_SECURE_dma_m2m_start` → `info registers` 로 PSP_S 와 PSPLIM_S 의 레지스터 이름을 확인한다
   (ST-LINK gdbserver 가 `psp_s`/`psplim_s` 로 보여 주는지 [미확인]). `p/x $psp` 가 stock context 스택(0x3000xxxx)이어야 한다.
2. `x/52wx 0x30000408` (S_P1 Task A context 바닥, 이전 208 B 분해 절차) 로 FP 확장 프레임 모양을 직접 본다.

---

## 2. 항목 A: 공유 헤더와 guard형 인코딩

**의도.** NS 와 Secure 가 같은 스위치와 판정 기준을 쓰게 한다.

**봐야 할 코드.**
- `secure_context.c AllocateContext`: `xSecureContextHandle = ulSecureContextIndex + 1UL;` → stock handle 은 1..8.
  - **guardasstack.md 3.1 의 "bit0 = 0 이면 stock" 은 틀리다.** handle 1, 3, 5, 7 은 bit0 이 1 이다 [코드].
- `secure_context.c FreeContext`: handle 이 1..8 일 때만 동작한다 → guard형 값은 무시된다 (Free 쪽은 수정할 필요 없음).

**바꿀 내용.**
- 새 파일 `Secure_nsclib/secstack_cfg.h` (Secure 와 NS 모두 include 경로에 있음. NS 경로는 Phase 시작 때 subdir.mk 에서 확인):
  ```c
  #ifndef SECSTACK_STATIC
  #define SECSTACK_STATIC 1
  #endif
  /* secure_context.c secureconfigMAX_SECURE_CONTEXTS (기본 8) 와 같아야 함 */
  #define SECSTACK_MAX_STOCK_HANDLE 8u
  /* xSecureContext > SECSTACK_MAX_STOCK_HANDLE 이면 guard형: 값 = guard Secure alias base */
  ```
- 새 파일 `Secure/Inc/secure_stack.h` (Secure 전용): `main.c` 의 `DMA_TASK_*` 매크로(EVAL_MEM 분기 포함)를 옮기고 다음을 정의한다.
  `GUARD_A_S = DMA_TASK_A_GUARD_BASE + 0x10000000`, `GUARD_STRIDE = 0x300`, `N_GUARD = 2`, `SECSTACK_REST = 248`,
  `SECSTACK_SEAL = 0xFEF5EDA5`. `main.c` 는 이 헤더를 include 한다 (값은 같음).
  - `.s` 에서도 include 하므로 C 전용 문법(캐스트, `u` 접미사)은 피한다. `0x2001b600` 처럼 접미사 없이 쓴다.

**확인.** `./build.sh` 통과. Secure `.map` 의 `SMPU` 관련 리터럴이 전과 같다 (`main.o` 의 `.text` 비교).

**연계.** PendSV 판정(§6), stub 의 guard 검사(§5), Suspend/Resume 조회(§4), 바인딩(§7).

---

## 3. 항목 B: park (stock 두 곳만 수정)

**의도.** Secure Thread 가 쉬는 동안에도 PSP_S 가 쓸 수 있는 메모리를 가리키게 한다. SG 직후, stub 이 guard 를 설치하기 전에
예외가 오면 하드웨어는 PSP_S 에 프레임을 쌓는다. 지금처럼 PSP_S = 0 이면 0xFFFFFFxx 에 쓰다가 fault 가 난다 [아키텍처 도출].
stock 은 이 창이 없다 (Load 로 미리 stock 스택을 설치하므로).

**봐야 할 코드.**
- `secure_context.c SecureContext_Init`: `secureportSET_PSPLIM( securecontextNO_STACK ); secureportSET_PSP( securecontextNO_STACK );`
- `secure_context_port.c SaveContextAsm` 끝: `movs r1, %0 / msr psplim, r1 / msr psp, r1`

**바꿀 내용 (guardasstack.md 와 다른 점).** PSPLIM 은 0 그대로 두고 PSP 만 park 로 바꾼다. 그래서 Alloc/Load 의 `PSPLIM == NO_STACK`
전제는 **수정하지 않는다**.
- Init: `secureportSET_PSPLIM(0); secureportSET_PSP((uint32_t)&g_secstack_park[SECSTACK_REST]);` (`#if SECSTACK_STATIC`)
- SaveContextAsm: `msr psplim, r1(=0)` 다음 `ldr r1, =g_secstack_park + 248` (또는 movw/movt), `msr psp, r1`.
  순서를 지킨다: PSPLIM=0 먼저.
- park 버퍼 `g_secstack_park[256] __attribute__((aligned(256)))` 는 `secure_stack.c` 에 정의하고, 맨 위 8 B 에 seal 을 넣는다 (§4 Init).

**효과.** stock Task 의 Save 에 상수 적재 1개가 더해진다 [추정: 1–2 cyc]. 그 밖의 stock 동작은 같다.

**확인.**
- 빌드 후 `arm-none-eabi-objdump -d --disassemble=SecureContext_SaveContextAsm Secure.elf` 에서 msr 순서를 확인한다.
- 보드: Plain Task LED 토글 정상. 아무 Task 도 Secure 에 없을 때 halt → PSP_S = park+248, PSPLIM_S = 0.

**연계.** park 를 쓰는 곳: SG 직후 창(§5), Suspend/Resume 의 실패 처리(§4), stub 복귀(§5). stock Load 전제(PSPLIM==0).

---

## 4. 항목 C: `Secure/Src/secure_stack.c` (Init / Suspend / Resume)

**의도.** guard Task 가 Secure 안에서 선점됐을 때만 그 SP 를 slot 에 맡겼다가 되돌린다.

**봐야 할 코드.** `secure_context.c` 의 NSC 함수 형식(`secureportNON_SECURE_CALLABLE`, IPSR 검사), `secure_context_port.c` 의 FP 관용구,
`secure_port_macros.h` 의 `secureportSET_PSP/PSPLIM`, `secureportREAD_IPSR`.

**바꿀 내용 (스케치).**
```c
#if SECSTACK_STATIC
uint8_t g_secstack_park[256] __attribute__((aligned(256)));
volatile uint32_t g_secstack_slot[N_GUARD];      /* 0 = 비어 있음. bit0 = 1: 경우 ④ 로 옮긴 프레임 */
volatile uint32_t *g_pxNsSecureCtx;              /* NS xSecureContext 주소. stub 이 읽음 */

static int prvIndex(uint32_t g) { /* g 가 GUARD_A_S 또는 GUARD_A_S+STRIDE 면 0/1, 아니면 -1 */ }
static void prvPark(void) { secureportSET_PSPLIM(0); secureportSET_PSP((uint32_t)&g_secstack_park[SECSTACK_REST]); }

secureportNON_SECURE_CALLABLE void SecureStack_Init(volatile uint32_t *pxNs)
{   /* IPSR != 0 이고 처음 한 번만. cmse_check_address_range(pxNs, 4, CMSE_NONSECURE | CMSE_MPU_READWRITE)
       각 guard (Secure alias) 와 park 의 +248, +252 에 seal 2개. 마지막에 prvPark() */ }

secureportNON_SECURE_CALLABLE void SecureStack_Suspend(uint32_t ctx)
{   /* IPSR == 14 (PendSV) 아니면 return. i = prvIndex(ctx); i < 0 이면 prvPark(); return;
       FP 관용구로 지연 저장을 끝냄 (configENABLE_FPU 일 때 SaveContextAsm 과 같은 vstmdb/vldmia s0)
       sp = PSP; sp 가 [G, G+248) 안이면 slot[i] = sp                      (경우 ③)
       아니면 (경우 ④) n = (sp 블록의 rest top) - sp; n <= 248 이면 [sp, sp+n) 을 G+248-n 으로 word 복사,
              slot[i] = (G+248-n) | 1;  아니면 slot[i] = 0 (fail closed)
       prvPark(); */ }

secureportNON_SECURE_CALLABLE void SecureStack_Resume(uint32_t ctx)
{   /* IPSR == 14 아니면 return. i 유효하고 slot[i] != 0:
          PSPLIM=0 → PSP = slot & ~1 → PSPLIM = (slot & 1) ? 0 : G;  slot[i] = 0
       아니면 PSP = 0, PSPLIM = 0 (fail closed: 이 Task 가 Secure 로 돌아가면 fault) */ }
#endif
```
- 경우 ④ 에서 PSPLIM=0 으로 되돌리는 이유: stock NSC (BENCH 의 `SECURE_nop3` 등)는 복귀할 때 park 로 돌아가지 않는다.
  PSPLIM 이 G 로 남으면 다음 stock Task 의 Load 전제(`PSPLIM==0`)가 깨진다 [코드].
- slot 을 Resume 에서 0 으로 지우는 이유: slot != 0 은 "이 guard 에 정지된 Secure 프레임이 있다"는 뜻이다.
  stub 이 이 값을 보고 다른 Task 의 설치를 거부한다 (§5, 바인딩 무결성).

**확인.**
- 빌드, `nm Secure.elf | grep -E "SecureStack_|g_secstack"`. `Secure_nsclib` 에 세 함수의 veneer 가 생겼는지 확인한다.
- 보드: `x/2wx GUARD_A_S+248` = `0xfef5eda5 0xfef5eda5`, `p g_secstack_slot` 은 평상시 `{0, 0}`.

**연계.** PendSV(§6) 가 부른다. stub(§5) 이 slot 을 읽는다. park(§3).

---

## 5. 항목 D: 중재자 stub `Secure/Src/secure_dma_entry.s`

**의도.** 컴파일 옵션과 상관없이 FP 명령 없이, 필요한 것만 쌓는 중재자. guard 는 검증 **전에** 커널이 정한 값으로 설치한다.

**봐야 할 코드.** 현재 `secure_nsc.c` 의 `verify_inst` (명령 순서), `SECURE_dma_m2m_start` (DMA 레지스터 쓰기 순서),
P2 디스어셈블의 오프셋 (DMA1 기준 IFCR +4, CCR1 +8, CNDTR1 +0xC, CPAR1 +0x10, CM0AR1 +0x14), Secure 에서 `DMA1 = DMA1_S`
(`DMA1_BASE_S = AHB1PERIPH_BASE_S = 0x50000000 + 0x20000 = 0x50020000` [코드: stm32l562xx.h `PERIPH_BASE_S`, `AHB1PERIPH_BASE_S`]).

**바꿀 내용 (스케치, 확인하며 고칠 것).**
```asm
#include "secstack_cfg.h"
#include "secure_stack.h"
#if SECSTACK_STATIC
    .syntax unified
    .thumb
    .text
    .global SECURE_dma_m2m_start, __acle_se_SECURE_dma_m2m_start
    .type   SECURE_dma_m2m_start, %function
    .type   __acle_se_SECURE_dma_m2m_start, %function
    .thumb_func
SECURE_dma_m2m_start:
__acle_se_SECURE_dma_m2m_start:
    @ r0=src r1=dst r2=len lr=NS 복귀. scratch 는 r3, r12 만. 여기까지 PSP_S = park
    mrs   r3, ipsr
    cbnz  r3, rej_park                @ handler mode 호출 거부
    movw  r12, #:lower16:g_pxNsSecureCtx
    movt  r12, #:upper16:g_pxNsSecureCtx
    ldr   r12, [r12]
    ldr   r12, [r12]                  @ r12 = xSecureContext (현재 Task, NS 읽기)
    movw  r3, #:lower16:GUARD_A_S
    movt  r3, #:upper16:GUARD_A_S
    cmp   r12, r3
    beq   1f
    add   r3, r3, #GUARD_STRIDE
    cmp   r12, r3
    bne   rej_park
    movw  r3, #:lower16:(g_secstack_slot+4)
    movt  r3, #:upper16:(g_secstack_slot+4)
    b     2f
1:  movw  r3, #:lower16:g_secstack_slot
    movt  r3, #:upper16:g_secstack_slot
2:  ldr   r3, [r3]
    cbnz  r3, rej_park                @ 이 guard 에 정지 프레임이 있음 → 거부
    add   r3, r12, #SECSTACK_REST
    msr   psp, r3                     @ PSP 먼저
    msr   psplim, r12                 @ 그다음 limit = G
    @ ---- 이후 예외 프레임은 guard 에 쌓인다 ----
    cbz   r2, rej_guard
    subs  r3, r2, #1
    adds  r3, r3, r1                  @ end, C = overflow
    bcs   rej_guard
    ttat  r12, r1
    ttat  r3, r3
    cmp   r12, r3
    bne   rej_guard
    tst   r3, #(1 << 19)              @ NS-MPU 쓰기 가능
    beq   rej_guard
    subs  r3, r2, #1
    add   r3, r3, r1
    ttt   r12, r1
    ttt   r3, r3
    cmp   r12, r3
    bne   rej_guard
    tst   r12, #(1 << 16)             @ MRVALID (신규, gap-analysis 3번)
    beq   rej_guard
    tst   r12, #(1 << 21)             @ 기존 verify_inst 와 같은 판정
    beq   rej_guard
    push  {r4}                        @ STREXB 상태용 (r4 는 NS 값이라 보존)
    movw  r3, #:lower16:g_dma_busy
    movt  r3, #:upper16:g_dma_busy
    ldrexb r12, [r3]
    cbnz  r12, rej_pop                @ (clrex 는 rej_pop 에서)
    movs  r12, #1
    strexb r4, r12, [r3]
    cbnz  r4, rej_pop                 @ 선점되어 실패 → 이번 요청 거부 (재시도 루프로 바꿔도 됨)
    movw  r3, #:lower16:DMA1_BASE_S_ASM
    movt  r3, #:upper16:DMA1_BASE_S_ASM
    ldr   r12, [r3, #8]
    bic   r12, r12, #1
    str   r12, [r3, #8]               @ CCR1.EN = 0
    str   r0, [r3, #0x10]             @ CPAR1  = src
    str   r1, [r3, #0x14]             @ CM0AR1 = dst
    str   r2, [r3, #0x0C]             @ CNDTR1 = len
    movs  r12, #1
    str   r12, [r3, #4]               @ IFCR = CGIF1
    ldr   r12, [r3, #8]
    orr   r12, r12, #1
    str   r12, [r3, #8]               @ CCR1.EN = 1
    movs  r0, #0
    pop   {r4}
    b     to_park
rej_pop:
    clrex
    pop   {r4}
rej_guard:
    mvn   r0, #0
to_park:
    movs  r12, #0
    msr   psplim, r12                 @ PSPLIM=0 먼저
    movw  r12, #:lower16:(g_secstack_park+SECSTACK_REST)
    movt  r12, #:upper16:(g_secstack_park+SECSTACK_REST)
    msr   psp, r12
    b     clear_ret
rej_park:
    mvn   r0, #0
clear_ret:
    mov   r1, lr
    mov   r2, lr
    mov   r3, lr
    mov   r12, lr
    msr   APSR_nzcvqg, lr
    bxns  lr
    .size SECURE_dma_m2m_start, . - SECURE_dma_m2m_start
#endif
```
- `cbz/cbnz` 은 앞쪽으로만, 짧은 거리(126 B 이내)만 간다. 범위를 넘으면 어셈블러가 에러를 내므로 `cmp + bne` 로 바꾼다.
- `DMA1_BASE_S_ASM`, `GUARD_A_S` 는 §2 헤더에 접미사 없는 숫자로 둔다.
- BENCH_TS 의 T1/T4 는 `#if defined(BENCH_TS)` 안에서 `DWT->CYCCNT` (0xE0001004) 를 읽어 `g_benchT1/T4` 에 쓴다. 처음과 `movs r0,#0` 직전에 r3/r12 만 사용.
- `secure_nsc.c`: 기존 C `SECURE_dma_m2m_start` 를 `#if !SECSTACK_STATIC` 으로 감싼다. `s_busy` 는 `g_dma_busy` 로 이름을 바꾸고
  외부 링크로 만든다 (`static` 제거). DMA ISR 과 init 도 같은 변수를 쓴다. `verify_inst` 는 bench 용으로 그대로 둔다.
- 여기서 레지스터 r4–r11 은 `push {r4}`/`pop` 말고는 건드리지 않는다. FP 레지스터는 전혀 쓰지 않는다 → FP 정리 불필요 [아키텍처 도출].

**효과.** stub 안에 FP 명령 0, 쌓는 것은 r4 하나(4 B)뿐. 함수 프레임도 없다. guard 사용 최대 ≈ 4 + 72 (FPCA=0) 이고,
NS 가 FP 를 써서 FPCA=1 이어도 ≈ 4 + 208 + 4 ≤ 248 [아키텍처 도출].

**확인.**
1. `arm-none-eabi-objdump -d -j .gnu.sgstubs Secure.elf | grep -B1 -A2 dma_m2m_start` → `sg` 다음 `b.w <__acle_se_SECURE_dma_m2m_start>`.
   안 생기면: 두 심볼이 같은 주소·`%function`·global 인지 `nm` 으로 확인한다 (GNU ld 의 수동 `__acle_se_` 처리는 [미확인]).
2. `objdump --disassemble=__acle_se_SECURE_dma_m2m_start | grep -cE "vmov|vmsr|vmrs|vpush|vstm"` → 0.
3. NS 쪽 `secure_nsclib.o` 에 `SECURE_dma_m2m_start` 가 그대로 있다 (NS 코드는 안 바꿈).
4. 보드: `b DMA1_Channel1_IRQHandler` → `p/x $psp` 가 `GUARD_A_S..+248` 안, `x/wx $psp+24` 의 stacked PC 가 stub 안,
   `p/x $lr` 의 FType(bit4) = 1 (기본 프레임).

**연계.** 커널이 넣는 `xSecureContext` (§7), slot (§4), park (§3), DMA ISR 의 `g_dma_busy = 0`, GTZC 핸들러 (변경 없음).

---

## 6. 항목 E: NS PendSV 분기 (`portasm.c`, `configENABLE_MPU == 1` 쪽만)

**의도.** guard Task 는 Secure 안에서 선점됐을 때(EXC_RETURN.S=1)만 NSC 를 부른다.

**봐야 할 코드.** `PendSV_Handler` 의 `cbz r0, save_ns_context` / `save_s_context:` / `push {r0-r2, lr}` 와
복원 쪽의 `ldmdb r2!, {r0, r3-r5, lr}` → `str r0, [r4]` → `cbz r0, restore_ns_context` / `restore_s_context:`.
- 나가는 쪽: lr = PendSV 진입 EXC_RETURN = **나가는 Task** 의 값 [코드].
- 들어오는 쪽: `ldmdb` 가 lr 에 **들어오는 Task** 의 EXC_RETURN 을 실은 뒤에 분기한다 [코드]. guardasstack 3.6 의 확인 사항은 해결됐다.

**바꿀 내용.**
```
 " cbz r0, save_ns_context                \n"
#if SECSTACK_STATIC
 " cmp r0, #8                             \n"   /* SECSTACK_MAX_STOCK_HANDLE */
 " bls save_s_context                     \n"   /* stock handle */
 " tst lr, #0x40                          \n"   /* S: Secure 스택에 프레임? */
 " beq save_ns_context                    \n"   /* guard형 + NS 에서 선점 → NSC 0회 */
 " push {r0-r2, lr}                       \n"
 " bl SecureStack_Suspend                 \n"
 " pop {r0-r2, lr}                        \n"
 " b save_ns_context                      \n"
#endif
 " save_s_context: ... (기존)
```
들어오는 쪽도 같은 모양으로 넣는다 (`cbz r0, restore_ns_context` 다음, `push {r1-r3, lr}; bl SecureStack_Resume; pop {r1-r3, lr}; b restore_ns_context`).
`.extern SecureStack_Suspend/Resume` 을 추가하고, `#include "secstack_cfg.h"` 를 넣어 숫자 8 대신 매크로를 stringify 해서 쓴다
(CLAUDE.md: `DMA_STR/DMA_STR_` 이중 간접). non-MPU 분기와 IAR 포트는 건드리지 않는다.

**효과.** 경우 ①: 기존 대비 NSC 2회 → 0회. 추가 비용은 `cmp/bls/tst/beq` 4개 [추정].

**확인.**
- `objdump --disassemble=PendSV_Handler NonSecure.elf` 에서 분기 위치를 확인한다.
- 보드: `b SecureStack_Suspend` 는 평상시 거의 걸리지 않아야 한다. 중재자 안에서 `set $pc` 없이 breakpoint 로 멈췄다가
  `continue` 해서 tick 이 선점하게 하면 걸린다 (§9.3).

**연계.** Suspend/Resume (§4), stock Save/Load (변경 없음), `xSecureContext` 값 (§7).

---

## 7. 항목 F: NS 커널 바인딩 (`port.c`, `portmacrocommon.h`)

**의도.** 빌드 시점이 아니라 **스케줄러 시작 전에 한 번** DMA Task 의 `xSecureContext` 칸에 guard 주소를 넣는다.

**봐야 할 코드.**
- `pxPortInitialiseStack` (MPU 쪽): `ulContext[...] = portNO_SECURE_CONTEXT; /* xSecureContext. */` 다음에 PSP, PSPLIM, CONTROL, EXC_RETURN 이 온다.
- `vPortFreeSecureContext`: `ldr r2,[r0]` (TCB 첫 멤버 = 저장 위치) → `ldr r1,[r2, SECURE_CONTEXT_OFFSET]`.
  `portasm.c:50-55` 에서 FPU+MPU, PAC 없음이면 −20 → 저장 위치에서 5 워드 아래가 xSecureContext 칸 [코드].
- `vPortSVCHandler_C`: `portSVC_ALLOCATE_SECURE_CONTEXT`, `portSVC_START_SCHEDULER` (`SecureContext_Init()` 호출 위치).

**바꿀 내용.**
- `void vPortBindSecureStack(TaskHandle_t xTask, uint32_t ulGuardS)` (특권, 스케줄러 전용):
  `uint32_t *p = *(uint32_t **)xTask; p[-5] = ulGuardS;` 위 오프셋 근거를 주석으로 달고, `SECURE_CONTEXT_OFFSET` 과 일치하는지 확인한다.
  선언은 `portmacrocommon.h` 의 `vPortAllocateSecureContext` 근처.
- ALLOCATE: `if (xSecureContext > SECSTACK_MAX_STOCK_HANDLE) break;` 를 맨 앞에 둔다 (guard형 재할당 차단).
- START_SCHEDULER: `SecureContext_Init();` 다음에 `SecureStack_Init(&xSecureContext);`
  (NS `.bss` 초기화와 Secure MPCBB 설정이 모두 끝난 시점이라 seal 기록이 안전 [코드: 호출 순서]).
- `portCLEAN_UP_TCB` / Free: 수정하지 않는다 (§2: Secure 가 1..8 아닌 값은 무시). Task 삭제 시 SVC 1회는 남는다.

**확인.** NS `.map` 에 `vPortBindSecureStack`. 보드: DMA Task 실행 중 `p/x xSecureContext` = `0x3001b700` (EVAL_MEM A guard) 같은 guard alias.

**연계.** PendSV(§6) 가 이 값을 읽는다. stub(§5) 이 `g_pxNsSecureCtx` 로 읽는다. Free 경로.

---

## 8. 항목 G: 앱 (`app_freertos.c`)과 빌드 파일

**봐야 할 코드.** `vCreateAppTasks` 의 `xTaskCreateRestricted( &xDmaTaskAParams, &hDmaTaskA );`,
`vDmaTaskA/B` 첫 줄 `portALLOCATE_SECURE_CONTEXT`, `dma_task_mem_t` 의 `guard[]` 와 `_Static_assert` (256 B 정렬).

**바꿀 내용.**
- 생성 직후: `vPortBindSecureStack(hDmaTaskA, (uint32_t)s_dmaTaskMem[DMA_TASK_A].guard + 0x10000000u);` (B 도 같음).
  NS 는 실제 심볼 주소를 쓰고 Secure 는 자기 상수와 비교한다. 배치가 바뀌면 stub 이 거부하므로 어긋남이 바로 드러난다.
- DMA Task 의 `portALLOCATE_SECURE_CONTEXT` 를 `#if !SECSTACK_STATIC` 로 감싼다. Plain Task 는 그대로 둔다.
- `MEDIATOR_INSECURE` 빌드에서는 바인딩도 하지 않는다 (`#if SECSTACK_STATIC && !defined(MEDIATOR_INSECURE)`).
  Secure 쪽도 같은 조건으로 묶는다.
- 빌드 파일 (CLAUDE.md 규칙): Secure `Debug/Application/User/subdir.mk` 에 `secure_stack.c` 를 기존 C 항목과 같은 형식으로 추가한다.
  `.s` 는 `S_SRCS/OBJS/S_DEPS` 와 `Application/Startup/subdir.mk` 의 규칙(`-x assembler-with-cpp`)을 본떠 넣되
  `-I../../../Secure/Inc -I../../../Secure_nsclib` 를 더한다. 두 `.o` 를 `objects.list` 에도 추가한다.
  `scripts/eval/build_variant.sh` 가 매크로를 넘기는 방식을 보고 `SECSTACK_STATIC=0` 을 넘길 수 있는지 확인한다.

**확인.** `./build.sh clean && ./build.sh`, `check_layout.sh` (EVAL_MEM 주소 0x2001b500 그대로).

---

## 9. 확인 절차 모음

### 9.1 원본 동일성 (`SECSTACK_STATIC=0`)
```
git worktree add ../dma-tz-orig pre-secstack && (cd ../dma-tz-orig && ./build.sh)
# 현재 트리를 SECSTACK_STATIC=0 으로 빌드한 뒤
for s in Secure NonSecure; do
  arm-none-eabi-objcopy -O binary <orig>/*_$s.elf /tmp/.../o_$s.bin
  arm-none-eabi-objcopy -O binary <cur>/*_$s.elf  /tmp/.../c_$s.bin
  cmp o_$s.bin c_$s.bin && echo "$s same"
done
```
같지 않으면 `#if` 밖으로 샌 변경이 있다는 뜻이다.

### 9.2 정적 확인 (매 단계)
- veneer (§5-1), FP 명령 0 (§5-2), PendSV 분기 (§6), 심볼 (`nm`).

### 9.3 보드 (사용자 실행)
- 기대 동작: A/B 전송이 계속 완료된다 (10 초당 29–30회). Cluster 8 B = 00..07. Plain LED 정상. 공격 (a-1)(a-2) 의 `configASSERT` 통과, (b) 의 TZIC 중단 동작이 같다.
- 쉼 상태: `psp_s = g_secstack_park+248`, `psplim_s = 0`, `g_secstack_slot = {0,0}`.
- 경우 ③: `b SECURE_dma_m2m_start` 다음 몇 번 `stepi` 로 `msr psplim` 뒤까지 간다. 1 ms 넘게 멈췄다가 `continue` 하면
  tick 이 선점한다 (디버그 정지 중 SysTick 동작 여부는 [미확인]. 안 되면 `set var` 로 `SCB->ICSR` PENDSVSET 을 쓴다).
  `b SecureStack_Suspend` / `b SecureStack_Resume` 이 각각 1회 걸리고, 전송이 정상 완료되어야 한다.
- 음성: Tmr Svc 처럼 바인딩되지 않은 Task 에서 호출하면 −1. NS ISR 에서 호출하면 −1 (IPSR 검사).
- 메모리: guard 에 GDB 로 패턴을 칠하고 (`set {int}addr = 0xa5a5a5a5`, Secure alias), 60 초 뒤 high-water 를 본다. 기대 ≤ 80 B.
  DMA Task 의 Secure 힙 할당은 0 (`xFreeBytesRemaining` 이 2080 B 늘어남).

### 9.4 측정 (Phase 4, 기존 BENCH_PHASE 재사용)
전환 비용 ① (stock vs static), 요청 경로 outer/inner, 경우 ③ 비용, guard high-water. 조건은 기존 표와 같게 (P1/P2, 110/32 MHz, off/warm, N=1000).

---

## 10. 기록할 것
- `docs/gap-analysis.md`: "Secure 스택 static binding (논문 밖 확장)" 항목과 guardasstack.md 대비 변경 다섯 가지
  (인코딩 > 8, park 는 PSP 만, 중재자 전체 asm + MRVALID + LDREXB, 경우 ④ 의 PSPLIM=0 복원, slot 점유 검사).
- 단계마다 `docs/CHANGES.md` 1줄, 커밋.
- 미확인 목록: MSR PSP 의 limit 검사, SG 직후 예외 수용, GNU ld 의 수동 `__acle_se_`, ST-LINK 의 `psp_s` 레지스터 이름,
  디버그 정지 중 SysTick, FP 확장 프레임에서 s16–s31 을 쌓는 정확한 조건, 무결성 서명 bit0 의미.
