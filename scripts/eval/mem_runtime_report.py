#!/usr/bin/env python3
"""Runtime SRAM report: S vs I from results/mem/<build>/runtime*.json (run_runtime.py) + static .su frames.

  python3 -I scripts/eval/mem_runtime_report.py [results/mem]   ->  results/mem/runtime.md, results/mem/runtime_summary.json

Repetitions (runtime.json, runtime_2.json, ...): a high-water mark is a maximum, so the report takes the max over
repetitions and prints the range when the repetitions differ.
"""
import os, sys, json, glob, re

ROOT = sys.argv[1] if len(sys.argv) > 1 else "results/mem"
PAIRS = [("P1", "S_P1", "I_P1"), ("P2", "S_P2", "I_P2")]

def reps(b):
    fs = sorted(glob.glob(os.path.join(ROOT, b, "runtime*.json")))
    return [json.load(open(f)) for f in fs if not f.endswith("_try.json")]

def su(b, world, fn):
    for f in glob.glob(os.path.join(ROOT, b, "su", world, "**", "*.su"), recursive=True):
        for l in open(f):
            parts = l.rstrip("\n").split("\t")
            if len(parts) >= 3 and re.search(r":%s$" % re.escape(fn), parts[0]):
                return int(parts[1]), parts[2]
    return None

def metrics(R):
    """Flatten one run into {metric: bytes used}."""
    m = {}
    for k in ("secure_heap", "ns_heap"):
        m[k] = R[k]["total"] - R[k]["min_ever_free"]
    for c in R["secure_contexts"]:
        m["ctx:" + c["task"]] = c["used"]
    # MSP use after NS start: from the MSP value when NS starts down to the deepest word reached.
    # Secure: MSP_S at the NS reset (Secure main's frames above it are boot-only). NS: SP = _estack at the NS reset.
    def run_use(mm, top):
        u = mm["used_below_painted_top"]; hi = int(mm["painted"][1], 16)
        return top - (hi - u) if u else None
    m["msp_s"] = run_use(R["msp_s"], R["msp_s_at_ns_reset"])
    m["msp_ns"] = run_use(R["msp_ns"], int(R["msp_ns"]["painted"][1], 16) + R["msp_ns"]["unpainted_top"])
    for t, v in R["ns_task_stacks"].items():
        m["task:" + t] = v["used"]
    return m

def agg(runs):
    out = {}
    for r in runs:
        for k, v in metrics(r).items():
            out.setdefault(k, []).append(v)
    return {k: (max(x for x in v if x is not None) if any(x is not None for x in v) else None, v) for k, v in out.items()}

def cell(a):
    if a is None: return "할당 없음"
    mx, vs = a
    vs = [v for v in vs if v is not None]
    return f"{mx}" if len(set(vs)) <= 1 else f"{mx} ({min(vs)}–{mx})"

def dd(a, b):
    if a is None or b is None or a[0] is None or b[0] is None: return "—"
    return f"{a[0] - b[0]:+d}"

LABEL = {"secure_heap": "Secure 힙 최대 사용 (10240 중)", "ns_heap": "NS 힙 최대 사용 (8192 중)",
         "msp_s": "Secure MSP 사용 (NS 시작 후)", "msp_ns": "NS MSP 사용 (NS 시작 후)"}

summary = {}
out = ["# 런타임 SRAM 사용량 (scripts/eval/mem_runtime_report.py 생성)", "",
       "보드 STM32L562E-DK, `run_runtime.py` (HowmemUsageWork.md §11). 단위 바이트. 스택은 최대 사용 깊이(high-water).",
       "값은 반복 측정의 최댓값, 괄호는 반복 간 범위 (다를 때만).", ""]
for prof, sn, iname in PAIRS:
    SR, IR = reps(sn), reps(iname)
    if not SR or not IR:
        out += [f"## {prof}", "", "(측정 없음)", ""]; continue
    A, B = agg(SR), agg(IR)
    summary[prof] = {"S": {k: v[0] for k, v in A.items()}, "I": {k: v[0] for k, v in B.items()}}
    out += [f"## {prof}", "",
            f"반복: S {len(SR)} 회, I {len(IR)} 회. 측정 구간 {SR[0]['secs']} 초 (xTickCount S {[r['xTickCount'] for r in SR]}, I {[r['xTickCount'] for r in IR]}).", "",
            "### 힙과 MSP", "", "| 항목 | S | I | Δ (S − I) |", "|---|---|---|---|"]
    for k in ("secure_heap", "ns_heap", "msp_s", "msp_ns"):
        out.append(f"| {LABEL[k]} | {cell(A.get(k))} | {cell(B.get(k))} | {dd(A.get(k), B.get(k))} |")
    out += ["", "### Secure context 스택 (Secure 힙 안, 각 1024 B)", "", "| Task | S | I | Δ |", "|---|---|---|---|"]
    for k in sorted({k for k in A if k.startswith("ctx:")} | {k for k in B if k.startswith("ctx:")}):
        out.append(f"| `{k[4:]}` | {cell(A.get(k))} | {cell(B.get(k))} | {dd(A.get(k), B.get(k))} |")
    out += ["", "### NS Task 스택", "", "| Task | 크기 | S | I | Δ |", "|---|---|---|---|---|"]
    for t, v in SR[0]["ns_task_stacks"].items():
        k = "task:" + t
        out.append(f"| `{t}` | {v['size']} | {cell(A.get(k))} | {cell(B.get(k))} | {dd(A.get(k), B.get(k))} |")
    out += ["", "### 기능 확인 (스택 측정 뒤 따로 센 구간)", "", "| 항목 | S | I |", "|---|---|---|"]
    for lab, f in (("완료 ISR 횟수 / 초 / tick", lambda r: f"{r['completions']['hits']} / {r['completions']['secs']} / {r['completions']['ticks']}"),
                   ("Task A Cluster 8 B", lambda r: f"{r['functional']['taskA']['dst']}: {r['functional']['taskA']['dst8']}"),
                   ("Task B Cluster 8 B", lambda r: f"{r['functional']['taskB']['dst']}: {r['functional']['taskB']['dst8']}"),
                   ("dmaStatus A / B", lambda r: f"{r['functional']['taskA']['dmaStatus']} / {r['functional']['taskB']['dmaStatus']}"),
                   ("DMA1_CH1 CCR (NS alias 로 읽음)", lambda r: r['functional']['dma1_ch1_ccr'])):
        out.append(f"| {lab} | {'; '.join(f(r) for r in SR)} | {'; '.join(f(r) for r in IR)} |")
    s0 = SR[0]["functional"]
    if "smpu" in s0:
        out.append(f"| Secure MPU 영역 4 / 5 (RBAR, RLAR) | {s0['smpu']['4']} / {s0['smpu']['5']} | — |")
        out.append(f"| MPCBB1 VCTR13 | {s0['mpcbb1_vctr13']} | — |")
    out += ["", "### 정적 프레임 (`-fstack-usage`, 함수 하나의 프레임, 호출 깊이 미포함)", "",
            "| 함수 | 세계 | S | I |", "|---|---|---|---|"]
    for fn, w in (("SECURE_dma_m2m_start", "Secure"), ("verify_inst", "Secure"), ("SECURE_dma_m2m_init", "Secure"),
                  ("DMA1_Channel1_IRQHandler", "Secure"), ("GTZC_IRQHandler", "Secure"),
                  ("DFSDM1_FLT3_IRQHandler", "NonSecure"), ("DMA1_Channel1_IRQHandler", "NonSecure"),
                  ("vDmaTaskA", "NonSecure"), ("vDmaTaskB", "NonSecure")):
        a, b = su(sn, w, fn), su(iname, w, fn)
        g = lambda x: f"{x[0]} ({x[1]})" if x else "없음"
        out.append(f"| `{fn}` | {w} | {g(a)} | {g(b)} |")
    out.append("")
# default build (Task B fix check)
D = reps("chk_default_P1")
if D:
    r = D[0]; f = r["functional"]
    out += ["## 기본 빌드 (Task B 수정 확인, chk_default_P1)", "", "| 항목 | 값 |", "|---|---|",
            f"| 완료 ISR 횟수 / 초 | {r['completions']['hits']} / {r['completions']['secs']} |",
            f"| Task A Cluster | {f['taskA']['dst']}: {f['taskA']['dst8']}, status {f['taskA']['dmaStatus']} |",
            f"| Task B Cluster | {f['taskB']['dst']}: {f['taskB']['dst8']}, status {f['taskB']['dmaStatus']} |",
            f"| Secure MPU 영역 4 / 5 | {f.get('smpu', {}).get('4')} / {f.get('smpu', {}).get('5')} |",
            f"| MPCBB1 VCTR13 | {f.get('mpcbb1_vctr13')} |", ""]
    summary["default"] = {"functional": f, "completions": r["completions"]}
open(os.path.join(ROOT, "runtime.md"), "w").write("\n".join(out) + "\n")
json.dump(summary, open(os.path.join(ROOT, "runtime_summary.json"), "w"), indent=1)
print("\n".join(out))
