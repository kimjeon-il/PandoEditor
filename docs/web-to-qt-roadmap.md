# 웹 → Qt 순차 이식 계획

## 범위와 시작 조건

[기능 조사](web-feature-audit.md)와 [데이터 모델](qt-data-model-design.md)을 구현 단위로 나눈다. 기준 웹 17c3dbe / 0.33.0, Qt 5a1717d. 이후 M1.1–M1.2 구현과 검증은 [별도 기록](qt-v3-implementation.md)에 정리했다. M1.3 공통 명령·스냅샷 ChangeSet과 기존 PC/모바일 편집 진입점 전환은 [명령 구현 기록](qt-command-implementation.md)에 정리했다. M1.4 스냅샷 작업·취소·세션 수명은 [작업 구현 기록](qt-job-implementation.md)에 정리했다. M2 이후의 웹 기능 이식은 후속 계획이다. [기능 동등성 계약](web-parity-contract.md)에 따라 실제 웹 진입·입력·결과·확정 경계를 임의로 변형하지 않는다. 모바일 공통 경로·360px 검증과 실제 Android 기기 검증은 구분한다.

순서: M1 공통 문서·명령 → M2 가져오기 → M3 영토 객체·웹 조작 → M4 국경·영토 → M5 지명·수계·분포 → M6 역사·GIS → M7 전체 세계지도·대규모 렌더링. M1–M6는 작은 합성 지도/자료만 사용한다. 세계지도 때문에 도메인 의미 설계를 미루지 않는다.

모든 단위는 PC 메뉴/패널 및 모바일 동일 기능 진입점, 동일 commandId/결과, 저장→재열기, Undo/Redo, 초안 취소/실패를 함께 완료해야 한다. 지원하지 않는 기능은 버튼을 연결한 척하지 않고 사유를 표시한다. 플랫폼별 코드에 도메인 규칙을 복제하지 않는다.

## M1 — 문서 v3·참조·명령 기반

대응 F02/06/08/15/36/40/42/43. 선행 없음. 사용자에게 새 기능을 크게 노출하기 전에 데이터와 원자성을 고정한다.

| 단위 | 입력 → 출력 | 오류/검증 | PC·모바일 진입 |
|---|---|---|---|
| M1.1 식별자·도형·관계 | legacy decoded document 또는 typed fixture → immutable document + 인덱스 | 중복/없는 참조, 순환, 소속 불일치, 기간 충돌 거절 | 아직 UI 노출 없음; 두 어댑터가 동일 core 사용 |
| M1.2 Qt v1/v2 migration | 기존 파일 bytes → v3 candidate/report → v3 저장 bytes | 기존 id/name/color/memo/opacity/layerId/모든 레이어 속성·순서·도형 보존 | PC 열기/저장; 모바일 열기/저장·내보내기 |
| M1.3 CommandProcessor | Request(commandId/projectInstanceId/documentId/revision/targets/args) → preview → ChangeSet | prepare/validate 실패·취소 무변경, no-op 무이력, 단일 Undo | PC 확정/취소·Undo/Redo; 모바일 확정/취소·Undo/Redo |
| M1.4 jobs/session | snapshot + cancelToken → candidate result | 늦은 결과·재열기·Undo 후 결과 폐기 | PC 진행 표시/취소; 모바일 진행 표시/취소 |

구현 파일 경계 제안: core document/refs/geometry/validation/commands, app codec/migrations 및 Qt models. 현재 core/project.h API는 어댑터로 점진 전환하여 기존 테스트를 한꺼번에 버리지 않는다. 문자열 Qt 의존성은 codec/model 경계에 둔다.

M1 입력 fixture는 Qt v1 최소 국가 1개, v2 국가 2개+레이어 3개(숨김/잠금/opacity/순서 포함), holes/MultiPolygon, 동일 좌표를 공유하는 속성 변경, 중첩 A→S1→S2 및 지방 R, BCE/year/date 구간을 포함한다. expected 파일은 개발자가 새 구현 출력으로 자동 덮어쓰지 않고 기존 codec/웹 의미와 대조한다.

완료: v1/v2→v3→재열기의 semantic equality, 새 v3 codec 실패 원자성, 관계 validation, 속성 변경에서 GeometryRef identity 유지, 한 ChangeSet Undo/Redo, 취소/오래된 job 테스트 통과. 현재 core/storage/editor/ui 회귀 테스트를 모두 실행하고 결과를 별도 기록. 성능 수치를 측정하지 않았다면 빠르다고 주장하지 않는다.

M1.3 계약 보완: before/after는 문서·인덱스·CountryView를 함께 소유하는 불변 스냅샷이다. 하나의 적용 또는 즉시 명령은 국가·레이어 초안까지 묶어 한 ChangeSet으로 처리한다. prepare/실패/취소/NoOp는 기존 Redo도 보존한다. confirm/Undo/Redo는 revision을 증가시키고, dirty는 별도 저장 스냅샷과의 의미상 차이로 판단한다. preview는 일회성이며 같은 파일 재열기도 새 projectInstanceId를 갖는다. 미확정 초안이 있는 Undo/Redo는 자동 확정하지 않고 적용/취소를 요구한다. M1.4에서는 스냅샷 기반 실제 백그라운드 준비, 최신 key 대체, 취소, 재열기/Undo/Redo 후 늦은 결과 폐기를 구현한다. 실제 웹 도형 worker/RPC는 M4 후속이다. 이름·메모 UI는 웹 change 확정과 일치하도록 M1.4에서 독립 확정으로 정정했으며, 모든 필드에 일괄 적용을 강제하지 않는다.

## M2 — 웹 완전 저장본 가져오기와 보존 장벽

대응 F28/30–32/34/36/40/45. 선행 M1.

| 단위 | 입력 → 출력 | 오류/검증 | PC·모바일 진입 |
|---|---|---|---|
| M2.1 형식 판정 | bytes → Qt legacy / web full 3–5 / 거절 | delta는 BASE_DATA_REQUIRED; 미래 버전·잘못된 JSON·중복 키·한도 거절 | 파일 → 웹 프로젝트 가져오기 / 모바일 파일 메뉴 동일 이름 |
| M2.2 웹 migration | web schema3/4/5 → normalized schema5 + archive | E32/E33 golden fixture와 의미 동일; UUID/국가 ID 보존 | 파일 선택 뒤 처리 단계·취소 |
| M2.3 매핑·보존 | normalized full → v3 candidate + ImportReport | 전체 root 필드 분류, unsupported fragment/의존성·자료 누락 표시 | PC 보고 패널 / 모바일 보고 시트 |
| M2.4 확정 | 승인된 candidate hash + revision → 새 세션 | 실패/취소/저장 실패/보고 후 원본 수정 시 기존 작업 유지 | 가져오기 확정/취소; dirty 확인 |
| M2.5 roundtrip guard | 미지원 포함 문서 → 안전한 이름/메모 수정 → 저장/재열기 | payload 값·타입·배열/null/미존재 유지; 위험 명령 capability 차단 | 객체 목록 제한 배지와 명령 거절 사유 |

첫 가져오기는 객체를 보존하는 것과 편집/표시하는 것을 분리한다. 아직 구현되지 않은 지명·분포·수계는 retained로 표시한다. 초기에는 전체 원본 archive 및 미해석 fragment가 보수적 구조 변경 제한을 유발할 수 있다. M3–M6에서 도메인을 해석하면서 그 제한을 좁힌다.

완료: schema3/4/5 및 autosave-full 성공, delta 거절 안내, override 우선순위, flag(Default/None/Embedded), migration styles/order/visibility, 미지원 알려진/알 수 없는 dependencies, 외부 dataset unavailable 사례 모두 검증. 가져오기 성공 전의 Undo 보존과 성공 후 새 이력 시작을 각각 테스트한다. 웹 원본 파일에는 쓰지 않는다.

## M3 — 국가·하위단위·지방과 웹 작업 흐름

대응 F01–11/13–15/22/30–33. 선행 M1/M2. 도형 연산이 필요한 버튼은 M4 완료 전 제한 표시.

| 단위 | PC 진입 / 모바일 진입 | 문서·저장·Undo 기준 |
|---|---|---|
| M3.1 검색·선택·포커스 | 검색·목록·지도·객체 메뉴 / 검색 탭·터치 선택·객체 시트 | 단일/다중 primary와 순서 일치, focus는 viewport만 변경; 선택으로 dirty 만들지 않음 |
| M3.2 속성·잠금 | 선택 패널 / 하단 편집 패널 | 이름·메모·색상 및 객체/레이어 잠금; 유효하지 않은 초안은 유지·확정 거절; 취소 무변경 |
| M3.3 생성·관계·종류·삭제 | 추가 및 종류 변경·소속 선택 / 추가 탭·동일 대화 흐름 | 작은 준비 도형으로 객체 생성; parent/sovereign·기간 변경·삭제 영향 미리보기; 자식/분포 참조 단일 Undo |
| M3.4 표시 모델 | 레이어 트리·보기 / 보기 탭·목록 | semantic group과 userLayer 분리, legacy 레이어 순서 유지, objectOrder/visibility/style 저장 |
| M3.5 선택 강조 | 지도·선택 패널 / 터치 선택 | 부모/자식 중복 선 제거, 단독 자식 전체 윤곽 유지; 객체 종류 간 opacity 합성 회귀 |

종류 전환 ID는 유지한다. 자식 승격/재소속/삭제 선택은 명시적 preview 없이 자동 수행하지 않는다. 분포 의미가 아직 미지원이면 해당 변경은 차단하고 M5 이후 해제한다. 웹 흐름과 다른 Qt 임시 버튼으로 기능 완료를 판정하지 않는다.

완료: 같은 fixture/동일 CommandRequest를 두 UI에서 보내 결과와 저장문서가 같음. 잠금·숨김 선택 정책, 부모 순환, 날짜별 소속 충돌, invalid draft, 레이어 속성 roundtrip, 다중 선택 취소를 확인. 실제 Android 입력은 아래 별도 gate.

## M4 — 국경·영토 연산과 초안

대응 F01/04/05/12/16–22/41/43/44. 선행 M3 및 해당 참조 도메인의 안전성 확인.

| 단위 | 웹 순서와 PC·모바일 진입 | 완료 기준 |
|---|---|---|
| M4.1 영역 선택·초안 | 신규 국가/영토 편입 → 원본/그린 영역/구성 영역 → 미리보기; 모바일 동일 순서 | 선·면 초안/자유곡선/점 수정·취소; 미확정 상태 저장문서에 없음 |
| M4.2 합병·분할 | 영역 합치기/나누기 → 호환 대상·절단선 → 후보 → 확정 | holes/MultiPolygon/접점/불인접/자식 포함 fixture; 복합 변경 한 Undo |
| M4.3 공유 국경 | 국가 다중 선택 → 국경 조정 / 모바일 다중 선택 메뉴 | 양쪽 도형 동시 갱신, 한쪽만 바뀌는 실패 금지; 국경 파생 캐시 재생성 |
| M4.4 해안선/자식 | 해안선 조정·정합 → 영향 미리보기 / 모바일 동일 명령 | 국가 권위, 자식 clip/remove/reassign, 분포 참조 정책; 취소/오래된 worker 결과 무변경 |
| M4.5 참조 이미지 보조 | 참조 이미지 불러오기·배치·선 추적 / 모바일 대응 진입 검토 | 이미지 세션과 도형 변경 구분; 웹에 없는 프로젝트 이미지 저장을 몰래 동등성 항목으로 추가하지 않음 |

커널 선정은 동일 fixture로 union/difference/intersection/split 정확성, 좌표 보존, 취소 가능성 및 빌드 적합성 평가 후 결정한다. 전체 세계지도 벤치마크는 여기서 하지 않는다. 오차 기준과 실패 조건이 없는 시각 확인만으로 국경 정합을 통과시키지 않는다.

## M5 — 지명·수도·국기·수계·분포·기타 객체

대응 F03/23–29/32/45/46. 선행 M2/M3, 도형 편집은 M4. 하위단위·지방의 국기 메뉴도 국가 전용 속성과 구분하여 동일 3상태로 이식한다.

| 단위 | PC / 모바일 | 완료 기준 |
|---|---|---|
| M5.1 지명·국가 기호 | 지명 추가/국가 속성/보기 / 추가·편집·보기 | 위치·수도·국기 3상태·라벨 고정/충돌/가시성 저장; 기본 자료 누락 표시 |
| M5.2 강·호수 | 강/호수 추가·선/면 초안 / 동일 도구·확정/취소 | 도메인 속성/출처/좌표 유지, 내장 기본 수계와 hydroEdits 구별 |
| M5.3 분포 | 분포 추가 → 종류·레이어·영역/도형·비율 / 동일 시트 | 개별 share 범위만 검증, 60+70 허용; layer 삭제 시 항목/자식 parent 처리; dominant/intensity |
| M5.4 기타 객체 | 이동/잠금/삭제 / 동일 객체 메뉴 | 여섯 도형 종류와 source.details 보존; 직접 생성/분할을 활성화하지 않음 |
| M5.5 지원 승격 | 미지원 설명/재검증 / 동일 설명 | 원본 fragment의 typed 승격 검증 후에만 capability 확대; archive가 수정 값 덮어쓰지 않음 |

완료: 각 단위의 create/edit/delete(해당 기능 허용 범위만), save/reopen, 단일 Undo/Redo, 초안 취소 및 reference integrity. 도형 변경에 연결된 분포/라벨의 실패와 잠금도 포함한다.

## M6 — 역사·기간별 관계·GIS

대응 F34–39. 선행 M3/M4/M5.

- 역사 라이브러리: PC 라이브러리 창 / 모바일 라이브러리 시트에서 동일 자료·기준 날짜·추가 명령. dataset/library ID·version·sourceGeometryVersion 및 partial 상태 보존, 누락 자료 자동 치환 금지.
- 기간별 관계: 현재 source의 날짜 정밀도·관계 해석을 구현하고 날짜별 parent cycle/sovereign/겹침 테스트. 데이터 해석을 전체 연도별 지도 재생으로 확대하지 않는다.
- 라이브러리 스냅샷: entityRefs/referenceDate로 추가되는 동작과 UI 접근성을 별도 검증. 숨겨진 입력만 연결된 상태라면 완료 아님.
- GIS: PC GIS 열기/내보내기 / 모바일 동일 파일 메뉴. GeoJSON ZIP/GeoPackage의 웹 adapter 의미에 맞춘 역할 선택·출처·미지원 fallback·좌표 검증. 일반 프로젝트 가져오기와 별도 형식이다.
- 작업 취소/잘못된 GIS/해안 정합 실패 시 후보만 폐기. 성공 시 한 Undo, 저장/재열기에서 포함 자료와 외부 참조 구분.
- 범용 타임라인 재생은 F37의 별도 제품 결정 사항. 이름만 HistoryManager로 만들고 기능 완료를 주장하지 않는다.

## M7 — 전체 세계지도 및 대규모 렌더링 (마지막)

선행 M1–M6 해당 이식 검증 통과. 그 전에는 전체 데이터셋 적용을 하지 않는다.

PC·모바일 동일 dataset manifest와 문서 로딩 명령을 사용하되 물리 메모리·viewport에 맞는 파생 캐시만 조정한다. 원본 좌표 단순화/다른 자료 대체로 성능을 맞추지 않는다. 날짜변경선·극점·다중 섬·holes·큰 ID 목록, 투영 전환, GPU culling/selection index, worker 대량 편집·취소, 메모리와 대형 저장 한도를 검증한다.

성능 gate는 타깃 PC/Android 기기·dataset 버전·측정 시나리오·허용 프레임/메모리/로딩 시간을 먼저 고정한 뒤 측정한다. 현재 측정값이나 보편적 FPS 목표를 꾸며 넣지 않는다. 그 전 단계에서 정상인 작은 지도의 도메인 결과가 대규모에서도 동일한지 회귀한다.

## 고정 검증 사례와 판정

| 사례 | fixture / 기대 결과 | gate |
|---|---|---|
| V01 legacy 보존 | v1/v2 → v3, 기존 속성·색상·레이어 속성/순서·모든 도형 좌표 의미 동일 | M1 |
| V02 중첩 소속 | 국가 A, S1→A, S2→S1, 지방 R, dated 관계 및 분포 참조를 작은 full 파일로 가져오기 | M1/M2/M3 |
| V03 잘못된 관계 | cycle, missing country, sovereign mismatch, 같은 endpoint 포함 기간 중첩, year0 거절 | M1/M2 |
| V04 복합 편집 | 합병/종류 전환 시 자식/분포/표시 참조 처리 또는 명시적 차단, 한 번 Undo로 전체 복원 | M3/M4/M5 |
| V05 미지원 보존 | unknown 큰 정수/null/배열/참조 포함 → 이름 수정 → 저장 → 재열기에서 값/타입 유지, 위험 편집 차단 | M2 |
| V06 렌더 의미 | 부모·자식 동시 선택/자식 단독 선택/국가+레이어 opacity/그룹 겹침/혼합/순서 | M3/M5/M7 |
| V07 실패 원자성 | 취소, decode 실패, apply 검증 실패, 저장 실패, 보고 후 수정, 오래된 job, 파일 재열기 후 job | M1/M2/M4 |
| V08 입력 동등성 | PC·모바일 동일 command와 동일 문서 결과; 물리 Android 입력 여부 별도 | 모든 UI gate |

각 테스트는 입력 fixture, expected semantic document, before/after revision/dirty/history/selection, 결과 코드, 지원/미지원 표시를 기록한다. JSON 객체 key 순서가 아니라 데이터 의미를 비교하되 배열 순서와 좌표는 비교 대상이다.

## 검증 실행 및 플랫폼 기록 원칙

최초 설계 조사 당시 웹 단위 검증은 **85개 중 84 통과/1 오류 문구 불일치 실패**였고, 그 조사 단계에서는 신규 v3 fixture/C++ 테스트나 기존 Qt 테스트를 실행하지 않았다. 이는 당시 기록이다. 이후 M1.1–M1.2 검증은 [v3 구현 기록](qt-v3-implementation.md), M1.3과 전체 회귀 검증은 [명령 구현 기록](qt-command-implementation.md)을 참조한다. 웹 상세 재현 목록은 기능 조사 문서에 유지한다.

후속 UI 단위마다 desktop과 360px 레이아웃에서 한글 버튼·툴바·하단 시트·다중 선택·확정/취소를 확인한다. 데스크톱 창을 좁힌 검증을 실제 Android로 기록하지 않는다. Android SAF, touch/long-press/pinch, 한글 IME, 화면 회전, 물리 Back은 실제 실행 기록이 있을 때만 통과다. 하이퍼바이저/OS 기능 변경·재부팅, ARM64 배포/스토어 서명은 이 문서 작업에 포함되지 않는다.

## 산출물 수용과 다음 요청

문서 수용 기준: 등록 객체 8종/공통 action 9개 및 생성·속성·파일·보조 작업이 F행에 있고, root 저장 필드 전체가 매핑되며, 확인되지 않은 동작과 테스트 실패가 드러나고, M1/M2의 입력·출력·실패·검증 기준이 지정된 상태. 브라우저 동작 동등성은 후속 구현 gate이며 이번 문서 수용과 구분한다.

다음 구현 요청의 최소 범위는 **M2.1–M2.2: 실제 웹 완전 저장본 형식 판정·migration**이다. M2.3–M2.5의 보고/보존/확정까지 검증되기 전에는 웹 가져오기 전체를 완료로 표시하지 않는다. 앱 기능을 구현할 때도 별도 요청 없이 웹 저장소/배포본을 변경하거나 커밋·푸시하지 않는다.
