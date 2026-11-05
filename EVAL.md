# 역할
너는 STM32L562E-DK(Cortex-M33, TrustZone) 펌웨어의 성능·보안 평가를 수행하는 엔지니어다.
대상 프로젝트: ~/dma-tz/project (Secure: 베어메탈, NonSecure: FreeRTOS V11.3.1 MPU wrappers v2)
참고 문서: docs/gap-analysis.md, docs/impl-notes-2026-10-31.md

# 배경
- 제안 기법: Secure DMA mediator(요청 시점 검증) + MPCBB 가드 블록(전송 시점 경계 초과 탐지).
  - verify_inst (Secure/Src/secure_nsc.c):
    TTAT×2 → NS-MPU 동일 영역 + RW 확인
    TTT×2 → S-MPU 동일 영역(Cluster 태그) + NSRW 확인
- 비교 대상: D-Box(NDSS'22). Cortex-M3 32MHz, DWT CYCCNT, 10회 평균, min/avg/max.
  - micro: context switch 287→292 cycles, task creation, DMA validation 569/657/792
  - macro: USART/I2C/SPI/ADC TX/RX 1~100B, Pool/IT/insecure DMA 대비 사이클 오버헤드 %
  - 그 외: CPU 사용률, 전력, 메모리
- 현재 gdb 확인 결과:
  - verify_inst(0x2001b700, 8):
    TTAT 결과 0x08BF0205 (NS-MPU region 5, RW=1)
    TTT 결과 0x08BF0204 (S-MPU region 4, NSRW=1) → 통과
  - statMediator = 828 (min=max=avg, 32회), statRoundtrip = 841
  - 논문 값: 빈 NSC 66, 검증 포함 160 (TTT×2 구성)
- 디버그 서버: ~/st-link.sh 스크립트를 통해서 gdb 서버를 열 수 있음.
    디버그 연결: gdb-multiarch, target remote localhost:61234, 
    
  심볼: NonSecure ELF + add-symbol-file Secure ELF 두 파일을 project/{Secure,NonSecure}_Debug/ directory아래에서 찾을 것 

# 작업 규칙
1. 측정값을 추정하거나 지어내지 말 것.
   모든 수치는 실제 실행 로그(raw CSV)에 근거하고, 측정 못 한 항목은 "미측정"으로 표기.
2. 보안 로직(verify_inst, MPCBB/S-MPU/SAU 설정, TZIC 핸들러)은 수정 전 반드시 변경안과 이유를 제시하고 내 승인을 받을 것.
   계측 코드 추가는 승인 없이 가능하되, 매크로(BENCH_*)로 on/off 가능하게 할 것.
3. 각 Phase 시작 전 클린 리빌드 후, ELF·map 파일과 git hash를 results/<phase>/에 보관.
4. gdb 조작은 batch 스크립트(gdb-multiarch -batch -x *.gdb)로 자동화하고 스크립트도 커밋.
5. 하드웨어 배선(SPI, USART 등) 은 현재로서는 생략하고, M2M으로 대체하여 평가를 수행할 것 . 
6. 코드 수정이 필요하다면, 원본을 .c_bak 형태로 백업한 뒤 diff 와 함께 저장해둘 것 
7. 코드 확인 중에 최적화가 가능한 부분이 존재한다면 보고서에 명시해놓고, 그 원인과 예상되는 결과를 적을것 

# Phase 0: 환경 고정
- git status/hash 기록. Secure → NonSecure 순서로 클린 빌드하여 소스-바이너리 불일치 해소.
- 컴파일 옵션 기록: -O 레벨, -mcpu, -mfloat-abi, -mcmse, ICACHE 설정, flash wait state, SYSCLK.

# Phase 1: 측정 인프라 검증
- statMediator/statRoundtrip의 시작·끝 지점을 코드에서 찾아 정확한 측정 경계를 문서화.
  경계 안에 syscall(SVC), mutex, 큐 연산이 포함되는지 명시.
- CYCCNT 판독 주체 확인: NS 비특권 태스크가 직접 읽는지, syscall 경유인지.
- Secure 상태에서 CYCCNT 카운트 여부 확인:
  DWT_CTRL.CYCDISS, DAUTHSTATUS를 디버거 연결/비연결 양쪽에서 확인.
  Secure 구간 길이를 아는 NOP 루프로 교차 검증.
- 계측 자체 비용: 연속 CYCCNT 판독 2회 차이(N=1000) → 보정값.
- 캐시 조건: warm(현재), cold(ICACHE invalidate 직후 첫 호출) 둘 다 측정.

# Phase 2: 828 사이클 분해
- 타임스탬프 지점:
  T0 NS 호출 직전
  T1 Secure 진입 직후
  T2 verify_inst 반환 후
  T3 DMA 채널 레지스터 설정 후
  T4 EN 직후
  T5 NS 복귀 직후
  T6 전송 완료 감지
  Secure 측 값은 Secure 전역 배열에 저장 후 gdb로 회수.
- 각 구간 N=1000, min/avg/max/stdev 표로 보고.
- arm-none-eabi-objdump -d로 NSC veneer(SG), 진입 함수 prologue/epilogue, 복귀 시 레지스터·FP 클리어 코드를 확인하고, 명령 수와 측정값을 대조.
- 가설별 판정: (a) syscall/mutex 포함 (b) DMA 레지스터 접근 대기 (c) FP 클리어 (d) TTAT 추가분 (e) 전송과 복귀의 중첩.

# Phase 3: 기능·보안 검증 (negative test)
아래 각 케이스에 대해 TTAT/TTT 결과 워드, verify_inst 반환값, 기대값 일치 여부를 표로 기록.
1. Cluster 내부 정상 버퍼
2. Cluster 경계를 1바이트 넘는 끝점
3. 끝점이 가드 블록 내부
4. base가 태스크 전역 데이터(Cluster 직전 base-1 포함)
5. 다른 태스크의 Cluster
6. NS 커널 데이터
7. Secure 주소
8. len=0
9. base+len 주소 랩어라운드
10. 특권 NS 태스크에서 호출
11. S-MPU Cluster 영역이 가드 블록을 포함하지 않는지 경계 주소로 확인
추가 보고(수정은 승인 후):
- M2M에서 src_buf 미검증 상태: 커널 데이터를 src로 지정한 복사가 성공하는지
- s_busy 경쟁 조건 재현 가능성

# Phase 4: D-Box 동형 micro benchmark
측정 조건: N=10(D-Box와 동일) 및 N=1000, min/avg/max. -O0와 프로젝트 기본 최적화 둘 다.
- Context switch:
  baseline = 동일 빌드에서 secure context 미할당 태스크 간 전환
  제안 = secure context 할당 태스크 간 전환
  → 증가율 %
- 초기화 비용: 부팅 시 S-MPU/MPCBB/TZIC 설정 사이클. 태스크 생성 비용 변화.
- DMA 검증: verify_inst 단독, mediator 전체(Phase 2 경계 기준) 각각 보고.

# Phase 5: D-Box 동형 macro benchmark (하드웨어 가용 여부 확인 후)
- 대상: USART TX/RX(ST-LINK VCP), SPI/I2C(슬레이브 보드 있을 때), ADC, M2M.
- 전송 길이: 1~100B (M2M은 1~1024B 추가).
- 방식: Pool, IT, insecure DMA(NS 직접 설정), 제안 기법.
- 길이별 10회. 오버헤드 % = (제안 - baseline) / baseline. D-Box Fig.7·Table X와 같은 형식으로 표와 그래프 작성.
- CPU 사용률: Tracealyzer 대신 idle hook 카운터 방식. 방법을 문서화할 것.

# Phase 6: 메모리 오버헤드
- baseline 대비 flash/RAM 차이를 map 파일로 비교.
- 구성 요소별 분해: mediator, 가드 블록 256B × 태스크 수, 정렬 패딩, 테이블.
- S-MPU 영역 사용 수.

# Phase 7: 전송 시점 보호 정량화
- 쓰기·읽기 방향 각각 len을 변조해 경계를 초과시킨다.
- 측정 항목:
  DMA 시작 → TZIC ISR 진입 사이클
  중단 시점 CNDTR 잔여값
  가드 블록 이후 메모리 무결성
  Cluster와 가드 사이 패딩 구간의 미탐지 범위

# Phase 8: 보고서
- results/report.md:
  각 Phase 결과표
  D-Box 비교표(코어/클럭/최적화/측정 경계/검증 범위 차이를 명시하고, 절대 사이클 직접 비교 불가 항목 표기)
  가설 판정 결과
- 사이클 절감 후보 목록: 구간별 예상 절감량과 근거 포함. 코드 수정은 승인 후 진행.
- raw CSV, gdb 스크립트, 빌드 산출물 경로 목록.


