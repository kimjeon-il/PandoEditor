# 웹·앱 대도시 목록 동기화

**검수된 도시 55개**, 검수 JSON **8개 배치**이다. '8개'는 도시 수가 아니라 파일 배치 수다.
8개 배치의 도시 수는 순서대로 **9, 9, 7, 5, 5, 4, 8, 8**개다.

원본 웹 커밋: `fc0cb82a9e363be6a3009cf07932a3c9ce59ddd2`
앱의 `reports/places/tier1-major-cities-batch*.json` 파일은
원본 웹 데이터와 내용이 완전히 동일하다. 웹은 이 검수 자료의 정본이다.

`reports/places/source-manifest.json`의 `reviewInventory`에 배치별
레코드 수와 전체 고유 GeoNames ID 수를 기록했고, JS/Qt 테스트에서
각 배치의 수·ID 중복과 원본 SHA를 모두 검증한다.

별도의 GeoNames 후보 32,277건은 검수 대기 자료다. 웹·앱에서 공통으로
게시되는 확정 도시 목록과 혼동하거나 자동 채택하면 안 된다.

주의: `assets/place/manifest.json`과 웹 `assets/data/places/manifest.json`
모두 런타임 타일이 아직 0개이다. 검수 기록 55개를 앱 지도에 현재
실제로 표시하고 있는 것은 아니다. 별도 PLAC v2 타일 생성·게시
단계가 필요하다.
