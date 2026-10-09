# 수계 v0.13.2 2파일 패키지 — 웹·앱 전체 브랜치 적용 기록

- 적용일: 2026-10-09
- 웹 저장소: `kimjeon-il/Pando` — 7개 브랜치 모두 v0.13.2 패키지 사용
- 네이티브 저장소: `kimjeon-il/PandoEditor` — `work/gis` 원본을 기준으로 나머지 4개 브랜치에 수계 변경만 선택 반영
- 전체 제품 브랜치를 무관한 GIS 코드로 자동 병합하지 않음.

## 단일 실행용 패키지

`hydro/v0.13.2/manifest.json` 53,375 bytes, Git Blob `6f6606ad01be6a9e295995222bfa984876e8caff`, SHA-256 `2bde5c10bdf4d26fd29d2b983b70029022cfaa9fda816a105e6f4338b85f0d24`.

`hydro/v0.13.2/hydro.bin` 11,974,120 bytes, Git Blob `6e3365aed7fccc4ecb2faee71d730e31af6058b4`, SHA-256 `91c2268c6dbfdfa7fc6103d8ad0b4eeca71f6c522df5c0118db69a3654ff532a`.

인덱스·기본/상세 메타데이터·3개 압축 샤드의 구조/오프셋·각 영역 SHA-256은 매니페스트에 유지. 웹 HTTP Range와 네이티브 파일 seek 부분 읽기, 레거시 v0.13.1 사용자 지정 출처 읽기 호환성 유지. 이전 v0.13.0·v0.13.1은 별도 삭제 검증 전까지 보존한다.

## 앱 브랜치별 선택 반영

| 브랜치 | 초기 통합 커밋 | 후속 컴파일 교정 | CI 자동 실행 범위 반영 |
| --- | --- | --- | --- |
| main | `9ce06b9b6197b9683eb128438b9522c198db5c65` | `aeb37c65b695115da6b80690919e4291b5de414d` | `23e717897fab1e8e7b05d3ecefa94b6010316d02` |
| work/objects | `2441ea1feea780f38fc9bc6370c0204fd06b3a8f` | `30f5ae6d199464010b361846c363f01f434f2e7f` | `262270653e2995784ca590aa047f5a5bebc7a666` |
| work/places | `1ccf7cfa49f2ccfb949d4cf0e5239e722027191b` | `e828412ce0c1b0ab0a5d0940766f14484df82b6a` | `97596d6ceef0991a155eb44bbabca16345e352e9` |
| work/ui | `e1f3433838c8b15b4e0215135ee2e0b295a51cbd` | `768df4051b8e271b8e540c9397672ef385dab2a0` | `0bffbfdcc1dd00bc1388b0ee9575e00b337e4c98` |

각 4개 브랜치에서 19개 지정 파일만 변경. `app/physicaldatastore.cpp`의 기존 컴파일 오류(`const sourceUrl`/ `const explicitSource` 유형 누락)를 GIS에서 이미 검증한 `const auto`/`const bool` 선언으로 별도 정정. 원인과 수정 범위가 확인된 뒤에 반영함.

`work/places`에서는 지명 CMake 타깃(`placenamedisplay`, `place_sync_contract_tests`)과 이름 다국어 라벨 `nameLines` 코드를 그대로 유지. 앱 `work/gis`는 자체 승인된 WorldDataset 매니페스트 v2 형식을 유지하며, 나머지 4개는 기존 v1 포맷과 국가 데이터 출처를 보존하고 수계 고정값만 교체.

## CI 실검증

- `work/gis` 기존 검증: [#37932510796](https://github.com/kimjeon-il/PandoEditor/actions/runs/37932510796) — 58/58 통과.
- `main`: [#37934955686](https://github.com/kimjeon-il/PandoEditor/actions/runs/37934955686) — 58/58 통과.
- `work/objects`: [#37934961568](https://github.com/kimjeon-il/PandoEditor/actions/runs/37934961568) — 58/58 통과.
- `work/places`: [#37934965564](https://github.com/kimjeon-il/PandoEditor/actions/runs/37934965564) — 58/58 통과.
- `work/ui`: [#37934969159](https://github.com/kimjeon-il/PandoEditor/actions/runs/37934969159) — 58/58 통과.

매 실행에서 pinned world asset 검사, Qt 빌드, 수계 manifest(11), 런타임(9), 실제 통합 패키지(3), 하천선택·취소·복구(26), 물리 자산 저장소(9)를 모두 통과. 신규 파일/기존 파일의 내용은 모든 웹 7개·앱 5개 브랜치에서 동일한 Git Blob이 출처.

### 무관한 기존 실패의 분리

앱 `work/places`의 별도 지명 Snapshot Parity CI는 이미 통합 이전 브랜치 커밋 `f92c9257250e5486e368df9fad7c46cbdb208b84`에서 [#37926116193](https://github.com/kimjeon-il/PandoEditor/actions/runs/37926116193) 실패 중이었다. [통합 반영 후 #37934437356](https://github.com/kimjeon-il/PandoEditor/actions/runs/37934437356)에서도 오사카 지명 연구 메모 `'편집상'` 문자열을 기대하는 단위 검사가 실패했다. 해당 테스트와 지명 데이터는 본 수계 변경 대상에 없으며 수정하지 않았다. 당시 Native place 컴파일 실패에 포함된 `physicaldatastore.cpp` 선언 오류는 이번 교정에서 해소했고 수계 전용 CI의 네이티브 빌드가 통과했다. 이 사실을 전체 지명 회귀 성공이라고 확대하지 않는다.

## 잔여 작업

- 기존 수계 v0.13.0/v0.13.1 자산은 레거시 출처/연구·이전 사용자 프로젝트 호환성 검증 후 별도 삭제 검토.
- 실제 사용자 저장 프로젝트의 외부 출처와 구형 네이티브 배포본 전체를 검사한 것은 아님.
- 무관한 지명 스냅샷 실패는 장소 데이터 워크플로에서 별도 수정해야 함.
