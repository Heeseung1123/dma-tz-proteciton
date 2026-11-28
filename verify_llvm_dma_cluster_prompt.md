Verify and harden `llvm_dma_cluster/` against the real `~/dma-tz` project. Keep working until everything below passes, then keep improving on your own.

## Context
- `llvm_dma_cluster/` holds:
  - LLVM 18 passes: `DmaCluster` (NS LTO plugin) and `SecureGuardSelect` (Secure, analysis only).
  - Python generators: `gen_layout.py` and `pack_guards.py`. They write the linker scripts, the manifest and the headers.
  - Secure code: `secure_dma_boot.c`, `secure_dma_entry.S`, `secure_guard_bind.c` and `secure_context_patch.md`.
  - Build scripts.
  - Read `README.md` and `EXPLAINED.md` first.
- Target: STM32L562 (Cortex-M33, TrustZone) running FreeRTOS with the ARMv8-M MPU+TZ port. The Secure side stays on GCC + GNU ld; clang is used for analysis only. The NS side uses clang + lld + the LTO plugin.
- None of this code has ever been compiled or run.

## Rules
- Never edit files inside `~/dma-tz`. If project or FreeRTOS kernel files must change, copy them into `llvm_dma_cluster/src/` with the same relative path. Edit the copy, and point the build at it.
- Keep the design intent:
  - DMA policy is enforced by S-MPU tag regions (TTT) plus the caller's NS-MPU (TTAT).
  - Each cluster is followed by an MPCBB guard.
  - No runtime lookup tables for guards.
  - No added per-call or per-switch cycles.
  - The mediator is assembly and atomic.
- Keep the current landing-pad design. Do not switch to SVC entry unless asked.
- Keep the GCC baseline buildable and unchanged, so the earlier cycle measurements (commit 983a749) stay comparable.

## Verify, in order
1. The plugins build against LLVM 18.
2. The NS 2-pass build succeeds. `dma_layout.json` lists the expected DMA buffers, tasks and shared clusters, with no unresolved buffers. `check_layout.py` passes.
3. The Secure guard analysis and packing succeed. The Secure image builds with the generated `.ld` files and `--in-implib`. The veneer symbols are unchanged.
4. On the board, if one is reachable:
   - boot succeeds: `dma_boot_config()` returns 0;
   - allowed DMA copies work;
   - out-of-cluster, cross-task and guard-overrun requests are rejected or blocked;
   - an interrupt pending at mediator exit lands on the pad without a fault;
   - the context-switch and NSC cycle counts are measured against the baseline.
5. Negative tests:
   - a stale NS image triggers the fail-closed return (-7);
   - an annotated buffer that cannot be placed fails the build.

## Loop
- After each fix, rebuild and rerun everything from step 1.
- Log each finding, fix and result in `llvm_dma_cluster/VERIFY_LOG.md`. Commit on a branch, never on main.
- When all steps pass, look for improvements: analysis precision, fewer wasted bytes, clearer diagnostics, more tests. Apply them under the same rules.
- Stop and report only when a decision belongs to me. Examples: a design-intent conflict, hardware you cannot reach, or a change that adds overhead.
