# 1914년 독일제국–덴마크 국경: 5차 Hvidding-Ufer(0717) 서단 해안·제방 검토

**기준시점 1914-07-31. 대상은 1번 국경표석부터 0718 Hvidding 도엽 접점까지의 임시 육상 국경선 약 0.29380km. 1880년 원본 지도와 해안 제방 건설의 연대 차이를 구분하며, 마스터 좌표를 임의 수정하지 않는다.**

- 주 지도: [Deutsche Fotothek — Messtischblatt 33 Hvidding-Ufer, 1880](https://www.deutschefotothek.de/documents/obj/71051521), 1878년 측량·1880년 발행, 축척 1:25,000, SLUB WMS **10006009 / df_dk_0010001_0717**.
- 연속 도엽: [Hvidding 1880](https://www.deutschefotothek.de/documents/obj/71051522), WMS **10006008 / df_dk_0010001_0718**.
- 임시 국경 시작점: **(8.6622877°E, 55.2763431°N)** — 2014년 원위치 복원으로 소개된 1번 표석의 **현대 OSM 좌표**(ID **13124642635**). 1865/1914 해안 정확 좌표의 독립 측량 근거는 아님.
- 0718 도엽 접점: **(8.6666666667°E, 55.276881205°N)**.
- 표식 위상: 0717 구간 15개 정점의 단일 연속선으로 유지. 원본 1,343개 좌표를 변경하지 않음.
- 적용 기준: [GIS 역사적 국경 공통 규칙](../../../docs/historical-border-reconstruction-policy.md), 웹 최대 확대 flatZoom64, 콘텐츠 너비 2560 CSSpx, 실측 가능한 독립 사료가 있을 때 0.5 CSSpx 이하 목표. 해안·조약·연대 오류는 화면상 미세해도 검증.
- 작업 브랜치: 웹·앱 **work/gis**. main 병합 금지.

## 1. 1865년 국제경계 규정: 육상선·해상선 분리

[1865년 국경획정위원회 의정서 Article I §§1–2](https://da.wikisource.org/wiki/Freden_i_Wien_(1864)_Gr%C3%A6nsereguleringskommissionen):
- **§1 해상 국경:** 북해에서 **Mandø–Rømø 두 섬 간격의 중점**에서 **Ribe 대성당 탑 방향**으로 연결하는 선을 기초로 하며, 해안 가까이에서 이 선을 벗어나 **1번 국경표석**으로 연결된다.
- **§2 육상 국경:** **Vester Vedsted 교구의 남부 경계가 북해 해안에 이르는 지점**의 1번 표석에서 시작하여 동쪽으로 이어진다.
- 해상/육상 경계는 **동일한 현대 해안선을 따라 자동으로 이어붙이는 방식이 아님**. 본 0717 0.29km는 **육상 구간만** 검토한다. 해상선을 벡터화하거나 1914년 해안선을 확정하지 않는다.

## 2. 제방·하천·표석의 각기 다른 건설 시기

| 시설·기준 | 시대·변화 | 1914년 지도 해석 |
|---|---|---|
| **Fløjdiget**, Vester Vedsted 남쪽 측면 제방 | 1911년 착수, **1912/13년경 이미 조성**, 역사적으로 1864–1920년 옛 국경을 따라 남쪽으로 굴곡 | **1914년 존재 가능성 높음**, 1880년 원도엽에 나타나지 않는 게 정상 |
| **Ribediget 전체** | **1911–1915년 건설**, 공식 인계 **1915-05-10** | 사업 완공 시점과 국경 서단의 국부 제방 조성 시점을 혼동하지 않는다 |
| **Rejsbydiget (Christian X dike)** | **1923–1925년 건설** | 1914년에는 존재하지 않음. 남쪽 새 간척지·제방을 1914년 독일제국 해안으로 사용 금지 |
| **Råhede Sluse** | Rejsbydiget 건설 이후 Råhede Bæk에 새 수문 설치·유역 일부 재편 | **1914년 시설 아님.** 오늘날 'Råhede Sluse'라는 표석 위치안내 명칭만 현대 지명으로 사용 |
| **1번 표석** | 2014-08-20, 현지 역사단체가 '원위치'라고 판단한 자리에 복귀 | 물리적 복원 기록은 참조하되 1914년 정확한 조간대·해안선을 확정한 GPS 증거로 보지 않는다 |

근거:
- 덴마크 문화유산청 **Vadehavet kulturarvsatlas**: https://slks.dk/omraader/kulturarv/bevaringsvaerdige-bygninger-og-miljoeer/bevaringsvaerdige-bygninger-metode/atlas/vadehavet-kulturarvsatlas/bebyggede-strukturer/niveau-2-de-store-landskaber/marsken-ved-ribe-aa
- Vester Vedsted 지역사 **Fløjdiget**: https://vestervedsted.dk/floejdiget-i-vester-vedsted/
- Ribe 제방·수문 역사 안내(1915-05-10 공식 인계): https://danhostel-ribe.dk/wp-content/uploads/2022/03/Kammerslusen_A2skilte_WEB.pdf
- Grænseforeningen **Rejsby Diget**: https://graenseforeningen.dk/leksikon/rejsby-diget
- Jens Tyge Møller, **Afvandingsproblemer i Rejsbymarsken**, Geografisk Tidsskrift 56 (1957), Råhede Bæk의 1923~25년 이후 수문 설치와 유역 변동: https://tidsskrift.dk/geografisktidsskrift/article/download/46290/57034?inline=1
- Grænseforeningen **Råhede Sluse / grænsesten nr. 1**, 2014년 원위치 복귀: https://graenseforeningen.dk/om-graenselandet/genforeningssten/raahede-sluse-graensesten-nr-1

## 3. 중요한 지도 제작 원칙

1. **해안선과 제방선을 동일시하지 않는다.** 제방은 내수·홍수 보호 공사일 수 있고, 제방 밖에 조간대·자연 간석지·육지가 따로 존재할 수 있다.
2. 시대별 지도 상태를 명시한다. **1880년 지도는 제방 공사 이전의 해안자료**이며, 1914년 실제 해안선에 자동 적용하지 않는다.
3. 1914년 육상 국경의 1번 표석 위치와 **정확한 물가**를 구별한다. 2014년 복원점은 정확한 1914년 해안 기선이 아니다.
4. **Rejsbydiget 및 Råhede Sluse의 1923~1925년 이후 물길/지형은 1914년 해안 데이터에서 배제**한다.
5. 현재 지도상 최대 확대에서 **0.2938km 구간이 차지하는 픽셀 규모**를 우선 평가한다. 귀속이나 해안형상의 법적 변경이 없고 화면상 의미가 없는 굴곡은 추적하지 않는다.
6. 원래 독일–덴마크 해상국경은 **Mandø/Rømø 기준으로 별도 작업**하며, 이번 육상 0.29km와 혼합하지 않는다.

## 4. WMS 원본 영상 3개 대조 — 작업 실행 확인

웹 CI [GitHub Actions 실행 **37828150782**](https://github.com/kimjeon-il/Pando/actions/runs/37828150782) **성공**. 1878년 측량·1880년 발행된 0717 Hvidding-Ufer 및 0718 Hvidding의 WMS 원본을 합성해 다음 **원본·중첩 영상 3쌍**을 생성했다.

- **western-landing**: 서해안에 접한 역사 국경 시작점과 옛 해안의 상대위치.
- **historical-start-closeup**: 1880년 원본의 `1`번 경계표식과 현대 복원 1번 표석 및 후보선의 시작점.
- **wing-dike-context**: 1880년 측량 당시 남쪽 습지·해안 지형 전체와 1912년 무렵에야 등장한 측면 제방의 시대 차이.

**육안 판독:** 1880년 지도에는 바다에 접한 역사 국경의 **1번 표식 기호**가 보이고, 후보 국경선은 그 동쪽의 굵은 역사 경계 기호를 큰 방향에서 대체로 따른다. 2014년 복원 위치를 뜻하는 노란 표식은 역사 지도상의 1번 기호와 약간 떨어져 있다. 그러나 원지도 기호 폭·지도 축척·지오리퍼런싱 잔차와 표석 복원 좌표의 출처 때문에 **오차를 미터 단위로 확정하거나 1914년 시작점을 그 위치로 강제로 이동하지 않는다.**

1880년 원본에 1912년 Fløjdiget·1923~1925년 Rejsbydiget이나 Råhede 수문이 표시되지 않은 것은 연대상 자연스럽다. **1880년 해안 및 표식 위치만으로 1914년 조석·제방·조간대까지 자동 복원하지 않는다.** 0717과 0718 원도엽의 **세로 접합 자국(측량 격자·도엽 경계)**을 인공 제방이나 자연 해안선으로 해석하지 않는다.

| 자동 검사 | 결과 |
|---|---:|
| 0717 1번 표식~0718 접점 후보선 길이 | **0.29380km** |
| 원본 좌표점 | **15개** |
| 다음 0718 도엽과 연결 | **오차 0** |
| 최대 확대 flatZoom64 / 2560 CSS px에서 후보선 전체 길이 | **2.03254 CSS px** |
| 동일 후보선을 2개 끝점으로 단순화한 최대 화면 차이 | **0.051716 CSS px** |
| 1880년 역사 국경 원선과의 독립 정량 오차 | **미측정** |
| 1914년 물리적 해안선·해상 국경 벡터화 | **미실시** |

2.03 CSS px라는 작고 짧은 국경선 자체를 표석마다 정밀 추적하는 작업은 불필요하다. **그러나 해당 위치에서 국가 육지와 해안선·조간대가 무엇인지는 역사적 귀속 문제이므로 화면상 0.5px 규칙으로 무시하지 않는다.**

- 결과 원본: `german-denmark-1914-north-schleswig-001-026.0717-hvidding-ufer-stage5.json`.
- 비파괴 후보선: `german-denmark-1914-north-schleswig-001-026.0717-hvidding-ufer-stage5.geojson`.
- 재현 코드: `tools/verify_north_schleswig_1914_0717_coast.py`.
- 원본·중첩 이미지: GitHub Actions 7일 아티팩트 `north-schleswig-0717-hvidding-ufer-stage5`. **스캔 이미지는 Git에 영구 저장하지 않음.**

## 5. 완료·보류 구분과 6차 인계

**자료·육안 조사 완료:** 1865년 육상/해상 국경 분리 규정, 역사 표석 1번 위치, 1880년 지도 중첩, 1912/13 국경 측면 제방, 1915년 전체 제방 인계, 1923~25년 남쪽 Rejsby 제방과 Råhede 수문, 2014년 표석 복원 기록, 0717/0718 접합.

**수정 보류:** 1914년의 실제 해안선·조간대 경계, 1880년 지도와 2014년 복원 표석의 차이에 대한 지오리퍼런싱 검증, 국토 귀속과 해상 국경 접속점, 역사적 폴리곤 해안선 확정.

**작업 판정: overlay-reviewed / provisional.** 기존 국경 마스터는 변경하지 않았다.

6차 전체 통합 검증에서 잔여 과제로 (1) 1번·1914년 해안 접점과 해상 국경 분리, (2) 26번 잠정 분리와 이후 하천 연결, (3) Hårup 일부 큰 물굽이 차이, (4) Flads Å 합류점·오른쪽 강변, (5) 1914년 역사적 변경일과 공유 국경 토폴로지를 확인한다. 새 역사자료 없는 곳을 임의로 수 m 재측량하지 않는다.
