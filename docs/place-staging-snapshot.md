# 웹·앱 검수 도시 정본 동기화

웹의 검수 정본 `data/places-tier1-major-cities`에서 **12개 배치·72개 도시**를 반영한 읽기 전용 자료다.
배치별 개수: 9, 9, 7, 5, 5, 4, 8, 8, 4, 4, 5, 4개.

## 12차 — 중부·동유럽 잔여 수도

| 도시 | GeoNames ID | 대표 원어 | 이력·구분 |
|---|---:|---|---|
| 브라티슬라바 | 3060972 | Bratislava (sk) | 독일어 Pressburg·헝가리어 Pozsony·슬로바키아어 Prešporok, 1919-03-27 명칭 고시 |
| 키시너우 | 618426 | Chișinău (ro) | 러시아어 Кишинёв, 1812·1918·1940·1991 정치 변화를 도시 개칭과 구분 |
| 룩셈부르크 | 2960316 | Lëtzebuerg (lb) | 도시·국가·시 행정구역 분리, 프랑스어·독일어명 병존 |
| 파두츠 | 3042030 | Vaduz (de) | 리히텐슈타인의 소규모 수도 예외 포함, 영어와 원어 동일 |

1919년 브라티슬라바의 3월 27일 관보 고시는 표기 전환 기준으로 사용한다.
1919년 2월의 행정 사용을 설명하는 슬로바키아 정부 자료와 정밀도 차이를
각 레코드의 `researchNote`에서 명시했다. 1801년 프레스부르크는
독일어 Pressburg 기준 **판도편집기의 옛 지도 편집 표기**이며, 당시
헝가리어 Pozsony의 존재를 부정하지 않는다.

## 동기화·검증 구조

- 웹 원본 커밋: `b2dfbe523be315331073c8781ee2c141edab74c6`.
- 앱 `reports/places/tier1-major-cities-batch*.json`: 고정된 웹 원본 복사본.
- `reports/places/source-manifest.json`: 원본 Git blob SHA·배치별 도시 수·고유 ID 총수.
- `tools/place-runtime-contract/v2-snapshot.test.mjs`: 해시·ID 중복·12차 명칭 전환 독립 검증.
- `tests/place_sync_contract_tests.cpp`: Qt 명칭 선택 함수의 1919-03-26/27 경계 테스트.

웹의 `contracts/places/v2.json` 바이너리 골든 사례 네 건은 별도로 유지한다.
이 배치는 지명 검수 자료이며 웹·앱 모두 실제 지도용 PLAC v2 manifest가
아직 빈 상태다. 도시를 지도에 나타내려면 타일·검색 인덱스 게시가 필요하다.
