# 웹·앱 대도시 검수 목록 동기화

웹의 지명 검수 정본 `data/places-tier1-major-cities`에서 **11개 배치, 68개 도시**를 검증한 읽기 전용 복사본이다.
각 배치 도시 수: 9, 9, 7, 5, 5, 4, 8, 8, 4, 4, 5개.
"11개"는 도시 수가 아니라 배치 수이며, GeoNames P/PPLC 취락 ID만 별도로 보존한다.

## 11차: 서부 발칸 수도·코소보 행정 중심지

| 도시 | GeoNames ID | 현행 기본 언어명 | 검수 사항 |
| --- | ---: | --- | --- |
| 사라예보 | 3191281 | Sarajevo | 보스니아어·크로아티아어·세르비아어 병존, Saraybosna는 튀르키예어 변이 |
| 포드고리차 | 3193044 | Podgorica | 1946-07-13부터 티토그라드, 1992-04-02부터 포드고리차 |
| 스코페 | 785842 | Скопје | Shkupi·Üsküp 병용, 고대 Scupi는 별도 유적 |
| 티라나 | 3183875 | Tiranë | 1920년 임시, 1925년 정식 수도 지정은 개칭이 아님 |
| 프리슈티나 | 786714 | Prishtina | 알바니아어·세르비아어 병존. 코소보 국가 지위는 별도 역사·정치 정보 |

포드고리차의 1946-07-13은 시청 연혁 기준 표기 전환일이다. 초기 법령의 제정일·철자와
관련된 이견은 원본 `historicalGeography.notes[].researchDetail`에서 구별해 보존한다.
프리슈티나의 `sourceCountryCode: XK`는 GeoNames 식별 코드로, 어떤 시대의 주권이나
국제적 국가 승인 여부를 자동 판단하는 필드가 아니다.

## 정본·검증

- 웹 원본 커밋: `164b9ae6a98a516c8504c565337976d3c71a56d4`.
- 앱 `reports/places/tier1-major-cities-batch*.json`: 웹 원본 그대로 미러링.
- `reports/places/source-manifest.json`: 검수 배치 수와 개별 파일 Git blob SHA.
- `tools/place-runtime-contract/v2-snapshot.test.mjs`: 모든 배치의 취락 ID 중복·누락,
  정책 출처와 실제 파일 해시, 11차 표기 기간 검증.
- `tests/place_sync_contract_tests.cpp`: Qt의 역사적 한국어 표기 함수에 대해
  1946-07-12, 1946-07-13, 1992-04-01, 1992-04-02를 검사.

Web `contracts/places/v2.json`의 원시 바이너리 골든 사례는 기존 4개로 유지한다.
이번 작업은 지명 검수 자료 동기화이며, Web/App 실제 `places` 런타임 manifest는 여전히 비어 있다.
검증 자료를 지도에서 표시하려면 별도 PLAC v2 타일/검색 인덱스 생성·게시 단계가 필요하다.
