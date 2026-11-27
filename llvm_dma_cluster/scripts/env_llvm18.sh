# source scripts/env_llvm18.sh — LLVM 18 / lld 18 / cmake.
# 시스템 apt 에 llvm-18 이 설치되어 있으면 그것을, 없으면 sudo 없이 풀어 둔 로컬 prefix
# (apt download + dpkg -x, LLVM18_PREFIX 기본 ~/opt/root18) 를 쓴다.
if [ -x /usr/lib/llvm-18/bin/ld.lld ]; then
  export LLVM=/usr/lib/llvm-18/bin
  export LLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
else
  _P=${LLVM18_PREFIX:-$HOME/opt/root18}
  export LLVM=$_P/usr/lib/llvm-18/bin
  export LLVM_DIR=$_P/usr/lib/llvm-18/lib/cmake/llvm
  export LD_LIBRARY_PATH=$_P/usr/lib/x86_64-linux-gnu:$_P/usr/lib/llvm-18/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
  export PATH=$_P/usr/bin:$PATH
  export CMAKE_ROOT=$_P/usr/share/cmake-3.31
  unset _P
fi
