# 데이터 버전 관리 10단계 — Pages 전환·구버전 삭제·main 선별 병합 실행 준비

- 검증일: 2026-10-09 (Asia/Seoul). 웹 `kimjeon-il/Pando`와 앱 `kimjeon-il/PandoEditor`.
- 구현 위치: 웹 `work/gis` (도구·테스트·CI). 앱 `work/gis`에는 공통 증거 보고서만 기록.
- **실행 범위는 준비·오프라인 테스트·읽기 전용 원격 분석까지.** Pages 배포 설정 변경, 현재 배포, 기존 GIS 원본 삭제, `work/gis → main` 코드 병합을 수행하지 않았다.
- **최종 실데이터 CI:** [GitHub Actions #37866525748](https://github.com/kimjeon-il/Pando/actions/runs/37866525748), **success**, 단위검사 **49/49**. [전체 실행 계획·선별 병합·브랜치별 삭제 시뮬레이션 JSON](https://github.com/kimjeon-il/Pando/actions/runs/37866525748/artifacts/11588098513).

## 1. 세 작업별 준비 완료 범위

| 준비 업무 | 구현 및 검증 | 아직 하지 않은 일 |
| --- | --- | --- |
| GitHub Pages 배포 전환 | 정밀 파일 목록, 배포용 스테이징 도구, Git Blob 해시 기반 파일 검증, 용량 초과 차단, 별도 승인 후 사용하는 Pages Actions 템플릿 | Pages 배포 소스 변경·실서비스 업로드 |
| 구버전 파일 정리 | 정확한 2,124개 경로·Blob SHA·크기 목록, 웹 7개 브랜치별 예상 삭제 경로·기존 HEAD 복원 지점·무결성 대조 | 웹 또는 앱 브랜치의 원본 자산 삭제·브랜치 변경 |
| `main` 제품 코드 반영 | 웹·앱 3-way 공통조상 비교, 데이터 버전 관리에 직접 필요한 파일만 화이트리스트 선별 | 전체 `work/gis` 병합·충돌 자동 덮어쓰기·릴리스 빌드 교체 |

## 2. Pages 전환 절차의 실제 구현

새 도구 `tools/prepare-pages-release-bundle.mjs`:

- 기본 `--mode check`는 Stage 9 파일 목록과 Stage 8 삭제 잠금 목록으로 **1GB 제한·구버전 공개 URL 전체 보존·필수 GDAL/지형/수계/역사 카탈로그 포함 여부**를 검증한다.
- 선택적인 `--mode stage`는 **모든 차단 조건이 사라지고 명시적으로 호출된 경우** 새로운 출력 폴더에 게시 대상 파일을 복사한다. 각 파일을 복사하면서 Git Blob SHA-1(헤더 포함) 및 길이를 실제 바이트로 검증하고 SHA-256 증거를 남긴다. 원본 경로를 지우거나 변경하지 않는다.
- 스테이징 폴더는 기존 경로가 있으면 덮어쓰지 않으며, 원본 작업 트리 안에 생성할 수 없다. 파일 경로 탐색·심볼릭 링크를 거부한다.
- 현재 정밀 게시 구성은 `main` **1,047,586,364 bytes**, `work/gis` **1,070,012,175 bytes**로 공식 게시 1GB 제한 초과. **실제 전체 사이트는 아직 스테이징·업로드·배포하지 않았다.** 테스트에서는 작은 임시 저장소의 바이트 대조 동작을 검증했다.
- 배포 승인 후 사용할 비활성 템플릿은 [`docs/validation/gis-pages-activation.template.yml`](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/gis-pages-activation.template.yml)에 기록했다. **현재 `.github/workflows/`에 두지 않아 활성화되지 않는다.**
- 템플릿은 추후 검증을 통과한 경우에만 `main`의 승인된 SHA와 새 Stage10 증거를 조회하고, 구 URL 파일 전부를 검사해 `actions/upload-pages-artifact` / `actions/deploy-pages`를 실행하도록 구성한다. 실제 적용 시 Pages Settings → Source에서 GitHub Actions를 명시적으로 선택하고 `github-pages` 환경을 보호해야 한다.
- 공식 Pages 게시 사이트 1GB 제한 및 GitHub Actions 배포 절차: https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits , https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site

## 3. 전체 브랜치 구버전 삭제 커밋 사전 계산

새 `tools/plan-gis-release-transactions.mjs`:

- GitHub REST의 Git 트리에서 웹 7개·앱 5개 브랜치의 **현재 HEAD SHA**를 각각 고정해 조사한다.
- 웹 브랜치별로 구형 2,124개 원본의 **정확한 Blob SHA·바이트가 맞는 파일만** 가상 삭제 목록에 넣는다. 일부 브랜치에 없는 파일은 다시 생성하지 않고 `alreadyAbsentFiles`로 기록한다. Blob/크기가 다르면 제거 목록에 넣지 않고 경고한다.
- 각 브랜치의 `expectedHead`와 `rollbackRef`를 원본 HEAD SHA로 고정한다. 실제 반영은 Git ref 변경에 대한 lease 검사를 전제로 한다. **다중 브랜치 변경이 원자적으로 처리되는 것은 아니다.**
- 실데이터 결과: 원본과 다른 해시가 발견된 웹 브랜치 **0개**, 대상 26개 그룹·2,124개 파일·564,189,000 bytes 유지. 이번 단계의 `executed=false`, `destructiveOperations=0`.
- 앱 브랜치에는 웹 구파일 삭제 목록을 적용하지 않는다. 이미 포함된 역사 라이브러리·국가지도 원본, Qt 리소스 및 기존 Windows 릴리스의 독립 사용 계약을 유지한다.

## 4. `main` 제품 코드 선별 병합 목록

- 웹 `work/gis`는 `main`과 **232 commits ahead / 10 behind**(조사 시점), 앱은 **144 ahead / 11 behind**. 두 저장소 모두 무조건 전체 병합하지 않는다.
- 웹은 이 데이터 버전 관리 배포에 필요한 코드·SHA world bundle·Worker/로더·생성기·검사 도구 중 **23개 파일**을 3-way 검토 후보로 분류하고 다른 **183개 변경 경로**는 이번 범위에서 제외했다.
- 앱은 원본/오프라인 데이터 연동·빌드·검증기 등 **17개 파일**을 후보로 분류하며, 3-way 기준 공통 조상 이후 양쪽에서 동시 변경되어 자동 대입하면 안 되는 **6개 파일**을 별도로 발견했다:

  `app/physicaldatastore.cpp`, `app/worlddataset.cpp`, `assets/world/NOTICE.md`, `assets/world/manifest.json`, `tests/country_label_anchor_tests.cpp`, `tools/verify-world-assets.mjs`.

- 앱에서 무관한 **78개 변경 경로**는 이번 선별 반영에서 제외한다. 독일제국·덴마크 국경, 네덜란드 복원, 시아첸 조사 등 다른 GIS 주제 및 중간 산출물은 메인 데이터 버전 관리 릴리스에 자동으로 합치지 않는다.
- 단계별 리뷰·3-way 충돌 처리·Qt 오프라인 테스트를 통과한 뒤에만 필요한 코드만 `main`에 반영할 수 있다.

## 5. 실제 통합 릴리스를 차단하는 현재 사유

1. **SITE_OVER_1GB** — GIS 전환 호환 게시 프로필 70,012,175 bytes 초과. 2,124개 기존 공개 URL과 전체 GIS 가져오기 기능을 유지한 1GB 이하 실동작 구성이 아직 검증되지 않았다.
2. **APP_THREE_WAY_CONFLICTS** — 앱 코드 6개 파일의 수동 충돌 해소·재검증 필요.
3. **UNVERIFIED_URL_BODY_PARITY** — 기존 공개 URL 2,124개 각각의 이전/이후 HTTP 본문 SHA-256 증명 없음. 이전 단계의 그룹별 HEAD 검사는 충분하지 않다.
4. **PAGES_SOURCE_STILL_BRANCH** — GitHub Pages는 여전히 웹 `main` 루트 기반의 branch-source로 게시 중.
5. **QT_REGRESSION_OPEN** — 5단계 추가 네이티브 컨트롤러 CI 검사 4개 실패가 미해결.
6. **APP_RELEASE_ARTIFACTS_UNREVIEWED** — 기존 앱 미리보기 Releases 23개에 대한 모든 바이너리 내부 의존성 감사 미완료.
7. **EXPLICIT_RELEASE_APPROVAL_PENDING** — 파괴적 코드/데이터 변경과 공개 배포 설정 전환은 별도 실행 승인 필요.

## 6. 실행 가능한 후속 릴리스 순서

1. 기능·구 URL·배포량 요구사항 사이의 충돌을 해결한다. 다른 도메인에 자산을 옮기는 것만으로 이전 `github.io` 바이너리 URL이 유지되지는 않는다.
2. 웹·앱 각각 선별된 파일을 검토하고 앱 6개 3-way 충돌 및 기존 4개 Qt 회귀를 먼저 해결한다. 본 작업과 관계없는 `work/gis`의 다른 연구자료는 계속 해당 브랜치에 둔다.
3. 새 `main` 고정 SHA로 자동 검증 전체를 재수행하고, **1GB 이하/구 URL 2,124개/현행 모든 GIS 기능**을 재확인한다. 브랜치별 삭제 잠금 목록도 최신 HEAD로 재생성한다.
4. Pages 전환 템플릿을 별도 승인받아 `main`에 활성화하고 Settings에서 Actions 게시 방식으로 전환한다. `github-pages` 보호 환경과 이전 게시·복원 SHA를 확보한다.
5. 배포 사이트에서 구 URL 전수 본문 SHA-256 및 웹/앱 회귀를 검사한다. 실패 시 삭제는 수행하지 않으며 이전 배포 경로로 복귀한다.
6. 승인한 릴리스에서만 웹 각 브랜치의 정확한 구파일을 `expectedHead`와 일치할 때 삭제 커밋으로 반영한다. 앱 내장 자료는 자동 삭제하지 않는다. 다중 ref의 부분 실패에 대비해 각 브랜치별 취소/되돌림 커밋과 결과 로그를 보관한다.

## 7. 변경하지 않은 것

- GitHub Pages 배포 설정·실제 서비스 정적 파일.
- 기존 2,124개 구버전 GIS 파일과 앱 내장 국가·수계·지형·역사 라이브러리.
- `main`에 배포/제품 구현 병합 또는 파일 정리 적용.
- 기존 브랜치·상설 워크트리 생성·삭제.

**결론: 세 작업의 자동 사전검증, URL 보존형 Pages 스테이징 도구, 브랜치별 삭제 시뮬레이션, `main` 선별 병합 검토 목록 및 비활성 배포 템플릿을 마련했다. 실제 릴리스는 현행 차단 조건이 풀릴 때까지 승인 불가.**
