# 중재자 1회 호출의 실행 명령 추적 (cmpAsmMediator.md §6). 먼저 ~/st-link.sh 실행.
#   DIR=<BENCH_DECOMP 빌드 결과 디렉터리> gdb-multiarch -batch -x scripts/eval/trace_mediator.py
# Bench_Main -> bench_decomp_main 이 처음 부르는 SECURE_dma_m2m_start (C) 와 SECURE_dma_m2m_start_asm 을
# NS veneer 진입부터 NS 복귀 지점까지 stepi 로 따라가며 PC 와 명령을 DIR/trace_{c,asm}.txt 에 쓴다.
# 함수 밖 PC (예외 처리기 등) 는 표시만 하고 따로 센다.
import gdb, os, subprocess
d = os.environ['DIR']
ns = [f for f in os.listdir(d) if f.endswith('_NonSecure.elf')][0]
se = [f for f in os.listdir(d) if f.endswith('_Secure.elf')][0]
NSE, SEE = '%s/%s' % (d, ns), '%s/%s' % (d, se)

def syms(elf):
    out = subprocess.run(['arm-none-eabi-nm', '-S', elf], capture_output=True, text=True).stdout
    r = {}
    for l in out.splitlines():
        p = l.split()
        if len(p) == 4: r[p[3]] = (int(p[0], 16), int(p[1], 16))
        elif len(p) == 3: r.setdefault(p[2], (int(p[0], 16), 0))
    return r
SN, SS = syms(NSE), syms(SEE)

gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('file ' + NSE)
gdb.execute('add-symbol-file %s' % SEE, to_string=True)
gdb.execute('target remote localhost:61234')
gdb.execute('load ' + SEE); gdb.execute('load ' + NSE)
gdb.execute('monitor reset')
def reg(r): return int(gdb.parse_and_eval('$' + r)) & 0xffffffff

def trace(tag, veneer):
    bp = gdb.Breakpoint('*0x%x' % SN[veneer][0], type=gdb.BP_HARDWARE_BREAKPOINT, temporary=True)
    gdb.execute('continue', to_string=True)
    ret = reg('lr') & ~1
    lines, n_out = [], 0
    for _ in range(2000):
        pc = reg('pc')
        if pc == ret: break
        insn = gdb.execute('x/i $pc', to_string=True).strip()
        sym = gdb.execute('info symbol 0x%x' % pc, to_string=True).strip()
        lines.append('0x%08x  %-60s  %s' % (pc, sym[:58], insn.split(':', 1)[-1].strip()))
        gdb.execute('stepi', to_string=True)
    with open('%s/trace_%s.txt' % (d, tag), 'w') as f:
        f.write('# %s: %d instructions from NS veneer to NS return (0x%x)\n' % (tag, len(lines), ret))
        f.write('\n'.join(lines) + '\n')
    print(tag, len(lines))

gdb.execute('monitor reset')
trace('c', '__SECURE_dma_m2m_start_veneer')
trace('asm', '__SECURE_dma_m2m_start_asm_veneer')
print('done', d)
gdb.execute('detach')
