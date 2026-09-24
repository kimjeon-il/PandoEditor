# M3.2 — 국가·하위단위·지방의 속성·잠금

## 기준·전달 상태

- 웹 원본: kimjeon-il/world-map `58e4087f85aa51884bc4ab80959e05010d94d7d5`.
- 시작 소스: M3.1 QML 전체 ZIP, SHA-256 `c99bf6fe354fd628ba98f106fccf96546f634f15f0e7b4cfd540564e9d1c8cba`.
- 이 ZIP에는 원격 `1aeb400` 이후 QML 변경이 포함된다. 원격 main을 그대로 작업 기준으로 삼지 않았다.
- 격리된 로컬 작업 브랜치 `codex/m3-2-properties`; main 병합·릴리스 없음.
- 본 기록 작성 시 원격 반영 전이다. 전달 manifest에서 최종 Git 상태를 별도로 확인한다.
- 새 코드의 자체 diff/테스트 검토이며 독립 리뷰어 검토를 수행한 것은 아니다.

## 구현 범위

| 경로 | 구현 |
|---|---|
| 국가 이름 | 입력 trim, 변경값과 원본명 분리, 빈 변경값은 원본명/기본 지명으로 표시 |
| 하위단위·지방 이름 | 빈 저장 이름 허용, 이름 없는 종류별 표시, 동종·동일 소속국 이름 충돌 경고 |
| 메모 | 국가 live 원문, 하위단위·지방 trim; 국가 serializer/Undo/Redo 복원은 원본 prune 의미 |
| 색상 | 원본 65색 배열, 명시색 지정/기본값 제거, 상위 하위단위→국가 상속, 기본값 미리보기 |
| 사용자 지정 | HEX 3/6자리, RGB/HSL 숫자, HSV 평면/색조, 경계 검증, HSL 입력 유지, 적용/취소/Enter/Escape/Tab |
| 잠금 | 단일/다중 객체 잠금, 혼합이면 전부 잠금, 모두 잠기면 전부 해제; 선택 유지 |
| 다중 색상 | 웹의 직접 객체 잠금 예외와 동일 색상 checkpoint; 보존/Qt 레이어 보호는 유지 |
| 지방 유효기간 | 연도/일자/BCE/부호 확장 연도, nullable, 0년·잘못된 날짜·역전 거절 |
| 화면 | 단일 선택 툴바, 메모 팝업, 수동 편집창, 다중 공통 작업, 기존 Qt UI는 명시적 호환 메뉴 |
| 가져오기 | 원본 저장기에서 만든 작은 v5 완전본의 속성 편집, S/R 도형 즉시 표시, 원본/미해석 데이터 보존 |

국가·하위단위 날짜 입력을 새로 만들지 않는다. 다중 이름·메모·날짜 변경, 생성·삭제·소속 변경,
종류 전환·국기·지명·전체 표시/혼합 기능은 이번 단계의 구현으로 표시하지 않는다.
기본색은 현재 Qt의 고정 밝은 지도에 대응한다. 전체 지도 테마/투영/대규모 성능 동등성은 후속이다.

## 데이터·이력

`ProjectDocument`의 국가 `baseName`과 `nameExplicit`, `ObjectStyle.explicitColor`를 분리했다.
표시 RGB가 같아도 상속 상태와 명시 상태는 다르다. 상속색 계산은 도형/관계를 변경하지 않는다.
`DocumentState`는 문서·검증 인덱스·읽기 모델·CountryView를 함께 소유한다.

새 등록 명령: `territorial.field`, `territorial.color`, `territorial.color.reset`,
`territorial.batch-color`, `territorial.lock`. 대상·인수·잠금·보존 규칙·문서 전체 검증을 거친다.
모든 새 필드 명령은 다른 객체/다른 필드의 초안을 자동 수집하지 않는다.
필드 세션 및 색상 세션은 문서 인스턴스/revision/대상을 고정하고 늦은 확정은 거절한다.

동일 색상의 batch-color와 표시된 기본 국명을 다시 비우는 특정 경로는 원본대로 checkpoint를 남긴다.
외부 요청에 임의의 force-history/bypass-lock 옵션을 만들지 않았다. 기존 Redo는 성공 시에만 제거한다.
일반 NoOp·준비 실패·검증 실패·취소의 문서와 이력은 유지한다.

원본 국가 override의 history pruning(빈 이름 키 제거, 메모 앞뒤 공백 제거)을 Undo/Redo에 반영한다.
정규화가 필요할 때만 후보 문서와 인덱스를 먼저 구성하고, 실패하면 커서/revision/saved 상태를 유지한다.
정규화가 필요 없는 스냅샷 복원은 기존 무할당 경로다. 이미 저장된 정규화 bytes로 돌아오면 dirty도 복구한다.

## Qt v4 저장

v1/v2/v3/v4를 읽고 v4로 저장한다. v3까지의 이전 앱은 새 파일을 열지 못한다.
기존 파일을 읽는 것만으로 디스크의 파일을 덮어쓰지 않는다.

- 국가는 `baseName`, `nameExplicit` 필드를 갖는다. S/R은 기본명 필드를 비우고 일반 이름으로 저장한다.
- `presentation.objectStyles.*.color: null`은 자동/상속, 6자리 HEX는 명시색이다.
- 이전 Qt 색은 모두 명시값으로 유지한다. 납작해진 과거 자료에서 원래 상속 의도를 추정하지 않는다.
- archive가 최신 이름·색·관계·잠금을 역덮어쓰지 않는다. v4의 미지 필드는 실제 sourceSchema=4로 보존한다.
- nativeSourceVersion은 읽기 때의 안내용이며 문서 bytes/semantic equality에 포함되지 않는다.

M2에서 보존한 자료의 보호를 통째로 풀지 않는다. 정해진 모양의 표시 데이터와 비어 있는 미지원 컬렉션,
알려진 출처 필드만 명시적 참조/효과 경계로 인정한다. 새로운 필드/알 수 없는 하위 구조는 비문자 속성 편집 제한을 유지한다.
지명·분포 등이 있는 모든 실사용 웹 파일의 전 기능 편집을 이 단계가 보장하는 것은 아니다.

## 화면 스포이트의 정확한 범위

`platform/screencolorpicker.*`는 사용자 버튼으로 요청할 때만 `QScreen::grabWindow(0)`으로 각 화면을
메모리에서 캡처하고 선택한 픽셀의 sRGB HEX를 반환하는 어댑터다. 파일 저장·전송 기능은 없다.
논리 좌표/캡처 픽셀 크기 비율, 여러 화면, Escape 취소, 캡처 후 화면 구조 변경 취소,
늦게 전달된 완료 이벤트 폐기, 화면 창 수명을 분리했다. 문서에는 사용자 지정의 적용 전까지 반영하지 않는다.
Windows/X11 기능 경로를 두고 Android/Wayland/offscreen 등 지원하지 않는 환경에서는 버튼을 숨긴다.

**실제 전체 화면 색상 추출은 이 환경에서 검증하지 못했다.** Xvfb/X11 실행은 Qt xcb 플러그인의
`libxcb-cursor.so.0` 의존성이 없어 창 시작 전에 실패했다. 공통 CTest의 screen_color_tests 통과는
지원하지 않는 offscreen 환경에서 기능을 노출하지 않고 시작을 거절하는 검사만 의미한다.
이 결과를 Windows 화면 추출·멀티모니터·실제 DPI 검증으로 확대하지 않는다.

## 검증 증거

Linux Qt 6.8.3, `QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`.

- 시작 기준 전체 CTest: 19/19.
- 원본 고정 30개 JavaScript 파일의 Git blob hash 검증.
- 원본 실행 ↔ 실제 C++/QML JS 엔진 차등 검사: **2,330개 입력/시나리오** 일치.
  색 변환 2,009개, JS trim 26개, 날짜 쌍 289개, 다중 단계 명령 시나리오 6개.
  명령 시나리오 안에는 메타데이터/상속/잠금/동일색/반복 빈 이름/Undo/Redo가 들어 있다.
- 기존 선택 reducer 원본 대조 2,066회는 회귀 묶음으로 유지.
- 최종 전체 CTest: **23/23 통과**, 실패·건너뛰기 0. 원문은 `final-results.xml`과 `final-tests.log`.
- 새 실제 QML 속성 시나리오 10종 × PC 1100px / 모바일 모드 360px = **20개**.
  마우스·합성 touch·한글 IME commit 이벤트를 사용했다. 실제 Android IME 테스트는 아니다.
- AddressSanitizer + UndefinedBehaviorSanitizer: 코어/명령/모델/작업/속성/원본 대조 8개 묶음.
  Qt 시스템 라이브러리의 leak 판정과 혼동하지 않도록 `detect_leaks=0`; 누수 검증 완료가 아니다.
- 메모리 실패 주입: prepare 59, confirm 11, replace 31, history 정규화 38개 위치에서 상태 보존.
- 사용자 이름과 오류는 웹 textContent처럼 PlainText로 표시하며 HTML 태그를 해석하지 않는다.
- 초기 RED와 회귀 실패 수정 로그를 검증 ZIP에 함께 보존한다. 실패 테스트를 빼거나 skip하지 않았다.

원본의 기존 단위 테스트 일부를 함께 실행했을 때 country-display의 기존 기대 문구 불일치
1건이 있었다(12 pass / 1 fail). 원본 코드는 고치지 않았으며 해당 원본 테스트 전체 통과라고 표시하지 않는다.

### 실행하지 못한 검증

Chromium의 원본 DOM fixture 실행은 최초 loopback 페이지에서 `ERR_BLOCKED_BY_ADMINISTRATOR`로 차단됐다.
브라우저 정책을 우회하거나 다른 주소/라우트로 다시 실행하지 않았다. 준비한
`tools/m32-browser-contract.py`는 재실행용 검증 도구이며, 실행 통과한 검증 증거가 아니다.
배포된 실제 웹사이트 전체 UI, 스포이트 실화면, Windows 네이티브, Android 빌드/실기기,
네이티브 IME·물리 Back·실제 회전·다중 모니터/DPI는 미검증이다.

## 재실행

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
node tools/m32-property-oracle.mjs build/property_probe
```

개발용 browser-contract 도구는 Playwright와 시스템 Chromium이 필요하다. 관리자 제한이 있는 환경에서
보안 설정을 낮추어 실행하는 방법을 제공하지 않는다. 실제 화면 추출 테스트는 허용된 Windows/X11
테스트 데스크톱에서 `screen_color_tests`를 실행해야 한다.
