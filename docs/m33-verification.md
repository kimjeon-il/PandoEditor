# M3.3 검증 대응표

기준 웹 원본은 `58e4087f85aa51884bc4ab80959e05010d94d7d5`다. 아래 테스트가 모두 통과해야 M3.3을 완료로 판정한다. `territorial_structure_tests`의 각 항목은 하나의 문서에서 순차 실행되지만 매 단계마다 revision·candidate·Undo를 독립 검증한다.

| 사양 | 검증 테스트 |
|---|---|
| S01 | `territorial_structure_tests`: parent 변경·단일 Undo |
| S02 | `model_tests`: 순환·sovereign 불일치 거절, `territorial_structure_tests`: invalid plan |
| S03 | `geometry_predicate_tests`, `territorial_structure_tests`: parent 밖 생성/관계 거절 |
| S04 | `model_tests`: dated boundary graph, `migration_tests::temporalWireAndAllGeometryKinds` |
| S05 | `territorial_structure_tests`: region/base relation 명령 경로 |
| S06 | `ui_tests::territorialStructureDeleteDialogAcrossPcAnd360px`: region parent command 미노출 |
| S07 | `m33_structure_oracle --group delete`, `retained_reference_tests::unknownAndArchiveDependenciesBlockStructuralDelete` |
| S08 | `m33_structure_oracle --group delete`: base child 및 child-first fixture |
| S09 | `retained_reference_tests::deleteRewritesCandidateAndUndoRestores`, `deleteRewritesPresentationKeys` |
| S10 | `m33_structure_oracle --group delete`: mixed multi-delete 원자성 |
| S11 | `territorial_structure_tests`: base child gate |
| S12 | `territorial_structure_tests`: candidate 전체 validation |
| S13 | `retained_reference_tests::unknownAndArchiveDependenciesBlockStructuralDelete` |
| S14 | `territorial_structure_tests`: prepared subunit 생성·Undo |
| S15 | `territorial_structure_tests`, `ui_tests::territorialCreateSetupWaitsForPreparedGeometryAcrossPcAnd360px` |
| S16 | `territorial_structure_tests`: duplicate ID·부모 밖 geometry 거절 |
| S17 | `territorial_structure_tests`: promotion plan과 owner set |
| S18 | `territorial_structure_tests`: promotion patch confirm·Undo |
| S19 | `territorial_structure_tests`, `retained_reference_tests::conversionRewritesAllKnownPathsLosslessly` |
| S20 | `territorial_structure_tests`: demotion patch confirm·새 ID·Undo |
| S21 | `ui_tests::territorialConversionSetupDoesNotMutateDocumentAcrossPcAnd360px` |
| S22 | `retained_reference_tests::staleDeletePreviewLeavesDocumentUntouched` |
| S23 | `ui_tests::territorialConversionSetupDoesNotMutateDocumentAcrossPcAnd360px` |
| S24 | `ui_tests::territorialConversionSetupDoesNotMutateDocumentAcrossPcAnd360px` |
| S25 | `retained_reference_tests::deleteThenV4RoundTripPreservesRewrittenExtension`, `migration_tests` |
| S26 | `ui_tests`의 구조 삭제·전환·생성 1100px/360px 반복 fixture |
| S27 | `retained_reference_tests::unknownAndArchiveDependenciesBlockStructuralDelete` |
| S28 | `territorial_structure_tests`, `geometry_predicate_tests`: structural-only geometry 불변 |

Oracle 그룹은 `parent`, `sovereign`, `containment`, `delete`, `create`, `convert-preview`, `transfer-plan`, `reference-rewrite`다. 기대값은 고정 웹 모듈을 직접 실행해 만들며 native probe에 기대값을 전달하지 않는다.

M4가 공급하지 않는 union/difference/split 계산, Android 실기 입력, 전체 세계지도는 이 표의 완료 범위가 아니다. `GeometryPatch` 부재·revision/owner 불일치는 문서 비변경 거절이 완료 동작이다.
