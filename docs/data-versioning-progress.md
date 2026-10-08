# 데이터 버전 관리 구조 개선 — 진행 기록

- 기준일: 2026-10-08
- 범위: 웹 `kimjeon-il/Pando` · 앱 `kimjeon-il/PandoEditor`.
- 주 구현 브랜치: 웹 `work/gis` (별도 실험 브랜치 생성 없음).
- **1단계:** 웹·앱 데이터 인벤토리 및 참조 관계 조사 완료. 원본은 미변경.
- **2단계:** 웹에서 독립 데이터 매니페스트·내용 해시 기반 객체·추가 생성/검증 CLI 구현 및 커밋. 새 데이터 번들에 대한 정적 경로/Blob/크기 검사 완료. 기존 앱 버전 기반 생성기·웹 로더는 전환 전까지 유지하며 원격 CI·실제 렌더링 검증은 별도 필요.
- **3단계:** 웹 `work/gis`에서 신규 `world/current.json` 런타임 전환, 콘텐츠 주소 기반 자산 캐시, 국경선 캐시 URL 매니페스트 전달, 빌드·검증 경로 개편 코드를 구현·커밋함. 정적/분리 검증은 보고서에 기록했으며 전체 CI·실제 브라우저/Qt 테스트는 미완료.
- **4~6단계:** 미착수. 앱 내부 데이터 교체, 기존 자산 삭제, 실제 서비스 배포는 진행하지 않음.

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
- 다음 단계 전 전체 빌드/실브라우저 검사와 이전 캐시 호환성을 추가 검증할 필요가 있음.

## 위험 및 남은 의존성

- 웹 Worker는 새 데이터 매니페스트로 전환했지만 공유 국경선 캐시의 레거시 `v0.34.0` 경로는 호환 목적으로 유지함.
- `build-world-preview.mjs`의 출력 기준은 프로그램 버전 대신 고정 데이터 입력 매니페스트로 변경됐으며, 구형 배포 자산은 호환성을 위해 유지함.
- 웹 수계 v0.13.1은 v0.13.0 index/detail/shards를 사용하므로 현재 모두 유지.
- 1단계 웹·앱 역사 라이브러리 285개 중 91개가 바이트 단위로 다르며 실질 내용·압축 차이는 별도 검토 필요.
- 신규 해시 경로와 과거 경로가 공존하므로 작업 폴더/배포 파일 수가 일시적으로 증가함. 6단계에서 안전하게 정리.
- 앱의 데이터 고정 출처·오프라인 실행·Qt 빌드 설정은 이번 단계에서 변경하지 않음.

## 브랜치 반영 규칙

이 진행 기록을 현재 존재하는 웹·앱 **모든 브랜치**에 동일한 바이트로 반영한다. 제품 구현 및 상세 보고서는 웹 `work/gis`에 남기며 `work/*` 간 무관한 코드 자동 병합과 `work/* → main` 구현 코드 반영은 하지 않는다. GitHub 원격 문서 반영은 로컬 워크트리 갱신과 별개이다.
