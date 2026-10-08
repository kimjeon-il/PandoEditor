# 데이터 버전 관리 구조 개선 — 진행 기록

- 기준일: 2026-10-08
- 범위: 웹 `kimjeon-il/Pando`, 앱 `kimjeon-il/PandoEditor`.
- 주 작업 브랜치: 양쪽 `work/gis`. 원본 GIS 데이터·애플리케이션 소스·빌드 설정은 **1단계에서 미변경**.
- **1단계 (현행 자산 및 참조 관계 조사): 조사·기준선 문서화 완료.**
- 2~6단계: 미착수. 웹 로더/데이터 생성기 변경, 앱 동기화 구현, 구버전 파일 삭제, 배포는 진행하지 않음.

## 재현 가능한 조사 기준

- 웹 스냅샷: [`2b79bcbe48c2`](https://github.com/kimjeon-il/Pando/tree/2b79bcbe48c2b725624a576c7746607771875a93).
- 앱 스냅샷: [`5b3e42a9696a`](https://github.com/kimjeon-il/PandoEditor/tree/5b3e42a9696abb34cadb3f681e47fa973456593c).
- 웹 세부 보고서: [work/gis의 1단계 조사 보고서](https://github.com/kimjeon-il/Pando/blob/work/gis/docs/validation/data-versioning-phase1-inventory.md).
- 앱 세부 보고서: [work/gis의 1단계 조사 보고서](https://github.com/kimjeon-il/PandoEditor/blob/work/gis/docs/validation/data-versioning-phase1-inventory.md).

## 검증된 기준선 및 보류 사항

- 웹 활성 미리보기 매니페스트 파일 5건의 경로·저장 크기 일치.
- 앱 기본 지도 매니페스트 파일 6건의 경로·Git Blob ID 일치.
- 수계 `v0.13.1`의 직접·상위버전 참조 6건 존재·저장 크기 일치: **총 17/17 메타데이터 검사 통과**.
- 웹과 앱의 BJN/SER 국명 기준점 및 수계 매니페스트 Git Blob이 서로 일치함.
- 역사 라이브러리 파일명 285개를 상호 대조한 결과 194개가 동일 Blob이고 91개가 다름. 바이트 차이가 원본 내용 차이인지 압축 차이인지는 미검증.
- 위 결과는 Git tree/manifest 기반 정적 조사 결과이며, 로컬 전체 테스트·브라우저·Qt 빌드·성능 측정을 수행한 결과가 아님.
- 삭제 가능 판정은 **아직 하지 않음**. 특히 수계 v0.13.0은 v0.13.1의 실제 의존 데이터다.

## 다음 작업 (2단계)

1. 웹 기본 세계지도 묶음의 독립적인 데이터 identity·manifest 도입 설계 및 구현.
2. 국가 source → 정밀 패킷 / 미리보기 / 메시 / 공유 경계의 생성 의존성 보존.
3. 앱은 웹의 확정된 데이터 계약에 맞춰 후속 단계에서 반영; 앱의 검증된 고정 번들·오프라인 실행은 유지.
4. 브랜치별 진행 기록은 여기에 반영하되, 구현 코드 및 불필요한 원격 브랜치 전체 병합을 다른 브랜치에 자동 전파하지 않는다.

## 브랜치 반영 규칙

이 문서만 양 저장소의 **현재 존재하는 모든 브랜치**에 독립적인 문서 커밋으로 갱신한다. 상세 기술 보고서는 각 저장소의 `work/gis`에 남긴다. `main`에 구현 코드 병합, 상설 워크트리 삭제, 브랜치 강제 이동은 수행하지 않는다.
