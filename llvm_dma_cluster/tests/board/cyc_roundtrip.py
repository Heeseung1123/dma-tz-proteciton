import gdb, os, statistics
S, N = os.environ["SELF"], os.environ["NSELF"]
sites = [int(x, 16) for x in os.environ["SITES"].split(",")]
gdb.execute("set pagination off"); gdb.execute("set confirm off")
gdb.execute(f"file {S}"); gdb.execute(f"add-symbol-file {N}")
gdb.execute("target remote localhost:61234")
gdb.execute(f"load {S}"); gdb.execute(f"load {N}")
gdb.execute("monitor reset")
def rd(a): return int(gdb.parse_and_eval(f"*(unsigned*){a:#x}")) & 0xffffffff
def wr(a, v): gdb.execute(f"set *(unsigned*){a:#x} = {v:#x}")
res = {}
for site in sites:
    gdb.execute("delete")
    gdb.execute(f"hbreak *{site:#x}"); gdb.execute(f"hbreak *{site+2:#x}")
    smp = []
    for k in range(16):
        gdb.execute("continue", to_string=True)
        pc = int(gdb.parse_and_eval("$pc")) & ~1
        if pc != site:
            continue
        r1 = int(gdb.parse_and_eval("$r1")) & 0xffffffff
        wr(0xE000EDFC, rd(0xE000EDFC) | (1 << 24)); wr(0xE0001000, rd(0xE0001000) | 1)
        c0 = rd(0xE0001004)
        gdb.execute("continue", to_string=True)
        pc2 = int(gdb.parse_and_eval("$pc")) & ~1
        c1 = rd(0xE0001004); r0 = int(gdb.parse_and_eval("$r0"))
        if pc2 == site + 2:
            smp.append(((c1 - c0) & 0xffffffff, r1, r0))
    res[site] = smp
for site, smp in res.items():
    v = [s[0] for s in smp[4:]]   # 앞 4 회 = warm-up
    print(f"SITE {site:#x} dst={smp[0][1]:#x} ret={set(s[2] for s in smp)} n={len(v)} min={min(v)} median={statistics.median(v)} all={v}")
gdb.execute("delete"); gdb.execute("detach")
