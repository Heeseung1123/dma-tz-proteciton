#!/usr/bin/env python3
"""check_pair.py secure.elf ns.elf — 보드에 올리기 전에 Secure/NS 이미지 짝을 정적으로 검사한다.

secure_dma_boot.c 의 dma_boot_config() 와 secure_guard_bind.c 의 guard_bind_init() 이 부팅 때
할 검사를 ELF 만으로 재현해, 보드에서 나올 반환값(0, -1 … -7)을 미리 알려 준다.
  - manifest: ns.elf 의 .dma_manifest (DMA_MANIFEST_NS_ADDR 에 있어야 함)
  - Secure 데이터가 놓인 guard: secure.elf 의 .gbind<k> 섹션 주소 (pack_guards.py 생성)
같은 상수는 secure_dma_boot.c 와 맞춰야 한다 (TAG_RNR0, NS_SRAM_LO/HI, DREGION=8: STM32L5 S-MPU).
종료 코드: 기대 반환값이 0 이면 0, 아니면 1.  --expect N 으로 다른 값을 기대할 수 있다 (음성 시험).
"""
import argparse
import struct
import sys

from elftools.elf.elffile import ELFFile

MAGIC, VERSION, MAXN = 0x444D414D, 1, 8
MANIFEST_ADDR = 0x08040400
TAG_RNR0, DREGION = 4, 8
NS_SRAM_LO, NS_SRAM_HI = 0x20018000, 0x20040000
SEC_ALIAS = 0x10000000


def load(path):
    elf = ELFFile(open(path, "rb"))
    syms = {}
    st = elf.get_section_by_name(".symtab")
    for s in st.iter_symbols():
        if s.name:
            syms.setdefault(s.name, (s["st_value"], s["st_size"]))
    return elf, syms


def read(elf, addr, n):
    for s in elf.iter_sections():
        if s["sh_type"] != "SHT_NOBITS" and s["sh_flags"] & 2 and s["sh_addr"] <= addr < s["sh_addr"] + s["sh_size"]:
            o = addr - s["sh_addr"]
            return s.data()[o:o + n]
    return None


def boot(m, bind):
    """dma_boot_config() 재현. m = manifest bytes, bind = k_guard_bind_ns 목록. (rc, 설명)"""
    if m is None:
        return -1, f"manifest @ {MANIFEST_ADDR:#x} 없음 (NS 가 이 주소에 .dma_manifest 를 두지 않음)"
    magic, ver, n = struct.unpack_from("<III", m, 0)
    if magic != MAGIC or ver != VERSION:
        return -1, f"magic/version {magic:#x}/{ver}"
    if n == 0 or n > MAXN or n > DREGION - TAG_RNR0:
        return -2, f"count {n} (S-MPU 태그 가능 {DREGION - TAG_RNR0})"
    e = [struct.unpack_from("<IIIII", m, 12 + 20 * i) for i in range(n)]
    for i, (_id, _k, s, g, gb) in enumerate(e):
        ge = (g + 256 * gb) & 0xFFFFFFFF
        if (s & 31) or (g & 255) or s >= g or gb == 0:
            return -3, f"e[{i}] 정렬/크기 (start {s:#x}, end {g:#x}, guard_blocks {gb})"
        if s < NS_SRAM_LO or ge > NS_SRAM_HI or ge < g:
            return -4, f"e[{i}] NS SRAM 밖 [{s:#x},{ge:#x})"
        for j in range(i):
            s2, ge2 = e[j][2], e[j][3] + 256 * e[j][4]
            if s < ge2 and s2 < ge:
                return -5, f"e[{i}] 와 e[{j}] 겹침"
    blocks = {e[i][3] + 256 * k for i in range(n) for k in range(e[i][4])}
    for a in bind:
        if a not in blocks:
            return -7, f"Secure 가 guard {a:#x} 에 데이터를 두었는데 NS manifest 에 그 guard 가 없음 (이미지 불일치)"
    return 0, f"count {n}, guard 결합 {len(bind)} 블록"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("secure_elf")
    ap.add_argument("ns_elf")
    ap.add_argument("--expect", type=int, default=0)
    a = ap.parse_args()
    selt, ssym = load(a.secure_elf)
    nelf, _ = load(a.ns_elf)
    # Secure 데이터가 실제로 놓인 곳 = secure.elf 의 .gbind<k> 출력 섹션 (pack_guards.py 생성, 비어 있지 않은 것).
    # (k_guard_bind_ns[] 는 GCC 가 상수 접기로 심볼 없이 없앨 수 있어 기준으로 쓰지 않는다)
    bind = sorted(s["sh_addr"] - SEC_ALIAS for s in selt.iter_sections()
                  if s.name.startswith(".gbind") and s.name[6:].isdigit() and s["sh_size"] > 0)
    rc, why = boot(read(nelf, MANIFEST_ADDR, 12 + 20 * MAXN), bind)
    print(f"[check_pair] dma_boot_config() 예상 = {rc}: {why}")
    if rc != a.expect:
        print(f"[check_pair] FAIL: 기대 {a.expect}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
