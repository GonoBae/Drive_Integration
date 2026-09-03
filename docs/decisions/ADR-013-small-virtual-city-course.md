# ADR-013: R1 배경을 작은 가상 도심 코스로 전환

## 상태

- 결정: 채택
- 결정일: 2026-08-31
- 사용자 승인: “작은 가상 도심 코스로 바꾸길 희망합니다”
- 대체 범위: Wall Street·Broad Street 실지리 재현, 실제 지도 기반 importer,
  NYSE/Federal Hall과 R1 Cesium 의존. 기존 아키텍처의 ADR-001/ADR-008 요약 중
  해당 지역/배경 전제만 대체하며 과거 이력은 보존한다.

## 배경

사용자는 Unreal 클라이언트 포트폴리오 완성을 우선하며, 실제 지역 재현보다 작은
가상 도심 주행 코스를 원한다. 물리·통신·표현의 검증 기반은 이미 존재하지만 완성된
도시 환경과 LaneGraph·traffic·record/replay는 아직 없다. 기존 대상을 구현한 것으로
간주하지 않고, 승인된 새 지역 범위를 기능표·일정·아키텍처에 함께 반영한다.

## 결정

R1은 한 블록 규모의 가상 도심에서 수동운전을 시연하는 로컬 코스다. 첫 제작안은
약 240×200m 안의 주행 루프, 양방향 도로, 신호 교차로 1곳, 보도·횡단보도,
짧은 경사와 정차 공간이다. 치수·배치·스타일은 구현 중 조정할 수 있는 설계 초안이다.

| R1에서 변경/제외 | 유지할 기능·완료 기준 |
|---|---|
| 실제 Wall/Broad 지도·통행 규칙 재현 | authored 도로 중심선에서 생성한 방향성 LaneGraph·통행/정지선 연결 |
| NYSE/Federal Hall 등 실존 랜드마크 | 주행을 보여줄 수 있는 작은 모듈형 도심 외관 |
| Cesium/WGS84 지리 정렬·streaming | 로컬 ENU 원점 metadata·FLU↔Unreal 좌표와 자세 계약 |
| GIS 원본 확보·지리 importer | 자체 source의 버전/출처·checksum, 외부 에셋의 라이선스 기록 |
| 실제 지역 맞춤 환경 크기 | 작더라도 주행 루프·벽/커브·신호·보행 동선 검증 |

다음은 범위 변경으로 삭제하지 않는다.

- 외부 물리 SDK 없는 C++ 차량 물리 권한과 Unreal 지면/marker 측정 경계.
- 직접 WS binary/Protobuf, Hello/checksum/play/sequence fence와 Health/SafeStop HUD.
- 신호 1곳, NPC 차량 3~4대, 보행자 6~8명의 규칙 기반 반복 동작.
- SensorRig 골격, 주행 기록·결정적 재생.
- 해당 지역 조건을 새 가상 코스로 치환한 AT-01~10, 목표 PC 1080p 성능,
  30분 안정성, 패키지·영상·문서.

## 데이터와 실행 경로

승인 시점에는 제작 예정이었으며, 후속 구현으로 2026-08-31 새 맵
`/Game/VirtualCity/Maps/L_VirtualCity`와 `map_packages/virtual_city_v1`을 실제 생성·Bake했다.
기존 `NewMap`, `landscape_local_v1`, `wall_broad_v1` bootstrap과 코드/테스트는 보존했다.
map-local `VirtualCityGameMode`와 전용 launcher를 사용한다. 정확한 실행 절차는
[빠른 시작](../virtual_city_quickstart.md)을 따른다.

기존 WGS84 수학과 설정값은 회귀/호환용이다. 가상 도시에 실재하는 위경도를 부여한
것으로 해석하지 않는다. plugin 설치/제거와 외부 에셋 구매·배포는 이 승인에 포함하지
않는다. 자체 기본 도형으로 먼저 코스를 구성하고 외형 에셋은 별도 검토한다.

## 검증과 일정 영향

첫 blockout은 도형 594개, 전용 ground component 59개, static OBB marker 228개와
저장형 자체 색상 재질 10개다. 약 633.1m 시험 루프의 `drive_route.csv`는 364개
QA checkpoint만 담으며 LaneGraph나 traffic 구현을 대체하지 않는다. 현재 외형은
Engine Cube 기반 prototype art다.

실제 Bake는 `SIMGHF2`, `201×241` lattice/48,441 samples/48,000 cells이고
48,039 valid samples/47,600 drivable cells다. checksum은 `fnv1a64:17293781b645eac0`.
명목 240×200m와 달리 cube 끝면 ray hit 누락으로 valid 지면은 E `[-119,119]`,
N `[-20,180]`m의 **238×200m**이며 동·서 각 1m를 valid 범위로 계산하지 않는다.

> 위 수치는 2026-08-31 **첫 blockout의 1m Bake 이력**이다. 2026-09-01 보도·연석
> support 이행, 보도/연석 top 정합과 50cm 재Bake 뒤 현재 package는 checksum
> `fnv1a64:942842ea8d76b7a2`의
> 401×481 snapshot이다. 현재 수치는 [package README](../../map_packages/virtual_city_v1/README.md),
> 물리/저작 경계는 [ADR-012](ADR-012-unreal-ground-measurement-boundary.md)를 따른다.

저장 맵 재로드·geometry/marker 정렬·asphalt/높이 검증, C++ dense route와 입력 기반
실제 물리 완주의 선행 시험, UE Automation 11/11, Editor/Game 빌드는 통과했다.
최종 QA 재실행 결과는 [작업일지](../worklogs/2026-08-31.md)에 합산한다.

단일 표면 heightfield로 표현할 수 있는 도로부터 제작하고 다층 도로·터널은 첫 코스에서
제외한다. 실제 scene 측정→v2 package→C++ loader→주행/충돌→재Bake를 확인해야 한다.
회색 박스가 존재하거나 자동 테스트가 통과했다는 이유만으로 외형 품질·PIE·패키지
인수를 완료 처리하지 않는다.

실제 지도/랜드마크 의존은 줄지만 차량, 교통, 기록, QA 시간이 없어지는 것은 아니다.
잔여 공수는 [일정표](../01_schedule.md)의 가정별 추정으로 관리한다. Core 9/7·버퍼
9/8과 Should 9/11·버퍼 9/14를 관리 목표로 유지하고 9/4는 조건부 선행 인수 후보다.

상세 제작 순서는 [배경 제작 계획](../04_environment_plan.md), 기능 ID별 변경은
[기능표](../02_feature_matrix.md)를 따른다.
