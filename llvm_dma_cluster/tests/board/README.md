# 보드 시험 스크립트 (2026-11-28 실행본)
`~/st-link.sh` (ST-LINK_gdbserver :61234) 를 띄운 뒤 `gdb-multiarch -nx -batch -x <파일>`.
`.gdb` 는 build/ 절대 경로가 박혀 있다 (실행 당시 그대로). 결과: VERIFY_LOG.md "보드 확인".
- s1_3_boot_dma_attack.gdb : 1–3단계 (부팅, 60 s 정상 DMA, 공격 결과)
- s4_5_svc_write_guard.gdb : 5단계 SVC 경로, 4단계 쓰기 guard (CPAR1 = NS flash, CNDTR1 = 528)
- s4b_read_guard.gdb       : 4단계 읽기 guard (CPAR1 = guard)
- s4c_shared_guard.gdb     : 4c단계 공유 버퍼 쓰기 guard (Task A → s_dmaShared, CNDTR1 = 336 → guard B)
- s6_stale.gdb             : 6단계 stale 짝 → −7
- cyc_roundtrip.py         : 요청 1 회 (svc → 다음 명령) CYCCNT. env SELF NSELF SITES
- cyc_icache.py            : 같은 측정 + ICACHE hit/miss, on/off. env SELF NSELF SITE
