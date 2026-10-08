# 웹·앱 대도시 목록 동기화

검수된 도시 **59개**, 배치 **9개**. 9개는 도시의 수가 아니라 검수 배치 파일의 수다.
배치별 도시 수: 9, 9, 7, 5, 5, 4, 8, 8, 4개.

## 9차: 발트 3국 및 벨라루스 수도

- 탈린 (588409): 에스토니아어 Tallinn, 역사적 독일어 Reval 및 러시아어 Ревель
- 리가 (456172): 라트비아어 Rīga, 영어 Riga
- 빌뉴스 (593116): 리투아니아어 Vilnius, 폴란드어 Wilno(역사적·언어적 변이)
- 민스크 (625144): 벨라루스어 Мінск, 러시아어 Минск

4개 모두 GeoNames P/PPLC 취락이며 동일 이름을 가진 ADM 행정구역과 구별한다.
구체적인 역사적 지명 변이의 일별 자동 전환은 독립된 사료가 없는 한 설정하지 않았다.

## 출처 및 검증

- 웹 원본 검수 자료: `kimjeon-il/Pando` 커밋 `6a69ad1a25d7ed3f7b7b3ed6ade116a83df44b4c`
- 앱 `reports/places/tier1-major-cities-batch*.json`: 웹 원본 복사본
- `reports/places/source-manifest.json`: 원본 Git blob SHA 및 9배치/59도시 ID 수
- `tools/place-runtime-contract/v2-snapshot.test.mjs` 및 `tests/place_sync_contract_tests.cpp`: 소스 해시·배치별 건수·ID 중복 검증

기존 `contracts/places/v2.json`의 4개 대표 검증 사례는 별도로 고정되어 있으며,
9차 신규 검수 자료에 대한 자동 런타임 변환은 아직 없다.

주의: **웹·앱 둘 다 지도용 PLAC v2 manifest는 비어 있음.**
검수 목록 59개를 모두 지도에 표시하려면 지명 타일 빌드와 게시가 별도로 필요하다.
GeoNames 후보 32,277건을 전체 검증 완료로 취급하지 않는다.
