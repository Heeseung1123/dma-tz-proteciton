set pagination off
set confirm off
file /home/sysop/dma-tz/llvm_dma_cluster/build/secure/secure.elf
add-symbol-file /home/sysop/dma-tz/llvm_dma_cluster/build/ns/ns.elf
target remote localhost:61234
monitor reset
echo \n=== 4c. shared buffer write guard (Task A -> s_dmaShared, guard B)\n
hbreak *0x0c000f20 if $r1 == 0x200184c0
c
p/x $r1
p/x *(unsigned*)0x50020008
set *(unsigned*)0x50020010 = 0x08040000
set *(unsigned*)0x5002000C = 0x150
x/4wx 0x20018600
x/4wx 0x30018500
delete
python
import threading
threading.Timer(3.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
c
echo \n--- after\n
p/x *(unsigned*)0x50032818
p/x *(unsigned*)0x50020008
x/4wx 0x20018600
x/4wx 0x30018500
p s_busy
p/x s_taskA.priv.v
p/x s_privB.v
p s_taskA.priv.okCount
p s_privB.okCount
p s_taskA.priv.attack
echo \n--- TZIC log (guard 안 Secure 객체)\n
p/x g_tzic_sum
p/x g_tzic_log0
p/x g_tzic_log1
detach
