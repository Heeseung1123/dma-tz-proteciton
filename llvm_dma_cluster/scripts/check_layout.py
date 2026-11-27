#!/usr/bin/env python3
"""ns.elf 정적 검증: cluster/guard 정렬·겹침, 버퍼 포함 관계, manifest 값 일치
   사용: check_layout.py ns.elf layout_final.json   (pip install pyelftools)"""
import json
import struct
import sys

from elftools.elf.elffile import ELFFile

NS_SRAM = (0x20018000, 0x20040000)


def main() -> int:
    elf = ELFFile(open(sys.argv[1], "rb"))
    lay = json.load(open(sys.argv[2]))
    syms = {s.name: (s["st_value"], s["st_size"])
            for s in elf.get_section_by_name(".symtab").iter_symbols() if s.name}
    bad = []

    def addr(name):
        if name in syms:
            return syms[name]
        for k, v in syms.items():              # LTO 가 internal 심볼에 접미사를 붙인 경우
            if k.startswith(name + "."):
                return v
        return None

    spans = []
    for i, c in enumerate(lay["clusters"]):
        s = syms[f"__dmac{i}_start"][0]
        e = syms[f"__dmac{i}_end"][0]
        g = syms[f"__dmac{i}_guard_end"][0]
        if s % 32: bad.append(f"c{i}: start {s:#x} 32B 비정렬")
        if e % 256: bad.append(f"c{i}: guard {e:#x} 256B 비정렬")
        if not (NS_SRAM[0] <= s and g <= NS_SRAM[1]): bad.append(f"c{i}: NS SRAM 밖")
        spans.append((s, g, c["id"]))
        for b in c["buffers"]:
            a = addr(b["name"])
            if a is None:
                bad.append(f"c{i}: 심볼 {b['name']} 없음")
            elif not (s <= a[0] and a[0] + b["size"] <= e):
                bad.append(f"c{i}: {b['name']} @ {a[0]:#x} 가 [{s:#x},{e:#x}) 밖")
        print(f"c{i} {c['id']:20s} cluster [{s:#010x},{e:#010x}) guard [{e:#010x},{g:#010x})")

    spans.sort()
    for (s1, g1, n1), (s2, g2, n2) in zip(spans, spans[1:]):
        if s2 < g1: bad.append(f"{n1} 와 {n2} 겹침")

    # cluster 가 아닌 심볼이 guard 안에 있으면 안 된다
    for name, (a, size) in syms.items():
        for i, c in enumerate(lay["clusters"]):
            e = syms[f"__dmac{i}_end"][0]; g = syms[f"__dmac{i}_guard_end"][0]
            if e <= a < g and not name.startswith("__dmac"):
                bad.append(f"guard c{i} 안에 심볼 {name} @ {a:#x}")

    # 태스크 NS-MPU region (dma_regions.h → TaskParameters_t.xRegions) 이 실제 이미지에 들어갔는지:
    # {cluster start, cap} 쌍이 읽기 전용 데이터에 owner 수만큼 있어야 한다 (dma_regions.h 를 쓰는 파일을
    # 재컴파일하지 않았거나 placeholder 가 남으면 0 개).
    ro = b"".join(x.data() for x in elf.iter_sections()
                  if x["sh_type"] == "SHT_PROGBITS" and (x["sh_flags"] & 2) and not (x["sh_flags"] & 1))
    words = struct.unpack_from(f"<{len(ro) // 4}I", ro[: len(ro) // 4 * 4])
    for i, c in enumerate(lay["clusters"]):
        s = syms[f"__dmac{i}_start"][0]
        cap = syms[f"__dmac{i}_end"][0] - s
        n = sum(1 for k in range(len(words) - 1) if words[k] == s and words[k + 1] == cap)
        if n != len(c["owners"]):
            bad.append(f"c{i}: NS-MPU region {{{s:#x}, {cap:#x}}} 이 {n} 개 (owner {len(c['owners'])} 개 기대)")
    waste = sum(syms[f"__dmac{i}_end"][0] - syms[f"__dmac{i}_fill"][0] for i in range(len(lay["clusters"])))
    print(f"cluster 안 미사용 {waste} B (256 B 올림), guard {256 * len(lay['clusters'])} B")

    sec = elf.get_section_by_name(".dma_manifest")
    d = sec.data()
    magic, ver, cnt = struct.unpack_from("<III", d, 0)
    if magic != 0x444D414D or cnt != len(lay["clusters"]):
        bad.append(f"manifest magic/count 불일치 ({magic:#x}, {cnt})")
    for i in range(cnt):
        _id, kind, ms, me, gb = struct.unpack_from("<IIIII", d, 12 + 20 * i)
        if ms != syms[f"__dmac{i}_start"][0] or me != syms[f"__dmac{i}_end"][0]:
            bad.append(f"manifest e[{i}] 주소 불일치")
    print(f"manifest @ {sec['sh_addr']:#x}: count={cnt}")

    for m in bad:
        print("FAIL", m)
    print("OK" if not bad else f"{len(bad)} 개 실패")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
