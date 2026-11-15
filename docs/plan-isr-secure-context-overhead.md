# Plan: Secure DMA mediator vs Insecure DMA channel — ISR and secure-context overhead

## Context
Existing bench (`bench_ns.inc` / `bench_s.inc`, IDs 1–8) measures only the *request* side of the
Secure mediator. Two costs are still unmeasured, and both disappear if a task drives an NS DMA channel directly:
1. **ISR overhead**: completion goes NS thread → Secure `DMA1_Channel1_IRQHandler` → BLXNS → NS
   callback → FNC_RETURN → Secure exception return. Baseline: NS handler → callback.
2. **Secure-context overhead**: every task calling the mediator needs `portALLOCATE_SECURE_CONTEXT`
   (secure stack 1024 B + 8 B seal + secure heap header) and PendSV adds the NSC calls
   `SecureContext_SaveContext/LoadContext` on every switch of that task.

The goal is a set of numbers that can sit in the same report as Mediator.md/cmpTable.md: same harness, same
SRAM2 tables, same `run_bench.py` flow, BENCH_N=1000, warm cache.

## Baseline "insecure channel" (BENCH_PHASE only)
DMA1_Channel2 is released to NS: `SECM=0`, IRQ targeted to NS. Same controller, same 8 B M2M,
same src/dst (BENCH_SRC → Task A cluster). Both are NS transactions (CH1 has SSEC/DSEC=0), so the
transfer time is equal [추정]. RM0438 DMA “secure/non-secure channel” section: section number 미확인.
The default build is unchanged, so all channels stay Secure as the paper requires.

## Code changes (English comments, ≤3 lines each, no new macros except the IDs)

### 1. `project/Secure/Src/main.c`: after `Secure_DMA_LockAllChannels()`
```c
#if defined(BENCH_PHASE)
  /* Bench baseline only: DMA1 CH2 left non-secure, its IRQ routed to NS. */
  DMA1_Channel2->CCR &= ~DMA_CCR_SECM;
  NVIC_SetTargetState(DMA1_Channel2_IRQn);
#endif
```

### 2. `project/NonSecure/Src/bench_ns.inc`
- Extend the header in place (SRAM2, no .bss growth → `check_layout.sh` still 0x2001b600):
  `typedef struct { volatile uint32_t magic, id, n, tcb, alloc; } bench_hdr_t;`
- New IDs: `ISR_CALIB 9`, `ISR_NS 10`, `ISR_S 11`, `START_NS 12`, `CTX_NS 13`, `CTX_S 14`.
- Callback, NS handler, direct start:
```c
/* Completion callback for both paths: stamps entry time, releases the waiting task. */
static void bench_dma_cb(uint32_t st)
{ (void)st; BENCH_HDR->tcb = DWT->CYCCNT; s_dmaTaskMem[DMA_TASK_A].globals.v.dmaDone = 1u; }

/* NS-channel ISR: same body as the Secure DMA1_Channel1_IRQHandler, callback called directly. */
void DMA1_Channel2_IRQHandler(void)
{
    uint32_t isr = DMA1->ISR;
    if (!(isr & (DMA_ISR_TEIF2 | DMA_ISR_TCIF2))) { DMA1->IFCR = DMA_IFCR_CGIF2; return; }
    DMA1_Channel2->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF2;
    bench_dma_cb((isr & DMA_ISR_TEIF2) != 0u);
}

/* Insecure baseline start: same register sequence as the mediator, no verification. */
__attribute__((noinline)) static void ns_dma_start(void *src, void *dst, uint32_t len)
{ DMA1_Channel2->CCR &= ~DMA_CCR_EN; DMA1_Channel2->CPAR = (uint32_t)src;
  DMA1_Channel2->CM0AR = (uint32_t)dst; DMA1_Channel2->CNDTR = len;
  DMA1->IFCR = DMA_IFCR_CGIF2; DMA1_Channel2->CCR |= DMA_CCR_EN; }
```
- ISR sample. The transfer completes while PRIMASK_NS is set, so the IRQ is taken exactly at `cpsie`.
  AIRCR.PRIS=0 (`partition_stm32l562xx.h:305`) means PRIMASK_NS also masks the Secure IRQ (priority 5)
  [아키텍처 도출]. Both IRQs use priority 5.
```c
/* path 0 = calib (no IRQ), 1 = NS channel, 2 = Secure mediator.
 * col0 = cpsie -> task sees dmaDone, col1 = cpsie -> NS callback entry. */
static void bench_isr_once(int path, volatile uint8_t *dst, volatile uint32_t *row)
{
    volatile uint8_t *done = &s_dmaTaskMem[DMA_TASK_A].globals.v.dmaDone;
    dst[BENCH_LEN - 1u] = (path == 0); *done = (path == 0);
    __disable_irq();
    if (path == 2) { (void)SECURE_dma_m2m_start(BENCH_SRC, (void *)dst, BENCH_LEN); }
    else if (path == 1) { ns_dma_start(BENCH_SRC, (void *)dst, BENCH_LEN); }
    while (dst[BENCH_LEN - 1u] == 0u) { }          /* transfer done, TC pending */
    __DSB(); __ISB();
    uint32_t t0 = DWT->CYCCNT;
    __enable_irq();
    while (!*done) { }
    row[0] = DWT->CYCCNT - t0;
    row[1] = BENCH_HDR->tcb - t0;
}
```
- Context-switch partner and sample:
```c
/* Yield partner. sec != NULL: owns a secure context, so PendSV adds SecureContext_Save/Load.
 * Allocation cost is recorded once in BENCH_HDR->alloc. */
static void bench_partner(void *sec)
{
    if (sec) { uint32_t t0 = DWT->CYCCNT;
               portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);
               BENCH_HDR->alloc = DWT->CYCCNT - t0; }
    for (;;) { taskYIELD(); }
}

/* Round trip A -> partner -> A (two PendSV). A owns a secure context in both cases. */
static void bench_ctx(TaskHandle_t p, uint32_t id)
{
    vTaskResume(p); taskYIELD();                   /* warm-up; partner allocates here */
    for (uint32_t i = 0; i < BENCH_N; i++) {
        __DSB(); __ISB(); uint32_t t0 = DWT->CYCCNT;
        taskYIELD();
        BENCH_ROW(i)[0] = DWT->CYCCNT - t0; BENCH_ROW(i)[1] = BENCH_HDR->alloc;
    }
    vTaskSuspend(p); bench_table_done(id);
}
```
- `Bench_Main` tail (before `BENCH_ID_END`): channel init + the loops, in the existing style:
```c
DMA1_Channel2->CCR = DMA_CCR_MEM2MEM | DMA_CCR_MINC | DMA_CCR_PINC | DMA_CCR_PL_1 | DMA_CCR_TCIE | DMA_CCR_TEIE;
NVIC_SetPriority(DMA1_Channel2_IRQn, 5); NVIC_EnableIRQ(DMA1_Channel2_IRQn);
SECURE_dma_m2m_init((void *)bench_dma_cb);         /* Secure ISR now calls bench_dma_cb too */
/* ISR_CALIB / ISR_NS / ISR_S: warm-up once, then BENCH_N x bench_isr_once(path, dst, BENCH_ROW(i)) */
/* START_NS: BENCH_MEASURE_NS(a, ns_dma_start(...)) under __disable_irq, IRQ drained after */
/* CTX: xTaskCreate(bench_partner, "Pn"/"Ps", 128, NULL/(void*)1, (tskIDLE_PRIORITY+1)|portPRIVILEGE_BIT, &h),
 *      suspend both, then bench_ctx(hNs, CTX_NS); bench_ctx(hS, CTX_S); */
```
Partners are created dynamically from the existing 8 KB NS heap, so the .bss layout does not change. They are privileged
(DWT and SRAM2 access, same as Task A in BENCH_PHASE). 2 of 8 `secureconfigMAX_SECURE_CONTEXTS` are used
(A, Ps). Task B and Plain are suspended before they run, so they never allocate a context.

### 3. `scripts/eval/run_bench.py`
- `NAMES` adds 9–14: `isr_calib`, `isr_ns(total, to_cb)`, `isr_s(total, to_cb)`, `start_ns`,
  `ctx_ns`, `ctx_s(rt, alloc)`.
- Stack (no firmware code): at the END event, use `arm-none-eabi-nm` on the Secure ELF to find
  `xSecureContexts` and `xFreeBytesRemaining`. For each used context, write limit, start and high-water
  (scan up from pucStackLimit to the first non-zero word; the secure heap is .bss and starts zeroed) to `secure_ctx.txt`.

## What each result means
| Quantity | Formula |
|---|---|
| ISR, Secure path (net) | `isr_s.total − isr_calib.total` |
| ISR, NS channel (net) | `isr_ns.total − isr_calib.total` |
| **ISR overhead** | `isr_s − isr_ns` (calib cancels). Split: `to_cb` delta = entry + BLXNS, rest = FNC_RETURN + exit |
| Start, insecure | `start_ns − ns_calib`; compare with mediator `START col0 − ns_calib` |
| **Per-switch secure-ctx cost** | `ctx_s − ctx_ns` = SaveContext + LoadContext of one task (2 NSC calls in PendSV) |
| Alloc (one-shot) | `ctx_s col1` (SVC + NSC + secure pvPortMalloc) |
| **Stack overhead** | allocated = 1024 + 8 seal + heap header (from `xFreeBytesRemaining` drop); used = high-water of A (mediator path) vs Ps (context only) |

Secure ISR stack: the extra Secure MSP usage (handler frame + 8 B BLXNS return frame) is read from the
prologue in `*_Secure.list` [코드]. It is not instrumented.

## Known biases (written into the report)
- The TC flag may be pended a few cycles after the last byte becomes visible. This is the same for both paths [추정].
- A tick can land inside a sample, so the report uses min/median.
- Secure is -O0 under P1 and -O1 under P2. The ISR bodies are equivalent C. The Secure handler additionally writes `s_busy`
  and checks `s_ns_callback` (a few instructions).

## Extension: release the secure context after DMA completion

### Why it can help [아키텍처 도출, 코드 근거]
- The secure context, meaning a PSP_S stack owned by the task, is needed only while the task runs inside the mediator.
  The Secure DMA ISR runs on MSP_S (handler mode), not on the task's PSP_S.
  After the NSC call returns, the task's secure stack is empty, but PendSV still runs
  `SecureContext_SaveContext/LoadContext` (portasm.c:291-300, 412) on every switch-out and switch-in of the task.
- If the task holds no context (`xSecureContext == 0`), PendSV skips both calls (`cbz r0`), so a switch costs the
  same as `ctx_ns`. The context pool (8 slots) and the secure heap (1040 B per task) are then shared over
  time and not held per task.
- Cost: every request pays alloc + release. Break-even: the task must switch more than
  `k = (alloc + release) / (ctx_s − ctx_ns)` times per request. All three terms come from this bench.
- An earlier release point is also possible: right after `SECURE_dma_m2m_start` returns, before completion.
  This also removes the Save/Load paid while the task waits for completion. When the wait blocks instead of
  busy-waiting, that wait is where most switches happen. Measure both release points.

### Why FreeRTOS cannot do it today [코드]
- `SecureContext_FreeContext` (secure_context.c:293) refuses to free the **loaded** context
  (`pucStackLimit != PSPLIM` check). The running task's own context is always loaded, so self-free is a no-op.
- `portSVC_FREE_SECURE_CONTEXT` accepts calls only from privileged functions (port.c:1186), because it exists for task deletion.
- `xSecureContext` (port.c:589) stays non-zero, so PendSV would keep calling Save/Load.
- A failed alloc stops at `configASSERT` (port.c:1180). Re-allocating per request makes this a runtime failure path.

### Minimal kernel addition (kernel port change, not in the paper → `docs/gap-analysis.md` entry first)
- `secure_context.c`: new NSC function. Only via SVC (IPSR != 0). Owner must match and the context must be the loaded one:
```c
/* Free the caller's loaded context and unload PSP_S/PSPLIM_S, so the next
 * SVC allocate succeeds and PendSV skips Save/Load. Handler mode only. */
secureportNON_SECURE_CALLABLE void SecureContext_ReleaseContext( SecureContextHandle_t xHandle, void * pvTaskHandle )
{ /* checks as FreeContext, but require == PSPLIM; vPortFree; vReturnSecureContext;
     secureportSET_PSPLIM( securecontextNO_STACK ); secureportSET_PSP( securecontextNO_STACK ); */ }
```
- `portmacrocommon.h`: `portSVC_RELEASE_SECURE_CONTEXT 106`, `portRELEASE_SECURE_CONTEXT()`, which calls the
  naked `vPortReleaseSecureContext` (`svc 106; bx lr`, same shape as `vPortAllocateSecureContext`).
- `port.c` SVC handler: `case 106: SecureContext_ReleaseContext( xSecureContext, pxCurrentTCB ); xSecureContext = portNO_SECURE_CONTEXT; break;`
  Unprivileged tasks may call it, because it only touches the caller's own context.
- Task usage: `portALLOCATE_SECURE_CONTEXT(..); SECURE_dma_m2m_start(..); [wait]; portRELEASE_SECURE_CONTEXT();`.
  The release can also go before the wait.

### Added measurements (same harness)
- `CTX_ALLOC_REL` (ID 15): the partner loops N times `t0; alloc; t1; release; t2`. col0 = alloc, col1 = release.
  This replaces the single alloc sample.
- `CTX_REL` (ID 16): round trip with a partner that allocated and then released. It should equal `ctx_ns`, which confirms
  that PendSV skips Save/Load.
- `REQ_PERSIST` / `REQ_REL` (IDs 17/18): one full request cycle in Task A with k forced `taskYIELD()` to a no-context
  partner while waiting (k = 0, 1, 4). Persistent context vs alloc→start→release→wait. This gives the measured crossover k,
  to check against the formula above.
- `secure_ctx.txt`: secure heap free bytes and in-use slots after the release, to show the 1040 B returned.

### Open questions before implementing
1. Is a kernel port change (new SVC plus a secure_context.c function) acceptable for this PoC? It is outside the paper,
   so it will be logged in gap-analysis.md.
2. Release point: after completion, right after start returns, or measure both (recommended)?
3. Alternative to compare, with no alloc/free: one shared secure stack, with the mediator made non-preemptible by
   `BASEPRI_S`, which masks NS PendSV/SysTick when PRIS=0. This removes Save/Load for all tasks. Not planned unless requested.

## Commit / docs
One commit (§4 evaluation): files above plus a `docs/CHANGES.md` line. After the board run, add results under
`results/isrctx/*` and a short `cmpIsrCtx.md`.

## Verification
1. `./build.sh clean && ./build.sh`, then `scripts/eval/build_variant.sh isrctx P2_110M_warm_ts0 P2 BENCH_PHASE=1 BENCH_ICACHE`
   (runs `check_layout.sh`: expects `s_dmaTaskMem=2001b600`). Also run the default build (no BENCH_PHASE) to confirm it is unchanged.
2. Disassemble `DMA1_Channel2_IRQHandler`, `bench_isr_once` (`cpsie` directly after the CYCCNT read)
   and the Secure handler in the `.list` files.
3. On the board (after `~/st-link.sh`): `PHASE=isrctx scripts/eval/run_all.sh`, or a single variant:
   `DIR=results/isrctx/P2_110M_warm_ts0 gdb-multiarch -batch -x scripts/eval/run_bench.py`.
   Expected: new CSVs `isr_*.csv`, `start_ns.csv`, `ctx_*.csv`, `secure_ctx.txt`. `isr_s > isr_ns`, `ctx_s > ctx_ns`.
   GDB spot checks: `p/x *(uint32_t*)0x40020008 & 0x40000` → DMA1_CH2 CCR SECM = 0, and `p/x *(uint32_t*)0xE000E380` (NVIC ITNS0) bit 30 = 1 (DMA1_Channel2_IRQn = 30).
