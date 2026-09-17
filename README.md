# Pandoeditor

판도연구소의 C++ / Qt Quick 크로스플랫폼 지도 편집 앱 기반입니다.
독일·폴란드·체코·오스트리아·슬로바키아 지도를 선택하고 색을 바꾸며,
이름·메모·RGB 색상·불투명도와 레이어를 편집할 수 있는 프로토타입입니다.
모든 편집은 공통 Undo/Redo와 프로젝트 저장/열기에 연결됩니다.

프로젝트 파일은 Qt v1/v2/v3를 읽고 **v3로 저장**합니다. 기존 파일은 열기만으로 변경되지 않으며,
v3 저장본은 이전 앱에서 열 수 없습니다. 기존 파일을 남기려면 다른 이름으로 저장하세요.
공통 영토 모델과 마이그레이션 범위는 [M1.1–M1.2 기록](docs/qt-v3-implementation.md)에 정리했습니다.

## 구성

- `core/`: Qt와 OS에 의존하지 않는 C++ 공통 코어
- `app/`: Qt 앱 실행 및 코어 연결
- `renderer/`: 지도 투영과 표시용 경로 생성
- `assets/`: 독립된 예제 지도와 출처·해시
- `ui/common/`: 공통 QML 루트 창
- `ui/desktop/`: 넓은 화면·좁은 화면 공통 작업 화면
- `docs/architecture.md`: 계층별 책임과 확장 원칙

버전은 최상위 `CMakeLists.txt`의 `project(VERSION ...)`에서 관리합니다.
코어의 `pandoeditor::version()`과 앱 제목에 동일한 값이 사용됩니다.

## Windows 빌드

필요한 도구:

- CMake 3.21 이상
- C++17 컴파일러: Visual Studio 2022의 C++ 데스크톱 개발 도구 등
- Qt 6.5 이상의 데스크톱 개발 키트와 Qt Quick
- Qt 키트와 일치하는 컴파일러 및 CPU 아키텍처

### 이 PC에 설치한 MinGW 환경

Qt 6.8.3과 MinGW 13.1은 `C:\Users\taeeu\Qt`에, CMake와 Ninja는
`C:\Users\taeeu\AppData\Local\Pandoeditor\installer\Scripts`에 설치했습니다.
전역 PATH 변경 없이 PowerShell에서 다음 명령으로 빌드합니다.
Qt의 QML 검색 도구가 한글 경로를 잘못 해석하는 현상을 확인하여,
`C:\Users\taeeu\Qt\Pandoeditor-source`를 원본 프로젝트로 연결하는 junction을 만들었습니다.
이는 복사본이 아니며, 어느 경로로 수정해도 같은 파일입니다.
빌드 결과는 영문 경로 `C:\Users\taeeu\Qt\Pandoeditor-build`에 둡니다.

```powershell
$env:PATH = "C:\Users\taeeu\Qt\6.8.3\mingw_64\bin;C:\Users\taeeu\Qt\Tools\mingw1310_64\bin;C:\Users\taeeu\AppData\Local\Pandoeditor\installer\Scripts;" + $env:PATH
cmake -S C:/Users/taeeu/Qt/Pandoeditor-source -B C:/Users/taeeu/Qt/Pandoeditor-build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:/Users/taeeu/Qt/6.8.3/mingw_64" -DCMAKE_CXX_COMPILER=g++
cmake --build C:/Users/taeeu/Qt/Pandoeditor-build
& C:\Users\taeeu\Qt\Pandoeditor-build\pandoeditor.exe
```

### 다른 PC에서 Visual Studio 사용

Visual Studio 2022 x64 개발 환경에서 실행합니다. 아래 Qt 경로는 실제 설치 경로로 바꿉니다.
MinGW 빌드와 구분되는 별도 빌드 폴더를 사용합니다.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/<version>/msvc2022_64"
cmake --build build-msvc --config Debug
$env:PATH = "C:\Qt\<version>\msvc2022_64\bin;" + $env:PATH
.\build-msvc\Debug\pandoeditor.exe
```

빌드 결과는 지정한 별도 빌드 폴더에 생성합니다. 소스 폴더 안에서 직접 CMake 빌드를 실행하지 않습니다.
이 명령은 개발 환경에서 실행하기 위한 것이며, 배포 패키지 생성은 아직 구성하지 않았습니다.

## Qt 없이 코어만 빌드

CMake와 C++17 컴파일러가 있으면 Qt 설치 없이 공통 코어만 빌드할 수 있습니다.

```powershell
cmake -S . -B build-core -G Ninja -DCMAKE_CXX_COMPILER=g++ -DPANDOEDITOR_BUILD_APP=OFF
cmake --build build-core --config Debug
```

## 개발 방향

Windows에서 공통 코어와 편집 흐름을 먼저 검증하고, 같은 코어를 사용하는 Android UI를 확장합니다.
이후 macOS/iOS, 필요 시 Linux를 검토합니다. 현재 모바일 빌드와 플랫폼 지원을 검증한 상태는 아닙니다.
기존 웹 프로젝트와 독립된 저장소입니다. 지도 데이터 일부의 독립 사본만 포함하며,
앱 실행과 빌드에는 웹 저장소가 필요하지 않습니다.

## 편집하기

- 국가 클릭/탭: 선택. 빈 영역 클릭/탭: 선택 해제.
- 드래그: 이동. 휠·핀치·확대/축소 버튼: 배율 변경. 전체: 화면 맞춤.
- 색상 팔레트: 선택 국가 색 변경. 취소/다시 또는 Ctrl+Z/Ctrl+Y: 편집 이력 이동.
- 국가 탭: 국가 목록 선택, 이름·메모·RGB 색상·불투명도·소속 레이어 편집. 투명한 국가도 목록에서 선택할 수 있습니다.
- 레이어 탭: 추가·이름 변경·위/아래 이동·숨김·잠금·불투명도·빈 레이어 삭제. 마지막 레이어는 삭제할 수 없습니다.
- 잠긴 레이어의 국가는 지도에서 선택하거나 수정할 수 없습니다. 목록에서 상태를 확인하고 레이어 탭에서 잠금을 해제합니다.
- 투명도 슬라이더는 드래그 중 미리보기하며 손을 놓을 때 Undo 한 번의 편집으로 확정합니다.
- 이름은 Enter 또는 포커스 이동 시, 메모는 포커스 이동 시 확정합니다. 저장 전에도 입력 중인 편집을 확정합니다.
- 저장 또는 Ctrl+S: 프로젝트 저장. 열기 또는 Ctrl+O: 저장된 프로젝트 열기.
- 800px 미만에서는 국가 패널이 하단으로 이동합니다. 화면 크기를 바꿔도 편집 상태는 유지됩니다.
- 저장하지 않은 변경이 있으면 열기·종료 전에 저장/버리기/취소를 선택합니다.

파일은 `.pando.json` 확장자의 버전 3 JSON이며 지도 도형·국가 속성·레이어 순서와 상태를 함께 보관합니다.
기존 버전 1/2 파일은 그대로 열 수 있습니다. 저장하면 v3가 되며 열기만으로 원본 파일을 변경하지 않습니다.
다른 파일에 의존하지 않습니다. 기존 웹판 파일 형식은 지원하지 않습니다.
예제는 실행 시 새 프로젝트로 표시되며, 편집 전에는 미저장 변경이 없는 상태입니다.
프로토타입은 파일 크기를 64 MiB로 제한합니다. 세계지도·날짜변경선·국경 편집은 범위 밖입니다.

## 테스트

위의 PATH 설정 후 다음을 실행합니다.

```powershell
ctest --test-dir C:/Users/taeeu/Qt/Pandoeditor-build --output-on-failure
ctest --test-dir C:/Users/taeeu/Qt/Pandoeditor-core-build --output-on-failure
```

`core_tests`는 Qt 없이 도형 선택·국가 속성·레이어 명령·편집 이력·저장 시점 복귀를 검사합니다.
`editor_tests`는 v1 변환·v3 JSON 왕복, 한글 경로와 메모, 잘못된 레이어 참조·파일·저장 실패 시 상태 보존을 검사합니다.
`model_tests`는 공통 관계·기간·참조 검증과 도형 공유를, `migration_tests`는 v1/v2 보존·v3 왕복과 미해석 JSON 보존을 검사합니다.
`ui_tests`는 화면 조작, 입력 중 화면 폭 변경, 드래그의 Undo, 레이어 UI와 미저장 확인창을 검사합니다.
또한 겹치는 도형으로 국가·레이어 투명도 합성과 순서·숨김을 픽셀 검증합니다.
화면 캡처는 빌드 폴더의 `app/desktop.png`, `app/compact.png`에 생성됩니다.
레이어 화면은 `app/layers-desktop.png`, `app/layers-compact.png`에 생성됩니다.
Android 빌드와 실물 터치 기기의 핀치 동작은 아직 검증하지 않았습니다.

## 지도 출처와 재추출

Natural Earth 5.1.1 1:50m 국가 데이터에서 5개국을 추출했습니다.
좌표는 그대로 유지하고 국가 이름·ID·초기 색상만 앱 형식으로 정리합니다.
원본·추출본 SHA-256과 이용 조건 링크는 `assets/provenance.json`에 있습니다.

```powershell
./tools/extract-sample.ps1 -Source '../map editor/assets/data/countries-ne-5.1.1-50m.geojson'
```

이 명령은 개발 중 데이터를 재생성할 때만 사용합니다. 일반 빌드에서는 실행하지 않습니다.
