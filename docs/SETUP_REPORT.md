# 설정 리포트

생성: 2026-10-31 19:29

## 원본 위치 (읽기 전용으로 사용)
- 프로젝트: /home/sysop/FreeRTOS_TZ -> /home/sysop/STM32CubeL5/Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone
- 커널: /home/sysop/FreeRTOS_kernel -> /home/sysop/STM32CubeL5/Middlewares/Third_Party/FreeRTOS/Source
- ROOT: /home/sysop/STM32CubeL5

## 복사한 경로 (ROOT 기준)
- Drivers/CMSIS/Include
- Drivers/BSP/STM32L562E-DK
- Drivers/BSP/STM32L5xx_Nucleo
- Drivers/STM32L5xx_HAL_Driver/Inc
- Drivers/STM32L5xx_HAL_Driver/Src
- Middlewares/Third_Party/FreeRTOS/Source
- Drivers/CMSIS/Device/ST/STM32L5xx/Include
- Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone

## 원본 git 정보 (.git 은 복사하지 않음)
- /home/sysop/STM32CubeL5 @ 43121a2467a9fef3d6c8fbb839cf7458e43ecf8f https://github.com/STMicroelectronics/STM32CubeL5.git (uncommitted 변경 있었음)
- /home/sysop/STM32CubeL5/Middlewares/Third_Party/FreeRTOS @ 11b40ff300714fbbeb3d00754bfdacae5e8486fb git@github.com:Heeseung1123/FreeRTOS_DMATZ.git (uncommitted 변경 있었음)

## 경로 정규화
- 변경 270곳

## 빌드 테스트
- toolchain: /opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin (ST GCC: True)
- 순서: Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/STM32CubeIDE/Secure/Debug, Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone/STM32CubeIDE/NonSecure/Debug
- 결과: True

## 전체 로그
```
== 0. 입력 확인
  프로젝트: /home/sysop/FreeRTOS_TZ -> /home/sysop/STM32CubeL5/Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone
  커널    : /home/sysop/FreeRTOS_kernel -> /home/sysop/STM32CubeL5/Middlewares/Third_Party/FreeRTOS/Source
  기존 /home/sysop/dma-tz -> /home/sysop/dma-tz.old-20261031-192945 로 옮김 (원본 아님, 이전 시도 결과)

== 1. 원본 프로젝트가 참조하는 경로 분석
  eclipse project: STM32CubeIDE  (FreeRTOS_SecureIOToggle_TrustZone)
  eclipse project: STM32CubeIDE/NonSecure  (FreeRTOS_SecureIOToggle_TrustZone_NonSecure)
  eclipse project: STM32CubeIDE/Secure  (FreeRTOS_SecureIOToggle_TrustZone_Secure)
  공통 기준 디렉터리(ROOT): /home/sysop/STM32CubeL5
  복사 대상: Drivers/CMSIS/Include
  복사 대상: Drivers/BSP/STM32L562E-DK
  복사 대상: Drivers/BSP/STM32L5xx_Nucleo
  복사 대상: Drivers/STM32L5xx_HAL_Driver/Inc
  복사 대상: Drivers/STM32L5xx_HAL_Driver/Src
  복사 대상: Middlewares/Third_Party/FreeRTOS/Source
  복사 대상: Drivers/CMSIS/Device/ST/STM32L5xx/Include
  복사 대상: Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone

== 2. 원본 스냅샷 (실행 전)
  [ok] 1523 개 파일 기록
  원본 git: /home/sysop/STM32CubeL5 @ 43121a2467a9 https://github.com/STMicroelectronics/STM32CubeL5.git (uncommitted 변경 있음)
  원본 git: /home/sysop/STM32CubeL5/Middlewares/Third_Party/FreeRTOS @ 11b40ff30071 git@github.com:Heeseung1123/FreeRTOS_DMATZ.git (uncommitted 변경 있음)

== 3. 복사 (.git 제외)
  [ok] 복사 완료 (131M). 바로가기: project -> Projects/STM32L562E-DK/Applications/FreeRTOS/FreeRTOS_SecureIOToggle_TrustZone

== 4. git init + 원본 그대로 커밋
  [ok] tag baseline-original

== 5. 경로 정규화 (복사본만 수정)
  [ok] 절대경로 270 곳을 복사본 기준 상대경로로 변경
  [ok] 복사본의 프로젝트 설정은 원본을 참조하지 않음

== 6. 빌드 테스트 (복사본에서만)
  toolchain: /opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin (ST GCC)
  [ok] Secure/Debug 빌드 성공 -> FreeRTOS_SecureIOToggle_TrustZone_Secure.elf
  [!] Secure/Debug 에 이번 빌드가 만들지 않은 오래된 파일이 있음: NS.elf (원본 Debug/ 에서 복사된 것, 무시해도 됨)
  [!] Secure/Debug 에 이번 빌드가 만들지 않은 오래된 파일이 있음: S.elf (원본 Debug/ 에서 복사된 것, 무시해도 됨)
  [ok] NonSecure/Debug 빌드 성공 -> FreeRTOS_SecureIOToggle_TrustZone_NonSecure.elf
  [!] NonSecure/Debug 에 이번 빌드가 만들지 않은 오래된 파일이 있음: NS.elf (원본 Debug/ 에서 복사된 것, 무시해도 됨)
  [!] NonSecure/Debug 에 이번 빌드가 만들지 않은 오래된 파일이 있음: S.elf (원본 Debug/ 에서 복사된 것, 무시해도 됨)

== 7. 문서와 설정 파일 추가
  [ok] 논문 복사 + 텍스트 추출
```
