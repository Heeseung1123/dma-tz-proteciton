import json, sys
L = json.load(open(sys.argv[1]))
cl = {c["id"]: c for c in L["clusters"]}
def bufs(cid): return sorted(b["name"] for b in cl[cid]["buffers"])
kinds = sorted({(d["kind"], d["severity"], d["task"]) for d in L["diagnostics"]})
fails = []
def chk(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond: fails.append(msg)
chk(sorted(L["tasks"]) == ["taskA", "taskB", "taskH", "taskQ", "taskS", "taskT", "taskV", "timerTask"], f"tasks {sorted(L['tasks'])}")
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
T, Q = cl["t.taskT"], cl["t.taskQ"]
bT = {b["name"]: b for b in T["buffers"]}
chk(list(bT) == ["s_memT"] and T["head"] == 32 and bT["s_memT"]["head"] == 32 and bT["s_memT"]["section"] == ".dma.h.taskT",
    f"struct 일부 DMA: s_memT head 32 (상수/가변 인덱스 모두 dst 필드), section .dma.h.taskT ({T['head']})")
chk(("struct-field", "warning", "taskT") in kinds, "struct-field 경고 (head 설명)")
bQ = {b["name"]: b for b in Q["buffers"]}
chk(sorted(bQ) == ["bufH", "bufQ"] and bQ["bufH"]["hosted"] and bQ["bufH"]["section"] == ".dma.x.taskQ"
    and Q["hosted_owners"] == ["taskT"] and Q["owners"] == ["taskQ"] and Q["hosted_bound"] == 16,
    "shared_host: bufH 는 taskQ cluster 끝 (.dma.x.taskQ), 추가 region owner = taskT")
chk(not any(c["kind"] == "shared" and any(b["name"] == "bufH" for b in c["buffers"]) for c in L["clusters"]),
    "bufH 는 별도 공유 cluster 를 만들지 않음")
sys.exit(1 if fails else 0)
