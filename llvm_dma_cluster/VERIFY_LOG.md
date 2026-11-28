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
| 25 | `gen_layout.py`: cluster 별 bound → cap 표 출력 | 현재 미사용 192 B (공유 64 B cluster 의 256 B 올림), guard 768 B |
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

## 시험 요약 (마지막 실행: 처음부터 `rm -rf build && build_all.sh && tests/run_tests.sh`, SVC 경로)
- 1단계 플러그인: OK (LLVM 18.1.8). 2단계 NS: OK (1/2차 JSON 동일, check_layout OK). 3단계 Secure: OK (veneer 16 개 동일, check_pair 0).
- 4단계 보드: **실행 안 함** (D4). 절차·기대값: BOARD_TEST.md (SVC 경로 주소로 갱신).
- 5단계 + 개선 시험: `tests/run_tests.sh` PASS 18 / FAIL 0.
- GCC baseline: `./build.sh` 그대로 (프로젝트 원본 파일 변경 없음. 작업 전부터 있던 `secure_context.c` 공백 수정 1 줄은 건드리지 않음).

## 결정이 필요한 항목 (사용자)
- ~~D1 mediator 오버헤드~~, ~~D2 컨텍스트 전환 오버헤드~~ — SVC 경로 전환(#28–#33)으로 해소: mediator·Secure context 명령열이 baseline 과 동일.
- **D3 NS-MPU region.** 공유 cluster 때문에 DMA 태스크가 configurable region 3/3 을 쓴다 (baseline 2/3). CLAUDE.md 의 "NS-MPU 영역 추가 사용 금지" 와 충돌.
  공유 버퍼를 빼면 2/3 으로 돌아간다.
- **D4 보드.** STLINK-V3 가 연결되어 있지만 CLAUDE.md 는 보드 실행을 사용자 몫으로 정하고 있고, 이 이미지들은 한 번도 실행된 적이 없어 올리지 않았다.
  BOARD_TEST.md 절차로 직접 확인하거나, 올려도 된다고 하면 그 절차대로 진행한다.
- **D5 커밋 규칙.** "~/dma-tz 안 파일 수정 금지" 지시에 따라 docs/CHANGES.md, docs/gap-analysis.md 에는 기록하지 않았다 (이 로그에만).
- **D6 툴체인.** LLVM 18 은 sudo 없이 `~/opt/root18` 에 풀어 썼다. 정식 설치(apt.llvm.org 또는 `apt install llvm-18-dev lld-18`)를 원하면 env_llvm18.sh 가 자동으로 그쪽을 쓴다.
