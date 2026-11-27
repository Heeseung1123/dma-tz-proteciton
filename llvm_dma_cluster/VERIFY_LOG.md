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
