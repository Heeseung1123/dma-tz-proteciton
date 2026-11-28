# VERIFY_LOG — llvm_dma_cluster 실검증 기록

브랜치: `verify/llvm-dma-cluster` (impl/paper 에서 분기). 규칙: `~/dma-tz` 의 프로젝트 파일은 수정하지 않고,
바꿔야 하는 파일은 `llvm_dma_cluster/src/<같은 상대 경로>` 로 복사해 고친다. GCC baseline(`./build.sh`)은 그대로.

## 환경
- LLVM 18: 시스템에는 LLVM 19 만 설치, sudo 불가 → `apt download` + `dpkg -x` 로 `~/opt/root18` 에 풀어 사용
  (clang/lld/llvm-dev 18.1.8, cmake 3.31.6, ninja 1.12.1). `source scripts/env_llvm18.sh`.
  추출본의 `LLVMExports.cmake`/`ClangTargets.cmake` 에 박힌 `/usr/lib/llvm-18` 를 추출 경로로 sed 치환함.
- GCC: ST GNU tools 14.3.rel1 (build.sh 와 같은 경로). newlib sysroot 는 `arm-none-eabi-gcc -print-sysroot`.

## 기록 (발견 → 수정 → 결과)
| # | 단계 | 발견 | 수정 | 결과 |
|---|---|---|---|---|
| 1 | 1 | `find_package(LLVM 18)` 이 18.1.8 을 거부 (LLVMConfigVersion 은 major.minor 일치 요구) | 버전 없이 찾고 `LLVM_VERSION_MAJOR EQUAL 18` 검사 | configure 진행 |
| 2 | 1 | 한 디렉터리에 `add_llvm_pass_plugin` 2개 → LLVMProcessSources 오류 | `PARTIAL_SOURCES_INTENDED` | configure OK |
| 3 | 1 | `SmallPtrSet<_,64/128>`: LLVM 18 static_assert SmallSize ≤ 32 | DmaCluster: `SmallSetVector<Function*,32>`(삽입 순서 → 결정적), SecureGuardSelect: `SmallPtrSet<_,32>` | 두 .so 빌드 OK |
| 4 | 1 | cluster 안 버퍼 순서가 `std::map<GlobalVariable*,…>` 포인터 순서 → 1차/2차 JSON 비결정 가능 | cluster 별 버퍼를 이름 순 정렬 | |
| 5 | 2 | lld: `_estack = ORIGIN(RAM)…` 가 MEMORY 정의 앞 → "memory region not defined" (GNU ld 는 허용) | NS .ld 사본에서 `_estack` 을 MEMORY 뒤로 | |
| 6 | 2 | lld LTO: inline asm `ldr rX, =imm` literal pool 이 `privileged_functions` 섹션 끝에 몰려 범위 초과 | `src/.../non_secure/portasm.c` 사본: `=` 를 쓰는 naked asm 블록 6개 끝(마지막 분기 뒤)에 `.ltorg` | |
| 7 | 2 | inline asm 에서만 참조되는 `MPU_*Impl`, `vSystemCallExit` 를 LTO 가 제거 → undefined | `portasm.c`, `mpu_wrappers_v2_asm.c` 는 LTO 밖 일반 오브젝트로 컴파일 (`NOLTO_REL`). DMA sink 없음 → 분석 범위 영향 없음 | |
| 8 | 2 | `dma_clusters_pass1.ld` 가 `> FLASH` 고정, 이 프로젝트 NS Flash 이름은 `ROM` | `@RAM@/@FLASH@` 치환 (`NS_RAM`/`NS_FLASH`) | |
| 9 | 2 | **정밀도**: opaque pointer 에서 태스크 함수·타이머 콜백이 모두 `void(ptr)` → 간접 호출 보수 연결로 `prvTimerTask` 가 vDmaTaskA/B 를 "호출" → 모든 버퍼가 공유 cluster 로 (owner 에 prvTimerTask) | 주소가 태스크 생성 API 의 entry 인자(또는 그 TaskParameters_t 초기값)로만 쓰이는 함수는 간접 호출 후보에서 제외 | 전용 2 + 공유 1 로 정상 분리 |
| 10 | 2 | `MPU_xTaskCreateRestricted` 래퍼 내부 호출이 "entry 해석 실패" 경고 | 래퍼가 자기 인자를 그대로 넘기는 호출은 건너뜀 | 경고 0 |
| 11 | 2 | annotate 로 지정한 버퍼는 초기값/상수/외부 정의 검사를 거치지 않음 (NOLOAD 로 초기값이 조용히 사라짐), 없는 태스크 이름도 통과 | annotate 버퍼에도 같은 조건 검사 → `annotated-unplaceable` error | (5단계 음성 시험에서 확인) |
| 12 | 2 | 태스크 전용으로 annotate 한 버퍼의 owner 에 자동 판정 owner 가 합쳐짐 → 다른 태스크 NS-MPU 에 남의 cluster 가 들어감 (TTAT 우회) | 전용 cluster owner = 그 태스크 하나. 다르면 `annotate-override` warning | |
| 13 | 2 | 공격 시험 호출(남의 버퍼를 일부러 넘김)이 owner 를 오염 | 설정 `exclude_callers` (noinline 함수 이름). 인자 역추적도 그 호출 위치로는 올라가지 않음 | |
| 14 | 2 | 진단 순서가 함수 순회 순서에 의존 | (loc, kind, task, msg) 정렬 | |
| 15 | 2 | sink 목록에 `SECURE_dma_m2m_start_asm/_svc/_table` 없음 (NS 가 이 NSC 로 부르면 버퍼를 못 찾음) | 설정에 추가 | |
| 16 | 2 | (원본 baseline, 수정 안 함) `xPlainTaskParams` 의 `portMPU_REGION_READ_ONLY`(=6) 를 V11 port 가 tskMPU 플래그로 해석 → RO 가 아니라 RW+XN | 사본만 `tskMPU_REGION_READ_ONLY` | baseline 은 그대로 둠 |

### 2단계 결과 (NS 2-pass)
- 앱 사본(`src/.../NonSecure/Src/app_freertos.c`): DMA 목적지를 평범한 static 전역으로 (`s_dmaDstA[256]`, `s_dmaDstB[256]`, 공유 `s_dmaShared[64]`),
  태스크 globals+source 는 태스크 private 구조체 1개 = NS-MPU region 1개, cluster region 은 `dma_regions.h`.
  NS-MPU 사용: 태스크당 3/3 (private 1 + 전용 cluster 1 + 공유 cluster 1). 원본은 2/3 사용 — **공유 cluster 때문에 1개 더 씀** (결정 필요 항목 참조).
- `dma_layout.json`: tasks = prvTimerTask, vDmaTaskA, vDmaTaskB, vPlainTask / clusters = s0{A,B}: s_dmaShared(64),
  t.vDmaTaskA: s_dmaDstA(256), t.vDmaTaskB: s_dmaDstB(256) / errors 0, diagnostics 0. 1차·2차 JSON 동일.
- `check_layout.py` OK: c0 [0x20018000,0x20018100) guard 0x20018100, c1 [0x20018200,…) guard 0x20018300, c2 [0x20018400,…) guard 0x20018500,
  manifest @0x08040400 count=3. `xDmaTaskAParams.xRegions` = {s_privA,0x60,RW} {0x20018000,0x100} {0x20018200,0x100} (ELF 에서 직접 읽음).
| 17 | 3 | clang: naked 함수 안의 `(void)arg;` 를 거부 (`SECURE_dma_m2m_start_asm/_svc`), GCC 는 허용 | 사본 `secure_nsc.c` 에서 `#if !defined(__clang__)` (분석 IR 전용, GCC 산출물 불변) | ② 통과 |
| 18 | 3 | 하네스 `secure_dma_entry.S` 가 `SECURE_dma_m2m_start` 와 `s_busy` 를 쓰는데 원본 `secure_nsc.c` 에 같은 이름의 C mediator 와 `static s_busy` | 사본에서 `-DDMA_MEDIATOR_PAD` 일 때 C mediator 제외, `s_busy` extern | veneer 의 `b.w` 가 .S 본문(`__acle_se_SECURE_dma_m2m_start` 0x0c000238)으로 감 |
| 19 | 3 | `secure_guard_bind.c`: GUARD_BIND_N==0 이면 `prvIsGuardBlock` 미사용 경고 | `#if GUARD_BIND_N > 0` | 경고 0 (남은 RWX LOAD 경고는 baseline 과 동일) |

### 3단계 결과 (Secure)
- `secure_context.c/_port.c` 사본: secure_context_patch.md §1–§4 그대로 (`SECSTACK_STATIC=1`). 쉼 PSPLIM 검사도 `securecontextIDLE_PSPLIM`(= pad base)로.
- `main.c` 사본: `SMPU_SetupTaskRegions()` → `g_dma_boot_status = dma_boot_config(); if (≠0) Error_Handler();`
- 분석(②): 부팅 경로 함수 178 개. annotated 모드 → chosen 0. possible 7 개(pSecureErrorCallback, s_ns_status, g_tzic_sr3_at_arm,
  secure_heap 의 pxEnd/xStart/xFreeBytesRemaining/xMinimumEverFreeBytesRemaining) — 모두 부팅 뒤 NSC/ISR 에서만 쓰임을 코드로 확인.
  xSecureContexts 는 보수적으로 "부팅 경로" 판정(간접 호출 후보 때문) — 안전 쪽 오판.
- 배치(③): guard 2 개(pad 제외: 0x30018300, 0x30018500), 사용 0.
- Secure 최종(④): GCC 14.3, CubeIDE 와 같은 플래그(-O0), `--in-implib=⓪`. text 12808 / data 48 / bss 11992.
- veneer(⑤): ⓪(= baseline `Secure/Debug/secure_nsclib.o`, baseline 오브젝트를 새로 링크한 것과 byte 동일) 과 16 개 심볼·주소 동일.
| 20 | 2/3 | 설정의 `"out"` 경로가 고정 → 시험 변형 빌드가 본 빌드의 layout/candidates 를 덮어씀 | pass 에 `DMA_CLUSTER_OUT` / `SECURE_GUARD_OUT` 환경 변수 (빌드 스크립트가 출력 디렉터리 기준으로 설정) | |
| 21 | 5 | `k_guard_bind_ns[]`(1 원소 static const)를 GCC -O0 가 심볼 없이 상수 접기 → ELF 로 기대 guard 를 읽을 수 없음 | `check_pair.py` 는 secure.elf 의 `.gbind<k>` 출력 섹션 주소(= Secure 데이터 실제 위치)를 기준으로 | stale 짝 -7 예상 |

### 5단계 결과 (보드 없이) — `tests/run_tests.sh` PASS 11 / FAIL 0
- 추가 도구: `scripts/build_all.sh` (1–3단계 전체), `scripts/check_pair.py` (secure.elf + ns.elf → `dma_boot_config()` 예상 반환값),
  `tests/host_guard_bind.c` (실제 `secure_guard_bind.c` 를 호스트에서 실행).
- T1 guard 결합 변형(`PACK_ARGS=--include-possible`): 7 개 32 B 가 Task A guard(0x30018300)로, veneer 동일, check_pair 0.
- T2 stale NS(`-DDMA_TEST_SHIFT`: Task A cluster 512 B → 뒤 guard 이동): check_pair -7, 호스트 실행 `guard_bind_init` = -1 (→ `dma_boot_config` -7 → `Error_Handler`),
  짝이 맞는 manifest 로는 0.
- T3 빌드 실패해야 하는 것: 초기값 있는 annotate 버퍼, 없는 태스크 이름, 스택 버퍼, cluster 7 개(> 태그 4), 128 KB cluster(lld region 초과) — 모두 실패 + 기대 진단.
- T4 annotate 양성: sink 사용처 없는 annotate 버퍼 배치, Task A 가 쓰는 B-annotate 버퍼 → owner = B 만 + `annotate-override` 경고.

## 개선 (모든 단계 통과 후)
| # | 내용 | 결과 |
|---|---|---|
| 22 | 진단/`sites` 위치에 인라인 호출 체인 (`app_freertos.c:179 <- app_freertos.c:225`) — 래퍼가 per-TU -O2 에서 인라인되면 태스크 줄이 안 보였음 | 버퍼마다 태스크의 호출 줄이 나옴 |
| 23 | `unresolved-load` 는 README 상 "annotate 로 보완" 인데 error 가 남아 STRICT 빌드가 항상 실패 → 그 태스크(또는 공유)에 annotate DMA 버퍼가 있으면 warning 으로 낮춤 (놓친 버퍼는 런타임 TTT/TTAT 가 거부 = fail-closed) | 단위 시험 |
| 24 | `check_layout.py`: `{cluster start, cap}` NS-MPU region 쌍이 이미지 읽기 전용 데이터에 owner 수만큼 있는지 (dma_regions.h 미반영 탐지), cluster 미사용 바이트 보고 | 음성 시험 `stale_regions` |
| 25 | `gen_layout.py`: cluster 별 bound → cap 표 출력 | 당시 미사용 192 B (공유 64 B cluster 의 256 B 올림) → #34 에서 해소, guard 768 B |
| 26 | `tests/pass/`: opt 단위 시험 13 항목 (void(ptr) 간접 호출 정밀도, Argument 역추적, 포인터 변수, heap/stack error, exclude_callers, annotate) | PASS |
| 27 | (#28 에서 삭제) `secure/secure_dma_entry_min.S` (선택안, **기본 아님**): baseline `SECURE_dma_m2m_start_asm` 본문 + pad 검사 6 명령. `MEDIATOR_S=min` | 빌드·veneer 동일·check_pair 0 |

## SVC 경로로 전환 (2026-11-28, 사용자 결정: "SVC 경로로 바꿔서 다시 검증, landing pad 삭제")
프로젝트가 채택한 mediator 는 SVC + asm (`configDMA_MEDIATOR_SVC` 기본 1, `SECURE_dma_m2m_start_svc`)인데, 하네스는
검증 지시문("landing pad 유지")에 따라 Thread mode NSC + landing pad 를 쓰고 있었다 → 프로젝트 설계로 되돌림.

| # | 단계 | 내용 | 결과 |
|---|---|---|---|
| 28 | 3 | 삭제: `secure/secure_dma_entry.S`, `secure_dma_entry_min.S`, `secure_context_patch.md`, `src/` 의 `secure_context.c`/`secure_context_port.c` 사본. `secure_common.sh` 에서 `-DDMA_MEDIATOR_PAD -DSECSTACK_STATIC=1`, `MEDIATOR_S` 제거. 사본 `secure_nsc.c` 는 clang naked 수정(#17)만 남김 (원본과 4 줄 차이) | Secure context 는 원본 그대로 |
| 29 | 3 | `secure_dma_boot.c`: `g_pad_top`/`g_idle_psp` 와 (3) pad 설정 제거. `secure_guard_bind.c`/`pack_guards.py`/`check_pair.py`: "cluster 0 첫 guard = pad" 제외 규칙 제거 → guard 3 개 모두 Secure 데이터용 | guard 칸 2 → 3 |
| 30 | 2 | NS 사본 `app_freertos.c`: `App_DmaM2M` 가 원본의 `App_DmaStartSvc` (`svc #110`) 사용. `configDMA_MEDIATOR_SVC != 1` 이면 `#error` | — |
| 31 | 1/2 | **pass 가 inline asm 을 보지 않아** `App_DmaM2M` 가 인라인되면 sink 가 사라짐 → cluster 0 개, 2차 컴파일 실패. pass 에 `{"svc": N, "args": [레지스터]}` sink 추가 (`svcOf`: asm 문자열 `svc $k`/`svc #N`, 제약 `{rK}`·묶인 입력으로 레지스터→값), `trace()` 가 `ptrtoint` 를 벗김. 단위 시험 2 항목 추가 (`svc #110` 두 형태 배치, `svc #111` 무시) | sites = app_freertos.c:184 (인라인된 svc), cluster 3 개 그대로 |
| 32 | 5 | T2 stale: pad 제외가 없어지자 Secure 데이터가 cluster 0 guard(0x30018100)로 가고, 기존 shift(Task A cluster 확대)는 그 guard 를 옮기지 않음 → 진짜로 stale 이 아니어서 0 이 맞음. shift 버퍼를 공유 cluster(`dma:shared:vDmaTaskA,vDmaTaskB`)에 넣어 cluster 0 부터 밀리게 함 | check_pair -7, host -1 |
| 33 | 5 | T5 교체 (min mediator → SVC 경로 검사): Secure 에 pad 심볼 없음, NS 의 DMA 요청 10 곳 모두 `svc #110` 이고 NSC 호출은 `SVC_Handler` 안 한 곳, `__acle_se_SECURE_dma_m2m_start_svc`(44)·`SecureContext_Load/SaveContextAsm`(7/11) 명령열이 GCC baseline Secure ELF 와 동일 | PASS |

남은 차이 1 개: NS `SVC_Handler` → NSC 분기가 GCC 는 veneer `ldr.w pc,[pc]` (1 명령), lld 는 thunk `movw/movt/bx r12` (3 명령).
요청마다 1 회, ±1–2 cycle [추정] — 보드 측정(BOARD_TEST.md §7)으로 확인.

## cluster 앞 빈 칸 채움 (2026-11-28, 사용자 요청: "c0 의 빈 공간에 다른 NS 데이터를 넣을 수 있게")
| # | 내용 | 결과 |
|---|---|---|
| 34 | `gen_layout.py`: cluster 용량을 256 B → **32 B 단위**로, cluster 를 guard 쪽에 붙임(끝 = 256 B 경계 유지). 블록 안 cluster 앞 칸(`__dmac<i>_pre_start..start`)에 1차 링크 map 의 `.bss.<이름>` 객체를 FFD 로 배치 (`--fill-map`, 크기/정렬 우선 두 순서 중 나은 쪽), 2차 링크 `ASSERT` 로 크기 일치 확인, 앞 칸도 zero table 에 포함. `build_ns.sh`: `DMA_FILL=on|off`, `DMA_FILL_EXCLUDE` | c0 = [0x200180c0, 0x20018100) 64 B, 앞 칸 192/192 B = `s_privA`, `s_privB`. c1/c2 는 256 B 그대로(앞 칸 0). S-MPU RNR4 RBAR 0x200180c3, NS-MPU region {0x200180c0, 0x40} |
| 35 | 안전성 근거: 앞 칸은 S-MPU 태그 밖 → mediator TTT 거부 [아키텍처 도출]. owner 의 DMA NS-MPU region 은 cluster 만 덮음. DMA 주소 증가 방향이라 cluster 넘침은 guard 에 먼저 닿음 [논문 §3]. 공격 시험 attack[2] (A 자신의 globals = 이제 c0 바로 앞 s_privA) 가 −1 이어야 함 (보드 확인 항목) | T6: on/off 두 빌드 배치 검사 PASS |

## 보드 확인 (2026-11-28, STM32L562E-DK + STLINK-V3, 사용자 지시로 실행) — 스크립트: tests/board/
이미지: build/secure/secure.elf + build/ns/ns.elf (SVC 경로, DMA_FILL=on). 값은 모두 [실측].

| 단계 | 결과 | 판정 |
|---|---|---|
| 1 부팅 | `dma_boot_config()` = 0, `g_dma_boot_status` = 0. RNR4/5/6 = 200180c3/200180e1, 20018203/200182e1, 20018403/200184e1. MPCBB1 VCTR[12] = 0x2a | 기대와 같음 |
| 2 정상 DMA (60 s) | okCount A 122 (2.03/s), B 87 (1.45/s). dstA 00..07, dstB 80..87, shared 00..07 / +32 80..87, dmaStatus 0 | 같음 |
| 3 요청 시점 공격 | `attack = {0,-1,-1,-1,-1,-1}` ([2] = c0 앞 칸의 s_privA → 앞 칸이 DMA 대상 아님도 확인). SFSR 0, CFSR 0 | 같음 |
| 5 SVC 경로 | mediator 첫 명령에서 IPSR 0xb, MSP_S 0x30017f58 사용, PSP_S = 0 (DMA 태스크 Secure context 없음), 호출자 SVC_Handler. 실행 뒤 SFSR/CFSR 0 | 같음 |
| 4 쓰기 guard | CCR.EN 직전 CNDTR1 8→528, CPAR1 = NS flash 0x08040000. 결과: 260 B 전송 (CNDTR 잔여 268 = c1 256 B + guard 4 B), GTZC_IRQHandler 가 EN=0, SR3 지움. guard 너머 0x20018400 무변조, Secure 에서 본 guard 0 그대로 | 보호는 기대와 같음 |
| 4 쓰기 guard (이후) | **okCount A 7, B 6 에서 정지**. GTZC_IRQHandler 는 채널만 끄고 NS 통지·`s_busy` 해제가 없어 Task A 가 `ulTaskNotifyTake(portMAX_DELAY)` 에서 영구 대기(mutex 보유) → B 도 정지 | **BOARD_TEST 의 "계속 증가" 는 틀린 [추정]이었음.** 원본 secure_nsc.c 동작 |
| 4 읽기 guard | CPAR1 = guard 0x20018300, CNDTR1 16. 2 회 반복 동일: 8 B 전송(목적지에 0 기록 = RAZ), GTZC_IRQHandler 가 EN=0 (CNDTR 8), DMA ISR = 0x5 (GIF, HTIF) **TEIF 없음**, Task A 영구 대기. ISR 정지점을 둔 실행에서는 전송이 끝까지(16 B, TCIF) 간 뒤 GTZC → TC 경로로 복구됨 | Secure 데이터 유출 없음(RAZ), 채널 중단은 TZIC 로. **CLAUDE.md "Secure 블록 DMA 읽기 → bus error → TEIF" 와 다름 → 질문** |
| 6 stale | t_bind Secure + t_stale NS: Error_Handler 에서 정지, `g_dma_boot_status` = −7. 짝이 맞는 t_bind NS: 0, NonSecure_Init 도달 | 같음 |

### 사이클 (7단계) — GDB 정지점 2 개(svc, 다음 명령) 사이 DWT CYCCNT, warm-up 4 회 뒤 12 회
| 이미지 | 요청 1 회 (svc → 복귀) | 비고 |
|---|---|---|
| 이 빌드 (clang LTO -O2 NS, GCC -O0 Secure) | **428** (12/12 동일) | |
| baseline GCC (`./build.sh`, NS -O1, Secure -O0) | **543** (12/12 동일, 두 호출 지점 같음) | |
- 경로의 명령열은 Secure mediator·NS SVC_Handler 모두 같다 (T5). 다른 것은 NS → NSC 분기(GCC veneer 1 명령 vs lld thunk 3 명령)뿐인데 이 빌드가 115 cycle 적다.
- 구간 분해(정지점 추가, 절대값은 정지 때문에 커짐): 차이 전부가 `svc` → `SVC_Handler` 첫 명령(예외 진입) 구간 (236 vs 351). FPCA = 0 (두 이미지), ICACHE 는 두 이미지 모두 꺼져 있음(ICACHE_CR EN = 0, hit/miss 0). 원인 미확인 [추정: 예외 진입 중 벡터/스택 접근 또는 정지 재개 방식의 차이].
- 정지점 방식이라 논문 표 2 (코드 안 DWT, 32 회) 값과 직접 비교하지 않는다. 정밀 비교는 baseline 의 BENCH_PHASE 방식(코드 안 DWT)을 이 빌드에 옮겨야 한다.

### 메모리 오버헤드 (기존 수동 배치 `s_dmaTaskMem` 대비, ELF 심볼/섹션 크기) [실측]
| 항목 | baseline (수동 배치) | 이 빌드 (LLVM 자동 배치) |
|---|---|---|
| DMA 영역 | `s_dmaTaskMem[2]` 1536 B = 태스크마다 globals 256 + cluster 256 + guard 256 | `.dma_c0..2` 1536 B = c0 (앞 칸 192 + cluster 64 + guard 256), c1/c2 (cluster 256 + guard 256) |
| guard | 512 B (2 개) | 768 B (3 개: 공유 cluster 1 개 추가) |
| globals 패딩 (cluster 를 256 경계에 맞추려는 것) | 2 × 244 B = 488 B (globals 12 B 를 256 B 로) | 0 (globals 는 일반 .bss, 32 B 정렬 s_priv 2 × 24 B = 48 B) |
| 배열 정렬 패딩 (aligned(256)) | 152 B (s_dmaSrcA 끝 → s_dmaTaskMem) | 0 (cluster 가 NS SRAM 시작 0x20018000) |
| cluster 올림 낭비 | 0 | 0 (공유 64 B → 64 B, 앞 칸 192 B 는 s_privA/B 로 채움) |
| **보호 때문에 쓰는 SRAM 합계** | **1152 B** (guard 512 + 패딩 640) | **816 B** (guard 768 + 정렬 48). 공유 cluster 를 빼고 같은 2 cluster 로 보면 560 B |
| NS SRAM 전체 (.data + .bss + .dma_*) | 21312 B | 21112 B (−200 B, NS 컴파일러가 달라 참고값) |
| NS Flash 추가분 | — | manifest 172 + zero table 24 + `dma_zero_clusters` 76 = **+272 B** (NS 전체 Flash 34120 → 38776 B 는 GCC -O1 vs clang -O2 LTO 차이가 섞여 비교 불가) |
| Secure Flash (둘 다 GCC -O0) | .text 11208 B | 12016 B (**+808 B**: dma_boot_config 804 + prvSetTag 64 + prvMaxTags 44 + guard_bind_init 88 − SMPU_SetupTaskRegions/OneTaskRegion 212 + main 20) |
| Secure SRAM | .data 24 / .bss 10448 | .data 32 (+8: g_dma_boot_status 등) / .bss 10448 |
| S-MPU 태그 / MPCBB guard 블록 | 2 / 2 | 3 / 3 |

## 공유 cluster 제거 + guard 위반 복구 (2026-11-28, 사용자 결정 D8/D9: "remove the shared cluster, insert the recovery code on GTZC_IRQHandler")
- **D9 공유 cluster 제거** (D3 결정을 뒤집음). `app_freertos.c` 사본에서 `s_dmaShared` 와 두 태스크의 공유 전송을 뺐다.
  pass 의 `dma:shared:` 지원은 그대로 (T3 too_many 가 계속 쓴다). `DMA_TEST_SHIFT` 는 `dma:vDmaTaskA` 로 바꿔 c0 을 키운다.
  - 결과 [빌드]: cluster 2 개, c0 = Task A [0x20018000,0x20018100), c1 = Task B [0x20018200,0x20018300), guard 0x20018100 / 0x20018300.
    두 cluster 모두 256 B 이므로 앞 칸 없음. DMA 태스크 NS-MPU configurable region 2/3 (globals + 전용 cluster).
  - T6 은 기본 빌드(앞 칸 없음)와 annotate_ok 변형(c1 = 304 B → cap 320 B, 앞 칸 192 B = s_privA, s_privB)으로 나눠 확인.
- **D8 GTZC_IRQHandler 복구** (`secure_nsc.c` 사본). 진입 시 CCR 을 읽어 EN = 0, SRAM1F 지움은 원본 그대로.
  진입 시 EN == 1 이고 s_busy == 1 이면: CGIF1 지움 → s_busy = 0 → NS status word = 2 → doorbell pend.
  - EN 조건 이유: SVC mediator 는 PRIMASK 를 쓰지 않으므로 `s_busy = 1` 과 `EN = 1` 사이에 다른 원인의 TZIC 가 끼면 채널 소유를 뺏게 된다 [아키텍처 도출].
  - 남은 경합 [추정]: DMA ISR(우선순위 5) 가 TC 를 읽은 뒤 EN 을 끄기 전에 GTZC(0) 가 끼면 doorbell 이 두 번 pend 되고 status 가 0 으로 덮인다.
    guard 접근이 마지막 beat 와 같은 경우뿐이고 보호(쓰기 무시·채널 중단)에는 영향 없음, 보고값만 틀린다. DMA ISR 은 측정 경로(ISR_TS)라 고치지 않았다.
  - NS: `App_DmaM2M` 이 완료 status != 0 이면 -2 반환 (dmaStatus 1 = TEIF, 2 = guard 위반).
  - mediator / Secure context 명령열은 그대로 (T5: baseline 과 동일 44 / 7 / 11).
  - 보드: 아직 다시 확인하지 않음. 기대값은 BOARD_TEST.md 4단계 (dmaStatus = 2, s_busy = 0, okCount 계속 증가).
- 메모리 (개정 이미지) [빌드 실측]:

| 항목 | baseline (수동 배치) | 이전 (공유 cluster) | 개정 |
|---|---|---|---|
| guard | 512 B | 768 B | **512 B** |
| 패딩 | 640 B | 48 B | 48 B (s_priv 32 B 정렬) |
| 보호에 드는 NS SRAM | 1152 B | 816 B | **560 B** |
| NS Flash 추가 | — | +272 B | **+264 B** (manifest 172 + zero table 16 + dma_zero_clusters 76) |
| Secure .text | 11208 B | 12016 B (+808) | **12080 B (+872)** = +808 부팅 코드 + 64 GTZC 복구 |
| S-MPU 태그 / guard 블록 | 2 / 2 | 3 / 3 | 2 / 2 |

- Secure Flash +808 B 내역 (GCC -O0, `objdump -dl` 줄 단위): dma_boot_config 802 B = 엔트리 복사+범위 검사 272, MPCBB 이중 루프 142,
  겹침 검사 134, S-MPU 루프 116, 헤더 검사 64, 프레임 50, guard_bind 호출 24. 같은 파일을 -O1 / -Os 로 빌드하면 456 / 408 B [실측: 오브젝트 크기].

## Task A struct + Task B cluster 끝 공유 버퍼 (2026-11-28, 사용자 결정 D10)
사용자 요구: 공유 DMA 버퍼 64 B 를 guard 추가 없이 Task B guard 바로 앞(②)에 두고 Task B 의 S-MPU 태그로 덮는다.
Task A 는 globals 를 c0 앞에 붙여 NS-MPU region 2 개로 하되, 붙는 이유는 배치 도구가 아니라 struct 여야 한다.
- 앱 (`app_freertos.c` 사본): `static struct { dma_task_priv_t priv; uint8_t dst[256]; } s_taskA;` (priv 96 B, dst 오프셋 96),
  `s_dmaShared[64]` 을 A (n%4==0) 와 B (n%4==2, +32) 가 씀. 공격 시험 8 개 (+[6] 공유 정상, [7] 공유 끝이 guard B).
- pass (`DmaCluster.cpp`):
  - 구조체 전역의 필드 판정: `offsetInto()` 가 상수 GEP / 가변 인덱스 GEP(상수 앞부분) 에서 G 기준 오프셋 → 최상위 필드.
    DMA 필드 중 가장 앞 필드의 오프셋 = head (32 B 배수 아니면 error `struct-field-align`). head 를 가진 구조체는
    `.dma.h.<task>`, 뒤에 DMA 에 안 쓰는 필드가 있으면 warning `struct-field-tail`. 한 cluster 에 head 둘이면 error.
    이전 동작 (구조체 전체를 태그 안에 넣음) 이면 같은 struct 의 globals 가 DMA 대상이 되어 attack[2] 가 통과했을 것 [아키텍처 도출].
  - 설정 `shared_host` ({"vDmaTaskA,vDmaTaskB": "vDmaTaskB"}): 그 owner 집합의 공유 버퍼는 별도 cluster 없이
    `.dma.x.<host>` 로 host cluster 끝에. JSON 에 head / body_bound / hosted_bound / hosted_owners.
- `gen_layout.py`: 블록 = [pre .bss][head][태그: 전용 버퍼 … 끝에 공유 버퍼(guard 바로 앞, 32 B 단위 칸)][guard].
  NS-MPU region: host owner = {obj, head+cap}, 다른 owner = {shr, 공유 칸}. S-MPU (manifest) = {start, end} (head 제외).
- `check_layout.py`: head 구조체 시작 = obj, 시작+head = 태그 시작, 공유 버퍼 ⊂ [shr, end), 공유 region 개수 확인.
- 결과 [빌드]: c0 obj 0x200180a0 (s_taskA), 태그 [0x20018100, 0x20018200), guard A 0x20018200;
  c1 앞 칸 s_privB 0x20018300, 태그 [0x200183c0, 0x20018500) = s_dmaDstB + s_dmaShared@0x200184c0, guard B 0x20018500.
  NS-MPU: A {0x200180a0,0x160}+{0x200184c0,0x40}, B {s_privB,0x60}+{0x200183c0,0x140} → 둘 다 2/3. S-MPU 2, guard 2.
- 요청 시점 기대 [아키텍처 도출]: attack = {0,-1,-1,-1,-1,-1,0,-1}. [2] 는 TTAT 통과 (struct 전체가 A 의 region) → TTT 거부.
- 남는 성질 [아키텍처 도출]: Task B 의 s_dmaDstB 에서 넘친 DMA 는 공유 버퍼에 닿는다 (guard 사이에 없음). B 는 원래 공유 버퍼
  쓰기 권한이 있으므로 권한 밖 쓰기는 아니다. Task A 가 공유 버퍼에서 넘치면 guard B.
- 메모리 [빌드 실측]: guard 512 B, 앞 칸 미사용 27 B (c0 160 중 133) + 96 B (c1 192 중 96) → 보호 관련 NS SRAM 635 B
  (수동 배치 1152 B). NS Flash +264 B, Secure .text 12080 B (D8/D9 와 같음: Secure 코드 변경 없음).
- 시험: pass 단위 19 항목 (struct head 상수/가변 인덱스, shared_host 추가), T6 을 새 배치(main / annotate_ok / fill off)로. 보드: 미확인.

## 시험 요약 (마지막 실행: 처음부터 `rm -rf build && build_all.sh && tests/run_tests.sh`, SVC 경로)
- 1단계 플러그인: OK (LLVM 18.1.8). 2단계 NS: OK (1/2차 JSON 동일, check_layout OK). 3단계 Secure: OK (veneer 16 개 동일, check_pair 0).
- 4단계 보드: **실행함** (2026-11-28, 위 "보드 확인"). 1·2·3·5·6 기대와 같음, 4 는 보호 동작 확인 + 기대와 다른 점 2 개 (중단 뒤 복구 없음, 읽기에서 TEIF 없음).
- 5단계 + 개선 시험: `tests/run_tests.sh` PASS 21 / FAIL 0 (D10 개정 후, pass 단위 19 항목).
- GCC baseline: `./build.sh` 그대로 (프로젝트 원본 파일 변경 없음. 작업 전부터 있던 `secure_context.c` 공백 수정 1 줄은 건드리지 않음).

## 결정이 필요한 항목 (사용자)
- ~~D1 mediator 오버헤드~~, ~~D2 컨텍스트 전환 오버헤드~~ — SVC 경로 전환(#28–#33)으로 해소: mediator·Secure context 명령열이 baseline 과 동일.
- ~~D3 NS-MPU region~~ — 사용자 결정 (2026-11-28): 공유 cluster 는 의도된 동작 → 이후 D9 에서 공유 cluster 제거로 바뀜 (region 2/3).
- ~~D4 보드~~ — 사용자 지시로 실행 (위 "보드 확인").
- **D7 guard 읽기에서 TEIF 없음.** [실측] NS DMA 의 guard(Secure 블록) 읽기는 bus error 없이 0 을 읽고 TZIC SR3 만 선다. CLAUDE.md 확정 사실과 다름 → 어떤 조건에서 TEIF 를 봤는지 확인 필요.
- ~~D8 guard 위반 뒤 복구~~ — 사용자 결정 (2026-11-28): 복구 넣음 (위 "공유 cluster 제거 + guard 위반 복구"). 원래 내용: GTZC_IRQHandler 가 채널만 끄므로 요청 태스크가 영구 대기, 채널 mutex 때문에 다른 DMA 태스크도 정지. 논문 범위(채널 중단)는 충족. 복구(예: s_busy 해제 + 실패 통지)를 넣을지 결정 필요 (원본 secure_nsc.c 변경 = 측정 경로 밖 ISR).
- **D5 커밋 규칙.** "~/dma-tz 안 파일 수정 금지" 지시에 따라 docs/CHANGES.md, docs/gap-analysis.md 에는 기록하지 않았다 (이 로그에만).
- **D6 툴체인.** LLVM 18 은 sudo 없이 `~/opt/root18` 에 풀어 썼다. 정식 설치(apt.llvm.org 또는 `apt install llvm-18-dev lld-18`)를 원하면 env_llvm18.sh 가 자동으로 그쪽을 쓴다.
