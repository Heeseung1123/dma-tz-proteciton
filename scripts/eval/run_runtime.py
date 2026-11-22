# Runtime SRAM use on the board: heaps, task stacks, Secure context stacks, MSPs (cmpMemUsage_result.md §5).
#   DIR=results/mem/S_P1 [SECS=60] [CSECS=10] [REP=_2] gdb-multiarch -nx -batch -x scripts/eval/run_runtime.py
# Needs the ST-LINK gdbserver on localhost:61234 (~/st-link.sh). Writes $DIR/runtime.json.
#
# Method (HowmemUsageWork.md §11):
#  1. Load both ELFs, reset, hardware-break at the first NS instruction (Reset_Handler from the NS vector table).
#     Secure boot is finished there, but no task exists yet and the Secure heap has never been allocated from.
#  2. Paint with 0xa5a5a5a5: the whole Secure heap ucHeap (Secure context stacks are carved out of it later and
#     FreeRTOS does not fill them), the Secure MSP region [_end, MSP_S - 64) and the NS MSP region [_end, _estack - 64).
#     NS task stacks need no painting: tasks.c fills them with 0xa5 at creation (tskSET_NEW_STACKS_TO_KNOWN_VALUE).
#  3. Run SECS seconds with no breakpoint, halt.
#  4. Read heap counters, scan every stack from its low end for the first word that is not the pattern
#     (= deepest use), read xTickCount.
#  5. Only then: count DMA completions for CSECS seconds with a hardware breakpoint on the NS completion ISR (ignore
#     count), read both Clusters and dmaDone/dmaStatus, and (S) halt inside Secure code to read the Secure MPU, MPCBB.
import gdb, os, struct, subprocess, json, threading, sys
def log(*a): print('[runtime]', *a); sys.stdout.flush()

D = os.environ['DIR']; SECS = int(os.environ.get('SECS', '60'))
TOOL = os.environ.get('TOOLCHAIN_BIN', '/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.'
                      'gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin')
NS_ELF = [os.path.join(D, f) for f in os.listdir(D) if f.endswith('_NonSecure.elf')][0]
S_ELF = [os.path.join(D, f) for f in os.listdir(D) if f.endswith('_Secure.elf')][0]
INSECURE = os.path.basename(D).startswith('I')
PAT = 0xA5A5A5A5

def nm(elf):
    out = {}
    for l in subprocess.run([os.path.join(TOOL, 'arm-none-eabi-nm'), '-S', elf], capture_output=True, text=True).stdout.splitlines():
        p = l.split()
        if len(p) == 4: out[p[3]] = (int(p[0], 16), int(p[1], 16))
        elif len(p) == 3: out.setdefault(p[2], (int(p[0], 16), 0))
    return out
SS, NN = nm(S_ELF), nm(NS_ELF)
def one(d, prefix):   # static locals get a suffix: Idle_Stack.2
    k = [n for n in d if n == prefix or n.startswith(prefix + '.')]
    return d[k[0]]

gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('file ' + NS_ELF)
gdb.execute('target remote localhost:61234')
gdb.execute('load ' + S_ELF); gdb.execute('load ' + NS_ELF)
gdb.execute('monitor reset')
inf = gdb.selected_inferior()
def rd(a, n=1): return list(struct.unpack('<%dI' % n, bytes(inf.read_memory(a, 4 * n))))
def wr(a, words): inf.write_memory(a, struct.pack('<%dI' % len(words), *words))
def paint(lo, hi):
    lo = (lo + 3) & ~3; hi &= ~3
    for a in range(lo, hi, 1024):
        n = min(1024, hi - a) // 4
        wr(a, [PAT] * n)
    return lo, hi
def scan(lo, hi):
    """Lowest non-pattern word in [lo, hi): bytes used from the top = hi - that address."""
    lo = (lo + 3) & ~3; hi &= ~3
    data = bytes(inf.read_memory(lo, hi - lo))
    for i in range(0, len(data), 4):
        if struct.unpack_from('<I', data, i)[0] != PAT:
            return hi - (lo + i)
    return 0
def reg(name):
    try: return int(gdb.parse_and_eval('$' + name)) & 0xFFFFFFFF
    except gdb.error: return None

# ---- 1. stop at the first NS instruction
ns_reset = NN['Reset_Handler'][0] & ~1   # ELF symbol (a debugger read of the NS vector table returned 0)
log('NS reset handler 0x%x, continue to it' % ns_reset)
gdb.execute('hbreak *0x%x' % ns_reset)
gdb.execute('continue', to_string=True)
gdb.execute('delete')
msp_s = reg('msp_s')
log('stopped at NS reset, pc=0x%x msp_s=%s' % (reg('pc') or 0, hex(msp_s) if msp_s else None))
painted = {}
# ---- 2. paint
h_addr, h_size = SS['ucHeap']
painted['secure_heap'] = paint(h_addr, h_addr + h_size)
s_lo = SS['_end'][0]; s_top = SS['_estack'][0]
s_hi = (msp_s - 64) if msp_s and s_lo < msp_s <= s_top else (s_top - 1024)
painted['msp_s'] = paint(s_lo, s_hi)
n_lo = NN['_end'][0]; n_top = NN['_estack'][0]
painted['msp_ns'] = paint(n_lo, n_top - 64)

log('painted', painted)
# ---- 3. free run, no breakpoints (a breakpoint halt inside an ISR changes interrupt timing and stack depth)
def run_for(secs):
    t = threading.Timer(secs, lambda: gdb.post_event(lambda: gdb.execute('interrupt')))
    t.start(); gdb.execute('continue', to_string=True); t.cancel()
log('free run %d s' % SECS)
run_for(SECS)
log('halted')
isr = 'DMA1_Channel1_IRQHandler' if INSECURE else 'DFSDM1_FLT3_IRQHandler'

# ---- 4. read
R = {'build': os.path.basename(D), 'secs': SECS, 'isr': isr,
     'xTickCount': rd(NN['xTickCount'][0])[0], 'msp_s_at_ns_reset': msp_s,
     'painted': {k: [hex(a), hex(b)] for k, (a, b) in painted.items()}}
# heaps
R['secure_heap'] = {'total': h_size, 'free': rd(SS['xFreeBytesRemaining'][0])[0],
                    'min_ever_free': rd(SS['xMinimumEverFreeBytesRemaining'][0])[0]}
R['ns_heap'] = {'total': NN['ucHeap'][1], 'free': rd(NN['xFreeBytesRemaining'][0])[0],
                'min_ever_free': rd(NN['xMinimumEverFreeBytesRemaining'][0])[0]}
# Secure context stacks
ctx = []
base = SS['xSecureContexts'][0]
for i in range(8):
    cur, lim, start, task = rd(base + 16 * i, 4)
    if lim == 0: continue
    try: name = gdb.parse_and_eval('((TCB_t *)%d)->pcTaskName' % task).string()
    except gdb.error: name = hex(task)
    ctx.append({'task': name, 'limit': hex(lim), 'start': hex(start), 'size': start - lim, 'used': scan(lim, start)})
R['secure_contexts'] = ctx
def msp(lo_hi, top):
    u = scan(*lo_hi)
    # deepest address reached = painted top - u; depth from the stack top (_estack) includes the frames that were
    # live when painting (Secure main -> NonSecure_Init, BLXNS) and the 64 B margin, so it is an upper bound for those
    return {'painted': [hex(lo_hi[0]), hex(lo_hi[1])], 'used_below_painted_top': u,
            'depth_from_estack': (top - (lo_hi[1] - u)) if u else None, 'unpainted_top': top - lo_hi[1]}
R['msp_s'] = msp(painted['msp_s'], s_top)
R['msp_ns'] = msp(painted['msp_ns'], n_top)
# NS task stacks (filled with 0xa5 by tasks.c)
st = {}
for task, sym in (('DMA_Task_A', 'xDmaTaskAStack'), ('DMA_Task_B', 'xDmaTaskBStack'), ('Plain_Task', 'xPlainTaskStack'),
                  ('IDLE', 'Idle_Stack'), ('Tmr Svc', 'Timer_Stack')):
    a, n = one(NN, sym)
    st[task] = {'size': n, 'used': scan(a, a + n)}
R['ns_task_stacks'] = st
# functional state: completions counted AFTER the stacks were read (CSECS seconds, breakpoint with ignore count)
CSECS = int(os.environ.get('CSECS', '10'))
tick0 = rd(NN['xTickCount'][0])[0]
gdb.execute('hbreak %s' % isr); bnum = gdb.breakpoints()[-1].number
gdb.execute('ignore %d 1000000' % bnum, to_string=True)
run_for(CSECS)
R['completions'] = {'secs': CSECS, 'hits': gdb.breakpoints()[-1].hit_count, 'ticks': rd(NN['xTickCount'][0])[0] - tick0}
gdb.execute('delete')
fn = {}
for t in (0, 1):
    a = int(gdb.parse_and_eval('(unsigned)&s_dmaTaskMem[%d].cluster.dst' % t))
    fn['task%s' % 'AB'[t]] = {'dst': hex(a), 'dst8': list(bytes(inf.read_memory(a, 8))),
                              'dmaDone': int(gdb.parse_and_eval('s_dmaTaskMem[%d].globals.v.dmaDone' % t)),
                              'dmaStatus': int(gdb.parse_and_eval('s_dmaTaskMem[%d].globals.v.dmaStatus' % t))}
fn['dma1_ch1_ccr'] = hex(rd(0x40020008)[0])
if not INSECURE:
    # The MPU registers are banked; a debugger access sees the bank of the state the core is halted in. Halt inside
    # Secure code (the mediator's Secure entry, reached every 500 ms) so 0xE000ED98.. shows the Secure MPU.
    gdb.execute('hbreak *0x%x' % (SS['__acle_se_SECURE_dma_m2m_start'][0] & ~1))
    t = threading.Timer(5, lambda: gdb.post_event(lambda: gdb.execute('interrupt')))
    t.start(); gdb.execute('continue', to_string=True); t.cancel(); gdb.execute('delete')
    fn['halted_in_secure_at'] = hex(reg('pc') or 0)
    rnr = rd(0xE000ED98)[0]; smpu = {}
    for r in (4, 5):
        wr(0xE000ED98, [r]); smpu[r] = [hex(x) for x in rd(0xE000ED9C, 2)]
    wr(0xE000ED98, [rnr])
    fn['smpu'] = smpu
    try: fn['mpcbb1_vctr13'] = hex(rd(0x50032D34)[0])
    except gdb.MemoryError: fn['mpcbb1_vctr13'] = 'read failed'
R['functional'] = fn
R['SystemCoreClock'] = int(gdb.parse_and_eval('SystemCoreClock'))
OUTF = os.path.join(D, 'runtime%s.json' % os.environ.get('REP', ''))
json.dump(R, open(OUTF, 'w'), indent=1)
print(json.dumps(R, indent=1))
gdb.execute('detach')
