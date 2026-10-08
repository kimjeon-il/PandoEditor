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

## 2026-10-08 지명 명칭 선별 정책 (데이터에는 아직 미적용)

- 같은 지명 ID·같은 역사 시점에서는 ko/en/native 대표명을 각 최대 1개만 인정하며, 실제 개칭·한국어 지도 편집 기준 변경이 있을 때 해당 언어만 교체한다.
- 구어명·별칭·단순 철자/로마자 변형은 원칙적으로 정제 목록에서 제외한다. 검색상 필수적인 별칭과 복수 공용어는 명확한 사용 사유·언어·출처가 있을 때만 예외적으로 보존하며 지도에 추가 대표명으로 표시하지 않는다.
- 중국 대륙의 중국어계 지명은 **1912-01-01(중화민국 건국)** 전에는 한국식 한자음, 이후에는 중국어 발음 기반 한국어 지명을 사용한다. 중국에서 실제로 개칭한 사건과는 별도로 기록한다.
- 웹과 앱의 규칙 정본을 `reports/places/historical-display-policy.json` 및 `reports/places/korean-map-label-policy.json`으로 동기화하였다. 1~15차 배치 87개 도시 명칭, 실행용 데이터, 바이너리 계약·렌더러는 건드리지 않았고 기존 이름별 정제 검사는 후속 작업이다.

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
