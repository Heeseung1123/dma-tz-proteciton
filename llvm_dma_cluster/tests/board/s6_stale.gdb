set pagination off
set confirm off
file /home/sysop/dma-tz/llvm_dma_cluster/build/t_bind/secure/secure.elf
target remote localhost:61234
load /home/sysop/dma-tz/llvm_dma_cluster/build/t_bind/secure/secure.elf
load /home/sysop/dma-tz/llvm_dma_cluster/build/t_stale/ns/ns.elf
monitor reset
hbreak Error_Handler
hbreak NonSecure_Init
c
bt 2
p g_dma_boot_status
x/4wx 0x30018100
detach
