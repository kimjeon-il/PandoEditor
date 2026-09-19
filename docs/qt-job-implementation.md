# M1.4 — 스냅샷 작업·취소·세션 수명

## 기준 및 범위

2026-09-19. M1.3 `2b0440fb48571570c5126de5e389b57763a2a271`에서 분기한 `codex/m1-4-worker-sessions`에서 작업한다. `main` 기준 `6ae7d80`의 Windows 프레임 코드를 포함하며 기존 두 브랜치와 웹 저장소에는 쓰지 않는다. 앱 main 병합·배포·Android 패키지는 이 작업에 포함하지 않는다.

웹 원본 대조와 앞으로의 완료 기준은 [기능 동등성 계약](web-parity-contract.md)에 기록했다. 이번 단계는 실제 Qt 백그라운드 명령 준비 경로와 세션 안전성을 구현한다. 아직 웹 도형 연산기나 웹 파일 가져오기 기능을 구현했다는 뜻은 아니다.

## 데이터와 실행 경계

`Project::snapshot()`은 편집 스레드에서 `ProjectSnapshot`을 만든다. 문서·인덱스·CountryView를 함께 보유하는 기존 불변 DocumentState와 projectInstanceId/revision을 소유하므로, Project가 재열기·이동 대입·파괴되어도 작업의 읽기 참조가 유지된다. `CommandProcessor::prepare(snapshot, request)`는 동일 검증·잠금·보존 규칙으로 후보를 준비한다. 기존 `prepare(Project, request)`는 스냅샷 어댑터이며, worker에서 가변 Project를 직접 읽지 않는다.

`core/jobs.h/.cpp`는 Qt 없는 owner-thread 스케줄러다. 실제 웹 지도 편집 클라이언트처럼 한 작업만 실행하며, 같은 key의 대기 작업은 최신 한 개만 남긴다. 실행 중인 같은 key는 coalesced로 표시하지만 물리 작업 종료 전에는 실행 슬롯을 풀지 않는다. 다른 key는 priority 내림차순, 같으면 요청 순서대로 실행한다. enqueue의 메모리 준비가 끝나기 전에 기존 작업을 취소하지 않는다.

작업 티켓은 스케줄러 내 ID뿐 아니라 내부 identity를 가지므로 다른 스케줄러의 같은 숫자 ID나 이미 처리된 결과는 수락하지 않는다. cancel/cancelKey/cancelAll/close와 aborted 사유를 지원한다. 해제 시 토큰도 닫는다. 공유 취소/진행 상태만 atomic이고, 큐·Project 변경은 편집 스레드에서 직렬화한다.

`JobToken`은 중단 사유와 마지막 진행률을 하나의 atomic 값으로 관리한다. 진행값은 -1(미정), 0–100 범위이고 감소하거나 취소 후 갱신되지 않는다. 후보 검증 작업은 시작/끝만 보고하므로 중간 백분율을 꾸며 표시하지 않는다.

## Qt runner와 완료

`app/commandjobrunner`는 QtConcurrent::run과 QFutureWatcher를 연결한다. worker callable은 소유한 snapshot/request/token 등 값만 캡처한다. Project나 Controller 포인터를 worker에 전달하지 않는다. 완료는 QObject 수신자에 연결된 편집 스레드 이벤트로 전달하며 move-only PrepareResult를 이동해 받는다. 태스크 예외는 준비 실패로 전달한다.

취소는 즉시 티켓을 obsolete로 표시하고 호출자 완료 통지를 이벤트 루프에 예약한다. worker가 협력하지 않아도 늦은 후보는 폐기한다. 하나의 동기 validateDocument 실행 중간을 강제로 중단하는 기능은 없으며, Qt future cancel만으로 중단된다고 주장하지 않는다. 폐기된 Controller/runner의 완료 연결은 실행되지 않고 worker가 가진 스냅샷은 안전하게 살아 있다.

실제 완료 시 프로젝트 인스턴스·문서 ID·revision을 확인한다. 수락한 preview도 CommandProcessor::confirm에서 다시 검사하므로 완료와 확정 사이의 변경을 거절한다. 오래된 계산은 문서·dirty·저장 기준·Undo/Redo에 반영되지 않는다. 실패·취소·NoOp도 기존 Redo 분기를 보존한다. 실제 적용만 한 ChangeSet과 revision 증가 한 번을 만든다.

M4에 해당하는 worker RPC 프로토콜, 작업 timeout/crash 재기동, geometryRevision 기반 읽기 전용 국경 캐시의 별도 유효성 정책, 실제 도형 연산은 아직 이식하지 않았다. 여기서는 문서를 변경하는 후보의 엄격한 project revision 계약을 구현한다. 스케줄링 지표는 진단용 일부 수치이며 웹의 모든 latency percentile 계측을 이식했다고 보지 않는다.

## 공통 Controller/UI

`preparePendingEditsAsync()`는 후보만 준비하고, `applyPendingEditsAsync()`는 후보가 수락되면 기존 confirm 계약으로 적용한다. PC/모바일은 같은 경로를 사용한다. 초안을 수정하거나 미리보기를 취소하거나 성공적으로 다른 프로젝트를 열면 이전 결과는 적용되지 않는다. 파일 읽기/검증 실패는 기존 작업 문서와 초안을 먼저 지우지 않는다.

공통 패널에는 실제 작업 상태일 때만 진행 표시와 ‘작업 취소’ 버튼이 나타난다. 작업 취소는 초안을 보존하고, 별도 초안 취소는 명시적으로 초안을 버린다. 아직 확정하지 않은 초안의 Undo/Redo는 기존 정책대로 적용/취소를 요구한다. 코어에서는 실제 Undo/Redo로 revision이 바뀐 뒤 늦게 도착한 결과도 검증한다.

웹 이름·메모 필드는 각 change에 대응하여 따로 확정한다. 이름 Enter/포커스 이동, 메모 편집 후 포커스 이동이 진입점이다. 다른 속성 초안을 함께 확정하거나 다시 로드하지 않는다. 잘못된 입력은 초안을 보존한다. 이 정정은 M1.3 기록의 ‘모든 필드가 적용 버튼으로만 확정’ 부분보다 우선한다. 프로그램 API로 요청한 진짜 복합 변경과 잔여 프로토타입 속성의 적용 기능은 여전히 한 Undo다.

## 테스트 우선 기록

원본 core 4개 CTest 묶음을 먼저 실행했다. 새 코어 API 골격을 연결한 10개 시나리오가 실제 assertion 실패하는 것을 확인한 뒤 구현했고, 수명 종료 시나리오를 보강했다. Qt red 커밋 `bd8395657e6e4ab76e090044e7d6df9d56087e71`은 컴파일이 성공한 뒤 새 controller 7개 시나리오가 실패했다. 기존 9개 묶음은 이 red 실행에서도 통과했다. 실행 ID는 `35410893852`다.

## 실행한 검증

- 로컬 C++17 Debug: 기존 command/할당 실패/core/model + 신규 job, CTest **5/5 통과**.
- 로컬 AddressSanitizer + UndefinedBehaviorSanitizer: 동일 CTest **5/5 통과**.
- 기존 할당 실패 주입 회귀: prepare 51, confirm 11, replace 28 위치에서 검증 통과. 이 숫자를 신규 모든 큐·Qt 이벤트 할당 실패를 검증한 수치로 확대하지 않는다.
- Qt 6.8.3 / Ubuntu 24.04 / offscreen·software: 전체 CTest **12/12 통과**, 컴파일 경고와 QML 경고 없음. 실행 `35411841231`에서 정확한 소스 트리 `a022ad797e20f5c5b7a8e88684ce6b275b9b9b31`을 검증한 뒤 코드 커밋 `7de00edb301135d38aaacc03546122f910ca37cc`로 반영했다. 이후 문서 마무리 커밋은 별도의 읽기 전용 CI에서 다시 검사한다.

코어 job 테스트는 원본 [1,3] 실행/대기 abort/stale 사례, 우선순위 FIFO, close/중복·타 스케줄러 결과, Undo/Redo/동일 파일 재열기, 실제 스레드에서 snapshot prepare, 공유 도형 수명, 검증 실패, 확정 전 재검사, 단일 Undo/NoOp/Redo, 취소 후 진행 정지를 확인한다.

Qt runner 테스트는 semaphore로 실제 작업을 지연하여 별도 스레드 실행·GUI 완료·최신 대기 작업·늦은 취소 결과·worker 예외·runner 파괴 후 결과 폐기를 확인한다. Controller는 즉시 무변경, 초안 보존, 재열기, preview/confirm, NoOp/실패, PC/모바일 저장 bytes와 revision/Undo를 비교한다. QML은 PC·360px에서 이름/메모의 독립 확정과 복합 비동기 적용·취소를 확인한다. 빠른 작업 취소 테스트의 버튼 진입은 실제 QML clicked 신호를 직접 발생시켜 완료 이벤트와의 경합을 고정하며, 물리 터치 검증으로 기록하지 않는다.

## 재현과 제한

```sh
cmake -S . -B build-core -G Ninja -DPANDOEDITOR_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core --parallel 2
ctest --test-dir build-core --output-on-failure

cmake -S . -B build-sanitize -G Ninja -DPANDOEDITOR_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-sanitize --parallel 2
ctest --test-dir build-sanitize --output-on-failure

# Qt 6.8.3 / Ubuntu 24.04
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
```

실제 Android 기기·cross-build·SAF·한글 IME·Back·회전, Windows 네이티브 실행, 전체 세계지도 성능/메모리, 강제 worker preemption은 검증하지 않았다. 외부 리뷰어 또는 별도 agent의 독립 코드 리뷰를 받은 것으로 기록하지 않는다. 저장 I/O는 기존 경로이며 이미 확정한 명령과 파일 쓰기를 한 분산 트랜잭션으로 묶지 않는다.

다음 단계는 M2의 실제 웹 완전 저장본 가져오기다. 기존 Qt v1/v2/v3 열기를 웹 파일 호환으로 표현하지 않는다.

## 변경 전달과 재현성

소스 전송용 일회성 CI는 패치 SHA-256과 변경 전/후 Git tree를 검증하고 전체 테스트 성공 후에만 격리 브랜치에 비강제 push했다. 전송 파일과 해당 일회성 workflow는 그 코드 커밋에서 제거했다. 최종 저장소에는 읽기 전용 `.github/workflows/m1-4-validation.yml`이 남으며 main·웹 저장소의 실행 정책이나 배포 파일을 변경하지 않는다.
