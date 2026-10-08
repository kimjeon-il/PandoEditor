# 데이터 버전 관리 1단계 — 앱 GIS 자산 및 웹 출처 기준선

> 정적 인벤토리: 2026-10-08. 기준 저장소 `kimjeon-il/PandoEditor`, 고정 Git tree [`5b3e42a9696abb34cadb3f681e47fa973456593c`](https://github.com/kimjeon-il/PandoEditor/tree/5b3e42a9696abb34cadb3f681e47fa973456593c) (`work/gis` 조사 시점). 기준 웹 Git tree [`2b79bcbe48c2b725624a576c7746607771875a93`](https://github.com/kimjeon-il/Pando/tree/2b79bcbe48c2b725624a576c7746607771875a93).
> 이 문서는 조사 기록만 추가한다. 앱 파일·C++·QML·Qt resources·고정 매니페스트는 수정하지 않는다.

## 1. 현행 앱 자산 분류

`assets/**` 전체 624개, 34,779,338 bytes. (Git tree `truncated=false`.)

| 자산군 | 파일 수 | 추적 파일 크기 합계 (bytes) |
|---|---:|---:|
| 내장 기본 지도 assets/world/ | 13 | 23,013,610 |
| 독립적인 역사 라이브러리 | 285 | 6,906,410 |
| 기본 국기 | 245 | 3,442,962 |
| 검증용 geometry 원본·이식 사본 | 77 | 1,032,551 |
| 지명 manifest | 1 | 108 |
| 역사 파일 | 1 | 344,874 |

`assets/world/**`는 13개이며, 실제 지도 데이터는 웹을 매번 동적으로 내려받지 않고 고정 사본으로 유지한다. 예외적으로 수계·지형의 세부 타일은 별도 설치/manifest 계약에 따른다. 앱 오프라인 동작과 원본 출처 고정을 개편 시 보존해야 한다.

## 2. 기본 세계지도 6개 고정 자산

현재 `assets/world/manifest.json`이 선언한 6건과 실제 Git tree Blob을 확인했다.

| 논리 자산 | 상대 경로 (assets/world 기준) | 크기 (bytes) | Git Blob SHA 일치 |
|---|---|---:|---|
| `countryPreview` | `countries-preview-v0.33.0.geojson.gz` | 917,604 | 일치 |
| `previewMesh` | `world-mesh-preview-v0.33.0.bin.gz` | 1,733,548 | 일치 |
| `countryCanonical` | `countries-canonical-v0.33.0.pcg.gz` | 5,122,043 | 일치 |
| `canonicalMesh` | `world-mesh-v0.12.6.bin.gz` | 14,619,043 | 일치 |
| `terrain` | `terrain/v0.12.6/manifest.json` | 2,024 | 일치 |
| `hydro` | `hydro/v0.13.1/manifest.json` | 52,975 | 일치 |

추가 필수 메타데이터: `country-label-anchors-v0.10.1.json` (13,826 bytes), `physical-inventory-c0bd31d1.json`, DEM 출처/재현 inventory, DEM `v0.13.3` manifest.

앱의 선언된 원본 `worldMapCommit`은 `c0bd31d13dc8495593d78cf51f7cc195de7c9469` (기본 지도). 국명 기준점과 수계 manifest는 이후 웹 `a87f4d27fb1bc16528aa57242ecb52e28e4550b2`에서 교정된 Blob을 사용하도록 별도 provenance를 보유한다(`assets/world/NOTICE.md`). 여기서 앱 정밀 국가 패킷 v0.33.0과 웹 활성 v0.36.0은 **동일 Blob이 아니다**. 임의로 현재 웹 파일 하나만 덮어쓰지 말 것.

## 3. 실제 참조/검증 코드 분류

| 책임 | 파일 | 현재 동작·2단계 이후 위험 |
|---|---|---|
| 내장 지도 적재 | `app/worlddataset.cpp`, `app/worlddatasetloader.cpp` | 6개 파일명·버전·Git Blob SHA를 C++에 재하드코딩하고 매니페스트와 대조. 자동 동기화 시 여러 위치를 고쳐야 함 |
| 기본 파일 inventory | `assets/world/manifest.json`, `assets/world/NOTICE.md` | 기본 데이터/후속 패치 출처·버전·SHA; 한 묶음으로 감사 가능해야 함 |
| 패키지 검증 | `tools/verify-world-assets.mjs` | C++와 별도의 6개 예상 파일명·Blob 표를 유지. 데이터 갱신 시 검증기·manifest 함께 수정하는 중복 |
| 외부 물리 자료 | `assets/world/physical-inventory-c0bd31d1.json`, `assets/world/terrain/**`, `assets/world/hydro/**` | 외부 자료의 절대 출처 커밋/해시 고정; 섣불리 제거 금지 |
| 번들/QML 자원 | `app/CMakeLists.txt` | 국명 anchors 및 DEM 관련 resource 파일 경로 하드코딩; 파일 리네임 시 패키징 수정 필요 |
| 프로젝트 미리보기 | `app/projectpreviewcache.cpp` | 원본·알고리즘·저장 프로젝트 해시를 식별자로 사용하며 기본 지도 내장 패킷과 별개. 유지 |
| 역사 catalog | `assets/territorial-library-v2/index.json` 및 284개 gzip | 앱 독립 사본; 웹 `generated/v2`와 파일명 일치 여부·해시 drift 검증 필요 |
| 장소 | `assets/place/manifest.json` | 현재 빈 v1 manifest, 추가 지명 데이터 동기화 시 도메인별 계약 필요 |

## 4. 웹과의 동일성/상이점

- 내장 world manifest 6개 파일 모두 git-tree 존재·Git Blob 일치. 웹의 대응 **같은 명칭·버전** 파일 6개도 Blob 일치.
- 국명 anchors/BJN/SER 교정본 및 hydro v0.13.1 manifest가 현재 웹의 Blob과 동일하다.
- 앱 역사 라이브러리 파일명 285개 중 194개만 현재 웹 생성본과 Git Blob 일치, 91개 불일치. gzip 압축 차이·문서 속성 차이·실제 형상 차이는 **이 조사만으로 식별 불가능**.
- 앱 정밀 국가 패킷은 v0.33.0, 웹의 활성 패킷은 v0.36.0. 국가 ID/순서·파일 포맷·문서 geometry 호환성 검증 없는 자동 교체 금지.
- `assets/world`는 웹처럼 연속 프로그램 버전마다 지도를 복제한 구조가 아니므로 대량 삭제에 따른 이득은 낮다. 문제는 **고정 출처정보의 다중 하드코딩과 동기화 번거로움**이다.

## 5. 검사 결과 및 후속 기준

- 웹 시작 자산 경로/크기 5/5, 앱의 manifest/Blob 6/6, 웹 수계의 v0.13.0 의존 파일 참조/크기 6/6: 총 **17/17 정적 검사 통과**.
- 이번 확인은 도형 바이트/시맨틱 비교, 실제 앱 Qt 빌드, Web Worker 실행, Android 렌더링, 오프라인 패키지 실행 테스트는 **포함하지 않는다**.
- 후속 앱 개선은 웹에서 확정된 **불변 데이터 묶음 매니페스트 해시**를 받아 검증된 자산만 선택적으로 동기화하는 방향. 단, 자동 반영/임의 온라인 최신 버전 채택은 금지; 독립 실행 유지.
- 기존 6개 파일과 동기화된 anchors·hydro manifest를 1단계에서 변경하거나 재다운로드하지 않는다.
