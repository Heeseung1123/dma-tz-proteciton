set pagination off
set confirm off
file /home/sysop/dma-tz/llvm_dma_cluster/build/secure/secure.elf
add-symbol-file /home/sysop/dma-tz/llvm_dma_cluster/build/ns/ns.elf
target remote localhost:61234
monitor reset
echo \n=== 5. SVC path\n
hbreak *0x0c000ec6
c
p/x $xpsr & 0x1ff
info registers msp psp msp_s psp_s msp_ns psp_ns
bt 6
delete
python
import threading
threading.Timer(3.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
c
p/x *(unsigned*)0xE000EDE4
p/x *(unsigned*)0xE000ED28
p s_taskA.priv.okCount
echo \n=== 4. write guard\n
hbreak *0x0c000f20 if $r1 == 0x20018100
c
p/x $r0
p/x $r1
p/x $r2
p/x *(unsigned*)0x50020008
set *(unsigned*)0x50020010 = 0x08040000
set *(unsigned*)0x5002000C = 0x210
p/x *(unsigned*)0x5002000C
x/8wx 0x20018300
x/4wx 0x200181f8
p s_taskA.priv.okCount
p s_privB.okCount
delete
python
import threading
threading.Timer(3.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
c
echo \n--- after\n
p/x *(unsigned*)0x50032818
p/x *(unsigned*)0x50020008
p/x *(unsigned*)0x5002000C
p/x *(unsigned*)0x50020000
x/8wx 0x20018300
x/4wx 0x200181f8
x/4wx 0x30018200
p s_busy
p/x s_taskA.priv.v.dmaStatus
x/4wx 0x08040200
p s_taskA.priv.okCount
p s_privB.okCount
p/x s_taskA.priv.v
p/x *(unsigned*)0xE000EDE4
p/x *(unsigned*)0xE000ED28
python
import threading
threading.Timer(3.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
c
echo \n--- +3s\n
p s_taskA.priv.okCount
p s_privB.okCount
detach
