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
