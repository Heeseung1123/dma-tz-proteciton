# gdb-multiarch -batch -x scripts/eval/run_bench.py   (env DIR=결과 디렉터리)
# 두 ELF flash -> reset -> bench_hit 마다 SRAM2 표를 CSV 로 옮기고 환경 레지스터를 기록한다.
import gdb, os, struct, threading, csv
d = os.environ['DIR']
files = {k: [f for f in os.listdir(d) if f.endswith(k + '.elf')][0] for k in ('_NonSecure', '_Secure')}
HDR, ROWS, COLS = 0x20030000, 0x20030040, 2
NAMES = {1: ['ns_calib'], 2: ['empty_nsc'], 3: ['start_total', 'inner_t4_t1'],
         4: ['inner_calib'], 5: ['verify'], 6: ['verify_calib'],
         7: ['start_table_total', 'inner_table_t4_t1'], 8: ['verify_table']}
gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('file %s/%s' % (d, files['_NonSecure']))
gdb.execute('target remote localhost:61234')
gdb.execute('load %s/%s' % (d, files['_Secure'])); gdb.execute('load %s/%s' % (d, files['_NonSecure']))
gdb.execute('monitor reset')
gdb.execute('hbreak bench_hit')
inf = gdb.selected_inferior()
def rd(addr, n): return struct.unpack('<%dI' % n, bytes(inf.read_memory(addr, 4 * n)))
while True:
    fired = []
    t = threading.Timer(60, lambda: (fired.append(1), gdb.post_event(lambda: gdb.execute('interrupt'))))
    t.start(); gdb.execute('continue', to_string=True); t.cancel()
    if fired:
        raise gdb.GdbError('TIMEOUT ' + gdb.execute('bt 5', to_string=True))
    magic, tid, n = rd(HDR, 3)
    if magic != 0xBE4C0002: raise gdb.GdbError('bad magic')
    if tid == 0xFF: break
    v = rd(ROWS, n * COLS)
    with open('%s/%s.csv' % (d, NAMES[tid][0]), 'w', newline='') as f:
        w = csv.writer(f); w.writerow(NAMES[tid])
        for i in range(n): w.writerow(v[i * COLS:i * COLS + len(NAMES[tid])])
with open(d + '/env.txt', 'w') as f:
    f.write('SystemCoreClock = %d\n' % int(gdb.parse_and_eval('SystemCoreClock')))
    for name, a in [('FLASH_ACR', 0x50022000), ('ICACHE_CR', 0x50030400), ('DWT_CTRL', 0xE0001000), ('DAUTHSTATUS', 0xE000EFB8), ('DHCSR', 0xE000EDF0)]:
        f.write('%s = %08x\n' % (name, rd(a, 1)[0]))
print('done', d)
gdb.execute('detach')
