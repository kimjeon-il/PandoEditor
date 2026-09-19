# PandoEditor M3.2 — 웹 원본 동등성 기반 속성·잠금 편집 설계 및 구현계획

> 상태: 설계 제안. M3.2 제품 코드는 작성·변경하지 않았으며 구현 테스트 통과를 주장하지 않는다.
> 후속 구현은 `superpowers:executing-plans` 또는 사용 가능한 독립 검토 실행 체계로 작업별 수행한다.

**Goal:** 국가·하위단위·지방의 기본 속성, 색상 편집 및 객체 잠금을 웹판의 실제 사용자 조작·정규화·이력 결과에 맞춰 Qt 공통 경로로 연결한다.

**Architecture:** M1의 불변 문서·CommandProcessor·ChangeSet을 유지하고, 객체/필드별 요청과 읽기 모델을 추가한다. 선택·초안·색상창은 세션 상태, 명시 속성·잠금·기간은 문서 상태로 구분한다. 원본의 사용자 관찰 가능한 예외는 명령별 호환성 정책으로 제한적으로 표현한다.

**Tech Stack:** 기존 C++17 / Qt 6.5 이상 프로젝트, 검증 기준 Qt 6.8.3 / Qt Quick·Qt Test / CMake·CTest. 원본 JavaScript 함수 비교는 Node.js를 사용한다.

**Spec:** 이 문서의 §1–§8이 M3.2 기능 계약이다. 상위 요구는 ‘웹사이트 기능을 변형하지 않고 그대로 가져오기’와 M3.1의 선택·문서 편집 분리 계약이다. 예전 계획의 추정과 현재 원본이 충돌하면 차이를 기록하고 현재 원본을 따른다.

## 0. 기준 소스와 이번 확인 범위

- 웹 기준: `kimjeon-il/world-map@58e4087f85aa51884bc4ab80959e05010d94d7d5`.
- Qt 기준: `codex/m3-1-selection@1aeb400e9738b518deef1c5c4806bfc76ad6a40a` **+ 완성된 M3.1 QML 패치**.
- 완성 소스: `PandoEditor-M3.1-QML-source.zip`.
- 위 ZIP SHA-256: `c99bf6fe354fd628ba98f106fccf96546f634f15f0e7b4cfd540564e9d1c8cba`.
- 원격 기준 커밋만으로 시작하면 검색·겹침 선택창 등 완성된 QML 작업이 빠진다. 작업 시작 시 ZIP/패치 반영 여부를 먼저 대조한다.
- 이번에는 원본 코드 대조와 원본 함수에 fixture를 주입한 소규모 동작 확인만 수행했다. 전체 웹 브라우저 UI 검증, M3.2 앱 구현·빌드·PC/Android 검증은 수행하지 않았다.
- GitHub 쓰기·커밋·병합·배포는 이번 계획 작성의 범위가 아니다.

## Global Constraints

1. 필드 이름뿐 아니라 진입 위치, 확정 시점, 기본값/상속, 실패 시 화면 반응, Undo 단위를 대조한다.
2. 현재 Qt 임시 입력폼을 그대로 세 객체에 확대하지 않는다. 웹 원본에 없는 일괄 이름·메모·날짜 편집을 추가하지 않는다.
3. 국가·하위단위·지방을 한 문서 모델과 공통 명령 경로로 처리한다. PC/모바일에 별도 문서 편집 구현을 두지 않는다.
4. 순수 선택·검색·hover·카메라 조작은 문서·문서 revision·기존 Undo/Redo·저장 내용에 영향을 주지 않는다.
5. 실패·취소·오래된 결과는 원자적으로 거절한다. 다른 객체에 초안이 적용되는 경우를 허용하지 않는다.
6. 명시값과 표시값을 구분한다. 같은 색으로 보이더라도 ‘상속’에서 ‘명시’로 바뀌면 실제 문서 변경이다.
7. 원본에서 특정 동작이 동일 문서에도 이력을 만드는 경우 일반 NoOp 최적화로 없애지 않는다. §5의 명령별 예외에 한한다.
8. 미해석 데이터 보호는 원본의 객체 잠금과 별개다. 잠금 호환성 예외가 보존 보호나 프로젝트 읽기 전용 상태를 우회하면 안 된다.
9. 실제 도형 변경, 객체 생성/삭제/종류 전환, 부모·소속국 변경은 이 단계에 혼합하지 않는다.
10. 원본을 개선하거나 명백해 보이는 불일치를 수정하려면 이식과 별도의 변경으로 기록한다.

## Review Focus

- 상속색과 같은 색을 직접 지정한 뒤 부모색 변경: 표시가 같았다는 이유로 명시 설정을 잃지 않아야 한다.
- A의 메모/색상창을 열고 B 또는 다중 선택으로 전환: 늦은 입력·스포이트 결과가 B에 저장되면 안 된다.
- 잠긴 객체가 포함된 다중 색상 편집: 단일 편집의 잠금 규칙을 무조건 재사용하면 원본과 달라진다.
- 같은 색을 반복 선택한 상태의 Redo: 원본의 이력 생성과 Redo 분기 제거를 비교해야 한다.
- 이전 v3에 이미 평탄화된 상속 정보: 원본 archive를 다시 덮어씌워 최근 편집을 되돌리면 안 된다.

## 1. 사용자 화면과 대상 범위

### 1.1 원본 화면 구조

`index.html`의 `selectionToolbar`를 기본 속성 진입점으로 삼는다.

- 국가: `국명` 입력, 색상 버튼, 메모 팝업.
- 하위단위: `하위단위명` 입력, 색상 버튼, 메모 팝업.
- 지방: `지방명` 입력, 색상 버튼, 메모 팝업.
- 편집창은 별도의 열기/닫기 동작으로 다룬다. 단순 선택을 폼 전체 확정이나 편집창 자동 개방과 결합하지 않는다.
- 객체 잠금은 편집창의 공통 객체 명령이다. Qt 사용자 레이어 잠금으로 대신하지 않는다.
- 여러 객체 선택 시 단일 객체 툴바 대신 원본 `multiProperties`의 공통 작업을 사용한다.
- 모바일 시트에 툴바가 가려질 때에는 원본의 비활성·팝업 닫기 동작도 대조한다. 가려진 툴바에 터치가 전달되면 안 된다.
- 색상창·메모·검색·겹침 후보창의 닫기 우선순위는 M3.1의 Back/Escape 흐름과 결합한다.

국기·수도·지명·형상 조작 버튼의 존재와 해당 기능의 이식 완료 여부는 별개다. 아직 구현하지 않은 버튼을 가짜 동작으로 대체하지 않는다.

### 1.2 객체별 기능표

| 항목 | 국가 | 하위단위 | 지방 | M3.2 처리 |
|---|---|---|---|---|
| 이름 | 국명 | 하위단위명 | 지방명 | 독립적인 change 확정 |
| 메모 | 메모 팝업 | 메모 팝업 | 메모 팝업 | 객체별 정규화와 확정 동작 대조 |
| 색상 | 명시색 / 기본색 | 명시색 / 상속 | 명시색 / 기본값(해석은 관계 기반) | 원본 팔레트·사용자 지정·초기화 |
| 유효기간 시작/종료 | 현재 편집 필드 없음 | 현재 편집 필드 없음 | 있음 | 지방만 입력 연결; 다른 종류의 저장된 기간은 보존 |
| 객체 잠금 | 있음 | 있음 | 있음 | 공통 명령; 선택 해제·자식 일괄 잠금과 구분 |
| 소속 국가 | 해당 없음 | 있음 | 있음 | 관계 변경은 M3.3에 유지 |
| 상위 단위 | 해당 없음 | 조건부 선택 | 기존 관계 표시용, 비활성 | 지방을 새 계층 편집 대상으로 만들지 않음 |
| 국가별 불투명도·자유 경계 굵기 | 원본 기본 속성 툴바 항목 아님 | 동일 | 동일 | Qt 임시 입력을 공통 폼에 확대하지 않음 |

표시/숨김과 객체 순서 전체는 M3.4 범위다. M3.2는 기존 선택 가능 여부·숨김 표시를 보존하며, 전체 가시성 기능까지 완료로 표시하지 않는다.

## 2. 이름·메모·기간 계약

### 2.1 이름

- 원본 change 이벤트가 전달하는 앞뒤 공백 제거를 이식한다. 숫자·한글·문장부호에 새로운 제한을 추가하지 않는다.
- 국가 원본명과 사용자 이름 override를 분리한다. 표시용 국명은 원본의 override → 기본명 → ID fallback을 따른다.
- 국가 이름을 빈 값으로 돌리는 동작을 현재 Qt의 ‘빈 이름 오류’로 일괄 거절하지 않는다.
- 하위단위·지방의 빈 저장명과 `이름 없는 하위단위` / `이름 없는 지방` 표시용 fallback을 구분한다. fallback 문자열을 자동으로 저장명에 써 넣지 않는다.
- 원본의 이름 충돌 표시는 같은 종류·같은 소속국 안의 trim 및 한국어 소문자 비교다. 중복 경고를 UNIQUE 제약으로 바꾸지 않는다.
- 이름 변경 후 지도 선택 문맥·검색 결과·읽기용 목록을 갱신한다. 도형이나 부모 관계를 재작성하지 않는다.

### 2.2 메모

- 국가 `notes` change는 입력값을 그대로 commit 경로에 전달한다.
- 하위단위·지방은 `normalizeTerritorialUnits`의 문자열 정규화에서 앞뒤 공백이 제거된다. 내부 줄바꿈은 유지한다.
- 웹 저장 경로의 `pruneCountryOverrides`는 별도로 문자열을 정리한다. 따라서 ‘필드 확정 직후’와 ‘저장 후 재열기’ 결과를 각각 원본과 비교한다.
- 이름과 메모를 하나의 폼으로 일괄 확정하지 않는다.
- 단순 선택 이동이 초안 확정이 되지 않도록 M3.1의 원래 객체별 초안 소유권을 유지한다.

### 2.3 지방 유효기간

- 시작일·종료일을 각각 change 단위로 확정한다. 둘을 새 Apply 버튼 아래 하나로 합치지 않는다.
- `YYYY`와 `YYYY-MM-DD`, 부호 있는 확장 연도, BCE, 윤년을 원본 `temporal.js`와 비교한다.
- 빈 값은 열려 있는 경계다. 현재 날짜나 임의의 시작 연도로 대체하지 않는다.
- 연도 0, 실제 존재하지 않는 날짜, 시작이 종료보다 늦은 기간은 거절한다.
- 부모·소속국·기간별 관계나 참조 객체의 기간을 자동 수정해서 통과시키지 않는다.
- 미해석 데이터에 기간 의존성이 있으면 필요한 범위를 검증하고, 안전성을 판단하지 못하면 원자적으로 거절한다.

### 2.4 이벤트와 오류

| 이벤트 | 계획된 처리 |
|---|---|
| 문자 입력 중 | 해당 객체·필드의 초안만 갱신 |
| 원본과 동등한 이름/메모 change | 그 필드 하나만 요청 |
| 선택·검색·hover·지도 이동 | 문서 편집 없음; 원래 대상의 초안 보존 |
| 이름에서 메모로 정상적인 필드 이동 | 이름만 확정; 메모와 색은 건드리지 않음 |
| 한글 조합 중 Enter | IME 확정과 필드 확정을 구분; 중복 commit 금지 |
| 검증 실패 | 문서/이력 무변경; 화면의 입력 유지/기존값 복귀는 원본 필드 경로대로 |
| 파일 재열기/대상 삭제/프로젝트 교체 | 이전 세션의 결과 적용 금지 |

‘모든 실패에서 입력 유지’도 웹 원본보다 우선하는 일괄 UI 규칙으로 사용하지 않는다. 예컨대 지방 메타데이터 거절 경로는 원본 속성을 다시 표시한다. M3.1의 **선택만으로** 초안을 잃지 않는 계약과, 실제 편집 검증 실패의 원본 화면 반응은 분리한다.

## 3. 색상 편집 계약

### 3.1 팔레트

- `app-environment.js`의 색상·명칭과 `paletteColorsByTone()` 순서를 그대로 가져온다. Qt의 임시 8색 견본으로 대체하지 않는다.
- 원본 팔레트는 명도 행마다 중성색과 색상 계열을 배치한다. 값·순서·선택 체크·키보드 접근을 fixture로 고정한다.
- 팔레트 색 클릭은 즉시 그 색상 명령을 한 번 확정한 뒤 닫는다.
- 국가 ‘기본 색상’, 하위단위 ‘국가색 상속’, 지방 ‘기본 색상’ 문구와 역할을 그대로 둔다.
- 다중 선택 팔레트에는 원본에 없는 ‘모두 상속으로 되돌리기’를 추가하지 않는다.

### 3.2 사용자 지정

원본 `custom-color-control.js`의 채도·명도 평면, 색조 슬라이더, HEX, RGB/HSL 전환, 각 숫자 필드, 미리보기, 적용·취소를 포팅한다.

- HEX는 `#` 유무와 3자리/6자리 처리, 소문자 저장 및 대문자 표시를 구분한다.
- RGB는 0–255, HSL은 H 0–360 및 S/L 0–100 범위를 원본과 대조한다.
- 색 공간 변환의 반올림과 회색에서 이전 hue 유지, HSL 입력 중 다른 채널 값이 재계산으로 튀지 않는 동작을 보존한다.
- 평면 방향키 1% 및 Shift+방향키 10% 동작을 유지한다.
- 유효하지 않은 값은 적용 비활성 및 원본 메시지를 사용한다. 입력 중 잘못된 문자열을 즉시 기본색으로 확정하지 않는다.
- 창 내부 조작은 문서가 아니라 색상창 초안만 바꾼다. 실제 적용 버튼/해당 Enter 동작에서 한 번 요청한다.
- 취소·Escape·바깥 닫기·선택 대상 전환은 적용하지 않는다. 색상창의 Enter/Escape가 지도 명령이나 상위 창 닫기로 중복 전달되지 않게 한다.
- 스포이트는 실제 화면 추출 어댑터를 사용한다. 지원 환경에서는 같은 역할을 제공하고, 지원 불가 환경은 원본의 capability 기반 숨김과 구분해 기록한다. 지도 내부 색상 읽기를 화면 스포이트라고 바꾸지 않는다.
- 색상창 종료 시 스포이트 요청을 취소하고 늦은 결과를 무시한다. OS별 실제 지원 검증 없이 기능 완료를 주장하지 않는다.

### 3.3 명시값과 상속

문서의 색상 상태는 최소한 다음 두 가지를 구별해야 한다.

```text
Automatic            명시 색 없음; 종류와 관계에 따라 기본색/상속을 해석
Explicit(#rrggbb)    사용자 명시 색; 현재 상속색과 같더라도 별도 상태
```

- 국가 Automatic은 현재 테마의 기본 육지색을 따른다. 테마는 프로젝트 색상 override가 아니다.
- 하위단위/지방은 자기 명시색 → 하위단위 부모 체인 → 부모 국가 또는 소속국 색 → 원본 fallback 순서를 원본 resolver 그대로 구현한다.
- 부모가 하위단위인 경우와 지방인 경우를 동일하게 처리하지 않는다.
- 초기화는 명시색 제거다. 부모의 현재 RGB를 복사해 고정하지 않는다.
- 부모색 변경으로 상속 자식의 표시가 달라지는 것은 파생 표시 변경이다. 자식마다 새 색상 값·Undo를 만들지 않는다.
- 잠긴 자식도 명시 값을 직접 바꾸지 않은 채 상속 표시가 달라질 수 있다. 이를 자식 편집으로 오인해 부모의 정상 색상 변경을 막지 않는다.
- 실제 관계 변경은 M3.3에 남긴다. M3.2는 현재 관계의 색상 해석만 필요하다.

예: 부모가 빨강인 상태에서 자식에게 빨강을 **직접 지정**했다면, 나중에 부모가 파랑이 되어도 자식은 빨강이다. 자식을 상속 상태로 초기화한 뒤에는 파랑을 따른다. 이 네 단계와 각 Undo를 테스트한다.

## 4. 객체 잠금과 다중 선택

### 4.1 잠금의 차원

- `objectLocked`: 해당 객체의 직접 잠금.
- `legacyUserLayerLocked`: Qt 구버전 사용자 레이어의 호환성 잠금. 웹 객체 잠금과 별도.
- `projectBusy/readOnly`: 프로젝트 교체·처리 중·선택이 아닌 도구·활성 형상 초안 등의 원본 UI 차단 조건.
- `preservationBlocked(effect, target)`: 미해석 데이터 보호. 위 잠금 예외와 독립.

선택 가능, 메모 열람 가능, 필드 편집 가능, 잠금 해제 가능을 하나의 `selectedEditable`로 판단하지 않는다.

### 4.2 단일 객체

- 잠긴 객체도 선택·검색·위치 이동 가능.
- 이름·색상 트리거는 원본 툴바처럼 비활성. 메모 팝업은 읽기 전용으로 볼 수 있다.
- 잠금 해제 명령 자체를 객체 잠금 검사로 막지 않는다.
- 부모 잠금을 모든 자식의 직접 잠금으로 복제하지 않는다.
- 잠금 변경은 국가의 경우 원본 override의 true/필드 제거 의미를, 하위단위·지방은 직접 잠금 값을 현재 모델과 매핑한다.
- 기존 Qt 사용자 레이어가 잠겨 있으면 객체 잠금을 풀었다는 이유로 그 레이어의 별도 보호를 자동 해제하지 않는다.
- 지방의 기간 필드는 원본이 실제로 비활성화하는지와 commit 시 거절하는지를 별도로 대조한다. 이름 필드의 enabled 조건을 복제해 UI를 임의 변경하지 않는다.

### 4.3 다중 잠금

| 선택 상태 | 버튼 의미 | 결과 |
|---|---|---|
| 모두 잠김 | 잠금 해제 | 선택된 객체 모두 해제 |
| 모두 해제 | 모두 잠금 | 모두 잠금 |
| 일부만 잠김 | 모두 잠금 | 모두 잠금 |

각 객체의 bool을 하나씩 뒤집는 동작이 아니다. 한 번의 명령과 하나의 Undo이며 선택 순서·primary·anchor는 유지한다. 명시적으로 요청한 잠금 값이 이미 전부 같으면 원본처럼 이력 없이 반환한다.

### 4.4 다중 속성 범위

국가·하위단위·지방 조합의 공통 색상과 잠금만 이번에 새로 연결한다. 표시/숨김은 M3.4와 구분한다. 다중 이름·메모·기간·소속국 편집이나 새로운 혼합값 편집 폼을 추가하지 않는다.

다중 색상 버튼은 원본에서 마지막 입력/기본값을 쓰는 공통 색상 명령이다. 단순히 여러 색이 섞였다고 새 ‘혼합 색상’ 입력 규칙을 만들어 문서를 덮어쓰지 않는다.

## 5. 기존 M1 가정과 다른 원본 동작: 숨기지 않고 명시

### 5.1 잠긴 대상의 일괄 색상

원본 단일 툴바는 객체 잠금으로 막히지만, `commonBatchCapabilities()`와 `batchSetColor()`에는 같은 잠금 필터가 없다. 따라서 현재 원본의 일괄 색상 명령은 잠긴 대상도 변경한다.

**설계 결정:** 웹에서 접근 가능한 일괄 색상 경로를 그대로 이식한다. 단일 필드 명령의 잠금 차단을 제거하는 것이 아니라, 등록된 `territorial.batch-color`의 객체 잠금 정책만 원본에 맞춘다. 클라이언트 args에 임의 `ignoreLocks`를 추가하지 않는다. 미해석 보호, 프로젝트 교체 상태, Qt 사용자 레이어의 별도 호환 보호는 우회하지 않는다.

UI에서 해당 경로가 활성화되는 조건은 실제 DOM/선택 조합 fixture로 추가 검증한다. 여기서 확인한 함수 결과를 모든 브라우저 화면의 실기기 검증으로 확대하지 않는다.

### 5.2 동일 색상 반복과 Undo

원본 `batchSetColor()`는 실제 값 비교 전에 `recordHistory()`를 호출한다. `history-service.js`의 record는 스냅샷을 추가하고 기존 future를 지운다. 원본 함수 실행에서도 같은 색 반복 시 문서 값은 같지만 이력이 추가되고 Redo가 지워졌다.

**설계 결정:** 일반 명령의 semantic NoOp는 유지하되, 이 웹 명령만 ‘동일 문서의 명시적 이력 체크포인트’를 표현한다. `historyEffect=RecordCheckpoint`는 등록된 명령 정의에서 결정하고 외부 요청값으로 선택하지 못한다. before/after가 같은 ChangeSet도 이 경로에서는 의미 있는 사용자 이력 동작이다.

- 취소·실패·stale에는 체크포인트를 남기지 않는다.
- native revision은 유효한 체크포인트 확정/Undo/Redo에서도 증가하여 기존 미리보기를 무효화한다. 웹의 내부 revision 정수와 값 자체가 같아야 한다는 요구는 아니다.
- 웹의 사용자 미저장 표시/저장 동작은 별도 관찰 항목이다. 순수 문서 비교 dirty만으로 동일색상 재확정의 저장 상태를 조용히 지워서는 안 된다. 필요 시 이력에 기록되는 edit-content token과 저장 기준 token을 도입하며 선택·검색은 token을 바꾸지 않는다.
- 따라서 예전 ‘원본과 같으면 항상 NoOp’ 문장은 이 명령에 그대로 적용하지 않는다. 최신의 ‘원본 동작 그대로’ 요구를 따른 명시적 계약 조정이다.

### 5.3 이름/메모 오류 표시

빈 이름 일괄 거절, 중복 이름 금지, 모든 메모 동일 trim, 모든 오류 초안 유지 역시 일반 규칙으로 강제하지 않는다. 원본 필드 처리 및 저장/재열기 결과를 각각 fixture로 고정한다.

## 6. 문서·저장 설계

### 6.1 필요한 표현

현재 `ObjectStyle`은 최종 RGB와 opacity만 가진다. M3.2에서는 아래 의미를 문서가 표현해야 한다.

- 국가 기본명과 명칭 override, 비국가 객체의 원시 이름.
- 명시색 존재 여부와 RGB. 해석된 effective color는 읽기 전용 파생값.
- 객체 직접 잠금.
- 지방의 nullable 시작/종료일과 원본 precision.
- 해석에 필요한 기본 관계. 관계 편집 명령은 추가하지 않는다.

`DocumentState`의 파생 읽기 캐시에 표시 이름·색·상속 출처를 만들고 기존 `CountryView`를 그 읽기 결과의 호환 어댑터로 유지한다. 화면과 저장소가 각각 독립 수정 가능한 명칭/색상을 갖는 이중 원본은 만들지 않는다.

### 6.2 파일 형식 기본안: Qt v4

**이 계획에서는 새 명시/상속 상태를 안전하게 저장하기 위해 Qt v4 출력을 기본안으로 선택한다.** 이전 계획의 ‘v3 선택 필드 확장’은 호환성 입증 조건부였으며, 이전 reader가 상속을 모른 채 색상 편집을 허용할 수 있다는 문제가 있으므로 조용한 의미 손실보다 명시적인 버전 경계를 택한다.

- 기존 Qt v1/v2/v3 읽기는 유지한다.
- 열기만으로 원본 파일을 덮어쓰지 않는다. 사용자가 저장할 때 새 형식으로 쓴다.
- 새 웹 schema 3/4/5 가져오기에서는 명시색 존재 여부·원래 국명·기간을 처음부터 보존한다.
- 이미 v3로 평탄화된 파일은 확인 가능한 원시 데이터만 활용한다. 최근 native 편집을 source archive 값으로 복구해 덮어쓰지 않는다.
- 명시/상속 의도를 복원할 증거가 부족한 기존 v3 색은 현재 보이는 색을 명시값으로 보존하고 이 제한을 마이그레이션 보고서에 알린다. 없어진 과거 의도를 추측해 ‘완전 복원’했다고 표시하지 않는다.
- 과거 국가 기본명은 검증된 불변 출처가 있으면 그 출처를 fallback로 보존할 수 있지만, 현재 이름 override는 현재 문서를 우선한다.
- 이전 앱의 새 형식 거절과 새 앱의 이전 형식 읽기를 테스트한다. 웹 저장본 schema 번호를 임의로 바꾸는 작업은 아니다.

### 6.3 보존 데이터

- M2 `migrationArchive`는 과거 원본이며 현재 편집 상태를 덮어쓰는 원본이 아니다.
- `/properties/style/color` 등 이번에 해석한 값만 활성 모델로 옮긴다. style 안의 알 수 없는 항목은 계속 별도 보존한다.
- 원시 JSON 처리에는 기존 lossless 경로를 사용한다. 큰 정수·null·필드 미존재·배열 순서를 일반 JSON double 변환으로 훼손하지 않는다.
- 원본 archive와 다른 미지원 국기·수도·분포·수계·지명 데이터는 이름·색상·잠금 변경 때문에 삭제되지 않아야 한다.
- 저장 전에 canonical 상태를 재검증한다. 원본보다 더 엄격한 기존 Qt 검증 때문에 정상 웹 입력을 거절하는 경우를 ‘예상된 통과’로 처리하지 말고 동등성 결함으로 기록한다.

## 7. 명령과 초안 인터페이스

### 7.1 요청 종류

기존 `CommandRequest`의 식별자·문서·revision 계약은 유지하고 args에 다음 명령별 데이터를 추가한다.

| commandId(제안) | 입력 | 잠금/이력 |
|---|---|---|
| `territorial.field` | 대상 1개, field(name/notes/validFrom/validTo), 값 또는 경계 clear | 단일 필드 정책; 실제 변경 1 ChangeSet |
| `territorial.color` | 대상 1개, 명시 RGB | 단일 편집 정책 |
| `territorial.color.reset` | 대상 1개 | 명시색 제거; 이미 Automatic이면 NoOp |
| `territorial.batch-color` | 고정된 대상 목록, RGB | 원본 일괄 정책; 명시적 체크포인트 가능 |
| `territorial.lock` | 고정된 대상 목록, 최종 bool | unlock 자체 허용; 이미 같으면 NoOp |

다음 의미를 혼동하지 않는 typed args를 사용한다.

```cpp
// 인터페이스 설계. 이 문서 작성에서 제품 코드에 추가한 선언은 아니다.
enum class ColorMode { Automatic, Explicit };
struct ColorValue { ColorMode mode; std::uint32_t rgb = 0; };
enum class BoundaryOperation { Keep, Set, Clear };
struct BoundaryPatch { BoundaryOperation operation; std::string value; };
struct TerritorialFieldPatch {
    ObjectRef target;
    std::optional<std::string> name;
    std::optional<std::string> notes;
    BoundaryPatch validFrom{BoundaryOperation::Keep, {}};
    BoundaryPatch validTo{BoundaryOperation::Keep, {}};
};
struct SetTerritorialColor { std::vector<ObjectRef> targets; ColorValue value; };
struct SetTerritorialLocked { std::vector<ObjectRef> targets; bool value; };
```

field 명령은 실제로 바뀐 필드 하나를 보낸다. 사용자 지정 색상을 적용한다고 이름·메모·다른 레이어 초안을 모아 보내지 않는다. color의 Automatic 상태에서 rgb는 비교·저장의 의미값이 아니며 명시값으로 취급하지 않는다.

### 7.2 읽기 모델·초안

- `ObjectPropertyView`: ref/kind, 저장값과 표시값, 이름 경고, 명시색/유효색/상속 여부, 잠금 상태와 필드별 가용성, read-only 관계 요약.
- `ObjectEditPolicy`: 단일 필드, color reset, batch color, lock 각각의 허용/거절 사유. 원본 기능 권한과 미해석 보호를 구분한다.
- `EditorDraftStore`: `{projectInstanceId, ObjectRef, field}`를 키로 사용한다. 이름·메모·기간 초안과 색상창 초안을 구분한다.
- `ColorEditSession`: 시작 시 프로젝트 인스턴스·documentId·revision·대상 ref 목록·세션 ID를 캡처한다. 적용 시 재확인하고, 대상 변경 시 원본처럼 창을 닫는다.
- UI 표시·검색·스타일 갱신은 성공한 명령 이후 한 번 publish한다. 도형 GeometryRef 및 좌표 배열은 보존한다.
- 기존 국가 API는 새 명령을 호출하는 호환 래퍼다. 별도 CountryChange 이력이나 국가 전용 수정 원본을 되살리지 않는다.

## 8. 제외 범위와 연동 경계

| 항목 | 다음 단계/정책 |
|---|---|
| 부모·소속국 변경 | M3.3. 지방의 기존 parent는 원본에서도 read-only라는 점 유지 |
| 국가↔하위단위 종류 전환, 실제 생성·삭제 | M3.3/M4. 단순 kind 변경으로 흉내 내지 않음 |
| 전체 가시성·순서·공유 경계 강조 | M3.4/M3.5 |
| 국기·수도·지명·수계·분포 편집 | M5. 데이터 보존은 계속 |
| 역사 시점별 관계 편집·라이브러리 | M6. 저장된 기본/기간 관계는 훼손하지 않음 |
| 전체 지구 투영·대규모 렌더링 | M7 |

색상 상속의 읽기 해석은 M3.2를 완결하기 위한 선행 부분으로 M3.4에서 앞당긴다. 이를 이유로 M3.4 전체 완료로 표시하지 않는다. 국기·관련 버튼을 구현하지 않은 상태를 ‘전체 웹 속성 화면 완성’이라고 표현하지 않는다.

## 9. 작업 순서와 수정 파일

### Task 1 — 원본 실행 계약 고정

**Create:** `tests/fixtures/web-properties/`, `tools/m32-property-oracle.mjs`, `tests/property_parity_probe.cpp`.
**Sources:** §11의 원본 파일. Qt 기대값으로 정답을 역생성하지 않는다.
**Produces:** 입력 이벤트 → 필드 상태/표시값/기록 수/Redo/오류/창 상태의 fixture.

- [ ] 단일/다중, 잠금 조합, 같은 색 반복, 이름 공백, 기간 오류의 원본 실행 fixture 작성.
- [ ] 서비스 호출과 실제 DOM 사용자 경로의 차이를 분리; 도달 불가능한 내부 함수만 보고 UI 기능을 추가하지 않음.
- [ ] 저장 전/후 결과 fixture 분리. 색상 비교 시 명시 여부 포함.
- [ ] 새 Qt probe가 현재 미구현 상태에서 기대한 불일치로 실패하는 것을 확인.

### Task 2 — 명칭·색상 상태와 v4 저장/마이그레이션

**Modify:** `core/include/pandoeditor/document.h`, `core/src/document.cpp`, `core/src/documentstate.h`, `app/projectcodec.cpp`, `app/webimport.cpp`, `core/src/commands.cpp`의 의미 동등성.
**Create:** `core/include/pandoeditor/objectproperties.h`, `core/src/objectproperties.cpp`, `tests/object_properties_tests.cpp`, `tests/property_migration_tests.cpp`.
**Consumes:** 원본 명칭/색상/날짜 fixture.
**Produces:** 원시 속성과 effective 표시값의 단일 읽기 모델; Qt v4 왕복.

- [ ] Explicit와 Automatic이 같은 RGB로 보여도 다르다는 실패 테스트.
- [ ] 이름 fallback, 빈 비국가 이름, v1/v2/v3 업그레이드 및 최근 편집 보존 테스트.
- [ ] 관계 기반 색상 resolver와 immutable 파생 읽기 캐시 구현.
- [ ] v4 codec·웹 importer·구버전 읽기 연결; 보존된 원시 JSON 무손실 확인.
- [ ] 기존 sample·migration·M2 가져오기 회귀 실행.

### Task 3 — 필드별 속성/기간 명령

**Modify:** `core/include/pandoeditor/commands.h`, `core/src/commands.cpp`, `app/editorfields.cpp`, `app/editorcontroller.h`, `app/editorcontroller.cpp`, `app/editorcommands.cpp`.
**Create:** `app/editorproperties.cpp`, `tests/property_command_tests.cpp`, `tests/property_editor_tests.cpp`.
**Consumes:** Task 2 읽기 모델/저장 계약.
**Produces:** `territorial.field` 및 필드별 정책·오류·독립 Undo.

- [ ] S/R에서 변경이 현재 거절되는 실패 테스트.
- [ ] 이름·메모·기간 정규화와 오류 화면 disposition을 원본 결과와 대조.
- [ ] 이름 한 필드 확정이 다른 필드/레이어 초안을 모으지 않도록 연결.
- [ ] 성공·실패·취소·stale에서 문서·이력·선택·GeometryRef 확인.
- [ ] 기존 국가 API를 새 경로 래퍼로 전환하고 M1/M2/M3.1 회귀 실행.

### Task 4 — 객체 잠금·일괄 정책·이력 체크포인트

**Modify:** `core/src/commands.cpp`, `core/src/project.cpp`, `core/include/pandoeditor/project.h`, `app/editorproperties.cpp`, 필요한 저장 상태 어댑터.
**Create:** `core/include/pandoeditor/objecteditpolicy.h`, `core/src/objecteditpolicy.cpp`, `tests/property_history_tests.cpp`.
**Consumes:** Task 1 잠금/일괄 원본 fixture와 Task 3 명령 경로.
**Produces:** 명령별 허용 정책과 원본 일괄 Undo/Redo 경계.

- [ ] mixed lock → all locked → all unlocked의 각 Undo/Redo 실패 테스트.
- [ ] 원본 batch color의 locked 대상과 동일값 이력/Redo 제거 fixture를 Qt에서 재현.
- [ ] 명령 등록부가 이력 체크포인트 예외를 소유하도록 구현; args에 범용 우회 flag 금지.
- [ ] 보존 보호·Qt 레이어 잠금·프로젝트 차단이 객체 잠금 예외로 해제되지 않는지 검사.
- [ ] 저장·Undo 복귀·다시 선택의 dirty 표시를 원본/기존 계약에 따라 검사.

### Task 5 — 팔레트·사용자 지정 색상 세션

**Create:** `ui/common/ObjectColorPicker.qml`, `ui/common/CustomColorEditor.qml`, `app/editorcolors.cpp`, `platform/screencolorpicker.h`, `tests/color_editor_tests.cpp`.
**Modify:** `app/editorcontroller.h`, `app/CMakeLists.txt`, 필요한 플랫폼별 색상 추출 구현.
**Consumes:** Task 2 색상 상태, Task 3–4 명령.
**Produces:** 팔레트 즉시 명령 및 사용자 지정 세션의 적용/취소.

- [ ] 팔레트 값/순서, HEX3/6, RGB/HSL 반올림·입력 유지 실패 테스트.
- [ ] 사용자 지정 조작은 apply 이전 문서/dirty/이력 무변경임을 검사.
- [ ] 잘못된 입력·pointer cancel·Enter/Escape·선택 변경·늦은 스포이트 결과를 검사.
- [ ] 상속 초기화와 부모색 변경의 표시 재계산을 연결; 자식 도형/이력 불변 검사.
- [ ] 플랫폼 capability별 노출 정책을 확인하고 실제 화면 추출은 지원 플랫폼에서 별도 검증.

### Task 6 — 기본 툴바·메모·지방 기간·다중 속성 QML

**Create:** `ui/common/TerritorialSelectionToolbar.qml`, `ui/common/ObjectNotesPopover.qml`, `ui/common/RegionValidityFields.qml`, `ui/common/MultiObjectProperties.qml`.
**Modify:** `ui/common/EditorPanel.qml`, `ui/common/MapView.qml`, `ui/desktop/DesktopWorkspace.qml`, `ui/common/Main.qml`, `app/editorselection.cpp`, `app/CMakeLists.txt`.
**Consumes:** Task 3–5 공통 read model/commands/session.
**Produces:** PC/모바일 공통 실제 사용자 입력 경로.

- [ ] 원본의 단일 선택 툴바/수동 편집창 열기·닫기를 연결.
- [ ] 메모 readonly와 이름/color disabled를 별도로 표현.
- [ ] 지방에만 기간 필드 노출; 미구현 소속 변경이나 종류 전환을 활성화하지 않음.
- [ ] 폼 전체 Apply/Cancel을 웹 속성 확정 방식으로 사용하지 않고 색상창의 Apply/Cancel은 유지.
- [ ] 검색/선택/Back/가려진 모바일 툴바/IME 전환의 M3.1 불변조건을 회귀 검사.

### Task 7 — PC·360px 동등성·저장 회귀·검토

**Create:** `tests/property_ui_tests.cpp`, `docs/qt-properties-implementation.md`.
**Modify:** `README.md`, `docs/web-to-qt-roadmap.md`, 관련 CMake/CI 검증 등록.

- [ ] 아래 인수 시나리오를 PC 1100px/모바일 모드 360px에서 같은 fixture로 실행.
- [ ] canonical 문서, native revision 변화, Undo/Redo, 저장 bytes의 PC/모바일 일치 확인.
- [ ] 웹과 Qt는 다른 파일 포맷이므로 raw bytes끼리 같다고 비교하지 않음. 웹 의미 필드와 이벤트/이력 결과를 대조.
- [ ] 기존 모든 CTest 실행. 예전 예상값과 달라진 테스트는 원본 증거를 붙여 조정하며 삭제/skip으로 통과시키지 않음.
- [ ] 새 폼·색상창·잠금·오류·좁은 화면 캡처를 직접 검토.
- [ ] Windows 네이티브/Android 빌드·실기기 검증은 수행한 범위만 별도 기록.
- [ ] 소스·패치·검증 로그 및 원격 반영 여부를 각각 기록. main 병합·배포는 별도 요청 없이 수행하지 않음.

## 10. 인수 시나리오

| ID | 입력/동작 | 반드시 확인할 결과 |
|---|---|---|
| P01 | 국가/하위단위/지방 각 하나 선택 | 정확한 명칭·색상·메모 진입점; 지방만 기간 |
| P02 | 이름 A 변경 후 메모 입력 | 이름만 확정, 별도 Undo, 나머지 초안 보존 |
| P03 | 빈 이름·공백·한글 조합 입력 | 원본 fallback/trim; 객체 ID 유지; 새 필수값 제약 없음 |
| P04 | 같은 소속·종류의 같은 이름 | 원본과 같은 경고, 임의 중복 거절 없음 |
| P05 | 국가와 지방에 앞뒤 공백 있는 메모 | 확정 직후와 저장·재열기 각각 원본 결과 일치 |
| P06 | 국가명 override 삭제 의미 | 기본명 fallback; archive가 최근 override를 덮어쓰지 않음 |
| P07 | 지방의 빈 시작/종료, 연도만, BCE, 윤년 | 원본 precision/열린 경계, 잘못된 날짜 거절 |
| P08 | 기간 역전·관계 검증 실패 | 문서/이력 무변경, 필드별 원본 오류 표시/복귀 |
| P09 | 팔레트 일반 색 클릭 | 즉시 한 명령; 이름/메모 초안 미확정 |
| P10 | RGB/HSL/HEX/SV/hue 연속 변경 후 취소 | 문서·Undo/Redo·저장 내용 무변경 |
| P11 | 잘못된 HEX/RGB/HSL 후 적용 | 적용 비활성; 정상 입력으로 회복 가능 |
| P12 | 부모색과 같은 명시색 지정 → 부모색 변경 | 자식은 명시색 유지; 같은 표시색을 NoOp로 오판하지 않음 |
| P13 | 자식 초기화 → 부모색 변경 → Undo/Redo | 상속 재개; 자식에 독립 문서색을 복사하지 않음 |
| P14 | 중간 부모가 하위단위/지방/국가인 색상 해석 | 원본 parent traversal 및 fallback 일치 |
| P15 | 단일 객체 잠금 상태에서 이름·색상·메모 | 이름/색상 비활성, 메모 읽기 가능; 선택 유지 |
| P16 | 자식이 선택되지 않은 부모 잠금 | 자식 직접 잠금값을 변경하지 않음 |
| P17 | 일부 잠금 상태의 일괄 버튼 두 번 | 전부 잠금 → 전부 해제; 각 1 Undo |
| P18 | 잠긴 대상 포함 다중 색상 | 원본의 공통 경로 결과 일치; 보호 extension은 별도 검사 |
| P19 | 같은 색 일괄 재확정·기존 Redo 있음 | 원본 이력 체크포인트와 Redo 분기 제거 재현 |
| P20 | A 색상창/메모 중 B 선택·같은 파일 재열기 | A 입력/늦은 결과가 B/새 인스턴스에 적용되지 않음 |
| P21 | 색상창 열린 상태의 Escape/Back/터치 취소 | 내부창 닫기만 수행; 지도 편집/상위 종료 중복 없음 |
| P22 | 새 웹 가져오기 → 편집 → v4 저장·재열기 | 이름원본/override, 명시/상속, 잠금, 기간 보존 |
| P23 | 이전 v3 편집본 마이그레이션 | 최근 값 유지; 잃어버린 상속 의도 추측 금지; 제한 보고 |
| P24 | 미해석 flag/분포/수계/대형 정수 포함 | 비관련 원시 보존 및 필요한 영향 차단 유지 |
| P25 | 검색/hover/선택만 반복, 기존 Undo/Redo 존재 | 문서·revision·dirty·이력·저장 bytes 무변경 |
| P26 | 모바일 시트가 툴바를 가림·회전 | 가려진 대상 터치 금지, 초안 소유권·팝업 수명 유지 |
| P27 | 비어 있거나 stale한 대상 목록 | 부분 성공 없이 거절; 허위 성공 토스트 없음 |
| P28 | 모든 M3.2 명령 성공/Undo/Redo | ID·GeometryRef·좌표·관계는 명세상 변경하지 않는 한 동일 |

### 후속 실행 명령

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
node tools/m3-selection-oracle.mjs build/selection_probe
node tools/m32-property-oracle.mjs build/property_parity_probe
```

마지막 명령 및 새 테스트 target은 위 구현 단계에서 추가할 산출물이다. 현재 존재하거나 통과한 명령이라고 주장하는 것이 아니다.

## 11. 원본 근거 목록

모두 웹 기준 `58e4087`의 파일이다. 함수 이름과 처리 단계를 기준으로 다시 열어 확인한다.

| 파일 | 확인 대상 |
|---|---|
| `index.html` (243–299) | 선택 툴바의 이름·색상·메모와 국가/하위단위/지방 구분 |
| `index.html` (공통 속성/지방 폼) | 다중 공통 색상, 지방 기간 필드, 관계 폼 |
| `country-property-controller.js` | 이름 trim·notes change, 표시 refresh |
| `selection-toolbar-presentation.js` | 단일 선택 노출, readonly/disabled, 메모·색상창, 모바일 occlusion |
| `app-domain-assembly.js` | 실제 isMutationBlocked 의존성 연결 |
| `property-editor-bindings.js` | 대상별 실제 change binding; 지방에만 기간 |
| `object-property-controller.js` | 이름 충돌 표시, 기본/상속색 표시, 지방 parent readonly |
| `app-object-metadata.js` | field dispatch, locked 거절, 정규화/검증 오류 시 refresh |
| `territorial-service.js` | 값 비교·메타데이터·잠금 명령 |
| `territorial-units.js` | 정규화, 기본 명칭 adapter, nullable 기간 |
| `country-feature.js` | 국가명 fallback, override 저장 pruning |
| `project-serializer.js` | 저장/자동저장 시 실제 필드 투영 |
| `app-color-picker.js` | palette/default/custom 이벤트와 즉시/적용 구분 |
| `custom-color-control.js` | HEX/RGB/HSL/HSV, 입력·Enter·Escape·pointer·스포이트 |
| `color-adapter.js` | 명시값 조회/제거와 도메인별 저장 위치 |
| `territorial-scope.js` | 실제 부모 체인을 따른 색상 해석 |
| `app-environment.js` | 팔레트 정의와 테마별 기본 국가색 |
| `app-object-commands.js` | 공통 capability, 일괄색상, 단일/다중 잠금 버튼 |
| `app-navigation-bindings.js` | 실제 objectLockBtn → batchToggleLocked 연결 |
| `history-service.js` | 이력 기록 시 동일값 사전 비교 없음, future 제거 |
| `temporal.js` | 연도/일자·BCE·확장연도·기간 비교 |

### 이번 원본 함수 확인 결과

수정하지 않은 `createObjectCommands`와 `createHistoryService`에 국가 A/지방 R fixture와 표시/저장 알림 어댑터를 주입했다. 실제 브라우저 DOM 전체 검증은 아니다.

```json
{
  "sameColorCreatesHistory": true,
  "sameColorClearsRedo": true,
  "lockedTargetsReceiveBatchColor": true,
  "batchLockAllOrUnlockAll": true
}
```

이 결과는 원본의 현재 경로에 대한 계획 근거다. M3.2 C++ 구현이 통과했다는 뜻이 아니다.

## 12. 완료 판정

M3.2 완료는 ‘세 종류에서 입력이 된다’가 아니라 **원본의 해당 조작·값·상속·잠금·반복 명령·Undo 및 저장/재열기 결과를 재현한다**는 뜻이다. 지원하지 않은 기능/플랫폼/이전 저장본의 복원 불확실성은 별도 제한으로 명시한다. 발견한 웹 동작 차이를 Qt 쪽에서 임의 개선한 후 동등성 완료로 처리하지 않는다.
