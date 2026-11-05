# Phase 0: 환경 고정

- 기준 커밋: 96795901123348690313ed555865c2d4d3e00ce0 (branch impl/paper)
- 툴체인: GNU Tools for STM32 14.3.rel1 (arm-none-eabi-gcc 14.3.1)
- 공통 옵션 [코드, subdir.mk]: -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -g3 --specs=nano.specs -ffunction-sections -fdata-sections
- Secure: -O0 -mcmse / NonSecure: -O1 (cmse 없음)

## 런타임 레지스터 [실측, default/gate.log]
| 항목 | 값 | 해석 |
|---|---|---|
| statMediator (Task A) | 828/828/828 | EVAL 값 재현 |
| statRoundtrip (Task A) | 841/841/841 | EVAL 값 재현 |
| statMediator (Task B) | min=0xFFFFFFFF, max=0 | 전 회차 거부. main.c 주소 불일치와 일치 |
| FLASH_ACR 0x50022000 | 0x00000005 | LATENCY=5 |
| ICACHE_CR 0x50030400 | 0x00000000 | EN=0, ICACHE 꺼짐 |
| SystemCoreClock | 110000000 | 110 MHz |
| RCC_CFGR 0x50021008 | 0x0000000F | SW=SWS=PLL |
| DWT_CTRL | 0x40000001 | CYCCNTENA=1, CYCDISS(bit23)=0 |
| DAUTHSTATUS | 0x000000FF | 디버거 연결 상태 |
| DHCSR | 0x00130003 | C_DEBUGEN=1 (디버거 연결 상태) |
| SRAM2 0x20030000 | 쓰기/읽기 확인 (gdb) | NS 계측 버퍼 후보 |

## 산출물
`results/phase0/default/` 의 manifest.txt, gate.log. ELF/map 은 .gitignore 대상이라 디스크에만 있고 sha256 은 manifest 에 있다.
