# Why is the NS DMA task stack 64 B deeper in S than in I? (cmpMemUsage_result.md §5.3, §5.6)
#   DIR=results/mem/S_P1 [N=30] gdb-multiarch -nx -batch -x scripts/eval/probe_task_frame.py   -> $DIR/frame_probe.json
#
# The CM33 MPU port saves a switched-out task's context in its TCB, ending with
#   { xSecureContext, PSP, PSPLIM, CONTROL, EXC_RETURN }  right below *pxTopOfStack   (portasm.c PendSV_Handler)
# EXC_RETURN bit 4 (FType) = 0 -> the hardware pushed the FP-extended frame (26 words, 104 B) on the task stack,
#                         = 1 -> basic frame (8 words, 32 B).
# CONTROL bit 2 (FPCA) = 1 -> the task has an active FP context, so its next exception will push the extended frame.
# The script halts the board N times at irregular moments and records, for every task that is not running,
# FType, FPCA, the PC in the hardware frame and the frame depth (stack top - PSP). At the end it dumps the
# deepest used words of DMA_Task_A's stack.
import gdb, os, struct, subprocess, json, threading, random

D = os.environ['DIR']; N = int(os.environ.get('N', '30'))
TOOL = os.environ.get('TOOLCHAIN_BIN', '/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.'
                      'gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin')
NS_ELF = [os.path.join(D, f) for f in os.listdir(D) if f.endswith('_NonSecure.elf')][0]
S_ELF = [os.path.join(D, f) for f in os.listdir(D) if f.endswith('_Secure.elf')][0]
def nm(elf):
    out = {}
    for l in subprocess.run([os.path.join(TOOL, 'arm-none-eabi-nm'), '-S', elf], capture_output=True, text=True).stdout.splitlines():
        p = l.split()
        if len(p) == 4: out[p[3]] = (int(p[0], 16), int(p[1], 16))
    return out
NN = nm(NS_ELF)
def sym_at(pc):
    best = None
    for n, (a, s) in NN.items():
        if a <= pc < a + max(s, 2) and (best is None or a > NN[best][0]): best = n
    return best

gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('file ' + NS_ELF)
gdb.execute('target remote localhost:61234')
gdb.execute('load ' + S_ELF); gdb.execute('load ' + NS_ELF)
gdb.execute('monitor reset')
inf = gdb.selected_inferior()
def rd(a, n=1): return list(struct.unpack('<%dI' % n, bytes(inf.read_memory(a, 4 * n))))
def run_for(secs):
    t = threading.Timer(secs, lambda: gdb.post_event(lambda: gdb.execute('interrupt')))
    t.start(); gdb.execute('continue', to_string=True); t.cancel()

STACKS = {'DMA_Task_A': NN['xDmaTaskAStack'], 'DMA_Task_B': NN['xDmaTaskBStack'], 'Plain_Task': NN['xPlainTaskStack']}
run_for(3.0)                                    # let all tasks run a few periods
# Task handles are kernel-object-pool indices with MPU wrappers v2 (hTask = 1, 2, ...), not TCB pointers.
# Find each TCB in the NS heap instead: the TCB whose pxStack is the task's stack array and whose name matches.
def find_tcb(task):
    off = int(gdb.parse_and_eval('(unsigned)&((TCB_t *)0)->pxStack'))
    ha, hs = NN['ucHeap']; ws = rd(ha, hs // 4)
    for i, w in enumerate(ws):
        if w == STACKS[task][0]:
            cand = ha + 4 * i - off
            try:
                if gdb.parse_and_eval('((TCB_t *)%d)->pcTaskName' % cand).string() == task: return cand
            except gdb.error: pass
    return 0
handles = {t: find_tcb(t) for t in STACKS}
print('tcb', {t: hex(h) for t, h in handles.items()})
samples = []
random.seed(1)
for k in range(N):
    run_for(0.15 + random.random() * 0.4)
    cur = int(gdb.parse_and_eval('(unsigned)pxCurrentTCB'))
    for task, h in handles.items():
        if not h or h == cur: continue
        end = rd(h)[0]                           # *pxTopOfStack = end of saved context in ulContext
        sec, psp, psplim, control, excret = rd(end - 20, 5)
        hw = rd(end - 52, 8)                     # r0-r3, r12, lr, pc, xpsr copied from the hardware frame
        top = STACKS[task][0] + STACKS[task][1]
        samples.append({'k': k, 'task': task, 'exc_return': hex(excret), 'ftype': (excret >> 4) & 1,
                        'control': hex(control), 'fpca': (control >> 2) & 1, 'xSecureContext': sec,
                        'psp_depth': top - psp, 'pc': hex(hw[6]), 'pc_sym': sym_at(hw[6] & ~1), 'lr': hex(hw[5])})

# deepest used words of DMA_Task_A's stack (tasks.c fills stacks with 0xa5)
a, n = STACKS['DMA_Task_A']
words = rd(a, n // 4)
first = next(i for i, w in enumerate(words) if w != 0xA5A5A5A5)
dump = [{'addr': hex(a + 4 * i), 'from_top': a + n - (a + 4 * i), 'word': hex(w),
         'sym': sym_at(w & ~1) if 0x08040000 <= w < 0x08080000 else None} for i, w in enumerate(words[first:first + 40], first)]
R = {'build': os.path.basename(D), 'samples': samples, 'stackA_highwater_from_top': a + n - (a + 4 * first), 'stackA_dump': dump}
json.dump(R, open(os.path.join(D, 'frame_probe.json'), 'w'), indent=1)
print('done', D, len(samples))
gdb.execute('detach')
