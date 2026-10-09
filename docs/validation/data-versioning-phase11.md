# 데이터 버전 관리 11단계 — 구버전 URL 보존 의무 해제 및 지형 v0.12.0 실제 삭제

- 정책 적용: 2026-10-09.
- 범위: 웹 `kimjeon-il/Pando` 전체 7개 브랜치; 앱 `kimjeon-il/PandoEditor` 5개 브랜치에 동일 정책 문서 게시.
- **사용자 승인:** 구버전 URL의 영구 접근을 더 이상 보장하지 않으며, 우선순위 1번 지형 `assets/data/terrain/v0.12.0/` 삭제 진행.
- 새 정책: [공통 데이터 URL 정책](../data-url-retention-policy.md). 웹·앱 12개 브랜치에서 동일 Git Blob `d0549afe76a41d27a69a6fb66d18be89c11cbc93`.
- **실제 소스 삭제 완료**: 지형 v0.12.0 **335개 파일, 374,094,147 bytes**를 웹 7개 브랜치 각각에서 제거. 최신 지형 v0.12.6은 모든 브랜치에서 335개 파일, 367,620,868 bytes 그대로 보존.
- 앱의 `assets/world/**`는 전혀 삭제하지 않음. 내장 데이터 매니페스트 v1/v2 모두 지형 v0.12.6 사용 확인.

## 1. 웹 브랜치별 삭제·복원 커밋

| 브랜치 | 삭제 전 HEAD (롤백 기준) | 구버전 삭제 커밋 |
| --- | --- | --- |
| `main` | `9439d336aa398e93f0ce6bcf0402a73114561ece` | `2f8b8b17c9ded57b44f84e0015cc937a8a52ee9f` |
| `tmp/fetch-north-schleswig-west` | `ef8e792d78566630b6d780209143aa8dcfb61315` | `d52ed801ae5a5cf2643f0f7238b75fefd1d679d4` |
| `work/gis` | `a643fb436feac01c7e91efe6854fc120494814d4` | `cec57efa5f1594ee330ceeab53442dbe75f1d5a7` |
| `work/hydro-names` | `d678a476ba2d3e09c02fcc1030d21205b93d7782` | `c299dd26ff6b7ac5e2c22c7860f75c2542f1de6f` |
| `work/objects` | `91701f9924c564f4e14d0bd1665ee9bd8c211af4` | `9219c67190533f4344fdf791a22b47465920f3f2` |
| `work/places` | `d4b312d46b80ab9d6ffdbb41da14b0b681da3781` | `4265ac27ec8f5489f71eac02615d8e06d13233af` |
| `work/ui` | `1b85bc0327bb7a7f001faa21cc50b06dbc29c774` | `ea3e4f5bcfd90f05ca8e9f4ab9201ee9acf8baea` |

각 변경은 해당 브랜치 HEAD를 부모로 추가하고 GitHub 갱신 시 예상 SHA를 검사했다. 기존 Git 객체의 전체 이력을 삭제하거나 강제로 브랜치를 이동하지 않았다. 파일별 정확한 원본 Git Blob·bytes 및 일괄 삭제 전 7개 HEAD는 [335개 원본 복원 목록(JSON)](terrain-v0120-retirement-evidence.json)에 기록.

## 2. 배포 및 작동 버전 검증

- 웹 `main`의 `assets/data`: **734,341,743 bytes**, `main` 전체 추적 파일 합계 **832,753,069 bytes**. 웹 `work/gis` `assets/data`: **780,027,279 bytes**.
- GitHub Pages는 기존 **main 루트 branch-source** 방식을 유지했다. [Pages 빌드·배포 #37868609497](https://github.com/kimjeon-il/Pando/actions/runs/37868609497) **success**.
- [실서비스/전체 브랜치 검증 #37869177623](https://github.com/kimjeon-il/Pando/actions/runs/37869177623) **success**, 단위검사 5/5:
  - 웹 7개 브랜치의 `v0.12.0` 파일 **0개**, 현재 `v0.12.6` 파일 **335개씩**.
  - 앱 5개 브랜치 모두 native terrain `v0.12.6` 및 동일 정책 문서 검증.
  - 기존 `https://kimjeon-il.github.io/Pando/assets/data/terrain/v0.12.0/manifest.json`: **HTTP 404** (의도한 접근 종료).
  - 현행 `https://kimjeon-il.github.io/Pando/assets/data/terrain/v0.12.6/manifest.json`: **HTTP 200**.
- [7단계 실데이터 감사 #37868849732](https://github.com/kimjeon-il/Pando/actions/runs/37868849732), [8단계 #37868849805](https://github.com/kimjeon-il/Pando/actions/runs/37868849805), [9단계 #37868849760](https://github.com/kimjeon-il/Pando/actions/runs/37868849760), [10단계 #37868907775](https://github.com/kimjeon-il/Pando/actions/runs/37868907775) 최종 성공. 9단계 Node 검사 35/35, 10단계 49/49.
- 배포 보수적 파일 프로필 재검사: 웹 `main` **673,492,217 bytes**, `work/gis` **695,918,028 bytes**로 모두 기존 1GB 제한 이내. 기존 26그룹/2,124개 보관 후보에서 현재 **25그룹/1,789개/190,094,853 bytes**만 남음.

## 3. 정책 변경에 따라 수정한 검증기

- 과거 실데이터 고정수 `2,124/564,189,000`에 의존하던 도구를 **현재 남아 있는 보관 후보의 Blob·바이트 수와 비교하는 동적 검사**로 변경:
  - `tools/plan-pages-deployment-footprint.mjs`
  - `tools/plan-gis-release-transactions.mjs`
  - `tools/plan-gis-archive-rollout.mjs`
  - `tools/audit-legacy-release-consumers.mjs`
  - 해당 단위검사 및 비활성 Pages 승인 템플릿.
- 공개된 구버전 URL이 존재한다는 사실만으로 **무조건 보존해야 한다고 판정하는 기존 7단계 구 정책을 해제**. 단, 기능·앱 내장 데이터에서 실제 사용하는 버전/경로와 승인되지 않은 다른 역사 데이터는 자동 삭제하지 않는다.
- 새로운 `tools/check-retired-terrain.mjs`, `tests/unit/retired-terrain-audit.test.mjs`, `.github/workflows/retired-terrain-v0120-gate.yml`은 구 지형이 다시 유입되는 경우와 현재 지형/앱 참조 손상을 자동 감지한다.

## 4. 명확한 범위 제한

- **이번에 실제 삭제된 구버전 그룹은 지형 v0.12.0 하나뿐이다.** 수계 구버전이나 다른 국가 파일은 추가 정리 승인 없이 자동 삭제하지 않았다.
- 실서비스의 과거 `v0.12.0` URL 접근 종료는 새 정책상 허용되는 결과이다. 향후 다른 구버전 URL도 버전별 검수·승인 후 접근이 중단될 수 있다.
- GitHub Pages **배포 방식(브랜치→Actions) 변경은 하지 않았다**. 새 정책 덕분에 1GB를 넘던 용량 문제가 해결됐지만, 그것이 앱 Qt 미해결 회귀·`main` 선별 제품 코드 병합을 승인했다는 뜻은 아니다.
- 웹 `main`의 전체 애플리케이션 CI에는 지형 파일 삭제와 직접 연관되지 않은 UI·편집/네이티브 오류가 별도로 보고돼 있다. 새 지형 삭제 전용 CI와 기존 데이터 관리 7~10단계의 타깃 검사 성공을 전체 앱 CI 무결성과 혼동하지 않는다.
- **기존 Git 이력의 압축 저장 용량은 별도다.** 현재 HEAD의 추적 파일 합계 374MB 감소는 GitHub 저장소 객체 DB의 즉각적인 동일 크기 감소를 의미하지 않는다.

**결론: URL 정책 변경 + 웹 7개 브랜치의 실제 335개/374MB 삭제 + 기존 Pages 배포 성공 + 7웹/5앱 브랜치 및 공개 current/retired URL 검증 완료.**
