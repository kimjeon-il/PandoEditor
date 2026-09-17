# 웹판 기능 조사 및 Qt 대응표

## 범위와 증거 수준

- 조사일: 2026-09-17. 웹 `17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da`, 앱 버전 `0.33.0`; Qt `5a1717d0154fa8c6efc4a8b8e6a0e7984927040b`.
- 소스 기반 기능 조사다. 이번에는 브라우저/Qt UI/Android 입력을 실행하지 않았다. 함수·테스트 존재를 UI 동작 확인으로 올리지 않는다.
- 상태: **동작 확인**(해당 UI 시나리오 실행), **구현 확인·실행 미검증**(연결 구현 확인), **일부 구현**(제한/연결 미확인), **UI만 존재**(핸들러 확인 불가), **없음**. 이번 조사에서 UI 전체 동작 확인으로 표시한 행은 없다.
- 단위 테스트 실행 결과는 아래 별도 기록이다. Qt 대응은 현재 소스 범위이며 동작 동등성 인증이 아니다.
- 표의 E번호는 아래 고정 커밋 소스 링크, M번호는 [이식 단계](web-to-qt-roadmap.md)다. 저장 필드 전체 매핑은 [모델 설계](qt-data-model-design.md#웹-저장-필드-대응표)에 있다.
- UI만 존재 상태에 해당한다고 확정한 행은 없다. 숨겨진 기능은 별도 제한을 명시한다.

## 기능 대응표

PC는 해당 메뉴/선택 패널, 모바일은 추가·검색·편집·보기와 하단 편집 패널을 진입점으로 사용한다. 동일 기능의 화면 위치 차이를 별도 기능으로 세지 않는다. 각 행은 UI → 핸들러/서비스 → 상태 → 저장 → 테스트 연결을 나타낸다. 미실행 브라우저 테스트는 회귀 후보이지 통과 증거가 아니다.

| ID | 웹 이름·조작 순서 | 대상 | 구현 연결 근거 | 저장 영향 | 현재 Qt 대응 | 선행 | 웹 검증 상태 | 테스트 근거/후속 후보 |
|---|---|---|---|---|---|---|---|---|
| F01 | 국가 추가 → 영역 선택/그리기 → 미리보기 → 확정 | country | E01 addCountryBtn → E06/E07 finishNewCountrySelectionDraft | countriesData + countryOverrides | 없음 | M1/M3 | 구현 확인·실행 미검증 | territorial-edit-plan |
| F02 | 국가 선택 → 이름·색상·메모 수정 | country | E41 → 국가 필드 명령 → override | countryOverrides.{name,color,notes} | 일부 구현: 이름·색상·메모 | M1/M3 | 구현 확인·실행 미검증 | project-state |
| F03 | 국가 선택 → 수도·국기 설정/없음/기본 | country | E20/E21 → override 조회/렌더 | countryOverrides.{capital,flagDataUrl} | 없음 | M1/M4 | 구현 확인·실행 미검증 | 브라우저 country-label-flags |
| F04 | 하위단위 추가 → 부모·방식(split/draw/geojson) → 확정 | subunit | E02 addSubunitBtn → E08 → E11/E12 | territorialUnits | 없음 | M1/M3/M4 | 구현 확인·실행 미검증 | territorial-service |
| F05 | 지방 추가 → 그리기/GeoJSON → 확정 | region | E02 addRegionBtn → E08 → E11 | territorialUnits | 없음 | M1/M3/M4 | 구현 확인·실행 미검증 | territorial-units |
| F06 | 하위단위/지방 선택 → 이름·메모·유효기간 | subunit/region | E11 updateMetadata → E10 검증 | territorialUnits.properties | 없음 | M1/M3 | 구현 확인·실행 미검증 | territorial-service |
| F07 | 종류 변경 → 대상 종류·소속 → 영향 확인 → 확정 | territorial | E03 territorial.change-type → E09 confirmTerritorialTypeConversion | 국가/영역/관계/분포 참조 복합 변경 | 없음 | M1/M3 | 구현 확인·실행 미검증 | 브라우저 territorial-type-conversion |
| F08 | 하위단위 상위 단위·소속 국가 변경 | subunit/region | E11 updateMetadata → E10 부모 검증 | parentId/sovereignId | 없음 | M1/M3 | 구현 확인·실행 미검증 | territorial-units |
| F09 | 삭제 → 대상·종속 영향 확인 → 확정 | 전체; generic 제한 포함 | E03 object.delete → E04 requestBatchDelete | 객체 + 참조 + 표시 설정 | 일부 구현: 레이어 제거만 | M1/M3 | 구현 확인·실행 미검증 | project-invariants |
| F10 | 지도/목록에서 단일 객체 선택 | 전체 | E42 → E05 selection(primaryKey/items) | 없음: 세션 | 일부 구현: 국가 단일 선택 | M1/M3 | 구현 확인·실행 미검증 | object-registry |
| F11 | 다중 선택 → 공통 작업 사용 가능 여부 확인 | 전체 | E04 commonBatchCapabilities → E05 | 선택 자체 저장 없음; 작업별 저장 | 없음 | M1/M3 | 구현 확인·실행 미검증 | 브라우저 territorial-selection |
| F12 | 그린 영역/구성 영역 선택 → 편입·신규 생성 | territorial | E06 + E14 → E07 preview | 초안은 세션, 확정 시 도형 | 없음 | M1/M4 | 구현 확인·실행 미검증 | 브라우저 annex-drawn-selection |
| F13 | 검색 → 결과 선택 → 편집 대상 전환 | 전체 | E01 검색 진입 → E42/E05 선택 경로 | 없음: 세션 | 없음 | M3 | 일부 구현: 검색별 실행 경로 미검증 | 브라우저 single-context-selection |
| F14 | 선택 객체로 이동 | 전체 | E03 object.focus → E04 focusObjectRef | 없음: viewport; 도형 이동 아님 | 없음 | M3 | 구현 확인·실행 미검증 | object-registry |
| F15 | 잠금/잠금 해제 → 편집 가능 조건 갱신 | 전체 | E03 object.lock.toggle → E04/E11/E16 | 객체 locked (국가는 override) | 일부 구현: 레이어 잠금만 | M1/M3 | 구현 확인·실행 미검증 | territorial-service |
| F16 | 영토 편입 → 원본·대상 선택 → 초안 → 미리보기 → 확정 | country/subunit | E01 annexTerritoryBtn → E06/E07/E12/E13 | donor/target 도형 + 자식·참조 | 없음 | M4 | 구현 확인·실행 미검증 | territorial-edit-plan |
| F17 | 영역 합치기 → 호환 대상 선택 → 확정 | territorial | E03 object.merge → E07 completeCountryMerge / E08 | 도형·객체·관계·분포 | 없음 | M4 | 구현 확인·실행 미검증 | territorial-edit-plan |
| F18 | 영역 나누기 → 절단선 → 후보 선택 → 확정 | territorial | E03 object.split → E13 buildCutSplitCandidates | 도형·신규 객체·자식 참조 | 없음 | M4 | 구현 확인·실행 미검증 | territorial-edit-plan |
| F19 | 국경 조정 → 공유 국경 초안 → 확정 | country | E03 territorial.edit-border → E13/E38 | 양쪽 국가 도형 | 없음 | M4 | 구현 확인·실행 미검증 | 브라우저 projection-border |
| F20 | 해안선 조정 → 외곽선 편집 → 확정 | territorial | E03 territorial.edit-coast → E08/E12 | 국가 권위 도형 + 영향받는 자식 | 없음 | M4 | 구현 확인·실행 미검증 | territorial-edit-plan |
| F21 | 해안선 정합 → 불일치 미리보기 → 확정 | territorial | E03 territorial.reconcile-coast → E08 refreshTerritorialCoastAvailability | territorialUnits.geometry | 없음 | M4 | 구현 확인·실행 미검증 | territorial-edit-plan |
| F22 | 자식 추가·구획 제거·도형 재지정 | subunit/region | E01 addSubunitChildBtn/removeSubunitDivisionBtn → E08/E12 | 자식 목록/coverageMode/geometry | 없음 | M3/M4 | 구현 확인·실행 미검증 | territorial-edit-plan |
| F23 | 지명 추가 → 위치 지정 → 속성 편집/삭제 | label | E02 addLabelBtn → E09 commitLabelEdit / E22 | labels + labelSettings | 없음 | M5 | 구현 확인·실행 미검증 | 브라우저 map-editing-foundation |
| F24 | 강 추가 → 선 초안 → 확정·속성 편집 | river | E02 addRiverBtn → E07 finishHydroDraft → E17 | hydroEdits (line) | 없음 | M5 | 구현 확인·실행 미검증 | 브라우저 map-editing-foundation |
| F25 | 호수 추가 → 면 초안 → 확정·속성 편집 | lake | E02 addLakeBtn → E07 finishHydroDraft → E17 | hydroEdits (polygon) | 없음 | M5 | 구현 확인·실행 미검증 | 브라우저 map-editing-foundation |
| F26 | 언어·민족·종교 분포 추가 → 레이어·항목 설정 | distribution | E02 addDistributionBtn → E16 createLayer/addEntry → E15 | distributionLayers/Entries | 없음 | M5 | 구현 확인·실행 미검증 | distribution-service |
| F27 | 분포 영역 참조/독립 도형·비율·기간·삭제 | distribution | E16 addEntry/deleteLayer → E15 | 독립 share 0~100; 자식 레이어 parent 해제 | 없음 | M5 | 구현 확인·실행 미검증 | distribution-model |
| F28 | 기타 객체 선택 → 이동/잠금/삭제 | generic | E02 fallbackOnly → E04/E18 | genericFeatures + source | 없음 | M2/M5 | 구현 확인·실행 미검증 | source-provenance |
| F29 | 기타 객체 직접 생성·분할 | generic | E02 creatable=false; E19 finishSplitGenericFeatureDraft 거절 | 직접 생성 경로를 이식하지 않음 | 없음 | 해당 없음 | 없음: 일부 잔존 함수는 사용자 기능 아님 | object-registry |
| F30 | 보기/레이어 목록 → 표시 순서·객체 순서 조정 | 전체 | E43 → E23 overlayOrder/objectOrder | layerPresentation | 일부 구현: 사용자 레이어 순서 | M3 | 구현 확인·실행 미검증 | project-state |
| F31 | 그룹/객체 표시·투명도·혼합 설정 | 전체 | E23 스타일 → 렌더 소비 | layerVisibility/itemVisibility/layerPresentation | 일부 구현: 국가·레이어 opacity/visibility | M3/M7 | 구현 확인·실행 미검증 | project-state |
| F32 | 경계·라벨·국기 표시, 고정 위치·충돌 | territorial/label | E20/E22/E23 → label layout | labelSettings/표시 설정 | 없음 | M5/M7 | 구현 확인·실행 미검증 | 브라우저 country-label-flags |
| F33 | 부모·자식 동시 선택 경계 강조 | territorial | E05 → E24 중복 윤곽 구간 배제 | 없음: 파생 렌더 상태 | 없음 | M3/M7 | 구현 확인·실행 미검증 | territorial-highlight-boundary |
| F34 | 역사 라이브러리 → 자료·기준 날짜 → 추가 | territorial/분포 등 | E25 → E26 instantiate → 문서 변경 | 가져온 객체/기간/출처; 라이브러리 전체와 별개 | 없음 | M6 | 구현 확인·실행 미검증 | 브라우저 historical-library |
| F35 | 역사 라이브러리 스냅샷 추가 | library entityRefs | E25 requestSnapshot 핸들러; E01 숨김 입력 | 스냅샷의 객체를 추가; partial 경고 | 없음 | M6 | 일부 구현: 핸들러 존재, 일반 UI 접근 미확인 | 브라우저 historical-library |
| F36 | 기간별 관계·날짜에 따른 소속 해석 | territorial | E10 resolveTerritorialRelation → E27 | territorialRelations + validFrom/validTo | 없음 | M1/M6 | 구현 확인·실행 미검증 | temporal / territorial-units |
| F37 | 연도 슬라이더로 전체 문서 상태 전환 | 전체 | E10 날짜별 관계 함수는 존재; 전체 UI 전환 근거 미확인 | 범용 연도별 문서 스냅샷 필드 없음 | 없음 | M6 별도 승인 대상 | 없음: 완성 타임라인으로 판정하지 않음 | 관련 테스트만으로 완료 판단 금지 |
| F38 | GIS 열기 → 형식·역할·검증 → 가져오기 | 다중 도메인 | E01 openGisBtn → E28/E29 | 도형·속성·source; fallback generic | 없음 | M6 | 구현 확인·실행 미검증 | 브라우저 gis-interchange |
| F39 | GIS 내보내기 → GeoPackage/GeoJSON ZIP | 다중 도메인 | E01 dataExportBtn → gis-export-controller.js | 외부 교환 파일, 프로젝트 저장과 별개 | 없음 | M6 | 구현 확인·실행 미검증 | 브라우저 gis-interchange |
| F40 | 파일 저장/열기·자동저장 복구 | 문서 | E46 → E31 → E30/E32/E33 | 전체 필드; full/delta 구별 | 일부 구현: Qt v1/v2 + 앱 복구 | M1/M2 | 구현 확인·실행 미검증 | project-serializer / project-migrations |
| F41 | 초안 점/선 편집 → 취소/완료 | 도형 초안 | E47/E07/E08 → 확정 시 거래 | 확정 전 EditorSession 대응 | 일부 구현: 속성 초안만 | M1/M4 | 구현 확인·실행 미검증 | 브라우저 draft-freehand |
| F42 | 실행 취소/다시 실행 (편집 스냅샷) | 문서 | E36 → E34/E35 rollback/history | 세션 이력; 프로젝트의 연도 상태 아님 | 일부 구현: 국가 속성·레이어 | M1 | 구현 확인·실행 미검증 | project-command-pipeline / project-transaction |
| F43 | 백그라운드 처리 → 취소·최신 결과 반영 | 도형 작업 | E37/E38 → E35/E13 | 확정된 결과만 문서 변경 | 없음: 동등 계약 미구현 | M1/M4 | 구현 확인·실행 미검증 | worker-job-scheduler |
| F44 | 참조 이미지 배치·정합·선 추적 | 편집 보조 | E40 및 reference-image-*; E47 연결 | E30 저장 필드에 이미지 세션 없음 | 없음 | M4 후속 단위 | 일부 구현: 프로젝트 이미지 복구 미지원/미확인 | 브라우저 reference-image-overlay |
| F45 | 지형·기본 수계 표시/자료 선택 | 외부 dataset | E17/physical-layer-service.js → 렌더 | physicalSettings/physicalSourceInfo; 실제 dataset 별개 | 없음 | M5/M7 | 구현 확인·실행 미검증 | project-serializer |

| F46 | 하위단위·지방 선택 → 국기 메뉴 → 업로드/없음/기본 | subunit/region | app-domain-assembly.js → app-object-metadata.js commitTerritorialUnitMeta → country-flags.js | territorialUnits.properties.metadata.flagDataUrl | 없음 | M3/M5 | 구현 확인·실행 미검증 | 브라우저 editor-flag-menu |

## 등록 항목 대조

등록 객체 8종을 빠짐없이 분류했다: country(F01–03), subunit(F04/06), region(F05/06), distribution(F26–27), label(F23), river(F24), lake(F25), generic(F28–29). 분포의 세 종류는 단일 등록 타입의 subtype이다.

등록 사용자 명령 9개: `object.focus` F14, `object.lock.toggle` F15, `object.delete` F09, `territorial.change-type` F07, `territorial.edit-border` F19, `territorial.edit-coast` F20, `territorial.reconcile-coast` F21, `object.merge` F17, `object.split` F18. 등록만으로 모든 도메인에 적용 가능한 것은 아니다. capability/locked/type/selection 조건을 함께 확인해야 한다. 나머지 생성·속성·저장·GIS·초안·보기 명령은 기능별 행에 묶었다.

특히 generic은 서비스 내부 add/잔존 편집 함수가 있어도 직접 생성 기능이 아니다. 선택 객체로 이동은 카메라 이동이지 도형 좌표 이동이 아니다. 하위단위 소속 변경도 사용자 레이어 이동과 다르다.

## 현재 Qt와의 차이

[Project 모델](https://github.com/kimjeon-il/Pandoeditor/blob/5a1717d0154fa8c6efc4a8b8e6a0e7984927040b/core/include/pandoeditor/project.h)은 Country(id/name/polygons/color/memo/opacity/layerId)와 Layer(id/name/visible/locked/opacity)만 소유한다. 단일 국가 선택, 국가 속성 변경, 레이어 추가·제거·이동 및 Undo가 기준선이다. 국가 생성·소속 관계·국경 연산이 있다고 간주하지 않는다.

[현재 codec](https://github.com/kimjeon-il/Pandoeditor/blob/5a1717d0154fa8c6efc4a8b8e6a0e7984927040b/app/projectcodec.cpp)은 Qt v1/v2 전용이며 웹 파일을 여는 기능이 아니다. 기존 UI/저장 기반은 재사용하지만 Country+Layer를 웹 의미의 상위 모델로 삼으면 손실이 발생한다.

## 실행한 검증

다음 16개 `tests/unit/<이름>.test.mjs`를 기준 웹 체크아웃에서 Node `--test --test-reporter=tap`으로 실행했다.

```text
project-migrations project-state project-serializer territorial-units
territorial-service territorial-edit-plan distribution-model distribution-service
temporal source-provenance object-registry project-command-pipeline
project-transaction project-invariants worker-job-scheduler territorial-highlight-boundary
```

결과: **85개 중 84 통과, 1 실패, skip/cancel 0**. 실패는 `project-invariants.test.mjs:27`의 dangling references 테스트: 기대 정규식 `/상위 소속/`과 실제 `R의 상위 단위 MISSING이 존재하지 않습니다.`가 불일치한다. 참조 거절 자체는 발생했지만 테스트는 실패다. 이번 범위에서는 수정하지 않았다. 이 결과로 전체 웹 테스트·브라우저 회귀·Qt 이식 검증 성공을 주장하지 않는다.

## 조사 판정과 남은 확인

등록 객체·등록 action·프로젝트 root 저장 필드는 대응표에 등록했다. 검색의 모든 필터, 라이브러리 스냅샷의 일반 UI 접근, 이미지 세션 복구, 연도별 전체 상태 전환은 검증 공백으로 남긴다. 나머지 기능도 소스 확인과 실제 사용자 동작 검증은 구분한다. 첫 구현 계약 및 실패 조건은 M1/M2에 고정한다.

부모/자식 동시 선택은 자식 전체 윤곽을 숨기는 것이 아니라 겹치는 구간만 제외해야 한다. 투명도는 모든 객체 종류에서 같은 합성 경로를 검증한다. 현재 소스의 스타일 정규화는 boundaryWidth를 기본값으로 정규화하는 제한도 있으므로 저장 필드 존재를 임의 굵기 UI 지원으로 해석하지 않는다.

## 고정 소스 색인

- E01: [index.html](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/index.html) — PC/mobile buttons and modal entry points.
- E02: [assets/js/modules/map-object-categories.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/map-object-categories.js) — MAP_OBJECT_TYPES / MAP_OBJECT_CATEGORIES.
- E03: [assets/js/modules/object-action-registry.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/object-action-registry.js) — OBJECT_ACTIONS / objectActionApplies.
- E04: [assets/js/modules/app-object-commands.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-object-commands.js) — focusObjectRef / batchSetLocked / requestBatchDelete / batchSetColor.
- E05: [assets/js/modules/selection-domain.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/selection-domain.js) — createSelectionDomain.
- E06: [assets/js/modules/app-territory-selection-workflow.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-territory-selection-workflow.js) — territory selection workflow.
- E07: [assets/js/modules/app-country-commits.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-country-commits.js) — finishDraft / completeCountryMerge / preview preparation.
- E08: [assets/js/modules/app-territorial-drafts.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-territorial-drafts.js) — previewTerritorialEdit / territorial creation.
- E09: [assets/js/modules/app-territorial-conversion.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-territorial-conversion.js) — confirmTerritorialTypeConversion / compound reference updates.
- E10: [assets/js/modules/territorial-units.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/territorial-units.js) — normalizeTerritorialRelations / resolveTerritorialRelation.
- E11: [assets/js/modules/territorial-service.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/territorial-service.js) — updateMetadata / setLocked / runGeometryTransaction.
- E12: [assets/js/modules/territorial-edit-plan.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/territorial-edit-plan.js) — geometry and child impact plans.
- E13: [assets/js/modules/app-cut-geometry.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-cut-geometry.js) — prepareCutDraft / buildCutSplitCandidates / applyWorkerCountryPatches.
- E14: [assets/js/modules/app-territory-components.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-territory-components.js) — territory component selection.
- E15: [assets/js/modules/distribution-model.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/distribution-model.js) — layer and entry normalization.
- E16: [assets/js/modules/distribution-service.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/distribution-service.js) — createLayer / addEntry / deleteLayer.
- E17: [assets/js/modules/app-hydro-settings.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-hydro-settings.js) — normalizeHydroEdit.
- E18: [assets/js/modules/generic-feature-service.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/generic-feature-service.js) — lossless generic normalization.
- E19: [assets/js/modules/app-generic-commands.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-generic-commands.js) — finishSplitGenericFeatureDraft rejects operation.
- E20: [assets/js/modules/app-country-labels.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-country-labels.js) — country label and flag rendering.
- E21: [assets/js/modules/country-flags.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/country-flags.js) — effectiveCountryFlagUrl.
- E22: [assets/js/modules/label-layout.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/label-layout.js) — normalizeLabelSettings / automaticLabelSettings.
- E23: [assets/js/modules/layer-presentation.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/layer-presentation.js) — normalizeLayerPresentation / object rank.
- E24: [assets/js/modules/territorial-highlight-boundary.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/territorial-highlight-boundary.js) — parent-child outline exclusion.
- E25: [assets/js/modules/historical-library-controller.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/historical-library-controller.js) — requestSnapshot / referenceDate / instantiate.
- E26: [assets/js/modules/historical-library-service.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/historical-library-service.js) — library service.
- E27: [assets/js/modules/temporal.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/temporal.js) — date precision and interval rules.
- E28: [assets/js/modules/gis-domain.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/gis-domain.js) — GIS domain.
- E29: [assets/js/modules/gis-import-transaction.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/gis-import-transaction.js) — GIS transaction.
- E30: [assets/js/modules/project-state.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/project-state.js) — PROJECT_STATE_FIELDS / schema validation.
- E31: [assets/js/modules/project-serializer.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/project-serializer.js) — buildProject / buildAutosave / restoreCountriesFromDelta.
- E32: [assets/js/modules/project-migrations.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/project-migrations.js) — schema 3 to 4 migration.
- E33: [assets/js/modules/subunit-migration.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/subunit-migration.js) — schema 4 to 5 migration.
- E34: [assets/js/modules/project-command-pipeline.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/project-command-pipeline.js) — runMutation / rollback.
- E35: [assets/js/modules/project-transaction.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/project-transaction.js) — async project transaction.
- E36: [assets/js/modules/app-project-snapshots.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-project-snapshots.js) — editable snapshots for history.
- E37: [assets/js/modules/worker-job-scheduler.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/worker-job-scheduler.js) — latest job scheduling.
- E38: [assets/js/modules/map-edit-worker-client.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/map-edit-worker-client.js) — worker client.
- E39: [assets/js/modules/source-provenance.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/source-provenance.js) — normalizeSourceProvenance.
- E40: [assets/js/modules/reference-image-controller.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/reference-image-controller.js) — reference image workflow.
- E41: [assets/js/modules/country-property-controller.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/country-property-controller.js) — country properties.
- E42: [assets/js/modules/object-selection-controller.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/object-selection-controller.js) — ObjectRef and ordered selection.
- E43: [assets/js/modules/layer-tree-controller.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/layer-tree-controller.js) — layer and object tree.
- E44: [assets/js/modules/version-contract.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/version-contract.js) — schema versions.
- E45: [assets/js/modules/project-invariants.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/project-invariants.js) — reference integrity.
- E46: [assets/js/modules/app-connect-project-io.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-connect-project-io.js) — project file wiring.
- E47: [assets/js/modules/draft-editor.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/draft-editor.js) — draft geometry editing.

## 테스트와 추가 연결 근거 링크

단위 테스트 16개는 위 실행 결과에 포함한다. 아래 브라우저 테스트는 미실행 후보다.

- [project-migrations.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/project-migrations.test.mjs)
- [project-state.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/project-state.test.mjs)
- [project-serializer.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/project-serializer.test.mjs)
- [territorial-units.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/territorial-units.test.mjs)
- [territorial-service.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/territorial-service.test.mjs)
- [territorial-edit-plan.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/territorial-edit-plan.test.mjs)
- [distribution-model.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/distribution-model.test.mjs)
- [distribution-service.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/distribution-service.test.mjs)
- [temporal.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/temporal.test.mjs)
- [source-provenance.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/source-provenance.test.mjs)
- [object-registry.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/object-registry.test.mjs)
- [project-command-pipeline.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/project-command-pipeline.test.mjs)
- [project-transaction.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/project-transaction.test.mjs)
- [project-invariants.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/project-invariants.test.mjs)
- [worker-job-scheduler.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/worker-job-scheduler.test.mjs)
- [territorial-highlight-boundary.test.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/unit/territorial-highlight-boundary.test.mjs)
- 미실행: [country-label-flags.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/country-label-flags.spec.mjs)
- 미실행: [territorial-type-conversion.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/territorial-type-conversion.spec.mjs)
- 미실행: [territorial-selection.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/territorial-selection.spec.mjs)
- 미실행: [single-context-selection.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/single-context-selection.spec.mjs)
- 미실행: [annex-drawn-selection.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/annex-drawn-selection.spec.mjs)
- 미실행: [projection-border.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/projection-border.spec.mjs)
- 미실행: [map-editing-foundation.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/map-editing-foundation.spec.mjs)
- 미실행: [historical-library.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/historical-library.spec.mjs)
- 미실행: [gis-interchange.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/gis-interchange.spec.mjs)
- 미실행: [draft-freehand.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/draft-freehand.spec.mjs)
- 미실행: [reference-image-overlay.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/reference-image-overlay.spec.mjs)
- 미실행: [editor-flag-menu.spec.mjs](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/tests/browser/editor-flag-menu.spec.mjs)
- 추가 연결: [app-domain-assembly.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-domain-assembly.js)
- 추가 연결: [app-object-metadata.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/app-object-metadata.js)
- 추가 연결: [gis-export-controller.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/gis-export-controller.js)
- 추가 연결: [physical-layer-service.js](https://github.com/kimjeon-il/world-map/blob/17c3dbe9c5cae2c3d11fec6503c4d68e1b4e21da/assets/js/modules/physical-layer-service.js)
