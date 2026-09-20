# M4 1차: 계산·영토 이전·종류 전환

## 기준과 범위

- 웹 기준: 로컬 `map editor@17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da`.
- 배포 사이트와 로컬 소스가 동일하다는 주장은 하지 않는다.
- M3.4 WIP, Windows 프레임, `dist/`, 웹 저장소를 보존한다. 커밋·푸시·배포하지 않는다.
- 그리기·사용자 합병·절단선 분할·해안선·공유 국경·전체 세계지도는 제외한다.
- M3.4 multiply·종류별 boundary·전체 표시 메뉴의 완료 여부는 이번 작업과 별개다.

## 코드 경로

`EditorController` 구조 세션 → `CommandJobRunner` →
`prepareTerritorialGeometry` → QJSEngine 좌표 연산 →
`CommandProcessor::prepare` 후보 문서 → 도형 미리보기 → 명시적 confirm.

- 코어 `geometryoperations.h`는 Qt 비의존 요청·결과·취소 callback 계약이다.
- 제품 커널은 `assets/geometry`에 별도 보관한다. 출처, SHA-256, MIT 고지는 그 디렉터리 README/라이선스에 기록한다.
- Qt 6.8.3에서 원본 minified splaytree 삽입 식이 빈 결과를 내는 재현을 확인했다.
  원본 파일을 바꾸지 않고 해시 확인 후 동등한 개별 문장으로 변환한다.
- 빈 연산 결과는 성공한 Empty이며 오류/취소와 별도다. 기존 국가 전체 차감은 거절한다.
- JS 실행 자체를 선점 중단하지 않는다. 호출 전후 취소 검사와 job/session/revision 검사로 적용을 차단한다.
- 입력 owner와 결과 owner는 병렬 매핑으로 구분한다. 국가→하위단위의 결과는 세션에서 생성한 새 ID다.
- 원국가·목적국가·이전 영역과 겹치는 이전 소속 하위단위가 Patch 범위다.
  자식의 소속 변경, 빈 이전 부모 삭제, 참조 정리는 후보 문서에서 처리한다.
- 결과 확정은 한 ChangeSet이다. 지도 캐시는 확정 및 Undo/Redo 때 갱신한다.
- 기존 국가 ID 유지/새 하위단위 ID, native membership 없는 객체, 최신 표시 상태를 지원한다.

## 웹 Oracle 근거

- `app-territorial-conversion.js`, `territorial-units.js`, `temporal.js`,
  `polygon-clipping.min.js`는 58e4087→17c3dbe diff가 없어 기존 fixture를 재사용한다.
- 달라진 `territorial-edit-plan.js`는 17c3dbe 별도 fixture로 고정한다.
- `map-edit-worker.js`, `country-geometry.js`도 17c3dbe 원본과 blob hash를 고정한다.
- `tools/m4-geometry-oracle.mjs`는 실제 웹 kernel.plan 및 국가 전환 함수/executeMerge를 실행한다.
  Native probe는 실제 코어 Plan→계산→prepare→confirm 결과를 반환한다.
- 도형 배열 시작점·방향 차이는 대칭차가 빈 결과인지 비교하여 허용한다.
- 비교 사례: 이전, 독립, 국가→하위단위, hole, MultiPolygon, 빈 이전 부모,
  국가 전체 차감 거절, 인접한 두 국가.
- 국가 전환 harness는 UI 알림·화면 갱신을 비활성화하며 completeness 자동 생성은
  no-op seam이다. 이 검사는 completeness 합성의 동등성 증거가 아니다.

## 검증 항목

- `geometry_calculator_tests`: union/difference/intersection, 빈 결과, hole,
  MultiPolygon, 좌표 불변성, 비정상 입력, 취소, worker 소유 엔진.
- `territorial_geometry_tests`: 세 작업의 실제 면적·관계, 미리보기 비변경,
  PC/모바일 공통 Controller, 취소, stale, retained JSON, v5 재열기, Undo/Redo.
- `command_allocation_tests`: geometry Patch 준비의 각 할당 실패와 원본/이력 보존.
- `selection_editor_tests::chooserNamesIgnoreObjectOrder`: 이름순 Chooser와 렌더순 picking 분리.
- `selection_ui_tests::territorialGeometryPreview`: 1100px/360px QML 미리보기·확정·Undo/Redo.
- 2026-09-20 ASCII 경로 빌드에서 전체 CTest 32/32 통과(125.97초).
  `m4_geometry_web_parity`는 17c3dbe 고정 fixture의 8개 구조 transaction 비교를 통과했다.
  QML은 offscreen/software backend로 1100px/360px 경로를 검증했다.

## 회귀 수정

- 속성·표시 Undo/Redo가 도형 캐시 갱신 신호를 무조건 내보내던 문제를 고쳤다.
  이제 unit의 geometry binding이 실제로 달라질 때만 투영/피킹을 재구축하고
  구조 UI를 닫는다.
- 보존된 `flagDataUrl`은 실제 이미지 바이트로 검증한 뒤에만 QML Image로 넘긴다.
  손상된 data URL은 보존되지만 표시하지 않아 import 화면의 디코드 경고를 만들지 않는다.

## 실행하지 않은 검증

Android 실기 입력·SAF·IME·회전, sanitizer, 전체 세계지도와 대규모 성능 검증은 이번 증거에 포함하지 않는다.
Offscreen 캡처의 한글 폰트 부재는 실제 Windows 폰트 표시 검증으로 간주하지 않는다.
