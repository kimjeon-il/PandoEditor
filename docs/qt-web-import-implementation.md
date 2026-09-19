# M2 — 웹 완전 저장본 가져오기

## 기준과 완료 범위

2026-09-19. `codex/m2-web-import`는 M1.4 `91dbd31313922cdd98b89318dc4a077e73b802e2`를 이어 받는다. 앱 main 기준 `6ae7d807bd54dd27cca4a8d2c530340ec25601b5`의 Windows 프레임과 M1 작업을 유지한다. 앱 main 병합·배포본 갱신·웹 저장소 변경은 수행하지 않는다.

원본은 `kimjeon-il/world-map`의 `58e4087f85aa51884bc4ab80959e05010d94d7d5`다. 이 단계는 M2.1 형식 판정, M2.2 migration, M2.3 매핑/보존/보고, M2.4 후보 확정, M2.5 재열기/보존 장벽을 구현한다. **가져오기 지원과 웹 지도 전체의 표시·편집 지원은 다르다.** M3 이후 기능을 이번 완료 범위에 포함하지 않는다.

## 지원하는 파일과 경계

| 입력 | 처리 |
|---|---|
| `pandolab-project-state`, schema 3·4·5 | 실제 웹 일반 완전 저장본 → schema 5 정규화 → Qt v3 후보 |
| `pandolab-autosave-full`, schema 3·4·5 | 자동저장 완전본을 같은 경로로 처리 |
| `pandolab-autosave-delta` 또는 full과 delta가 혼재된 입력 | `BASE_DATA_REQUIRED`로 거절. 기본 자료를 임의로 대체하지 않음 |
| `pandoeditor-project`, version 1·2·3 | Qt 파일로 판별. 기존 열기/가져오기를 사용하도록 분리 |
| 미래/구형 미지원 버전·모호한 형식·잘못된 JSON·중복 키 | 오류와 함께 후보 폐기. 기존 프로젝트는 유지 |

앱의 기존 저장 경계인 **입력 64 MiB, 변환·보존 후 Qt 저장 결과 64 MiB**를 모두 검사한다. 원본 archive 때문에 결과가 더 커질 수 있으며, 다시 열 수 없는 크기의 후보를 확정하지 않는다. JSON 중첩 제한은 기존 128이다. 현재 영토 모델의 2차원 Polygon/MultiPolygon만 매핑하며, 추가 Z/M 좌표는 버리지 않고 `UNSUPPORTED_GEOMETRY`로 거절한다. 도형 단순화·재투영·대체 데이터로 제한을 우회하지 않는다.

## 원본과 대조한 변환

`app/webmigration.cpp`는 다음 원본의 입력 경계를 옮긴다.

- `project-migrations.js`: 3→4, 레거시 국가 ID/이름 결정, editor_* 속성의 override 이관, 기존 override 우선, drawings/genericFeatures 변환과 그룹 별칭.
- `subunit-migration.js`: 4→5, territory/admin→subunit, 한정된 필드 제거, 객체 키·스타일·순서·가시성 변환. 메모 속 문자열을 일괄 치환하지 않는다.
- `country-feature.js`, `generic-feature-service.js`, `source-provenance.js`, `version-contract.js`: canonical 속성과 원본 출처 계약.
- `project-serializer.js`: 일반/자동저장 full과 delta 구분. `project-state.js` 및 실제 영토·분포 모델: 알려진 버전·ID·관계·입력 제약.

`tests/fixtures/web-import`의 v3/v4/v5/scalars 정답은 **C++ 결과가 아니라 수정하지 않은 원본 `migrateProjectToCurrent()`**로 생성했다. `provenance.json`에 원본 커밋과 파일별 SHA-256이 있다. `tools/verify-web-import-goldens.mjs`는 기본적으로 읽기 전용 검증이며 `--write`를 명시해야 정답을 갱신한다. 숫자 ID의 최단 십진 표현, 지수/고정 표기, BOM/NEL 등 문자열 trim 경계도 원본 정답과 비교한다.

미해석 큰 정수·소수·지수 토큰은 double을 거치지 않는다. JSON 객체 키 순서는 정규화하지만 배열 순서·값 타입·null·필드 미존재를 구분한다. 원본 문자열과 숫자 토큰 보존 검증은 JavaScript JSON.parse의 binary64 한계와 구분되는 별도 테스트다.

## 매핑과 보존

`app/webimport.cpp`는 국가·하위단위·지방의 ID, 모든 ring/part 좌표, 이름·메모·잠금, 기본/기간별 부모·소속국 관계를 기존 v3 모델에 매핑한다. canonical 문서와 전체 인덱스를 검증하고, 표시용 투영까지 후보 쪽에서 만든다. 알려진 참조 오류·순환·기간 충돌·잘못된 좌표는 실패다.

국가 이름·메모·색상은 실제 override 우선순위를 따른다. 명시 RGB를 유지하며 기본 색상은 웹 light 테마 어댑터의 `#cccccc`다. 웹의 테마는 이 저장 파일에 포함되지 않으므로 현재 Qt light 화면의 초기 해석이며 dark 테마 이식을 의미하지 않는다. 하위단위 스타일·객체별 표시·혼합·테두리·라벨 등은 원본 그대로 보존한다. 국가/하위단위/지방 레이어는 기존 표시기를 위한 의미 그룹 어댑터이며, 웹의 폴더나 사용자 계층을 임의로 평탄화한 것이 아니다.

국기는 **Default(필드 없음) / None(null) / Embedded(명시값)**를 구분해 보고한다. 국기 이미지·수도·지명·수계·분포·기타 객체·출처·외부 라이브러리 자료·표시 설정은 해당 기능을 편집할 수 있는 것처럼 바꾸지 않고 retained로 보관한다. 기본 국기 및 외부 자료 참조는 unavailable-reference로 표시하며 네트워크에서 자동 대체하지 않는다. 분포는 개별 share 0–100을 검사하되 **60+70을 허용**하고 합계를 100으로 고치지 않는다.

모든 root 필드는 mapped/archived/retained/unavailable-reference로 분류한다. 전체 원본은 `migrationArchive`, 미지원 fragment는 별도 extension과 의존성/금지 효과를 가진다. 원본 archive는 나중에 편집된 canonical 이름·메모 위에 다시 적용하지 않는다. 알 수 없는 의존성은 보수적으로 이름·메모 외 변경을 차단한다. 알려진 국기/수도와 분포 참조도 기록하며, 객체 목록에 ‘제한’과 이유를 표시한다.

웹의 엄격한 schema 검사와 달리 M2 보존 정책은 **알 수 없는 추가 필드를 삭제하거나 즉시 버전 승격하지 않고 보관**한다. 이것은 해당 필드를 지원한다는 뜻이 아니다. 알려진 필드가 잘못된 컨테이너/버전/참조를 가진 경우에는 거절한다. 원본 전체 객체의 장래 의미를 해석했다고 주장하지 않는다.

## 준비·확정·취소와 저장 실패

`app/editorwebimport.cpp`는 M1.4의 단일 실행 슬롯·latest-wins runner를 재사용한다. worker는 저장 경로·스냅샷·취소 토큰·독립 결과 상자만 가지며 가변 Controller/Project를 직접 읽지 않는다. 파일 읽기·migration·문서 검증·투영 생성은 UI 스레드 밖에서 수행한다.

준비하는 동안 현재 문서·초안·선택·revision·저장 기준·Undo/Redo·기존 명령 preview를 바꾸지 않는다. 확인된 source/candidate SHA-256과 원래 projectInstanceId/documentId/revision, 초안 변경 epoch를 기록한다. 보고 이후 편집·Undo/Redo·같은 파일 재열기가 있으면 `STALE_RESULT`로 거절한다. 잘못된 후보 hash는 적용되지 않으며 성공/취소 후 토큰을 재사용할 수 없다.

보고서에서 기존 작업을 ‘저장’ 또는 ‘버리기’로 명시한다. 저장을 선택하면 미확정 초안을 **임시 Project에만** 적용·검증하여 파일을 먼저 쓴다. 기존 live `save()`를 호출해 초안 이력부터 확정하지 않는다. 잘못된 초안, 저장 경로/쓰기 실패, 원본 덮어쓰기 시도는 문서·초안·Redo와 검토 후보를 보존한다. 취소도 동일하게 기존 작업을 유지한다.

성공 시 이미 준비된 Project·인덱스·CountryView·투영을 함께 교체한다. 새 projectInstanceId, revision 0, 빈 Undo/Redo로 시작하며 가져온 문서는 미저장 상태다. 원본 경로를 기본 저장 대상으로 지정하지 않고, 현재 세션에서 원본과 동일한 로컬 경로/URI로 저장·내보내기를 시도하면 막는다. 로컬 별칭은 canonical 경로로 비교한다. 가져오기 자체는 웹 원본에 쓰지 않는다. Qt 저장본을 웹 형식으로 역변환하는 exporter는 이 단계 범위가 아니다.

취소는 후보 적용을 막는 협력적 취소다. 파서 또는 파일 공급자의 동기 호출 중간을 강제 종료하는 기능은 아니며, 늦은 완료는 세션 검사를 거쳐 폐기한다. 완료 후 UI 통지는 state swap 뒤에 수행하며 통지 오류를 ‘기존 상태가 유지된 가져오기 실패’로 잘못 보고하지 않는다.

## PC·모바일 진입

공통 화면의 **웹 프로젝트 가져오기 → 파일 선택 → 변환/검증 → 보존·제한 보고 → 저장/버리기 후 확정 또는 취소**를 제공한다. 추가 보고서는 승인된 M2 전환기의 데이터 손실 방지 경계이며, 웹판의 일반 속성 편집에 새로운 필수 적용 단계를 추가하지 않는다.

파일 선택과 보고서가 이름·메모의 focus-out 확정을 유발하지 않도록 이 흐름에서만 해당 필드의 자동 확정을 잠시 보류한다. 취소하면 초안 그대로 복귀한다. 작업 중 진행 표시는 실제 진행률을 알 수 없는 indeterminate이고 가짜 백분율을 표시하지 않는다. Escape/Back·작업 취소도 같은 취소 경로를 사용한다.

360px에서도 보고서 경로/설명은 줄바꿈되고 확정·취소 버튼을 모두 사용할 수 있다. QML의 실제 버튼 클릭과 키 입력을 시험했다. native 파일 공급자의 파일 선택 완료 자체는 동일 QML 콜백에 URL을 전달해 시험하므로 Android SAF 실기기 검증과 구별한다.

## 검증 기록

기준 코어 CTest 5/5, 기존 Qt 전체 12/12를 먼저 실행했다. 웹 원본 `project-migrations.test.mjs`, `project-state.test.mjs`, `project-serializer.test.mjs`는 **20/20 통과**했다. 원본 웹 전체 테스트나 브라우저 조작을 전부 검증했다는 뜻은 아니다.

신규 테스트는 API 골격/미구현 동작을 상대로 assertion 실패를 먼저 확인했다. 형식·golden migration·매핑·Controller·PC/모바일 UI red 로그를 남겼다. 추가 검토에서 숫자 ID 표기/Unicode trim, 잘못된 컨테이너와 모델 추가 필드 보존 장벽, 잘못된 기타 객체 source 계약을 각각 재현 후 수정했다. QML Escape 테스트는 닫힌 native chooser에 포커스가 남는 offscreen 환경을 진단해 대상 창 활성화를 명시했다. 오류 검사를 제거하지 않았다.

로컬 환경: Debian 13, GCC 14.2, Qt 6.8.3, offscreen/software.

| 검증 | 결과와 범위 |
|---|---|
| 전체 Debug CTest | **15/15 통과**: 기존 12개 + web_import_tests/editor/UI 3개 |
| AddressSanitizer + UndefinedBehaviorSanitizer | **7/7 통과**: 기존 코어 5개 + 신규 importer/Controller. Qt UI sanitizer 검증을 뜻하지 않음 |
| 원본 JS oracle | 고정한 v3/v4/v5/scalars 4개 fixture의 expected를 원본으로 재검증, C++ migration과 비교 |
| 저장 경계 | 입력 초과·archive 확장으로 출력 64 MiB 초과를 거절하는 테스트 |
| 세션 안전 | 준비/취소/실패, 기존 명령 preview, 최신 요청, hash 불일치, stale, 저장 실패/성공, 일회성 확정, 원본 보호 |
| 데이터 보존 | 좌표·holes/MultiPolygon·부모/기간·국기 3상태·미해석 큰 정수/null/배열·분포 60+70·root 분류·저장 후 재열기 |
| 공통 UI | PC/360px 보고서·확정·취소·제한 배지, 입력 중 필드 보존, 같은 결과와 저장 bytes |

재현:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
node tools/verify-web-import-goldens.mjs /path/to/world-map-at-58e4087

cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64 \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-sanitize --target command_allocation_tests command_tests core_tests model_tests job_tests web_import_tests web_import_editor_tests --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize -R '^(command_allocation_tests|command_tests|core_tests|model_tests|job_tests|web_import_tests|web_import_editor_tests)$' --output-on-failure
```

## 미검증·후속 범위

실제 Android SAF/터치/IME/Back/회전, Android cross-build, Windows 네이티브 실행은 이번 검증에 포함하지 않는다. 전체 세계지도 성능·메모리 예산도 측정하지 않았다. 독립 리뷰어가 리뷰한 것으로 기록하지 않는다. 네이티브 공급자 호출의 강제 중단이나 신규 모든 메모리 할당 지점의 실패 주입을 검증했다고 확대하지 않는다.

M3–M7: 국가 외 객체와 웹 표현의 완전 렌더링·검색/다중 선택·생성/종류/관계 편집·국경/영토 알고리즘·지명/수계/분포 편집·역사/GIS·전체 세계지도가 남는다. 현재 Qt RGB/불투명도/레이어 프로토타입 조작도 웹 원본과의 개별 동등성 검증이 별도로 필요하다. M2 완료는 이들을 보존하면서 웹 완전 저장본을 안전하게 읽고 Qt v3로 저장·재열기하는 범위다.
