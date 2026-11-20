# gdb-multiarch -batch -x scripts/eval/icache_mon.py  (env DIR). ICACHE hit/miss monitor per bench table (no halts inside a table).
import gdb, os, struct, threading
d = os.environ['DIR']
files = {k: [f for f in os.listdir(d) if f.endswith(k + '.elf')][0] for k in ('_NonSecure', '_Secure')}
CR, HM, MM = 0x50030400, 0x50030410, 0x50030414
gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('file %s/%s' % (d, files['_NonSecure']))
gdb.execute('target remote localhost:61234')
gdb.execute('load %s/%s' % (d, files['_Secure'])); gdb.execute('load %s/%s' % (d, files['_NonSecure']))
gdb.execute('monitor reset')
gdb.execute('hbreak bench_hit')
inf = gdb.selected_inferior()
def rd(a): return struct.unpack('<I', bytes(inf.read_memory(a, 4)))[0]
def wr(a, v): inf.write_memory(a, struct.pack('<I', v))
def reset_mon():
    c = rd(CR) | (3 << 16)
    wr(CR, c | (3 << 18)); wr(CR, c)
out = []
first = True
while True:
    fired = []
    t = threading.Timer(60, lambda: (fired.append(1), gdb.post_event(lambda: gdb.execute('interrupt'))))
    t.start(); gdb.execute('continue', to_string=True); t.cancel()
    if fired: raise gdb.GdbError('TIMEOUT')
    tid = rd(0x20030004)
    if not first: out.append((tid, rd(HM), rd(MM)))
    first = False
    if tid == 0xFF: break
    reset_mon()
with open(d + '/icmon.txt', 'w') as f:
    f.write('# tid hits misses (counted from previous bench_hit to this one; ~1001 iterations)\n')
    for r in out: f.write('%d %d %d\n' % r)
print('done', d, out)
gdb.execute('detach')
