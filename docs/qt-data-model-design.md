# Qt 공통 데이터 모델 및 웹 가져오기 설계

## 결정과 적용 범위

설계 기준은 [기능 조사](web-feature-audit.md)의 웹 17c3dbe / 0.33.0, Qt 5a1717d다. 이후 M1.1–M1.2에서 공통 영토 모델과 Qt v3 codec을 구현했다. 구현 범위·실제 wire 형식·검증은 [M1.1–M1.2 기록](qt-v3-implementation.md)을 참고한다. 아래의 분포·수계·범용 명령·웹 가져오기 계약 전체가 구현됐다는 뜻은 아니다. 웹 → Qt 단방향이며 웹으로 되돌려 내보내는 호환성을 약속하지 않는다.

핵심 분리: 도메인 문서 / 화면 표현 / 편집 세션 / 파생 렌더 캐시. 국가 소속과 사용자 레이어는 서로 참조할 수 있어도 같은 트리가 아니다. 웹 코드를 통째로 옮기는 대신 저장·명령 의미를 작은 fixture로 고정한다.

## 소유권과 식별자

| 구성 | 저장되는 데이터와 계약 |
|---|---|
| ProjectDocument | documentId, 도메인 컬렉션, GeometryStore, PresentationState, provenance, datasets, extensions. 런타임 revision/Undo는 저장 본체와 분리 |
| TerritorialUnit | 안정 ID, kind(country/subunit/region), name, notes, geometryRef, locked, validity, coverageMode(partition/explicit), metadata, source. country 전용 속성은 별도 선택 필드 |
| CountryProperties | capital(원본 값 보존), flagPolicy(Default/None/Embedded), flagAssetRef, 필요 시 원본 표시 이름. 소속 국가의 국기를 하위단위에 복제하지 않음 |
| TerritorialRelation | unitRef, parentRef?, sovereignRef?, validity 및 mode(base/dated). 기본 소속과 날짜별 소속 모두 이 컬렉션이 소유 |
| GeometryStore | GeoJSON 종류별 원본 경위도 double 좌표와 불변 geometryId/version. 점·선·면·Multi 계열, 원본 ring/part 순서 유지 |
| DistributionLayer | ID, type(language/ethnicity/religion), name/color/locked, parentLayerRef?, groups, validity, metadata |
| DistributionEntry | ID, layerRef, source(territorialRef 또는 geometryRef), share, certainty, validity, metadata |
| PlaceLabel | 안정 ID, 위치, 텍스트/종류, 선택적 countryRef, 원본 도메인 속성·출처. 자동 국가 라벨은 별도 객체로 중복 생성하지 않음 |
| HydroFeature | ID, kind(river/lake), geometryRef, name/notes/editorColor, source 및 웹 수계 속성 보존. 기본 수계와 사용자 편집 수계 구분 |
| GenericFeature | ID, geometryRef, name/notes/color/locked/source, fallbackOnly. 임의 일반 도형 생성 모델로 바꾸지 않음 |
| PresentationState | semanticGroups, userLayers, membership, groupOrder, objectOrder, groupStyles, objectStyles, visibility, labelSettings, distributionSettings, physicalSettings |
| EditorSession | 선택(primary + ordered refs), hover, 도구/입력 초안, viewport/projection, 미리보기, job/cancel 상태, active reference date. 프로젝트 JSON에 넣지 않음 |

ObjectRef는 `{domain,id}`. domain은 territorial/distributionLayer/distributionEntry/label/hydro/generic/userLayer로 고정한다. 웹 distribution 선택은 레이어와 항목을 구분하여 어댑터에서 변환한다. 타입은 대상 조회 결과이며 identity key에 넣지 않는다. 따라서 country → subunit 전환으로 참조 ID를 재발급하지 않는다. 국가 문자열 ID 및 다른 도메인의 UUID를 보존한다. 국가·하위단위·지방은 같은 territorial namespace이므로 동일 ID 충돌은 가져오기 오류다. 다른 도메인의 같은 문자열은 ObjectRef로 구별된다. 새 객체에만 새 ID를 발급한다.

핵심 인덱스: ObjectRef→객체, geometryId/version→불변 도형, 부모→자식, 국가→소속, 대상→역참조, layer→entries. 문서 검증 후 원자적으로 갱신하고 ID 조회에 전 객체/전 GPU 버퍼 탐색을 사용하지 않는다.

## 관계와 시간 규칙

기본 관계는 unit마다 최대 1개(mode=base); 웹 properties.parentId/sovereignId에서 만든다. 웹 country 어댑터의 sovereignId=self는 국가 자체 의미로 해석한다. 국가에 임의 부모를 만들지 않는다. dated 관계는 웹 relation ID를 그대로 유지한다. base에 웹 ID가 없으면 내부 파생 key를 사용하되 기존 unit ID는 변경하지 않는다.

referenceDate에서 dated 관계가 있으면 **두 필드 전체를** 대체하고 없으면 base로 돌아간다. dated parent가 비어 있으면 base parent를 몰래 채우지 않는다. 연도별 지도 도형 스냅샷은 이 계약에 포함하지 않는다.

날짜는 `{text, precision:year|date}`로 직렬화하고 비교는 웹 temporal 규칙의 정규화 경계로 한다. 음수 연도(기원전), 부호와 연도/날짜 정밀도, year 0 금지를 유지한다. 웹 허용 연도 자릿수(부호 없는 4자리, 부호 포함 4~6자리)·월일 유효성 규칙을 golden fixture로 고정한다. null 끝점은 열린 구간; 연도 시작/끝으로 확장한 **포함 구간**끼리 겹치면 충돌한다. 같은 날짜를 공유하는 두 dated 관계도 거절한다.

검증은 모든 관계 경계 구간에서 수행한다: 없는 unit/parent/국가 참조, self-parent/간접 순환, subunit의 부모 종류 또는 부모와 sovereign 불일치, 한 unit의 dated 구간 중첩, 기간 역전/0년. region에 subunit의 의무 부모 규칙을 일괄 적용하지 않는다. 웹의 허용 구조를 fixture로 유지하되 기간별 순환처럼 웹 검증이 약한 경우 Qt에서 더 엄격히 거절하고 사전 보고서에 이유를 표시한다. 자료 유효기간 밖에서 참조되는 관계는 import 오류로 보고; 기간을 자동 축소하지 않는다.

## 도형과 표현 계약

원본은 경위도 좌표. 투영 XY, SVG path, bounding box, 공간 인덱스, triangulation, GPU buffer, 공유 국경·인접성은 파생 캐시다. 투영 좌표를 저장 원본으로 역승격하지 않는다. 유효하지 않은/지원하지 않는 도형을 조용히 단순화하거나 다른 도형으로 바꾸지 않는다.

GeometryRef=(geometryId,version). 속성 편집은 동일 참조를 공유한다. 도형 편집만 새 버전을 만들며 Undo가 참조하는 버전은 수거하지 않는다. 저장은 현재 문서에서 사용 중인 버전만 포함한다. 캐시 key는 geometry version + projection + 필요한 style/selection revision. 좌표 오차 허용치는 연산 종류별 테스트에 명시하고 JSON roundtrip에서는 double roundtrip을 유지한다.

공유 국경의 영구 master와 국가 폴리곤을 동시에 저장/수정하는 중복 소유는 도입하지 않는다. 국경 조정은 파생 공유선에 대한 편집 계획을 양쪽 원본 국가의 새 geometry version으로 변환한다. 국가 해안선이 권위이며 자식 clip/remove 영향은 별도 preview에 나온다. 연산 커널 라이브러리 선택은 M4의 작은 도형 적합성 평가에서 결정; 모델은 union/difference/intersection/split 인터페이스만 의존한다.

PresentationState의 userLayers는 기존 Qt 레이어 ID/이름/잠금/가시성/불투명도/순서를 유지하는 독립 컨테이너다. semanticGroups는 웹 countries/subunits/regions/... 표시 역할이다. 웹 파일은 semantic 그룹을 갖는 기본 사용자 레이어로 매핑하되 영토 parent로 만들지 않는다. 기존 Qt country.layerId는 membership으로 변환한다.

색상과 opacity는 표현 속성; 문서에 저장하지만 영토 소속과 무관하다. 객체×표시 그룹×사용자 레이어 opacity를 단계별로 합성한다. 그룹 opacity는 자식 각각에 반복 곱해 겹침을 진하게 만들지 않도록 offscreen group 합성 계약을 둔다. normal/multiply와 객체 순서를 보존하며 미지원 blend는 정상 표시처럼 주장하지 않는다. 선택 강조는 본체 fill alpha 변경과 분리; 부모와 자식의 중복 윤곽 구간만 제거한다. 잠금은 객체 또는 userLayer가 잠겼을 때 수정 차단, 선택/포커스는 가능. 숨김 객체의 지도 hit-test와 목록 선택은 서로 다른 정책으로 제공한다.

QML 어댑터는 ObjectListModel(role: ref/type/name/visible/locked/editable/unsupported), SelectionModel(primary/ordered selection), CommandAdapter(capability와 reasonCode/영향 preview/execute)를 제공한다. QObject/QVariant/QString은 어댑터·codec 경계에 머문다. C++ 코어가 편집 가능 여부와 참조 규칙을 최종 판단하며 UI disable만으로 방어하지 않는다.

## 명령·작업·Undo 계약

```text
prepare(documentSnapshot, CommandRequest, CancellationToken) -> EditPlan
preview(EditPlan) -> ImpactReport
validate(currentDocument, EditPlan) -> ValidationReport
apply(validatedPlan) -> ChangeSet + revision
```

Request에는 commandId, projectInstanceId, documentId, baseRevision, targetRefs, typed arguments가 필요하다. EditPlan은 additions/removals/fieldChanges/geometryRefs/relationChanges/presentationChanges, dependency changes, warnings, proposed selection을 가진다. 미리보기 승인 token은 plan hash와 revision에 묶는다. 표시만 바꾼 뒤 다른 계획을 적용할 수 없다.

합병 시 생존 대상 ID를 사용자가 지정한다. 자식 parent/sovereign, 기간 관계, 분포 territorial 참조, 라벨 소속과 표시 설정을 역참조 인덱스로 수집한다. 동일 분포 레이어 항목이 합쳐져도 비율을 더하거나 100으로 정규화하지 않는다. 충돌한 항목은 독립 항목으로 보존하거나 명시적 사용자 해결을 요구한다. 종류 전환도 원본 ID 유지, 국가 전용 속성은 inactive typed extension으로 보존하고 현재 기능에서 편집하지 않는다.

한 apply는 **한 Undo 항목**이다. before/after field patch와 immutable geometry refs로 복원하며 도형 전체를 속성 변경마다 복제하지 않는다. apply 전에 검증된 후보 문서를 완성한 후 swap한다. 오류/취소는 문서·revision·dirty·Undo cursor·선택을 변경하지 않는다. 성공 후 렌더 알림 실패는 커밋 실패로 위장하지 않고 렌더 재구축 오류를 별도로 보고한다. Undo/Redo도 revision을 증가시켜 오래된 작업을 무효화한다. no-op은 이력/dirty를 바꾸지 않는다.

백그라운드는 immutable snapshot을 읽고 결과만 반환한다. 적용 시 projectInstanceId(열기마다 새 값), documentId, baseRevision, jobId, cancel 여부가 모두 일치해야 한다. 저장된 documentId만으로 같은 파일을 다시 연 세션의 결과를 적용하지 않는다. 불일치는 STALE_RESULT, 취소는 CANCELLED로 폐기하며 최신 문서에 임의 rebase하지 않는다.

## Qt v3 저장 구조

`format="pandoeditor-project", version=3`. 웹 schemaVersion과 절대 공유하지 않는다. UTF-8 JSON을 유지하고 schemaVersion 3~5라는 이유로 Qt v3로 인식하지 않는다.

```json
{
  "format": "pandoeditor-project",
  "version": 3,
  "documentId": "new-document-id",
  "units": [],
  "relations": [],
  "geometries": [],
  "distributionLayers": [],
  "distributionEntries": [],
  "labels": [],
  "hydroFeatures": [],
  "genericFeatures": [],
  "presentation": {
    "userLayers": [],
    "membership": [],
    "groupOrder": [],
    "objectOrder": [],
    "groupStyles": {},
    "objectStyles": {},
    "visibility": {},
    "labelSettings": {},
    "distributionSettings": {},
    "physicalSettings": {}
  },
  "assets": [],
  "datasets": [],
  "provenance": [],
  "extensions": []
}
```

이 예시는 필드 구조 예시이며 기존 Qt validator에 입력하는 실행 fixture가 아니다. geometries 항목은 id/version/geojson; assets는 id/mime/content(내장 데이터)/source; datasets는 datasetId/version/source/availability/embeddedRefs를 가진다. 문서 런타임 revision, Undo, 캐시는 저장하지 않는다. 자동복구용 세션 파일은 버전이 분리된 별도 파일로 설계하며 정상 프로젝트를 delta로 바꾸지 않는다.

Qt v1: countries의 id/name/color/geometry는 그대로; v1 codec이 생성하던 countries 레이어, memo 빈 값, opacity 1을 동일하게 보충한다. Qt v2: memo/opacity/layerId와 모든 레이어 속성, bottom-to-top 배열 순서를 그대로 이관한다. 색상은 RGB를 보존하고 도형 rings/holes/parts/좌표를 바꾸지 않는다. 기존 국가 ID 재발급 금지. 새 documentId와 내부 geometry ID만 생성한다. legacy 파일의 알 수 없는 속성도 extension에 보존한다. 오류 파일을 임의 보정하지 않는다.

## 웹 저장 필드 대응표

분류: D 직접 매핑, B 기본값 보충, T 의미 변환, P 원본 보존, X 첫 가져오기 거절. E번호 근거는 기능 조사 소스 색인 참조. 아래는 E30의 root 허용 필드 전체다.

| 웹 root 필드 | 처리 | Qt v3 대상 / 규칙 |
|---|---|---|
| format | T | project-state/autosave-full만 full 입력; Qt format으로 별도 기록 |
| schemaVersion | D | provenance.sourceSchema; 웹 마이그레이션 후에도 최초 번호 기록 |
| version | D | provenance.sourceAppVersion; Qt version과 구별 |
| savedAt | D | provenance.sourceSavedAt; Qt 저장 시 원본 시각 덮어쓰지 않음 |
| countriesData | T | units(kind=country) + geometries; Feature ID/geometry/properties.name/validFrom/validTo 보존 |
| countryDelta | X | delta 형식은 BASE_DATA_REQUIRED. 웹에서 완전 저장본 내보내기 안내 |
| countryOverrides | T/B | name/notes→unit, color→objectStyles, capital/flagDataUrl→CountryProperties, locked→unit |
| sourceInfo | D/P | provenance, 모르는 구조는 원본과 함께 유지 |
| labels | T/P | PlaceLabel + geometry/labelSettings; 미해석 속성은 source fragment |
| genericFeatures | T/P | GenericFeature + source; 여섯 GeoJSON 종류 유지 |
| hydroEdits | T/P | HydroFeature; pandolab_schema_version/domain/id/category/name/notes/editorColor 및 나머지 props 보존 |
| territorialUnits | T | units + geometries + base relations; 국가를 중복 생성하지 않음 |
| territorialRelations | T | dated relations; id/unitId/parentId/sovereignId/validFrom/validTo; schema1 출처 유지 |
| distributionLayers | D/T | id/type/name/color/locked/parentId/groups/validFrom/validTo/metadata; parentRef 변환 |
| distributionEntries | D/T | id/layerId/mode/territorialUnitId/geometry/share/certainty/validFrom/validTo/metadata |
| labelSettings | T/P | presentation; priority/minZoom/maxZoom/manualPosition/pinned/collisionGroup 포함, country key는 ref로 변환 |
| distributionSettings | D | renderMode/boundaryVisible; dominant/intensity 의미 유지 |
| layerPresentation | T/P | schema3 overlayOrder/styles/objectStyles/objectOrder를 그룹·객체 순서로 변환 |
| physicalSettings | P→T | presentation.physicalSettings의 원본 도메인 blob; 해석 전 UI 미지원 |
| layerVisibility | T | visibility.groups 및 symbol flags; 누락 기본값은 웹 정규화 적용 |
| itemVisibility | T | 도메인별 ObjectRef visibility; countryLabels도 별도 표시 대상 |
| baseDataset | D/P | datasets의 식별 정보; 전체 포함됐다는 뜻 아님 |
| landObjectModel | D/P | provenance.sourceContracts; lossless-fallback/directCreation=false 계약 확인 |
| territorialModel | D/P | sourceContracts; types/coverageModes/coastlineAuthority 확인 |
| distributionModel | D/P | sourceContracts; sharesAreIndependent=true/shareRange 보존 |
| physicalSourceInfo | D/P | terrain/hydro dataset/version/coordinatePolicy/selection, 실제 내장 자료와 분리 |

territorialUnits 세부: schemaVersion→sourceSchema, unitType→kind, name/notes/locked/validFrom/validTo→unit, parentId/sovereignId→base relation, coverageMode→unit, style→objectStyles, metadata/sourceFolderId/sourceLibraryId/sourceGeometryVersion→provenance 및 source metadata. 분포 share는 개별 0~100, 합계 제한 없음. Generic canonical properties는 schemaVersion/name/notes/color/locked/source; source의 schemaVersion/kind/dataset/sourceId/sourceFormat/sourceType/version/importedAt/details 모두 유지한다.

웹 표시 그룹 countries/subunits/regions/languages/ethnicities/religions/rivers/lakes/genericFeatures/labels/terrain 및 hydro/countryLabels 별칭을 보존한다. layerVisibility의 basemapLabels/countryFlags/subunitLabels/subunitFlags/regionLabels/regionFlags는 객체 가시성과 별개의 symbol 설정이다. itemVisibility의 hydro와 표시 river/lake 그룹을 혼동하지 않는다. styles는 opacity/boundaryVisible/boundaryWidth/labelsVisible/blendMode를 원본 보관하며 현재 웹 정규화로 얻은 유효 표시값과 원본 필드를 구별한다.

국기: flagDataUrl **필드 없음**은 Default, **null**은 None, 문자열 내장 데이터는 Embedded. 빈 값/잘못된 형식은 웹 정규화와 검증 결과를 따르며 임의 기본 국기로 바꾸지 않는다. Default 자료가 없으면 unavailable 상태로 표시한다. 추가 조사에서 하위단위·지방도 metadata.flagDataUrl을 수정하는 연결이 확인됐다(F46). 국가의 수도·국기 전용 속성과 구별하여 이 값은 ObjectRef별 TerritorialSymbolStyle의 동일 3상태로 매핑하고 원본 metadata를 보존한다. 국가 전용 속성만 만든 뒤 비국가 국기 설정을 버려서는 안 된다. 웹 projection/layerFolders/view는 세션 필드이며 full project root 저장 필드가 아니다. 이를 자동 복원했다고 주장하지 않는다.

## 웹 마이그레이션과 미지원 보존

입력 원본을 먼저 불변 보관 → 형식/크기/JSON 검증 → 웹 3→4→5 의미 마이그레이션 → 현재 웹 모델 검증 → Qt 매핑/검증 → 사전 보고 → 사용자 확정 → 문서 교체 순이다. Qt에서 브라우저 런타임을 실행하지 않고 웹 migration fixture의 입출력과 일치하는 C++ 변환기로 구현한다.

3→4: legacy editor_* 국가 속성을 override로 분리, 기존 override 우선; Feature ID 결정 우선순위와 legacy drawings→generic/source 변환을 E32대로 적용. 기존 유효 ID가 있으면 유지하며 ID 없는 legacy에 한해 웹의 결정적 fallback 규칙을 재현한다.
4→5: territory/admin→subunit, 레거시 adminLevel/isRemainder/partition metadata 제거 의미, territorial object key와 그룹/객체 visibility/order/style 변환을 E33대로 적용한다. 삭제되는 원본 필드는 migration archive에 남겨 원본 손실을 방지하되 활성 모델의 규칙으로 되살리지 않는다. 임의 metadata 문자열 전체에 replace를 하지 않는다.

미지원 payload envelope:
```text
extensionId, sourceFormat, sourceSchema, jsonPointer, payload,
status(unsupported|migrationArchive), dependencyKnowledge(known|unknown),
dependencies:[ObjectRef], forbiddenEffects:[delete,type,geometry,relation,...]
```

payload는 숫자 토큰 정밀도까지 보존 가능한 lossless JSON fragment로 읽고 저장한다(일반 double JSON 파싱으로 알 수 없는 큰 정수 손실 금지). 공백/키 배치의 바이트 동일성은 약속하지 않지만 값·타입·배열 순서·null/필드 부재는 보존한다. 중복 키 JSON은 모호하므로 거절한다. canonical mapped field와 extension의 원본은 역할이 다르다: 활성 값은 canonical만 소유하고 archive는 가져오기 당시 증거다. 재열기 때 archive 값으로 수정된 canonical을 덮어쓰지 않는다.

알려진 dependencies는 역참조 인덱스에 등록한다. 관련 객체 삭제/종류/도형/관계 변경 중 forbiddenEffects와 겹치면 UNSUPPORTED_DEPENDENCY로 차단. 알 수 없는 참조 구조가 하나라도 있으면 문서 전체 구조 변경(추가 포함, 삭제/종류/도형/관계/ID 변경)을 보수적으로 차단한다. 명시적 안전 목록의 표시 이름·메모 수정만 허용하되 해당 필드에 알려진 의존성이 있으면 이것도 막는다. 정책 변경/원본 버리기 기능은 이번 이식에 포함하지 않는다.

미지원 도메인의 원본 JSON 보존은 표시·편집 지원 완료가 아니다. 사전 보고서와 객체 목록에 retained/not-rendered/read-only를 분리한다. 지원이 추가되면 새 migrator가 fragment를 검증하여 typed model로 승격하고 의존성을 재계산한다.

외부 데이터는 정확한 datasetId/version/source와 included data 목록을 구분한다. ID만 있으면 unavailable-reference다. 자동 다운로드·최신판 치환·다른 해안선 대체를 하지 않는다. 내장 도형만으로 가능한 속성 편집은 허용하고 외부 자료가 필수인 정합 등은 DATASET_UNAVAILABLE로 막는다. 외부 URL을 임의 실행하거나 파일 경로를 자동 열지 않는다.

## 가져오기 원자성과 오류

ImportReport는 원본 형식/버전, 객체별 수량, 매핑/기본값/변환 내역, 미지원 fragment/표시 제한, 누락 dataset, ID/관계/도형 오류, 막히는 명령을 제공한다. 보고서는 candidate hash 및 현재 세션 revision과 연결한다. 오류가 하나라도 있으면 confirm 불가. 경고는 명시적 확인을 받고만 교체한다.

decode/prepare는 현재 문서를 바꾸지 않는다. confirm은 active document/revision이 보고 당시와 같은지 확인하고 미저장 변경 처리를 요청한다. 사용자가 저장을 선택했는데 실패하면 교체하지 않는다. 성공한 교체는 새 projectInstanceId와 새 Undo 이력을 시작한다. 실패·취소는 기존 dirty/선택/초안/Undo를 유지한다. 파일 저장도 임시 파일 작성·검증 후 원자적 교체 방식; 실패 시 기존 파일을 유지한다.

| 오류 코드 | 의미/행동 |
|---|---|
| INVALID_JSON / DUPLICATE_KEY / LIMIT_EXCEEDED | 파싱·중복 키·명시된 크기/깊이 제한 실패, 현재 작업 유지 |
| UNSUPPORTED_FORMAT / UNSUPPORTED_VERSION | Qt 미래 버전/웹 범위 밖 버전, 무리한 다운그레이드 금지 |
| BASE_DATA_REQUIRED | delta 제외, 웹 완전 저장본 안내 |
| INVALID_GEOMETRY / DUPLICATE_ID / DANGLING_REF | 위치(jsonPointer)/대상 ID를 보고, 자동 재발급·삭제 금지 |
| RELATION_CYCLE / SOVEREIGN_MISMATCH / PERIOD_CONFLICT / INVALID_DATE | 관계·기간 오류, 전체 가져오기 거절 |
| UNSUPPORTED_DEPENDENCY / LOCKED / DATASET_UNAVAILABLE | 요청 명령 불가, 문서 변경 없음 |
| CANCELLED / STALE_RESULT | 사용자 취소/오래된 작업 결과 폐기, 오류로 문서 손상시키지 않음 |
| SAVE_FAILED | 원본 파일·미저장 문서·이력 유지 |

구현 시 기본 입력 제한은 256 MiB UTF-8, 중첩 128단계로 시작하고 초과는 명시적 오류로 처리한다. M7에서 대규모 요구에 맞춰 스트리밍/상한 재평가하며 제한을 임의로 없애지 않는다. 데이터/모델의 기타 JSON 속성은 손실 없이 보존할 수 없으면 성공으로 처리하지 않는다.
