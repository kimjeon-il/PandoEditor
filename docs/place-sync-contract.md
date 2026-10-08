# 앱 PLAC v2 언어별 지명 이식 및 동기화 계약

원본 웹: `kimjeon-il/Pando:work/places`; 출처 고정 커밋은 `reports/places/source-manifest.json`의 `webCommit`으로 추적한다.
앱 작업: `kimjeon-il/PandoEditor:work/places`.

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


## 2차 한국 도시 역사명 검토 (2026-10-09)

- 인천·대구·대전 3개 도시 역사적 대표명 검수 완료. 요코하마·나고야·교토·광저우·톈진·난징은 다음 검수 대상.
- 인천 1945-10-10 제물포시 개칭, 1945-10-27까지 사용한 뒤 1945-10-28 인천부 복귀. Chemulpo는 당시 통용된 영어 외칭을 소급 선정했으며 법령상 영어 지명이라고 단정하지 않는다.
- 일본어식 영문 대표형은 Jinsen·Taikyu·Taiden, 광복 후 2000년 이전 영문형은 Inchon·Taegu·Taejon, 2000-07-07 이후는 현행 로마자 표기.
- 대전의 1801년 지명은 근대 도시가 아닌 농촌 취락. 시대별 도시 규모·등급은 아직 별도 미결정이며 배포 지명 데이터·렌더러를 변경하지 않았다.

## 역사 도시의 생성·소멸·재건 (2026-10-09, 정책만)

**도시 라벨의 시대별 유효 여부는 이름·행정구역·5단계 등급과 독립적이다.** 같은 장소의 어촌·농촌 취락이 실제 존재했다고 해서 당시 이미 도시 라벨 자격을 갖춘 것은 아니다. 반대로 옛 도시가 소멸해도 역사 장소·지명·유적·검색 레코드를 지우지 않는다.

- `displayTimeline[]`: 한국어·영어·원어 명칭의 역사적 변화만 보유. 이름이 오래되었다는 이유로 도시 존재 시기를 확장하지 않는다.
- `temporalEligibility`: 기존 `cityEstablishedFromYear`/`cityEstablishedFromDate` 자료는 보존하고, 후속 검수에서 `lifecycleReviewStatus`, `reviewedCoverage`, `activeIntervals[]`를 지원하도록 확장한다. 상태는 `unreviewed | provisional | verified`.
- `activeIntervals[]`: 도시로서 실제 지도 표시 자격이 있었던 기간을 **0개 이상** 보존한다. 여러 기간으로 소멸·재건을 표현한다. 배열이 없거나 검수 범위가 확정되지 않았다면 도시가 있었다/없었다고 단정하지 않는다.
- 기간 시작은 `fromDate`(정확한 날짜 포함) 또는 `fromYear`(연도만 확인)이고, 종료는 `untilDateExclusive`(정확한 첫 비활성일) 또는 `untilYear`(그 연도 중 소멸, 월일 불명)이다. 양쪽 끝은 독립적으로 생략 가능하다. 날짜 정밀도에서는 연도 경계일의 결론을 `unresolved`로 둔다.
- **평가 결과는 `eligible | ineligible | unresolved`의 3가지**다. 검증한 시간 범위 밖, 증거가 불충분한 기간, 연도만 알려진 전환 연도에 임의의 1월 1일·12월 31일을 대입하지 않는다.
- 동일 장소의 지명 변경·연속적 도시 성장은 하나의 ID를 유지한다. 실제 도시 중심지가 다른 위치로 이전하면 원칙적으로 기존 도시와 새 도시를 **별도 ID**로 두고 `historicalGeography`에 전임·후계 관계를 명시한다. 동일 장소에서 소멸·재건된 경우 증거가 있으면 단일 ID의 복수 기간으로 나타낼 수 있다.
- 중국 업(鄴) 같은 소멸 도시도 현대 GeoNames 후보에서 누락될 수 있으므로 역사자료 기반으로 별도 발굴할 수 있다. GeoNames의 역사·폐허·폐기 취락 분류는 발견 단서이지 도시 자격을 자동 확정하지 않는다.
- 요코하마·대전은 도시 표시 시작일을 나중에 실증하고, 업 같은 사례는 옛 도성 소멸 시기·현재 지명이나 새 소재지와의 관계를 먼저 검토한다. **이번 정책 수정에서 해당 도시의 실제 날짜·5단계 등급·표시 우선순위는 확정하지 않는다.**

미래 스테이징 구조 예시(가상의 도시로, 실제 지명에 적용하지 않음):

```json
{
  "temporalEligibility": {
    "lifecycleReviewStatus": "verified",
    "reviewedCoverage": { "fromYear": 1800, "untilYear": 2000 },
    "activeIntervals": [
      { "fromDate": "1830-05-01", "untilDateExclusive": "1870-01-01", "sourceUrl": "<근거문서 URL>" },
      { "fromDate": "1900-06-01", "sourceUrl": "<근거문서 URL>" }
    ]
  }
}
```

실제 구현 단계에는 기존 자료와의 충돌 검사, 관련 장소의 ID/위치 검증, 영문·원어명 변경과 독립된 날짜 선택, 검수 범위 및 3상태 판정 테스트가 필요하다. 기존 PLAC v2 바이너리와 웹·앱 렌더러는 이 확장 형식을 아직 지원하지 않으므로 **이번에는 정책만 추가**한다.

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
