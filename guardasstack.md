# 작업 지시: DMA 태스크용 static-binding Secure 스택 (guard-as-stack, 지연 전환)

## 0. 먼저 지킬 것
- 이 레포의 `CLAUDE.md` 규칙을 그대로 따른다. 특히:
  - 수정 전에 이해한 내용을 먼저 보고하고, 승인을 받기 전에는 대규모 수정을 하지 않는다.
  - 논문(`docs/paper/paper.txt`)과 충돌하는 부분은 `docs/gap-analysis.md`에 기록한다.
  - 주장마다 출처 태그를 붙인다: [논문] / [아키텍처 도출] / [실측] / [추정].
  - 레지스터 동작에는 RM0438이나 Armv8-M ARM의 근거 절을 밝히고, 근거가 없으면 '미확인'으로 쓴다.
  - 변경한 뒤에는 매번 `./build.sh`로 빌드를 확인한다.
  - 커밋마다 `docs/CHANGES.md`에 한 줄을 추가한다.
  - 보드 실행은 불가능하므로, 확인 절차(기대 UART 출력, 레지스터 값, GDB 명령)를 적어 준다.
- 이 작업은 논문 §3을 넘어서는 **저자가 승인한 확장**이다. `docs/gap-analysis.md`에 "Secure 스택 static binding (논문 밖 확장)" 항목을 만들고 아래 설계 요약을 기록한 뒤 시작한다.
- 작업 브랜치: `impl/paper`에서 `impl/secstack`을 새로 만든다. `baseline`, `baseline-original` 태그는 건드리지 않는다. push하지 않는다.

## 1. 목표
현재 구조와 문제:
- DMA 태스크(DMA_Task_A, B)는 stock FreeRTOS Secure context(heap 1040 B)를 쓴다.
- 그래서 전환될 때마다 PendSV가 `SecureContext_SaveContext`와 `SecureContext_LoadContext`를 NSC로 두 번 호출한다.
  - [실측] context switch +184–544 cycle (`results/summary_phase45.md`, commit 983a749)
  - [실측] 빈 NSC 1회 154 cycle (P1, 110 MHz, cache off, `Mediator.md`)

목표:
- **DMA 태스크만** 자기 MPCBB guard 블록(256 B)을 Secure 스택으로 쓰도록 빌드 시점에 고정한다(static binding).
- 스택 설치는 `SECURE_dma_m2m_start` 진입 stub이 한다.
- PendSV는 그 태스크가 **Secure 안에서 선점된 경우(EXC_RETURN.S=1)에만** Suspend/Resume NSC를 부른다.
- 그 밖의 태스크(Plain task 등)의 stock heap context 동작과 비용은 그대로 둔다. Insecure 빌드에도 같은 비용이 있으므로 개선 대상이 아니다.

범위:
- 경우 ①: NS 실행 중 전환 → NSC 0회
- 경우 ②: 요청 → stub이 스택 설치와 park 수행
- 경우 ③: 중재자 본문 실행 중 선점 → Suspend 1회 + Resume 1회
- 경우 ④: SG 직후 또는 park 이후 틈에서 선점된 뒤 전환 → 아래 3.6의 처리만 구현하고 평가에서는 제외한다.

## 2. 먼저 읽고 보고할 것 (Phase 0, 수정 없음)
아래를 읽고 각 항목의 현재 동작을 파일, 함수, 해당 코드 패턴으로 보고한다. 행 번호는 바뀔 수 있으니 패턴으로 찾는다.

1. `Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33/non_secure/portasm.c`, `PendSV_Handler` (`configENABLE_MPU == 1` 분기)
   - `ldr r0, [xSecureContext]` 다음의 `cbz r0, save_ns_context`와 `bl SecureContext_SaveContext`
   - restore 쪽의 `str r0, [xSecureContext]` 다음 `cbz r0, restore_ns_context`와 `bl SecureContext_LoadContext`
   - `lsls r3, r3, #25` / `bmi`: EXC_RETURN bit 6(S)을 판정하는 부분
2. `.../non_secure/port.c`
   - `xSecureContext` 전역(`PRIVILEGED_DATA`)
   - `vPortSVCHandler_C`의 `portSVC_ALLOCATE_SECURE_CONTEXT`, `portSVC_FREE_SECURE_CONTEXT`, `portSVC_START_SCHEDULER`(`SecureContext_Init` 호출)
   - `pxPortInitialiseStack`이 xSecureContext 칸을 초기화하는 위치
   - `prvSetupFPU` → `SecureInit_EnableNSFPUAccess`(FPCCR.TS=1)
   - `vPortFreeSecureContext`가 xSecureContext를 읽는 오프셋(−20)
3. `.../non_secure/portmacrocommon.h`: `portALLOCATE_SECURE_CONTEXT`, `portCLEAN_UP_TCB`
4. `.../secure/secure_context.c`, `secure_context_port.c`
   - `securecontextNO_STACK`(0)
   - `SecureContext_Init`: 이 프로젝트는 CONTROL_S를 특권+PSP(0x02)로 고정하도록 수정되어 있다. 확인할 것.
   - `AllocateContext`와 `LoadContext`의 전제 조건 `PSPLIM == NO_STACK`
   - `SaveContextAsm`이 저장 후 PSP와 PSPLIM을 0으로 설정하는 부분, 지연된 FP stacking을 끝내는 `vstmdb/vldmia s0` 관용구
5. `project/Secure/Src/secure_nsc.c`: `SECURE_dma_m2m_start`, `verify_inst`, `s_busy`, DMA ISR(doorbell), `GTZC_IRQHandler`
6. `project/Secure/Src/main.c`: S-MPU 설정(`MPU->CTRL = PRIVDEFENA|ENABLE`), guard MPCBB 설정 위치(`SMPU_SetupTaskRegions`), guard 주소 상수
7. `project/NonSecure/Src/app_freertos.c`: `dma_task_mem_t` 배치(globals 256 + cluster 256 + guard 256, `aligned(256)`), 태스크 생성(`xTaskCreateRestricted`), `portALLOCATE_SECURE_CONTEXT` 호출 위치, `DMA_TASK_PRIV_BIT`, `BENCH_PHASE`
8. Secure 링커 스크립트의 `.gnu.sgstubs`, Secure makefile의 `--cmse-implib`/`--out-implib`

보고 형식: 표(항목 | 현재 동작 | 근거 위치 | 이 설계에서 바꿀 점). 그 다음 아래 3절 설계와 충돌하는 점이 있으면 지적한다. **여기서 멈추고 승인을 기다린다.**

## 3. 설계 명세

### 3.1 값과 배치
- **guard Secure alias**: guard는 SRAM1의 NS alias(0x2xxx_xxxx)에 있다. Secure 코드는 반드시 Secure alias(+0x1000_0000)로 접근한다. [아키텍처 도출: SAU상 NS 주소로 접근하면 NS 트랜잭션이 되어 MPCBB에서 막힘]
- **guard 내부 배치**: `[base+248 .. base+255]` seal 2 word(0xFEF5EDA5 ×2), rest top = base+248, PSPLIM_S = base. 사용 가능 248 B.
- **park 블록 P**: Secure 정적 SRAM에 256 B, `aligned(256)` 하나. 배치는 guard와 같다(seal 위쪽, rest top = P+248).
- **xSecureContext 인코딩**:
  - 0 = context 없음
  - bit 0 = 0, 0이 아님 = stock heap handle (기존 그대로)
  - bit 0 = 1 = guard형, 값 = guard Secure alias base | 1
  - 먼저 stock handle의 bit 0이 항상 0인지 확인하고 보고한다. 아니면 다른 구분 방법을 제안한다.
- **slot**: Secure 정적 배열 `s_slot[N_GUARD]`. 인덱스는 `(base − GUARD_LO) >> 8`. guard 꼬리에 두지 않는다(seal이 rest top 바로 위에 있어야 하므로).

### 3.2 Secure: stock 수정 (heap context 동작과 비용은 바꾸지 않음)
- `SecureContext_Init`: PSP_S와 PSPLIM_S를 0 대신 park로 설정(P+248, P). CONTROL_S는 지금처럼 0x02.
- `SaveContextAsm`: 저장한 뒤 PSP=0, PSPLIM=0 대신 PSPLIM=0 → PSP=P+248 → PSPLIM=P 순서로 설정.
- `LoadContext`, `AllocateContext`의 전제 조건 `PSPLIM == NO_STACK`을 `PSPLIM == park base`로 변경.
- 위 세 곳 외의 stock 로직은 바꾸지 않는다.

### 3.3 Secure: 신규 `secure_stack.c`
- `SecureStack_Init(volatile uint32_t *pxNsSecureCtx)` (NSC)
  - IPSR ≠ 0이고 처음 호출일 때만 수행. `cmse_check_address_range`로 NS 주소인지 확인한 뒤 저장.
  - 각 guard와 P의 맨 위에 seal을 기록한다. MPCBB 설정과 NS `.bss` 초기화가 끝난 뒤여야 한다.
  - 마지막으로 park로 설정한다.
  - 기존 `SecureContext_Init`과 합칠지 따로 둘지는 Phase 0 보고에서 제안한다.
- `SecureStack_Suspend(uint32_t ctx)` (NSC, NS PendSV에서만 호출)
  - IPSR = 14 확인. `ctx`가 guard형이 아니면 park로 설정하고 반환.
  - `sp = PSP_S`. sp가 [base, base+248) 안이면 `s_slot[i] = sp`.
  - 밖이면 경우 ④다. 매크로 `SECSTACK_WINDOW_RELOCATE`(기본 1)일 때 다음을 수행한다.
    1. stock 관용구로 지연된 FP stacking을 끝낸다.
    2. `[sp, (sp & ~0xFF) + 248)` 구간을 `base + 248 − n` 위치로 word 단위 복사한다.
    3. slot에 새 sp를 기록한다.
  - 매크로가 0이면 slot에 0을 넣는다. 그러면 재개할 때 fault로 닫힌다.
  - 마지막으로 park로 설정한다 (PSPLIM=0 → PSP=P+248 → PSPLIM=P).
- `SecureStack_Resume(uint32_t ctx)` (NSC, NS PendSV에서만 호출)
  - IPSR = 14 확인. guard형이고 slot ≠ 0이면 PSPLIM=0 → PSP=slot → PSPLIM=base.
  - 아니면 PSP=0, PSPLIM=0 (fail closed).

### 3.4 Secure: `SECURE_dma_m2m_start` 진입 stub (어셈블리)
- 기존 C 함수를 `dma_start_body`로 이름을 바꾸고 `CMSE_NS_ENTRY`를 제거한다. 본문(`verify_inst` 포함)은 그대로 둔다.
- NS 쪽 이름(`SECURE_dma_m2m_start`)과 시그니처는 그대로 유지한다. NS 코드는 바꾸지 않는다.
- 새 파일 `secure_dma_entry.S`에 다음을 둔다.
  - `SECURE_dma_m2m_start`와 `__acle_se_SECURE_dma_m2m_start`를 같은 주소의 전역 함수 심볼로 정의한다.
  - 링커가 `.gnu.sgstubs`에 `SG; B.W` veneer를 만드는지 map 파일과 `arm-none-eabi-objdump -d -j .gnu.sgstubs`로 확인하고 보고한다.
  - 새 `.S`를 Debug 빌드 파일(`subdir.mk`, `objects.list`)에 추가한다(CLAUDE.md 빌드 규칙).
- 흐름:
  1. SG 직후 진입 틈: 스택을 쓰지 않는다. scratch는 **r3과 r12만** 쓴다(인자는 r0–r2이므로 r3이 비어 있음).
     - `mrs r3, ipsr`가 0이 아니면 reject (handler mode 요청 금지).
     - `r12 = *g_pxNsSecureCtx`(xSecureContext 값).
     - bit 0이 1이 아니면 reject. bit 0을 지워 base를 얻는다.
     - base가 256 B 정렬이고 [GUARD_LO, GUARD_HI) 안인지 확인. 아니면 reject.
     - `msr psplim, 0` → `msr psp, base+248` → `msr psplim, base`.
     - 상수는 literal pool 대신 `movw/movt`를 우선 사용한다(Flash 데이터 읽기 감소).
  2. `push {r4, lr}` → `bl dma_start_body` → `pop {r4, lr}`
  3. park: `msr psplim, 0` → `msr psp, P+248` → `msr psplim, P`
  4. CMSE 복귀 정리: r1, r2, r3, r12를 lr 값으로 덮고 `msr APSR_nzcvq, lr` → `bxns lr`. r0은 반환값.
  5. reject: r0 = −1, PSP_S는 건드리지 않고 4로.
- **FP 처리**:
  - `dma_start_body`와 그 호출 체인에 FP 명령이 없음을 objdump로 확인한다.
  - GCC 14.3.1에서 `-mgeneral-regs-only`나 `target("general-regs-only")`를 쓸 수 있는지 확인하고, 되면 그 파일에만 적용한다.
  - 쓸 수 없으면 stub 복귀 전에 CONTROL_S.SFPA를 확인해, 1이면 GCC의 cmse epilogue와 같은 방식으로 s0–s15와 FPSCR을 정리한다.
  - 어느 방식을 택했는지 보고한다.

### 3.5 중재자 본문의 원자성 (선점을 허용하므로 필요)
- `s_busy` 검사·설정을 LDREX/STREX(`__LDREXW/__STREXW`)로 바꾼다. 인터럽트 마스킹 없이 채널 소유권을 획득한다.
- 채널 레지스터는 소유권을 얻은 뒤에만 쓴다. 요청 사본(dst, len)이 있으면 enable 전에 DMB를 둔다.
- 검증(TTAT/TTT)은 원자성이 필요 없다. [아키텍처 도출: 입력은 레지스터, 호출 태스크의 NS-MPU(커널이 재개 전에 복원), S-MPU(정적)뿐이므로 선점되어도 결과가 유지됨]

### 3.6 NS: PendSV 분기 (`portasm.c`, MPU 분기만)
```
나가는 쪽:
  ldr r0,[xSecureContext] ; cbz r0, save_ns_context
  tst r0,#1 ; beq stock_save
  tst lr,#0x40 ; beq save_ns_context              @ guard형이고 S=0이면 NSC 없음
  push {r0-r2,lr} ; bl SecureStack_Suspend ; pop {r0-r2,lr} ; b save_ns_context
stock_save: (기존 SaveContext 호출 그대로)
들어오는 쪽:
  (xSecureContext 복원 뒤) cbz r0, restore_ns_context
  tst r0,#1 ; beq stock_load
  tst lr,#0x40 ; beq restore_ns_context
  push {r1-r3,lr} ; bl SecureStack_Resume ; pop {r1-r3,lr} ; b restore_ns_context
stock_load: (기존 LoadContext 호출 그대로)
```
- 들어오는 쪽에서 `lr`에 들어오는 태스크의 EXC_RETURN이 실려 있는 시점인지 확인하고 보고한다.
- 레지스터 보존이 기존 push/pop 패턴과 맞는지도 확인한다.
- non-MPU 분기와 다른 툴체인 포트(IAR 등)는 수정하지 않는다.

### 3.7 NS: 바인딩 API와 정리
- `port.c`에 `void vPortBindSecureStack(TaskHandle_t xTask, uint32_t ulGuardSecureBase)`를 추가한다.
  - 특권 전용, 스케줄러 시작 전에만 호출.
  - `vPortFreeSecureContext`가 읽는 위치(저장 context 끝에서 −20)에 `base | 1`을 쓴다.
  - 오프셋 근거를 보고한다.
- SVC `portSVC_ALLOCATE_SECURE_CONTEXT`: 현재 xSecureContext가 guard형이면 무시한다(재바인딩 차단).
- `portSVC_START_SCHEDULER`: `SecureContext_Init` 뒤에 `SecureStack_Init(&xSecureContext)`를 호출한다(3.3에서 합치기로 했다면 그 방식으로).
- `vPortFreeSecureContext` 또는 `portCLEAN_UP_TCB`: guard형이면 해제하지 않는다.
- 앱(`app_freertos.c`):
  - DMA 태스크 생성 직후 `vPortBindSecureStack(hDmaTaskA, GUARD_A_S)`처럼 바인딩한다.
  - DMA 태스크 본문의 `portALLOCATE_SECURE_CONTEXT`는 `#if` 매크로(`SECSTACK_STATIC`, 기본 1)로 제거한다.
  - Plain task는 그대로 둔다.
  - guard Secure 주소는 Secure `main.c`의 상수와 같은 값을 공유 헤더 한 곳에 둔다. 하드코딩이 두 군데로 갈라지지 않게 한다.
  - `MEDIATOR_INSECURE` 빌드는 영향이 없어야 한다.

### 3.8 규칙
- DMA 태스크는 `SECURE_dma_m2m_start` 외의 stock cmse 진입 함수를 호출하지 않는다. 현재 코드에서 이를 어기는 곳이 있는지 찾아 보고한다.
- heap context 태스크가 `SECURE_dma_m2m_start`를 호출하면 stub이 거부한다.

## 4. 단계와 커밋
| Phase | 내용 | 완료 조건 |
|---|---|---|
| 0 | 2절 보고 | 승인 |
| 1 | 3.2 stock 수정 + 3.3 `secure_stack.c` | 빌드 통과. heap context 동작이 같음을 diff로 설명 |
| 2 | 3.4 stub + 본문 이름 변경 + 3.5 원자성 | 빌드 통과, veneer 생성 확인, body에 FP 명령 없음 확인 |
| 3 | 3.6 PendSV + 3.7 바인딩 | 빌드 통과. NS·Secure 양쪽 map에서 심볼 확인 |
| 4 | 측정 코드 (5절) | `BENCH_PHASE` 빌드 통과 |

Phase마다 커밋하고 `docs/CHANGES.md`에 한 줄을 추가한다. 각 Phase가 끝나면 diff 요약과 다음 Phase 계획을 보고하고 승인을 기다린다.

## 5. 측정 준비 (기존 bench 틀 재사용)
- 기존 측정 harness(`BENCH_PHASE`, commit 983a749의 phase4/5 context switch 측정 방식)를 찾아 재사용한다.
- 추가할 측정:
  1. DMA 태스크 전환 비용 (경우 ①): stock context 빌드와 static 빌드 비교
  2. 요청 경로 outer/inner (stub 포함)
  3. 경우 ③: 테스트 빌드에서 `dma_start_body` 안에서 `SCB->ICSR = PENDSVSET`(NS PendSV pend 방법은 확인 후 사용)으로 선점을 유도하고 Suspend+Resume 경로 비용을 잰다
  4. Secure heap high-water, guard 스택 high-water (guard를 패턴으로 채워 측정)
- 측정 조건은 기존 표와 같게 한다(P1/P2, 110/32 MHz, cache off/warm, N=1000).

## 6. 보드 확인 절차를 함께 작성
- **기대 동작**: DMA 태스크 A, B가 계속 완료(10 s당 29–30회), Cluster 내용 00..07, Plain task LED 토글 정상.
- **GDB**:
  - `xSecureContext` 값 (DMA 태스크 실행 중이면 guard | 1)
  - Secure 쪽 `psp_s`, `psplim_s` (쉬는 상태면 park)
  - guard 맨 위 seal 값
  - `s_slot[]`
  - 중재자 안에 breakpoint를 걸고 강제 선점한 뒤 재개했을 때 정상 복귀하는지
- **음성 확인**:
  - context 없는 태스크(테스트용)가 `SECURE_dma_m2m_start`를 호출하면 −1 반환
  - NS ISR에서 호출하면 −1 반환

## 7. 하지 말 것
- stock heap context의 Save/Load/Allocate/Free 로직을 3.2 이외의 방식으로 바꾸지 않는다.
- NS-MPU 영역을 추가로 쓰지 않는다. MPCBB를 동적으로 재설정하지 않는다.
- 경우 ④의 성능을 최적화하지 않는다(평가 범위 밖).
- 불확실한 아키텍처 동작은 추측으로 구현하지 않는다. 아래 항목은 `docs/gap-analysis.md`에 '미확인'으로 남기고 질문한다.
  - MSR PSP에 PSPLIM보다 낮은 값을 쓸 때의 limit 검사 여부
  - SG 직후 예외 수용 여부
  - GNU ld에서 `__acle_se_` 수동 심볼 처리
