import json, sys
L = json.load(open(sys.argv[1]))
cl = {c["id"]: c for c in L["clusters"]}
def bufs(cid): return sorted(b["name"] for b in cl[cid]["buffers"])
kinds = sorted({(d["kind"], d["severity"], d["task"]) for d in L["diagnostics"]})
fails = []
def chk(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond: fails.append(msg)
chk(sorted(L["tasks"]) == ["taskA", "taskB", "taskH", "taskS", "taskV", "timerTask"], f"tasks {sorted(L['tasks'])}")
chk("t.timerTask" not in cl and all("timerTask" not in c["owners"] for c in L["clusters"]),
    "timerTask 의 void(ptr) 간접 호출이 태스크 함수로 연결되지 않음")
chk(bufs("t.taskA") == ["bufA", "bufP1", "bufP2"], f"t.taskA = {bufs('t.taskA')} (Argument 역추적 + 포인터 변수 후보 둘)")
chk(bufs("t.taskB") == ["bufAnn", "bufB"], f"t.taskB = {bufs('t.taskB')} (attack() 제외, annotate)")
chk(cl["t.taskB"]["owners"] == ["taskB"], "t.taskB owner = taskB 만 (attack 의 bufB 사용 무시)")
shared = [c for c in L["clusters"] if c["kind"] == "shared"]
chk(len(shared) == 1 and [b["name"] for b in shared[0]["buffers"]] == ["bufShared"]
    and shared[0]["owners"] == ["taskA", "taskB"], "bufShared → 공유 cluster {taskA,taskB}")
chk(("pointer-var", "warning", "taskA") in kinds, "gp: pointer-var warning")
chk(("heap-buffer", "error", "taskH") in kinds, "pvPortMalloc: heap-buffer error")
chk(("stack-buffer", "error", "taskS") in kinds, "지역 배열: stack-buffer error")
chk(bufs("t.taskS") == ["bufTest"], "taskS 의 정상 버퍼는 그대로 배치")
chk(("unresolved-load", "warning", "taskB") in kinds, "taskB c->dst: unresolved-load 가 annotate 덕분에 warning")
chk(bufs("t.taskV") == ["bufV", "bufV2"], f"t.taskV = {bufs('t.taskV')} (inline asm svc #110, r1)")
chk(all(b["name"] != "bufNo" for c in L["clusters"] for b in c["buffers"]), "svc #111 은 sink 아님")
chk(L["errors"] == 2, f"errors = {L['errors']} (heap, stack)")
sys.exit(1 if fails else 0)
