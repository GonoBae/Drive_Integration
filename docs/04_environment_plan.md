# 배경 제작 계획 — 작은 가상 도심 코스

## 결정 상태

**2026-08-31 사용자 승인: 기존 Wall Street·Broad Street 재현을 작은 가상 도심 코스로
변경한다.** 실제 지리 정렬·지도 취득·NYSE/Federal Hall 재현과 Cesium 연동을 R1 완료
조건에서 제외한다. 자체 차량 물리, 충돌, 차선·신호·NPC·보행자, 센서 골격,
기록/재생과 성능·안정성 기준은 유지한다.

이번 승인은 배경 방향 변경이다. 아래 정확한 치수·도로 배치·외형 스타일은 구현을
시작하기 위한 **수정 가능한 설계값**이며 사용자가 각각 확정한 값이 아니다.
방향 승인 후 같은 날 별도 blockout 맵을 실제 생성·Bake했으며, 에셋 구매는 하지 않았다.
[ADR-013](./decisions/ADR-013-small-virtual-city-course.md)에 범위 변경을 기록한다.

현재 저장소에는 기존 `NewMap` Landscape·SimBlank와 새 `L_VirtualCity`가 함께 있다.
새 코스는 Engine Cube와 자체 색상 재질로 만든 **주행 검증용 prototype art**이며,
완성된 도시 외형이 아니다. `NewMap`의 기존 미커밋 사용자 변경과 Landscape package는
보존했고 배경 제작용 맵으로 덮어쓰지 않았다.

## 실제 구현 상태 — 8/31 생성, 9/1 연석 support 보완

- `/Game/VirtualCity/Maps/L_VirtualCity`와 저장형 색상 재질 10개를 생성했다.
  도형 594개 중 기본 지면·도로 59개, 보도 116개, 연석 top 215개, 총 390개를
  전용 `VirtualCityGround`에 모았다. 시각 장애물과 정렬한 static OBB marker 228개는
  유지하며 연석은 Ground support와 `Curb` marker를 함께 가진다.
  50cm production swept near(+1R)/far(+3R) support band에서 도로/보도 연석 marker 중심
  full-footprint 214곳이 등판 후보이고, 뒤에 Barrier가 있는 `Curb_BayEnd` 중심 1곳은
  의도적으로 제외된다. 전체 along-length QA는 210/215 collider 전 길이와 T-opening 네
  끝단·BayEnd의 의도적 gap을 확인했다.
- 폭 8m의 양방향 도로, 좌측으로 순환하는 약 **633.1m** 시험 루프, T자 갈림길,
  보도·횡단보도·정지선·정차 공간, 약 4° 오르내리막과 높이 약 1.40m 평탄부를 구성했다.
- 같은 도로에 정렬한 **저작 방향성 lane 25개**를 별도 `traffic_network.json`으로 만들었다.
  양방향 루프·북측 T분기·정지선 connector·명시적 terminal을 포함하며, QA 주행 경로를
  제품 graph로 재명명한 것이 아니다. 정차 공간의 outbound lane은 terminal이고 강제
  U턴 연결은 없다. NPC/보행자의 intent·경로 추종은 아직 미구현이다.
- 교차로 1곳에 **차량 신호 head 3개·phase group 2개**를 구현했다. C++ 서버 simulation
  clock의 30초 위상과 남은 시간을 WorldState로 전달하며 Unreal은 서버 상태만 표시한다.
  신호 head는 런타임 `NoCollision` 표시물이며 기존 static collision/package는 변경하지
  않았다. 기존 북측 분기 도색 정지선과 달리 본선 2곳의 정지선은 lane 끝점으로만 정의됐고,
  새 본선 정지선 시각 표시는 아직 추가하지 않았다. [차선·신호 계약](./traffic_network_signals.md),
  [ADR-014](./decisions/ADR-014-server-clock-traffic-signals.md)를 따른다.
- 실제 Unreal collision 측정으로 `virtual_city_v1`의 `SIMGHF2`를 만들었다.
  50cm 간격의 `401×481` lattice, 총 192,881 samples 중 192,079 valid samples와
  191,200 valid·drivable cells, rough 169,690/asphalt 21,510 cells, checksum
  `fnv1a64:942842ea8d76b7a2`다.
- 명목 크기는 동서 240m×남북 200m지만 cube 끝면의 ray hit 누락 때문에 **실제 valid
  지면은 동서 239m×남북 200m**다. E `[-119.5,119.5]`, N `[-20,180]`m이고 동·서 끝
  각 0.5m는 valid 지면으로 보장하지 않는다. 코스와 외곽 벽 안쪽은 이 범위 안에 있다.
- 저장 맵 재로드 검증은 도형 transform, marker ID·pose·extent·semantic·활성 상태와
  364개 route checkpoint의 asphalt·높이·경사를 확인했다. C++ dense route/네 바퀴와
  실제 입력 기반 물리 한 바퀴의 선행 시험, blockout 생성 당시 UE Automation **11/11**,
  Editor/Game 빌드가 통과했다.
- lane/신호 구현 후 CTest **16/16**, UE Automation **23/23**, Editor 빌드가 통과했다.
  실제 32초/1,921-state 신호 smoke와 새 PIE/all-red·soft/hard timeout·동일 PIE 재연결,
  정상 tick의 RED/YELLOW/GREEN 3색 렌더 QA 및 이미지 확인도 통과했다. 이후 발견한
  heading 360° 경계값 보정도 완료했으며 최종 C++ `ALL_BUILD`, CTest **16/16**,
  UE Game Development·Editor 빌드와 `ValidateOnly`의 **25 lanes·3 heads·0 errors·0 warnings**를 확인했다.
  후속 결과는 [작업일지](./worklogs/2026-08-31.md)에서 관리한다.
- 9/2 보도 support를 연석 상단 24cm와 정렬한 뒤 실제 package 연석 회귀, CTest
  **18/18**, UE Automation **35/35**, Game/Editor Development 빌드와 맵·traffic 두
  `ValidateOnly`, Health·traffic 실제 WebSocket smoke가 통과했다.
  실제 PIE에서 위치별 연석 진입과 벽·Barrier를 확인하는 수동 gate는 남아 있다.
  [9/1 작업일지](./worklogs/2026-09-01.md)를 따른다.
- 실제 PIE 조작감·화면 접지/비관통·재Bake/reconnect와 목표 PC 성능·30분 안정성은
  별도 수동 gate다. headless 성공으로 이 항목들을 완료 처리하지 않는다.

실행·편집·재Bake는 [가상 도심 빠른 시작](./virtual_city_quickstart.md)을 따른다.

## 제작 기준

**주행 가능한 작은 한 구간을 먼저 완성하고, 그 주변을 모듈형 에셋으로 채운다.**
클라이언트 포트폴리오에서는 주행 장면, 입력·카메라·UI, 충돌 일치, 프레임 시간과
문제 진단 도구를 한 장면에서 확인할 수 있도록 구성한다. 도시 전체 크기보다 촬영하는
구간의 일관성과 반복 시험 가능성을 우선한다. 아래 크기는 설계값이지 실제 지리나
도로 설계 표준을 재현한다는 주장이 아니다.

| 요소 | 첫 제작안 | 확인할 기능 |
|---|---|---|
| 전체 영역 | 약 240×200m, 한 블록 규모 | 작은 범위에서 완주·복귀 가능한 동선 |
| 주행 루프 | 양방향 2차로, 직선·좌우 코너 | 가감속·후진·선회·차선 진행 방향 |
| 도로/보도 | 현재 도로 8m, 보도 2.5m | 차체 폭·카메라 여유·marker 충돌 정렬 |
| 교차로 | T자 1곳, 현재 차량 head 3개·phase group 2개 | 정지선·서버 신호·NPC 순환·횡단보도 |
| 경사 구간 | 짧은 3~6° 경사와 평탄한 연결부 | 차체 자세·바퀴 접지·등판/하강 |
| 정차 공간 | spawn과 분리한 정차/후진 구역, 자동 U턴 미연결 | 제동·후진·카메라 시연 |
| 건물·소품 | 낮 시간, 모듈형 건물 외관·가로등·표지판 | 화면 구성·재질 일관성·성능 |

현재 Engine 기본 도형과 자체 색상 재질로 blockout 코스를 만들었다. 차량이 도는
범위와 신호·보행 동선을 확인한 다음 외형을 교체한다. 구매한 도시 팩을 import해도
자체 물리용 도로·충돌·차선이 자동 완성되는 것은 아니다.

### 새 맵과 데이터 분리

- 생성된 맵: `/Game/VirtualCity/Maps/L_VirtualCity`.
- 생성·검증된 package: `map_packages/virtual_city_v1`.
- 원본은 `SimCoreVirtualCityLayout.h/.cpp`의 meter 기반 layout이며 editor-only
  `BuildVirtualCity` commandlet이 별도 scene/material 자산을 저장하고 production exporter로
  측정한다. 기존 대상 맵이 있으면 재생성을 거부한다. `-BakeOnly`는 저장 맵 geometry를
  다시 만들지 않고 재측정하며, `-ValidateOnly`는 저장 결과를 원본과 비교한다.
- map-local `VirtualCityGameMode`와 전용 launcher가 같은 package를 사용한다.
  실행 기본값이나 다른 맵의 설정을 추측하지 말고 빠른 시작 절차를 따른다.
- `drive_route.csv`는 364개 **자동 QA용** 위치/방향 checkpoint이며 LaneGraph,
  NPC AI, 제품 navigation 데이터가 아니다. collision manifest에 포함하지 않는다.
- 제품 lane/신호 원본은 `SimCoreVirtualCityTrafficLayout.h/.cpp`의
  `BuildTrafficLayout()`이다. editor-only `ExportVirtualCityTraffic`이 저장 맵의 실제
  collision과 정렬·높이를 검증해 `traffic_network.json`을 최초 생성하거나
  `-ValidateOnly`로 비교한다. collision checksum 변경 후에는 명시적 `-UpdateExisting`이
  canonical 임시 파일을 거쳐 기존 traffic JSON만 교체한다. `-nowrite`가 필수이며 맵·ground
  manifest를 저장하지 않는다. JSON의 `source_map_checksum`은 실제 collision manifest에
  결속된다. 일반 지면 재Bake가 traffic sidecar까지 자동 재생성하는 것은 아니다.
- `NewMap`과 `landscape_local_v1`은 기존 물리 회귀 시험용으로 보존한다.
- `wall_broad_v1`은 현재 코드/시험이 참조하는 이전 bootstrap fixture로 보존한다.
  그 이름이나 기존 WGS84 설정값이 새 가상 도시의 실제 위치를 뜻하지 않는다.
- 가상 코스는 로컬 ENU 원점과 FLU↔Unreal 변환을 사용한다. 위경도 정렬·Cesium
  georeference는 후속 기능이며 현재 좌표 수학/회귀 코드를 삭제하지 않는다.

## 제작 순서와 완료 기준

### 1. 주행면 blockout

현재 blockout 생성·Bake·자동 주행/저장 검증은 완료했다. 아래는 유지해야 할 제작 기준이며
수동 PIE·재Bake/reconnect 인수까지 모두 완료했다는 뜻은 아니다.

- 배경용 새 맵을 만들고 기존 `NewMap`은 물리 회귀 시험장으로 남긴다.
- 실제 크기의 도로 폭·코너·보도·정지선을 단순 메시로 배치한다. 차량이 한 바퀴
  돌아 spawn으로 복귀할 수 있는 루프를 먼저 확인한다.
- 도로 측정 source와 건물·장식 source를 분리한다. 도로의 blocking component들을
  전용 actor로 모아 `Ground Actor` 대상으로 삼는 구성을 먼저 검증한다. 건물 지붕을
  지면으로 잘못 측정할 수 있는 무차별 WorldStatic Bake는 도시 기본 작업 방식으로
  사용하지 않는다.
- 현재 heightfield는 위에서 내려다본 단일 표면이다. 겹친 고가도로·터널·다층 주행은
  지원이 검증되지 않았으므로 첫 코스에 넣지 않는다.
- heightfield만으로 얇은 연석 측면이나 벽을 표현하지 않는다. 이 package는 swept
  full-footprint의 경사·코너 raster 오차를 피하려고 50cm를 사용한다. 연석 top과 보도는 휠
  support로 Ground에 포함하고, 연석·벽의 차체 장애물 의미는 `SimCore Static Collider`
  OBB를 시각 메시와 맞춰 함께 Bake한다. 이중 역할은 semantic `Curb`에만 허용한다.
- 종료: 지면 v2 Bake, `static_colliders>0`, checksum 일치, 루프 주행·경사·코너·벽
  비관통·PIE 재시작·재Bake hot reload 확인. 시각 품질 작업의 선행 gate다.

### 2. 외형 교체

- 에셋 팩은 한 가지 스타일의 도로/보도와 모듈형 건물부터 선택한다. 서로 다른 팩을
  대량 혼합하기 전에 스케일·재질·충돌·UE 5.6 호환성을 소형 샘플로 확인한다.
- 촬영 근경은 입구·창문·가로등·표지판·연석을 배치하고, 중경은 반복 건물,
  원경은 단순 실루엣으로 제한한다. 건물 내부 제작은 첫 배경 패스에 포함하지 않는다.
- 반복 창문·가로등·펜스는 동일 mesh/material 그룹의 ISM/HISM 적용 후보로 관리한다.
  Epic 문서는 반복 메시의 instancing으로 Actor/UObject와 draw-call 비용을 줄이는
  방법을 설명한다. 적용 효과는 이 프로젝트에서 따로 측정한다.
  [UE 5.6 ISM 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-static-mesh-component-in-unreal-engine?application_version=5.6)
- R1 원경도 로컬 단순 건물/실루엣으로 만든다. Cesium·실제 지도 streaming은 도입하지
  않는다. 이 결정으로 기존 georeference 수학이나 설치된 다른 plugin을 삭제하지 않는다.
- 종료: 같은 루프에서 임시 메시를 외형 에셋으로 교체해도 충돌·접지·카메라가
  달라지지 않으며 사용한 모든 외부 에셋의 출처가 등록돼 있다.

### 3. 조명·카메라·성능

- 첫 외형 패스는 맑은 낮 한 가지로 고정한다. 비·야간·실내·시간대 변화는 이 문서로
  추가 승인한 기능이 아니며 이후 별도 판단한다.
- 현재 설정은 Lumen GI/reflections와 Virtual Shadow Maps가 켜져 있다. 이를 그대로
  성능 보장으로 해석하지 않는다. 낮은 비용의 설정과 비교하고 패키지에서 측정한다.
- Epic의 Lumen High/Epic 예산은 60/30fps 타깃 설명이며 **RTX 2060에서의 보장값이
  아니다**. 이 프로젝트의 판정은 i5-10400/RTX 2060, 1920×1080 패키지에서 한다.
  [UE 5.6 Lumen 성능 가이드](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine?application_version=5.6)
- 동일 루프에서 `stat unit`, `stat gpu`, Unreal Insights를 사용해 game/render/GPU
  시간을 분리 기록한다. 60fps 프레임 예산은 약 16.67ms이며 평균 FPS만으로 합격하지
  않는다. 최종 p95·지연 기준은 기능표를 따른다.
- 종료: 외형 전/후 측정 결과, 주행 영상, 알려진 문제, 패키지 재현 절차 확보.

## 에셋 선택·저장 규칙

1. 먼저 보유 에셋·허용 예산·원하는 스타일을 확인한다. 이 작업에서는 구매·다운로드·
   외부 plugin 설치를 진행하지 않았다.
2. [에셋 등록표](./assets/README.md)에 출처 URL, 취득일, 라이선스 유형/증빙 위치,
   사용 경로, 수정·출처 표시 조건을 기록한 뒤 import한다.
3. Fab에는 Standard와 CC-BY 등 서로 다른 라이선스가 있다. **무료 = 공개 Git 재배포
   가능**으로 간주하지 않는다. Standard의 독립 에셋 재배포 제한과 Reference-Only의
   source 제공 차이를 확인하고, 개별 취득 시점의 실제 라이선스를 기준으로 검토한다.
   [Fab 라이선스 안내 및 EULA](https://www.fab.com/eula)
4. 외부 에셋 원본을 공개 저장소에 push하기 전 배포 권한을 확인한다. 허용되지 않으면
   private 보관·설치 안내·프로젝트 내 재현용 placeholder 등 적합한 배포 방식을 정한다.
5. 후보 에셋은 작은 임시 검증 맵에서 먼저 UE 5.6 build/cook·축척·material·성능을
   검사한다. 대용량 팩 전체를 기본 주행 맵에 바로 넣지 않는다.

## 다음 작업

1. 생성한 코스에서 실제 PIE spawn·한 바퀴 주행·벽 비관통·경사 접지·후진을 확인한다.
   남쪽 직선 연석에 저속 coast와 충분한 속도로 각각 접근해 앞축→뒤축·차체 순차 상승,
   보도 위 안정과 Wall/Barrier 비관통도 확인한다. 자동 package 회귀는 통과했지만 이
   사용자 조작 인수는 아직 대기다.
   End PIE→Play reset과 변경된 맵 재Bake→자동 재연결도 확인한다.
2. 수동 시험에서 발견한 코너 반경·도로 폭·높낮이·카메라 문제를 수정하고 동일 자동
   회귀와 다시 비교한다. 생성 원본을 바꾸더라도 사용자 편집 맵을 자동 덮어쓰지 않는다.
3. 다음 자동 개발은 구현된 25-lane graph·3-head 서버 신호를 따르는 **NPC 1대의 차선
   추종·정지선 정지·신호에 따른 재출발**을 우선 검증한다. 통과 후 NPC 3~4대·보행자
   6~8명의 intent·경로 추종으로 확장하고 SensorRig·record/replay를 진행한다.
   목표 개체 수의 신호 준수·수동 인수는 별도 확인한다.
   본선 2곳 정지선 시각 표시는 별도 미완료 항목이다. 시각 도로와 차선/충돌의 단일 원본
   관리를 유지하며 실제 지도 importer는 요구하지 않는다.
4. 현재 기본 건물·창문·가로등 blockout을 통일된 외형으로 교체한다. 보유 에셋/예산
   확인과 라이선스 검토를 외부 에셋 import 전에 수행한다.

이 문서는 제작 기준과 blockout·lane/신호 기반 구현 기록이다. 최종 배경 품질,
NPC/보행자 동작, SensorRig·record/replay의 실제 개발과 수동·성능·30분 gate는
여전히 남아 있다. 관리 일정은
[일정표](./01_schedule.md)를 따르며, 지도 작업 감소를 전체 잔여 공수의 일괄 반감으로
계산하지 않는다.
