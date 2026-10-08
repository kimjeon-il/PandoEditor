# 대도시 검수 데이터: 앱 읽기 전용 스냅샷

원본은 웹 저장소 `kimjeon-il/Pando`의 `fc0cb82a9e363be6a3009cf07932a3c9ce59ddd2` 커밋이다.

- 검수 배치: `reports/places/tier1-major-cities-batch*.json` (8개)
- 지명 정책: `historical-display-policy.json`, `korean-map-label-policy.json`
- 원본 검증: `reports/places/source-manifest.json`에 Git blob SHA 고정

웹이 데이터의 유일한 검수 정본이다. 이 복사본은 앱 PLAC v2 생성·검증용으로
사용하며, 변경 시 원본 커밋과 무결성 및 리뷰 이력을 대조해야 한다.
이 JSON만 복사한다고 앱 내장 지명 타일이 자동 배포되지는 않는다.
현재 `assets/place/manifest.json`은 빈 상태다.
