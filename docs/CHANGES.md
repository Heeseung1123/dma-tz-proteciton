# 변경 이력 (기준: git tag baseline)

| 커밋 | 파일 | 요지 | 논문 절 | 재사용/신규 | 검증 |
|---|---|---|---|---|---|
| fd5ca66 | Secure/Src/secure_nsc.c | verify_inst() 에 TTA 기반 NS-MPU 검증 추가(기존 TTT/S-MPU 체크는 유지) | §3.2 | 기존 함수에 추가 | 빌드만, 보드 미검증 |
| 〃 | NonSecure/Src/app_freertos.c | DMA_Task_A/B(M2M, 전용 Cluster) + Plain_Task 로 평가용 Application 전면 교체, 중재자/ISR 사이클 측정 추가 | §4 | 신규 | 빌드만, 보드 미검증 |
| 〃 | Secure/Src/main.c | SMPU_SetupProbeRegion → SMPU_SetupTaskRegions: 태스크 Cluster 2개에 맞춘 S-MPU 영역 2개 + MPCBB 가드 2개 하드코딩 | §3.1(부분)/§3.3 | 기존 함수 대체 | 빌드만, 보드 미검증 |
| 〃 | docs/poc-inventory.md, docs/gap-analysis.md, docs/impl-notes-2026-10-31.md | 논문 vs 코드 조사 및 결정사항/구현 기록 | - | 신규 문서 | - |
