# 지도 오버레이·메뉴 통합 — 2단계

1단계 변경을 보존하고 지도·패널 배치와 메뉴만 변경한다.
작업창 구조(3단계), 전체 스타일(4단계)은 변경하지 않는다.

- MapView는 작업 영역 전체 크기를 유지한다. 패널을 열거나 닫아도
  viewport 크기, 카메라 중심·확대·이동·투영 원점이 달라지지 않는다.
- 상세·레이어 패널은 지도 위 오버레이다. 패널의 빈 부분에서도 지도 클릭/휠을 차단한다.
- 작은 창의 도구는 하단 패널 위로 피한다. 좁은 데스크톱에서는 패널과 겹칠 때만
  도구/검색의 가로 위치를 보정한다. 지도 카메라는 이동시키지 않는다.
- 보기는 비모달 계층형 메뉴다. 투영법 / 객체 종류 / 지형 / 선택 객체를
  아이콘으로 나누고, 세부 페이지에서 이전 메뉴로 돌아간다.
  하나의 팝업을 공유하므로 하위 메뉴가 창 밖으로 나가거나 다른 팝업과 겹치지 않는다.
- 표시, 경계, 색상, 이름, 국기, 불투명도, 강/호수, 분포 겹침/단일/활성 레이어/경계,
  라벨 고정/자동 위치와 선택 객체 숨김을 기존 컨트롤러 명령으로 보존한다.
- Esc/뒤로는 세부→상위→닫기 순서다. 오른쪽 방향키로 하위 메뉴에 진입한다.
  하위 메뉴 진입 시 뒤로 버튼, 닫기 시 보기 버튼으로 포커스를 돌린다.
- 파일 / 보기 / 추가는 한 번에 하나만 연다.
- 파일 메뉴: 새 프로젝트, 열기·저장·가져오기, GIS, 객체 상세 편집, 레이어 관리 그룹.
  새 프로젝트는 기존 미저장 저장/버리기/취소 확인을 재사용하며 기본 프로젝트로 초기화한다.
- 추가 메뉴: 국가/하위단위/지방, 분포/항목, 지명/강/호수, 라이브러리 그룹.
  콘텐츠 종류별 버튼이 1단계 상세 편집 패널에서 바로 생성 초안을 연다.
- 기존 고급 영토 RGB·불투명도·구조 작업은 상세 패널 내부의 고급 속성으로 이동했다.
  별도 레이어 관리 화면은 레이어만 표시한다. 구조 확인 대화상자는 하나만 생성한다.

기준 웹: b2c7bb991bb793a06d5cbdc44e8ae0b56a208122.
데스크톱 웹의 옆으로 뜨는 메뉴 대신, 앱에서는 한 팝업 안에서 하위 페이지로 이동한다.
픽셀 동등성이 아닌 계층·옵션·동작과 안전한 360px 배치를 이번 단계의 검증 대상으로 삼는다.
## 검증

- 앱 및 property_ui_tests / ui_tests / view_navigation_tests 빌드 성공.
- 전체 속성 UI: 30 통과, 실패 0.
- 셸·보기 옵션·비동기 적용/취소·구조 작업: 8 통과, 실패 0.
- 카메라 단위 검사: 10 통과, 실패 0.
- 실제 Windows 창의 오버레이/계층 메뉴/1단계 편집 흐름: 10 통과, 실패 0.
- 1100px/360px, 추가로 800px/360px × 560px 창에서 반복 열기/닫기와 도구 회피 검사.
- 카메라 전체 상태 비교, 패널 뒤 클릭 차단, 비모달·비딤 확인,
  오른쪽 방향키·Esc·포커스, 팝업 배타성, 하위 메뉴 재진입, 보기 옵션 변경,
  새 프로젝트 취소/버리기, 종류별 생성 진입을 실제 UI로 검사.
- git diff --check 성공. 1단계 수정과 사용자 덤프 보존. commit/merge/배포/release 없음.

명령:
cmake --build C:/Users/taeeu/Qt/Pandoeditor-build --target pandoeditor property_ui_tests ui_tests view_navigation_tests -j 2
property_ui_tests.exe (전체)
property_ui_tests.exe overlayAndHierarchicalMenus integratedContentFields integratedObjectKinds contentPanelSharedCommands
ui_tests.exe webShellLayoutAndMetadata viewAndAppearanceControlsMatchDesktopAndCompact webFieldsAndAsyncApplyCancelAcrossPcAnd360px territorialStructureDeleteDialogAcrossPcAnd360px territorialConversionSetupDoesNotMutateDocumentAcrossPcAnd360px territorialCreateSetupWaitsForPreparedGeometryAcrossPcAnd360px
view_navigation_tests.exe (전체)

테스트 로그: C:/Users/taeeu/AppData/Local/Temp/pando-step2-*.txt
로컬 실제 Windows 캡처: C:/Users/taeeu/AppData/Local/Temp/pando-step2-windows-captures/
step2-view-desktop.png / step2-view-mobile.png / step2-overlay-desktop.png / step2-overlay-mobile.png

한계: 실제 Windows 검사는 작은 fixture 및 기본 샘플 프로젝트로 수행했다.
전체 세계 데이터의 새 프로젝트 비동기 초기화 경로를 실제 사용자 창에서 수동 검증하지 않았다.
장시간 GPU 부하·PC/커널 강제 종료 원인 검사는 수행하지 않았다.
캡처의 외부 업로드 없음. 3~4단계 미시작.