# 변경 이력 (기준: git tag baseline)

| 커밋 | 파일 | 요지 | 논문 절 | 재사용/신규 | 검증 |
|---|---|---|---|---|---|
| fd5ca66 | Secure/Src/secure_nsc.c | verify_inst() 에 TTA 기반 NS-MPU 검증 추가(기존 TTT/S-MPU 체크는 유지) | §3.2 | 기존 함수에 추가 | 빌드만, 보드 미검증 |
| 787f80b | Secure/Src/secure_nsc.c | 리뷰 교정: TTA→TTAT(호출자가 항상 비특권), bit21→bit19(readwrite_ok), NS-MPU 쪽에도 영역번호 동일성(EORS/LSLS/BNE) 비교 추가 | §3.2 | 직전 커밋 수정 | 빌드 + 디스어셈블 확인, 보드 미검증 |
| 60088a2 | secure_nsc.c 주석, docs/impl-notes-2026-10-31.md | 기존 TTT/bit21 "상시 거부 의심" 철회(사용자가 보드에서 정상 동작 확인) — [추정]으로 재서술, 코드 변경 없음 | §3.2 | 문서/주석 수정 | 사용자 보드 실측 근거 |
| 〃 | NonSecure/Src/app_freertos.c | DMA_Task_A/B(M2M, 전용 Cluster) + Plain_Task 로 평가용 Application 전면 교체, 중재자/ISR 사이클 측정 추가 | §4 | 신규 | 빌드만, 보드 미검증 |
| 〃 | Secure/Src/main.c | SMPU_SetupProbeRegion → SMPU_SetupTaskRegions: 태스크 Cluster 2개에 맞춘 S-MPU 영역 2개 + MPCBB 가드 2개 하드코딩 | §3.1(부분)/§3.3 | 기존 함수 대체 | 빌드만, 보드 미검증 |
| 〃 | docs/poc-inventory.md, docs/gap-analysis.md, docs/impl-notes-2026-10-31.md | 논문 vs 코드 조사 및 결정사항/구현 기록 | - | 신규 문서 | - |
| (이 커밋) | NonSecure/Src/app_freertos.c, Secure/Src/main.c | 메모리 배치를 논문대로 수정: 태스크 전역 데이터(32B) -> DMA Cluster(dst 224B) -> 가드(256B)를 구조체 하나로 고정, NS-MPU 영역 1개 = 전역+Cluster, S-MPU 는 Cluster 에만, Secure 하드코딩 주소 갱신(BODY->CLUSTER), 공격 (a-2) 전역 데이터 대상 추가(#if 0) | §3.1 | 기존 struct/하드코딩 패턴 재사용 | 빌드 + nm 주소 확인, 보드 미검증 |
