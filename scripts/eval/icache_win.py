# gdb-multiarch -batch -x scripts/eval/icache_win.py  (env DIR, CPSIE=hex addr of cpsie in bench_isr_table). ICACHE misses in T0..t1 window via hbreak; perturbs (debugger reads), use as supporting data only.
import gdb, os, struct, threading
d = os.environ['DIR']; A0 = int(os.environ['CPSIE'], 16); A1 = A0 + 10   # after t1 CYCCNT read
files = {k: [f for f in os.listdir(d) if f.endswith(k + '.elf')][0] for k in ('_NonSecure', '_Secure')}
CR, HM, MM, SYST = 0x50030400, 0x50030410, 0x50030414, 0xE002E010
gdb.execute('set pagination off'); gdb.execute('set confirm off'); gdb.execute('set trust-readonly-sections on')
gdb.execute('file %s/%s' % (d, files['_NonSecure']))
gdb.execute('target remote localhost:61234')
gdb.execute('load %s/%s' % (d, files['_Secure'])); gdb.execute('load %s/%s' % (d, files['_NonSecure']))
gdb.execute('monitor reset')
inf = gdb.selected_inferior()
def rd(a): return struct.unpack('<I', bytes(inf.read_memory(a, 4)))[0]
def wr(a, v): inf.write_memory(a, struct.pack('<I', v))
def cont():
    fired = []
    t = threading.Timer(60, lambda: (fired.append(1), gdb.post_event(lambda: gdb.execute('interrupt'))))
    t.start(); gdb.execute('continue', to_string=True); t.cancel()
    if fired: raise gdb.GdbError('TIMEOUT')
def pc(): return int(gdb.parse_and_eval('$pc')) & ~1
gdb.execute('hbreak bench_hit')
res = {}
while True:
    cont()
    tid = rd(0x20030004)
    if tid in (9, 10, 11):                         # next table is 10 (S) / 12 (N)
        name = {9: 'S', 10: 'CAL_N', 11: 'N'}[tid]
        syst = rd(SYST); wr(SYST, syst & ~1)   # stop NS SysTick so no tick pends during halts
        gdb.execute('hbreak *0x%x' % A0); gdb.execute('hbreak *0x%x' % A1)
        rows = []
        for i in range(40):
            cont(); assert pc() == A0, hex(pc())
            c = rd(CR) | (3 << 16); wr(CR, c | (3 << 18)); wr(CR, c)
            cont(); assert pc() == A1, hex(pc())
            rows.append((rd(HM), rd(MM), 0))
        gdb.execute('delete %d %d' % (2 + 2 * len(res), 3 + 2 * len(res)))
        wr(SYST, syst)
        res[name] = rows[5:]                   # drop first iterations (warm-up after halts)
    if tid == 0xFF or len(res) == 3: break
with open(d + '/icwin.txt', 'w') as f:
    for k, rows in res.items():
        for r in rows: f.write('%s %d %d %d\n' % ((k,) + r))
for k, rows in res.items():
    print('done', d, k, 'misses', sorted(set(r[1] for r in rows)), 'hits', sorted(set(r[0] for r in rows)))
gdb.execute('detach')
