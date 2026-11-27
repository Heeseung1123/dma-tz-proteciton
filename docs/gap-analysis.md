# 논문 vs 코드 Gap 분석

poc-inventory.md의 상세 근거를 바탕으로, "논문에 있는데 코드에 없는 것" / "코드에 있는데 논문과 다른 것"을 정리한다.
CLAUDE.md 규칙 2에 따라 **코드를 임의로 고치지 않고** 여기 기록만 한다.

## A. 논문/CLAUDE.md에는 있는데 코드에 없는 것

1. **§3.1 메모리 배치 자동화 유도(수동)가 실제로 적용된 태스크가 0개.**
   `DMA_ALLOCATE_BUFFER` 매크로와 `.guard_table` 링커 섹션은 틀만 있고 전부 비활성/미사용. 태스크 전역 데이터 → Cluster → 가드 블록 순서를 강제하는 출력 섹션이 링커 스크립트에 없다.
2. **§3.2 요청 시점 검증의 "(1) 호출자 권한 확인 → NS-MPU(TTAT/TTA)로 base, base+len-1 쓰기 가능 여부 확인" 단계가 코드에 전혀 없다.**
   CLAUDE.md는 "호출자 권한을 구분하는 부분은 제외해도 된다"고 했지만, "NS-MPU, S-MPU 확인을 inline assembly로 구현"이라고 명시했다. 현재 `verify_inst()`는 TTT(S-MPU)만 2회 호출하고, TTA/TTAT(NS-MPU)는 **한 번도 호출하지 않는다.** 즉 요구된 두 단계 중 한 단계가 전부 빠져 있다.
3. **S-MPU 영역 유효성(`mpu_region_valid`, TT 결과 비트16) 검사가 없다.**
   `verify_inst()`는 두 TT 결과의 하위 바이트(영역 번호)가 같은지만 비교하고, 그 영역 번호가 "유효한" 영역인지(`mpu_region_valid` 비트)는 보지 않는다. ARMv8-M 아키텍처상 TT 결과가 invalid일 때 영역 번호 필드 값은 UNKNOWN(구현 정의)이므로, 두 주소 모두 어떤 S-MPU 영역에도 속하지 않아도 우연히 같은 UNKNOWN 값을 반환하면 검증을 통과할 수 있다. 논문 3.2절 "둘 다 **유효**하고 같은 영역인지 확인한다"의 "유효"가 구현되어 있지 않다. [아키텍처 도출: arm_cmse.h의 cmse_address_info_t 비트 레이아웃 + TT 명령의 일반적 동작. 정확한 Armv8-M ARM(DDI0553) 절 번호는 미확인.]
4. **태스크별 Cluster에 대응하는 S-MPU 영역을 부팅 시 순회 설정하는 코드가 없다.** `SMPU_SetupProbeRegion()`은 고정 주소 하나(region 4, `0x20018500`)만 설정하는 사이클 측정용 하드코딩이다.
5. **MPCBB 가드 블록을 태스크 Cluster 경계에 맞춰 부팅 시 개별 설정하는 코드가 없다.** `MX_GTZC_S_Init()`은 ST 원본 데모 그대로 SRAM1/SRAM2를 통째로 NS/S 두 구간으로만 나눈다.
6. **§4 평가 애플리케이션(UART DMA 태스크 + 다른 태스크들) 자체가 없다.** 현재 유일한 NonSecure 태스크는 LED 토글 예제 하나.
7. **공격 시나리오 (a)(인자 조작), (b)(검증 통과 후 길이 변조) 코드가 전혀 없다.**
8. **빌드타임 DMA 버퍼 정보 테이블 저장(§3.1 "빌드 시점에 DMA 버퍼 정보를 저장")이 실질적으로 비어 있다.** `.guard_table` 섹션은 존재하지만 아무도 거기에 기록하지 않아 항상 빈 섹션으로 링크된다.

## B. 코드에 있는데 논문/CLAUDE.md와 다른 것

1. **DMA 컨트롤러/채널의 Secure 고정 방법이 논문 서술과 다르다.**
   논문 3.2절: "DMA 컨트롤러와 채널을 모두 **TZSC**로 Secure에 할당". 현재 코드(`Secure_DMA_LockAllChannels`)는 TZSC(GTZC_TZSC 주변장치 보안/권한 레지스터)가 아니라 **각 DMA 채널 자체의 `CCR.SECM/SSEC/DSEC` 비트**로 고정한다. STM32L5 DMA1/DMA2는 채널별 보안 속성이 TZSC가 아니라 채널 레지스터(CCR)에 있는 것이 RM0438상 맞는 것으로 보이나, 논문 문구의 "TZSC"와 코드의 구현 메커니즘 명칭이 불일치한다. [실측 아님/미확인 — RM0438의 정확한 절 대조 필요]
2. **Flash 테이블 방식(`g_dma_table`/`verify_table`)이 CLAUDE.md 지침("테이블 방식은 일단 주석 처리해놓을 것")과 달리 주석 처리되어 있지 않다.** 단지 호출되지 않을 뿐 컴파일은 되는 살아있는 코드다. 지침 위반이라기보다 "비활성화 방식"이 다른 것이라 사소하지만 기록한다.
3. **CLAUDE.md는 커널을 "수정본"이라 서술하지만, 실제로는 `Middlewares/Third_Party/FreeRTOS/Source`에서 DMA/TZIC/GTZC/MPCBB 관련 키워드가 전혀 검색되지 않고, `baseline-original` 태그 대비 diff도 0이다.** 커널이 정말 수정되었다면 그 수정이 이 트리에 반영되지 않았거나, "수정본"이 의미하는 바(설정 변경만? 아직 계획 단계?)가 다를 수 있다. → 아래 질문 목록 Q1 참고.
4. **`SECURE_dma_m2m_start`/`verify_inst`는 `dst_buf`(쓰기 대상)만 검증하고 `src_buf`는 검증하지 않는다.** 논문은 "DMA 요청(base, len)"이라고만 해서 방향을 특정하지 않지만, CLAUDE.md의 공격 시나리오는 "읽기/쓰기 양방향"을 요구한다(§4 "읽기 방향 모두에서 경계 초과가 동일하게 탐지·차단되었다"). 현재 `src_buf`는 검증 없이 그대로 DMA 소스 주소(`CPAR`)로 쓰인다. M2M 전송이라 소스도 메모리이므로, 소스 쪽이 공격자가 통제하는 다른 버퍼일 경우 소스 측 Cluster 밖 읽기를 막는 로직이 코드상 없다. (단, 전송 시점 보호(가드 블록)는 방향과 무관하게 동작하므로 2차 방어선은 있다.)

## C. 커널 vs upstream V11.3.1

로컬에서 확인 가능한 범위:
- 저장소 자체의 `baseline-original`(import 시점) ↔ 현재 HEAD 사이에는 커널 변경이 전혀 없다(경로 정규화 이후 무변경).
- 사용자 fork(`/home/sysop/STM32CubeL5/Middlewares/Third_Party/FreeRTOS`, 원격 `FreeRTOS_DMATZ.git`)에는 `vendor-baseline` 태그가 있으나 커밋 메시지가 "FreeRTOS-Kernel **10.3.1**"이라고 되어 있어 CLAUDE.md가 말하는 V11.3.1과 버전이 다르다. 그 태그 대비 diff는 862개 파일 규모(버전업 때문에 전체 레이아웃이 다름)라 "DMA 보호를 위한 의도적 수정"을 식별하는 데 쓸 수 없다.
- 이 환경에 순정 FreeRTOS-Kernel V11.3.1 트리가 별도로 없어(네트워크로 받아오지 않는 한) 정확한 파일 단위 upstream diff를 만들지 못했다. **[미확인]**으로 남긴다.

## 구현 순서 제안

의존관계상 아래 순서를 권장한다 (뒷 단계가 앞 단계의 산출물을 전제로 함):

1. **§3.2의 누락된 NS-MPU 검증(TTA) 추가.** `verify_inst`에 TTA(특권 전용, 중재자는 항상 Secure 특권이므로 TTA만 있으면 됨) 호출과 `mpu_region_valid`/`readwrite_ok` 비트 검사를 더한다. 기존 TTT 2회 구조에 TTA 2회를 더하는 정도라 가장 작고 독립적인 변경이며, 표 2 "검증 포함(S-MPU 태그) 160 cycles"의 재측정 기준선도 이 수정 이후로 다시 잡아야 한다.
2. **§3.1 메모리 배치를 실제 태스크 1~2개에 적용.** 주석 처리된 `DMA_ALLOCATE_BUFFER` 매크로를 되살리거나 더 단순한 수동 배치(섹션 attribute 없이 전역 배열 선언 순서로 유도, CLAUDE.md "추가적인 메모리 섹션없이 단순 코드 배치" 지침)로 Cluster 1개를 만든다. 이게 있어야 3,4단계가 "무엇을 보호하는지" 가질 수 있다.
3. **부팅 시 S-MPU 영역/MPCBB 가드 설정을 그 Cluster에 맞춰 1개만 수동으로 박는다.** 지금의 `SMPU_SetupProbeRegion`을 실제 Cluster 주소로 바꾸는 정도의 작업. 8개 제한을 생각해 지금은 1개로 충분.
4. **UART DMA 태스크 + 보조 태스크 1~2개로 구성된 평가용 Application 작성.** 2,3단계의 Cluster/가드가 실제로 쓰이는 첫 소비자.
5. **공격 시나리오 (a)(인자 조작), (b)(전송 중 길이 변조) 테스트 코드 작성 및 실행 절차 문서화.**
6. **§4 사이클 측정 벤치마크(dma_test)를 되살려 새 verify_inst 기준으로 재측정**, 표 2 재현.
7. (선택, 범위 외일 수 있음) 테이블 방식 비교용 재측정 — CLAUDE.md가 비교 대상으로 유지하라고 했으므로 7번은 1~6번과 별개로 수행 가능.

## 결정이 필요한 질문

- **Q1. 커널 "수정본" 서술의 의미.** 이 레포의 FreeRTOS 커널 소스에서 DMA/TZIC/GTZC/MPCBB 관련 변경을 찾지 못했고, 비교용 fork의 `vendor-baseline` 태그도 V10.3.1이라 버전이 안 맞습니다. (a) 커널 수정이 아직 계획 단계라 코드엔 없는 것인지, (b) 사용자 PC의 다른 위치에 이미 만들어 둔 수정본이 있는데 이 레포에 복사가 덜 된 것인지, (c) "수정본"이 FreeRTOSConfig.h 같은 설정 레벨 변경만 가리키는 것인지 확인 부탁드립니다.
-> git tag는 다르지만, 실제 코드는 FreeRTOS v11 으로 변경하였고, 내부 코드도 변경하였지만, FreeRTOS 커널 부분은 문제가 생기면 내가 직접 수정할 것이므로 신경쓰지 않아도 됨. 

- **Q2. NS-MPU 검증(TTA) 추가 범위.** CLAUDE.md는 "호출자 권한 확인 단계는 없어도 됨"이라 했는데, 이는 (a) TTA/TTAT 자체를 아예 호출하지 않아도 된다는 뜻인지, 아니면 (b) 권한 구분(특권/비특권 분기) 없이 **TTA 하나만** 호출해 NS-MPU의 RW 가능 여부는 확인하라는 뜻인지 — 저는 코드와 문구를 같이 볼 때 (b)로 해석했습니다(구현 순서 1번). 맞는지 확인 부탁드립니다.
-> 권한 구분없이 TTA 만으로 확인하도록. 

- **Q3. DMA 보안 고정 메커니즘 표현.** 논문 본문의 "TZSC"라는 단어와 실제 구현(채널 CCR.SECM)이 가리키는 레지스터가 다른데, 이걸 논문 오기로 보고 그냥 둘지, 아니면 코드 주석에 "RM0438 상 DMA 채널 보안은 TZSC가 아니라 채널 CCR임"을 명시해 둘지 의견 부탁드립니다.
-> 이거는 논문 표현상 맞는 표현임 

- **Q4. `src_buf`(소스) 쪽 검증.** 현재 목적지만 버퍼 단위로 검증합니다. M2M 공격 모델에서 소스도 같은 수준으로 검증해야 하는지(추가 TTA/TTT 2회), 아니면 소스는 Secure 쪽이 이미 신뢰하는 고정 버퍼로 한정할 것인지 범위를 정해주세요. 구현 순서에서 1번 작업과 동시에 처리하는 게 효율적입니다.
-> M2M이 아닌 UART DMA 이므로 Src는 신경쓰지 않아도 됨. 

- **Q5. 평가 Application의 구체 형태.** CLAUDE.md는 "UART DMA 를 사용하는 Task 와 다른 Task 들"이라고만 되어 있습니다. UART DMA 태스크의 역할(에코? 고정 패턴 송신?)과 "다른 태스크"의 개수/역할(그냥 idle 비교군인지, 별도 Cluster를 가진 공격 대상 태스크인지)을 정해주시면 구현 순서 4번을 바로 시작할 수 있습니다.
-> DMA 격리 및 Cluster를 확인하기 위한 Cluster 소유 태스크 1개와 그렇지 않은 태스크 1개씩 

- **Q6. 테이블 방식 코드 처리.** CLAUDE.md 지침대로 `g_dma_table`/`verify_table`을 `#if 0`로 감싸 둘지, 아니면 "주석 처리"의 의미가 그냥 "경로에서 빼놓는다(호출 안 함)" 정도로 충분한지 확인 부탁드립니다. 지금 상태(호출만 안 함, 컴파일은 됨)를 유지해도 괜찮다면 별도 작업 불필요합니다.
-> 벤치마크시에는 내가 직접 코드를 작성할 것임. 

DMA cluster 부분은 일단 현재로서는 구현하지 않을 것이고, S-MPU를 이미 구현되어있는 코드처럼 하드코딩시켜서 진행할 것임. 이때 DMA mediator 에 NS-MPU 검증 단계만 추가. 
추가적으로 MPU 격리 및 DMA protection이 정상적으로 작동과 사이클을 확인하기 위한 DMA 사용 태스크 2개(M2M방식), 일반 DMA 미사용 태스크 1개로 NonSecure application 코드 작성. 
먼저, DMA mediator의 사이클 오버헤드 및 Secure ISR 를 거치는 오버헤드를 측정하기 위한 코드도 각 DMA 사용 태스크에 추가. 
메모리 침범 시나리오를 가정하기 위한 코드는 일단 주석으로.
새로운 커밋으로 만들어서 추후에 원본을 복원할 수 있도록 하고, 어떤 부분을 왜 그렇게 수정하였는지를 담고 있는 md 파일을 하나 만들것. 


## D. EVAL 사이클 평가에서 확인한 논문과의 차이 (2026-11-05)

상세는 results/report.md. 논문과 보안 코드는 고치지 않았다.

1. **논문 표 2의 측정 조건과 현재 빌드 조건이 다를 수 있다.** 논문은 "캐시가 채워진 상태"라고 쓰지만 현재 빌드는 ICACHE off다(baseline-original부터 `#if 0`). 빈 NSC는 ICACHE on에서 59로 논문 66에 가깝고 off에서 162다 [실측]. 논문 측정 당시 조건은 미확인이다. 질문: 표 2를 측정한 빌드에서 ICACHE를 켰는가?
2. **논문 160과의 차이 (정정).** 처음에 "재현 안 됨, 약 37 미설명"으로 기록했으나 측정 코드를 단순화한 재측정에서 바뀌었다. P1 110MHz ICACHE warm, DMA IRQ 차단에서 start 전체 164, 빈 NSC 59다 [실측]. TTAT 몫 약 7(이전 측정)을 빼면 약 157이다 [도출]. 즉 ICACHE on이면 논문 값 근처다. warm 값은 코드 배치에 민감해서 이전 측정은 204였다.
3. **표 2 경계와 ISR 중첩.** 논문이 8바이트 M2M을 쓰므로 조건에 따라 복귀 전에 ISR이 들어와 경계에 섞인다 [실측]. 논문 측정에 포함되었는지는 미확인이다.
4. **호출자 권한별 TTA/TTAT.** 논문 §3.2는 특권이면 TTA라고 쓰지만 코드는 TTAT로 고정이다. CLAUDE.md 범위에서 허용된 단순화지만 측정 시 태스크가 특권이므로 기록한다.

## E. DMA 완료 통지: BLXNS 콜백 → NS doorbell IRQ (2026-11-19)

논문 §3 은 요청 시점 검증과 전송 시점 보호(TZIC 중단)만 정하고, 정상 완료를 NS 에 어떻게 알리는지는 정하지 않는다 [논문]. 그래서 충돌은 아니고 논문 밖의 설계 선택이다. 사용자 요청으로 기본 구현을 바꿨다. 상세와 측정 계획은 `cmpISRoverhead.md`.

1. **변경.** 이전(A): Secure `DMA1_Channel1_IRQHandler` 가 NS 함수를 BLXNS 로 호출했다. 이 콜백은 Secure IRQ 문맥이라 FreeRTOS FromISR API 를 쓸 수 없고 Task 는 spin 했다 [도출]. 현재(B): Secure ISR 이 NS 상태 워드에 결과를 쓰고 NS-target IRQ 105 (DFSDM1_FLT3, 미사용)를 pend 한다. NS doorbell ISR 이 소유 Task 를 notify 한다 [코드]. A 는 `DMA_NOTIFY_BLXNS` 빌드에 비교 대상으로 남겼다.
2. **가드 위반 중단 경로는 바꾸지 않았다.** `GTZC_IRQHandler` 는 채널만 끄고 doorbell 을 울리지 않으며 `s_busy` 도 풀지 않는다 [코드]. 공격 (b) 에서 B 의 Task 는 `ulTaskNotifyTake(portMAX_DELAY)` 에 영원히 블록된다. 이전 A 에서도 spin 이 끝나지 않았으므로 새 결함은 아니다. 질문: TZIC 중단 시 status=2 로 doorbell 을 울리고 `s_busy=0` 으로 할까? (`AttackScenario_*` 는 `#if 0`.)
3. **IRQ 자원 1개 추가.** 논문 §5 한계(가드 256B + S-MPU 1개)에 더해 NVIC IRQ 1 개와 NS 상태 워드 4B 를 쓴다 [코드]. 논문 한계 절에는 없다.
4. **상태 워드 포인터는 NSC 로 바뀔 수 있다.** `SECURE_dma_m2m_init` 는 아무 NS 코드나 다시 부를 수 있다. 호출자 권한의 `cmse_check_address_range` 로 호출자가 쓸 수 있는 NS 주소만 받으므로 Secure 메모리를 쓰게 만들 수는 없다 [도출]. 다만 특권 NS 호출자는 (PRIVDEFENA 로) 가드 블록 주소도 넘길 수 있다. 그러면 Secure ISR 이 가드에 4B 를 쓴다. 가드에는 NS 데이터가 없으므로 영향은 없다고 본다 [추정]. 콜백 포인터를 다시 등록하던 A 와 노출 범위가 같다.

## F. ISR 측정용 Insecure DMA 기준선 (2026-11-20)

논문에 없는 평가 전용 설정이다 [논문 밖]. `BENCH_ISR_TS` 빌드에서만 DMA1_CH2 를 SECM=0 으로 풀고 IRQ 30 을 NS 로 보낸다 (Secure `main.c`).
CH2 는 중재자와 가드 검사 없이 NS 가 직접 쓴다. 기본 빌드와 다른 평가 빌드에는 영향이 없다 [코드]. 측정 방법은 `cmpISRoverhead.md` §12.

## G. 메모리 측정용 Insecure 중재자 기준선과 발견 사항 (2026-11-22)

측정 계획 `cmpMemUsage.md`, 결과 `cmpMemUsage_result.md`, 절차 `HowmemUsageWork.md`.

1. **Insecure 중재자 (논문 밖, 평가 전용).** `MEDIATOR_INSECURE` 빌드에서만 존재한다. DMA1_CH1 을 SECM=0, IRQ 29 를 NS 로 두고
   NS 가 직접 설정한다 (패턴 3: 비특권 DMA Task 에 NS-MPU 영역 1개(r7, 0x40020000+32B, Device, RW, XN)로 DMA1 ISR/IFCR + CH1 레지스터를 허용).
   CLAUDE.md 의 "NS-MPU 영역 추가 사용 금지" 는 제안 기법(S) 에 대한 것이고 S 빌드는 NS-MPU 를 추가로 쓰지 않는다 [코드]. 기본 빌드에는 영향이 없다
   (기본 빌드 ELF 의 alloc 섹션 내용과 심볼 표가 변경 전과 동일 [코드]).
2. **[기존 결함, 수정하지 않음] 기본 빌드의 Task B S-MPU/MPCBB 하드코딩이 NS 배치와 맞지 않는다.** Secure `main.c` 는
   `DMA_TASK_B_CLUSTER_BASE = 0x2001c000` (가드 0x2001c100) 이지만, 기본 빌드에서 `s_dmaTaskMem` = 0x2001b600 이므로 실제 B Cluster 는 0x2001ba00 이다
   [코드: nm]. 0x2001c000–0x2001c1ff 는 `Timer_Stack` (0x2001bc50–0x2001c44f) 안이라, (a) Task B 의 `verify_inst` 는 TTT 영역이 무효라 거부될 것이고,
   (b) Timer 태스크 스택의 256B 블록이 MPCBB Secure 가 되어 NS 쓰기가 무시된다 [도출, 보드 미확인]. 9679590 부터 있던 값이다 (`g_dma_tbl` 항목 6 도 같은 값).
   BENCH 빌드는 Task B 를 suspend 하므로 측정에는 드러나지 않았다.
   **수정 (2026-11-22, 사용자 승인):** `DMA_TASK_B_CLUSTER_BASE = DMA_TASK_A_CLUSTER_BASE + 0x300` (= sizeof(dma_task_mem_t)) 로 계산하게 바꿨다
   → 기본 빌드 B Cluster 0x2001ba00, 가드 0x2001bb00. `g_dma_tbl` 의 B 항목(기본 6, BENCH_TBL_BEST 1)도 0x2001ba00. 변경 전/후 Secure 기본 빌드 차이는
   리터럴 4 워드뿐이다 (SMPU 설정의 B limit/base/guard, `g_dma_tbl` 항목 6). 명령·심볼·NS 이미지는 동일 [코드]. 보드 확인 절차: `cmpMemUsage_result.md` §7.5.
   `EVAL_MEM` 빌드는 자기 배치(0x2001b500)에서 A = 0x2001b600, B = 0x2001b900 을 쓰고, `mem_measure.py` 가 Secure 리터럴과 NS 배치를 대조한다.
3. **`EVAL_MEM` 에서 `s_dmaTaskMem` 이 0x100 내려간다.** 코드가 접근하지 않는 `ucPrivateBuffer` (256B) 를 빼서다. 그래서 `EVAL_MEM` 빌드 전용 Cluster 주소와
   `LAYOUT_EXPECT=2001b500` 검사를 두었다 (`build_variant.sh`).
4. **참고: `s_dmaSrcA/B` NS-MPU 영역 base 가 32B 정렬이 아니다** (`aligned(4)`, 예: 0x2001b448). 포트가 base 를 내림, limit 을 32B 로 맞추므로 실제 영역은
   요청보다 넓다 (0x2001b440–0x2001b47f) [코드: port.c 1986–1988]. 기본 빌드도 같다. 이번 측정에서는 바꾸지 않았다.

## H. 스택 없는 중재자 `SECURE_dma_m2m_start_asm` (논문 밖 확장, 2026-11-27, 사용자 선택 안 C)

`MEDIATOR_ASM` (기본 1, `Secure_nsclib/secure_nsc.h`). 기존 C 중재자 `SECURE_dma_m2m_start`/`verify_inst` 는 그대로 두었다.
`MEDIATOR_ASM=0` 이면 Secure·NS 바이너리가 변경 전과 같다 [실측: objcopy -O binary 후 cmp, P1].

1. **구조.** `cmse_nonsecure_entry` + `naked` 이고, 쓰는 레지스터는 r0–r3, r12 뿐이다. push/call/FP 명령이 없다 [실측: objdump].
   GCC 14.3.1 이 `__acle_se_` 심볼과 `.gnu.sgstubs` veneer (`sg; b.w`) 를 만들고, 자동 에필로그는 만들지 않는다 [실측].
   DMA Task A/B 는 `portALLOCATE_SECURE_CONTEXT` 를 하지 않는다. 예외: `BENCH_PHASE` (C bench 진입 함수가 스택을 씀), `DMA_NOTIFY_BLXNS` (C 중재자를 씀).
2. **인터럽트.** 함수 전체를 `cpsid i` (PRIMASK_S) 로 감싼다. Secure Thread 에서 예외가 들어오면 프레임이 PSP_S 에 쌓이는데,
   Secure context 가 없는 Task 는 PSP_S = 0 이라 fault 가 나기 때문이다 [아키텍처 도출].
   PRIMASK_S 는 실행 우선순위를 0 으로 올리므로 우선순위 설정이 가능한 S/NS 예외가 들어오지 못한다 [아키텍처 도출, DDI0553 절 번호 미확인].
   `cpsid` 가 동작하는 것은 CONTROL_S 가 특권(0x02)으로 고정되어 있기 때문이다 [코드: secure_context.c SecureContext_Init].
   **알려진 구멍 (프로토타입 한계로 수용):** SG → `b.w` → `cpsid` 사이의 창에 예외가 오면 fault 가 난다. NMI/HardFault 는 마스킹되지 않는다.
   SG 직후 예외 수용 근거 절: 미확인.
3. **C 경로에서 뺀 것과 근거.**
   - LDREX/STREX: PRIMASK_S 중에는 필요 없다. 단일 코어이고, `s_busy` 를 쓰는 다른 주체는 DMA ISR 뿐인데 마스킹된다 [아키텍처 도출]. 기존 바이트 `s_busy` 를 ldrb/strb 로 쓴다.
   - `len == 0` 검사: SUBS/ADDS 가 dst ≠ 0 이면 C=1 → BCS 에서 거부된다. dst = 0 이면 end = 0xFFFFFFFF 가 되고, NS-MPU 비특권 RW 비트로 거부된다 [아키텍처 도출, 보드 미확인].
   - CCR.EN = 0: `s_busy == 0` 이면 EN == 0 이다. EN 을 켜는 곳은 이 함수뿐이고, ISR 은 EN 을 끈 뒤 s_busy 를 지우며, TEIF 는 하드웨어가 EN 을 끄고, GTZC 중단은 s_busy = 1 을 남긴다 [코드].
   - IFCR = CGIF1: ISR 이 TC/TE 양쪽에서 s_busy = 0 전에 CGIF1 을 지운다 [코드].
4. **순서 변경.** 잠금 → CPAR/CM0AR/CNDTR 쓰기 → 검증 → EN 순서다. 채널을 소유하고 EN = 0 이면 레지스터 값은 아무 효과가 없으므로, 그 뒤 r0/r2 를 재사용해 끝 주소를 다시 계산하지 않는다.
   거부되면 s_busy 를 놓고 EN 은 0 으로 남는다. 레지스터에 남은 NS 값은 다음 요청이 덮어쓴다 [아키텍처 도출].
5. **CMSE 복귀 정리.** r12 (TT 결과 / CCR 값) 만 지운다. r1 = NS dst, r2 = dst+len-1, r3 = DMA1_Channel1_S 주소 또는 1, r0 = 반환값으로 Secure 정보가 없다.
   APSR 는 거부 경로에서만 지운다 (TTT 결과 CMP 의 플래그). 성공 경로의 마지막 플래그 쓰기는 TST #imm (C 상수) 와 MOVS 이고, V 는 NS 입력에서 나온 ADDS 값이다 [아키텍처 도출].
   FP 레지스터는 건드리지 않으므로 지우지 않는다.
6. **판정은 기존과 같다.** src 미검증 (B-4), MRVALID 미검사 (A-3) 를 그대로 두었다.
7. **`BENCH_TS` 훅.** T1 은 `cpsid` 직후, T4 는 EN 직후다. 이 함수를 부르는 NS bench 항목은 아직 없다.

**H 보완 (2026-11-27, 보드 실측):** H-2 의 "알려진 구멍" 은 SG ~ `cpsid` 창에 그치지 않는다.
마스킹 중 pending 된 인터럽트가 `cpsie` 뒤, `bxns` 전에 Secure 에서 받아들여져 PSP_S = 0 에 프레임이 쌓인다 [실측: PSP_S 0xFFFFFFE0, EXC_RETURN 0xFFFFFFFD / 원인 추정].
context 없는 Task 의 문맥 전환 측정에서 1000 회 중 약 절반이 멈췄다. 상세와 해결 후보 A/B/D 는 cmpAsmMediator.md §8 에 있다. 사용자 결정 대기 중이다.

**H 해결 후보 A 구현 (2026-11-27, 사용자 선택).** NS `SVC_Handler` fast path (SVC #110) → Handler mode 에서 `SECURE_dma_m2m_start_svc` 를 부른다.
Secure 예외 프레임이 MSP_S 에 쌓이므로 PSP_S 문제가 없다. 커널 `portasm.c` / `portmacrocommon.h` 를 수정했다: 논문에 없는 확장이고 `configDMA_MEDIATOR_SVC` 로 끌 수 있다.
상세는 cmpAsmMediator.md §9 에 있다.
