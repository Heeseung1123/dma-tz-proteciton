#!/usr/bin/env python3
"""Figures for the memory evaluation (cmpMemUsage_result.md, HowmemUsageWork.md, cmpMemUsage.md).

  python3 -I scripts/eval/mem_figures.py [results/mem] [docs/figures/mem]

Charts read the same ELF/map as mem_measure.py (it is imported), so they follow the numbers.
Diagrams (pipeline, paths, address maps) are drawn from the same data where they show addresses.
SVG text is converted to paths (svg.fonttype = path) so Korean renders without the font installed.
Colors: reference categorical slots in their documented order (dataviz references/palette.md, light):
  S = slot 1 blue, I = slot 2 orange; P1 = slot 3 aqua, P2 = slot 4 yellow (adjacent pair);
  layout parts: guard = slot 5 magenta, padding = slot 6 green, fill = slot 7 violet.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mem_measure as mm
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, PathPatch, FancyArrowPatch
from matplotlib.path import Path

ROOT = sys.argv[1] if len(sys.argv) > 1 else "results/mem"
OUT = sys.argv[2] if len(sys.argv) > 2 else "docs/figures/mem"
mm.ROOT = ROOT
os.makedirs(OUT, exist_ok=True)

SURF, INK, INK2, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#8a8984", "#e4e3df"
BLUE, ORANGE, AQUA, YELLOW, MAGENTA, GREEN, VIOLET = ("#2a78d6", "#eb6834", "#1baf7a", "#eda100",
                                                      "#e87ba4", "#008300", "#6250d6")
SEC_BG, NS_BG, NEUTRAL, NEUTRAL2 = "#efeeea", "#ffffff", "#c3c2b7", "#dcdbd5"
CLUSTER = "#9ec5f4"

from matplotlib import font_manager as _fm
_CJK = "Noto Sans CJK JP"   # .ttc face matplotlib registers first; same glyphs (Hangul included) for every CJK face
for _f in ("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"):
    if os.path.exists(_f):
        _fm.fontManager.addfont(_f)
        _CJK = _fm.FontProperties(fname=_f).get_name()

plt.rcParams.update({
    "font.family": _CJK, "svg.fonttype": "path", "font.size": 10,
    "figure.facecolor": SURF, "axes.facecolor": SURF, "savefig.facecolor": SURF,
    "text.color": INK, "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2,
    "axes.edgecolor": GRID, "axes.spines.top": False, "axes.spines.right": False,
    "svg.hashsalt": "dma-tz-mem",   # deterministic SVG ids (re-running gives the same file)
    "axes.titlesize": 11, "axes.titleweight": "bold", "axes.titlelocation": "left",
})

def save(fig, name):
    p = os.path.join(OUT, name)
    fig.savefig(p, format="svg", bbox_inches="tight", pad_inches=0.15, metadata={"Date": None})
    if os.environ.get("FIG_PNG_DIR"):   # preview copies for visual review (not committed)
        fig.savefig(os.path.join(os.environ["FIG_PNG_DIR"], name[:-4] + ".png"), dpi=110, bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)
    print("wrote", p)

# ------------------------------------------------------------------ helpers
def hbar(ax, y, x0, x1, h, color, r_px=4):
    """Horizontal bar, square at the baseline (x0), 4px rounded at the data end (x1)."""
    if x1 == x0: return
    fig = ax.figure; fig.canvas.draw_idle()
    bb = ax.get_window_extent()
    xr = ax.get_xlim(); yr = ax.get_ylim()
    rx = r_px * (xr[1] - xr[0]) / bb.width
    ry = r_px * abs(yr[1] - yr[0]) / bb.height
    s = 1 if x1 > x0 else -1
    rx = min(rx, abs(x1 - x0)); ry = min(ry, h / 2)
    yb, yt = y - h / 2, y + h / 2
    v = [(x0, yb), (x1 - s * rx, yb), (x1, yb), (x1, yb + ry), (x1, yt - ry), (x1, yt),
         (x1 - s * rx, yt), (x0, yt), (x0, yb)]
    c = [Path.MOVETO, Path.LINETO, Path.CURVE3, Path.CURVE3, Path.LINETO, Path.CURVE3, Path.CURVE3,
         Path.LINETO, Path.CLOSEPOLY]
    ax.add_patch(PathPatch(Path(v, c), facecolor=color, edgecolor="none", zorder=3))

def box(ax, x, y, w, h, text, fc=NS_BG, ec=NEUTRAL, tc=INK, fs=9, bold=False, r=0.012, lw=1.0):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad=0,rounding_size={r}",
                                facecolor=fc, edgecolor=ec, linewidth=lw, zorder=2))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs, color=tc,
            fontweight="bold" if bold else "normal", zorder=3, linespacing=1.35)

def arrow(ax, x0, y0, x1, y1, color=INK2, text=None, fs=8, style="-|>", lw=1.2, toff=(0, 0.012)):
    ax.add_patch(FancyArrowPatch((x0, y0), (x1, y1), arrowstyle=style, mutation_scale=10,
                                 color=color, linewidth=lw, zorder=4, shrinkA=0, shrinkB=0))
    if text:
        ax.text((x0 + x1) / 2 + toff[0], (y0 + y1) / 2 + toff[1], text, ha="center", va="bottom",
                fontsize=fs, color=INK2, zorder=5)

def canvas(w, h):
    fig = plt.figure(figsize=(w, h)); ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
    return fig, ax

from matplotlib.patches import Patch
def legend(fig, items, x, y, fs=9, loc="upper left"):
    """Legend with square swatches, one row, no frame (figure coordinates)."""
    fig.legend(handles=[Patch(facecolor=c, edgecolor="none", label=l) for l, c in items], loc=loc,
               bbox_to_anchor=(x, y), ncol=len(items), frameon=False, fontsize=fs, handlelength=1.0,
               handleheight=1.0, handletextpad=0.5, columnspacing=1.6, borderaxespad=0)

# ------------------------------------------------------------------ data
B = {n: mm.Build(n) for n in ("S_P1", "I_P1", "S_P2", "I_P2")}
COMPS = {}
for prof, sn, iname in mm.PAIRS:
    COMPS[prof] = mm.comps(mm.diff(B[sn], B[iname]), B[sn])

# ================================================================== fig 01: pipeline (How §0)
def fig_pipeline():
    fig, ax = canvas(11, 4.3)
    ax.text(0.01, 0.96, "메모리 측정 흐름", fontsize=12, fontweight="bold", va="top")
    box(ax, 0.01, 0.50, 0.17, 0.34, "소스\n(기본 빌드는 그대로)\n\nEVAL_MEM\n벤치·비교 대상·\n미사용 데이터 제외\n\nMEDIATOR_INSECURE\nI 빌드", fs=8.5)
    box(ax, 0.23, 0.56, 0.15, 0.22, "build_variant.sh\n\ngcc 래퍼: -O 치환\n+ -D 매크로\n(Secure·NS 양쪽)", fs=8.5)
    for i, n in enumerate(("S_P1", "I_P1", "S_P2", "I_P2")):
        box(ax, 0.43 + (i % 2) * 0.075, 0.70 - (i // 2) * 0.12, 0.068, 0.09, n,
            fc="#e8f1fb" if n[0] == "S" else "#fdeee6", ec=BLUE if n[0] == "S" else ORANGE, fs=8.5, bold=True)
    ax.text(0.505, 0.53, "results/mem/<빌드>/\nELF · map · manifest", ha="center", va="top", fontsize=8, color=INK2)
    box(ax, 0.63, 0.50, 0.17, 0.34, "mem_measure.py\n\nreadelf: 섹션→영역\nmap: 입력 섹션 크기\nnm/objdump: 검사\ngdb: sizeof, xRegions\n\nS − I 차분 → C1–C10", fs=8.5)
    box(ax, 0.84, 0.62, 0.15, 0.22, "layout.md (빌드별)\nsummary.md\ncomponents.csv", fs=8.5)
    box(ax, 0.84, 0.36, 0.15, 0.17, "cmpMemUsage_result.md\n(사람이 정리)", fs=8.5, fc=SEC_BG)
    arrow(ax, 0.18, 0.67, 0.23, 0.67); arrow(ax, 0.38, 0.67, 0.43, 0.67)
    arrow(ax, 0.58, 0.67, 0.63, 0.67); arrow(ax, 0.80, 0.73, 0.84, 0.73); arrow(ax, 0.915, 0.62, 0.915, 0.53)
    # loadcmp side path
    box(ax, 0.23, 0.10, 0.15, 0.20, "기본 빌드 2개\nref_default_P1 (변경 전)\nchk_default_P1 (변경 후)", fs=8.5)
    box(ax, 0.43, 0.10, 0.15, 0.20, "loadcmp.sh\n적재 섹션 내용\n+ 심볼 표 비교", fs=8.5)
    box(ax, 0.63, 0.10, 0.17, 0.20, "same → 기본 빌드 불변\n(디버그 정보는 무시)", fs=8.5, fc=SEC_BG)
    arrow(ax, 0.38, 0.20, 0.43, 0.20); arrow(ax, 0.58, 0.20, 0.63, 0.20)
    arrow(ax, 0.305, 0.56, 0.305, 0.30, text=None)
    ax.text(0.312, 0.43, "매크로 없이", fontsize=8, color=INK2, va="center")
    save(fig, "fig01_pipeline.svg")

# ================================================================== fig 02: S vs I paths (result §2, How §1)
def fig_paths():
    fig, ax = canvas(11, 5.2)
    ax.text(0.01, 0.995, "비교 대상: 요청 경로와 완료 경로", fontsize=12, fontweight="bold", va="top")
    for x0, title, col in ((0.01, "S: Secure 중재자 (제안 기법)", BLUE), (0.51, "I: Insecure 중재자 (기준선, 패턴 3)", ORANGE)):
        ax.add_patch(Rectangle((x0, 0.50), 0.48, 0.38, facecolor=NS_BG, edgecolor=NEUTRAL, lw=1))
        ax.add_patch(Rectangle((x0, 0.06), 0.48, 0.42, facecolor=SEC_BG, edgecolor=NEUTRAL, lw=1))
        ax.text(x0 + 0.005, 0.875, "Non-secure", fontsize=8, color=MUTED, va="top")
        ax.text(x0 + 0.005, 0.47, "Secure", fontsize=8, color=MUTED, va="top")
        ax.add_patch(Rectangle((x0, 0.885), 0.48, 0.006, facecolor=col, edgecolor="none"))
        ax.text(x0, 0.905, title, fontsize=10.5, fontweight="bold", va="bottom")
    # S request
    box(ax, 0.03, 0.72, 0.10, 0.09, "DMA Task\n(비특권)", fs=8.5)
    box(ax, 0.17, 0.72, 0.09, 0.09, "NS veneer\n→ SG", fs=8.5)
    box(ax, 0.17, 0.33, 0.11, 0.10, "SECURE_dma_\nm2m_start", fs=8.5, fc="#e8f1fb", ec=BLUE)
    box(ax, 0.31, 0.33, 0.08, 0.10, "verify_inst\nTTAT×2\nTTT×2", fs=8, fc="#e8f1fb", ec=BLUE)
    box(ax, 0.41, 0.33, 0.07, 0.10, "DMA1\nCH1\nSECM=1", fs=8)
    arrow(ax, 0.13, 0.765, 0.17, 0.765); arrow(ax, 0.215, 0.72, 0.215, 0.43)
    ax.text(0.205, 0.47, "NSC 호출", ha="right", va="center", fontsize=8, color=INK2)
    arrow(ax, 0.28, 0.38, 0.31, 0.38); arrow(ax, 0.39, 0.38, 0.41, 0.38)
    # S completion
    box(ax, 0.03, 0.10, 0.12, 0.10, "Secure DMA ISR\n*s_ns_status\ndoorbell pend", fs=8, fc="#e8f1fb", ec=BLUE)
    box(ax, 0.03, 0.55, 0.12, 0.09, "doorbell ISR\n(IRQ 105, NS)", fs=8.5, fc="#e8f1fb", ec=BLUE)
    box(ax, 0.30, 0.55, 0.12, 0.09, "App_DmaNotify\nOwner → Task", fs=8.5)
    ax.plot([0.445, 0.445], [0.33, 0.15], color=INK2, lw=1.2, zorder=4)
    arrow(ax, 0.445, 0.15, 0.15, 0.15)
    ax.text(0.452, 0.24, "TC\nIRQ 29", fontsize=8, color=INK2, va="center")
    arrow(ax, 0.09, 0.20, 0.09, 0.55)
    ax.text(0.097, 0.30, "tail-chain", fontsize=8, color=INK2, va="center")
    arrow(ax, 0.15, 0.595, 0.30, 0.595)
    ax.text(0.17, 0.245, "부팅 설정 (S 만)\nS-MPU 영역 4·5 = Cluster\nMPCBB 가드 2 블록\nTZIC SRAM1IE · GTZC_IRQHandler\nDMA1/2 채널 14 개 SECM",
            fontsize=7.5, color=INK2, va="center")
    # I request
    box(ax, 0.53, 0.72, 0.10, 0.09, "DMA Task\n(비특권)", fs=8.5)
    box(ax, 0.67, 0.72, 0.13, 0.09, "Insecure_dma_m2m_\nstart (inline)", fs=8.5, fc="#fdeee6", ec=ORANGE)
    box(ax, 0.86, 0.72, 0.10, 0.09, "DMA1 CH1\nSECM=0", fs=8.5)
    arrow(ax, 0.63, 0.765, 0.67, 0.765); arrow(ax, 0.80, 0.765, 0.86, 0.765, text="NS-MPU r7", toff=(0, 0.035))
    box(ax, 0.86, 0.55, 0.10, 0.09, "NS DMA ISR\n(IRQ 29, NS)", fs=8.5, fc="#fdeee6", ec=ORANGE)
    box(ax, 0.67, 0.55, 0.13, 0.09, "App_DmaNotify\nOwner → Task", fs=8.5)
    arrow(ax, 0.91, 0.72, 0.91, 0.64); arrow(ax, 0.86, 0.595, 0.80, 0.595)
    ax.text(0.53, 0.27, "Secure 쪽에 DMA 관련 코드 없음\n(CH1 SECM 해제 + IRQ 29 ITNS 만)\n\n검증 없음 · 가드 없음 · doorbell 없음",
            fontsize=8.5, color=INK2, va="center")
    save(fig, "fig02_s_vs_i_paths.svg")

# ================================================================== fig 03: regions (result §3.1, §8.1)
def fig_regions():
    regs = ["S-Flash", "S-NSC", "S-SRAM", "NS-Flash", "NS-SRAM"]
    fig, axes = plt.subplots(1, 2, figsize=(11, 3.6), sharey=True)
    fig.subplots_adjust(wspace=0.08, top=0.80, bottom=0.12, left=0.09, right=0.98)
    xmax = max(max(B[n].totals()[r] for r in regs) for n in B) * 1.55
    for ax, prof in zip(axes, ("P1", "P2")):
        s, i = B["S_" + prof].totals(), B["I_" + prof].totals()
        ax.set_xlim(0, xmax); ax.set_ylim(len(regs) - 0.4, -0.6)
        ax.set_yticks(range(len(regs))); ax.set_yticklabels(regs)
        ax.tick_params(axis="y", length=0); ax.tick_params(axis="x", labelsize=8)
        ax.xaxis.grid(True, color=GRID, lw=1); ax.set_axisbelow(True)
        ax.spines["left"].set_visible(False)
        ax.set_xticks([0, 10000, 20000, 30000, 40000]); ax.set_xticklabels(["0", "10K", "20K", "30K", "40K"])
        ax.set_title(f"{prof}  ({'Secure -O0, NS -O1' if prof == 'P1' else 'Secure -O1, NS -O1'})", pad=8)
        for k, r in enumerate(regs):
            for off, val, col in ((-0.17, s[r], BLUE), (0.17, i[r], ORANGE)):
                hbar(ax, k + off, 0, val, 0.30, col)
            d = s[r] - i[r]
            ax.text(max(s[r], i[r]) + xmax * 0.012, k, f"S {s[r]:,} / I {i[r]:,}   Δ {d:+,}",
                    va="center", fontsize=8, color=INK2)
        ax.set_xlabel("바이트", fontsize=8)
    fig.text(0.09, 0.95, "영역별 메모리 사용량: S vs I", fontsize=12, fontweight="bold")
    legend(fig, [("S (Secure 중재자)", BLUE), ("I (Insecure 중재자)", ORANGE)], 0.98, 0.99, loc="upper right")
    save(fig, "fig03_regions.svg")

# ================================================================== fig 04: component deltas (result §3.2, §3.4, §8.3)
def fig_components():
    keys = list(mm.COMP_NAMES)
    names = [f"{k} {mm.COMP_NAMES[k].replace(' (sgstub + NS veneer)', '')}" for k in keys]
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.6), sharey=True)
    fig.subplots_adjust(wspace=0.10, top=0.84, bottom=0.10, left=0.17, right=0.98)
    for ax, idx, title in ((axes[0], 0, "Δ Flash (S − I)"), (axes[1], 1, "Δ SRAM (S − I)")):
        vals = {p: [COMPS[p][k][idx] - COMPS[p][k][idx + 2] for k in keys] for p in ("P1", "P2")}
        lo = min(min(v) for v in vals.values()); hi = max(max(v) for v in vals.values())
        span = hi - lo
        ax.set_xlim(min(0, lo) - span * 0.08, hi + span * 0.22); ax.set_ylim(len(keys) - 0.4, -0.6)
        ax.set_yticks(range(len(keys))); ax.set_yticklabels(names, fontsize=9)
        ax.tick_params(axis="y", length=0); ax.tick_params(axis="x", labelsize=8)
        ax.xaxis.grid(True, color=GRID, lw=1); ax.set_axisbelow(True); ax.spines["left"].set_visible(False)
        ax.axvline(0, color=INK2, lw=1, zorder=2)
        tot = {p: sum(vals[p]) for p in vals}
        ax.set_title(f"{title}   합계 P1 {tot['P1']:+,} · P2 {tot['P2']:+,}", pad=8)
        for k in range(len(keys)):
            for off, p, col in ((-0.17, "P1", AQUA), (0.17, "P2", YELLOW)):
                v = vals[p][k]
                hbar(ax, k + off, 0, v, 0.30, col)
                if v:
                    ax.text(v + (span * 0.012 if v > 0 else -span * 0.012), k + off, f"{v:+,}",
                            va="center", ha="left" if v > 0 else "right", fontsize=7.5, color=INK2)
    fig.text(0.17, 0.95, "구성요소별 증가량 (바이트, P1 vs P2)", fontsize=12, fontweight="bold")
    legend(fig, [("P1 (Secure -O0)", AQUA), ("P2 (Secure -O1)", YELLOW)], 0.98, 0.99, loc="upper right")
    save(fig, "fig04_components.svg")

# ================================================================== fig 05: SRAM breakdown (result §3.3)
def fig_sram():
    S, I = B["S_P1"], B["I_P1"]
    guard = 2 * 256
    pad = 2 * (S.dma["coff"] - I.dma["coff"])
    fill = S.inp["NonSecure"].get((".bss", "*fill*", ""), 0) - I.inp["NonSecure"].get((".bss", "*fill*", ""), 0)
    total = S.totals()["SRAM 합계"] - I.totals()["SRAM 합계"]
    other = total - guard - pad - fill
    parts = [("가드 블록 2 × 256", guard, MAGENTA), ("globals 정렬 패딩 2 × 224", pad, GREEN),
             ("aligned(256) 앞 fill", fill, VIOLET), ("상태 변수 + Secure fill", other, NEUTRAL)]
    W, H = 11, 1.9
    fig = plt.figure(figsize=(W, H)); ax = fig.add_axes([0.02, 0.36, 0.96, 0.24])
    wpx, hpx = 0.96 * W * 72, 0.24 * H * 72            # axes size in points; 1 data unit = 1 pt
    ax.set_xlim(0, wpx); ax.set_ylim(0, hpx); ax.axis("off")
    x = 0; gap = 2.0
    for label, v, col in parts:
        w = v / total * wpx
        if w - gap > 8:
            ax.add_patch(FancyBboxPatch((x + gap / 2, 0), w - gap, hpx, boxstyle="round,pad=0,rounding_size=4",
                                        facecolor=col, edgecolor="none"))
        else:
            ax.add_patch(Rectangle((x + gap / 2, 0), max(w - gap, 1.0), hpx, facecolor=col, edgecolor="none"))
        if v / total > 0.08:
            ax.text(x + 8, hpx / 2, f"{v} B ({v / total * 100:.0f}%)", va="center", fontsize=9,
                    color="white", fontweight="bold")
        x += w
    fig.text(0.02, 0.86, f"SRAM 증가 {total} B 의 구성 (P1 = P2)", fontsize=12, fontweight="bold")
    legend(fig, [(f"{l}  {v} B", c) for l, v, c in parts], 0.02, 0.24, fs=8.5)
    save(fig, "fig05_sram_breakdown.svg")

# ================================================================== fig 06: DMA layout (result §3.6, How §2.1)
def fig_layout():
    S, I = B["S_P1"], B["I_P1"]
    fig, ax = canvas(11, 4.6)
    ax.text(0.01, 0.97, "DMA 배치 (NS SRAM, s_dmaTaskMem), 같은 바이트 축척", fontsize=12, fontweight="bold", va="top")
    total = 2 * S.dma["msize"]; scale = 0.86 / total; x0 = 0.12
    def strip(y, Bd, label, col):
        d = Bd.dma; ins = Bd.insecure
        ax.text(0.01, y + 0.05, label, fontsize=10, fontweight="bold", va="center")
        ax.text(0.01, y - 0.01, f"0x{d['base']:08x}\n{2 * d['msize']} B", fontsize=8, color=INK2, va="center")
        ax.add_patch(Rectangle((0.005, y - 0.075), 0.004, 0.17, facecolor=col, edgecolor="none"))
        x = x0
        for t in range(2):
            segs = [("globals", d["gsize"], NEUTRAL), ("패딩", d["coff"] - d["gsize"], GREEN),
                    ("Cluster" if not ins else "DMA 버퍼", 256, CLUSTER)]
            if not ins: segs.append(("guard", 256, MAGENTA))
            tstart = x
            for name, n, c in segs:
                w = n * scale
                ax.add_patch(Rectangle((x, y), w, 0.08, facecolor=c, edgecolor=SURF, lw=1.5))
                if w > 0.03:
                    ax.text(x + w / 2, y + 0.04, f"{'AB'[t]}.{name}\n{n}", ha="center", va="center", fontsize=7.5,
                            color="white" if c in (GREEN, MAGENTA) else INK)
                x += w
            # NS-MPU r5 bracket: globals + cluster
            r5w = (d["coff"] + 256) * scale
            ax.plot([tstart, tstart, tstart + r5w, tstart + r5w], [y + 0.10, y + 0.115, y + 0.115, y + 0.10], color=INK2, lw=1)
            ax.text(tstart + r5w / 2, y + 0.12, f"NS-MPU r5 ({'AB'[t]})", ha="center", va="bottom", fontsize=7.5, color=INK2)
            if not ins:
                cx = tstart + d["coff"] * scale
                ax.plot([cx, cx, cx + 256 * scale, cx + 256 * scale], [y - 0.01, y - 0.025, y - 0.025, y - 0.01], color=BLUE, lw=1.2)
                ax.text(cx + 128 * scale, y - 0.03, f"S-MPU 영역 {4 + t}", ha="center", va="top", fontsize=7.5, color=INK)
                gx = cx + 256 * scale
                ax.text(gx + 128 * scale, y - 0.03, "MPCBB Secure", ha="center", va="top", fontsize=7.5, color=INK)
        if ins:
            ax.text(x + 0.01, y + 0.04, "← 끝 (S 보다 960 B 작음)\nDMA1 레지스터는 NS-MPU r7", fontsize=8, color=INK2, va="center")
    strip(0.62, S, "S_P1", BLUE)
    strip(0.22, I, "I_P1", ORANGE)
    legend(fig, [("globals (사용 12 B)", NEUTRAL), ("정렬 패딩", GREEN), ("Cluster / DMA 버퍼 256 B", CLUSTER),
                 ("가드 256 B (MPCBB Secure)", MAGENTA)], 0.12, 0.09, fs=8.5)
    save(fig, "fig06_dma_layout.svg")

# ================================================================== fig 07: NS-MPU usage (result §3.5)
def fig_nsmpu():
    fig, ax = canvas(11, 3.3)
    ax.text(0.01, 0.96, "NS-MPU 영역 사용 (DMA Task 하나, FreeRTOS ARM_CM33 포트, 8 영역)", fontsize=12, fontweight="bold", va="top")
    fixed = ["0 커널\n특권 Flash", "1 커널\n비특권 Flash", "2 커널\n시스템 콜", "3 커널\n특권 SRAM", "4 태스크\n스택"]
    for row, (name, Bd, col, tint) in enumerate((("S", B["S_P1"], BLUE, "#e8f1fb"), ("I", B["I_P1"], ORANGE, "#fdeee6"))):
        y = 0.52 - row * 0.36
        ax.text(0.01, y + 0.07, name, fontsize=12, fontweight="bold", va="center", color=INK)
        regs = Bd.dma["regions"]["DMA_Task_A"]
        labels = ["globals + Cluster" if name == "S" else "globals + DMA 버퍼", "DMA src 32 B",
                  "DMA1 레지스터 32 B\n(Device, XN)"]
        for k in range(8):
            x = 0.05 + k * 0.105
            if k < 5:
                box(ax, x, y, 0.095, 0.15, fixed[k], fc=NEUTRAL2, ec=NEUTRAL2, fs=8)
            else:
                a, n, p = regs[k - 5]
                if n:
                    box(ax, x, y, 0.095, 0.15, f"{k} xRegions[{k - 5}]\n{labels[k - 5]}", fc=tint, ec=col, fs=7.5, lw=1.4)
                else:
                    box(ax, x, y, 0.095, 0.15, f"{k} 비어 있음", fc=NS_BG, ec=NEUTRAL, fs=8, tc=MUTED)
        used = sum(1 for _, n, _ in regs if n)
        ax.text(0.905, y + 0.075, f"사용자 영역\n{used} / 3", fontsize=10, fontweight="bold", va="center")
    ax.text(0.05, 0.035, "S 는 DMA 레지스터를 Secure 코드가 쓰므로 영역 7 이 남는다 (논문: NS-MPU 추가 사용 없음). "
            "I 는 비특권 Task 가 직접 쓰려면 영역 하나가 더 필요하다.", fontsize=8.5, color=INK2)
    save(fig, "fig07_nsmpu.svg")

# ================================================================== fig 08: Task B address fix (result §7.4, §7.5)
def fig_taskb():
    R = mm.Build("ref_default_P1"); C = mm.Build("chk_default_P1")
    sym = C.sym["NonSecure"]
    base = sym["s_dmaTaskMem"][0]
    tstack, tsize = sym["Timer_Stack.0"][0], sym["Timer_Stack.0"][1]
    lo, hi = base, tstack + tsize
    fig, ax = canvas(11, 4.4)
    ax.text(0.01, 1.0, "Task B S-MPU/MPCBB 주소 수정 (기본 빌드)", fontsize=12, fontweight="bold", va="top")
    sc = 0.86 / (hi - lo); X = lambda a: 0.12 + (a - lo) * sc
    y = 0.50
    # NS memory strip
    for t in range(2):
        tb = base + t * 0x300
        for off, n, c, nm in ((0, 0x100, NEUTRAL, "globals"), (0x100, 0x100, CLUSTER, "Cluster"), (0x200, 0x100, MAGENTA, "guard")):
            ax.add_patch(Rectangle((X(tb + off), y), 0x100 * sc, 0.10, facecolor=c, edgecolor=SURF, lw=1.5))
            ax.text(X(tb + off) + 0x80 * sc, y + 0.05, f"{'AB'[t]}.{nm}", ha="center", va="center", fontsize=7.5,
                    color="white" if c == MAGENTA else INK)
    mid = base + 0x600
    ax.add_patch(Rectangle((X(mid), y), (tstack - mid) * sc, 0.10, facecolor=NS_BG, edgecolor=NEUTRAL, lw=1))
    ax.text(X(mid) + (tstack - mid) * sc / 2, y + 0.05, "htim6\nuwTick", ha="center", va="center", fontsize=7, color=INK2)
    ax.add_patch(Rectangle((X(tstack), y), tsize * sc, 0.10, facecolor=NEUTRAL2, edgecolor=SURF, lw=1.5))
    ax.text(X(tstack) + tsize * sc / 2, y + 0.05, f"Timer_Stack (0x{tstack:08x}–0x{tstack + tsize - 1:08x})", ha="center", va="center", fontsize=8)
    ax.text(0.01, y + 0.05, "NS SRAM\n(실제 배치)", fontsize=8.5, va="center", fontweight="bold")
    for a in (base, base + 0x300, tstack, tstack + tsize):
        ax.text(X(a), y - 0.015, f"0x{a:05x}"[-5:], ha="center", va="top", fontsize=7, color=MUTED)
    # before (above) / after (below) markers
    old_c, old_g = 0x2001c000, 0x2001c100
    new_c, new_g = base + 0x300 + 0x100, base + 0x300 + 0x200
    for (c, g, yy, txt, ok) in ((old_c, old_g, 0.70, "수정 전: B = 0x2001c000 하드코딩 → Timer_Stack 을 태그·Secure 화", False),
                                (new_c, new_g, 0.26, f"수정 후: B = A + 0x300 = 0x{new_c:08x} → 실제 B.Cluster / B.guard", True)):
        col = INK if ok else "#c62e2e"
        for a, nm in ((c, "S-MPU 5"), (g, "MPCBB")):
            ax.add_patch(Rectangle((X(a), yy), 0x100 * sc, 0.07, facecolor="none", edgecolor=col, lw=1.6,
                                   linestyle="-" if ok else (0, (3, 2))))
            ax.text(X(a) + 0x80 * sc, yy + (0.08 if not ok else -0.01), nm, ha="center",
                    va="bottom" if not ok else "top", fontsize=7.5, color=col)
            arrow(ax, X(a) + 0x80 * sc, yy if not ok else yy + 0.07, X(a) + 0x80 * sc, y + 0.10 if not ok else y, color=col, lw=1)
        ax.text(0.12, yy + (0.15 if not ok else -0.09), txt, fontsize=9, color=col, va="center", fontweight="bold")
    ax.text(0.12, 0.035, f"검사: Secure 리터럴 대조 — 수정 전 B 불일치 ({'일치' if R.smpu['B_ok'] else '불일치'}), 수정 후 "
            f"{'일치' if C.smpu['B_ok'] else '불일치'}.  Secure 이미지 차이는 리터럴 4 워드, NS 이미지 동일.", fontsize=8.5, color=INK2)
    save(fig, "fig08_taskb_fix.svg")

# ================================================================== fig 09: map → component (How §4.2–4.3)
def fig_classify():
    fig, ax = canvas(11, 4.2)
    ax.text(0.01, 0.97, "map 입력 섹션 → S − I 차분 → 구성요소", fontsize=12, fontweight="bold", va="top")
    box(ax, 0.01, 0.30, 0.22, 0.52, "map 파일 (빌드마다)\n\n.text.verify_inst  0x32  secure_nsc.o\n.bss.s_dmaTaskMem  0x600  app_freertos.o\n.text  0xd8  libgcc.a(cmse.o)\n.fini.__stub  0x28  linker stubs\n*fill*  0xa3\n…", fs=7.8)
    box(ax, 0.28, 0.30, 0.20, 0.52, "(출력, 입력, 오브젝트) → 크기\n\nS 표, I 표를 키로 맞춤\n크기가 같은 키는 버림\n\n잔차 = 섹션 − 입력 합\n(ALIGN, 문자열 병합)", fs=8.2)
    box(ax, 0.53, 0.30, 0.20, 0.52, "COMP 매핑\n(세계, 입력 정규식, 오브젝트)\n\nverify_inst → C1\nSECURE_dma_m2m_start → C2\n…\n*fill*, 잔차 → C10\n\n미분류 1 건 이상 → 실패", fs=8.2)
    box(ax, 0.78, 0.55, 0.21, 0.27, "구성요소 표\nC1–C10 × Flash/SRAM\n(SRAM 의 PROGBITS 는\nFlash 에도 더함)", fs=8.2)
    box(ax, 0.78, 0.30, 0.21, 0.20, "검증: 구성요소 합 Δ\n= 영역 합 Δ (readelf)", fs=8.2, fc=SEC_BG)
    arrow(ax, 0.23, 0.56, 0.28, 0.56); arrow(ax, 0.48, 0.56, 0.53, 0.56); arrow(ax, 0.73, 0.68, 0.78, 0.68)
    arrow(ax, 0.885, 0.55, 0.885, 0.50)
    ax.text(0.01, 0.17, "같은 실행에서 함께 하는 검사:  NSC 허용 목록 (S 12 / I 10)  ·  금지 심볼  ·  미참조 함수 (분기 대상·리터럴·벡터·stub)  ·  "
            "S-MPU 리터럴 ↔ NS 배치 대조", fontsize=8.5, color=INK2)
    ax.text(0.01, 0.09, "하나라도 실패하면 exit 1. 결과: results/mem/summary.md, <빌드>/layout.md, components.csv", fontsize=8.5, color=INK2)
    save(fig, "fig09_classify.svg")

# ================================================================== fig 10: address map (How §4.1)
def fig_addrmap():
    S = B["S_P1"]
    fig, ax = canvas(11, 3.6)
    ax.text(0.01, 0.97, "메모리 영역 (링커 스크립트 MEMORY) 과 측정 단위", fontsize=12, fontweight="bold", va="top")
    cols = [("Secure", "S-Flash", 0x0C000000, 248), ("Secure", "S-NSC", 0x0C03E000, 8), ("NonSecure", "NS-Flash", 0x08040000, 256),
            ("Secure", "S-SRAM", 0x30000000, 96), ("NonSecure", "NS-SRAM", 0x20018000, 96)]
    x = 0.02
    for w, name, base, kb in cols:
        used = S.reg[w][name]
        bw = 0.18
        isf = "Flash" in name or "NSC" in name
        ax.add_patch(Rectangle((x, 0.26), bw, 0.52, facecolor=SEC_BG if w == "Secure" else NS_BG, edgecolor=NEUTRAL, lw=1))
        h = 0.52 * used / (kb * 1024)
        ax.add_patch(Rectangle((x, 0.26), bw, max(h, 0.004), facecolor=BLUE, edgecolor="none", alpha=0.85))
        ax.text(x + bw / 2, 0.81, f"{name}\n0x{base:08x} · {kb} KB", ha="center", va="bottom", fontsize=8.5, fontweight="bold")
        ax.text(x + bw / 2, 0.26 + max(h, 0.004) + 0.02, f"S_P1 사용 {used:,} B ({used / kb / 10.24:.1f}%)", ha="center", va="bottom", fontsize=7.8, color=INK)
        secs = {"S-Flash": ".isr_vector .text .rodata\n.init/.fini + .data LMA", "S-NSC": ".gnu.sgstubs",
                "NS-Flash": ".isr_vector .privileged_functions\n.freertos_system_calls .text\n.rodata + .data LMA",
                "S-SRAM": ".data .bss\n._user_heap_stack", "NS-SRAM": ".data .bss\n._user_heap_stack"}[name]
        ax.text(x + bw / 2, 0.23, secs, ha="center", va="top", fontsize=7.3, color=INK2)
        x += bw + 0.015
    ax.text(0.02, 0.0, "배경: 회색 = Secure, 흰색 = Non-secure. 파란 채움 = 사용량 (영역 높이 대비).  .data 는 SRAM(VMA) 과 Flash(LMA) 양쪽에 센다.",
            fontsize=8, color=INK2)
    save(fig, "fig10_address_map.svg")

# ================================================================== fig 11: runtime results (result §5)
def fig_runtime():
    import json
    pth = os.path.join(ROOT, "runtime_summary.json")
    if not os.path.exists(pth): print("skip fig11 (no runtime_summary.json)"); return
    R = json.load(open(pth))
    groups = [("힙 최대 사용", [("secure_heap", "Secure 힙"), ("ns_heap", "NS 힙")]),
              ("스택 최대 깊이", [("msp_s", "Secure MSP"), ("ctx:DMA_Task_A", "Secure ctx DMA_Task_A"),
                                ("ctx:DMA_Task_B", "Secure ctx DMA_Task_B"), ("ctx:Plain_Task", "Secure ctx Plain_Task"),
                                ("msp_ns", "NS MSP"), ("task:DMA_Task_A", "NS 스택 DMA_Task_A"),
                                ("task:DMA_Task_B", "NS 스택 DMA_Task_B")])]
    profs = [p for p in ("P1", "P2") if p in R]
    fig, axes = plt.subplots(2, len(profs), figsize=(11, 6.6), gridspec_kw={"height_ratios": [2, 7]}, squeeze=False)
    fig.subplots_adjust(wspace=0.32, hspace=0.42, top=0.86, bottom=0.10, left=0.17, right=0.97)
    for row, (gname, items) in enumerate(groups):
        xmax = max((R[p][w].get(k) or 0) for p in profs for w in "SI" for k, _ in items) * 1.45
        for col, prof in enumerate(profs):
            ax = axes[row][col]
            ax.set_xlim(0, xmax); ax.set_ylim(len(items) - 0.4, -0.6)
            ax.set_yticks(range(len(items))); ax.set_yticklabels([l for _, l in items] if col == 0 else [""] * len(items), fontsize=8.5)
            ax.tick_params(axis="y", length=0); ax.tick_params(axis="x", labelsize=8)
            ax.xaxis.grid(True, color=GRID, lw=1); ax.set_axisbelow(True); ax.spines["left"].set_visible(False)
            ax.set_title(f"{gname} — {prof}", pad=6, fontsize=10)
            for k, (key, _) in enumerate(items):
                sv, iv = R[prof]["S"].get(key), R[prof]["I"].get(key)
                for off, v, c in ((-0.17, sv, BLUE), (0.17, iv, ORANGE)):
                    if v is None:
                        ax.text(xmax * 0.01, k + off, "할당 없음", va="center", fontsize=7.5, color=MUTED)
                    else:
                        hbar(ax, k + off, 0, v, 0.30, c)
                        ax.text(v + xmax * 0.012, k + off, f"{v:,}", va="center", fontsize=7.5, color=INK2)
            ax.set_xlabel("바이트", fontsize=8)
    fig.text(0.17, 0.95, "런타임 SRAM 사용 (보드 실측, 60 초, 반복 최댓값)", fontsize=12, fontweight="bold")
    fig.text(0.17, 0.015, "MSP 는 NS 시작 후 사용분 (NS 첫 명령 시점의 MSP 부터 가장 깊은 곳까지). Secure context 는 Secure 힙 안의 Task 별 Secure 스택.",
             fontsize=8, color=INK2)
    legend(fig, [("S (Secure 중재자)", BLUE), ("I (Insecure 중재자)", ORANGE)], 0.97, 0.985, loc="upper right")
    save(fig, "fig11_runtime.svg")

# ================================================================== fig 12: runtime method (How §11)
def fig_runtime_method():
    fig, ax = canvas(11, 5.4)
    ax.text(0.01, 0.99, "런타임 측정 순서 (run_runtime.py, 빌드 하나)", fontsize=12, fontweight="bold", va="top")
    steps = [("① 플래시 + 리셋", "Secure → NS ELF\nmonitor reset", NS_BG),
             ("② NS 첫 명령에서 정지", "hbreak Reset_Handler\nSecure 부팅 끝,\nNS 미실행", NS_BG),
             ("③ 칠하기 0xa5a5a5a5", "Secure ucHeap 전체\nSecure MSP 아래쪽\nNS MSP", "#f6e3ec"),
             ("④ 60 초 실행", "브레이크포인트 없음\n(계측 간섭 없음)", NS_BG),
             ("⑤ 정지 후 읽기", "힙 카운터\n스택 긁기 (high-water)", SEC_BG),
             ("⑥ 10 초 완료 횟수", "NS 완료 ISR hbreak\n+ ignore 카운트", NS_BG),
             ("⑦ Secure 안에서 정지", "S-MPU 영역 4·5\nMPCBB VCTR13", SEC_BG)]
    w = 0.128; x = 0.01
    for i, (t, d, c) in enumerate(steps):
        box(ax, x, 0.66, w, 0.22, f"{t}\n\n{d}", fc=c, fs=7.8)
        if i < len(steps) - 1: arrow(ax, x + w, 0.77, x + w + 0.014, 0.77)
        x += w + 0.014
    ax.text(0.01, 0.58, "③ 에서 칠하는 Secure SRAM (S_P1 주소, 아래 = 낮은 주소)", fontsize=9.5, fontweight="bold")
    # Secure SRAM map (vertical strip, not to scale)
    segs = [(".data/.bss 앞부분", NEUTRAL2, 0.035), ("ucHeap 10 KB — 전체 칠함\n뒤에 Secure context 스택 1 KB 씩 잘려 나감", "#f6e3ec", 0.13),
            (".bss 나머지 + ._user_heap_stack", NEUTRAL2, 0.04), ("MSP_S 영역 — [_end, MSP_S − 64) 칠함", "#f6e3ec", 0.15),
            ("부팅 프레임 + 64 B (칠하지 않음)", NEUTRAL, 0.04)]
    y = 0.06
    for name, c, h in segs:
        ax.add_patch(Rectangle((0.03, y), 0.30, h, facecolor=c, edgecolor=SURF, lw=1.5))
        ax.text(0.18, y + h / 2, name, ha="center", va="center", fontsize=7.6)
        y += h
    ax.text(0.03, 0.035, "0x30000000", fontsize=7, color=MUTED); ax.text(0.03, y + 0.008, "_estack 0x30018000", fontsize=7, color=MUTED)
    # scan illustration
    ax.text(0.40, 0.58, "⑤ 긁기: 낮은 끝에서 위로, 처음 깨진 워드 = 최대 깊이", fontsize=9.5, fontweight="bold")
    sx, sw = 0.42, 0.10
    for k in range(10):
        used = k >= 6
        ax.add_patch(Rectangle((sx, 0.06 + k * 0.045), sw, 0.04, facecolor=CLUSTER if used else "#f6e3ec", edgecolor=SURF, lw=1.2))
        ax.text(sx + sw / 2, 0.08 + k * 0.045, "값" if used else "a5a5a5a5", ha="center", va="center", fontsize=7)
    arrow(ax, sx - 0.02, 0.07, sx - 0.02, 0.33, color=INK2)
    ax.text(sx - 0.025, 0.20, "스캔", ha="right", va="center", fontsize=8, color=INK2)
    ax.plot([sx + sw + 0.01, sx + sw + 0.03], [0.06 + 6 * 0.045] * 2, color=INK, lw=1)
    ax.text(sx + sw + 0.035, 0.06 + 6 * 0.045, "high-water", va="center", fontsize=8)
    ax.text(sx + sw + 0.035, 0.06 + 10 * 0.045, "스택 시작 (top)", va="top", fontsize=8, color=INK2)
    ax.text(sx + sw + 0.035, 0.07, "스택 끝 (limit)", va="center", fontsize=8, color=INK2)
    ax.text(0.68, 0.50, "사용량 = top − high-water\n\n• NS Task 스택: tasks.c 가 생성 때 0xa5 로 채움\n  → 칠하지 않고 긁기만\n"
            "• Secure context 스택: 채워지지 않음\n  → 할당 전에 ucHeap 전체를 칠함\n• MSP: 아무도 채우지 않음 → 직접 칠함\n\n"
            "• 힙: xMinimumEverFreeBytesRemaining\n  (부팅 이후 최소 잔량) 으로 최대 사용",
            fontsize=8.3, va="top", color=INK)
    save(fig, "fig12_runtime_method.svg")

for f in (fig_pipeline, fig_paths, fig_regions, fig_components, fig_sram, fig_layout, fig_nsmpu, fig_taskb, fig_classify, fig_addrmap,
          fig_runtime, fig_runtime_method):
    f()
