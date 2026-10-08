# 데이터 버전 관리 5단계 — 웹·앱 역사 라이브러리 출처·의미 교차검증

## 범위 및 불변 조건

- 웹: kimjeon-il/Pando, 작업 브랜치 work/gis.
- 앱: kimjeon-il/PandoEditor, 작업 브랜치 work/gis.
- **앱 내장 역사 라이브러리의 바이트는 수정하지 않는다.** 285개 고정 파일과 네이티브 고정 index SHA를 보존한다.
- 웹 또는 앱의 main으로 제품 코드를 병합하지 않으며 기존 파일 삭제·재압축·배포를 수행하지 않는다.
- 비교 기준은 움직이는 브랜치가 아니라 명시적으로 고정한 **40자리 웹 커밋** 두 개다.

## 1. 웹·앱 정적 차이 및 출처

| 항목 | 앱 vs 앱 원본 웹 | 앱 vs 5단계 조사 웹 |
| --- | ---: | ---: |
| 비교 파일 | 285 | 285 |
| Git Blob 동일 | 285 | 194 |
| Git Blob 상이 | 0 | 91 |
| 색인 변경 | 없음 | index.json 1개 |
| 국가별 gzip 변경 | 없음 | 90개 |
| 계보 항목 | 동일 262 | 동일 262 |
| 스냅샷 | 동일 2 | 동일 2 |

- **앱 원본 웹:** ebcfae4d27b29cbbea6416a7045a4806930204be.
- **5단계 조사 웹:** a47c4c54655a802a64679a679fc7fa8a576bee56.
- 앱 색인 SHA-256: 63c072095fd6c95034365f6d7f9d4e4ff99f71684890bffc8e678fe741b4334c.
- 변경된 90개 index 엔트리는 lifetime.validFrom/validFrom 및 대응하는 압축·압축 해제 크기와 SHA-256만 달랐다. 각 압축 해제 크기는 정확히 **8바이트 증가**했다.
- 대표 예: 앙골라의 시작일 null → 1975-11-11, 알바니아 null → 1912-11-28.
- 이 수치는 Git 객체·색인 기반 정적 비교다. 실제 도형의 JSON 의미·압축 해제 차이가 없는지 CI의 별도 검증으로 확인한다.

## 2. 구현·검증 절차

- 도구: **tools/compare-territorial-library.mjs**. 양쪽 index의 ID/lineage/snapshot 및 모든 284개 gzip 자산의 저장 바이트 SHA·크기, 압축 해제 크기, JSON, index의 도형 메타데이터를 검사한다.
- 분류: byte-identical / gzip-only / json-format-only / lifetime-start-only / geometry-or-version-drift / other-semantic-drift.
- **--gate exact**는 앱의 고정 원본 웹과 파일·색인이 285/285 완전히 동일해야 통과한다.
- **--gate lifetime-only**는 존속 시작일 변경 또는 내용이 동등한 gzip/JSON 직렬화 차이만 허용한다. 형상이나 도형 버전·다른 의미 속성이 달라지면 차단한다.
- **--output**에 국가 ID별 양쪽 SHA, 유효기간 및 분류를 JSON으로 기록한다. **입력 디렉터리에는 쓰지 않는다.**
- 테스트: tools/territorial-library-crosscheck.test.mjs. SHA·크기 손상/실제 geometry drift/메타데이터 drift/기간 차이/압축 방식 차이를 직접 검사한다.
- Actions: .github/workflows/world-dataset-stage5-gate.yml. Node 22에서 웹 원본 커밋과 최신 조사 커밋을 각각 별도 체크아웃하여 실데이터의 exact 및 lifetime-only 검사 결과를 산출물로 남긴다.

## 3. 미변경 범위

- 앱 기본 국가 지도의 교체는 4단계에서 승인 전 보류되었으며 5단계에서도 실행하지 않는다.
- 앱 내장 역사 라이브러리의 고정 출처·네이티브 인덱스 SHA 변경은 이 단계에서 승인받지 않았으므로 수행하지 않는다.
- 수계/지형 전체 타일, Qt UI 화면·GPU 렌더링, Windows/Android 배포, 사용자 프로젝트 저장/Undo/Redo 전체 회귀는 검증 대상 밖이다.
- 6단계의 구형 자산 정리는 현재 참조·테스트/fixture·배포 재현 가능성을 확인한 뒤 별도로 수행해야 한다.
