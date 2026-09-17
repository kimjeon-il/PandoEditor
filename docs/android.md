# Android 개발 빌드

첫 대상은 **Android 35 x86_64 에뮬레이터용 Debug APK**입니다.
일반 ARM64 휴대폰용 APK, Play Store 배포, 릴리스 서명은 이번 범위가 아닙니다.
Windows와 같은 공통 코어·지도 데이터 5개국·버전 2 프로젝트를 사용합니다.
전체 세계지도는 마지막 단계에 추가합니다.

## 도구와 경로

| 도구 | 버전 |
| --- | --- |
| Qt Android / Windows 호스트 도구 | 6.8.3 android_x86_64 / mingw_64 |
| Java | Temurin JDK 17.0.20.1+1 |
| Android command-line tools | 22.0 (15859902) |
| NDK | r26b / 26.1.10909125 |
| Compile / target SDK | 35 |
| 최소 API | 28 |
| Build tools | 35.0.0 |
| Android Gradle Plugin | 8.6.0 (Qt 제공 템플릿) |
| Gradle | 8.10 (Qt 제공 wrapper) |
| Platform tools / Emulator | 37.0.1 / 37.1.11 (설치 시점) |
| Android 35 x86_64 기본 이미지 | revision 2 |

NDK 버전은 설치된 Qt의 `qt.toolchain.cmake`에 기록된 빌드 도구와 맞췄습니다.
[Qt Android 설치 문서](https://doc.qt.io/qt-6.8/android-getting-started.html)와
[Qt 6.8.3 Gradle 템플릿](https://github.com/qt/qtbase/blob/v6.8.3/src/android/templates/build.gradle)을 기준으로 구성합니다.

기본 위치는 `%USERPROFILE%/AppData/Local/Pandoeditor/android`입니다.
JDK, SDK, AVD, Gradle 캐시, 빌드 결과를 이 디렉터리 아래에 둡니다.
Qt는 기존 `%USERPROFILE%/Qt`를 사용합니다. 전역 PATH·기존 Java 설정은 변경하지 않습니다.
스크립트의 `-AndroidRoot`, `-QtRoot`, `-BuildTools`로 경로를 바꿀 수 있습니다.
현재 스크립트는 앞서 설치한 Qt Windows 호스트 및 Python/aqt/CMake/Ninja 환경을 전제로 합니다.

## 설치 및 빌드

프로젝트 폴더의 PowerShell에서:

```powershell
# SDK 라이선스는 설치 중 표시됩니다. -AcceptLicenses는 동의한 경우에만 사용합니다.
./tools/install-android.ps1 -IncludeSystemImage
./tools/build-android.ps1
```

설치 스크립트는 JDK/명령행 도구 다운로드의 SHA-256을 검증합니다.
SDK Manager와 aqt는 각 저장소 메타데이터로 패키지를 설치합니다.
SDK 설치 시작 전 12 GiB 이상의 여유 공간을 요구합니다.
한글 소스 경로는 Qt 도구 호환성을 위해 `android/source` junction으로 연결하며,
소스를 복사하거나 기존 웹 프로젝트를 참조하지 않습니다.

빌드 결과 기본 위치:

```text
%USERPROFILE%/AppData/Local/Pandoeditor/android/build-x86_64/app/android-build/build/outputs/apk/debug/android-build-debug.apk
```

Debug 서명은 개발용이며 배포용 키가 아닙니다. APK·키·SDK는 저장소에 넣지 않습니다.
Android 크로스 빌드에서는 Windows용 테스트 실행 파일을 만들지 않습니다.
테스트는 기존 Windows 빌드 디렉터리에서 실행합니다.

2026-09-17 최종 검증 결과:

- APK 크기: 25,974,960 bytes (24.77 MiB)
- SHA-256: `8CD24CE75ADF36E51A16AB8641F6DB6738540CD0542CA7B7408C57B9950D8257`
- Debug APK Signature Scheme v2 서명 정상
- 패키지 `org.pandolab.pandoeditor`, ABI `x86_64`, 최소 API 28, target/compile API 35
- Windows 공통 테스트 `core_tests`, `storage_tests`, `editor_tests`, `ui_tests` 4/4 통과

## 에뮬레이터

```powershell
./tools/run-android.ps1 -StartEmulator
# 이미 실행 중인 x86_64 에뮬레이터를 지정하려면:
./tools/run-android.ps1 -DeviceSerial emulator-5580
```

스크립트는 `Pandoeditor_API35` AVD를 사용하며, 자동 실행은 화면 없는 개발 검증용입니다.
이 PC에 해당 AVD를 생성했고 목록에 표시되는 것을 확인했습니다. 생성 도구가 이미지의
`devices.xml` 누락 경고를 출력했지만 Pixel 5 설정 생성 및 종료 코드는 성공했습니다.
임의의 연결 기기에 앱을 설치하지 않도록 serial을 명시적으로 지정합니다.
가속 검사에 실패하면 실행을 중단하며 Windows 기능·BIOS·드라이버 설정은 바꾸지 않습니다.

2026-09-17 이 PC 검사: `HypervisorPresent=False`, 에뮬레이터 `-accel-check` 결과는
`Android Emulator hypervisor driver is not installed on this machine`입니다.
따라서 Windows 가상화 플랫폼 등 적절한 가속 환경 구성과 필요 시 재부팅은 별도 작업입니다.
APK 빌드 성공은 실제 Android 조작 검증을 의미하지 않습니다.

## 모바일 파일 흐름

- 가져오기: 시스템 파일 선택기에서 읽고 전체 형식/도형 검증을 마친 뒤 현재 프로젝트를 교체합니다.
- 기기에 저장: 앱 전용 공간의 현재 프로젝트 한 개에 원자적으로 저장합니다. 다음 실행 때 복원합니다.
- 내보내기: 먼저 기기에 저장한 뒤 시스템 선택기로 외부 문서에 내보냅니다.
- 시스템 선택 취소/읽기 실패는 현재 작업을 교체하지 않습니다.
- 내보내기 실패에도 내부 저장본은 유지됩니다. 외부 제공자는 원자적 저장을 보장하지 않습니다.
- 내부 저장은 명시적인 저장 동작입니다. OS 강제 종료까지 보호하는 자동 저장은 아닙니다.
- 앱 삭제/데이터 초기화 시 내부 저장이 지워질 수 있으므로 보관·공유할 작업은 내보내세요.

`content://`는 로컬 경로로 변환하지 않고 Qt의 Android 파일 엔진을 통해 읽고 씁니다.
외부 문서에 QSaveFile을 적용하지 않습니다. 관련 제한은 [QFile Android 안내](https://doc.qt.io/qt-6.8/qfile.html)에 있습니다.
Qt가 기본 추가하는 `WRITE_EXTERNAL_STORAGE`는 매니페스트에서 `maxSdkVersion="27"`로 재정의했습니다.
최소 API가 28이므로 지원 대상 기기에서는 이 권한이 적용되지 않습니다. APK에는
`READ_EXTERNAL_STORAGE`와 `MANAGE_EXTERNAL_STORAGE`가 없습니다.

Qt 6.8에는 공개 safe-area API가 없어 Android 15 테마에서 edge-to-edge 강제를 해제했습니다.
[Qt 안내](https://doc.qt.io/qt-6.8/android-manifest-file-configuration.html)의 대응 방식이며,
Qt 또는 target API를 올릴 때 화면 여백 처리를 다시 검증해야 합니다.

## 실제 Android 검증 체크리스트

아래 항목은 에뮬레이터/기기에서 통과하기 전까지 **미검증**입니다.

- 설치·실행과 5개국 지도 렌더링
- 터치 선택·이동·핀치, 국가 속성/레이어, Undo/Redo
- 세로/가로 회전 중 선택·입력 초안 유지
- 한글 IME 입력과 키보드가 열린 상태의 패널 스크롤
- 시스템 선택기 v1 가져오기, 내부 v2 저장과 종료 후 복원
- 한글 파일명으로 내보내기와 재가져오기
- 선택 취소, 저장 실패, 뒤로 가기 저장/버리기/취소
- 실제 Android SAF 제공자의 truncate/flush 동작과 앱 재실행 복원

Windows의 모바일 모드 테스트는 공통 제어 흐름만 검증하며 SAF 제공자·Android IME·물리 터치를 대체하지 않습니다.
ARM64 실기 APK와 스토어용 서명도 이번 단계 범위 밖입니다.
