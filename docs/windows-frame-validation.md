# Windows 32px 프레임 검증 (2026-09-18)

## 구현 범위

- `WindowsFrame`가 QML 창의 HWND 생성/해제/재생성과 네이티브 이벤트 필터를 소유한다.
- `Qt.Window`의 시스템 스타일을 유지하고 `WM_NCCALCSIZE`로 제목 영역만 클라이언트로 확장한다. DWM 테두리/그림자와 Windows 11 모서리 정책을 사용한다.
- 최대화 클라이언트 영역은 해당 모니터의 작업 영역 안으로 제한한다.
- 제목줄 32, 버튼 46×32는 **논리 픽셀**이다. 제목은 시스템 제목 글꼴, 아이콘은 벡터 도형이다.
- 실제 QQuickItem 위치를 client 좌표로 매핑해 hit-test한다. 최대화는 `HTMAXBUTTON`, 제목은 `HTCAPTION`, 8방향 가장자리는 Windows resize hit를 반환한다.
- QML의 수동 창 위치 계산과 투명 ResizeHandle을 제거했다. 이동·복원 드래그·크기 조절·시스템 메뉴는 Windows가 담당한다.
- 마우스 캡션 버튼은 네이티브 capture/release 경로, 키보드/접근성은 QML 경로를 사용하되 모두 `invoke()`에서 실행한다. `SC_CLOSE`도 QQuickWindow::close()를 거치므로 기존 미저장 확인을 우회하지 않는다.
- Qt Win32 dispatcher는 큐의 입력을 결과 포인터 없이 필터에 전달한다. 이를 임시 결과 저장소로 처리한다. NULL 포인터 기록으로 발생했던 실제 마우스 종료 오류를 수정하고 회귀 테스트를 추가했다.
- 네이티브 초기화 실패 시 custom caption을 숨기고 기본 시스템 프레임으로 복귀한다. 이유는 `fallbackReason`과 `WindowsFrame: using system frame:` 경고에 기록한다.
- Android/비 Windows/offscreen에서는 기본 창을 사용한다. 코어·저장 형식·지도/편집 패널 테마는 변경하지 않았다. 하단 파일 안내문의 높이 계산만 실제 텍스트 높이 기준으로 바꾸어 네이티브 resize 시 순환 경고를 제거했다.

## 검증 구분

검증 환경: Windows 11, Qt 6.8.3 MinGW, 1920×1080 단일 디스플레이, 시스템 DPI 96 (100%).

### 자동 테스트

- 최종 소스 기본 CTest **6/6 통과** (18.19초): core/model/migration/storage/editor/ui.
- 별도 Windows 프레임 테스트: 제목줄/버튼 논리 크기, QML 배치, `HTMAXBUTTON`·제목·모서리 hit, 네이티브 버튼 release, 최대화 작업 영역, 최대화/복원/최소화, 미저장 종료 취소, 기본 프레임 복귀/재연결, HWND destroy/create 후 재연결.
- client 물리 좌표 → 논리 좌표 변환을 1.0/1.25/1.5로 검사한다.
- `QT_QPA_PLATFORM=windows`, `QT_SCALE_FACTOR=1/1.25/1.5`로 프레임 테스트를 각각 실행한다. 이는 Qt 배율 강제 테스트이며 Windows 디스플레이 설정을 실제 125/150%로 바꾼 검증은 아니다.
- 기존 저장/재열기·Undo 회귀 테스트도 기본 CTest에 포함된다.
- 최종 네이티브 프레임 단독 실행은 세 Qt 배율 모두 **실패 0**. 로그: 빌드 폴더의 `frame-final-1.txt`, `frame-final-1.25.txt`, `frame-final-1.5.txt`. 일반/최대화 클라이언트 캡처에도 같은 배율 접미사를 붙였다.

### 실제 Windows 입력/화면에서 확인

- 일반 창의 둥근 모서리/테두리 및 최대화 후 모서리/여백 변화.
- 최소화, 최대화, 복원, 제목줄 더블클릭, 일반 창 이동, 최대화 창 끌어 복원.
- 좌/우/위/아래 및 네 모서리의 실제 resize: 각 20px 드래그에 맞춰 크기가 변경됨.
- Alt+Space 시스템 메뉴, 닫기 버튼과 Alt+F4의 미저장 확인, 취소 후 편집 유지, Undo 후 정상 종료.
- 화면 왼쪽 가장자리로 끌어 **960×1032, 원점 (0,0)**에 실제 스냅 배치됨. 작업 표시줄 영역은 제외됨.
- 일반/최대화 화면은 실제 앱 캡처로 확인했다. 자동 테스트의 `titlebar-normal.png`/`titlebar-maximized.png`는 QQuickWindow 클라이언트 렌더링 캡처이며 OS 그림자 캡처와 구분한다.

## 남은 검증 / 알려진 별도 문제

- **Snap Layouts hover 메뉴 자체와 메뉴에서 영역을 클릭하는 경로는 확인되지 않았다.** `HTMAXBUTTON` 응답과 가장자리 스냅 성공을 메뉴 검증 완료로 간주하지 않는다. 이 단계는 사용자 환경에서 추가 확인이 필요하다.
- 물리 디스플레이 125/150%, 서로 다른 DPI의 다중 모니터 이동, 자동 숨김/다른 위치의 작업 표시줄, Windows 10은 미검증이다.
- DWM 실패 시 복귀 코드는 구현했고 어댑터 해제/재연결을 테스트했지만, OS DWM을 강제로 중단하는 실험은 하지 않았다.
- Windows backend로 **UI 전체**를 추가 실행하면 기존 `recoveryDialog`의 `implicitHeight` 순환 경고 때문에 `mobileStorageFlow`, `attributesAndLayers`, `editingFlow`의 무경고 assertion이 실패한다 (**8 passed / 3 failed**, init/cleanup 포함). 프레임 단독 테스트와 기본 offscreen CTest 결과와 구분한다. 복구 대화상자는 이번 변경에서 재설계하지 않았다.
- Android 실행/APK 갱신은 이번 Windows 프레임 작업에 포함하지 않는다.

## 재검증 명령

기존 로컬 toolchain PATH 설정 후:

```powershell
cmake --build C:/Users/taeeu/Qt/Pandoeditor-build-android-final-20260917 -j 4
ctest --test-dir C:/Users/taeeu/Qt/Pandoeditor-build-android-final-20260917 --output-on-failure
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QUICK_BACKEND = 'software'
$env:QT_SCALE_FACTOR = '1.25' # 1 / 1.25 / 1.5 각각
./ui_tests.exe desktopTitleBarKeepsWorkspaceBelowLargeWindowFrame -o frame-native.txt,txt
```

포터블 갱신 위치: `dist/windows-portable/pandoeditor.exe`. 이전 실행 파일은 같은 폴더의 날짜가 붙은 `.bak` 사본으로 보존한다. 배포 ZIP에서는 모든 `.bak` 파일을 제외한다.

최종 로컬 EXE: **17,307,651 bytes**, SHA-256 `DD53F228E8115C98A52DD7346A64B6129CC54F216B4181141D984B9409D17208`. 빌드 결과와 포터블 파일의 해시 일치를 확인했다. 백업: `pandoeditor.exe.before-native-frame-20260918-215136.bak`.

GitHub Release 태그는 `v0.1.0-windows-native-frame`, 배포 자산은 `Pandoeditor-0.1.0-windows-native-frame.zip`이다. ZIP은 **41,191,536 bytes**, SHA-256 `317A719DAB0437FFA4257D42C0C244ABC9BA72932914B5136810E43A273061F5`이며 1,391개 항목에 `.bak` 파일이 없음을 확인했다.

## 참고

- [Microsoft: custom titlebar와 Snap Layouts](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-snap-layout-menu)
- [Microsoft: DWM custom frame](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)
- Qt 6.8.3 `qeventdispatcher_win.cpp`와 `qwindowscontext.cpp`: queued input의 NULL result와 입력 메시지 필터 중복 배제 계약.
