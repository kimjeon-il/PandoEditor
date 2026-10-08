# 데이터 버전 관리 9단계 — GitHub Pages 게시 산출물 1GB 타당성 실검증

- 작성일: 2026-10-09 (Asia/Seoul). 검증 대상: `kimjeon-il/Pando`의 `main` 및 `work/gis`; 앱 `kimjeon-il/PandoEditor`의 고정 출처.
- **9단계 GitHub Actions [#37828988316](https://github.com/kimjeon-il/Pando/actions/runs/37828988316) 성공.** Node 전체 단위검사 **35/35** 및 정적 Git Blob 전체 비교 완료.
- **배포·삭제 결론:** 기존 구 URL 2,124개와 현행 기능을 보존하는 산출물은 Github Pages의 게시 사이트 1GB 제한을 넘으므로 **실제 배포 전환/구버전 파일 삭제를 수행하지 않음**.

## 1. 정확한 실측 결과

| 항목 | 파일 추적 크기 |
| --- | ---: |
| 현행 `main` 전체 Git 추적 파일 | 1,206,838,217 bytes |
| 현행 `main`의 `assets/data` 전체 | 1,108,435,890 bytes |
| `main`에서 개발·생성·연구 전용으로 판단한 항목을 제외한 보수적 게시 구성 | **1,047,586,364 bytes** |
| `work/gis`의 SHA 기반 번들 및 기존 URL을 모두 수용하는 게시 구성 | **1,070,012,175 bytes** |
| 보수적 게시 제한 기준 | **1,000,000,000 bytes** |
| `main` 게시 구성의 기준 초과 | **47,586,364 bytes** |
| `work/gis` 전환 구성의 기준 초과 | **70,012,175 bytes** |

GitHub Pages 공식 사용량 제한은 **게시 사이트 최대 1GB**. 안전 게이트에서는 1GB를 보수적으로 **십진 1,000,000,000 bytes**로 정의해 판단한다. 전송 압축 크기나 tarball이 10GB 미만이라는 조건은 게시 사이트의 1GB 조건을 없애지 않는다.

- GitHub 공식 문서: https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits
- GitHub 배포 artifact 설명: https://github.com/actions/upload-pages-artifact

### `main` 보존 구성 세부 내역

| 범위 | 바이트 |
| --- | ---: |
| 기존 공개 구버전 26그룹, URL 2,124개 | 564,189,000 |
| 현행 지형 `v0.12.6` | 367,620,868 |
| 현행 수계 `v0.13.0`과 `v0.13.1` | 12,828,391 |
| 역사 라이브러리 생성본과 원본 생성 지도를 포함한 `generated/` | 19,324,232 |
| 웹 애플리케이션 JS/CSS/글꼴/벤더 자산 | 52,308,550 |
| `main` 기본 HTML 및 `.nojekyll` | 101,101 |
| 현행 세계지도·앱 고정 입력·호환 공유 경계 | 31,214,114 |
| 지명 매니페스트 등 | 108 |
| **합계** | **1,047,586,364** |

`main` Git 추적 전체에서 산출물 밖으로 분류한 항목은 159,251,853 bytes이지만, GitHub Pages에서 과거에 접근할 수 있던 *모든* 빌드 원본 URL의 계속된 접근까지 보증하는 것은 아니다. 현재 원본 파일 바이트는 Git 저장소에 남겨둔다.

## 2. 기술적으로 중요한 제약과 대안의 위험

- GDAL GIS 가져오기를 실제 구현한 `assets/js/gis-io.js`는 `assets/js/vendor/gdal/`의 WebAssembly와 `.data`를 사용한다. 두 대용량 파일은 **39,814,980 bytes**. 이를 단순 제외하면 GIS 가져오기 기능이 작동하지 않는다.
- 이 두 GDAL 파일만 배포에서 제외해도 `main` 산출물은 **1,007,771,384 bytes**로 여전히 제한을 초과한다.
- 추가로 `territorial-entities/generated/current-world.geojson`(12,415,783 bytes)까지 제외하면 **995,355,601 bytes**로 수치상 1GB를 밑돌지만, **GIS 가져오기와 기존 원본 참조 URL이 깨지는 비승인 가상안**이다. 안전한 대안이 아니다.
- 구버전 2,124개의 원래 경로를 유지하면서 바이너리를 다른 도메인에 단순 이전하는 것은 기존 `github.io/Pando/assets/data/...` URL 유지와 다르다. GitHub Pages만으로 투명한 바이너리 리버스 프록시를 제공하는 방안도 확인되지 않았다.
- 현재 현행 앱 기능/기존 공개 URL 보존/Pages 1GB 제한의 세 조건을 동시에 충족하는 **검증된** 산출물은 아직 없다. 용량 절감형 자료 포맷·외부 호스팅·기존 URL 정책 등을 비교하되, 외부 공개 URL과 동작의 후방 호환을 보증하기 전에는 전환하지 않는다.

## 3. 정확한 빌드/배포 범위 검사기 구현

- 웹 `work/gis` 신규 `tools/plan-pages-deployment-footprint.mjs`: Actions 배포 **전에** 고정 Git 트리 2개와 8단계 고정 파일 목록에서 공개 산출물 경로·크기·원본 Git Blob을 모두 계산하는 읽기 전용 도구.
- `main`과 `work/gis` 2개 프로필을 구분, 웹 실행물(특히 GDAL 2개 파일), 현재 수계/지형, 역사 라이브러리, 고정 세계지도 입력, 앱 고정 출처 및 구버전 파일 **2,124개 전부**의 존재 및 Git Blob을 검사.
- 구버전 파일은 경로뿐 아니라 각 Git Blob SHA·파일 바이트와 8단계 삭제 잠금 목록이 두 브랜치에서 **완전히 동일해야만 통과**한다. 현행 보호 필수 경로가 없으면 검사 실패.
- 개발 원본/연구 자료/보고서/테스트/스크립트를 배포 산출물에서만 제외하는 시뮬레이션을 수행하며, **Git 브랜치에서 삭제하지 않는다**.
- 기능 손실을 일으키는 가상 절감 방안은 용량 절감 효과만 수치화하고 `approvedForDeployment=false`로 분류한다. 공식 1GB를 넘으면 전체 배포 상태는 무조건 `eligibleForPagesCutover=false`.
- `tests/unit/pages-deployment-footprint.test.mjs`: 구 URL SHA, 원본/앱 출처, GDAL 기능 필수 자산, 다중 브랜치 원본 바이트, 불완전 Git 트리, Pages 용량 및 삭제/배포 방지 테스트.
- `.github/workflows/gis-pages-stage9-publication-gate.yml`: `contents: read` 권한만으로 실행. GitHub Pages 쓰기 권한, 실제 Pages artifact 업로드, 배포·브랜치 삭제 동작이 없다. 무거운 WebP/수계 바이너리는 내려받지 않고 Git 트리 메타데이터만 읽는다.
- [최종 CI #37828988316](https://github.com/kimjeon-il/Pando/actions/runs/37828988316) **35/35 테스트** 통과. [파일별 게시 경로/Blob 및 용량 검사 JSON](https://github.com/kimjeon-il/Pando/actions/runs/37828988316/artifacts/11572936027) 보존.

## 4. 명시적으로 남은 과제

1. 현재 GIS 가져오기·정밀 지도·역사 국가·수계·지형과 2,124개 구 URL을 모두 유지하면서 용량을 줄일 추가 수단을 확인한다. 최소 **70,012,175 bytes**(GIS 전환 프로필 기준)를 기능·URL 손실 없이 절감해야 한다. 여유 공간도 필요하다.
2. 이 요구사항이 Pages 단독 호스팅과 충돌하면 사용자에게 **기존 URL을 보존할 다른 배포 구조/호환 정책**에 대한 별도 선택을 제시한다. 임의로 서비스 도메인을 바꾸거나 GIS 가져오기 기능을 제거하지 않는다.
3. 1GB 이하의 실제 배포 산출물이 확정된 이후에만 **실제 바이너리 크기·SHA-256, 전체 2,124 URL의 전후 검증, 웹 브라우저 회귀와 앱 릴리스 계약**을 진행한다.
4. Pages Actions 전환은 별도 승인과 복원 경로를 갖춰 진행하고, 그 뒤 전체 브랜치에 대한 고정 SHA 일괄 정리 릴리스를 적용한다. 진정한 다중 Git ref 원자성은 존재하지 않으므로 개별 롤백 SHA 기록이 필요하다.

## 5. 변경하지 않은 것

- 기존 공개 Pages 설정 및 `main` 제품 실행 코드.
- 웹·앱에 포함된 GIS/국가/역사/지명/수계/지형 원본 파일 바이트.
- 앱의 고정 데이터 출처 및 기존 Windows 미리보기 릴리스.
- 기능별 `work/*` 코드의 병합 또는 상설 워크트리의 삭제.

**9단계 결론: 게시 파일 단위 판정 및 차단기 구현·검증 완료. 1GB 제약 충돌을 실제 파일 바이트로 입증했으므로, 배포·삭제는 계속 중단한다.**
