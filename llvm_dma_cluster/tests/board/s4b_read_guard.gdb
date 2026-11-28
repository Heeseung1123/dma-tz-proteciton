set pagination off
set confirm off
file /home/sysop/dma-tz/llvm_dma_cluster/build/secure/secure.elf
add-symbol-file /home/sysop/dma-tz/llvm_dma_cluster/build/ns/ns.elf
target remote localhost:61234
monitor reset
hbreak *0x0c000f20 if $r1 == 0x20018200
c
p s_privA.okCount
set *(unsigned*)0x20018200 = 0xdeadbeef
set *(unsigned*)0x20018204 = 0xdeadbeef
set *(unsigned*)0x20018208 = 0xdeadbeef
set *(unsigned*)0x2001820c = 0xdeadbeef
set *(unsigned*)0x50020010 = 0x20018300
set *(unsigned*)0x5002000C = 0x10
x/4wx 0x30018300
delete
python
import threading
threading.Timer(1.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
c
printf "end: CCR=%08x CNDTR=%08x ISR=%08x SR3=%08x CPAR=%08x\n", *(unsigned*)0x50020008, *(unsigned*)0x5002000C, *(unsigned*)0x50020000, *(unsigned*)0x50032818, *(unsigned*)0x50020010
x/4wx 0x20018200
p/x s_privA.v
p s_privA.okCount
p s_privA.attack
detach
