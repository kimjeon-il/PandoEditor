# Timeline / Temporal Contract v1

확정일: 2026-10-04

이 문서는 PandoLab 웹(`kimjeon-il/world-map`)과 PandoEditor 앱
(`kimjeon-il/PandoEditor`)이 공유하는 **시간대(타임라인) 기능의 규범 계약**이다.
플랫폼별 UI나 구현 언어와 무관하게 아래 의미는 동일해야 한다.

T0 단계는 의미 계약만 고정한다. 저장 스키마 변경, resolver, 편집 분기, UI 연결은
후속 단계에서 이 계약을 구현하며, T0 자체에서는 프로젝트 wire schema를 올리지 않는다.

## 1. 기준 브랜치

- 웹: `feat/timeline-web`, `world-map/main@90a0137`에서 분기
- 앱: `feat/timeline-app`, `PandoEditor/codex/integration@fdafc8e0`에서 분기

두 브랜치는 독립적으로 개발하지만 이 문서의 규칙을 별도로 변형하지 않는다.

## 2. 타임라인 커서

사용자가 조작하는 타임라인 커서는 **달(calendar month)** 단위다.

- canonical 형식: `YYYY-MM`
- 확장 연도: 기존 temporal 규칙과 동일하게 `+YYYYY-MM`, `-YYYY-MM` 등을 허용
- 연도 0은 허용하지 않는다.
- 월은 `01` ~ `12`
- 커서 자체에는 일(day) 정밀도를 사용하지 않는다.

예: `1914-07`, `1945-08`, `-0001-03`.

## 3. 월 스냅샷 해석

커서 `YYYY-MM`은 그 달의 **마지막 날 상태**를 의미한다.

예:

- `1914-07` -> 1914-07-31 상태
- `1916-02` -> 1916-02-29 상태
- `1915-02` -> 1915-02-28 상태

윤년 계산은 현재 웹/앱 temporal 구현과 동일한 proleptic Gregorian 규칙을 사용한다.

이 규칙 때문에 정확한 일자 자료도 월 단위 화면에 결정적으로 투영할 수 있다.
예를 들어 1914-07-15부터 유효한 기록은 `1914-07` 화면에 보이지만,
1914-07-15에 종료된 기록은 `1914-07` 월말 화면에는 보이지 않는다.

## 4. temporal 값 정밀도

프로젝트의 temporal endpoint는 다음 세 정밀도를 지원한다.

| 정밀도 | canonical 예 | 시작 경계 | 종료 경계 |
| --- | --- | --- | --- |
| year | `1914` | 1914-01-01 | 1914-12-31 |
| month | `1914-07` | 1914-07-01 | 1914-07-31 |
| date | `1914-07-15` | 1914-07-15 | 1914-07-15 |

따라서 웹 `temporal.js`와 앱 `temporal.cpp`는 T1에서 `month` precision을
같은 canonical 값과 같은 boundary로 지원해야 한다.

타임라인 커서는 month precision만 사용하지만, 자료의 `validFrom/validTo`는
year/month/date precision을 모두 사용할 수 있다.

## 5. 유효기간

모든 유효기간은 **양끝 포함(inclusive)** 이다.

- `validFrom = null`: 과거 방향 제한 없음
- `validTo = null`: 미래 방향 제한 없음
- 둘 다 `null`: 모든 시점에서 유효
- 시작 경계가 종료 경계보다 늦으면 오류

월 precision endpoint는 해당 달 전체를 뜻한다.

예:

- `validFrom: 1914-07, validTo: 1914-07`
  - 1914년 7월 전체에 유효
  - `1914-07` 타임라인에서 보임
- `validTo: 1914-07-15`
  - 1914-07-15까지 유효
  - `1914-07` 월말 타임라인에서는 보이지 않음
- `validFrom: 1914-07-15`
  - 1914-07-15부터 유효
  - `1914-07` 월말 타임라인에서는 보임

## 6. 객체 정체성

시간에 따라 형상, 관계, 속성이 달라져도 같은 역사적 객체는 **같은 논리 ID**를
유지한다.

- 시점 변경 때문에 객체 ID를 교체하지 않는다.
- renderer용 임시 ID, geometry version ID, GIS source ID를 논리 ID와 혼용하지 않는다.
- 시간별 형상과 관계는 후속 단계에서 versioned record로 표현하되, 객체의 정체성을
  복제해 해결하지 않는다.

웹의 `TerritorialEntity`와 앱의 territorial object 표현 차이는 플랫폼 내부 구현
차이다. 타임라인 계약상 동일 객체를 가리키는 논리 ID의 의미는 같아야 한다.

## 7. 결정적 resolver

특정 커서 월을 해석하면 같은 프로젝트 입력에서 웹과 앱이 동일한 유효 객체,
형상 version, 관계 graph를 얻어야 한다.

resolver는 다음 원칙을 따른다.

1. 커서 월을 그 달의 마지막 날 reference point로 변환한다.
2. 해당 reference point를 포함하는 기록만 유효 후보로 본다.
3. 같은 의미 슬롯에 동시에 둘 이상의 기록이 유효하면 프로젝트 오류다.
4. `last write wins`, 배열 순서 우선, 플랫폼별 임의 우선순위를 사용하지 않는다.
5. 부모 관계는 해당 시점의 유효 graph 전체에서 존재성, 종류 규칙, 자기 참조,
   순환을 검증한다.
6. renderer는 resolver 결과를 표시할 뿐 canonical project data를 수정하지 않는다.

구체적인 record schema와 resolver API는 T2/T3에서 정한다.

## 8. 타임라인 편집 경계

월 단위 타임라인에서 사용자가 `1914-07`을 보고 변경을 확정하면,
그 변경의 월 단위 효력 시작은 `1914-07`이다.

월 단위 기록을 분기하는 경우 기본 경계는 다음과 같다.

- 기존 기록: `... ~ 1914-06`
- 새 기록: `1914-07 ~ ...`

즉 같은 달을 이전/이후 기록에 중복 포함시키지 않는다.

정확한 일자 precision 원본이 있는 경우 이를 무손실로 유지하는 방법과 실제
split 명령의 원자성은 T4에서 구현한다. T0에서 임의로 일자를 반올림하거나
원본 endpoint를 덮어쓰지 않는다.

## 9. 상태 소유권

타임라인 커서 자체는 **프로젝트 콘텐츠가 아니라 view/presentation 상태**다.

커서 이동은:

- 프로젝트 콘텐츠를 dirty로 만들지 않는다.
- Undo/Redo 항목을 만들지 않는다.
- territorial geometry/relation을 수정하지 않는다.
- resolved snapshot, selection presentation, renderer cache는 필요한 범위에서
  다시 계산하거나 무효화할 수 있다.

반대로 특정 시점에서 사용자가 실제 편집을 확정한 행위는 프로젝트 콘텐츠 변경이며
기존 command/transaction/history 경계를 사용한다.

## 10. 웹/앱 동등성

동일한 fixture와 동일한 `YYYY-MM` 커서에 대해 최소 다음 값은 웹과 앱에서
동일해야 한다.

- visible logical entity IDs
- 각 entity가 참조하는 effective geometry version
- effective parent relation
- effective root/ancestor graph
- temporal interval 포함 여부
- 충돌/순환/역전 interval에 대한 성공·실패 판정

플랫폼별 UI 표현, 렌더링 API, 언어별 내부 자료구조는 달라도 된다.

## 11. 호환성 정책

현재 개발 저장 형식에 대한 별도 legacy reader, 이중 읽기/쓰기, fallback field,
구형 timeline migration을 추가하지 않는다.

후속 단계에서 timeline wire schema를 도입할 때 현재 canonical 형식을 한 번에
전환하고 현재 호출부와 fixture를 함께 갱신한다. 구형 개발 프로젝트를 유지하기
위한 병행 구현을 만들지 않는다.

## 12. T0에서 하지 않는 일

T0에서는 다음을 구현하지 않는다.

- 타임라인 UI
- `ResolvedWorld` 또는 동등 resolver
- geometry/relation history 저장 schema
- 프로젝트 schema version bump
- renderer 연결
- 시점 편집 split command
- 자동 migration

다음 단계 T1은 웹 `assets/js/modules/temporal.js`와 앱
`core/src/temporal.cpp` / `core/include/pandoeditor/temporal.h`의 month precision
동등 구현과 집중 테스트다.
