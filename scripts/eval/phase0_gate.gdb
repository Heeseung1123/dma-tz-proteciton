# Phase 0: 무계측 이미지 flash -> 실행 -> 측정값/런타임 레지스터 회수
# 사용: DIR=results/phase0/default gdb-multiarch -batch -x scripts/eval/phase0_gate.gdb
set pagination off
set confirm off
set print pretty off
python
import os,gdb
d=os.environ['DIR']
ns=[f for f in os.listdir(d) if f.endswith('_NonSecure.elf')][0]; s=[f for f in os.listdir(d) if f.endswith('_Secure.elf')][0]
gdb.execute('file %s/%s'%(d,ns))
gdb.execute('add-symbol-file %s/%s'%(d,s))
gdb.execute('target remote localhost:61234')
gdb.execute('load %s/%s'%(d,s))
gdb.execute('load %s/%s'%(d,ns))
gdb.execute('monitor reset')
end
python
import threading
threading.Timer(4.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
continue
echo --- stats\n
p s_dmaTaskMem[0].globals.v.statMediator
p s_dmaTaskMem[0].globals.v.statRoundtrip
p s_dmaTaskMem[1].globals.v.statMediator
p s_dmaTaskMem[1].globals.v.statRoundtrip
echo --- regs FLASH_ACR(S alias) ICACHE_CR ICACHE_SR RCC_CR RCC_CFGR RCC_PLLCFGR\n
x/1xw 0x50022000
x/1xw 0x50030400
x/1xw 0x50030404
x/1xw 0x50021000
x/1xw 0x50021008
x/1xw 0x5002100C
p/d SystemCoreClock
echo --- DWT_CTRL DWT_CYCCNT DEMCR DAUTHSTATUS DHCSR\n
x/1xw 0xE0001000
x/1xw 0xE0001004
x/1xw 0xE000EDFC
x/1xw 0xE000EFB8
x/1xw 0xE000EDF0
echo --- SRAM2 test (0x20030000)\n
set {unsigned int}0x20030000 = 0xA5A55A5A
x/1xw 0x20030000
info registers pc
detach
