# stock Secure context 수정 (쉼 PSP_S = landing pad 보존)

목적: 쉬는 동안 PSP_S 가 0 이 아니라 **landing pad top(또는 pad 위에 정지한 프레임 바닥)** 을 가리키게 하고, Plain 태스크의 stock Load/Save 를 거쳐도 그 값을 잃지 않게 한다.

- 변경 범위는 아래 3 곳이고 모두 `#if SECSTACK_STATIC` 로 감싼다.
- stock 전제인 `PSPLIM == 0` 은 그대로 유지된다. 쉬는 동안 PSPLIM_S 는 계속 0 이다.
- 추가 비용: Load 3개 명령, Save 2개 명령. 그 대신 Plain 태스크 전환마다 PSP 상수 적재가 없어진다(Save).

## 1. `secure_context.c` — `SecureContext_Init`

```c
#if SECSTACK_STATIC
    extern uint32_t g_idle_psp;                 /* dma_boot_config() 가 g_pad_top 으로 설정 */
    secureportSET_PSPLIM( 0 );
    secureportSET_PSP( g_idle_psp );
#else
    secureportSET_PSPLIM( securecontextNO_STACK );
    secureportSET_PSP( securecontextNO_STACK );
#endif
```

## 2. `secure_context_port.c` — `SecureContext_LoadContextAsm`

들어오는 stock 태스크의 heap 스택을 설치하기 **직전에** 현재 쉼 PSP_S 를 보존한다. 위치는 `msr control, r3`(MPU 분기) 다음, `msr psplim, r2` 앞이다. 이 시점에 r3 은 이미 소비되었다.

```c
        #if ( configENABLE_MPU == 1 )
        "   ldmia r1!, {r3}                   \n" /* (stock) r3 = CONTROL */
        "   msr control, r3                   \n" /* (stock) */
        #endif
#if SECSTACK_STATIC
        "   mrs  r12, psp                     \n" /* 쉼 PSP_S (pad top 또는 pad 위 정지 프레임) */
        "   movw r3, #:lower16:g_idle_psp     \n"
        "   movt r3, #:upper16:g_idle_psp     \n"
        "   str  r12, [r3]                    \n"
#endif
        "   msr psplim, r2                    \n" /* (stock) */
        "   msr psp, r1                       \n" /* (stock) */
```
- inline asm 의 clobber 목록에 `"r3", "r12", "memory"` 를 추가한다.
- Load 는 PendSV(Handler mode)에서 실행되므로, 그 사이 Thread 스택이 쓰이지 않는다.

## 3. `secure_context_port.c` — `SecureContext_SaveContextAsm` 끝

붙여주신 87행 `movs r1, %0`부터 PSP/PSPLIM 을 0 으로 만드는 부분을 교체한다.

```c
        "   str r1, [r0]                      \n" /* (stock) pucCurrentStackPointer = r1 */
#if SECSTACK_STATIC
        "   movs r1, #0                       \n"
        "   msr  psplim, r1                   \n" /* PSPLIM = 0 먼저 (stock 전제 유지) */
        "   movw r1, #:lower16:g_idle_psp     \n"
        "   movt r1, #:upper16:g_idle_psp     \n"
        "   ldr  r1, [r1]                     \n"
        "   msr  psp, r1                      \n" /* PSP = 보존해 둔 쉼 값 */
#else
        "   movs r1, %0                       \n" /* (stock) NO_STACK */
        "   msr psplim, r1                    \n"
        "   msr psp, r1                       \n"
#endif
```

## 4. (필수 보강) 쉬는 동안 PSPLIM_S = pad base

**왜 필요한가.** 드물게 두 태스크가 동시에 pad 위에 정지할 수 있다.
1. A 가 정지해 있는 동안 B 가 진입 창(SG 직후, `cpsid` 전 명령 경계 1개)에서 선점된다.
2. B 의 프레임이 A 의 프레임 아래에 쌓인다.
3. FP 프레임이면 212 B + 212 B 가 pad(248 B)를 넘는다.
4. PSPLIM_S = 0 이면 넘친 부분이 **pad 아래 NS cluster 에 기록**된다. 그 안에는 Secure PC 가 들어 있어 NS DMA 로 변조할 수 있다. 이는 **무결성 위반**이다.

PSPLIM_S = pad base 이면 이 상황이 STKOF(fail-closed)로 끝난다.

```c
/* Init */
secureportSET_PSP( g_idle_psp );
secureportSET_PSPLIM( g_pad_top - 248u );          /* PSP 먼저, limit 나중 */

/* SaveContextAsm 끝: 위 3 의 msr psp 다음에 추가 */
"   movw r1, #:lower16:g_pad_top      \n"
"   movt r1, #:upper16:g_pad_top      \n"
"   ldr  r1, [r1]                     \n"
"   sub  r1, r1, #248                 \n"
"   msr  psplim, r1                   \n"          /* PSPLIM = pad base */
```

`secure_context.c` 의 `SecureContext_AllocateContext` / `SecureContext_LoadContext` 에 있는 전제 검사도 함께 바꾼다.
- 바꾸기 전: `PSPLIM == securecontextNO_STACK`
- 바꾼 뒤: `PSPLIM == g_pad_top - 248`

stock LoadContextAsm 이 PSPLIM 과 PSP 를 덮어쓰는 순서(psplim → psp)는 기존과 같다.

**남는 위험.** 정수 프레임 두 개(72 + 72 B)는 pad 안에 들어가므로 STKOF 가 나지 않는다. 이 경우 두 태스크가 **잘못된 순서로 재개되면** 서로의 프레임을 unstack 하는 cross-task 혼동이 생길 수 있다.
- 이 상황이 생기려면 "A 가 창에서 정지" **그리고** "그 사이 B 의 1-경계 진입 창이 맞음"이 동시에 일어나야 한다.
- 정상 태스크는 NS `dmaMutex` 를 잡은 채 정지하므로, B 는 mediator 를 호출하지 않는다.
- 따라서 이 위험은 mutex 를 우회한 호출에서만 생긴다. gap-analysis 에 잔여 위험으로 기록할 것.

## 동작 확인 포인트

| 상황 | PSP_S |
|---|---|
| 부팅 후 스케줄러 시작 | `g_pad_top` |
| Plain 태스크 실행 중 (Load 이후) | 그 태스크의 heap 스택 |
| Plain 태스크가 나간 뒤 (Save 이후) | Load 직전에 보존한 값으로 복원 |
| DMA 태스크가 창에서 선점되어 pad 위에 정지 | 정지 프레임 바닥 (≠ `g_pad_top`) → 다른 태스크의 mediator 호출은 −1 |
| 정지한 DMA 태스크가 재개되어 BXNS | 프레임을 unstack 한 뒤 다시 `g_pad_top` |
