# gdb-multiarch -batch -x run_bench.py  (env: DIR=결과 디렉터리, TMO=테스트당 제한 초)
# 두 ELF 를 flash -> reset -> bench_hit 마다 NS(SRAM2)/Secure(g_bT) 표를 CSV 로 회수.
import gdb, os, struct, threading, csv, time
d = os.environ['DIR']; tmo = float(os.environ.get('TMO', '60'))
ns = [f for f in os.listdir(d) if f.endswith('_NonSecure.elf')][0]
s  = [f for f in os.listdir(d) if f.endswith('_Secure.elf')][0]
gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('file %s/%s' % (d, ns))
gdb.execute('add-symbol-file %s/%s' % (d, s))
gdb.execute('target remote localhost:61234')
gdb.execute('load %s/%s' % (d, s)); gdb.execute('load %s/%s' % (d, ns))
gdb.execute('monitor reset')
gdb.execute('hbreak bench_hit')
inf = gdb.selected_inferior()
def rd(addr, n): return struct.unpack('<%dI' % n, bytes(inf.read_memory(addr, 4 * n)))
gt = int(gdb.parse_and_eval('(unsigned int)&g_bT[0]'))
BASE = 0x20030000; ROWS = BASE + 0x40; K_NS = 6; K_S = 8
log = open(d + '/run.log', 'w')
while True:
    fired = {'v': False}
    def to():
        fired['v'] = True
        gdb.post_event(lambda: gdb.execute('interrupt'))
    t = threading.Timer(tmo, to); t.start()
    gdb.execute('continue', to_string=True)
    t.cancel()
    if fired['v']:
        pc = gdb.execute('info registers pc', to_string=True).strip()
        bt = gdb.execute('bt 6', to_string=True)
        log.write('TIMEOUT %s\n%s\n' % (pc, bt)); print('TIMEOUT', pc); print(bt); break
    magic, tid, n, kn, ks, arg = rd(BASE, 6)
    if magic != 0xBE4C0001:
        log.write('BAD MAGIC %x\n' % magic); print('BAD MAGIC'); break
    if tid == 0xFF:
        break
    nsv = rd(ROWS, n * K_NS) if kn else ()
    sv  = rd(gt, n * K_S) if ks else ()
    with open('%s/t%d.csv' % (d, tid), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['i', 'arg'] + ['n%d' % j for j in range(kn)] + ['s%d' % j for j in range(ks)])
        for i in range(n):
            row = [i, arg]
            row += [nsv[i * K_NS + j] for j in range(kn)]
            row += [sv[i * K_S + j] for j in range(ks)]
            w.writerow(row)
    log.write('ok id=%d n=%d arg=%d\n' % (tid, n, arg)); log.flush()
    print('ok id=%d n=%d arg=%d' % (tid, n, arg))
env = {}
env['g_bEnv(DWT_CTRL,DAUTHSTATUS,DHCSR,ICACHE_CR)'] = ['%08x' % v for v in rd(int(gdb.parse_and_eval('(unsigned int)&g_bEnv[0]')), 4)]
try:
    env['g_bBoot'] = list(rd(int(gdb.parse_and_eval('(unsigned int)&g_bBoot[0]')), 10))
except gdb.error:
    env['g_bBoot'] = 'n/a'
env['FLASH_ACR'] = '%08x' % rd(0x50022000, 1)[0]
env['ICACHE_CR'] = '%08x' % rd(0x50030400, 1)[0]
env['RCC_CFGR'] = '%08x' % rd(0x50021008, 1)[0]
env['SystemCoreClock'] = int(gdb.parse_and_eval('SystemCoreClock'))
with open(d + '/env.txt', 'w') as f:
    for k, v in env.items(): f.write('%s = %s\n' % (k, v))
    log.write('done\n')
print(env)
log.close()
gdb.execute('detach')
