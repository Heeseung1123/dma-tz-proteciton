# 매크로를 모두 끈 기본 빌드가 기존 값(statMediator 828, statRoundtrip 841)을 내는지 확인.
# DIR=<기본 빌드 디렉터리> gdb-multiarch -batch -x scripts/eval/check_default.gdb
set pagination off
set confirm off
python
import os,threading
d=os.environ['DIR']
ns=[f for f in os.listdir(d) if f.endswith('_NonSecure.elf')][0]; s=[f for f in os.listdir(d) if f.endswith('_Secure.elf')][0]
gdb.execute('file %s/%s'%(d,ns))
gdb.execute('target remote localhost:61234')
gdb.execute('load %s/%s'%(d,s)); gdb.execute('load %s/%s'%(d,ns))
gdb.execute('monitor reset')
threading.Timer(4.0, lambda: gdb.post_event(lambda: gdb.execute('interrupt'))).start()
end
continue
p s_dmaTaskMem[0].globals.v.statMediator
p s_dmaTaskMem[0].globals.v.statRoundtrip
detach
