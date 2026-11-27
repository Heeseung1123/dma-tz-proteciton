cmake_minimum_required(VERSION 3.20)
project(DmaCluster CXX)

# lld 와 "같은" LLVM 설치를 가리켜야 한다 (ABI 불일치 시 플러그인 로드 실패)
#   cmake -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm ...
find_package(LLVM 18 REQUIRED CONFIG)
message(STATUS "LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFS NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFS})
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# DmaCluster.so        : NS 이미지, ld.lld Full LTO 에서 실행 (DMA cluster 배치)
add_llvm_pass_plugin(DmaCluster DmaCluster.cpp)
# SecureGuardSelect.so : Secure 이미지, opt 로 분석만 (guard 에 옮길 Secure 데이터 선택)
add_llvm_pass_plugin(SecureGuardSelect SecureGuardSelect.cpp)
