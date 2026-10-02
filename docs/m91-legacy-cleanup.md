# M9.1 Legacy Cleanup

## 범위와 기준선

`codex/integration`의 M9.0 `7eba14dc9a7d484b92bc1bc35d06eb8b00bca894`를 기준으로,
중복 소스, forwarding header, 사용처가 없는 API/helper만 제거했다.
구현 전에 로컬 `33c0117`에서 이 커밋으로 fast-forward했다.

M9.1 코드 변경은 49개 파일(수정 29개, 삭제 20개)이며 이 보고서를 포함하면 50개다.
공통 `engine/` 구현, 저장 형식, 사용 중인 API, QML 동작과 테스트 assertion은 변경하지 않았다.
새 wrapper, 대체 엔진, frame pipeline, dirty/delta, GPU 수명, cache, label,
editing 또는 성능 최적화는 추가하지 않았다. 최초 구현·검증 단계에서는 커밋, push, 배포,
portable 재패키징을 수행하지 않았다. 이후 사용자 요청으로 M9.1만 통합 브랜치에
커밋·푸시하고 Windows portable 및 GitHub integration preview 배포를 진행한다.

## 정본 의존성

```text
Pandoeditor::Core
  ↑ PUBLIC
Pandoeditor::MapEngine = pandoeditor_map_engine
  구현: engine/src/
  공개 include: engine/include/pandoeditor/map/
  PRIVATE include: third_party/earcut/
  ↑ PUBLIC
pandoeditor_editor (app 및 Qt renderer/adapter)
  ↑
pandoeditor / Qt 회귀 테스트

엔진 단위 테스트 → Pandoeditor::MapEngine → Pandoeditor::Core
```

앱·renderer·테스트의 공통 엔진 include 31개를 `<pandoeditor/map/...>`로 통일했다.
`pandoeditor_editor`의 불필요한 PRIVATE earcut include만 제거했다.
실제 triangulation 구현을 빌드하는 MapEngine의 PRIVATE earcut 의존성은 유지했다.
Qt adapter가 필요한 기존 app/renderer include 디렉터리와 링크 의존성은 유지했다.

`renderer/mapprojection.*`, `maprenderitem.*`, `gpumapitem.*`, scenegraph backend,
`canonicalpacket.*`, `countrymesh.*`는 사용 중인 Qt 구현/adapter이므로 유지했다.
특히 `countrymesh.h`는 정본 타입을 포함하면서 `QByteArray` decoder를 선언하는 실제 adapter다.
사용 중인 signal도 유지했다.

## 삭제 대상과 근거

| 삭제 대상 | 근거 |
|---|---|
| `app/mapscenebuilder.h` | 정본 헤더만 포함하는 forwarding header. 호출부를 정본 include로 전환했다. |
| `app/mapscenebuilder.cpp` | CMake에서 빌드하지 않는 이전 복제품. 빌드되는 구현은 `engine/src/mapscenebuilder.cpp`다. |
| `renderer/countryculling.h/.cpp` | 헤더는 forwarder, cpp는 빌드되지 않는 복제품. 정본은 engine에 있다. |
| `renderer/geometrypacketcache.h/.cpp` | 동일. |
| `renderer/mapviewstate.h/.cpp` | 동일. |
| `renderer/projectionengine.h/.cpp` | 동일. 실제 Qt `MapProjection`은 별도로 유지했다. |
| `renderer/renderlod.h/.cpp` | 동일. |
| `renderer/renderpacket.h/.cpp` | 동일. |
| `renderer/renderquality.h/.cpp` | 동일. |
| `renderer/renderscene.h/.cpp` | 동일. |
| `renderer/scenepatch.h/.cpp` | 동일. |

| 제거한 API/helper | 사용처 확인 및 유지한 동작 |
|---|---|
| `EditorController::setScopedObjectVisibility` | M9.0의 선언·정의 2건만 존재. production/test/QML/문자열 호출 없음. 실제 사용하는 `toggleSelectionVisibility`와 core `SetScopedVisibility`는 유지했다. |
| 익명 namespace의 `cutCross` | 정의 1건, 호출 0건. 나머지 geometry 계산은 그대로다. |
| `MapUploadScheduler::enqueue`, `takeForFrame`, `cancelGeneration` | 선언·정의와 내부 queue 구현만 존재. 실제 소비자는 `beginFrame`, `reserve`, `frameBytes`를 사용한다. 다른 클래스의 `enqueue`는 유지했다. |
| `RenderUpload`, `UploadBatch`, `queued_`, `generation_` | 위 미사용 queue API에만 속하는 타입/상태. `generation_`는 `beginFrame`에서 쓰기만 했다. |

`beginFrame(uint64_t, size_t)` 시그니처, budget 계산, 보호 geometry와 단일 큰 node의
진행 보장, `reserve` 및 `frameBytes`의 동작은 유지했다.

## 수정 파일과 테스트

아래 경로는 이 저장소 루트를 기준으로 한다. 삭제 파일 20개는 위 표에 기재했다.

- app 수정 7개: `CMakeLists.txt`, `autosavecoordinator.h`, `editorcontroller.h`,
  `editorpresentation.cpp`, `geometrycalculator.cpp`, `mapscenebridge.h`, `worlddatasetloader.h`.
- renderer 수정 11개: `geographicimagemesh.cpp/.h`, `gpumapitem.cpp`, `maprenderitem.cpp/.h`,
  `scenegraph/mapmaterial.h`, `scenegraph/mapscenenode.h`, `terrainprovider.cpp/.h`,
  `uploadscheduler.cpp/.h`.
- tests 수정 11개: `m72_cpu_scene_adapter_tests.cpp`, `m72_packet_cache_tests.cpp`,
  `m72_packet_corpus_probe.cpp`, `m72_projection_tests.cpp`, `m72_render_packet_tests.cpp`,
  `m72_render_scene_tests.cpp`, `m72_scene_builder_tests.cpp`, `m72_scene_editor_tests.cpp`,
  `m72_view_pick_tests.cpp`, `m72_view_state_tests.cpp`, `map_render_tests.cpp`.

테스트 변경은 include 전환뿐이다. M9.0과 각 테스트 파일을 include 및 줄바꿈 차이를
제외하고 비교해 assertion·테스트 본문·skip 조건이 동일함을 확인했다.
등록된 CTest 이름 112개와 순서도 M9.0과 동일하다. 테스트를 삭제하거나 skip을 추가하지 않았다.

## 플랫폼별 검증

검증일: 2026-10-02. Windows x64, GCC 13.1, Qt 6.8.3, Ninja, CMake,
Debug `-O0 -g1`(assertion 활성), Qt `offscreen` 및 `software` 설정을 사용했다.

| 검증 | 결과 |
|---|---|
| 변경 전 M9.0 전체 Qt Debug configure/build | 통과. 아래 resource cache 재사용 조건 적용. |
| 변경 전 CTest 및 regression audit | **112/112 통과, 실패 0, skip 0**. CTest 291.83초. |
| 변경 후 전체 Qt Debug configure/build | 통과. 모든 target 빌드 완료. 아래 resource cache 재사용 조건 적용. |
| 변경 후 기존 CTest 112개 및 regression audit | **112/112 통과, 실패 0, skip 0**. CTest 258.59초. 변경 전과 같은 112개 이름/순서이며 Qt `SKIP :`도 0건. |
| 변경 후 Qt/QML 자동 검증 | 통과. `ui_tests`, `web_import_ui_tests`, `property_ui_tests`, `selection_ui_tests` 등 기존 suite의 실제 실행 결과. |
| 변경 후 full hydro fixture | `hydro_full_dataset_tests` 통과. optional gate를 skip하지 않았다. |
| Windows 앱 미빌드 엔진 검증 | `PANDOEDITOR_BUILD_APP=OFF`, 엔진 6개 target build 및 **6/6 통과, 실패 0, skip 0**. 6.52초. |
| Linux configure/build/CTest/QML | **미실행**. WSL 미설치 및 Docker 없음. Windows 결과를 Linux 통과로 처리하지 않았다. |
| 제거된 경로의 실행 코드·테스트·QML·CMake·문자열 참조 | **0건**. 보고서의 삭제 목록은 이 수치에 포함하지 않는다. |
| 이전 forwarding include 및 삭제한 고유 symbol | **0건**. |
| 공통 engine 구현 변경 | **0개 파일**. |

앱 미빌드 검증은 `map_engine_boundary_tests`, `map_camera_tests`, `map_picker_tests`,
`viewport_resource_scheduler_tests`, `builtin_hydro_channel_tests`, `label_engine_tests`다.
기존 core 테스트 등록 때문에 이 구성에서도 Qt Core package를 찾지만, 이 6개 엔진 target은
MapEngine/Core만 링크한다. 전체 headless suite 또는 Linux 실행 결과로 확대해 해석하지 않는다.

full fixture는 웹 저장소의 고정 커밋 `c0bd31d13dc8495593d78cf51f7cc195de7c9469`에서
Git archive로 추출했다. hydro v0.13.1 manifest/metadata와 그것이 참조하는 v0.13.0
index/shard, historical pilot을 공급했다. 10개 fixture의 Git 내용이 일치한다
(JSON 3개는 Git archive의 CRLF를 LF로 정규화해 대조; binary는 그대로 대조).
환경 변수 `PANDOEDITOR_HYDRO_FULL_MANIFEST`와 `PANDOEDITOR_HISTORICAL_FULL_PILOT`를 설정했다.
full hydro 테스트는 953 index tile, 5,173 logical feature, 16,548 metadata feature,
실제 viewport frame과 edit geometry 로드를 검증한다.

## 환경 조건과 검증 제한

1. 한글 소스 경로에서 Qt qmlimportscanner가 실패해 ASCII 경로의 검증 소스를 사용했다.
   GCC/Qt/runtime은 유지하고, 로컬 검증 링크만 별도 LLD를 사용했다. 저장소 CMake에
   이 환경 설정을 넣지 않았다.
2. 116,753,124바이트 `qrc_world_data.cpp`는 초기 M9.0 Debug 빌드에서 성공적으로 컴파일됐다.
   이후 재컴파일 시 GCC가 약 2GB allocation을 확보하지 못했다. 초기 M9.0의 정상 object를
   변경 전·후 동일하게 재사용했다. 생성 CPP SHA256 `3edeecbd0b999ee860588721880ae2b6a7572bc75991400b74c1e93a93c1b2d1`,
   object SHA256 `587a5ff662eefd45b429ddf03e4bab99d2f876b55380bc1020101caa3f22eb1d`를
   전후 확인했다. resource object는 Debug `-g`, 나머지는 `-O0 -g1`이며 `NDEBUG`는 없다.
   모든 app/renderer/test 소스는 M9.1 분리 소스에서 다시 빌드했다.
   따라서 결과는 동일 resource object를 사용한 전체 target 빌드이며, resource까지 새로
   컴파일한 clean build 통과를 주장하지 않는다.
3. 중단된 로컬 빌드에서 `webprojectgeopackage.cpp.obj`가 손상되어 link가 실패한 적이 있다.
   해당 task 전용 object만 제거·재컴파일해서 해결했다. M9.0 source/test 실패는 없었다.
   Python audit의 CP949 출력 문제도 UTF-8 설정으로 해결했으며 audit 자체는 통과했다.
4. 구현 도중 같은 작업트리에 다른 채팅의 startup/autosave 변경이 들어왔다.
   그 변경의 builder 인자 누락으로 혼합 소스 빌드가 실패했으므로, 해당 변경을 보존하고
   **M9.0 + M9.1 49개 변경만 적용한 별도 소스**에서 검증했다.
   실제 작업트리의 추가 변경 전체에 대한 통과를 주장하지 않는다.
   겹치는 파일은 `app/CMakeLists.txt`, `app/autosavecoordinator.h`, `app/editorcontroller.h`,
   `app/editorpresentation.cpp`, `app/worlddatasetloader.h`이며 M9.1과 별도 변경을 함께 보존했다.
5. 기존 `build/`와 `dist/`를 M9.1 빌드·배포에 사용하거나 수정하지 않았다.
   모든 M9.1 빌드/fixture/cache는 저장소 밖 task 전용 경로에 저장했다.
   작업 도중 별도 portable 작업으로 기존 `dist/windows-portable/pandoeditor.exe`의
   hash 변경 및 교체를 관측했다. 기존 산출물이 계속 동일했다는 주장은 하지 않는다.
   시작 시 executable SHA256은 `1ea895f137ca9df6ab2c3defce958081c1e0d3d069b6d8c9d8649ffa882a8ec8`,
   최초 구현·검증 종료 시 작업트리 portable은 `5d9254647d8ea07fefd37e0cbb8307a935a49c79cbf5d222a0d5f7b36250b241`이었다.
   기존 executable과 같은 hash의 사본은
   `C:/Users/taeeu/AppData/Local/Pandoeditor/integration-33c0117-release-20260928/portable/pandoeditor.exe`
   및 그 폴더의 build/zip-verify 사본에 남아 있음을 확인했다. 이 사본도 수정하지 않았다.
6. Qt/QML 결과는 offscreen/software 자동 검증이다. 실제 Windows 화면의 D3D/OpenGL,
   물리 Android, Linux 결과를 대신하지 않는다.

## 증거 파일

Qt 검증 폴더:
`C:/Users/taeeu/AppData/Local/Pandoeditor/m91-debug-ascii-20261002/`

- `baseline-results.xml`, `baseline-LastTest.log`, `baseline-audit.log`.
- `post-configure.log`, `post-editor-build.log`, `post-build.log`, `post-ctest.log`,
  `post-results.xml`, `post-LastTest.log`, `post-audit.log`.
- `baseline-test-registration.json`, `post-test-registration.json`.
- `scope-files.json`, `scope-audit.json`, `m91-only.patch`(M9.1만의 diff), `regression-comparison.json`.
- `fixture-provenance.json`, `resource-cache/provenance.json`, `linux-environment.json`,
  `preserved-artifact-hashes.json`, `artifact-check.json`.

분리 검증 소스:
`C:/Users/taeeu/AppData/Local/Pandoeditor/m91-validation-source/`

앱 미빌드 검증 폴더:
`C:/Users/taeeu/AppData/Local/Pandoeditor/m91-headless-20261002/`
(`configure.log`, `build.log`, `ctest.log`, `results.xml`).
