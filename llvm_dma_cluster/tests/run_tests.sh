#!/usr/bin/env bash
# run_tests.sh — 5단계 음성/양성 빌드 시험 (보드 없이). 본 빌드(build/)는 건드리지 않고 build/t_* 에 만든다.
#   전제: scripts/build_all.sh 가 한 번 성공 (build/pass, build/secure0)
set -uo pipefail
TOOLS=$(cd "$(dirname "$0")/.." && pwd)
source "$TOOLS/scripts/env_llvm18.sh"; export LLVM LLVM_DIR
B=$TOOLS/build
ROOT=$(cd "$TOOLS/.." && pwd)
export IMPLIB=$B/secure0/secure_nsclib.o
PASS=0; FAILN=0
ok()   { echo "PASS  $1"; PASS=$((PASS+1)); }
bad()  { echo "FAIL  $1"; FAILN=$((FAILN+1)); }

ns_build() {   # $1 = 이름, $2 = 추가 -D → 0/1, 로그 build/t_$1/ns.log
  mkdir -p "$B/t_$1"
  OUT="$B/t_$1/ns" EXTRA_DEFS="-DDMA_ATTACK_TESTS $2" bash "$TOOLS/scripts/build_ns.sh" > "$B/t_$1/ns.log" 2>&1
}
must_fail() {  # $1 이름, $2 -D, $3 로그에 있어야 할 문자열(grep -E)
  if ns_build "$1" "$2"; then bad "$1: 빌드가 성공함 (실패해야 함)"
  elif grep -qE "$3" "$B/t_$1/ns.log"; then ok "$1: 빌드 실패, 진단 /$3/"
  else bad "$1: 실패했지만 기대 진단 /$3/ 없음 (build/t_$1/ns.log)"; fi
}
manifest_args() {  # ns.elf → "start end gb ..." (host_guard_bind 인자)
  python3 - "$1" <<'EOF'
import struct, sys
from elftools.elf.elffile import ELFFile
d = ELFFile(open(sys.argv[1], "rb")).get_section_by_name(".dma_manifest").data()
n = struct.unpack_from("<I", d, 8)[0]
print(" ".join(f"{s:#x} {e:#x} {g}" for _i, _k, s, e, g in (struct.unpack_from("<IIIII", d, 12 + 20 * i) for i in range(n))))
EOF
}

echo "== T0 pass 단위 시험 (opt)"
if bash "$TOOLS/tests/pass/run.sh" > "$B/t_pass.log" 2>&1; then ok "pass 단위: $(grep -c '^ok' "$B/t_pass.log") 항목"
else bad "pass 단위 (build/t_pass.log)"; grep FAIL "$B/t_pass.log"; fi

echo "== T1 guard 결합 변형: --include-possible 로 Secure 데이터를 guard 에 배치한 전체 빌드"
if OUT_ROOT=$B/t_bind PACK_ARGS=--include-possible bash "$TOOLS/scripts/build_all.sh" > "$B/t_bind.log" 2>&1; then
  ok "t_bind: 전체 빌드 + veneer 동일 + check_pair 0"
  grep -A12 '^guard 블록' "$B/t_bind.log" | sed 's/^/      /'
else bad "t_bind: 전체 빌드 실패 (build/t_bind.log)"; fi

echo "== T2 stale NS 이미지: NS 만 배치를 바꿔 다시 빌드 → Secure(t_bind) 와 짝이 안 맞음 → -7"
if ns_build stale "-DDMA_TEST_SHIFT"; then
  python3 "$TOOLS/scripts/check_pair.py" "$B/t_bind/secure/secure.elf" "$B/t_stale/ns/ns.elf" --expect -7 \
    && ok "stale: check_pair 예상 -7" || bad "stale: check_pair 가 -7 을 예상하지 않음"
  HB=$B/host_guard_bind_tbind
  gcc -O0 -Wall -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -I"$TOOLS/common" -I"$B/t_bind/secure/gen" "$TOOLS/tests/host_guard_bind.c" \
      "$TOOLS/secure/secure_guard_bind.c" -o "$HB" || bad "host_guard_bind 컴파일 실패"
  # shellcheck disable=SC2046
  if "$HB" $(manifest_args "$B/t_bind/ns/ns.elf"); then ok "host: 짝이 맞는 manifest → guard_bind_init 0"
  else bad "host: 짝이 맞는 manifest 인데 실패"; fi
  # shellcheck disable=SC2046
  if "$HB" $(manifest_args "$B/t_stale/ns/ns.elf"); then bad "host: stale manifest 인데 guard_bind_init 이 0"
  else ok "host: stale manifest → guard_bind_init -1 (dma_boot_config → -7, Error_Handler)"; fi
else bad "stale: NS 빌드 실패 (build/t_stale/ns.log)"; fi

echo "== T3 배치할 수 없는 버퍼는 빌드 실패"
must_fail bad_init  "-DDMA_TEST_BAD_INIT"  "annotated-unplaceable.*초기값"
must_fail bad_task  "-DDMA_TEST_BAD_TASK"  "annotated-unplaceable.*vNoSuchTask"
must_fail stack_buf "-DDMA_TEST_STACK_BUF" "stack-buffer"
must_fail too_many  "-DDMA_TEST_TOO_MANY"  "S-MPU 태그 가능 수"
if REGION_USERS=- ns_build stale_regions ""; then bad "stale_regions: dma_regions.h 미반영인데 성공"
elif grep -q "NS-MPU region .* 0 개" "$B/t_stale_regions/ns.log"; then ok "stale_regions: check_layout 이 xRegions 미반영을 잡음"
else bad "stale_regions: 기대 진단 없음"; fi
must_fail oversize  "-DDMA_TEST_OVERSIZE"  "will not fit in region|overflow"

echo "== T4 annotate 양성"
if ns_build annotate_ok "-DDMA_TEST_ANNOTATE_OK"; then
  python3 - "$B/t_annotate_ok/ns/gen/layout_final.json" <<'EOF' && ok "annotate_ok: s_testAnnB → t.vDmaTaskB" || bad "annotate_ok: 배치 틀림"
import json, sys
L = json.load(open(sys.argv[1]))
c = {x["id"]: x for x in L["clusters"]}["t.vDmaTaskB"]
assert any(b["name"] == "s_testAnnB" for b in c["buffers"]) and c["owners"] == ["vDmaTaskB"]
EOF
else bad "annotate_ok: 빌드 실패"; fi
if ns_build override "-DDMA_TEST_OVERRIDE"; then
  python3 - "$B/t_override/ns/gen/layout_final.json" <<'EOF' && ok "override: owner = vDmaTaskB 만 + annotate-override 경고" || bad "override: owner 오염"
import json, sys
L = json.load(open(sys.argv[1]))
c = {x["id"]: x for x in L["clusters"]}["t.vDmaTaskB"]
assert any(b["name"] == "s_testOvr" for b in c["buffers"]) and c["owners"] == ["vDmaTaskB"], c
assert any(d["kind"] == "annotate-override" for d in L["diagnostics"])
EOF
else bad "override: 빌드 실패"; fi

echo "== T5 SVC mediator 경로 (landing pad 없음): 생성 이미지 검사"
OD="$LLVM/llvm-objdump -d --no-show-raw-insn"; S=$B/secure/secure.elf; N=$B/ns/ns.elf
insns() { $OD --disassemble-symbols="$1" "$2" 2>/dev/null | grep -E '^\s+[0-9a-f]+:' | cut -f2- | sed 's/0x[0-9a-f]*//g'; }
if "$LLVM/llvm-nm" "$S" | grep -qE ' (g_pad_top|g_idle_psp)$'; then bad "svc: Secure 에 landing pad 심볼이 남음"
else ok "svc: Secure 에 g_pad_top/g_idle_psp 없음"; fi
CALLERS=$($OD "$N" | awk '/^[0-9a-f]+ <.*>:$/{f=$2} /bl\s.*SECURE_dma_m2m_start/{print f}' | sort -u | tr '\n' ' ')
NSVC=$($OD "$N" | grep -cE 'svc\s+#0x6e')
if [ -n "$CALLERS" ] && [ -z "$(echo "$CALLERS" | tr ' ' '\n' | grep -vxE '<(SVC_Handler|dma_m2m_start_svc)>:|')" ] && [ "$NSVC" -gt 0 ]; then ok "svc: NS 의 DMA 요청 $NSVC 곳 모두 svc #110, mediator NSC 호출은 SVC_Handler(dma_m2m_start_svc 레이블) 에서만"
else bad "svc: NSC 호출자 [$CALLERS], svc #110 $NSVC 곳"; fi
SB=$(ls "$ROOT"/Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/STM32CubeIDE/Secure/Debug/FreeRTOS_SecureIOToggle_TrustZone_Secure.elf 2>/dev/null || true)
if [ -n "$SB" ]; then
  for f in __acle_se_SECURE_dma_m2m_start_svc SecureContext_LoadContextAsm SecureContext_SaveContextAsm; do
    if [ -n "$(insns $f "$S")" ] && diff <(insns $f "$SB") <(insns $f "$S") > /dev/null; then ok "svc: $f = GCC baseline 과 같은 명령열 ($(insns $f "$S" | wc -l) 개)"
    else bad "svc: $f 가 GCC baseline 과 다름"; fi
  done
else echo "SKIP  GCC baseline Secure ELF 없음 (./build.sh 후 다시)"; fi

echo "== T6 cluster 앞 빈 칸 채움 (DMA_FILL): 오른쪽 정렬 cluster + 앞 칸 NS 데이터"
# 기본 빌드: cluster 2 개 모두 256 B → 앞 칸 없음. 앞 칸은 annotate_ok 변형 (c1 = 304 B → cap 320 B) 으로 본다.
fill_check() {  # $1 ns.elf, $2 모드 (main | on | off)
  python3 - "$1" "$2" <<'EOF2'
import sys
from elftools.elf.elffile import ELFFile
e = ELFFile(open(sys.argv[1], "rb")); mode = sys.argv[2]
S = {s.name: (s["st_value"], s["st_size"]) for s in e.get_section_by_name(".symtab").iter_symbols() if s.name}
DMA = {"s_dmaDstA", "s_dmaDstB", "s_testAnnB"}
def cl(c): return tuple(S[f"__dmac{c}_{k}"][0] for k in ("pre_start", "pre_end", "start", "end"))
def objs(lo, hi): return sorted(n for n, (v, z) in S.items() if z and lo <= v < hi and not n.startswith("__"))
for c in (0, 1):
    ps, pe, cs, ce = cl(c)
    assert set(objs(cs, ce)) <= DMA and objs(cs, ce), (c, objs(cs, ce))
    assert not (set(objs(ps, cs)) & DMA), (c, objs(ps, cs))
assert cl(0) == (0x20018000, 0x20018000, 0x20018000, 0x20018100), [hex(x) for x in cl(0)]
ps, pe, cs, ce = cl(1)
if mode == "main":
    assert (ps, pe, cs, ce) == (0x20018200, 0x20018200, 0x20018200, 0x20018300), [hex(x) for x in cl(1)]
else:
    assert (ps, cs, ce) == (0x20018200, 0x200182c0, 0x20018400), [hex(x) for x in cl(1)]
    if mode == "on": assert objs(ps, cs) and pe > ps, objs(ps, cs)
    else:            assert pe == ps and not objs(ps, cs), objs(ps, cs)
    print("      c1 앞 칸:", objs(ps, cs), " cluster:", objs(cs, ce))
EOF2
}
fill_check "$B/ns/ns.elf" main && ok "fill main: c0/c1 = 256 B, 앞 칸 없음, cluster 안은 DMA 버퍼만" || bad "fill main: 배치 틀림"
if [ -f "$B/t_annotate_ok/ns/ns.elf" ]; then
  fill_check "$B/t_annotate_ok/ns/ns.elf" on && ok "fill on: c1 = [0x200182c0,0x20018400), 앞 칸 192 B 에 NS .bss (DMA 버퍼 아님)" || bad "fill on: 배치 틀림"
else bad "fill on: annotate_ok 빌드 없음"; fi
if DMA_FILL=off ns_build fill_off "-DDMA_TEST_ANNOTATE_OK"; then
  fill_check "$B/t_fill_off/ns/ns.elf" off && ok "fill off: 앞 칸 비움, cluster 위치 동일" || bad "fill off: 배치 틀림"
else bad "fill off: 빌드 실패 (build/t_fill_off/ns.log)"; fi

echo "== 결과: PASS $PASS / FAIL $FAILN"
[ "$FAILN" -eq 0 ]
