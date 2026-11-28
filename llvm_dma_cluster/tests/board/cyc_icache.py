# svc → 다음 명령 사이클 + ICACHE hit/miss (2 정지점만), ICACHE on/off
import gdb, os, statistics, struct
S, N, site = os.environ["SELF"], os.environ["NSELF"], int(os.environ["SITE"], 16)
CR, HM, MM = 0x50030400, 0x50030410, 0x50030414
gdb.execute("set pagination off"); gdb.execute("set confirm off")
gdb.execute(f"file {S}"); gdb.execute(f"add-symbol-file {N}")
gdb.execute("target remote localhost:61234")
gdb.execute(f"load {S}"); gdb.execute(f"load {N}")
inf = gdb.selected_inferior()
def rd(a): return struct.unpack('<I', bytes(inf.read_memory(a, 4)))[0]
def wr(a, v): inf.write_memory(a, struct.pack('<I', v))
for mode in ("icache_on", "icache_off"):
    gdb.execute("monitor reset")
    gdb.execute("delete"); gdb.execute(f"hbreak *{site:#x}", to_string=True); gdb.execute(f"hbreak *{site+2:#x}", to_string=True)
    smp = []
    for k in range(14):
        gdb.execute("continue", to_string=True)
        if (int(gdb.parse_and_eval("$pc")) & ~1) != site: continue
        if mode == "icache_off" and rd(CR) & 1:
            wr(CR, rd(CR) & ~1)
            while rd(0x50030404) & 1: pass       # SR.BUSYF
        wr(0xE000EDFC, rd(0xE000EDFC) | (1 << 24)); wr(0xE0001000, rd(0xE0001000) | 1)
        c = rd(CR) | (3 << 16); wr(CR, c | (3 << 18)); wr(CR, c)
        c0 = rd(0xE0001004)
        gdb.execute("continue", to_string=True)
        if (int(gdb.parse_and_eval("$pc")) & ~1) == site + 2:
            smp.append(((rd(0xE0001004) - c0) & 0xffffffff, rd(HM), rd(MM)))
    v = smp[4:]
    print(f"{mode}: cycles={sorted(set(x[0] for x in v))} median={statistics.median(x[0] for x in v)} hit={sorted(set(x[1] for x in v))} miss={sorted(set(x[2] for x in v))}  ICACHE_CR={rd(CR):#x}")
gdb.execute("delete"); gdb.execute("detach")
