# 데이터 버전 관리 4단계 — 앱 출처·동기화 계약 개편

## 범위·안전 원칙

- 저장소: `kimjeon-il/PandoEditor`, 작업 브랜치 `work/gis`.
- 웹 기준: `kimjeon-il/Pando`, 승인할 때는 **40자리 커밋 SHA**를 지정한다. 움직이는 `main`을 자동으로 따라가지 않는다.
- 기존 내장 지도·수계·지형·국명 기준점의 바이트는 이번 단계에서 **변경하지 않는다**.
- 앱 `main`으로 구현 코드를 병합하지 않으며 기존 워크트리·구버전·아카이브를 삭제하지 않는다.
- `assets/world/manifest.json`의 스키마를 2로 올리되, **프로젝트 파일 저장 스키마는 변경하지 않는다**.

## 1. 앱 내부 정본

`assets/world/manifest.json`은 단일 데이터 계약이다.

- 7개 역할: `countryPreview`, `previewMesh`, `countryCanonical`, `canonicalMesh`, `terrain`, `hydro`, `labelAnchors`.
- 역할별 내부 경로, 출처 `source.ref`·`source.path`, SHA-256, Git Blob SHA, 정확한 저장 바이트 크기, 필수/선택 상태.
- `origin.repository`, `origin.mode`, `origin.webCommit`, `origin.worldBundleSha256`.
- 기존 묶음은 `mixed-pinned`로 명확히 표기한다. 나라 도형과 메시·지형은 웹 `c0bd31d1`, 국명 기준점과 수계 매니페스트는 `a87f4d2` 출처다.
- 실제 승인된 웹 단일 번들 적용 시 `web-bundle`로 변경하며, 웹 `world/current.json` **원본 바이트의 SHA-256**과 커밋을 보존한다.

`app/worlddataset.cpp`는 국가 파일명·버전·Git Blob SHA를 개별 하드코딩하지 않고 이 매니페스트로 검증한다. Qt 빌드에 포함된 리소스 매니페스트가 실행 시 사용하는 고정 데이터 선택 근거이며, 무결성 검사를 통과하지 못하는 바이트는 거부한다. 단일 manifest 자체는 앱 빌드 리소스에 포함되며 앱 실행 시 온라인의 최신 manifest로 대체되지 않는다.

`app/CMakeLists.txt`는 매니페스트의 현재 역할별 경로를 읽어 Qt BIG_RESOURCES 및 국명 anchor resource에 등록한다. 소스 변경 시 CMake를 재구성해야 하며, app manifest 교체만으로 이미 배포된 Qt 바이너리가 변경되는 일은 없다.

## 2. 승인형 동기화 도구

도구: `tools/sync-world-data.mjs`

```bash
# 비교만 수행(파일 수정 없음)
node tools/sync-world-data.mjs --web-ref <승인 후보 웹 커밋 SHA>

# 같은 커밋의 기존 웹 작업 폴더로 비교 (git HEAD 일치 검증)
node tools/sync-world-data.mjs --web-ref <승인 후보 웹 커밋 SHA> --web-root <웹 체크아웃 경로>

# 보고서의 전체 번들 SHA-256을 보고 명시적으로 승인을 내린 경우에만 변경
node tools/sync-world-data.mjs --web-ref <위와 동일한 웹 커밋 SHA> --apply --approve-bundle <보고서의 64자리 SHA-256>
```

- 웹 `world/current.json`의 스키마·256비트 콘텐츠 주소·자산 저장 크기/압축 해제 크기·PCG/CMG 헤더·국가 수(258)/정본 좌표수(548454)·라벨 수를 검증한다.
- 로컬 현재 7개 파일의 SHA-256/Git Blob/크기가 우선 유효해야 한다. 검증되지 않은 구버전 앱에 무작정 다운로드를 적용하지 않는다.
- SHA 기반 새 파일만 추가하고 이전 파일을 보존한다. 기존 경로에 다른 바이트가 존재하면 실패한다. 새 자산 확인 후 마지막에 앱 매니페스트를 교체한다.
- `--approve-bundle`이 실제 가져온 `world/current.json`의 SHA와 같지 않으면 적용을 거절한다.
- **물리 자료 보호:** 웹 최신 수계/지형 매니페스트가 현재 앱에 고정된 값과 달라지면 `physicalDrift`로 표시하며, 물리 데이터 inventory·외부 설치 경로의 별도 정합성 검토 없이는 `--apply`를 거절한다.
- 앱 로더가 웹에 접속하지 않으므로 오프라인 실행·압축 번들 식별자·사용자 프로젝트와 미리보기 캐시의 분리를 유지한다.

## 3. 코드·검증 경계

| 경로 | 작업 |
|---|---|
| `assets/world/manifest.json` | v2의 단일 파일·출처·해시 계약 |
| `app/worlddataset.cpp`, `app/worlddataset.h` | 파일별 고정 상수 제거, 범용 manifest 검증 |
| `app/CMakeLists.txt` | Qt 내장 자산 경로를 manifest에서 생성 |
| `app/editorcontroller.cpp` | 승인된 manifest에 속한 label anchors 검증된 읽기 |
| `tests/country_label_anchor_tests.cpp` | 고정 QRC 파일명 의존 해소 |
| `tools/lib/world-dataset-contract.mjs` | 검증기·동기화 명령에 쓰는 공통 계약 |
| `tools/sync-world-data.mjs` | 읽기 전용 비교·명시적 승인 후 적용 |
| `tools/verify-world-assets.mjs` | 스키마 2에 대한 오프라인 7자산 검증 |
| `tools/verify-release-assets.mjs` | 동적 CMake 리소스 목록을 감안한 검사 |
| `tools/world-dataset-stage4.test.mjs` | 승인·경로·손상·멱등성·물리자료 보류 단위검사 |
| `.github/workflows/world-dataset-stage4-gate.yml` | 실제 Node/Qt 대상 검사 |

## 4. 미적용·후속

- 웹 최신 번들로 앱의 실제 기본지도를 교체하는 일은 **승인된 SHA의 별도 명시적 적용**에 해당하며, 이번에는 수행하지 않는다.
- 수계의 shard/index/detail과 DEM·raster 타일 전체 동기화, 역사 국가 catalog(앞선 조사에서 91개 파일 차이)의 의미 비교 및 교차 렌더링 검사는 5단계·후속 과제다.
- `tools/m71`, `tools/m72`, 이전 도형 oracle의 불변 기준 커밋은 새 앱 번들 정본으로 일괄 대체하지 않는다. 구형 fixture의 원본 추적이다.
- 패키지별 Windows/Android 바이너리 배포 검증을 수행하지 않고, 대상 Qt/Linux 오프라인 CTest를 수행한 경우에만 그 결과를 별도로 기록한다.

## 5. 검증 실적

실제 검증이 실행된 뒤 **최종 CI 실행 ID·결과·재시도/제약**을 이 아래에 추가한다. 검증 전에는 성공으로 주장하지 않는다.
