# Manual check: does DMA_Task_A take FP-extended exception frames? (cmpMemUsage_result.md §5.6, HowmemUsageWork.md §11.9)
#
# Usage (gdbserver running: ~/st-link.sh), B = S_P1 or I_P1:
#   gdb-multiarch -nx results/mem/B/FreeRTOS_SecureIOToggle_TrustZone_NonSecure.elf
#   (gdb) target remote localhost:61234
#   (gdb) load results/mem/B/FreeRTOS_SecureIOToggle_TrustZone_Secure.elf
#   (gdb) load results/mem/B/FreeRTOS_SecureIOToggle_TrustZone_NonSecure.elf
#   (gdb) monitor reset
#   (gdb) source scripts/eval/check_fp_frame.gdb
#
# Expected:            S_P1                          I_P1
#   EXC_RETURN         0xffffffac                    0xffffffbc
#   FType (bit 4)      0  = FP-extended frame        1  = basic frame
#   CONTROL.FPCA       1                             0
#   high-water         128 B from the stack top      64 B
#   0x3f800000 words   16 (s0-s15 = 1.0f)            none ("Pattern not found.")

set pagination off
set confirm off

# 1. Stop inside DMA_Task_A (its loop line in app_freertos.c) on the 3rd iteration: pxCurrentTCB is then A's TCB.
#    At -O1 the call is inlined, so gdb reports "App_DmaM2M (g=<s_dmaTaskMem>, ...) at app_freertos.c:257" (g = task A).
#    With MPU wrappers v2 the handle in globals.hTask is a kernel-object index, not a TCB pointer, so take it here.
hbreak app_freertos.c:346
ignore $bpnum 2
continue
set $tcbA = pxCurrentTCB
printf "\n[1] DMA_Task_A TCB = 0x%08x, name = %s\n", $tcbA, $tcbA->pcTaskName
delete

# 2. Stop in the idle task. Every task is blocked there, so A's context is saved in its TCB.
hbreak prvCheckTasksWaitingTermination
continue
delete
printf "[2] stopped in IDLE, pxCurrentTCB = 0x%08x (not A)\n", pxCurrentTCB

# 3. The port's PendSV stores {xSecureContext, PSP, PSPLIM, CONTROL, EXC_RETURN} right below *pxTopOfStack
#    (portasm.c: stmia r2!, {r0, r3-r5, lr}; str r2, [r1]).
set $end = *(unsigned int *)$tcbA
set $ctrl = *(unsigned int *)($end - 8)
set $exc = *(unsigned int *)($end - 4)
printf "[3] saved context tail at 0x%08x:\n", $end - 20
x/5xw $end - 20
printf "    EXC_RETURN = 0x%08x  FType(bit4) = %d  (0 = FP-extended 104 B frame, 1 = basic 32 B frame)\n", $exc, ($exc >> 4) & 1
printf "    CONTROL    = 0x%08x  FPCA(bit2)  = %d  (1 = task has an active FP context)\n", $ctrl, ($ctrl >> 2) & 1

# 4. The deepest part of A's stack. tasks.c filled it with 0xa5 at creation, so the first non-0xa5a5a5a5 word from the
#    bottom is the high-water mark. Show the top 128 B: in S the extended frame (with s0-s15 = 0x3f800000) is there.
set $top = (unsigned int)xDmaTaskAStack + sizeof(xDmaTaskAStack)
set $p = (unsigned int)xDmaTaskAStack
while *(unsigned int *)$p == 0xa5a5a5a5
  set $p = $p + 4
end
printf "[4] high-water = %d B from the stack top (first used word at 0x%08x)\n", $top - $p, $p
printf "    top 128 B of the stack (lowest address first):\n"
x/32xw $top - 128
printf "    words equal to 0x3f800000 (1.0f, the Secure entry's FP clearing value):\n"
find /w xDmaTaskAStack, +sizeof(xDmaTaskAStack), 0x3f800000
