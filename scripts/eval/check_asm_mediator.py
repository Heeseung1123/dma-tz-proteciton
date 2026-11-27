# 스택 없는 중재자 기능 확인 (gap H). 먼저 ~/st-link.sh 실행.
#   DIR=<build_variant.sh 결과 디렉터리> gdb-multiarch -batch -x scripts/eval/check_asm_mediator.py
# 기본 빌드(BENCH 없음)용. MEDIATOR_ASM=0 빌드에도 쓸 수 있다 (같은 확인, C 중재자 호출 지점).
#
# 1. flash, reset, 3 s 실행 후 정지. 두 Task 의 Cluster 앞 8 B 를 0 으로 지운다.
# 2. 중단점 (모두 NS 코드, hbreak):
#    - NS veneer (__SECURE_dma_m2m_start[_asm]_veneer) 진입: 호출 수. 지정한 회차에서 인자를 변조한다 (공격 (a)).
#    - 각 호출 지점의 복귀 주소: r0 (0/-1) 와 Secure s_busy 를 기록한다.
#    - NS doorbell ISR (DFSDM1_FLT3_IRQHandler): 완료 통지 수.
#    중단점은 stop() 에서 False 를 돌려 멈추지 않고 센다.
# 3. 12 s 더 실행 후 정지: 결과를 DIR/func_check.txt 에 쓴다.
import gdb, os, subprocess, threading, struct
d = os.environ['DIR']
ns = [f for f in os.listdir(d) if f.endswith('_NonSecure.elf')][0]
se = [f for f in os.listdir(d) if f.endswith('_Secure.elf')][0]
NSE, SEE = '%s/%s' % (d, ns), '%s/%s' % (d, se)

def sym(elf, name):
    out = subprocess.run(['arm-none-eabi-nm', elf], capture_output=True, text=True).stdout
    for l in out.splitlines():
        p = l.split()
        if len(p) == 3 and p[2] == name: return int(p[0], 16)
    raise gdb.GdbError('no symbol ' + name)

asm = True
try: VENEER = sym(NSE, '__SECURE_dma_m2m_start_asm_veneer')
except gdb.GdbError: asm = False; VENEER = sym(NSE, '__SECURE_dma_m2m_start_veneer')
vname = '__SECURE_dma_m2m_start%s_veneer' % ('_asm' if asm else '')
dis = subprocess.run(['arm-none-eabi-objdump', '-d', NSE], capture_output=True, text=True).stdout
RET = [int(l.split(':')[0], 16) + 4 for l in dis.splitlines() if ('bl\t' in l and '<%s>' % vname in l)]
S_BUSY = sym(SEE, 's_busy')
BASE = sym(NSE, 's_dmaTaskMem')
# dma_task_mem_t: globals 256 | cluster 256 | guard 256 (기본 빌드 배치, check_layout.sh)
STRIDE, CL = 0x300, 0x100
CLUSTER = [BASE + CL, BASE + STRIDE + CL]
GLOBALS = [BASE, BASE + STRIDE]

gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('file ' + NSE)
gdb.execute('target remote localhost:61234')
gdb.execute('load ' + SEE); gdb.execute('load ' + NSE)
gdb.execute('monitor reset')
inf = gdb.selected_inferior()
def rd(a, n): return bytes(inf.read_memory(a, n))
def wr(a, b): inf.write_memory(a, b)
def reg(r): return int(gdb.parse_and_eval('$' + r)) & 0xffffffff

def run_for(sec):
    threading.Timer(sec, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
    gdb.execute('continue', to_string=True)

run_for(3.0)
pc0 = gdb.execute('info symbol $pc', to_string=True).strip()
for c in CLUSTER: wr(c, b'\0' * 8)

# 변조 시나리오: 진입 회차 -> (이름, 변조 함수(r1, r2) -> (r1, r2))
def other(r1): return CLUSTER[1] if r1 == CLUSTER[0] else CLUSTER[0]
def own_globals(r1): return GLOBALS[0] if r1 == CLUSTER[0] else GLOBALS[1]
TAMPER = {
    4:  ('a-1 다른 Task 의 Cluster',          lambda r1, r2: (other(r1), r2)),
    6:  ('a-2 자기 globals (Cluster 밖)',      lambda r1, r2: (own_globals(r1), r2)),
    8:  ('a-3 len 이 Cluster 넘어 가드까지',   lambda r1, r2: (r1, CL + 32)),
    10: ('a-4 len = 0',                        lambda r1, r2: (r1, 0)),
    12: ('a-5 주소 오버플로 (len = 2^32-4)',   lambda r1, r2: (r1, 0xFFFFFFFC)),
    14: ('a-6 Secure 주소 (s_busy)',           lambda r1, r2: (S_BUSY, r2)),
}
log = {'in_call': False, 'dup': 0, 'enter': 0, 'ok': 0, 'rej': 0, 'doorbell': 0, 'busy_after': [], 'tamper': [], 'pending': None}

class Enter(gdb.Breakpoint):
    def stop(self):
        log['enter'] += 1; k = log['enter']; log['in_call'] = True
        if k in TAMPER:
            name, f = TAMPER[k]
            r1, r2 = f(reg('r1'), reg('r2'))
            gdb.execute('set $r1 = %d' % r1); gdb.execute('set $r2 = %d' % r2)
            log['pending'] = (k, name, r1, r2)
        return False
class Ret(gdb.Breakpoint):
    def stop(self):
        # 같은 복귀 지점이 두 번 걸릴 수 있다: asm 중재자는 DMA 완료 IRQ 를 PRIMASK_S 로 복귀 직전까지 막으므로,
        # gdb 가 이 중단점을 넘어가려고 single-step 할 때 그 IRQ 가 먼저 들어오고 같은 PC 로 돌아와 다시 걸린다.
        if not log['in_call']:
            log['dup'] += 1; return False
        log['in_call'] = False
        r0 = reg('r0'); busy = rd(S_BUSY, 1)[0]
        if log['pending']:
            k, name, r1, r2 = log['pending']; log['pending'] = None
            log['tamper'].append((k, name, r1, r2, r0, busy))
        else:
            log['ok' if r0 == 0 else 'rej'] += 1
            log['busy_after'].append(busy)
        return False
class Bell(gdb.Breakpoint):
    def stop(self):
        log['doorbell'] += 1; return False

Enter('*0x%x' % VENEER, type=gdb.BP_HARDWARE_BREAKPOINT)
for a in RET: Ret('*0x%x' % a, type=gdb.BP_HARDWARE_BREAKPOINT)
Bell('DFSDM1_FLT3_IRQHandler', type=gdb.BP_HARDWARE_BREAKPOINT)

run_for(12.0)
pc1 = gdb.execute('info symbol $pc', to_string=True).strip()
gdb.execute('delete')
cl = [rd(c, 8).hex(' ') for c in CLUSTER]
guard = [rd(BASE + i * STRIDE + 2 * CL, 8).hex(' ') for i in range(2)]   # NS 에서 읽음: MPCBB Secure 블록 (값 무의미)
heap = None
try:
    gdb.execute('add-symbol-file %s' % SEE, to_string=True)
    heap = int(gdb.parse_and_eval("'secure_heap.c'::xFreeBytesRemaining"))
except gdb.error as e:
    heap = 'read failed: %s' % e
with open(d + '/func_check.txt', 'w') as f:
    w = lambda s: (f.write(s + '\n'), print(s))
    w('mediator = %s (veneer 0x%x, return sites %s)' % ('asm' if asm else 'C', VENEER, [hex(a) for a in RET]))
    w('pc after 3 s = %s / after +12 s = %s' % (pc0, pc1))
    w('calls = %d (normal ok %d, normal reject %d), doorbell = %d, duplicate return hits ignored = %d' % (log['enter'], log['ok'], log['rej'], log['doorbell'], log['dup']))
    w('s_busy right after each normal return = %s' % log['busy_after'])
    w('cluster A[0:8] = %s' % cl[0]); w('cluster B[0:8] = %s' % cl[1])
    w('Secure xFreeBytesRemaining = %s' % heap)
    for k, name, r1, r2, r0, busy in log['tamper']:
        w('tamper #%d %-32s dst=0x%08x len=0x%x -> r0=%d s_busy=%d' % (k, name, r1, r2, struct.unpack('<i', struct.pack('<I', r0))[0], busy))
print('done', d)
gdb.execute('detach')
