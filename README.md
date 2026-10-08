# Pandoeditor

판도연구소의 C++ / Qt Quick 크로스플랫폼 지도 편집 앱 기반입니다.
기본 세계지도에서 영토를 선택하고 색을 바꾸며,
국가·하위단위·지방의 이름·메모·명시/상속 색상·잠금과 지방 유효기간을 편집합니다.
이전 Qt 전용 레이어·RGB·불투명도 조작은 호환 메뉴에 분리되어 있습니다.
모든 편집은 공통 Undo/Redo와 프로젝트 저장/열기에 연결됩니다.
스냅샷 작업·취소·세션 수명은 [M1.4 기록](docs/qt-job-implementation.md),
원본 기능을 변형하지 않는 이식 기준은 [웹 동등성 계약](docs/web-parity-contract.md)을 참조합니다.

프로젝트 파일은 Qt v1–v8을 읽고 **v8로 저장**합니다. 기존 파일은 열기만으로 변경되지 않으며,
v8 저장본은 v7까지만 지원하는 이전 앱에서 열 수 없습니다. 기존 파일을 남기려면 다른 이름으로 저장하세요.
웹 완전 저장본은 schema v3–v6을 읽습니다. 분포는 일반 숫자 값·단위·자동/수동 색 농도 범위와
겹쳐 보기/활성 레이어 하나 보기를 사용합니다. 기존 비율 값은 `%`·수동 0–100 범위로 전환합니다.
합병·편입은 대상 하나를 고정한 뒤 제공 영역을 선택하며, 분할은 작은 결과를 기본 새 영역으로
선택합니다. 미리보기의 이전 단계로 돌아가도 초안은 유지됩니다. 기존 객체의 콘텐츠 속성은
필드별로 확정·Undo/Redo하고, 객체 생성과 도형 변경은 하나의 트랜잭션으로 확정합니다.
공통 영토 모델과 마이그레이션 범위는 [M1.1–M1.2 기록](docs/qt-v3-implementation.md)에 정리했습니다.

## 개발 브랜치·워크트리 운영

웹·앱은 각각 `work/ui`, `work/objects`, `work/gis`, `work/places` 대분류 브랜치를
유지합니다. 로컬에서 각 브랜치의 상설 워크트리를 **한 번 생성해 계속 재사용**하고,
작업할 때마다 새 워크트리를 만들거나 작업 완료 후 자동 삭제하지 않습니다.
상세 절차는 [브랜치·워크트리 운영 정책](docs/branch-policy.md), 에이전트 필수 규칙은
[AGENTS.md](AGENTS.md)를 참고하세요.

## 현재 웹 기준 작업 화면

웹 `28e37086d7851bd3368afd86cf5a1c03c66ff2bb` 기준으로 상단 48px 한 줄 메뉴,
파일 작업 팝업, 하단 지도 명령 바, 국기·이름 선택 카드와 왼쪽 320px 상세 편집창을 사용합니다.
좁은 화면에서는 편집창을 하단으로 옮기며 검색과 도형 도구가 지도 클릭 영역을 가리지 않게 전환합니다.
정보·편집·관계 탭과 밝은/어두운 테마는 같은 컨트롤러 상태를 표시하고 기존 확정·Undo 계약을 유지합니다.
Qt 전용 레이어·기존 속성은 파일 메뉴, 지명·수계·분포와 역사 라이브러리는 추가 메뉴에 남깁니다.

`ui_tests::canonicalWorldShellCapture`는 실제 기본 세계 데이터를 소프트웨어 렌더러로 캡처합니다.
`webShellLayoutAndMetadata`는 PC/360px 배치, 메뉴 경로, 이름·메모 Undo, 도형 도구 겹침을 검사합니다.
`PANDOEDITOR_UI_CAPTURE_DIR`로 캡처 폴더를 지정할 수 있습니다. 이는 실제 GPU/PC 강제 종료 해결 검증이 아닙니다.
GIS·라이브러리 등 내부 대화상자와 일부 관계 설정은 기존 Qt UI를 유지하므로 완전한 픽셀 동등성은 아닙니다.

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
- 투명도 슬라이더와 RGB·레이어 속성의 잔여 초안은 적용 버튼으로 함께 확정하며 Undo 한 번으로 되돌립니다. 이는 현재 Qt 프로토타입 경로이며 웹의 모든 해당 UI와 동등하다는 뜻은 아닙니다. 적용의 후보 검증은 백그라운드에서 실행됩니다.
- 이름은 Enter 또는 포커스 이동 시, 메모는 편집 후 포커스 이동 시 각각 독립적으로 확정합니다. 다른 필드 초안은 그대로 둡니다. 실제 웹의 name/notes change 동작에 대응합니다. 저장 전에는 남아 있는 초안을 확정합니다.
- 계산 중 작업 취소는 입력 초안을 보존합니다. 별도 초안 취소는 미확정 입력을 버립니다. 미확정 초안이 있으면 Undo/Redo 전에 적용 또는 취소가 필요합니다.
- 저장 또는 Ctrl+S: 프로젝트 저장. 열기 또는 Ctrl+O: 저장된 프로젝트 열기.
- 800px 미만에서는 국가 패널이 하단으로 이동합니다. 화면 크기를 바꿔도 편집 상태는 유지됩니다.
- 저장하지 않은 변경이 있으면 열기·종료 전에 저장/버리기/취소를 선택합니다.

파일은 `.pando.json` 확장자의 버전 8 JSON이며 지도 도형·객체 속성·레이어 순서와 표시 상태를 함께 보관합니다.
기존 버전 1–7 파일은 열 수 있습니다. 저장하면 v8이 되며 열기만으로 원본 파일을 변경하지 않습니다.
Qt 문서는 포함된 도형을 자체 보관합니다. **웹 프로젝트 가져오기**는 웹 일반 완전 저장본 및 자동저장 완전본(schema 3–6)을 읽어 Qt v8로 저장할 수 있는 문서로 변환합니다. delta 저장본에는 기본 자료가 필요하므로 거절합니다. 미지원 객체/표현/외부 자료는 보존·제한 상태를 보고하며, 웹 전체 편집 기능이 지원된다는 뜻은 아닙니다.

웹 가져오기 보고서를 확인하고 기존 작업을 저장하거나 버린 뒤 확정합니다. 준비·취소·저장 실패 시 기존 초안과 이력을 유지합니다. 확정하면 새 세션과 빈 Undo 이력의 미저장 문서가 됩니다. 원본 웹 파일에 덮어쓰지 말고 Qt 파일로 다른 곳에 저장합니다. [M2 구현·검증·제한](docs/qt-web-import-implementation.md)을 참조하세요.
예제는 실행 시 새 프로젝트로 표시되며, 편집 전에는 미저장 변경이 없는 상태입니다.
프로토타입은 입력과 보존 데이터를 포함한 출력 파일 크기를 각각 64 MiB로 제한합니다. 현재 웹 영토 매핑은 2차원 Polygon/MultiPolygon이며 추가 Z/M 좌표는 버리지 않고 거절합니다. 세계지도·날짜변경선·국경 편집은 범위 밖입니다.

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

## M3.1 선택 UI

개발 브랜치에는 **검색 탭, Ctrl/⌘ 다중 선택, 겹친 영토 객체 선택창, 선택 객체로 이동**이 연결되어 있습니다.
검색·선택·hover·지도 이동은 편집 이력을 만들지 않으며, 입력 중 다른 객체를 선택해도 초안은 원래 객체에 남습니다.
검색 이름 행의 일반 선택은 검색을 닫고, Ctrl/⌘ 선택은 검색을 유지합니다. Shift 검색은 고정 웹 원본처럼 범위 선택을 수행하지 않습니다.

이름·메모의 일반 필드 확정은 계속 독립적입니다. 하위단위·지방은 검색·선택 가능하지만 속성·관계 편집 확장은 후속 단계입니다.
전체 표시/공유 경계/지구 투영의 동등성이나 Android 실기기 지원 완료를 뜻하지 않습니다.
[구현 범위·검증·제한](docs/qt-selection-ui-implementation.md)을 확인하세요.

## M3.2 속성·잠금

웹 원본 `58e4087`의 종류별 필드 change, 색상표 65색, 사용자 지정 HEX/RGB/HSL,
명시 색상 제거와 상속, 객체별/다중 잠금, 지방의 연도/일자 입력을 연결했습니다.
이름·메모는 필드별로 확정하며 사용자 지정 색상만 별도 적용/취소를 사용합니다.
선택 툴바와 수동으로 여닫는 편집창은 기존 Qt 호환 UI와 구분합니다.

v4는 국가의 원래 이름과 명시 이름, `color: null` 상속 상태를 보존합니다.
이전 Qt 파일의 색은 명시값으로 유지하며 과거 상속 의도를 archive에서 재추정하지 않습니다.
구조·국기·전체 표시 기능은 후속 단계입니다. 미해석 데이터 보호는 그대로 유지됩니다.

[구현·검증 및 플랫폼별 제한](docs/qt-property-implementation.md)을 확인하세요.
원본 함수/Qt 테스트와 실제 웹 브라우저 대조는 다른 검증입니다.
