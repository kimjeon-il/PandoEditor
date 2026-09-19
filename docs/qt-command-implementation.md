# M1.3 — 원자적 명령과 스냅샷 이력

## 기준과 범위

2026-09-18, `main`의 `6ae7d807bd54dd27cca4a8d2c530340ec25601b5`에서 분기한 `codex/m1-3-atomic-commands`에서 구현했다. 기준 커밋의 Windows 네이티브 창 프레임 변경을 유지한다. `main` 병합이나 배포본 갱신은 수행하지 않는다.

M1.3은 현재 제공하는 국가 속성·사용자 레이어·국가의 레이어 소속 변경에 공통 명령 계약을 도입한다. 도형 편집·웹 가져오기·백그라운드 작업 스케줄러는 추가하지 않는다. M1.4와 M2 이후는 후속 범위다. M1.1–M1.2 당시의 구현·검증은 [이전 기록](qt-v3-implementation.md)에 그대로 보존한다.

## 요청과 확정 계약

`core/include/pandoeditor/commands.h`의 `CommandRequest`는 `commandId`, `projectInstanceId`, `documentId`, `revision`, `targets`, `args`를 가진다. `args`는 문자열 JSON이 아니라 C++17의 typed property edits와 `variant` action이다. PC와 모바일 모두 `EditorController`가 같은 구조를 생성한다.

| commandId | 주 동작 | 함께 확정할 수 있는 초안 |
|---|---|---|
| `edit.properties` | 국가·레이어 속성 확정 | 국가 이름·메모·RGB·불투명도와 선택 레이어 이름·불투명도 |
| `country.color` | 즉시 색상 선택 | 현재 국가·레이어 속성 초안 전체 |
| `country.move` | 국가의 사용자 레이어 소속 이동 | 동일 |
| `layer.add` / `layer.remove` | 사용자 레이어 추가·삭제 | 동일 |
| `layer.move` | 사용자 레이어 순서 이동 | 동일 |
| `layer.visibility` / `layer.lock` | 레이어 표시·잠금 변경 | 동일 |

명령 ID와 action 종류의 불일치, 중복·없는 대상, 대상 목록과 인수의 불일치, 중복 속성 패치, 빈 이름, 잘못된 색상·불투명도를 거절한다. 전후 실제 차이에서 잠금과 `effectAllowed()`를 검사하므로 간접적으로 순서가 바뀌는 레이어도 보호 대상이다. 마지막 레이어 삭제나 참조가 남는 삭제는 전체 후보의 `validateDocument()`에서 거절한다.

잠금·보존 규칙은 **원본 상태**를 기준으로 검사한다. 같은 요청에서 잠금을 해제하더라도 원래 잠긴 국가를 수정하는 우회는 허용하지 않는다. 기존 정책대로 레이어 잠금은 소속 객체 편집을 막으며, 잠긴 레이어 자체의 이름·표시·순서 관리와 잠금 해제는 가능하다. 이 정책과 미해석 데이터의 별도 금지 효과는 서로 독립적이다.

`prepare`는 후보 문서에 속성 묶음과 action을 적용하고 전체 문서·파생 참조를 검증한다. 성공하면 읽기 전용 before/after와 요청 메타데이터를 담은 move-only preview를 반환한다. 원본 문서·revision·저장 기준·Undo/Redo·UI 초안은 변경하지 않는다. 정규화 후 원본과 같으면 `NoOp`이고 preview가 없다. 정상 범위의 레이어 이동이 이미 가장자리인 경우도 실제 변경이 없다면 `NoOp`다.

`confirm`은 프로젝트 인스턴스·문서 ID·revision·before 상태를 다시 확인한다. 하나의 성공한 요청은 ChangeSet 하나와 revision 증가 한 번으로 확정된다. 이력 메모리 할당까지 먼저 끝내고 상태를 교체하므로, 확정 실패가 기존 Redo 분기를 먼저 잘라내지 않는다. 취소·실패·NoOp에는 새 이력이 없고 기존 Redo도 유지된다.

preview는 한 번만 사용할 수 있다. 성공뿐 아니라 취소·stale·잘못된 프로젝트에 대한 확정 시도·확정 실패에서도 토큰을 소비한다. 같은 파일을 다시 열면 새 프로세스 내 세션 ID를 발급한다. 이 세션 ID는 파일에 저장하지 않으며 외부 프로세스 간 식별자 프로토콜을 뜻하지 않는다.

## 스냅샷·참조 수명·dirty

`core/src/documentstate.h`의 비복사·비이동 `DocumentState`를 최종 힙 주소에서 만든다. 한 상태가 `ProjectDocument`, `DocumentIndex`, `CountryView` 배열, 국가 조회 인덱스를 함께 소유한다. `CountryView`는 그 상태의 문서를 참조하고, `Project`·ChangeSet·저장 기준은 `shared_ptr<const DocumentState>`를 보유한다. 문서만 교체하고 이전 참조 배열을 재사용하지 않는다.

confirm·Undo·Redo는 완성된 상태 전체를 선택한다. 도형은 기존 `shared_ptr<const Geometry>`를 공유하며 속성 변경으로 좌표를 깊은 복사하지 않는다. `GeometryRef`와 도형 identity 유지, 독립된 동일 좌표 할당의 semantic equality를 테스트한다.

성공한 confirm·Undo·Redo만 revision을 1 증가시킨다. 할 일이 없는 Undo/Redo, prepare·취소·NoOp·저장은 증가시키지 않는다. 재열기는 새 세션에서 revision 0으로 시작한다. revision 최대값에서 변경은 거절한다. 모든 가변 조작은 호출자가 편집 스레드에서 직렬화해야 하며, 동시에 호출해도 안전한 worker API를 제공한다는 뜻은 아니다.

문서 dirty는 마지막 `markSaved()` 상태와 현재 문서의 의미상 차이다. `revision != savedRevision`이나 Undo 커서만으로 판단하지 않는다. **저장 → 변경 → Undo**로 저장 내용에 복귀하면 revision이 증가했어도 문서 dirty는 해제된다. 미해석 JSON payload와 envelope 추가 필드는 보수적으로 원문 바이트까지 같아야 동일하다고 판단한다.

UI의 미확정 초안과 가져온 미저장 상태는 문서 dirty와 별도로 관리한다. 앱의 미저장 표시는 세 조건의 합이다. prepare 자체가 dirty를 바꾸지는 않지만 이미 존재하는 초안의 미저장 표시를 숨기지도 않는다.

## PC·모바일 편집기 동작

`app/editorcommands.cpp`가 요청 생성·미리보기·확정·오류 메시지의 공통 어댑터다. 기존 `Project` 속성 API는 호환 어댑터로 남기되 같은 처리기를 통과한다. 컨트롤러에서 개별 setter를 차례대로 확정하던 경로는 제거했다.

공통 `EditorPanel.qml`의 **적용**은 선택 국가와 레이어의 속성 초안을 한 요청으로 확정한다. 필드 사이 포커스 이동과 슬라이더 놓기만으로는 자동 확정하지 않는다. 색상 견본·레이어 추가·삭제·순서·표시·잠금·소속 이동 같은 즉시 명령은 현재 초안과 해당 동작을 같은 후보에 묶는다. 뒤의 동작이 실패해도 앞의 초안만 먼저 적용되는 부분 성공은 없다.

`preparePendingEdits()`는 초안을 유지한다. `cancelPreview()`는 토큰만 폐기한다. UI의 **취소**는 별도 `discardPendingEdits()`를 호출하여 토큰과 미확정 초안을 명시적으로 버린다. preview를 만든 뒤 초안이 수정되면 기존 토큰은 폐기되어 이전 입력을 잘못 확정할 수 없다. `NoOp`를 prepare한 경우에는 확정할 토큰이 없다.

초안이 남아 있는 Undo/Redo는 `PENDING_EDITS` 메시지와 함께 거절하고 초안·문서·이력을 유지한다. 사용자가 적용 또는 취소를 선택한 뒤 Undo/Redo를 사용한다. 자동 초안 확정으로 새 이력이 생기거나 Redo가 사라지는 동작을 금지한다.

국가·레이어 선택 이동과 저장 시의 기존 초안 확정 진입점은 유지하되 한 요청으로 처리한다. 파일 쓰기는 별도 저장 단계다. **명령 확정 성공 뒤 파일 쓰기가 실패하면 문서는 미저장 상태로 남는다**. 저장 I/O 실패까지 이미 확정된 명령을 되돌리는 분산 트랜잭션은 구현하지 않는다. 열기/가져오기 후보 읽기·검증 실패는 기존 문서·초안·선택·이력을 유지한다.

실패는 `INVALID_ARGUMENTS`, `INVALID_TARGETS`, `LOCKED`, `UNSUPPORTED_DEPENDENCY`, `VALIDATION_FAILED`, `STALE_REVISION` 등의 코드와 한국어 안내로 전달한다. 잘못된 이름·색상 초안을 원래 값으로 덮어쓰지 않는다.

## 검증 기록

### 테스트 우선 확인

로컬 원본 코어·모델 CTest **2/2**를 먼저 실행했다. 명령 테스트 10개 시나리오를 최소 API 골격에 연결해 컴파일 후 실제 assertion 실패를 관찰하고 구현했다. 컨트롤러는 별도 원격 커밋 `1511e22`에서 신규 7개 시나리오가 기존 동작을 상대로 실패함을 확인했다. 이 실패는 단순 빌드 실패가 아니며 복합 Undo 분리·잘못된 초안 초기화·자동 확정으로 Redo 소실 등을 재현한다.

### 실행한 검증

- 로컬 C++17 Debug: `command_tests`, `command_allocation_tests`, 기존 `core_tests`, `model_tests` **4/4 통과**.
- 로컬 AddressSanitizer + UndefinedBehaviorSanitizer: 같은 **4/4 통과**.
- 메모리 할당 실패 주입: prepare **50**, confirm **11**, replace **28**, 총 **89개 실패 위치**에서 문서·참조·revision·dirty·기존 Redo 보존 확인. Undo/Redo는 할당을 금지한 상태에서도 성공한다.
- Qt 전체 회귀: Ubuntu 24.04 / Qt 6.8.3 / offscreen·software에서 CTest **9/9 통과**. 코드 커밋 `a7c4594e9ca31b6b107f3e90382d8b729627188e`, [실행 35357713365](https://github.com/kimjeon-il/PandoEditor/actions/runs/35357713365). 기존 core/model/migration/storage/editor/UI와 신규 command/할당 실패/command editor 테스트를 모두 포함한다.

코어 명령 테스트는 식별자·인수·대상 오류, validation 실패, 잠금·보존 장벽, 취소·소비한 preview 재사용, Undo/Redo/재열기에 의한 stale, NoOp의 Redo 보존, 저장 기준과 revision 분리, 복합 즉시 명령 단일 Undo, 참조 수명·도형 공유를 검증한다. PC/모바일 컨트롤러는 동일 입력의 결과 문서·revision·Undo/Redo·저장 bytes를 비교한다. QML 입력 테스트는 PC와 360px 모바일 모드에서 포커스 이동 무확정, 국가/레이어 복합 적용, 단일 Undo/Redo, 취소를 확인하고 화면을 캡처한다.

첫 Linux Qt 기준 실행 `35354522353`에서는 기존 core/model/migration/storage/editor가 통과했지만 `recoveryDialog.implicitHeight`의 순환 바인딩 경고로 UI 3개 시나리오가 실패했다. 경고 검사를 삭제하지 않고 복구 대화상자 높이를 명시하는 최소 변경을 포함했다. 기존 Windows 검증 결과와 이 Linux 기준 실패는 서로 다른 실행 환경의 기록이다.

재현 명령:

```sh
cmake -S . -B build-core -G Ninja -DPANDOEDITOR_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core --parallel 2
ctest --test-dir build-core --output-on-failure

cmake -S . -B build-sanitize -G Ninja -DPANDOEDITOR_BUILD_APP=OFF \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-sanitize --parallel 2
ctest --test-dir build-sanitize --output-on-failure

# Qt 6.8.3가 설치된 Ubuntu 24.04 runner
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ctest --test-dir build --output-on-failure --output-junit results.xml
```

격리 브랜치의 `.github/workflows/m1-3-validation.yml`은 테스트한 SHA의 소스 archive, CTest 상세 로그·JUnit·화면 PNG를 artifact로 남긴다. 소스는 3일, 테스트 결과는 7일 보관한다. 브랜치 push와 수동 실행만 등록하여 main의 기존 실행 정책을 변경하지 않는다.

## 검증하지 않은 것과 후속 제약

실제 Android 기기의 터치·한글 IME·SAF 공급자·물리 Back·회전, Android cross-build 및 Windows 네이티브 프레임 실행은 이번 검증에 포함되지 않는다. Linux offscreen의 모바일 공통 경로·360px 통과를 Android 실기기 통과로 기록하지 않는다. Windows 전용 테스트의 조건부 구문이 Linux에서 돌아갔다고 Windows 기능 검증을 주장하지 않는다.

큰 세계지도 성능·메모리 예산을 측정하지 않았다. 문서 스냅샷은 도형을 공유하지만 기타 문서 값과 이력 메타데이터의 복사 비용이 있고, 이력 개수 제한이나 영속화는 추가하지 않았다. M1.4의 비동기 작업 수명·취소·병렬 접근, M2의 import report/보존 장벽 승격과 M3 이후의 새 편집 명령은 별도 구현한다.
