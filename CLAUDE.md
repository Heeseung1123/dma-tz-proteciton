# 버퍼 단위 DMA 보호 (ARMv8-M TrustZone, STM32L562E-DK)

논문 최종본: docs/paper/paper.txt (그림/표는 docs/paper/paper.pdf). 논문이 스펙이다. 작업 전에 반드시 읽는다.

## 레포 구조
- project/ -> Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone : CubeIDE 프로젝트 (Secure/ 베어메탈, NonSecure/ FreeRTOS)
- 커널: Middlewares/Third_Party/FreeRTOS/Source (FreeRTOS V11.3.1, MPU wrappers v2, 수정본)
- 복사해 온 경로 전체:
  - Drivers/CMSIS/Include
  - Drivers/BSP/STM32L562E-DK
  - Drivers/BSP/STM32L5xx_Nucleo
  - Drivers/STM32L5xx_HAL_Driver/Inc
  - Drivers/STM32L5xx_HAL_Driver/Src
  - Middlewares/Third_Party/FreeRTOS/Source
  - Drivers/CMSIS/Device/ST/STM32L5xx/Include
  - Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone
- docs/SETUP_REPORT.md : 원본 위치, 복사 범위, 경로 변환 내역
- git tag baseline-original : 원본 그대로 / baseline : 작업 시작점 (비교 기준)

## 빌드
./build.sh  (정리 후 빌드: ./build.sh clean && ./build.sh)
상태: 설정 시점 빌드 테스트 통과
- Debug/ 의 makefile, subdir.mk, sources.mk, objects.list 는 CubeIDE 가 생성한 것을 커밋해 둔 것이다.
  새 .c 파일을 추가하면 같은 디렉터리의 subdir.mk 에 기존 항목과 같은 형식으로 C_SRCS/OBJS/C_DEPS 와 규칙을 추가하고,
  objects.list 에도 추가한다. 새 디렉터리면 sources.mk 의 SUBDIRS 와 makefile 의 include 도 추가한다.
- 링커 스크립트(.ld)를 바꾸면 Secure/NonSecure 양쪽 메모리 맵과 Secure_nsclib 영향까지 확인한다.

## 설계 요약 (논문 §3)
1. 메모리 배치: 태스크마다 DMA 버퍼를 하나의 연속 구간(DMA Cluster)으로 모아 태스크 전역 데이터와 인접 배치.
   현재는 프로토타입 제작이므로 자동 배치는 구현하지 않고, 추가적인 메모리 섹션없이 단순 코드 배치를 통한 수동 메모리 배치 유도로 구현할 것. 
   링커 순서: 태스크 전역 데이터 -> Cluster -> 가드 블록(DMA 주소 증가 방향). 
   빌드 시 DMA 버퍼 정보를 테이블로 저장. (빌드타임 자동 배치는 범위 밖)
2. 부팅 시(Secure): Cluster 마다 S-MPU 영역 1개(= TTT 태그), 가드 블록(256B)마다 MPCBB Secure 속성,
   TZIC 인터럽트 발생 시 모든 DMA 전송을 중단하도록 설정.
3. 요청 시점 검증 = Secure DMA 중재자(NSC 진입점):
   - DMA 컨트롤러와 채널은 TZSC 로 Secure (NS 가 직접 설정 불가)
   - (1) 호출자 권한 확인 -> 비특권 TTAT / 특권 TTA 로 NS-MPU 에서 base, base+len-1 둘 다 호출자 권한으로 쓰기 가능한지 확인. 현재 구현상으로는 호출자 권한을 확인하는 단계는 없어도 됨. 그 부분은 제외하고 NS-MPU, S-MPU 확인을 inline assembly로 구현하도록. 최대한 적은 instruction을 사용해야함. 이미 존재하는 코드를 참고하면 됨. 
   - (2) TTT 로 양 끝점의 S-MPU 영역 번호 조회 -> 둘 다 유효하고 같은 영역이면 전 범위가 Cluster 안, 현재 구현상으로는 같은 범위인지는 확인하지 않아도 될 것 같음. 
   - (3) 통과하면 DMA 전송을 NS 트랜잭션으로 설정해 시작 (그래야 가드 블록이 적용됨)
4. 전송 시점 보호: 경계를 넘은 NS DMA 쓰기/읽기가 가드 블록(Secure)에 닿으면 TZIC 인터럽트 -> 핸들러가 DMA 채널 중단.
   가드 블록에는 NS 데이터를 두지 않는다.
5. 한계(논문 명시): 태스크당 가드 256B + S-MPU 1개 추가, S-MPU 8개 한도.
   S-MPU 가 부족한 환경용 대안이자 비교 대상으로  테이블 방식도 유지한다. 테이블 방식은 일단 주석 처리해놓을것 

## 평가 재현 (논문 §4, 표 2)
- Task 별 메모리 격리을 포함한 논문의 범위를 실증하기 위한 UART DMA 를 사용하는 Task 와 다른 Task 들을 가지는 Application 코드 작성. 최대한 직관적이어야함. 
- 8바이트 M2M, 캐시 워밍 후 32회 반복, DWT 사이클 카운터
- 빈 NSC 진입·복귀 66 (상태 전환 약 58) / Flash 테이블(8항목) 검증 223 (최대 525) / S-MPU 태그(TTT x2) 160 (31/32회 동일)
- 공격 시나리오: (a) 요청 인자(주소/길이) 조작 -> 중재자가 거부
  (b) 검증 통과 후 전송 길이를 Cluster 너머로 변조 -> 가드 블록 첫 접근 시 TZIC -> 채널 중단, 가드 너머 메모리 무변조. 읽기/쓰기 양방향.

## 확정된 하드웨어 사실 (보드 실측. 뒤집지 말 것. 틀렸다고 판단되면 먼저 질문)
- MPCBB 는 Secure 블록으로의 NS DMA 쓰기를 bus error 없이 무시한다(TEIF 없음). TZIC SR3 의 SRAM1F 로 통지된다.
- Secure 블록으로의 DMA 읽기는 bus error -> TEIF 로 채널 자동 중단 가능.
- MPCBB 를 너무 일찍 설정하면 NS .bss 초기화 중 GTZC_IRQHandler 가 오발한다.
- Secure 에서 TT/TTT 는 SAU-NS 주소에 대해서도 S-MPU 영역을 반환한다. TTA/TTAT 는 NS-MPU 를 조회한다. 두 MPU 는 독립 뱅크.
- DWT 워치포인트는 DMA 쓰기에 반응하지 않는다.
- 빌드 함정: 매크로 stringify 는 DMA_STR/DMA_STR_ 이중 간접, __COUNTER__ 는 TU 마다 리셋되므로 static,
  링커의 .dma_table 은 KEEP() 필수, `.guard_table :` 콜론 앞 공백, `>FLASH` 는 MEMORY 영역명과 일치.

## 구현하지 말 것 (논문에 없음)
핸들 테이블, sealed capability token, generation counter, 태스크 전환 시 MPCBB 동적 재설정,
빌드타임 자동 배치(LLVM 등), NS-MPU 영역 추가 사용.

## 규칙
0. 이미 존재하는 코드들과 주석된 코드 및 설명을 참고, 활용하여 크게 벗어나지 않는 선에서 구현할 것. 
1. 수정 전에 이해한 내용을 먼저 보고한다. 승인 전 대규모 수정 금지.
2. 논문과 코드가 충돌하면 임의로 고치지 말고 docs/gap-analysis.md 에 기록하고 질문한다.
3. 레지스터 동작은 RM0438 근거 섹션을 밝히고, 근거가 없으면 '미확인'이라고 쓴다.
4. 주장마다 출처를 구분한다: [논문] / [아키텍처 도출] / [실측] / [추정].
5. 논문 절 단위로 커밋한다. 커밋마다 docs/CHANGES.md 에 한 줄 추가(파일, 요지, 논문 절, 기존 코드 재사용/신규, 검증 여부).
6. 매 변경 후 ./build.sh 로 빌드를 확인한다. 보드 실행은 불가하므로, 내가 보드에서 확인할 절차(기대 UART 출력, 레지스터 값, GDB 명령)를 적어준다.
7. 원본 대비 변경은 git diff baseline 으로 확인한다. tag 와 baseline-original 은 건드리지 않는다.
