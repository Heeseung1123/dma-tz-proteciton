set pagination off
set confirm off
file /home/sysop/dma-tz/llvm_dma_cluster/build/secure/secure.elf
add-symbol-file /home/sysop/dma-tz/llvm_dma_cluster/build/ns/ns.elf
target remote localhost:61234
monitor reset
hbreak *0x0c000f20 if $r1 == 0x20018100
c
p s_taskA.priv.okCount
set *(unsigned*)0x20018100 = 0xdeadbeef
set *(unsigned*)0x20018104 = 0xdeadbeef
set *(unsigned*)0x20018108 = 0xdeadbeef
set *(unsigned*)0x2001810c = 0xdeadbeef
set *(unsigned*)0x50020010 = 0x20018200
set *(unsigned*)0x5002000C = 0x10
x/4wx 0x30018200
delete
python
import threading
threading.Timer(1.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
c
printf "end: CCR=%08x CNDTR=%08x ISR=%08x SR3=%08x CPAR=%08x\n", *(unsigned*)0x50020008, *(unsigned*)0x5002000C, *(unsigned*)0x50020000, *(unsigned*)0x50032818, *(unsigned*)0x50020010
x/4wx 0x20018100
p s_busy
p/x s_taskA.priv.v
p s_taskA.priv.okCount
p s_taskA.priv.attack
echo \n--- TZIC log (guard 안 Secure 객체)\n
p/x g_tzic_sum
p/x g_tzic_log0
p/x g_tzic_log1
detach
