# 앱 PLAC v2 언어별 지명 이식 및 동기화 계약

원본 웹: `kimjeon-il/Pando:data/places-tier1-major-cities` (커밋 `fc0cb82a9e363be6a3009cf07932a3c9ce59ddd2`).
앱 작업: `kimjeon-il/PandoEditor:data/places-tier1-major-cities`.

앱의 `contracts/places/v2.json`은 원본 웹 브랜치에서 가져온 교환 계약이다.

## 구현 구조

- `app/placeruntimestore.h/.cpp`: PLAC v2 바이너리 레코드와 다국어/연도·일자 이력 디코딩. 기존 P7 v1 고정 테스트 자료도 유지.
- `app/placenamedisplay.h/.cpp`: 언어 토글, 기본값, 날짜별 지명 선택, 중복 제거 정책의 단일 C++ 소유자.
- `app/placeruntimeprovider.cpp`: v2 레코드 유지 및 선택·캐시·Worker 생명주기.
- `app/editorplace.cpp`: Qt 폰트 실측에 따른 여러 언어의 단일 충돌 상자 계산.
- `engine/include/pandoeditor/map/labelengine.h`, `engine/src/labelengine.cpp`: 하나의 지명 ID에 여러 텍스트 행 전달.
- `app/editorpresentation.cpp`, `ui/common/MapView.qml`: 다중 행 실제 표시, 우선 언어 강조.
- `app/editorview.cpp`, `ui/common/MapDisplayControls.qml`: 보기 → 지명 언어별 스위치와 사용자별 설정 저장.

한국어/영어/원어는 각각 켜고 끌 수 있으나 모두 끄지는 못한다. 동일 표기는 한 번만 보여주고, 없는 번역은 생성하지 않는다. 설정은 프로젝트 데이터가 아닌 사용자 환경설정 JSON의 `labels.place.languages`에 저장한다.

## 2026-10-09 지명 명칭 선별 정책 (1차 9개 도시 검토)

- 같은 지명 ID·같은 역사 시점에서는 ko/en 대표명은 각각 1개씩, 원어명은 원칙적으로 1개로 제한한다. 다만 해당 시점에 여러 공용어가 실질적으로 쓰이는 도시에서는 근거 있는 원어명을 최대 3개까지 허용한다.
- **원어명은 해당 시점의 통치국 또는 관할 행정기관이 공식적으로 사용한 도시명**을 선정한다. 중앙정부 주류 언어와 관할 지역의 공식 언어가 다르면 해당 지역의 공식 용례를 조사하고, 복수 공용어 사용이 입증되면 최대 3개를 시대별로 선정하되, 기존 단일 원어명 인터페이스에서는 첫 번째 이름을 대표로 사용한다.
- **한국어·영어명은 그 시대에 선정된 동일한 원어명에 대응하는 명칭**으로 정한다. 일반적인 번역·음역뿐 아니라 근거 있는 관용 외칭도 허용하되 시대가 다른 명칭을 혼합하지 않는다.
- 통치국의 변경 자체는 개칭이 아니다. 통치국 변경 시 공식 원어명을 재검토하고, 공식 도시명이 실제로 변경되었을 때만 관련 언어별 명칭을 검토한다. 한국어 지도 편집 규칙만 달라지면 한국어명만 변경한다.
- 구어명·별칭·단순 철자/로마자 변형은 원칙적으로 정제 목록에서 제외한다. 검색용 별칭은 대표명과 구별하며, 근거 있는 복수 공용어 이름은 정식 원어명으로 최대 3개 선정한다. 실제 지도 표출 수와 배율 정책은 후속 검토한다.
- 중국 대륙의 중국어계 지명은 **1912-01-01(중화민국 건국)** 전에는 한국식 한자음, 이후에는 중국어 발음 기반 한국어 지명을 사용한다. 중국에서 실제로 개칭한 사건과는 별도로 기록한다.
- 웹과 앱의 규칙 정본을 `reports/places/historical-display-policy.json` 및 `reports/places/korean-map-label-policy.json`으로 동기화하였다. 1차 9개 도시의 명칭 검토를 마쳤다. 델리는 1858-11-01 우르두어 대표 전환, 1947-08-15 힌디어·우르두어 병기, 2004-01-26 펀자브어 추가를 검수용 원어명 목록에 반영했다. 1858·1947은 편집상 날짜이고, 무굴기 페르시아어 공식성은 추가 검증이 필요하다. 2~15차와 실행용 데이터·바이너리 계약·렌더러는 변경하지 않았다.

복수 원어명은 `defaultNativeNames[]` 및 `displayTimeline[].nativeNames[]`에 시점별 전체 목록으로 보존했다. 기존 PLAC v2는 단일 원어명만 표시하며 실제 병기·배율별 출력 기능은 아직 구현하지 않았다.

## 검증 및 제약

`tests/place_sync_contract_tests.cpp`에는 4개 PLAC v2 바이트·언어·역사명 예제와 11개 시나리오, 설정 지속성과 Qt 라벨 객체 검사 코드가 있다.

Qt 글꼴 실제 치수와 웹의 CSS 추정 치수는 다를 수 있다. 앱과 웹 모두 지명용 단일 활성 지도 날짜가 아직 연결되지 않았다. 양쪽 내장 manifest도 비어 있으므로 실제 대도시 데이터의 게시·표시는 별도 작업이다.

기존 `tools/place-runtime-contract/`와 `tests/fixtures/web-place-runtime-source/`는 과거 웹 고정 소스 v1 감사·회귀 자료다. 원본을 덮어쓰지 않고 새 v2 검사와 병행한다.

Qt6 개발 환경에서 다음과 같이 집중 검증한다.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target place_sync_contract_tests place_runtime_store_tests place_controller_tests
ctest --test-dir build -R 'place_sync_contract_tests|place_runtime_store_tests|place_controller_tests' --output-on-failure
```
