# 독일제국–덴마크 1914년 국경 1~26번 표석: 검증 기록

작업 브랜치: **work/gis** / 기준일: **1914-07-31** / 구간: Råhede Sluse → Gels Å/Gelsbro.

**현재 지위: 임시 후보선 (26.683km, 1,343점). 1914년 이전 실측 지형도 중첩 미실시. 최종 국경선으로 사용 금지.**

## 1. 1865년 국경획정위원회 기록

전사본: https://da.wikisource.org/wiki/Freden_i_Wien_(1864)_Gr%C3%A6nsereguleringskommissionen

* Artikel I § 1: 해상 경계와 해안의 표석 1번 연결.
* Artikel I § 2: 표석 **1~22**는 Vester Vedsted·Ribe·Seem 교구의 남쪽 경계 및 큰 배수로를 따름.
* 같은 조항: 표석 **22~26**은 **'den østlige Sognegrænse af Seem By'**를 따라 Gels Å/Gelsbro에 도달함.
* 표석 **26 이후**의 Gels Å 하도는 후속 구간에 해당함.

**주의:** 원문은 *Seem By*이지만 바로 앞의 *Sognegrænse*는 **교구 경계**를 뜻한다. 이를 'Seem By, Seem'이라는 현재의 지적단위(ejerlav) 경계로 바꿔 해석해선 안 된다. 22번 표석의 당시 원위치는 별도 역사 지도에서 확인해야 한다.

## 2. 자동 GIS 대조

관련 파일:

* \`german-denmark-1914-north-schleswig-001-026.geojson\` — 작업 후보선.
* \`german-denmark-1914-north-schleswig-001-026.diagnostics.json\` — 생성 당시의 수치.
* \`german-denmark-1914-north-schleswig-001-026.audit.json\` — 최신 재검증 결과.
* \`../../audit_north_schleswig_1914_001_026.py\` — 검사 실행 파일.

- LineString 1개, 좌표 1,343점, 전체 약 26.683km, 자체 교차 없음.
- 표석 1 및 Gels Å 기록상 끝점과 정합. **현대자료 내부 정합성이지 사료 독립검증이 아님.**
- 현대 DAGI *Seem Sogn* 경계와 후보선의 마지막 1·2·3·5·8km 표본은 모두 거리 0m. 애초에 이 교구 경계를 사용해 생성한 선이므로 순환 검증.
- 현대 *Seem By, Seem* 지적경계(ejerlav code **1300755**)와 후보선의 마지막 1km는 표본 거리 중앙값 약 **3,043m**. 이는 두 경계가 다른 데이터임을 보일 뿐 원문상의 'Sognegrænse'가 잘못되었다는 근거는 아님.
- 그랜세스티엔(Grænsestien)은 산책로로서 정밀한 국제경계 좌표로 대체할 수 없다.

교구 API: https://demo.geoinfo.dk/server/rest/services/DAGI_Hele_DK/MapServer/1
지적 API: https://demo.geoinfo.dk/server/rest/services/Matriklen_Hele_DK/MapServer/6

## 3. 표석의 현대 현위치와 1914년 해안

- 표석 **1**은 기록상 2014-08-20 원위치로 복원: https://graenseforeningen.dk/om-graenselandet/genforeningssten/raahede-sluse-graensesten-nr-1
- 표석 **2**는 원래 Sprækbro/Grænsegrøften에 설치됐으며 현재 학교 정원에 있음. 현재 좌표를 선의 기준점으로 사용 금지: https://graenseforeningen.dk/om-graenselandet/genforeningssten/graensesten-nr-2-vester-vedsted
- OSM에 나타나는 표석 **7b·11·22·28·30·31** 등의 현대 전시 위치 또한 원위치 검증 전에는 스냅 금지.
- Ribe 제방과 수문은 **1911~1915년 건설**되었으므로 현재 해안선으로 1914년 서단을 확정하면 안 됨: https://xn--grnsesti-k0a.dk/foelg-stien/vester-vedsted-til-hoem-21-km

## 4. 원도엽 확인: 미완료

| 지도 | 시기/축척 | 진척 |
|---|---|---|
| 덴마크 Datafordeler Preussiske målebordsblade | **1877~1912 / 1:25,000** | 공식 WMS 출처 검증. 원도엽 입수·중첩 대기, API key 필요 |
| 덴마크 Høje målebordsblade | **1862~1899 / 1:20,000** | 1869년 Vester Vedsted 도엽 사례 확인, 중첩 대기 |
| 덴마크 Lave målebordsblade | **1901~1971 / 1:20,000** | 남슐레스비히/쇤더윌란 첫 발행이 1938년인 영역은 1914년 도엽으로 사용할 수 없음 |
| HistoriskeKort 원도엽·지적도 | 개별 도엽 | 정확한 측량·개정·발행연도 검증 대기 |

공식 지도 정보:
- https://datafordeler.dk/dataoversigt/historiske-kort-og-data/preussiske-maalebordsblade-wms/
- https://datafordeler.dk/dataoversigt/historiske-kort-og-data/lave-maalebordsblade-wms/
- https://slks.dk/omraader/kulturarv/bevaringsvaerdige-bygninger-og-miljoeer/bevaringsvaerdige-bygninger-metode/atlas/vadehavet-kulturarvsatlas/bebyggede-strukturer/niveau-3-bebyggelsesstrukturer-i-byomraader/vester-vedsted
- https://historiskekort.dk/ — 위치 검색 예: \`POINT (8.80 55.28)\`, \`POINT (8.88 55.32)\`.

## 5. 후보선 수정 승인 조건

1. 1914년 이전 도엽의 **번호·측량/개정/발행연도** 확인.
2. 원본 확보, 복수의 고정 기준점으로 지오리퍼런싱하고 잔차 기록.
3. **1~22 남쪽 교구 경계 → 22~26 동쪽 교구 경계 → Gels Å** 연속 확인.
4. 표석 22 원위치와 1914년 제방·수로·해안형상 검증.
5. 변경 전·후 좌표 편차·선 연결·교차 검사.
6. 입증된 세부 구간만 수정. 그 전에는 \`doNotTreatAsFinal=true\` 유지.

사료를 열람하지 못한 단계를 완료 처리하지 않는다.
