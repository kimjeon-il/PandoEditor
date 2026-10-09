# 데이터 버전 관리 구조 개선 — 진행 기록

- 최신 검증일: 2026-10-09 (원본 조사일 2026-10-08)
- 범위: 웹 `kimjeon-il/Pando` · 앱 `kimjeon-il/PandoEditor`.
- 주 구현 브랜치: 웹·앱 각각 `work/gis` (별도 실험 브랜치 생성 없음).
- **1단계:** 웹·앱 데이터 인벤토리 및 참조 관계 조사 완료. 원본은 미변경.
- **2단계:** 웹에서 독립 데이터 매니페스트·내용 해시 기반 객체·추가 생성/검증 CLI 구현 및 커밋. 새 데이터 번들에 대한 정적 경로/Blob/크기 검사 완료. 기존 앱 버전 기반 생성기·웹 로더는 전환 전까지 유지하며 원격 CI·실제 렌더링 검증은 별도 필요.
- **3단계:** 웹 `work/gis` 구현·커밋 및 GitHub Actions **대상 통합검증 완료**. 국가 데이터·생성기·자산/버전·구문·ESLint 확인, Node 단위검사 **33/33**, UI 번들 빌드, Chromium 실제 시작·캐시·복구 **3/3** 통과. 전체 Playwright 스위트·앱 Qt·배포는 범위 밖.
- **4단계:** 앱 `work/gis`에서 단일 v2 데이터 매니페스트·원본 출처·승인형 웹 동기화·Qt 리소스 경로 연동 구현 및 실제 GitHub Actions 대상 검증 완료. Node 6/6, 내장 자산 7개, 릴리스 소스 18개, Qt 오프라인 CTest 3/3 모두 통과. **웹 최신 자산을 앱에 실제 적용하지 않았으며 기존 기본 지도 바이트 유지.**
- **5단계:** 앱 `work/gis`에 읽기 전용 웹·앱 역사 국가 라이브러리 의미 비교와 전용 CI 추가·검증 완료. 원본 웹 `ebcfae4d`와 앱 285/285 동일(국가 파일 284/284+색인), 조사 웹 `a47c4c54`와는 **194개 완전 동일·90개 존속 시작일만 변경·도형/기타 의미 불일치 0건**. Node 8/8 및 2개 실데이터 교차검증 통과. 앱 원본 자산은 변경하지 않음.
- **6단계:** 웹 work/gis에서 구형 GIS 자산 3,101개의 Git 트리·매니페스트·앱 출처와 코드 의존성을 감사하는 도구 및 CI 추가. Node 7/7·실데이터 감사 성공, 현재 필요 자산 불일치 0건·앱 고정 원본 7/7 일치. 오래된 GIS 파일 2,124개(564,189,000 bytes)를 보관 검토 대상으로 분류하고 **삭제·재압축 없이 보존**.
- **7단계:** 웹 work/gis에 공개 Pages·기존 앱 Releases·웹/앱 전체 브랜치의 구형 GIS 자산 교차검증기를 추가하고 CI 실데이터 검증 **15/15 및 성공**. GitHub Pages는 main 루트 legacy 배포이며 **보관 후보 26그룹/2,124개/564,189,000 bytes의 각 그룹별 대표 URL이 현재 응답**한다. 앱 미리보기 릴리스 23개와 웹 7·앱 5개 브랜치 조사. **실제 삭제 가능으로 승인된 파일은 0개**, 기존 바이트·배포·제품 main 병합 없이 보존.

- **8단계:** 웹 work/gis에 기존 공개 URL 보존을 전제로 한 **웹 7·앱 5개 브랜치 통합 정리 사전검증기** 구현·커밋. [실제 CI #37826715828](https://github.com/kimjeon-il/Pando/actions/runs/37826715828) **Node 25/25 통과**, 파일별 Git Blob·복원 커밋을 고정한 2,124개/564,189,000 bytes 삭제 잠금 목록 생성, 기존 파일이 있는 웹 브랜치 간 SHA 차이 0건. 다만 Pages가 main /에서 배포 중이며 main assets/data 1,108,435,890 bytes가 공식 1GB 제한 초과. 앱 과거 23개 릴리스·전후 URL 전체 바이트 미검증으로 **배포·삭제 승인 0건**. 원본 데이터와 Pages 설정은 미변경.

- **9단계:** GitHub Pages의 실제 게시 파일 최소보존 구성을 `main`·`work/gis` Git 트리와 앱 고정 출처에 대조해 **2,124개 구 URL 원본 Blob 2/2 동일** 확인. [CI #37828988316](https://github.com/kimjeon-il/Pando/actions/runs/37828988316) **Node 35/35 통과**. 개발/연구용 파일을 제외하고도 `main` 1,047,586,364 bytes(+47,586,364), `work/gis` 1,070,012,175 bytes(+70,012,175)로 **Pages 게시 사이트 1GB 제한 초과**. GIS 가져오기 GDAL을 빼는 비호환안도 검증하고 배포 승인 거부. 배포 설정·기존 파일 삭제·제품 main 병합 미실시.

- **10단계:** 실제 Pages 전환·구형 파일 정리·`main` 제품 코드 반영을 위한 **선별 병합 및 일괄 삭제 시뮬레이션**을 웹 `work/gis`에 구현. [실제 CI #37866525748](https://github.com/kimjeon-il/Pando/actions/runs/37866525748) **49/49 통과**, 웹 7·앱 5개 브랜치 SHA 확인, 웹 23개·앱 17개 파일 선별 병합 검토 목록, 앱 3-way 수동 충돌 6건, 구형 2,124개 파일의 웹 브랜치별 Blob 차이 0건. 이전 배포 URL을 유지하도록 바이너리 해시를 검사하는 오프라인 Pages 스테이징 도구와 비활성 배포 승인 템플릿 준비. **1GB 초과·Qt 회귀·공개 URL 전수 바이트 검증 미완료로 배포/삭제/메인 제품 코드 병합 미시행.**

- **11단계:** **구버전 URL 영구 보존 정책 해제**(2026-10-09) 후, 웹 **7개 브랜치에서 구형 지형 `terrain/v0.12.0/` 335개·374,094,147 bytes 실제 삭제 커밋 적용**. 웹 최신 지형 `v0.12.6`은 전부 335개 보존, 앱 5개 브랜치는 내장 자료 그대로 유지·동일 URL 정책 문서 추가. [Pages 재배포 #37868609497](https://github.com/kimjeon-il/Pando/actions/runs/37868609497) 성공, [현재/구형 URL·12브랜치 검증 #37869177623](https://github.com/kimjeon-il/Pando/actions/runs/37869177623) 5/5 성공(구형 404·현행 200). `main` 전체 추적 파일 **832,753,069 bytes**, 데이터만 **734,341,743 bytes**. 단계9/10 업데이트 CI 각각 35/35·49/49 성공. Pages 방식 전환·main 제품 코드 병합 미실행.

## 기준선과 구현 링크

- 1단계 웹 기준 Git 스냅샷: [`2b79bcbe48c2`](https://github.com/kimjeon-il/Pando/tree/2b79bcbe48c2b725624a576c7746607771875a93)
- 1단계 앱 기준 Git 스냅샷: [`5b3e42a9696a`](https://github.com/kimjeon-il/PandoEditor/tree/5b3e42a9696abb34cadb3f681e47fa973456593c)
- 웹 1단계 보고서: [웹 상세 인벤토리](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase1-inventory.md)
- 앱 1단계 보고서: [앱 상세 인벤토리](https://github.com/kimjeon-il/PandoEditor/blob/work/gis/docs/validation/data-versioning-phase1-inventory.md)
- 2단계 웹 제품 코드 커밋: [`10e6de30dc34`](https://github.com/kimjeon-il/Pando/commit/10e6de30dc3489b5ee5353f9c45359b3f07c99c9)
- 2단계 설계·검증 문서: [웹 2단계 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase2.md)

## 2단계 변경 내용

1. `assets/data/world/build-input.json`: 기존 검증된 국가 데이터 자산의 고정 입력. 현행 출처는 `world-preview-v0.36.0.json`.
2. `assets/data/world/current.json`: 앱 버전 없는 `pandolab-world-bundle` schema 1 및 `defaultClassification` 포함.
3. `assets/data/world/objects/*-sha256-*`: 정밀 패킷·정밀 메시·미리보기 GeoJSON·미리보기 메시·국명 anchors 5개를 SHA-256 식별자로 발행. 원본과 **Git Blob 5/5 동일**.
4. `tools/build-world-bundle.mjs`: 정본 SHA, 5개 자산의 압축 저장/해제 크기·SHA·바이너리 헤더 검사; 동일 데이터 재사용, 손상된 기존 불변 객체 거부.
5. `pnpm build:world-bundle`·`pnpm check:world-bundle`을 추가하고 기존 `build:map-assets`·`check:preview`의 마지막 단계에 연결.
6. `tests/unit/world-bundle-versioning.test.mjs` 추가. 별도 샘플 환경에서 같은 계약의 독립 테스트 3/3 통과. **전체 웹 CI 및 Playwright는 실행 완료로 주장하지 않음**.

## 3단계 구현 현황

- 웹 구현 커밋: [`7c42b8fb28cf`](https://github.com/kimjeon-il/Pando/commit/7c42b8fb28cf3a43acd6a4e81ef9370010485dcc).
- [3단계 상세 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase3.md).
- Worker는 `world/current.json`을 로딩하고 데이터 자산의 SHA-256 경로를 사용함. 세계지도 콘텐츠 캐시와 역사 라이브러리 revision 캐시를 구분함.
- **주의:** 공유 국경선 캐시는 여전히 이전 버전 파일을 호환 참조하며, 신규 불변 이름으로의 완전한 이관은 완료되지 않음.
- GitHub Actions에서 실제 UI 빌드 및 핵심 Chromium 시작·캐시 검사 통과. 다만 전체 브라우저 회귀·이전 모든 캐시 조합·장기 GPU 검사까지 통과한 것은 아님.

## 3단계 실제 CI 검증 근거

- 완료된 GitHub Actions 최종 실행: [World Dataset Stage 3 Gate #37804486287](https://github.com/kimjeon-il/Pando/actions/runs/37804486287), 웹 `work/gis` 커밋 [`dcc296ae80eb`](https://github.com/kimjeon-il/Pando/commit/dcc296ae80eb5d3f1db013962629358804e98846).
- 국경선·미리보기·불변 번들 재생성/검사, 빌드 메타데이터 및 818개 JS 구문 검사, 영향 범위 ESLint: 모두 통과.
- 대상 Node 단위검사: **33개 통과 / 0개 실패**.
- 실제 Chromium: 모바일 DPR, reload 후 불변 국가 데이터 캐시 재사용, 캐시 손상 후 복구 **3개 통과 / 0개 실패**; `pnpm build:ui-bundle`도 통과.
- 초기 [실행 #37802544830](https://github.com/kimjeon-il/Pando/actions/runs/37802544830)의 테스트 파일 `TextDecoder no-undef`는 [`56256c515a04`](https://github.com/kimjeon-il/Pando/commit/56256c515a04e7a4908fef8313d1788b77341a2a)에서 수정 후 통과.
- 이전 지형·수계·연구 자료를 제외하는 CI sparse checkout에서도 두 작업이 모두 통과했다. 원본 Git 데이터나 웹·앱 공용 자산을 삭제한 것은 아님.
- **당시 3단계 검증 경계:** 웹 전체 회귀·GPU 성능 벤치마크와 배포 미실시. 앱 Qt 검증은 이후 4단계에서 **Linux 대상 빌드·오프라인 3/3**에 한해 통과함.
- 상세 근거: [웹 3단계 검증 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase3.md).

## 4단계 앱 데이터 출처·동기화 구조 및 실제 검증

- 앱 구현 대상: `kimjeon-il/PandoEditor`의 `work/gis`. 기존 패킷·메시·국명 앵커·수계·지형 원본 바이트는 **변경 없음**.
- 앱 `assets/world/manifest.json` 스키마 2: 데이터 경로·Git Blob·SHA-256·크기·필수 여부·각 파일의 웹 커밋/원본 경로를 단일 정본으로 통합. 현재 `mixed-pinned`는 웹 `c0bd31d1` 데이터와 이후 국명/수계 교정 `a87f4d2`를 명시한다.
- `app/worlddataset.cpp`의 고정 파일명/Blob 중복을 제거하고, `app/CMakeLists.txt`의 Qt 자산 등록을 단일 매니페스트 기준으로 전환. 앱은 **오프라인 독립 실행**하고 웹 최신 데이터를 자동 채택하지 않는다.
- 동기화 명령 `tools/sync-world-data.mjs --web-ref <40자리 SHA>`: 기본은 읽기 전용 비교. 파일 내용·출처·포맷 검증 후, 사용자가 별도 `--apply --approve-bundle <64자리 manifest SHA-256>`를 명시한 경우에만 변경 데이터를 앱에 반영한다.
- 개선 사항: **2개 콘텐츠 변경 자산만 다운로드**, 3개 동일 바이트 자산 재사용; 비교 후 앱 현행 매니페스트가 달라지면 옛 계획을 거부. 수계/지형 매니페스트 drift 발견 시 승인 적용 중단. 구형 파일 삭제 없음.
- 최종 [앱 4단계 GitHub Actions #37809630232](https://github.com/kimjeon-il/PandoEditor/actions/runs/37809630232) **2개 작업 성공**: 내장 자산 7개 검증, Node 6/6, 소스 릴리스 자산 18개 검증, Linux Qt 6.8.3 빌드 및 CTest 3/3.
- CI에서 읽기 전용 비교한 웹 기준 커밋 `65d34d172ee72867d9e19dbde2667607a2ea7d59`, 번들 `960e2f55964cbaebad10f6e397bf11fb5c4ad2464803ffe640f65b08bdcfb836`: 새로 다운로드 필요한 데이터 `countryPreview`, `countryCanonical`뿐. 실제 앱 적용·재패키징은 **수행하지 않음**.
- 상세 검증: [앱 4단계 보고서](https://github.com/kimjeon-il/PandoEditor/blob/work/gis/docs/validation/data-versioning-phase4.md).
- 검증 제외: Windows/Android 배포, 전체 앱/웹 통합 회귀, 사용자 프로젝트 전체 저장·Undo/Redo. 역사 라이브러리 차이는 이후 5단계에서 별도로 의미 검증.
 
## 5단계 앱·웹 역사 국가 라이브러리 교차검증 및 실제 CI

- 앱 구현 브랜치: kimjeon-il/PandoEditor의 work/gis. 읽기 전용 검증기 tools/compare-territorial-library.mjs, 8개 단위검사 tools/territorial-library-crosscheck.test.mjs, GitHub Actions world-dataset-stage5-gate.yml.
- 원본 추적: 앱 카탈로그는 웹 ebcfae4d27b29cbbea6416a7045a4806930204be의 285개 파일과 **Git Blob 285/285 동일**, 실제 압축 해제·색인·SHA 교차검증도 통과.
- 5단계 조사 웹 고정 커밋 a47c4c54655a802a64679a679fc7fa8a576bee56 대조: 전체 285개 중 **194개 파일 완전 동일**, 국가별 gzip 90개는 lifetime.validFrom만 달라짐. 각 파일의 압축 해제 크기 +8바이트. 색인 1개 차이는 이 시작일 및 파생 크기·SHA 메타데이터에 한정.
- **실제 도형/도형 버전/기타 의미의 드리프트: 0건.** 262개 계보·2개 스냅샷 일치, 손상·누락 0건.
- [최종 5단계 앱 GitHub Actions #37816746202](https://github.com/kimjeon-il/PandoEditor/actions/runs/37816746202): Node 단위검사 **8/8**, 원본 exact 및 조사 웹 lifetime-only 실데이터 **두 검증 모두 통과**, JSON 보고서 artifact 생성.
- [5단계 상세 보고서](https://github.com/kimjeon-il/PandoEditor/blob/work/gis/docs/validation/data-versioning-phase5.md). 카탈로그 실물·앱 코드의 고정 색인 SHA·기본지도 원본 바이트는 미변경.
- **제외 사항:** Qt 전체 UI/GPU 교차 렌더링, 타일 전체 설치, Windows/Android 배포, 사용자 프로젝트 저장·Undo/Redo 전면 회귀. 앱이 새 웹 카탈로그를 채택하려면 추후 별도 승인·호환성 검증이 필요.


## 6단계 GIS 자산 감사 및 검증

- 웹 구현: tools/audit-legacy-gis-assets.mjs, tests/unit/legacy-gis-assets-audit.test.mjs, .github/workflows/legacy-gis-stage6-gate.yml (제품 코드 변경은 web work/gis에만).
- [6단계 GitHub Actions #37820208955](https://github.com/kimjeon-il/Pando/actions/runs/37820208955): Node 7/7, Git 트리의 3,101개 데이터 파일·939개 소스 파일 검사, 현재 필수 자산 누락·크기·Blob 오류 0건, 앱의 고정 출처 7/7 일치.
- 현행 직접 보호 363개 경로에는 SHA 국가 번들, 현재 v0.36.0 생성 입력, 공유 경계, 지형 v0.12.6 타일 334개, 수계 v0.13.1 및 이 버전이 참조하는 v0.13.0, 앱이 고정한 웹 소스가 포함된다.
- **보관 검토(삭제 승인 전) 2,124개 / 564,189,000 bytes**: 수계 v0.12.2~v0.12.6 1,769개, 지형 v0.12.0 335개, 나머지 구형 세계지도 20개. 직접 테스트가 참조한 다른 3개 파일은 별도 유지/검토.
- 과거 매니페스트에만 존재하는 선행 불일치 **7건**: v0.30.0~v0.34.0의 현재 미추적 파일 참조 5건, v0.30.0~v0.31.0의 저장 크기 메타데이터 불일치 2건. 현행 런타임 오류와 구분하며 원본을 임의로 고치지 않았다.
- 동일 Git Blob 중복 그룹 71개, 명목상 중복 복사본 크기 101,453,860 bytes. Git은 같은 Blob을 재사용하므로 실제 저장소 절감량이라고 간주하지 않는다.
- [6단계 상세 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase6.md). 앱 work/gis에도 같은 보고서를 게시했다.
- **5단계 추가 Qt 검사 미해결:** [#37817249950](https://github.com/kimjeon-il/PandoEditor/actions/runs/37817249950)에서 네이티브 역사 카탈로그 테스트 통과, 편집 컨트롤러 30건 중 26건 통과·4건 실패. Node 실데이터 검증 결과와 분리하여 기록한다.
- 현재 6단계는 참조 감사·보관 후보 확정까지만 수행했다. 실물 삭제, 이전 URL 접근 차단, 앱 내장 번들 교체, main 제품 코드 병합, 배포는 수행하지 않았다.

## 7단계 공개 URL·릴리스 의존성 검증 결과

- 웹 작업 코드: tools/audit-legacy-release-consumers.mjs, tests/unit/legacy-release-consumers.test.mjs, .github/workflows/legacy-gis-stage7-gate.yml (웹 work/gis에만 반영).
- [최종 GitHub Actions #37822762630](https://github.com/kimjeon-il/Pando/actions/runs/37822762630) 통과: Node 총 15/15, 웹 7개·앱 5개 브랜치, 앱 Releases 23건 교차조사, 발견된 원본 출처 무결성 불일치 0건.
- GitHub Pages API의 실제 배포 URL은 https://kimjeon-il.github.io/Pando/ 이며, **main 브랜치의 루트에서 legacy 방식**으로 게시된다. main과 대부분의 work/*는 아직 world-preview-v0.36.0.json 기반의 예전 로더를 사용하고 work/gis만 world/current.json 번들 기반이다.
- 6단계 보관 검토 26개 버전 그룹(2,124개, 564,189,000 bytes)은 그룹별로 Git에 존재하는 대표 매니페스트·타일·압축 파일을 선택해 **현재 공개 URL의 HTTP HEAD 접근을 확인**했다. 그룹별 표본 검사이며 2,124개 모든 URL의 바이트 검증은 아니다.
- **전부 retain-public-asset-url:** 지금 파일을 제거하면 기존 Pages 경로의 공개 접근을 깨뜨릴 수 있다. Git에 이전 파일이 남는 것과 현재 외부 URL의 계속된 접근은 다른 문제이므로, 승인할 삭제 후보는 0개다.
- [7단계 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase7.md)와 [실제 브랜치·웹 URL 교차검증 JSON](https://github.com/kimjeon-il/Pando/actions/runs/37822762630/artifacts/11569922024) 기록. 앱 work/gis에도 동일한 보고서를 둔다.
- 다음 정리 과정은 **구 URL의 동일 경로 배포 보존**이 선행되어야 한다. 예: 과거 Git 커밋에서 정본을 가져와 Pages Actions 배포 산출물에 포함하는 방식. 기존 릴리스에 대한 전체 URL·원본 바이트 정합성 시험 및 사용자 승인 전에는 삭제·재압축·Pages 배포 방식 변경을 수행하지 않는다.
## 8단계 전체 브랜치 통합 정리 사전검증

- 웹 구현: tools/plan-gis-archive-rollout.mjs, tests/unit/gis-archive-rollout.test.mjs, .github/workflows/gis-archive-stage8-preflight.yml. 웹 work/gis에만 구현했고 Pages 배포 권한과 파일 삭제 기능을 부여하지 않았다.
- [CI #37826715828](https://github.com/kimjeon-il/Pando/actions/runs/37826715828) **25/25 단위검사 및 실데이터 프리플라이트 통과**. 브랜치별 HEAD SHA·보관 후보 경로/Blob/크기 교차검증 및 2,124개 파일에 대한 복원 가능한 SHA 고정 목록 생성.
- 현재 공식 Pages는 **main 루트 /**에서 배포 중. main의 assets/data가 **1,108,435,890 bytes**로 공식 Published Pages site **1GB 제한을 108,435,890 bytes 초과**. Actions 전환만으로 용량 제한이 없어지지 않으므로 정적 배포 파일 구성 변경이 필요하다.
- **기존 공개 자산 그룹 26개/2,124개/564,189,000 bytes는 그대로 보존**. 파일이 존재하는 웹 브랜치 간 다른 SHA/크기는 0건. App 브랜치 5개·이전 Releases 23개 및 Pages 공개 URL 의존성도 확인했지만 이전 바이너리 전체 정밀검사는 미실시.
- **차단 사유:** Pages 1GB 초과, 앱 23개 릴리스 내용 미검증, 전체 2,124개 구 URL의 배포 전후 SHA 검사 미실시, Pages 전환 자체 미실시, 실제 삭제 명시 승인 필요.
- [8단계 상세 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase8.md), [롤아웃 계획 및 2,124개 파일의 Blob 잠금 목록(JSON)](https://github.com/kimjeon-il/Pando/actions/runs/37826715828/artifacts/11572051217).
- 실행 순서: 고정 원본·브랜치 HEAD 확인 → 1GB 이하 동일 URL 정적 산출물 구축/검증 → Pages 전환·전후 URL 검증 → 모든 브랜치별 정확한 삭제 커밋 사전작성 → 승인받은 한 번의 조정된 배치 적용 → 사후 Web/App 회귀. GitHub 다중 브랜치 ref 변경은 진정한 원자적 트랜잭션이 아니므로 각 ref마다 lease/rollback 기록 필요.
- **8단계는 사전검증 구축 완료, 실물 삭제·배포 구성 전환은 미실시.** 5단계 미해결 Qt 컨트롤러 테스트 4건도 그대로 보류한다.

## 9단계 실제 Pages 정적 산출물 바이트 검증

- 웹 `work/gis`에 `tools/plan-pages-deployment-footprint.mjs`, `tests/unit/pages-deployment-footprint.test.mjs`, `.github/workflows/gis-pages-stage9-publication-gate.yml` 구현. Pages 쓰기 권한 없이 `main`·GIS 원본 Git 트리만을 사용하여 전체 게시 파일 목록과 경로·Git Blob SHA·크기를 산출.
- [실제 CI #37828988316](https://github.com/kimjeon-il/Pando/actions/runs/37828988316) **35/35 단위검사 성공** 및 구 URL 2,124개/564,189,000 bytes 양쪽 원본 완전 동일성 확인. [전체 경로·SHA JSON artifact](https://github.com/kimjeon-il/Pando/actions/runs/37828988316/artifacts/11572936027).
- `main`의 웹 기능+현재 GIS/역사 자료+2124개 공개 구 URL 게시 프로필 **1,047,586,364 bytes**, 공식 제한(보수적 십진 1,000,000,000 bytes)보다 **47,586,364 bytes 초과**. `work/gis`의 SHA 세계지도 번들까지 호환하는 전환 프로필 **1,070,012,175 bytes**, **70,012,175 bytes 초과**.
- JS/GDAL WebAssembly+data 두 파일만 **39,814,980 bytes**. 임의 생략하면 GIS 가져오기가 손상되며 생략해도 `main` 1,007,771,384 bytes로 1GB 초과. 추가로 국가 지도 원본까지 생략한 995,355,601 bytes 가상안은 기능/기존 파일 URL이 깨지므로 `approvedForDeployment=false`.
- GitHub Pages 정식 제한은 **게시 사이트 1GB**. Actions 업로드용 tarball의 별도 용량 한도는 이 제한을 대체하지 않는다. 원본 삭제나 새 CDN 이전만으로 기존 `github.io/Pando/assets/data/...` 공개 URL이 유지되지 않으므로 안전한 배포 전환이 아직 불가능하다.
- [9단계 웹·앱 공통 상세 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase9.md). 개발·연구 원본을 **배포 목록에서만 가상 제외**, 실제 파일·Pages 설정·앱·브랜치 제품 구현은 변경하지 않음. 배포/삭제가 가능한 것으로 승인된 파일 0개.
- 다음 단계에서 **GIS 기능·전체 기존 공개 경로·1GB 제약을 양립시키는 검증 가능한 호스팅/포맷 구조**를 확정해야 함. 불가하면 기존 URL 정책 변경은 명시적으로 다시 승인받아야 함.
## 10단계 Pages 전환·구파일 일괄 정리·main 제품 코드 반영 준비

- 웹 `work/gis` 구현: `tools/prepare-pages-release-bundle.mjs`(실제 바이트+Git Blob SHA 재검증, 1GB 차단, 새 스테이징 폴더 생성), `tools/plan-gis-release-transactions.mjs`(전체 브랜치 롤백 SHA·정확한 파일 삭제 가상 계획 및 3-way 선별 병합 검토), `tests/unit/pages-release-staging.test.mjs`, `tests/unit/gis-release-transactions.test.mjs`, `.github/workflows/gis-release-stage10-preflight.yml`(읽기 권한만).
- [실제 CI #37866525748](https://github.com/kimjeon-il/Pando/actions/runs/37866525748) **49/49 단위검사 통과**. 실데이터 원본 검사·web 7 / app 5 브랜치 HEAD·2,124개 원본 Blob 비교 성공. [승인 차단 사유, 선별 병합 파일·브랜치별 예상 삭제 및 복원 HEAD SHA(JSON)](https://github.com/kimjeon-il/Pando/actions/runs/37866525748/artifacts/11588098513).
- 웹 전체 `work/gis`는 main 대비 **232 commits ahead / 10 behind**, 앱은 **144 ahead / 11 behind**(조사 시점). 전체 GIS 브랜치 병합 금지; 웹 데이터 버전 관리 대상 **23개 파일**, 앱 **17개 파일**만 선별하고 각각 다른 183/78개 변경 경로를 제외. 앱 공통 조상 이후 양쪽 모두 수정한 파일 **6개**를 수동 병합 검토 대상으로 별도 기록.
- GitHub Pages 현행 1GB 제한 초과: main 게시 프로필 **1,047,586,364 bytes**, GIS 호환 프로필 **1,070,012,175 bytes**. `--mode stage`는 이 상태에서 실제 배포 파일 복사를 **거부**하며 CI는 비파괴 `--mode check`만 실행. 아울러 Stage 5 앱 Qt 편집 컨트롤러 테스트 실패 4건, 이전 앱 릴리스 23건의 내용 감사, 기존 모든 공개 URL 본문 SHA-256 비교가 남아 있음.
- [선별 병합 및 배포 상세 실행 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase10.md), [승인 후 활성화할 GitHub Pages workflow **비활성 템플릿**](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/gis-pages-activation.template.yml). 해당 템플릿은 `.github/workflows/`에 두지 않아 실 배포를 트리거하지 않는다.
- 실제 릴리스 준비 다음 조건: 1GB 이하로 옛 바이너리 URL 2,124개+GIS 가져오기/수계/지형/역사 국가 기능 보존 증명 → 앱 충돌 6개/Qt 실패 4개 해결 → 웹/앱 `main` 대상 파일 선별 통합 및 회귀 → Pages Actions 전환 승인/보호 환경 설정 → 구 URL 전수 바이트 대조 성공 → 사용자 승인 뒤에만 7개 웹 브랜치별 HEAD-lease 원본 정리, 실패 시 롤백.
- **10단계 결과는 준비 완료/실행 보류**: 구버전 파일을 지우거나 앱 내장 기본지도, Pages 설정, `main` 제품 코드를 변경하지 않았다. 기존 상설 브랜치·워크트리도 그대로 유지.
## 현재 적용 중인 데이터 URL 정책 및 실제 정리 현황 (2026-10-09)

- **이 절은 6~10단계 당시 작성한 과거 URL 영구 보존 조건보다 우선한다.** 이제 더 이상 사용하지 않는 버전의 `assets/data/**` URL에 대한 영구 접근/리디렉션을 보장하지 않는다. 현행 런타임·생성기·앱에서 실제 참조하는 자산은 계속 유지한다. [공통 정책](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/data-url-retention-policy.md).
- 승인된 첫 삭제 그룹: 웹 `assets/data/terrain/v0.12.0/` **335개 / 374,094,147 bytes**. 웹의 현존 7개 브랜치에서 정확한 Git Blob의 파일만 제거하고 기존 `v0.12.6` **335개는 유지**. 앱 브랜치 5개에는 **정책 문서만 반영**, 앱 내장 지도 데이터 변경 없음.
- 현재 Pages는 여전히 웹 `main` 루트 직접 배포 방식. [실제 Pages 빌드 #37868609497](https://github.com/kimjeon-il/Pando/actions/runs/37868609497) 성공. [삭제 전용 검증 #37869177623](https://github.com/kimjeon-il/Pando/actions/runs/37869177623) Node 5/5 성공: 구형 매니페스트 URL HTTP 404, 최신 매니페스트 URL HTTP 200, 웹 7개 브랜치 모두 구형 0·현행 335, 앱 5개 모두 최신 지형 참조.
- `main` 전체 현재 추적 파일 크기 **832,753,069 bytes**(데이터 **734,341,743 bytes**). 보수적 Pages 게시 프로필 `main` **673,492,217 bytes**, GIS 전환 프로필 **695,918,028 bytes**로 두 프로필 모두 1GB 이내. 구버전 보관 후보는 현재 **25그룹·1,789파일·190,094,853 bytes**이며, 자동으로 추가 삭제하지 않는다.
- [11단계 실삭제 보고서·7브랜치 롤백 SHA](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase11.md), [335개 원본 복원 블롭·크기 목록](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/terrain-v0120-retirement-evidence.json). 기타 앱 Qt 회귀 미해결, 전체 제품 코드 `main` 병합·Pages Actions 게시 방식 전환은 별도 작업이다.
## 위험 및 남은 의존성

- 웹 Worker는 새 데이터 매니페스트로 전환했지만 공유 국경선 캐시의 레거시 `v0.34.0` 경로는 호환 목적으로 유지함.
- `build-world-preview.mjs`의 출력 기준은 프로그램 버전 대신 고정 데이터 입력 매니페스트로 변경됐으며, 구형 배포 자산은 호환성을 위해 유지함.
- 웹 수계 v0.13.1은 v0.13.0 index/detail/shards를 사용하므로 현재 모두 유지.
- 웹·앱 역사 라이브러리 91개 바이트 차이 중 색인 1개·국가별 90개는 5단계에서 의미 검증 완료. 국경 형상 차이는 0건이며, 앱 원본 데이터 갱신은 수행하지 않음.
- 신규 불변 SHA 경로와 승인되지 않은 잔여 구버전 경로가 공존한다. 구 URL 영구 보존 의무는 11단계에서 해제했으며, 지형 v0.12.0은 실제 삭제했다. 나머지 구버전 그룹은 별도 승인/의존 확인 후 정리한다.
- 앱은 `assets/world/manifest.json` v2에 출처·SHA를 통합하고, Qt 리소스 경로를 해당 매니페스트에서 유도하도록 변경. 이미 포함된 원본 지도 바이트와 앱 오프라인 실행 정책은 유지.

## 브랜치 반영 규칙

이 진행 기록을 현재 존재하는 웹·앱 **모든 브랜치**에 동일한 바이트로 반영한다. 제품 구현 및 상세 보고서는 웹·앱 각각의 `work/gis`에 남기며 `work/*` 간 무관한 코드 자동 병합과 `work/* → main` 구현 코드 반영은 하지 않는다. GitHub 원격 문서 반영은 로컬 워크트리 갱신과 별개이다.
