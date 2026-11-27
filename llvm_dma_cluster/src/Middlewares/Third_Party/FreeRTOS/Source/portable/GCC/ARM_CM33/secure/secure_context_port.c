/*
 * FreeRTOS Kernel V11.3.1
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/* Secure context includes. */
#include "secure_context.h"

/* Secure port macros. */
#include "secure_port_macros.h"

#ifndef SECSTACK_STATIC
    #define SECSTACK_STATIC    0   /* llvm_dma_cluster: 1 = landing pad (secure_context_patch.md) */
#endif

void SecureContext_LoadContextAsm( SecureContext_t * pxSecureContext ) __attribute__( ( naked ) );
void SecureContext_SaveContextAsm( SecureContext_t * pxSecureContext ) __attribute__( ( naked ) );

void SecureContext_LoadContextAsm( SecureContext_t * pxSecureContext )
{
    /* pxSecureContext value is in r0. */
    __asm volatile
    (
        " .syntax unified                   \n"
        "                                   \n"
        " mrs r1, ipsr                      \n" /* r1 = IPSR. */
        " cbz r1, load_ctx_therad_mode      \n" /* Do nothing if the processor is running in the Thread Mode. */
        " ldmia r0!, {r1, r2}               \n" /* r1 = pxSecureContext->pucCurrentStackPointer, r2 = pxSecureContext->pucStackLimit. */
        "                                   \n"
        #if ( configENABLE_MPU == 1 )
            " ldmia r1!, {r3}               \n" /* Read CONTROL register value from task's stack. r3 = CONTROL. */
            " msr control, r3               \n" /* CONTROL = r3. */
        #endif /* configENABLE_MPU */
        "                                   \n"
#if SECSTACK_STATIC
        " mrs  r12, psp                     \n" /* llvm_dma_cluster §2: 쉼 PSP_S (pad top 또는 pad 위 정지 프레임) 보존 */
        " movw r3, #:lower16:g_idle_psp     \n"
        " movt r3, #:upper16:g_idle_psp     \n"
        " str  r12, [r3]                    \n"
#endif
        " msr psplim, r2                    \n" /* PSPLIM = r2. */
        " msr psp, r1                       \n" /* PSP = r1. */
        "                                   \n"
        " load_ctx_therad_mode:             \n"
        "    bx lr                          \n"
        "                                   \n"
        ::: "r0", "r1", "r2", "r3", "r12", "memory"
    );
}
/*-----------------------------------------------------------*/

void SecureContext_SaveContextAsm( SecureContext_t * pxSecureContext )
{
    /* pxSecureContext value is in r0. */
    __asm volatile
    (
        " .syntax unified                   \n"
        "                                   \n"
        " mrs r1, ipsr                      \n" /* r1 = IPSR. */
        " cbz r1, save_ctx_therad_mode      \n" /* Do nothing if the processor is running in the Thread Mode. */
        " mrs r1, psp                       \n" /* r1 = PSP. */
        "                                   \n"
        #if ( ( configENABLE_FPU == 1 ) || ( configENABLE_MVE == 1 ) )
            " vstmdb r1!, {s0}              \n" /* Trigger the deferred stacking of FPU registers. */
            " vldmia r1!, {s0}              \n" /* Nullify the effect of the previous statement. */
        #endif /* configENABLE_FPU || configENABLE_MVE */
        "                                   \n"
        #if ( configENABLE_MPU == 1 )
            " mrs r2, control               \n" /* r2 = CONTROL. */
            " stmdb r1!, {r2}               \n" /* Store CONTROL value on the stack. */
        #endif /* configENABLE_MPU */
        "                                   \n"
        " str r1, [r0]                      \n" /* Save the top of stack in context. pxSecureContext->pucCurrentStackPointer = r1. */
#if SECSTACK_STATIC
        " movs r1, #0                       \n" /* llvm_dma_cluster §3: PSPLIM = 0 먼저 */
        " msr  psplim, r1                   \n"
        " movw r1, #:lower16:g_idle_psp     \n"
        " movt r1, #:upper16:g_idle_psp     \n"
        " ldr  r1, [r1]                     \n"
        " msr  psp, r1                      \n" /* PSP = Load 에서 보존한 쉼 값 */
        " movw r1, #:lower16:g_pad_top      \n" /* §4: PSPLIM = pad base */
        " movt r1, #:upper16:g_pad_top      \n"
        " ldr  r1, [r1]                     \n"
        " sub  r1, r1, #248                 \n"
        " msr  psplim, r1                   \n"
#else
        " movs r1, %0                       \n" /* r1 = securecontextNO_STACK. */
        " msr psplim, r1                    \n" /* PSPLIM = securecontextNO_STACK. */
        " msr psp, r1                       \n" /* PSP = securecontextNO_STACK i.e. No stack for thread mode until next task's context is loaded. */
#endif
        "                                   \n"
        " save_ctx_therad_mode:             \n"
        "    bx lr                          \n"
        "                                   \n"
        ::"i" ( securecontextNO_STACK ) : "r1", "memory"
    );
}
/*-----------------------------------------------------------*/
