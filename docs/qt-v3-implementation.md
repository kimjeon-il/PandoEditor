# M1.1–M1.2: 공통 문서 모델과 Qt v3

> 이 문서는 M1.1–M1.2 당시의 기록이다. 후속 M1.3의 범용 명령·revision·스냅샷 Undo/Redo 및 현재 검증 결과는 [명령 구현 기록](qt-command-implementation.md)을 참조한다. 아래 Windows 검증과 작업 트리 상태는 당시 기준이며 후속 변경의 실행 결과가 아니다.

## 범위

2026-09-18. `core/include/pandoeditor/document.h`가 공통 도메인 모델이다.
`ProjectDocument`가 유일한 편집 데이터 소유자이고, 기존 국가 화면은 `CountryView`의 const 참조를 읽는다.
기존 `Country`는 예전 테스트·호출자를 위한 입력 DTO일 뿐 문서 안에 중복 저장하지 않는다.

- 국가·하위단위·지방, 기본/기간별 영토 관계, 레이어와 영토 소속 분리.
- ObjectRef·레이어·도형 버전·부모/국가/레이어 역참조 인덱스.
- `shared_ptr<const Geometry>` 불변 버전 공유. 문서 복사와 속성 Undo는 좌표를 복제하지 않는다.
- ID·참조·부모 순환·소속 불일치·포함 기간 중첩·날짜 검증.
- Qt v1/v2/v3 읽기, v3 저장. PC와 모바일은 동일 codec을 사용한다.
- 새 국가 종류의 편집 UI, 웹 가져오기, 범용 명령 처리기, 작업 revision, 합병·분할, 타임라인, 세계지도는 포함하지 않는다.

현재 렌더러/목록은 국가만 표시한다. 하위단위·지방과 미해석 데이터가 있으면 파일 안내에서 보존/미표시와 편집 제한을 알린다.
v1/v2를 열기만 해서는 디스크를 변경하지 않는다. 저장하면 v3가 되므로 이전 앱으로 다시 열 수 없다. 필요하면 **다른 이름으로 저장**한다.

## 구현된 v3 wire 형식

기본 루트는 `format: "pandoeditor-project"`, `version: 3`, `documentId`, `units`, `relations`, `geometries`, `presentation`, `extensions`이다.

- unit: `id`, `kind(country|subunit|region)`, `name`, `notes`, `geometryRef:{id,version}`, `locked`, `coverageMode`, `validity`.
- relation: `id`, `unitRef`, nullable `parentRef`/`sovereignRef`, `mode(base|dated)`, `validity`.
- validity: `{from,to}`. 각 끝점은 `null` 또는 `{text,precision:"year"|"date"}`. 기원전·부호·원본 날짜 정밀도를 유지하고 0년은 거절한다.
- geometry: `{id,version,geojson:{type,coordinates}}`. GeoJSON 점·선·면과 Multi 계열을 지원하며 영토는 Polygon/MultiPolygon을 참조한다.
- presentation: `userLayers`는 하단→상단 배열, `membership`은 `{ref:{domain,id},layerId}` 배열, `objectStyles`는 `{territorial:{"객체 ID":{color,opacity}}}` 객체다.
- extension: 설계서의 `extensionId`, `sourceFormat`, `sourceSchema`, `jsonPointer`, `payload`, `status`, `dependencyKnowledge`, `dependencies`, `forbiddenEffects` envelope.

미구현 도메인·표현 필드를 빈 기본값으로 채워 지원하는 척하지 않는다. 입력에 있으면 lossless extension으로 보관한다.
기존 ID와 배열의 국가/레이어/도형 ring·part 순서는 유지한다. membership/style의 key 순서는 의미가 없는 정규화 순서다.
legacy documentId는 원본 JSON의 정규화 해시에서 결정하며 객체 ID는 변경하지 않는다.

## 보존·안전 정책

알 수 없는 필드는 JSON pointer와 출처 스키마를 붙여 `unsupported`로 보관한다. 알려지지 않은 큰 정수·소수·지수 숫자는 double을 거치지 않는 원본 토큰이다. null, 값 타입, 배열 순서를 유지하고 모든 깊이의 중복 키를 거절한다.
기존 extension envelope의 추가 필드도 보존한다. archive payload는 수정된 정상 필드 위에 재적용하지 않는다.

미해석 의존성이 있으면 안전 목록의 이름·메모 외 변경을 보수적으로 제한한다. 알려진 의존성의 금지 효과를 별도로 확인하며, 실제 참조를 끊는 삭제는 허용하지 않는다.
현재 화면에서 제한된 초안을 확정/저장하려 하면 오류를 표시하고 초안과 이력을 유지한다.
일반 열기/모바일 가져오기는 후보 문서 검증과 투영 생성까지 끝낸 뒤 교체한다. 실패 시 기존 문서·선택·초안·dirty·Undo/Redo가 유지된다.

도형 버전은 현재 GeometryStore에 존재하는 항목을 보수적으로 모두 저장한다. 미해석 도메인의 참조를 아직 해석할 수 없기 때문에 미참조처럼 보이는 도형도 임의 삭제하지 않는다. 도형 편집·참조 수거는 후속 단계다.
codec 한도는 256 MiB/중첩 128, 현재 앱 저장 장치 경로의 기존 상한은 **64 MiB**다. 읽기보다 큰 결과를 저장하여 다시 열지 못하는 일이 없도록 쓰기 전에 같은 상한을 검사한다.
Windows/기기 내부 저장은 QSaveFile 원자적 교체다. Android SAF 내보내기는 공급자 스트림이므로 목적 파일의 원자성은 보장하지 않으며 기기 내부 저장본은 유지한다.

## 검증

2026-09-18 Windows MinGW Qt 6.8.3 빌드에서 CTest **6/6 통과**:

- `core_tests`: 기존 국가 선택·속성·레이어·Undo 회귀.
- `model_tests`: 계층 관계, 역참조, 불변 도형 공유, BCE/윤년/연말 날짜 경계, 실제 기간 공백, extension 의존 레이어 삭제의 원자적 거절.
- `migration_tests`: v1 기본값, v2 다국가/다중 레이어·holes/MultiPolygon 좌표 보존, v3 계층/기간 관계, lossless extension/중복 키/한도.
- `storage_tests`: 데스크톱·모바일 후보 읽기 실패 시 선택·초안·dirty·Undo 보존, 64 MiB 쓰기 상한, legacy 파일 비변경.
- `editor_tests`, `ui_tests`: 저장→재열기와 데스크톱/모바일 QML 편집 흐름. 360px 모바일 저장 화면도 자동 캡처했다.

포터블 Windows 실행 파일은 `dist/windows-portable/pandoeditor.exe`에서 기동·응답 검사를 수행했다.

실제 Android SAF·터치·한글 IME·물리 Back·회전은 이번 작업에서 검증하지 않았다. 모바일 모드의 Windows Qt 자동 테스트와 실제 Android 입력은 구분한다.
기존 제목 표시줄 미커밋 변경과 `dist/`를 보존했다. 이번 작업은 별도 커밋·푸시·배포본 갱신 없이 작업 트리에 남긴다.
