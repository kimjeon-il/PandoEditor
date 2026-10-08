# 데이터 버전 관리 8단계 — 기존 URL 보존형 배포 전환 및 전체 브랜치 일괄 정리 사전검증

검증일: 2026-10-09 (Asia/Seoul)
대상: 웹 kimjeon-il/Pando / 앱 kimjeon-il/PandoEditor
구현 위치: 웹 work/gis. 앱은 상세 보고서만 work/gis에 기록.
**원본 GIS 자산 삭제·재압축·배포 전환·main 제품 구현 병합: 실시하지 않음.**

## 1. 8단계 목적과 검증 상태

- 전체 웹 브랜치에 서로 다른 버전의 구형 GIS 파일이 남지 않도록, 검증된 하나의 삭제 목록을 브랜치별 고정 커밋과 대조해 일괄 정리할 수 있도록 준비한다.
- 현재 GitHub Pages의 main / 배포를 유지하면서, Actions 기반 게시 산출물로 이전할 때도 기존 공개 URL을 보존하는 것이 필수 조건이다.
- [8단계 통합 CI #37826715828](https://github.com/kimjeon-il/Pando/actions/runs/37826715828) **success**. Node 단위검사 25/25 통과. 실제 사전검증 도구가 웹 7개 / 앱 5개 브랜치와 실제 공개 Pages 설정을 확인함.
- 버전 그룹 26개, 파일 **2,124개**, Git 추적 원본 크기 합계 **564,189,000 bytes**를 정확한 Git Blob SHA, 상대경로, 복원 커밋과 함께 고정한 JSON 삭제 목록 생성.
- 각 웹 브랜치에 존재하는 후보 파일의 서로 다른 Git Blob/크기는 **0건**. 특정 브랜치에 이미 없는 파일은 삭제 작업 대상으로 삼지 않고 별도로 개수를 기록한다.
- 현재 **Pages 배포 가능 = false, 삭제 가능 = false**. 이는 예상된 차단 결과이며 사전검증 자체는 성공했다.

## 2. 현실적인 핵심 제약 — Pages 용량

| 검증 대상 | Git 추적 크기 |
| --- | ---: |
| 현재 웹 main의 assets/data 하위 | 1,108,435,890 bytes |
| 현재 웹 main의 assets 전체 | 1,160,744,440 bytes |
| 6단계 work/gis의 assets/data 하위 | 1,154,121,426 bytes |
| GitHub Pages 공식 게시 사이트 제한 | 1,000,000,000 bytes (=1GB 기준) |
| 웹 main에서 assets/data만으로 초과하는 양 | 108,435,890 bytes |

GitHub 공식 제약: https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits
배포 workflow: https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages

**소스 파일 전체를 단순 복사하여 Pages Actions에 올리는 방식은 용량 사전검사를 통과하지 못한다.** GitHub Pages 배포용 tar가 10GB 미만이라는 조건은 실제 게시 사이트 1GB 제한의 해제를 뜻하지 않는다.

따라서 실제 이관 전에는 기존 공개 경로와 콘텐츠 바이트를 그대로 보존하면서 배포 산출물에서만 빌드 전용 파일·불필요한 복제본을 제외하여 **전체 게시 산출물 1GB 이하**를 달성할 수 있는지 먼저 검증해야 한다. 2,124개 과거 공개 URL의 자산을 단순히 제외하는 것은 금지한다.

기존 주소 https://kimjeon-il.github.io/Pando/assets/data/... 와 완전히 같은 URL을 보존하려면 이 경로로 원본 바이트가 응답해야 한다. 다른 도메인에 파일을 옮기는 것만으로는 동일 URL 유지가 성립하지 않는다. 일반적인 GitHub Pages는 바이너리 URL 전체에 대한 서버측 리버스 프록시를 제공하지 않는다.

## 3. 구현된 사전검증

- 웹 tools/plan-gis-archive-rollout.mjs: 6단계 실제 데이터 의존 감사 + 7단계 웹·앱 브랜치 및 공개 URL 검사 후 단일 **불변 삭제 목록**과 전체 롤아웃 계획을 JSON으로 기록한다.
- 삭제 목록 각 행에는 정확한 assets/data 경로, Git Blob SHA, 추적 바이트, 버전 그룹, 추후 복원해야 할 고정 웹 Git 커밋을 기록한다.
- 브랜치별 기록에는 40자리 고정 HEAD SHA, 정확히 동일한 파일, 이미 없는 파일, 해시가 다른 파일을 분리한다. 해시가 다른 항목이 발견되면 전체 정리 준비 상태를 차단한다.
- Pages 용량 검사: 현재 실제 게시 소스 **main /**의 assets/data만으로 게시 제한을 초과하면 배포 전환 불가로 판정한다.
- 기존 앱 GitHub Releases 23건은 파일이 존재한다는 사실까지 확인했지만 배포 바이너리 전체 해제·검증은 진행하지 않았다. 이것도 삭제 사전승인 차단 사유다.
- 배포 전후 모든 구 URL의 본문 SHA-256 검사, 새 배포 실사용 회귀, 명시적인 파일 제거 승인이 없으면 삭제를 금지한다.
- 전용 CI: .github/workflows/gis-archive-stage8-preflight.yml (contents: read만 부여, **pages: write / id-token: write 미부여**). 실제 Pages 설정 수정·배포·Git ref 이동을 수행할 수 없는 읽기 전용 구성이다.
- 단위 검사: tests/unit/gis-archive-rollout.test.mjs. 고정 Git 트리 손상, 파일 수/크기 불일치, 브랜치별 해시 drift, Pages 1GB 초과, 배포 소스 예외, 승인 누락을 탐지한다.
- [실제 8단계 CI 결과](https://github.com/kimjeon-il/Pando/actions/runs/37826715828)에서 전체 단위검사 25/25 성공.
- [자동 생성한 전체 브랜치 롤아웃 계획과 2,124개 파일 단위 삭제 잠금 목록(JSON)](https://github.com/kimjeon-il/Pando/actions/runs/37826715828/artifacts/11572051217).

## 4. 감지한 실제 차단 사유

1. **PAGES_1GB_LIMIT:** 현재 main 데이터 디렉터리의 전체 크기만으로도 1GB 제한을 초과.
2. **PAST_NATIVE_RELEASES_UNVERIFIED:** 23개 앱 미리보기 릴리스 배포본의 전체 바이트·의존성 미검증.
3. **FULL_URL_SHA_PARITY_MISSING:** 삭제 후보 전체 2,124개에 대해 Actions 이전/이후 동일 URL 응답의 실제 본문 SHA 비교 미실시.
4. **NO_POST_CUTOVER_DEPLOYMENT:** 실제 게시 구조를 아직 전환하지 않았으므로 전환 후 접근/캐시/메시 로딩 회귀 증거 없음.
5. **EXPLICIT_DELETION_APPROVAL_REQUIRED:** 파괴적 파일 삭제를 시행할 별도 승인 및 브랜치별 커밋/복원 절차가 아직 없음.

현재 후보의 브랜치 간 파일 바이트 불일치 건수는 0이지만, 위 5개 제약이 남아 있는 한 실물 파일 제거는 금지한다.

## 5. 전체 브랜치 통합 실행 순서

1. **원본 잠금** — 이번에 생성한 JSON의 2,124개 파일 SHA, 전체 브랜치 HEAD, 현재 Pages 배포를 고정하고 변경분이 생기면 잠금 재생성.
2. **1GB 문제 해결** — 새 정적 배포 산출물의 전체 파일·총 바이트를 계산하고 필수 URL과 바이트를 보존. 빌드 전용 데이터 분리만으로 1GB 이하 구현이 불가능하면 기존 URL 유지 조건의 수정 등 별도 승인 필요.
3. **오프라인 배포 검증** — 기존 2,124개 URL에 대응하는 파일의 경로·SHA 확인, 현행 국가 지도·지형·수계·캐시·지도 편집 흐름 테스트.
4. **Pages 전환** — 승인된 GitHub Actions 산출물 업로드 및 Pages source 전환. 기존 게시본·커밋을 복귀 지점으로 보관하고, 실제 사이트의 기존/현행 URL을 모두 비교.
5. **브랜치별 삭제 커밋 사전작성** — 각 웹 브랜치의 고정 HEAD에서 허용된 파일만 제외한 커밋을 준비; 앱은 v1/v2 내장 파일을 유지하고 출처·검증기만 별도 조정.
6. **한 번의 조정된 정리 릴리스** — 명시적인 사용자 승인 하에 웹 모든 브랜치를 각 HEAD SHA를 조건으로 보호하며 순차 갱신. GitHub는 여러 브랜치 ref를 하나의 원자적 트랜잭션으로 이동시키지 않으므로 부분 실패를 감지하고 필요 시 개별 rollback.
7. **최종 회귀** — 공개 URL 2,124개 바이트, 국가/수계/지형 시작·히스토리 데이터, 앱 오프라인·릴리스, 웹·앱 현재 계약 확인.

## 6. 기존 미해결 문제 및 별도 범위

- 5단계 추가 Qt 검사에서 편집 컨트롤러 30개 중 4개가 실패했던 기록은 계속 유지. GIS 자산 감사 성공을 앱 편집 동등성 검증으로 오인하지 않는다.
- 6단계의 과거 세계지도 매니페스트 불일치 7건은 재현/출처 위험으로 그대로 보존한다. 원본 매니페스트를 현재 콘텐츠에 맞춰 임의로 교정하지 않는다.
- 제품 코드나 GIS 데이터는 8단계에서 변경되지 않았다. 실제 Pages 배포 방식·설정도 변경하지 않았다.
- 다른 브랜치에는 개발 코드를 자동 병합하지 않고 공통 진행 기록 문서만 동일하게 전달한다.

**결론: 8단계 통합 릴리스 계획·정밀 파일 잠금·브랜치별/게시 사이트 자동검증까지 완료. 기존 URL을 보존할 수 있는 1GB 이하 배포 산출물을 준비하기 전에는 실제 전환/삭제를 실행할 수 없다.**
