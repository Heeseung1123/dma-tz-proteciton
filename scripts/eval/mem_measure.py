#!/usr/bin/env python3
"""Static Flash/SRAM measurement: Secure mediator (S) vs Insecure mediator (I). cmpMemUsage.md.

  python3 -I scripts/eval/mem_measure.py [results/mem]

Inputs : results/mem/{S,I}_{P1,P2}/  (scripts/eval/build_variant.sh mem ... EVAL_MEM [MEDIATOR_INSECURE])
Outputs: results/mem/<build>/layout.md      per-build memory layout
         results/mem/summary.md             all tables (region, component, diff list, checks)
         results/mem/components.csv         component sizes per build
Exit 1 if any check fails (NSC allow-list, banned symbols, unreferenced mediator function,
unclassified S/I difference, map/ELF size mismatch, S-MPU hardcoding vs NS layout).
"""
import os, re, subprocess, sys, collections

TOOL = os.environ.get("TOOLCHAIN_BIN",
    "/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools."
    "gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin")
def tool(n): return os.path.join(TOOL, "arm-none-eabi-" + n)
def run(*a): return subprocess.run(a, check=True, capture_output=True, text=True).stdout

ROOT = sys.argv[1] if len(sys.argv) > 1 else "results/mem"
PAIRS = [("P1", "S_P1", "I_P1"), ("P2", "S_P2", "I_P2")]
WORLDS = ("Secure", "NonSecure")

# MEMORY regions of the two linker scripts (STM32CubeIDE/{Secure,NonSecure}/STM32L562QEIXQ_FLASH.ld)
MEM = {
    "Secure":    [("S-Flash", 0x0C000000, 248 * 1024), ("S-NSC", 0x0C03E000, 8 * 1024),
                  ("S-SRAM", 0x30000000, 96 * 1024)],
    "NonSecure": [("NS-Flash", 0x08040000, 256 * 1024), ("NS-SRAM", 0x20018000, 96 * 1024)],
}
DEV_FLASH, DEV_SRAM = 512 * 1024, 256 * 1024   # STM32L562QE (.ld comments: 512K / 256K)

NSC_S = {"SecureContext_Init", "SecureContext_AllocateContext", "SecureContext_FreeContext",
         "SecureContext_LoadContext", "SecureContext_SaveContext",
         "SecureInit_DePrioritizeNSExceptions", "SecureInit_EnableNSFPUAccess",
         "SECURE_SystemCoreClockUpdate", "SECURE_RegisterCallback", "SECURE_LEDToggle",
         "SECURE_dma_m2m_init", "SECURE_dma_m2m_start"}
NSC_I = NSC_S - {"SECURE_dma_m2m_init", "SECURE_dma_m2m_start"}
BANNED = re.compile(r"bench|verify_table|start_table|dma_tbl|nop3|g_tzic_sr3|ucPrivateBuffer|ulLookupTable")

# Component of every input section that may differ between S and I: (world, name regex, obj regex)
COMP = [
    ("C1", "요청 검증",                 r"Secure",    r"\.text\.verify_inst$", r"secure_nsc"),
    ("C2", "요청 진입점",               r"Secure",    r"\.text\.SECURE_dma_m2m_start$", r"secure_nsc"),
    ("C3", "초기화",                    r"Secure",    r"\.text\.(SECURE_dma_m2m_init|Secure_TZIC_ArmGuardIrqOnce|NVIC_ClearTargetState|NVIC_SetTargetState|__NVIC_ClearPendingIRQ|__NVIC_EnableIRQ|__NVIC_SetPriority)$", r"secure_nsc"),
    ("C3", "초기화",                    r"Secure",    r"\.text(\.cmse_check_address_range)?$", r"cmse"),
    ("C4", "완료 ISR + 통지",           r"Secure",    r"\.(text\.(DMA1_Channel1_IRQHandler|__NVIC_SetPendingIRQ)|bss\.(s_busy|s_ns_status))$", r"secure_nsc"),
    ("C4", "완료 ISR + 통지",           r"NonSecure", r"\.(text\.(DMA1_Channel1_IRQHandler|DFSDM1_FLT3_IRQHandler)|bss\.s_dmaStatusWord)$", r"app_freertos"),
    ("C5", "부팅 설정",                 r"Secure",    r"\.text\.(main|Secure_DMA_LockAllChannels|Secure_TZIC_Init|SMPU_SetupTaskRegions|SMPU_SetupOneTaskRegion|__NVIC_ClearPendingIRQ|NVIC_SetTargetState|__NVIC_EnableIRQ|__NVIC_SetPriority)$", r"main\.o"),
    ("C5", "부팅 설정",                 r"Secure",    r"\.text\.HAL_GTZC_MPCBB_ConfigMemAttributes$", r"gtzc"),
    ("C6", "전송 시점 보호",            r"Secure",    r"\.text\.GTZC_IRQHandler$", r"secure_nsc"),
    ("C7", "배치 (s_dmaTaskMem)",       r"NonSecure", r"\.bss\.s_dmaTaskMem$", r"app_freertos"),
    ("C8", "NSC 관문 (NS veneer)",      r"NonSecure", r"\.fini\.__stub$", r"linker stubs"),
    ("C8", "NSC 관문 (sgstub)",         r"Secure",    r"\.gnu\.sgstubs\.__stub$", r"linker stubs"),
    ("C9", "NS 호출부 (inline 포함)",   r"NonSecure", r"\.text\.(vCreateAppTasks|vDmaTaskA|vDmaTaskB)$", r"app_freertos"),
    ("C10", "정렬 fill",                r".*",        r"\*fill\*$", r".*"),
]
COMP_NAMES = collections.OrderedDict()
for c in COMP: COMP_NAMES.setdefault(c[0], c[1].split(" (")[0] if c[0] == "C8" else c[1])
COMP_NAMES["C8"] = "NSC 관문 (sgstub + NS veneer)"

def classify(world, name, obj):
    for cid, _, w, n, o in COMP:
        if re.fullmatch(w, world) and re.search(n, name) and re.search(o, obj):
            return cid
    return None

# ---------------------------------------------------------------- ELF / map readers
def sections(elf):
    out = []
    for l in run(tool("readelf"), "-SW", elf).splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)\s+\S+\s+(\S*)", l)
        if m and "A" in m.group(5):
            out.append((m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16)))
    return out

def region_of(world, addr):
    for name, base, size in MEM[world]:
        if base <= addr < base + size: return name
    return None

def regions(world, secs):
    """Bytes per MEMORY region; .data counts in SRAM (VMA) and Flash (LMA)."""
    r = collections.Counter()
    for name, typ, addr, size in secs:
        reg = region_of(world, addr)
        if reg is None: continue
        r[reg] += size
        if reg.endswith("SRAM") and typ == "PROGBITS":
            r[reg.replace("SRAM", "Flash")] += size
    return r

def map_inputs(mapf, alloc):
    """{(outsec, input name, obj): size} from the map, alloc output sections only."""
    d = collections.Counter(); out = None; pend = None; on = False
    for l in open(mapf, errors="replace"):
        l = l.rstrip("\n")
        if l.startswith("Linker script and memory map"): on = True; continue
        if not on: continue
        m = re.match(r"^(\.\S+)", l)
        if m: out = m.group(1); pend = None; continue
        if out not in alloc: continue
        m = re.match(r"^ \*fill\*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", l)
        if m: d[(out, "*fill*", "")] += int(m.group(2), 16); continue
        m = re.match(r"^ (\.\S+|COMMON)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+)$", l)
        if m: d[(out, m.group(1), os.path.basename(m.group(4)))] += int(m.group(3), 16); continue
        m = re.match(r"^ (\.\S+|COMMON)$", l)
        if m: pend = m.group(1); continue
        m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+)$", l)
        if m and pend: d[(out, pend, os.path.basename(m.group(3)))] += int(m.group(2), 16); pend = None; continue
        pend = None
    return {k: v for k, v in d.items() if v}

def nm(elf):
    s = {}
    for l in run(tool("nm"), "-S", elf).splitlines():
        p = l.split()
        if len(p) == 4: s[p[3]] = (int(p[0], 16), int(p[1], 16), p[2])
        elif len(p) == 3: s.setdefault(p[2], (int(p[0], 16), 0, p[1]))
    return s

def gdb(elf, *exprs):
    g = tool("gdb")
    a = [g, "-q", "-batch", "-nx", "-ex", "set print pretty off"]
    for e in exprs: a += ["-ex", "p " + e]
    out = run(*a, elf)
    return [re.sub(r"^\$\d+ = ", "", l) for l in out.splitlines() if l.startswith("$")]

# ---------------------------------------------------------------- per build
class Build:
    def __init__(self, name):
        self.name = name; self.dir = os.path.join(ROOT, name); self.insecure = name.startswith("I")
        self.elf = {w: os.path.join(self.dir, f"FreeRTOS_SecureIOToggle_TrustZone_{w}.elf") for w in WORLDS}
        self.map = {w: self.elf[w][:-4] + ".map" for w in WORLDS}
        self.secs = {w: sections(self.elf[w]) for w in WORLDS}
        self.reg = {w: regions(w, self.secs[w]) for w in WORLDS}
        self.inp = {w: map_inputs(self.map[w], {s[0] for s in self.secs[w]}) for w in WORLDS}
        self.sym = {w: nm(self.elf[w]) for w in WORLDS}
        self.errors = []
        self.check_map_sums(); self.check_nsc(); self.check_banned(); self.check_refs()
        self.dma = self.read_dma(); self.check_smpu()

    def check_map_sums(self):
        for w in WORLDS:
            tot = collections.Counter()
            for (o, _, _), v in self.inp[w].items(): tot[o] += v
            self.resid = getattr(self, "resid", {})
            for name, typ, addr, size in self.secs[w]:
                r = size - tot[name]
                # > 0: ALIGN/assignments not listed as input or fill. < 0: SHF_MERGE strings (.rodata),
                # the map lists input sizes before merging. Either way it enters the S/I diff as C10.
                self.resid[(w, name)] = r

    def nsc(self):
        return sorted(n for n, (a, _, t) in self.sym["Secure"].items()
                      if 0x0C03E000 <= a < 0x0C040000 and t in "Tt" and not n.startswith("$"))

    def check_nsc(self):
        want = NSC_I if self.insecure else NSC_S
        got = set(self.nsc())
        if got != want: self.errors.append(f"NSC list mismatch: extra {sorted(got - want)} missing {sorted(want - got)}")

    def check_banned(self):
        for w in WORLDS:
            bad = [n for n in self.sym[w] if BANNED.search(n)]
            ch2 = self.sym[w].get("DMA1_Channel2_IRQHandler")
            if ch2 and ch2[2] == "T": bad.append("DMA1_Channel2_IRQHandler (strong)")
            if bad: self.errors.append(f"{w} banned symbols: {bad}")

    def check_refs(self):
        """Every function defined by the mediator objects must be referenced (branch target,
        literal/vector word, or NSC stub)."""
        objs = ("secure_nsc.o", "main.o", "app_freertos.o")
        for w in WORLDS:
            dis = run(tool("objdump"), "-d", self.elf[w])
            targets = collections.Counter(re.findall(r"\s[0-9a-f]+ <([^>+]+)>", dis))
            words = set()
            for name, typ, addr, size in self.secs[w]:
                if typ != "PROGBITS": continue
                for l in run(tool("objdump"), "-s", "-j", name, self.elf[w]).splitlines():
                    m = re.match(r"^\s[0-9a-f]+\s((?:[0-9a-f]{1,8}\s){1,4})", l)
                    if m:
                        for wd in m.group(1).split():
                            if len(wd) == 8: words.add(int.from_bytes(bytes.fromhex(wd), "little"))
            for (o, nmn, obj), v in self.inp[w].items():
                if obj not in objs or not nmn.startswith(".text."): continue
                fn = nmn[len(".text."):]
                s = self.sym[w].get(fn) or self.sym[w].get("__acle_se_" + fn)
                if not s: continue
                a = s[0]
                if (targets[fn] + targets["__acle_se_" + fn]) > 0 or (a | 1) in words or a in words:
                    continue
                self.errors.append(f"{w} unreferenced function {fn} ({obj})")

    def read_dma(self):
        e = self.elf["NonSecure"]
        r = gdb(e, "sizeof(dma_task_globals_t)", "sizeof(dma_task_mem_t)", "&s_dmaTaskMem",
                "(int)&((dma_task_mem_t*)0)->cluster",
                "xDmaTaskAParams.xRegions", "xDmaTaskBParams.xRegions", "xPlainTaskParams.xRegions")
        d = {"gsize": int(r[0]), "msize": int(r[1]),
             "base": int(re.search(r"0x[0-9a-f]+", r[2]).group(0), 16), "coff": int(r[3])}
        d["regions"] = {}
        for task, txt in zip(("DMA_Task_A", "DMA_Task_B", "Plain_Task"), r[4:7]):
            regs = re.findall(r"pvBaseAddress = (0x[0-9a-f]+)[^,]*, ulLengthInBytes = (\d+), ulParameters = (\d+)", txt)
            d["regions"][task] = [(int(a, 16), int(n), int(p)) for a, n, p in regs]
        return d

    def check_smpu(self):
        """S only: Secure SMPU_SetupTaskRegions literals must hold both Cluster bases of the NS layout."""
        self.smpu = None
        if self.insecure: return
        a = self.dma["base"] + self.dma["coff"]; b = a + self.dma["msize"]
        # All Secure literals: at -O1 SMPU_SetupTaskRegions is inlined into main and the RBAR value
        # (base | SH | AP | XN) is folded, so compare the 32B-aligned part (RBAR BASE field).
        dis = run(tool("objdump"), "-d", self.elf["Secure"])
        lits = {int(x, 16) for x in re.findall(r"\.word\s+0x([0-9a-f]+)", dis)}
        hit = lambda v: any((x & ~0x1F) == v for x in lits)
        self.smpu = {"A": a, "B": b, "A_ok": hit(a), "B_ok": hit(b),
                     "lits": sorted(x for x in lits if 0x20000000 <= x < 0x20040000)}
        if not (self.smpu["A_ok"] and self.smpu["B_ok"]):
            self.errors.append(f"S-MPU hardcoding mismatch: NS clusters {a:#x}/{b:#x}, Secure literals "
                               f"{[hex(x) for x in self.smpu['lits']]}")

    def totals(self):
        s, n = self.reg["Secure"], self.reg["NonSecure"]
        return collections.OrderedDict([
            ("S-Flash", s["S-Flash"]), ("S-NSC", s["S-NSC"]), ("S-SRAM", s["S-SRAM"]),
            ("NS-Flash", n["NS-Flash"]), ("NS-SRAM", n["NS-SRAM"]),
            ("Flash 합계", s["S-Flash"] + s["S-NSC"] + n["NS-Flash"]),
            ("SRAM 합계", s["S-SRAM"] + n["NS-SRAM"])])

# ---------------------------------------------------------------- diff + components
def diff(S, I):
    rows = []
    for w in WORLDS:
        keys = set(S.inp[w]) | set(I.inp[w])
        fills = collections.Counter()
        for k in keys:
            a, b = S.inp[w].get(k, 0), I.inp[w].get(k, 0)
            if a == b: continue
            o, n, obj = k
            if n == "*fill*": fills[o] += a - b; continue
            rows.append((w, o, n, obj, a, b, classify(w, n, obj)))
        for o, dv in fills.items():
            rows.append((w, o, "*fill*", "", S.inp[w].get((o, "*fill*", ""), 0),
                         I.inp[w].get((o, "*fill*", ""), 0), "C10"))
        for (ww, o), r in S.resid.items():
            if ww != w: continue
            ri = I.resid.get((w, o), 0)
            if r != ri: rows.append((w, o, "(ALIGN/문자열 병합, 맵 미표시)", "", r, ri, "C10"))
    rows.sort(key=lambda r: (r[0], r[6] or "Z", r[2]))
    return rows

def flash_sram(w, outsec, secs):
    for name, typ, addr, size in secs[w]:
        if name == outsec:
            reg = region_of(w, addr) or ""
            if reg.endswith("SRAM"): return ("SRAM+Flash" if typ == "PROGBITS" else "SRAM")
            return "Flash"
    return "?"

def comps(rows, S):
    c = collections.OrderedDict((k, [0, 0, 0, 0]) for k in COMP_NAMES)   # S flash, S sram, I flash, I sram
    for w, o, n, obj, a, b, cid in rows:
        if not cid: continue
        kind = flash_sram(w, o, S.secs)
        for i, v in ((0, a), (2, b)):
            if "Flash" in kind: c[cid][i] += v
            if "SRAM" in kind: c[cid][i + 1] += v
    return c

# ---------------------------------------------------------------- output
def pct(a, b): return f"{(a - b) / b * 100:+.2f} %" if b else "—"
def bar(n, tot, w=40):
    k = round(n / tot * w) if tot else 0
    return "#" * k + "." * (w - k)

def layout_md(B):
    L = [f"# 메모리 레이아웃: {B.name}", "",
         f"`{B.dir}` 의 ELF/map 에서 `scripts/eval/mem_measure.py` 가 생성. 크기는 바이트.", ""]
    for w in WORLDS:
        for reg, base, size in MEM[w]:
            used = B.reg[w][reg]
            L += [f"## {reg}  0x{base:08x} – 0x{base + size - 1:08x}  ({size // 1024} KB)", "",
                  f"사용 {used} B = {used / size * 100:.2f} % `[{bar(used, size)}]`", "",
                  "| 섹션 | 시작 | 끝 | 크기 | 영역 대비 | |", "|---|---|---|---|---|---|"]
            for name, typ, addr, sz in B.secs[w]:
                if region_of(w, addr) != reg or sz == 0: continue
                note = " (Flash 에 LMA 사본)" if reg.endswith("SRAM") and typ == "PROGBITS" else ""
                L.append(f"| `{name}`{note} | 0x{addr:08x} | 0x{addr + sz - 1:08x} | {sz} | "
                         f"{sz / size * 100:.2f} % | `{bar(sz, size, 30)}` |")
            L.append("")
    d = B.dma; base = d["base"]
    L += ["## DMA 배치 (NS SRAM, `s_dmaTaskMem`)", "",
          f"`sizeof(dma_task_globals_t)` = {d['gsize']}, `sizeof(dma_task_mem_t)` = {d['msize']}, "
          f"Cluster 오프셋 = {d['coff']}, 총 {2 * d['msize']} B @ 0x{base:08x}", "", "```"]
    for t in range(2):
        tb = base + t * d["msize"]; tn = "AB"[t]
        L.append(f"0x{tb:08x}  [{tn}.globals {d['coff']:4d} B : 사용 {d['gsize']} + 패딩 {d['coff'] - d['gsize']}]"
                 f"{'  NS-MPU r5 시작' if True else ''}")
        L.append(f"0x{tb + d['coff']:08x}  [{tn}.cluster  256 B]  NS-MPU r5 끝"
                 + (f" | S-MPU 영역 {4 + t}" if not B.insecure else ""))
        if not B.insecure:
            L.append(f"0x{tb + d['coff'] + 256:08x}  [{tn}.guard    256 B]  MPCBB Secure 블록 (NS-MPU 밖)")
    L.append(f"0x{base + 2 * d['msize']:08x}  (끝)")
    L += ["```", ""]
    if B.smpu:
        L += [f"Secure 리터럴(S-MPU RBAR BASE) 대조: A 0x{B.smpu['A']:08x} "
              f"{'일치' if B.smpu['A_ok'] else '불일치'}, B 0x{B.smpu['B']:08x} {'일치' if B.smpu['B_ok'] else '불일치'}", ""]
    L += ["## NS-MPU 영역 (태스크별, FreeRTOS ARM_CM33 포트)", "",
          "영역 0–3 커널 고정, 4 = 태스크 스택, 5–7 = `xRegions[0..2]` (`portmacrocommon.h:173-179`, configTOTAL_MPU_REGIONS 8).", "",
          "| 태스크 | r5 | r6 | r7 | 사용 (r5–r7) |", "|---|---|---|---|---|"]
    for t, regs in d["regions"].items():
        cells = []
        for a, n, p in regs:
            if n == 0: cells.append("—"); continue
            attr = ("RW" if p & 2 else "RO") + (" DEV" if p & 16 else "") + (" XN" if p & 4 else "")
            cells.append(f"0x{a:08x}+{n} {attr}")
        L.append(f"| {t} | " + " | ".join(cells) + f" | {sum(1 for _, n, _ in regs if n)} / 3 |")
    L.append("")
    L += ["## NSC 진입점 (.gnu.sgstubs)", "", ", ".join(f"`{n}`" for n in B.nsc()), ""]
    L += ["## 검사", ""] + ([f"- FAIL: {e}" for e in B.errors] or ["- 모두 통과 (NSC 허용 목록, 금지 심볼, 미참조 함수, 맵 합계, S-MPU 대조)"])
    return "\n".join(L) + "\n"

def main():
    builds = {}; out = []; csv = ["profile,build,component,flash,sram"]; fail = False
    for prof, sn, iname in PAIRS:
        S, I = Build(sn), Build(iname); builds[sn] = S; builds[iname] = I
        for B in (S, I):
            open(os.path.join(B.dir, "layout.md"), "w").write(layout_md(B))
            fail |= bool(B.errors)
        rows = diff(S, I)
        unc = [r for r in rows if not r[6]]
        fail |= bool(unc)
        ts, ti = S.totals(), I.totals()
        out += [f"## {prof}", "", "### 영역별 (바이트)", "",
                "| 영역 | S | I | Δ (S−I) | Δ% (대 I) |", "|---|---|---|---|---|"]
        for k in ts:
            out.append(f"| {k} | {ts[k]} | {ti[k]} | {ts[k] - ti[k]:+d} | {pct(ts[k], ti[k])} |")
        out += ["", "### 섹션별 (바이트, 0 아닌 섹션)", "", "| 세계 | 섹션 | S | I | Δ |", "|---|---|---|---|---|"]
        for w in WORLDS:
            si = {n: s for n, _, _, s in S.secs[w]}; ii = {n: s for n, _, _, s in I.secs[w]}
            for n in si:
                if si[n] or ii.get(n): out.append(f"| {w} | `{n}` | {si[n]} | {ii.get(n, 0)} | {si[n] - ii.get(n, 0):+d} |")
        c = comps(rows, S)
        out += ["", "### 구성요소별 (바이트, S 와 I 가 다른 입력 섹션만)", "",
                "| # | 구성요소 | S Flash | S SRAM | I Flash | I SRAM | Δ Flash | Δ SRAM |", "|---|---|---|---|---|---|---|---|"]
        tot = [0, 0, 0, 0]
        for k, v in c.items():
            out.append(f"| {k} | {COMP_NAMES[k]} | {v[0]} | {v[1]} | {v[2]} | {v[3]} | {v[0] - v[2]:+d} | {v[1] - v[3]:+d} |")
            csv += [f"{prof},S,{k},{v[0]},{v[1]}", f"{prof},I,{k},{v[2]},{v[3]}"]
            tot = [x + y for x, y in zip(tot, v)]
        out.append(f"| | **합계** | {tot[0]} | {tot[1]} | {tot[2]} | {tot[3]} | {tot[0] - tot[2]:+d} | {tot[1] - tot[3]:+d} |")
        dfl, dsr = ts["Flash 합계"] - ti["Flash 합계"], ts["SRAM 합계"] - ti["SRAM 합계"]
        out += ["", f"구성요소 합계 Δ Flash {tot[0] - tot[2]:+d} / 영역 합계 Δ Flash {dfl:+d}, "
                f"구성요소 합계 Δ SRAM {tot[1] - tot[3]:+d} / 영역 합계 Δ SRAM {dsr:+d} "
                f"({'일치' if (tot[0] - tot[2], tot[1] - tot[3]) == (dfl, dsr) else '불일치: 섹션 단위 ALIGN 등'})", ""]
        out += ["### 차이 나는 입력 섹션 전체 (맵 기준)", "", "| 세계 | 출력 섹션 | 입력 섹션 | 오브젝트 | S | I | Δ | 구성요소 |",
                "|---|---|---|---|---|---|---|---|"]
        for w, o, n, obj, a, b, cid in rows:
            out.append(f"| {w} | `{o}` | `{n}` | {obj} | {a} | {b} | {a - b:+d} | {cid or '**미분류**'} |")
        out += ["", "### 검사", ""]
        for B in (S, I):
            out += [f"- {B.name}: " + ("통과" if not B.errors else "FAIL " + "; ".join(B.errors))]
        out += [f"- 미분류 차이: {len(unc)} 건", ""]
    hdr = ["# 메모리 측정 집계 (scripts/eval/mem_measure.py 생성)", "",
           f"장치 용량: Flash {DEV_FLASH // 1024} KB, SRAM {DEV_SRAM // 1024} KB. .data 는 SRAM 과 Flash(LMA) 양쪽에 센다.", ""]
    open(os.path.join(ROOT, "summary.md"), "w").write("\n".join(hdr + out) + "\n")
    open(os.path.join(ROOT, "components.csv"), "w").write("\n".join(csv) + "\n")
    print(open(os.path.join(ROOT, "summary.md")).read())
    if fail: print("CHECK FAILED", file=sys.stderr); sys.exit(1)

if __name__ == "__main__":
    main()
