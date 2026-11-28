set pagination off
set confirm off
file /home/sysop/dma-tz/llvm_dma_cluster/build/secure/secure.elf
add-symbol-file /home/sysop/dma-tz/llvm_dma_cluster/build/ns/ns.elf
target remote localhost:61234
monitor reset
load /home/sysop/dma-tz/llvm_dma_cluster/build/secure/secure.elf
load /home/sysop/dma-tz/llvm_dma_cluster/build/ns/ns.elf
monitor reset
echo \n=== 1. boot\n
b dma_boot_config
c
finish
p g_dma_boot_status
set $i = 4
while $i < 6
  set *(unsigned*)0xE000ED98 = $i
  printf "rnr %d: RBAR=%08x RLAR=%08x\n", $i, *(unsigned*)0xE000ED9C, *(unsigned*)0xE000EDA0
  set $i = $i + 1
end
p/x *(unsigned*)0x50032D30
delete
python
import threading
threading.Timer(60.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
c
echo \n=== 2. normal DMA (60 s)\n
p g_dma_boot_status
p s_taskA.priv.okCount
p s_privB.okCount
x/8bx &s_taskA.dst
x/8bx &s_dmaDstB
x/8bx &s_dmaShared
x/8bx ((char*)&s_dmaShared)+32
p/x s_taskA.priv.v.dmaStatus
echo \n=== 3. attack\n
p s_taskA.priv.attack
p/x *(unsigned*)0xE000EDE4
p/x *(unsigned*)0xE000ED28
p/x *(unsigned*)0x50032818
p/x $pc
bt 3
echo \n--- TZIC log (guard 안 Secure 객체)\n
p/x g_tzic_sum
p/x g_tzic_log0
p/x g_tzic_log1
detach
