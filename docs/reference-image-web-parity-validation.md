# 참조 이미지 웹 누락 기능 검증

앱 기준: `3934077519bb716cbb45b683bbb63d85dcc8d5ee`  
검증 코드 커밋: `184fccbc21995893ad4d1bd86592ca43cd227f08`  
고정 웹: `ebcfae4d27b29cbbea6416a7045a4806930204be`  
작업 브랜치: `codex/reference-image-web-parity`

이 문서와 실행 로그를 추가하는 최종 커밋은 위 코드 커밋에 문서만 추가한다. 실행 후 코드·fixture·runtime을 변경하지 않는다. 일반 Release 빌드이며 패키징·main 병합·배포는 하지 않는다.

## 구현과 실행 범위

ReferenceImageLibrary가 로컬 이미지와 배치 이력을 소유한다. geographicPoints의 ID·정규화 UV·경위도, anchor, mapQuad, cornerPinEnabled를 보존하고 기존 pixel controlPoints·배치 값도 유지한다. 프로젝트/GIS 교환 스키마 및 계약은 변경하지 않았다. 새 이미지 투명도는 웹 기본값 0.55이며 저장된 기존 값은 보존한다.

Qt QJSEngine에서 고정 웹 production 계산 모듈을 사용한다. 원본 5개 Git blob은 그대로 보존한다. ES2016 번들은 esbuild 0.25.10으로 생성하며 실제 앱과 worker가 같은 번들을 실행한다. 비교 expected 7+3+3건은 최초 원본 계산 결과로 고정되었으며 보정 결과에 맞춰 재생성하지 않았다.

| 순서 | 기능 | 최종 실행 상태 |
|---|---|---|
| 1 | 기준점 생성·양 끝 이동·삭제·전체 삭제, 5개 모드 및 오차 표시 | PASS |
| 2 | 네 모서리·anchor·TPS, 로컬 복원·flat/globe 매핑 | PASS |
| 3 | Live Wire 시작·점·마지막 취소·완료·적용·재시도·취소와 기존 확정 | PASS |
| 4 | 현재 초안 선 보정·지도 미리보기·적용 한 단계 Undo·취소 | PASS |
| 5 | 반전·초기화·회전 수치·잠금 및 프로젝트와 분리된 이력 | PASS |
| 6 | 화면 1px/Shift10px 방향키, 포커스·모달·계산·미리보기 차단 | PASS |
| 7 | Space 이동 및 키 해제·포커스·창 비활성화·포인터 취소 복귀 | PASS |

저장 실패의 state/history 보존, 원본 핀 좌표와 calibrated texture 일치, 제약 제거 후 이전 표시 위치 유지, 수정 단계 전환 후 line history 초기화, 오래된 worker/프로젝트/초안 요청 거부를 포함한다. 초안 import/export는 native 저장 좌표인 경위도를 유지한다. 화면 좌표와 overlay 좌표는 기존 변환 어댑터를 통해서만 변환한다.

## 환경과 원시 증거

Windows 11 Home 10.0.26200, Qt 6.8.3, MinGW GCC13.1.0, NVIDIA GTX1650 드라이버32.0.15.9186. 네이티브 검사 offscreen, 실제 창 검사 Windows 플랫폼 및 요청한 RHI d3d11, 창1100×760. 측정용 portable/runtime 복사 없이 설치된 Qt를 PATH로 사용했다.

- [일반 production 및 검사 빌드](evidence/reference-image-web-parity/build.txt): exit0.
- [네이티브 discovery](evidence/reference-image-web-parity/native-discovery.txt): 20개 기능 slot.
- [네이티브 실행](evidence/reference-image-web-parity/native.txt): 20개 기능 + init/cleanup =22 PASS, 실패0, skip0, exit0.
- [고정 원본·expected 검증](evidence/reference-image-web-parity/source.txt): 5개 원본 파일 및13개 사례, mismatch0, 각 process exit0.
- [화면 discovery](evidence/reference-image-web-parity/window-discovery.txt): 최종 선택 목록은 아래 명령에 고정.
- [실제 Windows 창 실행](evidence/reference-image-web-parity/window.txt): 필수7 흐름 + line history/menu 회귀2 =9개 기능, init/cleanup 포함11 PASS, 실패0, skip0, 누락0, exit0. 201887ms. 선택9개를 discovery와 실제 PASS 목록으로 대조했다.
- [독립 리뷰](evidence/reference-image-web-parity/review.md): 6개 Important 수정 후 Critical/Important 잔여0. 리뷰 자체는 실행 증거로 쓰지 않음.
- [fixture·source manifest·runtime·실행 binary SHA256](evidence/reference-image-web-parity/hashes.json).
- 원본 파일별 SHA256은 [source manifest](../app/reference-web/manifest.json)에 고정.
- 단계별 최초 실패와 재실행은 [진행 기록](reference-image-web-parity-progress.md)에 남겼다. 과거 실패를 최종 PASS로 덮어쓰지 않았다.

## 재현

```powershell
$env:PATH='C:\Users\taeeu\Qt\Tools\mingw1310_64\bin;C:\Users\taeeu\Qt\6.8.3\mingw_64\bin;'+$env:PATH
Set-Location 'D:\dev\worktrees\reference-image-web-parity\Pandoeditor(App)'
& 'C:\Users\taeeu\AppData\Local\Pandoeditor\installer\Scripts\cmake.exe' --build D:\build\Pandoeditor-reference-parity --target pandoeditor reference_image_tests edit_display_ui_tests -j2
if($LASTEXITCODE -ne 0){throw 'build failed'}
$env:QT_QPA_PLATFORM='offscreen'
& D:\build\Pandoeditor-reference-parity\reference_image_tests.exe
if($LASTEXITCODE -ne 0){throw 'native failed'}
$env:QT_QPA_PLATFORM='windows';$env:QSG_RHI_BACKEND='d3d11'
& D:\build\Pandoeditor-reference-parity\edit_display_ui_tests.exe geographicCalibrationActualWindowFlow cornerPinAndAnchorActualWindowFlow liveWireActualWindowToDraftFlow lineRefinementActualWindowPreservesDraftUntilApply placementControlsActualWindowRemainLocal keyboardVertexMovementUsesScreenPixelsAndFocusGuards spaceTemporaryPanReturnsToEditingAcrossCancellation referenceLineHistoryHasVisibleRedoAndResetsWithMethod referenceMenusKeepActionsOrderAndSafeOwnership:desktop-1100
if($LASTEXITCODE -ne 0){throw 'window failed'}
```

고정 소스 검증은 Node로 tools/reference-parity/verify-source.mjs에 웹 checkout 경로를 전달하고 calibration-fixtures.mjs, trace-fixtures.mjs, refine-fixtures.mjs를 각각 실행한다. `--record`를 사용하지 않는다. 원본은 웹 checkout 현재 branch가 아니라 지정 SHA의 Git blob과 비교한다.

## 남은 차이·미실행

- 1024px 초과 이미지의 Qt Smooth downsampling과 browser canvas, ICC/디코더 픽셀 동일성: NOT RUN. 현재 비교 corpus는 동일 PNG RGBA 입력의 알고리즘 검증이다.
- 지리적 이미지의 non-normal blend(Multiply/Screen/Difference)를 지도 framebuffer와 합성하는 웹 수준 효과: 기존 geographic 경로에서 미지원/미검증. 저장된 blend 값은 보존한다. 이 작업에서 지원 완료로 주장하지 않는다.
- 상세 날짜변경선·horizon 시각 품질과 대용량 이미지 응답성: 별도 장치 acceptance 미실행. globe 실제 frame과 공유 지리적 매핑 검사를 상세 시각 품질 전체 증거로 확대하지 않는다.
- 전체 CTest, M9.8 장시간 성능, Android: 이번 승인 범위에서 실행하지 않음.

실행된 7개 기능과 고정 계산 corpus의 결과만 판정한다. 모든 이미지·렌더링 조건의 완전한 웹 parity를 주장하지 않는다.
