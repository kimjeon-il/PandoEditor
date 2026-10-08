# 13차 — 유럽 소국·지중해 수도 검수 및 앱 동기화

웹 검수 정본 `data/places-tier1-major-cities`에서 검증한 **13개 배치·77개 도시**의 읽기 전용 앱 복사본.
배치별 도시 수: 9, 9, 7, 5, 5, 4, 8, 8, 4, 4, 5, 4, 5개.

| 도시 | GeoNames 취락 ID | 원어 대표형 | 참고 구별 |
|---|---:|---|---|
| 안도라라베야 | 3041563 | Andorra la Vella (ca) | AD PCLI 국가·ADM1 구분 |
| 산마리노 | 3168070 | San Marino (it) | 3168068 국가 및 Castello ADM1 구분 |
| 모나코 | 2993458 | Monaco (fr) | 2993457 공국 및 Monaco-Ville 지구 구분 |
| 발레타 | 2562305 | Il-Belt Valletta (mt) | 8334638 몰타 ADM1 구분 |
| 니코시아 | 146268 | Λευκωσία (el) / Lefkoşa (tr) | 146267 ADM1 및 유엔 완충지대 구분 |

산마리노 301년 건국은 전승으로 기록하며 도시 정식 건립일을 확정하지 않는다.
발레타의 1566년 착공과 1571년 기사단 수도 이전은 명칭 변경이 아니다.
니코시아의 1963-12-30 그린라인 설치 및 1974-08-16 정전선 확장은
역사적 사건으로만 관리한다. 경계 폴리곤은 별도의 GIS 검증 대상이다.
이 사건들로 한국어·그리스어·튀르키예어명을 자동 변경하거나 국가 귀속을 확정하지 않는다.

## 데이터 동기화

- 웹 원본 검수 커밋: `921440813ac92b6a88907c6e5359422b1e6c5230`
- 앱 고정 원본: `reports/places/tier1-major-cities-batch*.json`.
- `reports/places/source-manifest.json`: 원본 Git blob SHA 및 모든 취락의 고유 GeoNames ID 수.
- `tools/place-runtime-contract/v2-snapshot.test.mjs`: 13차 ID·언어·분단 사건·소국 국가/행정구역 구분 검증.
- `tests/place_sync_contract_tests.cpp`: Qt 선택기에서 3개 언어 동시 표시·동일 명칭 중복 제거 검증.

현재 양쪽 `main`에는 병합하지 않았으며 실제 지도용 PLAC v2 manifest 타일도 게시하지 않았다.
