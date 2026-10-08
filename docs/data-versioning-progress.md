# 데이터 버전 관리 구조 개선 — 진행 기록

- 최신 검증일: 2026-10-09 (원본 조사일 2026-10-08)
- 범위: 웹 `kimjeon-il/Pando` · 앱 `kimjeon-il/PandoEditor`.
- 주 구현 브랜치: 웹·앱 각각 `work/gis` (별도 실험 브랜치 생성 없음).
- **1단계:** 웹·앱 데이터 인벤토리 및 참조 관계 조사 완료. 원본은 미변경.
- **2단계:** 웹에서 독립 데이터 매니페스트·내용 해시 기반 객체·추가 생성/검증 CLI 구현 및 커밋. 새 데이터 번들에 대한 정적 경로/Blob/크기 검사 완료. 기존 앱 버전 기반 생성기·웹 로더는 전환 전까지 유지하며 원격 CI·실제 렌더링 검증은 별도 필요.
- **3단계:** 웹 `work/gis` 구현·커밋 및 GitHub Actions **대상 통합검증 완료**. 국가 데이터·생성기·자산/버전·구문·ESLint 확인, Node 단위검사 **33/33**, UI 번들 빌드, Chromium 실제 시작·캐시·복구 **3/3** 통과. 전체 Playwright 스위트·앱 Qt·배포는 범위 밖.
- **4단계:** 앱 `work/gis`에서 단일 v2 데이터 매니페스트·원본 출처·승인형 웹 동기화·Qt 리소스 경로 연동 구현 및 실제 GitHub Actions 대상 검증 완료. Node 6/6, 내장 자산 7개, 릴리스 소스 18개, Qt 오프라인 CTest 3/3 모두 통과. **웹 최신 자산을 앱에 실제 적용하지 않았으며 기존 기본 지도 바이트 유지.**
- **5단계:** 앱 `work/gis`에 읽기 전용 웹·앱 역사 국가 라이브러리 의미 비교와 전용 CI 추가·검증 완료. 원본 웹 `ebcfae4d`와 앱 285/285 동일(국가 파일 284/284+색인), 조사 웹 `a47c4c54`와는 **194개 완전 동일·90개 존속 시작일만 변경·도형/기타 의미 불일치 0건**. Node 8/8 및 2개 실데이터 교차검증 통과. 앱 원본 자산은 변경하지 않음.
- **6단계:** 미착수. 구형 자산 삭제·앱 기본지도 실제 교체·`main` 제품 코드 병합·배포 미진행.

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

## 위험 및 남은 의존성

- 웹 Worker는 새 데이터 매니페스트로 전환했지만 공유 국경선 캐시의 레거시 `v0.34.0` 경로는 호환 목적으로 유지함.
- `build-world-preview.mjs`의 출력 기준은 프로그램 버전 대신 고정 데이터 입력 매니페스트로 변경됐으며, 구형 배포 자산은 호환성을 위해 유지함.
- 웹 수계 v0.13.1은 v0.13.0 index/detail/shards를 사용하므로 현재 모두 유지.
- 웹·앱 역사 라이브러리 91개 바이트 차이 중 색인 1개·국가별 90개는 5단계에서 의미 검증 완료. 국경 형상 차이는 0건이며, 앱 원본 데이터 갱신은 수행하지 않음.
- 신규 해시 경로와 과거 경로가 공존하므로 작업 폴더/배포 파일 수가 일시적으로 증가함. 6단계에서 안전하게 정리.
- 앱은 `assets/world/manifest.json` v2에 출처·SHA를 통합하고, Qt 리소스 경로를 해당 매니페스트에서 유도하도록 변경. 이미 포함된 원본 지도 바이트와 앱 오프라인 실행 정책은 유지.

## 브랜치 반영 규칙

이 진행 기록을 현재 존재하는 웹·앱 **모든 브랜치**에 동일한 바이트로 반영한다. 제품 구현 및 상세 보고서는 웹·앱 각각의 `work/gis`에 남기며 `work/*` 간 무관한 코드 자동 병합과 `work/* → main` 구현 코드 반영은 하지 않는다. GitHub 원격 문서 반영은 로컬 워크트리 갱신과 별개이다.
