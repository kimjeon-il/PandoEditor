# 편집 UI 통합 — 1단계

기준: 앱 9eb6d68 / codex/integration, 웹 main b2c7bb991bb793a06d5cbdc44e8ae0b56a208122.
2026-10-03 배포 웹 index.html은 원격 고정 커밋과 줄바꿈 정규화 후 일치했다.
웹 property-editor-bindings.js의 change → 필드별 commit 계약을 기존 앱 content.edit 명령에 연결한다.

| 객체 | 같은 상세 패널에서 편집하는 필드·동작 |
| --- | --- |
| 국가·영토 | 기존 이름·메모·잠금·지역 유효기간, 국기 기본/없음/이미지 업로드, 국가 수도 |
| 지명 | 이름, 메모, 종류, 지도 위치 편집, 삭제 미리보기 |
| 사용자 수계 | 이름, 메모, 종류, 색상, 잠금, 도형 편집, 삭제 미리보기 |
| 기본 수계 | 편집용 복사 후 사용자 수계 필드로 진입 |
| 분포 레이어 | 이름, 색상, 단위, 자동/수동 색 농도 범위·최소/최대, 상위 레이어, 잠금, 유효기간 |
| 분포 항목 | 값, 레이어, 영토 참조/독립 도형, 확실성, 유효기간, 도형 편집, 삭제 미리보기 |
| 일반 GIS 객체 | 이름, 메모, 색상, 잠금, 도형 편집, 삭제 미리보기 |

구현 순서:
1. ContentPanel의 기존 필드·명령을 ObjectPropertyPanel의 정보 탭에 배치.
2. 선택한 콘텐츠의 기존 세션을 열고, 객체 전환·선택 해제 시 이전 세션을 취소.
   필드 확정과 국기 파일 입력에 객체 소유권 검사를 적용.
3. 추가 메뉴의 지명·수계·분포 경로를 같은 상세 창으로 연결하고 기존 Qt 콘텐츠 탭 제거.
4. PC/360px에서 기존 생성·미리보기·확정, 필드 저장, 선택 전환·취소·재진입 검증.

국기와 수도는 상세 패널 내부의 해당 버튼으로 필드를 펼친다.
기존 생성 및 도형 편집은 기존 트랜잭션 명령을 사용한다.
Qt 레이어/레거시 영토 RGB·불투명도 화면은 이번 범위가 아니므로 유지한다.
지도 축소 배치·보기 메뉴, 작업창, 공통 스타일 작업은 2~4단계로 남긴다.
새 웹 기능 전체 및 픽셀 동등성을 완료했다는 의미는 아니다.
## 검증 기록

- 실제 Qt 6.8.3/MinGW 빌드: pandoeditor, property_ui_tests, ui_tests,
  territorial_geometry_tests, command_editor_tests 성공.
- property_ui_tests 전체: 28 통과, 실패 0 (offscreen/software).
- territorial_geometry_tests 전체: 23 통과, 실패 0.
- command_editor_tests 전체: 9 통과, 실패 0.
- ui_tests의 셸/메타데이터·열기 초안·비동기 취소·삭제·편집 흐름: 7 통과, 실패 0.
- 실제 Windows 창/시스템 글꼴/software의 통합 입력·저장·추가 검사와 캡처도 수행.
  실제 입력으로 지명 이름/메모/종류, 수계 이름/메모/색상, 분포 값/확실성/단위를 확인.
  국기 없음 및 수도, 객체 전환, 선택 해제, 취소/재진입, 저장 시 미확정 필드 확정을 확인.
  생성/미리보기 상태의 저장 거절도 확인.
- 기존 사용자 덤프 파일 보존. commit/merge/배포/release 없음.

빌드 명령:
cmake --build C:/Users/taeeu/Qt/Pandoeditor-build --target pandoeditor property_ui_tests ui_tests territorial_geometry_tests command_editor_tests -j 2

이 PC의 설정된 실행 파일 출력 경로:
D:/Pandoeditor-mechanism-bin-20261003/pandoeditor.exe

로그: C:/Users/taeeu/AppData/Local/Temp/pando-step1-*.txt
실제 Windows 캡처:
C:/Users/taeeu/AppData/Local/Temp/pando-step1-windows-captures/step1-content-desktop.png
C:/Users/taeeu/AppData/Local/Temp/pando-step1-windows-captures/step1-content-mobile.png

미실행: 전체 실세계 데이터의 수동 편집, OS 파일 선택창을 통한 실제 국기 이미지 업로드,
장시간 GPU 부하 및 PC/커널 강제 종료 원인 재현. 실제 Windows 검사는 작은 fixture 문서를 사용했다.