# Secure mediator: DMA completion ISR overhead — analysis and measurement plan

Sources: `project/Secure/Src/secure_nsc.c`, `NonSecure/Src/app_freertos.c`,
`Secure/Inc/partition_stm32l562xx.h`, and the disassembly in `results/mediator/P2_110M_warm_ts0/*.list`
(P2 = Secure -O1 / NS -O1). Tags follow CLAUDE.md: [코드] = read from code/listing, [아키텍처 도출],
[실측], [추정], 미확인.

## 1. Current DMA completion workflow (Secure mediator)

Configuration [코드]:
- `SECURE_dma_m2m_init`: `NVIC_ClearTargetState(DMA1_Channel1_IRQn)` (IRQ 29 → Secure), priority 5, TCIE|TEIE.
- `AIRCR.PRIS = 0`, `BFHFNMINS = 0` (partition:297-313). `FPCCR: TS=0, CLRONRETS=0, CLRONRET=1`, LSPEN at
  reset default 1 (partition:338-354). `TZ_FPU_NS_USAGE=1`, `NSACR.CP10/11=3`.
- The NS callback is `App_DmaDoneCallback`, registered as `cmse_nsfptr_create(ns_cb)`.
- The waiting task busy-waits on `g->dmaDone` in NS thread mode (app_freertos.c `vDmaTaskA`).

Path, step by step (P2 listing addresses):

| # | Step | Where / stack | Cost source |
|---|---|---|---|
| 0 | TC sets `DMA1->ISR.TCIF1`, IRQ 29 pends | HW | — |
| 1 | Exception entry: NS thread preempted by a **Secure** handler. Basic frame pushed to PSP_NS. Background is NS, so no additional callee-saved state context is pushed [아키텍처 도출]. If the thread has `CONTROL.FPCA=1`, an extended frame is reserved (lazy) | PSP_NS | HW stacking, Secure vector fetch (VTOR_S, Secure flash) |
| 2 | `DMA1_Channel1_IRQHandler` 0c000a7c: read ISR, test TEIF/TCIF, `push {r4,lr}`, clear EN, IFCR, `s_busy=0`, load `s_ns_callback`, `cbz` | MSP_S | ~15 instr [코드] |
| 3 | cmse call setup: LSB clear (`lsrs/lsls`), r1–r3 ← r4, **16× `vmov.f32 sN,#1.0`** (s0–s15 scrubbed) | MSP_S | 21 instr. The first FP instruction triggers **lazy FP stacking** of the NS thread's s0–s15+FPSCR when FPCA was 1 [아키텍처 도출] |
| 4 | `__gnu_cmse_nonsecure_call` 0c000238: `stmdb {r5–r11,lr}` (8 w), 6 movs, `vpush {d8–d15}` (16 w), 8× vmov zero, FPSCR sanitize (vmrs/and/vmsr), `msr APSR`, `blxns r4` | MSP_S | ~25 instr, 24 words stored |
| 5 | BLXNS: pushes return address + partial RETPSR (2 w) onto MSP_S, LR=FNC_RETURN, state → NS | MSP_S | security state transition |
| 6 | `App_DmaDoneCallback` (NS handler mode, MSP_NS): 4 stores, `bx lr` | MSP_NS | same in both designs |
| 7 | FNC_RETURN: unstack 2 w from MSP_S, state → S | MSP_S | security state transition |
| 8 | `vpop {d8–d15}`, `ldmia {r5–r11,pc}`, `pop {r4,pc}` → EXC_RETURN | MSP_S | 24+2 words loaded |
| 9 | Exception return to NS thread, unstack (FP part only if lazy save happened) | PSP_NS | HW unstacking |
| 10 | Task loop sees `dmaDone` | NS | — |

A plain NS DMA channel with an NS ISR would run only steps 0, 1, 2, 6, 9 and 10. It has no FP instructions (NS -O1 C),
so there is no lazy FP save [추정, confirm in the listing].
**The Secure-only work is steps 3–5 and 7–8, plus the lazy FP save that step 3 can trigger.**

Facts that decide what to control for:
- **FPCA is set by every NSC call.** The NSC epilogue scrubs s0–s15 with 16 `vmov.f32` and sanitizes FPSCR
  before `bxns` (0c000930–0c000924) [코드]. CONTROL.FPCA is not banked, so the NS task returns with FPCA=1
  [아키텍처 도출 — verify with `mrs CONTROL`]. The next DMA ISR therefore reserves an extended frame,
  and step 3 forces the lazy save. The ISR overhead depends on whether the task called an NSC before it waited.
- Natural timing: an 8 B M2M finishes within a few tens of cycles after `EN`. Only ~20 instructions of the mediator epilogue
  follow `EN`, so in the real application the ISR often preempts the task **while it is still inside Secure
  (step 1 with Secure background)** [추정]. That is a different entry case from "NS thread waiting".
- `TS=0`: s16–s31 are not part of the exception frame. The stub saves d8–d15 in software instead (step 4) [코드].

## 2. Why the existing numbers cannot be used
- `statRoundtrip` / the old 828: start + transfer + ISR mixed together. The transfer overlaps the mediator epilogue.
- Timestamps placed *inside* a handler miss steps 1 and 9 (HW stacking/unstacking) and the vector fetch.
- The previous plan (one total + "to callback") cannot separate steps 3–5 from 7–8, and it does not control FPCA.

## 3. Measurement method

### 3.1 Deterministic trigger (both paths)
Let the transfer finish while the IRQ is masked, then release it at a known instruction:
```
__disable_irq()                 PRIMASK_NS; PRIS=0 → also masks the Secure prio-5 IRQ [아키텍처 도출]
start (mediator NSC | ns_dma_start)
poll dst[7] until written; then wait 32 cycles on CYCCNT     guarantees TC is pending
set FPCA state (F0: clear CONTROL.FPCA; F1: vmov s0,s0)
T0 = CYCCNT; E0 = EXCCNT
cpsie i
while (!done) {}
T5 = CYCCNT; E5 = EXCCNT
```
- `T5 − T0` covers steps 1–10 for exactly one exception, with the HW parts included.
- `DWT->EXCCNT` (8-bit, exception entry/exit overhead cycles) isolates the HW stacking part.
  `DWT_CTRL = 0x40000001` gives NOPRFCNT=0, so the profiling counters exist [실측 env.txt], and DAUTHSTATUS=0xff
  allows Secure non-invasive counting [실측]. Whether EXCCNT counts lazy FP stacking and FNC_RETURN: 미확인.
  The F0/F1 difference answers the first question.
- The NS baseline is DMA1_Channel2 with SECM=0 and ITNS=1. Its NS `DMA1_Channel2_IRQHandler` has the same C body as the
  Secure handler and calls the callback directly (from `docs/plan-isr-secure-context-overhead.md`).

### 3.2 Attribution timestamps (BENCH_TS build only, same convention as BENCH_T1/T4)
| Stamp | Location | Secure path | NS path |
|---|---|---|---|
| Ta | first statement of the DMA handler | Secure global | NS SRAM2 |
| Tb | callback entry | NS SRAM2 | NS SRAM2 |
| Tc | callback exit | NS SRAM2 | NS SRAM2 |
| Td | handler, right after the callback returns | Secure global | NS SRAM2 |

Segments (equivalent steps in parentheses):

| Segment | Secure path | NS path |
|---|---|---|
| `Ta−T0` | entry (1) | entry (1) |
| `Tb−Ta` | body+scrub+stub+BLXNS (2–5) | body+call (2) |
| `Tc−Tb` | callback (6) | callback (6) |
| `Td−Tc` | FNC_RETURN+vpop/ldm (7–8a) | return (≈0) |
| `T5−Td` | epilogue + exception return + loop (8b–10) | same |

Secure stamps are read after each sample through a getter NSC, outside the timed window (pattern of `SECURE_bench_inner`).
One stamp costs `ldr lit; ldr CYCCNT; str` and is calibrated as in `INNER_CALIB`. The clean total comes from the ts0 build.
The ts1 segments are attribution only. Check: `Σ segments(ts1) − total(ts0) ≈ 4 × stamp cost`.

### 3.3 Tables (rows in SRAM2, `BENCH_COLS` 2 → 6: total, exccnt, Ta, Tb, Tc, Td, all relative to T0)
| ID | Test |
|---|---|
| 9 | ISR_CALIB: done preset, no DMA. Loop + cpsie + read cost |
| 10 / 11 | ISR_NS_F0 / ISR_NS_F1 |
| 12 / 13 | ISR_S_F0 / ISR_S_F1 (F1 is the natural state after an NSC call) |
| 14 | ISR_NAT_S (optional): IRQ not masked, `T0` before the NSC, `T5` at done, minus the START total. This is the real "preempted inside Secure" case and is only a sanity check |

1000 rows × 24 B = 24 KB at 0x20030040, which ends below `BENCH_SRC` 0x20038000. `run_bench.py` COLS = 6.

### 3.4 Results to report
- **ISR overhead (headline)** = `ISR_S_F1 − ISR_NS_F1`. This is the realistic case, since the task has just called an NSC.
- Also `ISR_S_F0 − ISR_NS_F0`, which is the overhead without the FP effect, and `ISR_S_F1 − ISR_S_F0` = lazy FP save cost.
- EXCCNT: both paths should match within the same F state if there is no additional state context. A difference would mean
  extra HW stacking on Secure handler entry [아키텍처 도출 → verified by measurement].
- Breakdown: (2–5) − (2), and (7–8a). Compare against the instruction counts in §1. Report the expected 49 extra
  instructions plus 24+2 stored words as a sanity bound [코드].
- Min / median / max, N=1000, conditions {P1,P2} × {110,32 MHz} × {off,warm,cold}, using `run_all.sh` with `PHASE=isr`.

## 4. Code changes (English comments ≤ 3 lines, BENCH_PHASE only)
- `Secure/Src/main.c`: under BENCH_PHASE, also set `DWT_CTRL_EXCEVTENA`. Release DMA1_CH2 (SECM=0, ITNS).
- `Secure/Src/secure_nsc.c`: `BENCH_TI(k)` stamps at handler entry and after the callback (BENCH_TS only).
- `Secure/Src/bench_s.inc`: `SECURE_bench_isr_ts(k)` getter.
- `NonSecure/Src/bench_ns.inc`: `bench_dma_cb` (Tb/Tc stamps under BENCH_TS, done flag), `DMA1_Channel2_IRQHandler`,
  `ns_dma_start`, `bench_isr_once(path, fpca, row)`, tests 9–14, `BENCH_COLS 6`.
- `scripts/eval/run_bench.py`: COLS=6 and new NAMES.
- `.bss` stays unchanged (all bench state in SRAM2), so `check_layout.sh` = 0x2001b600.

## 5. Biases and controls
- TC pending: the 32-cycle guard after the dst poll. Check that `Ta−T0` has a tight distribution.
- SysTick inside a sample: use min/median, and EXCCNT exposes it as a double entry.
- -O0 Secure (P1) inflates steps 2–4. P2 has the same optimization level on both sides and is the fair headline.
- ICACHE is 1-way and shared S/NS. Secure handler, stub, and vector fetch are conflict-sensitive, hence the off/warm/cold sweep.
- The NS baseline handler skips `s_busy` and the `s_ns_callback` null check (3–4 instructions). Note this in the report.

## 6. Verification on the board
1. Build ts0/ts1 for P2 110 MHz warm, plus the default build (no BENCH_PHASE) unchanged.
2. Listing: `cpsie` directly after the `T0`/`E0` reads. The NS handler has no FP instructions. The Secure handler matches §1.
3. GDB before the run: `p/x *(uint32_t*)0xE0001000` (bit18 EXCEVTENA=1); `p/x *(uint32_t*)0x40020008 & 0x40000` (CH2 SECM=0).
   In bench_isr_once at T0: `p/x $control` (FPCA bit2 = 0 for F0 and 1 for F1).
4. Expected: `ISR_S_F* > ISR_NS_F*`. EXCCNT is equal between paths for the same F. `ISR_*_F1 ≥ ISR_*_F0` only for the Secure path.
