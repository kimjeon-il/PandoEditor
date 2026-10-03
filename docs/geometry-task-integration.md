# 지도 편집 작업창 — 3단계

1·2단계 변경을 유지한다. 4단계 전체 스타일 작업은 포함하지 않는다.
기준 웹은 로컬 world-map b2c7bb9와 같은 배포 HTML이다.
웹의 작업명·단계·대상·안내·검토/확정 구역을 적용한다.
현재 웹은 최소화 버튼을 숨기지만, 사용자 요청에 따라 앱에서는 접기/펼치기를 제공한다.

| 실제 작업 | 대상 및 입력 | 기존 명령 연결 |
| --- | --- | --- |
| 모양 편집 / 다시 그리기 / 새 객체 그리기 | 고정 대상, 정점 초안 | beginGeometryEdit / beginGeometryDraw / beginContentGeometry |
| 콘텐츠 이동 | 고정 콘텐츠, 객체 끌기 | geometrySetMoveMode 및 기존 drag 명령 |
| 병합 | 고정 대상, 지도에서 제공 영역 선택·목록 제외 | geometryToggleProvider / requestGeometryPreview |
| 편입 | 제공 영역 선택 → 영역 그리기 | geometryAdvanceStage / geometryBack |
| 분할 | 절단선, 새 객체로 남길 결과 | geometryAddPoint / geometryChooseSplitResult |
| 공유 경계 | 두 소유자, 경계 초안 | beginSharedBoundaryGeometry |
| 해안선 | 고정 대상, 해안선 초안 | beginCoastlineGeometry |

공통 표시는 MapView의 taskPanel에만 둔다. 작업별 세션·검증·계산·확정 명령은
기존 EditorController에 그대로 두고, geometryEditState에 실제 대상 이름을 추가했다.
영토 생성 준비·소속 변경·삭제의 별도 구조 대화상자는 이 단계 범위에서 바꾸지 않는다.

- setup / 제공 영역 선택 / 지도 편집 / 계산 / 검토 상태에 맞춰 가능한 버튼을 표시한다.
- 검토 중에는 초안 편집 버튼을 숨긴다. 분할에 폴리곤 점 삭제를 표시하거나 호출하지 않는다.
- 오류는 작업창 안에서 원문을 표시한다. 추정 면적이나 가짜 진행률은 추가하지 않는다.
- 접기는 표시 상태만 변경한다. 초안·제공 영역·미리보기·계산은 유지하며 작업 종료 때 초기화한다.
- 이전/취소/확정은 기존 명령 의미를 유지한다. 새 객체의 geometry 취소는 기존 속성 초안을 보존한다.
- 지도 전체 크기를 유지하며 데스크톱은 최대 380px, 작은 창은 가용 너비에 맞춘다.
  긴 대상 목록은 본문만 스크롤하고 작업명·확정 영역은 밖에 둔다.

검증: property_ui_tests의 geometryTaskWorkflow(desktop-1100/mobile-360)는 오류 표시,
접기/복원 초안 보존, 미리보기→이전→재검토→확정, undo/redo, 제공 영역 제외,
준비 단계로 복귀, 취소 후 분할 재진입을 실제 QML 입력으로 검사한다.
기존 territorial_geometry_tests는 각 작업의 모델 검증을 담당한다.
Windows 소프트웨어 렌더링 캡처는 로컬 Temp에만 저장한다. 장시간 GPU 부하 시험은 하지 않는다.
## 실제 검증 결과

- `cmake --build C:/Users/taeeu/Qt/Pandoeditor-build --target pandoeditor property_ui_tests territorial_geometry_tests ui_tests view_navigation_tests -j 2`: 성공.
- `QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`: property_ui_tests 전체 32 통과, territorial_geometry_tests 전체 23 통과, view_navigation_tests 전체 10 통과.
- ui_tests의 webShellLayoutAndMetadata, viewAndAppearanceControlsMatchDesktopAndCompact, webFieldsAndAsyncApplyCancelAcrossPcAnd360px, editingFlow: 6 통과(초기화/종료 포함).
- `QT_QPA_PLATFORM=windows`, `QT_QUICK_BACKEND=software`: geometryTaskWorkflow, integratedContentFields, integratedObjectKinds, overlayAndHierarchicalMenus: 10 통과.
- `git diff --check`: 오류 없음. 기존 LF→CRLF Git 안내만 발생.
- 최초 회귀 검사에서 기존 검색 행의 고정 대기와 폐기된 editorPanel 탐색이 실패했다. 비동기 행 생성 대기 및 통합 objectPropertyPanel 탐색으로 검사 경로를 수정하고 위 최종 검사에서 모두 통과했다.
- 로그: `C:/Users/taeeu/AppData/Local/Temp/pando-step3-{property-final,geometry-final,camera,shell-final,windows}.txt`.
- 실화면: `C:/Users/taeeu/AppData/Local/Temp/pando-step3-windows-captures/step3-review-{desktop,mobile}.png`, 육안 확인 완료.

미실행: 대규모 세계 지도에서 모든 작업의 수동 반복 검증, 장시간 GPU 부하 시험.
작은 fixture의 실제 Windows 창 검증과 기존 작업별 모델 시험을 수행했다.
GPU/커널 강제 종료 원인은 이 작업으로 해결했다고 주장하지 않는다.