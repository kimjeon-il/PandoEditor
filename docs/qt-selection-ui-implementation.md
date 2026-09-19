# M3.1 — 검색·다중 선택·겹침 선택창·지도 이동 QML 연결

## 기준과 범위

- 개발 기준: `codex/m3-1-selection` / `1aeb400e9738b518deef1c5c4806bfc76ad6a40a`.
- 웹 대조 기준: `kimjeon-il/world-map` / `58e4087f85aa51884bc4ab80959e05010d94d7d5`.
- 이번 범위: 기존 선택 코어·컨트롤러를 실제 QML 입력과 연결하고 PC/360px 공통 UI를 검증한다.
- M3.2 속성·잠금 편집 확장, M3.3 생성·관계·삭제, M3.4 전체 표시 동등성, M3.5 공유 경계 강조는 완료로 표시하지 않는다.
- main 병합, 웹 저장소 변경, 릴리스 갱신은 하지 않는다.

## 사용자 조작

| 경로 | 연결한 동작 |
|---|---|
| 검색 탭 | 이름·종류·ID 검색, 120ms 입력 지연, 빈 검색·결과 없음, 검색 지우기 |
| 검색 결과 | 일반 선택은 하나만 선택한 뒤 검색 닫기. Ctrl/⌘는 추가·해제. primary와 선택 순서는 공통 코어를 사용 |
| 지도 | 일반 클릭/터치 및 Ctrl/⌘ 추가·해제. 겹치면 선택창, 빈 곳은 선택 해제 |
| 겹친 객체 선택창 | 후보별 이름·종류, 열 때의 추가 선택 의도 유지, 방향키·Enter·Space, Escape·Back·닫기 |
| 지도 이동 | 검색 결과의 이동 버튼 또는 단일 선택의 이동 버튼. 선택 자체나 문서 내용을 변경하지 않음 |
| 숨김·잠금 | 숨김 객체는 지도 hit-test에서 제외하지만 검색에서 선택/이동 가능. 잠김과 선택 가능 여부는 별개 |
| 초안 | 검색·선택·선택창 이동으로 입력을 자동 확정하지 않음. 초안은 원래 객체에 남고 정상 이름/메모 필드 change는 독립 확정 |

**검색의 Shift 처리:** 고정 웹 `layer-tree-controller.js`는 Shift를 감지하지만 `orderedRefs: []`를 전달한다. `app-camera-navigation.js` → `selection-ui-controller.js` → 원본 reducer에서 이 경로는 범위 선택을 수행하지 않는다. Qt 검색에도 임의의 새 범위 선택 동작을 추가하지 않았다. 순서가 주어진 범위 선택과 scope별 anchor는 기존 코어/컨트롤러 테스트로 별도 검증한다.

**지도 후보:** 웹 `app-object-picking.js`에 맞춰 하위단위/지방은 PC 7px·모바일 12px 근접 허용을 사용하고 국가 후보는 가장 앞의 하나를 사용한다. 지원 중인 영토 도메인의 후보는 하위단위 → 지방 → 국가와 한국어 이름 비교 기준으로 정렬한다. 아직 지원하지 않는 지명·수계·분포 객체의 picking 완료를 의미하지 않는다.

**지도 이동 경계:** 현재 Qt 투영의 객체 bounds를 기존 `focusRequested` 신호로 받아 표시한다. 웹의 fit 계수 0.82×0.88 및 PC 10/모바일 12 상한을 적용하되, 전체 세계지도·구체 투영·날짜변경선·역사 시점별 국가 범위 해석까지 이식했다고 주장하지 않는다.

## 상태와 수명

`ObjectSearch.qml`, `ObjectSelectionButton.qml`, `ObjectChooser.qml`이 동일한 컨트롤러 선택 함수를 사용한다. 편집은 기존 CommandProcessor에 남는다. 검색·hover·선택·카메라를 파일 schema에 추가하지 않았다.

`editorpicking.cpp`는 후보를 프로젝트 스냅샷과 함께 세션에 보관한다. 후보 확정 전에 스냅샷과 가시성을 다시 확인하며, 문서 교체/문서 변경 및 카메라 이동·크기 변경은 선택창을 폐기한다. 닫기나 취소만으로 기존 선택을 바꾸지 않는다.

QML 입력 전환 중의 field commit을 막고 컨트롤러 재바인딩을 사용자 입력으로 취급하지 않는다. 포커스 이동에 의한 정상 필드 확정과 선택 동작 자체는 별개다. 미확정 preview 및 기존 Undo/Redo는 순수 선택 경로에서 유지된다.

여러 선택의 윤곽과 primary를 표시하지만 부모·자식 공유 선분 중복 제거는 M3.5 후속 범위다. 기존 Qt 국가 속성·사용자 레이어 UI는 유지했고 하위단위·지방 속성 편집을 새로 개방하지 않았다.

## 검증

검증 환경: Linux, Qt 6.8.3, `QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`. PC 1100px와 모바일 모드 360px를 각각 실제 QML에 로드한다.

- 선행 RED: 새 UI 10개 시나리오에서 검색 탭 부재, 지도 modifier 미연결, 선택창 부재를 확인했다.
- 추가 RED: 검색 결과에서 Ctrl+Enter modifier를 잃는 문제를 PC/360px 각각 재현한 뒤 수정했다.
- 최종 전체 CTest: **19/19 통과**, 실패 0, 건너뛰기 0 (`final-results.xml`, 총 42.94초).
- 새 실제 QML UI 시나리오: **6종 × PC/360px = 12개 통과**. QtTest의 초기화·정리를 포함하면 14개 PASS다.
- 수정하지 않은 고정 웹 reducer 대비 **2,066회 전이 일치**: 순서·primary·scope별 anchor·알림 비교.
- 기존에 실패하던 `editor_tests`와 `ui_tests`도 최종 전체 검사에서 통과했다.
- `git diff --check` 통과. 최종 캡처 6개를 검토했으며 후보 목록이 PC·360px에서 화면 안에 표시된다.
- 전체 기존 core/model/migration/storage/editor/command/job/web-import/UI 검사를 그대로 실행한다.
- 기존 editor/UI의 잠금·숨김 즉시 선택 해제 기대값만 승인된 선택 계약으로 교체했다. 대신 선택 유지, 실제 편집 차단, 가시성 값을 명시적으로 검사한다. 테스트를 제외하거나 skip하지 않는다.

### UI 인수 시나리오 (각 PC/360px)

1. 검색의 Ctrl/⌘ 추가·해제, Shift 원본 동작, primary, 일반 선택 후 검색 닫기.
2. 실제 지도 modifier 선택, 겹침 후보 선택, 열 때의 Ctrl 의도 유지, 잠긴 객체 선택, 숨김 지도 hit-test.
3. 검색 이동 버튼의 viewport 변경과 선택 유지, hover, 기존 Undo/Redo/revision/dirty/저장 bytes 보존.
4. 이름·한글 메모 초안을 입력한 상태의 검색·선택창 이동, 원래 객체 복귀, 정상 필드 독립 확정.
5. Back/Escape 취소, 창 크기 변경, 같은 파일 재열기 후 이전 선택창 폐기.
6. Ctrl+Enter 및 선택창 방향키·Enter, 숨긴 객체의 검색 선택/이동과 지도 선택 제외.

화면 캡처는 검색 다중 선택·겹침 후보창·선택 결과를 PC/360px별로 남긴다. 마우스와 합성 touch 이벤트를 사용하며 **Android 실기기 터치 검증으로 간주하지 않는다.**

### 검증 중 수정한 문제

- ColumnLayout의 implicitHeight에 의존해 후보 목록 높이가 0이 됨: Popup contentHeight와 ListView preferredHeight를 명시.
- 포인터 캡처와 Button 클릭 신호의 차이: mouse/touch/keyboard/accessibility를 한 invoked 경로로 연결.
- 검색 전환 중 초안 확정: 검색/선택창 전환 guard와 선택 재바인딩 guard 적용.
- 추가 tooltip이 다음 행의 touch hit-target을 가림: 웹 이름 검색 행에 없던 tooltip 제거.
- 키보드 Return이 modifier를 버림: QML key event의 실제 modifier를 전달.

## 재실행

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
node tools/m3-selection-oracle.mjs build/selection_probe
```

Windows 네이티브 실행, Android 빌드·실기기, 네이티브 IME·물리 Back·실제 기기 회전은 이번에 수행하지 않았다. 코드 검토는 이 작업의 diff와 테스트를 통한 자체 검토이며 별도 독립 리뷰어 검토가 아니다.

## 반영·전달 상태

이번 변경은 로컬 격리 작업 트리에서 구현·빌드·검증했다. GitHub의 `create_tree` 쓰기 요청이
“요청의 보안 상태를 결정하지 못했다”는 메시지로 차단되어, **원격 브랜치에 반영하지 못했다.**
차단을 우회하는 다른 쓰기 방식은 사용하지 않았다. 원격 작업 기준은 `1aeb400`이며 main은 `6ae7d80`이다.

완성된 변경은 해당 `1aeb400` 소스에 적용할 수 있는 unified patch, 전체 소스 ZIP, 검증 자료 ZIP으로 제공한다.
로컬 소스 복원용 Git 커밋은 실제 원격 커밋과 식별자가 다르며, 원격에 새 커밋이 생겼다는 뜻이 아니다.
Android·Windows 배포 파일은 포함하지 않는다.
