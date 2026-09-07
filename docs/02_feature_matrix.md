# 기능표

## 1. 문서 정보

| 항목 | 값 |
|---|---|
| 버전 | 3.20 |
| 작성일 | 2026-08-19 |
| 최종 수정 | 2026-09-07 |
| R1 | 수동운전 버티컬 슬라이스; Core RC 2026-09-07(9/8 수정 버퍼), Should 확장 2026-09-11(9/14 위험 버퍼) |
| R2 | 자율주행 환경·경로계획 기반, 일정 추후 확정 |
| R3 | 학습·평가 기반 FSD 연구, 일정 추후 확정 |
| 관련 문서 | [일정표](./01_schedule.md), [아키텍처](./03_architecture.md) |

## 2. 우선순위와 상태

**9/7 기준:** 58 lanes·신호 head 16개·교차로 2곳, NPC10·보행자8과 목적지 선택·우회·근접
후진 후 차선 변경을 구현했다. 충돌 후 이동·낙상·국부 차체/구조물 파손, 운전자와 세단 실내,
조명·클락션을 보완했고 네 collector 커브의 차선 교차를 v3로 수정해 저장 맵에 반영했다.
플레이어는 `1/2/3/4`로 세단·경차·트럭·오토바이를 선택하며 출발점 reset과 서버 물리 profile
변경이 함께 적용된다. 차종은 기록·재생에도 보존하며 SensorRig는 새 세션에서 초기화한다.

9/5 검증은 C++ **34/34**, 후속 관련 검사 **4/4**, Unreal **84/84** 통과다.
Unreal은 경고 없는 성공 79개와 기존 예상 경고 포함 성공 5개이며 실패는 없다.
최신 PIE 주행·충돌·차종/차선 표시, 운전석·외부 카메라 잔여, 패키징·서버 주소 설정,
목표 PC 1080p 60fps·입력 지연·30분 안정성과 촬영은 미인수다. 9/4 조기 인수는 미달성이며
9/7 Core 목표·9/8 수정 버퍼, 9/11 확장·9/14 위험 버퍼를 유지한다.
RGB smoke는 Should, LiDAR payload는 Future다. 오토바이는 네 접점 축약 모델로 실제 이륜
균형 물리가 아니며, 세단 외 차종의 실내·운전자 표현도 미포함이다.
[9/4](./worklogs/2026-09-04.md)·[9/5 작업일지](./worklogs/2026-09-05.md)에 상세 경계와 증거를 기록했다.

8/31 후속 이력: 회전좌표 적분의 가짜 에너지 증가를 제거했지만 당시 최고속 full-lock yaw 반전은
미해결이었다. 별도로 NPC 1대의 주행 기반을 선행했다.
[NPC 구현·검증 경계](./npc_lane_following.md), [당시 자동 검증](./worklogs/2026-08-31.md)을 따른다.
이하 이전 단계의 시험 수치는 해당 시점 이력이며 R1 전체 완료 선언이 아니다.

9/1 후속으로 가상 도심 연석·보도를 Ground support로 이행하고 semantic `Curb`만 검증해
planar body collision과 wheel/suspension 등판을 결합했다. 실제 package 자동 회귀와
 CTest 18/18·UE 35/35·Game/Editor 빌드·맵/traffic 검증은 통과했지만 사용자 PIE 인수는
 남아 있다. [9/1 작업일지](./worklogs/2026-09-01.md)를 따른다.

9/2 자동 검증 이력은 keyboard 미세 조향·rear side-brake drift·F3 overlay·full-body
rollover contact·6구역 dent·Ego↔NPC/보행자 반작용, `signal_city_v2` 42 lanes/16 heads
(차량8·보행8)/2 controllers,
NPC4·보행자8이다. map/traffic checksum은 `fnv1a64:7446108adad3e25b` /
`fnv1a64:dfcb5e4541d71adf`이며 CTest20/20·UE47/47·2,280-state smoke를 통과했다.
SensorRig metadata와 F5 snapshot CSV/F6 visual ghost는 골격만 구현됐고 결정적 물리 재생은
아니다. 서버는 공통 launcher가 port9000 listener PID와 실행 로그를 확인하며,
PIE·60fps·30분 인수는 남아 있다.
[빠른 시작](./signal_city_quickstart.md)과
[9/2 작업일지](./worklogs/2026-09-02.md)를 따른다.

9/3 사용자 승인 추가 범위: NPC 목적지 자동 선택·동일 목적지 우회·같은 방향 차선 변경.
현재 Signal City는 실제 저장 맵까지 좌·직·우 3차로로 갱신한 58 lanes/16 heads, NPC10대다.
map/traffic identity는 `fnv1a64:86c3103f3e2c7a5b` / `fnv1a64:b19c15afba6936d7`다.
기존 맵·패키지를 백업했으며 생성된 도로·연석·표시만 동기화했다.
이는 NPC 규칙 AI 확장이며 Ego 자율주행·보행자 자유 목적지 선택을 뜻하지 않는다.
[구현과 검증 경계](./npc_navigation.md)를 따르며 신규 전체 검증·PIE 인수 결과는 별도 기록한다.

| 표기 | 의미 |
|---|---|
| Must | R1 완료 선언에 반드시 필요 |
| Should | 시간이 허용되면 R1에 포함하지만 Must를 위험하게 만들면 연기 |
| Could | 선택 기능 또는 품질 개선 |
| Future | R2/R3 범위 |
| 결정 | 구현 방향이 합의됨 |
| 구현 | 현재 단계의 코드와 자동·통합 시험이 완료 기준을 충족 |
| 구현 중 | 일부 코드와 시험이 존재하지만 R1 완료 기준은 아직 미충족 |
| 실험 | 정해진 스파이크 결과로 상세 구현을 확정 |
| 제안 | 사용자 확인이 필요한 항목 |
| 연기 | R1에서 구현하지 않음 |

> 2026-08-21 검증 주석: `NET-001/002/003/006`, `UE-001/002`의 기능 경로는 이전 Windows UE 통합 검증을 통과했다. 이후 적용한 Unreal 연결 수명주기와 schema-v2 좌표·표시 경계는 이 Mac에서 정적 검토까지만 완료했으며, 최신 revision의 Windows UE 5.6 Game/Editor 재빌드·PIE는 R1 최종 게이트로 남아 있다.

> 2026-08-25 검증 주석: 최신 schema-v2 서버와 Unreal 5.6의 직접 수동운전 smoke test를 Windows에서 통과했다. 같은 날 추가한 MapPackage 지면 기반 z·pitch·roll 표시는 후속 PIE 시각 확인이 필요하다.

> 2026-08-26 물리 검증 주석: 외부 차량 SDK 없이 자체 모델의 경사 법선 하중·종횡 하중 이동, Ackermann 조향, RWD open differential와 60Hz 타이어 implicit coupling을 구현했다. 20° RWD 등판 자동 회귀와 CTest 9/9는 통과했으며, 특정 실차 정밀 검증과 사용자 Landscape 최종 PIE 등판은 후속이다.

> 2026-08-26 당시 상태: **M2 자동 게이트 완료·Unreal 수동 확인 대기, WP-03 진행 중**이었다. WP-03 자동 범위인 checksum/lifecycle gate, adaptive ground index·8m deterministic broad phase, strict OBB collision, Unreal `ASimCoreStaticCollider` authoring과 ground/static 원자적 export, opt-in `--demo-entities` lifecycle·`WorldState`·Unreal 표시는 구현됐다. 당시 최종 C++ Release 11/11, 핵심 4종 각 20회, UE 5.6 Editor build와 Landscape/demo runtime smoke도 통과했다. `landscape_local_v1`은 marker가 bake되지 않은 `static_colliders=0`이며 실제 PIE 벽·동적 충돌은 미검증이었다. 당시 잔여 Core는 38~55h였다.

> 2026-08-28 지면 전환 전 이력(아래 100cm snapshot으로 대체됨): 네 독립 wheel spring/damper 반력과 terminal footprint
> fail-close를 보완했고 실제 full-bounds Bake는 spacing 507cm, 19,208 triangles, ENU span 약
> 495.88×495.88×22.02m로 완료했다. host는 250ms manifest 감시, ground/static/checksum 전체
> 검증과 tick-boundary 원자 교체를 수행하고, Unreal은 checksum 변경 재연결에서 새
> PlaySession으로 Reset handshake를 자동 수행한다. generic sedan steering rate/return과
> lateral acceleration cap을 1.35rad/s·1.80rad/s·5.5m/s²로 보완해 약 5m/s 반경 4.52m,
> 약 10m/s 반경 20.08m 자동 계측을 통과했다. 최신 Windows Release build·CTest 14/14와
> 전체 suite 20회 280/280(48.35s), 핵심 7종 각 20/20, 선행 WebSocket 100/100,
> UE 5.6 Editor/Game build, GeoTransform Automation 1/1, same-PID hot-reload와 실제
> pre-Hello state 차단·invalid Hello 1008·same-sequence altered-payload 2회 거부·valid
> recovery state sequence 5148 smoke가 통과했다. 실제 Landscape PIE 조향·경사·자동 재연결과 `static_colliders>0` 벽 충돌은
> 수동 검증 대기다. 이 모델은 massless hub·1D suspension과 planar yaw를 사용하는
> reduced-order 구조이며 완전한 airborne/전복 6DoF나 특정 실차 동정 모델이 아니다.

> 2026-08-28 지면 책임 분리 주석: Unreal이 WorldStatic/Landscape 높이·ImpactNormal·
> 유효 cell을 기본 100cm compact snapshot으로 측정하고, C++는 immutable
> `GroundQuery`로 조회해 차량 물리만 계산하도록 분리했다. 실제 UE Bake는 `507×508` lattice,
> **257,556 samples/256,542 cells**, 6,177,368 bytes, 1m step과 checksum
> `fnv1a64:b8b0a6ccd89614de`의 `SIMGHF1`을 만들었다. 이후 `SIMGHF2` cell material ID와
> terrain friction multiplier, vehicle cfg v5 profile scale까지 writer→strict loader→tire
> force 경로를 구현했으며 v1은 default material·1.0 multiplier로 호환한다. 기존 20k
> triangle 제한과 507cm 자동 저하를 사용하지 않는다.
> 기존 triangle MapPackage v1 호환, header-only sentinel, checksum·manifest-last·hot reload를
> 유지한다. UE 5.6 Editor/Game build, C++ Release build, CTest 14/14과 신규 heightfield·
> runtime reload 각 50/50 반복이 통과했다. 서버 PID `6692`가 재시작 없이 새 checksum을
> verified/apply해 실제 writer→loader→tick swap도 통과했다. Landscape PIE 경사·요철·차체
> 자세, v2 material 재Bake, Sculpt 재Bake 후 Unreal 재연결과 `static_colliders>0` 충돌은
> 수동 gate다.

> 2026-08-28 선행 개발 주석: 자동 P2 5~7h 범위의 C++/UE GeoTransform·quaternion 계약과
> headless Automation 1/1, 양방향 Hello schema/map checksum/capability gate와 negotiated
> fingerprint, 경량 diagnostics
> HUD, strict runtime cfg&lt;CLI 외부 설정은 완료했다. HUD의 authoritative SafeStop/Health,
> 실제 PIE 좌표·표시, LaneGraph/traffic, SensorRig/recorder/replay는 남아 있으며 공통
> schema·최종 빌드·PIE를 순차 통합한다. 조건부 9월 4일 Core RC 인수
> 후보와 기존 9월 7일 관리 목표의 조건은 [일정표 2.4](./01_schedule.md#24-core-완료일-단축을-위한-병렬-선행-계획)에 따른다.

## 3. R1 범위 요약

> 2026-08-31 갱신: authoritative Health/HUD와 cross-play 전역 EStop 진단, 별도
> WorldStatic 지면·재질·static marker 자동 QA를 추가했다. 실제 UE 측정
> `SIMGHF2`·marker 2개의 QA package가 C++ 로더와 실제 WS 시험을 통과했다.
> 사용자 `NewMap`/기존 v1 package는 보존했으므로 이 결과를 실제 맵 Bake·벽 충돌·
> PIE 인수 완료로 올리지 않는다. 최신 수치와 남은 gate는
> [8/31 작업일지](./worklogs/2026-08-31.md)를 따른다. 같은 날 사용자가 배경을
> **작은 가상 도심 코스**로 변경하는 데 동의했다. 실제 Wall/Broad 재현과 Cesium·GIS
> 연동은 R1에서 제외하고, 기존 물리·통신·LaneGraph·교통·SensorRig·기록/재생·인수
> 기준은 유지한다. 상세 범위는 [제작안](./04_environment_plan.md)과
> [ADR-013](./decisions/ADR-013-small-virtual-city-course.md)을 따른다.

R1은 다음 장면을 완성하는 릴리스다.

> 플레이어는 로컬 에셋으로 구성한 작은 가상 도심의 주행 루프에서 C++ SimCore가 계산한 차량을 수동으로 운전한다. 양방향 도로, 신호 교차로 1개, 보도·횡단보도, 완만한 경사와 정차 공간을 갖추고, 소수의 NPC 차량·보행자 및 주행 기록·재생을 제공한다. 실제 지역의 지리적 재현은 목표로 하지 않는다.

2026-08-31 후속 구현으로 `/Game/VirtualCity/Maps/L_VirtualCity`와 `virtual_city_v1`을
실제 생성하고, 9월 1일 보도·연석 Ground support를 이행해 다시 Bake했다. 도형 594개/
ground component 390개(기존59+보도116+연석215)/static OBB 228개, 저장형 색상
재질 10개의 **prototype blockout**이다. 명목 240×200m와 달리 50cm Bake의 실제 valid
지면은 E `[-119.5,119.5]`, N `[-20,180]`m의 239×200m다. `SIMGHF2` 401×481 lattice의
192,881 samples 중 192,079 samples와 191,200 cells가 valid·drivable이고 checksum은
`fnv1a64:942842ea8d76b7a2`다. 기존 `NewMap`, `landscape_local_v1`, `wall_broad_v1`은
보존했다. 상세 실행은 [빠른 시작](./virtual_city_quickstart.md)을 따른다.

약 633.1m의 364개 `drive_route.csv` checkpoint는 **QA 전용**이며 LaneGraph나 traffic
완료를 뜻하지 않는다. 저장 맵 재로드 검증, C++ dense route·실제 입력 기반 물리 완주의
선행 시험, UE Automation 11/11, Editor/Game 빌드는 통과했다. 최종 회귀 재실행은
작업일지에서 관리하며 실제 PIE·외형 품질·목표 PC 성능·30분 안정성 gate는 남아 있다.

### 3.1 반드시 포함

8/31 차선·신호 후속: 별도 `traffic_network.json`에 25개 방향 차선/명시적 terminal/
T자 연결과 3개 차량 신호 head를 생성했다. C++가 고정 simulation time으로 30초 주기를
계산하고 WorldState와 함께 발행하며, Unreal은 검증된 fresh snapshot만 표시한다.
이는 차량 신호와 경로 **기반 구현**으로 NPC·보행 신호/통행 의도·Editor 수동 override·
AT-05 전체의 완료가 아니다. [설계·검증 경계](./traffic_network_signals.md)를 따른다.

같은 날 후속 승인한 조향 수정은 `input × max_steering_angle`의 속도 무관 목표각,
유한 rack 응답과 Ackermann 배분을 기준으로 한다. 속도 cap과
`comfortable_lateral_accel_mps2`는 제거하며 실제 궤적은 타이어 힘·부하가 결정한다.
당시 차량 설정은 strict v6로 전환했다. 이 v6 단계의 실제 ENU 궤적 측정과 C++ `ALL_BUILD`·CTest
**16/16(skip 0)**, UE Editor/Game Development 빌드·Automation **25/25**(신규 Steering
2개), 격리 Health smoke가 통과했다. 기존 신호 cleanup 경고 3개는 남고 신규 조향
시험 경고/실패는 없었다. 이 수치는 아래 추가 물리 수정 전 검증 이력이다.
기존 6.8/8.5m/s² 튜닝 수치는 이전 이력이다. [조향 계약](./vehicle_driving_refinement.md)을 따른다.

최신 소스는 strict cfg v7의 앞/뒤 per-tire cornering stiffness를 고정 60k/50k N/rad로 분리했다. v5/v6는
구 강성 값을 앞/뒤 새 항목에 각각 복사하고 v7로 올려야 하며, v5는 cap 항목도 삭제한다.
실제 Pawn Tick에서 접지 상실 후 조향 표시가 고정되는 버그를 재현·수정해
**UE Automation 26/26·Editor/Game 빌드**가 통과했다. 이는 fixture snapshot의 표시
회귀이며 실제 서버 최고속 주행을 대신하지 않는다. 서버의 hard-stop 지지력 기여가
타이어 마찰 계산에서 빠진 수정은 관련 직접 회귀와 기존 물리 시험을 통과했다. 하중 비례
강성 후보는 기존 Landscape 회귀에서 roll 65.681°>45°로 실패해 되돌렸다. 최종 C++
`ALL_BUILD`·**CTest 16/16(4.92초, 실패 0·skip 0)**와 **UE 26/26·Editor/Game 빌드**,
격리 Health smoke·새 서버 적용까지 통과했다. 일반 입력은 완만하게 개선됐지만
180km/h full-lock 과도 yaw 진동·사용자 PIE 인수는 남아 있다.

- 외부 C++ 차량 물리와 충돌 권한
- Unreal↔C++ 직접 양방향 통신
- 공통 지도·충돌 패키지와 LaneGraph
- 작은 가상 도심의 로컬 주행 루프와 보행 구역
- 신호 1개, NPC 차량 3~4대, 보행자 6~8명
- 센서 장착·시간·좌표 인터페이스 골격
- 1080p 60fps 목표와 30분 안정성 검증
- 주행 기록·재생과 촬영 가능한 패키지

### 3.2 명시적 제외

- 자율주행 학습과 학습 데이터 파이프라인
- 카메라 기반 인지 모델, 객체 검출, 차선 인식 모델
- Wall/Broad·NYSE·Federal Hall을 포함한 실제 지역·랜드마크 재현
- 실제 지도 GIS 취득·가공과 Cesium/WGS84 통합
- Manhattan 전체 또는 Lower Manhattan 전체 구현
- 대규모 군중 시뮬레이션
- 파편·부품 분리·고정밀 fracture를 포함한 상세 사고 재현(단순 누적 damage/차체 변형은 구현)
- 실차 계측 데이터 기반 정밀 차량 동정
- Google Photorealistic 3D Tiles 의존
- Python relay·ZMQ observer의 기능 확장과 R1 runtime 검증

### 3.3 Should 확장 범위(새 목표 9월 11일, 위험 버퍼 9월 14일)

8월 19일의 8월 27일 Core·8월 31일 확장 기준선은 미달성 이력으로 보존한다. 현재는 9월 7일 Core RC 통과 후에만 다음 Should 기능을 추가하며, 9월 8일 Must 수정 버퍼를 사용하면 Core 복구를 우선한다. 확장 포함 목표는 9월 11일이고 Core 수정으로 밀리면 9월 14일을 위험 버퍼로 사용한다. 핵심 성능·안정성을 깨면 해당 확장 기능만 제외한다.

- 속도·기어·FPS·연결·state age·SafeStop을 한 화면에 표시하는 운전 HUD
- 운전자·추적·고정·자유 카메라 전환
- reset·reconnect·scenario restart 퀵 액션과 데모 시나리오 프리셋
- 충돌·차선·휠 접촉·타이어 힘 디버그 오버레이
- replay timeline, play/pause, 배속, seek, 카메라 조작

## 4. 기능 상세

### 4.1 지도와 환경

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| MAP-001 | Must | 구현 중 | 가상 도심 MapPackage | Unreal-owned configurable height/normal/material snapshot(일반 기본100cm, 가상 도심50cm), v1/v2 호환, 전체-bounds preflight/Fit·2M sample 상한, marker authoring·manifest-last·strict loader·250ms 감시/tick swap 구현. `virtual_city_v1`의 Ground390/static228·checksum `e1805b…`와 초기 `signal_city_v2`의 Ground124/static49·checksum `7446108a…` 검증은 생성 당시 이력이다. 현재 Signal City는 3차로 갱신 후 collision `fnv1a64:86c3103f3e2c7a5b`를 사용하며 커브 도색 v3 적용 후에도 동일하다. 저장 맵·package 보호 갱신과 ValidateOnly를 통과했고 기존 맵을 보존했다. 최신 PIE·재Bake/reconnect 및 원점·spawn 계약의 최종 인수는 후속 | 다른 도시·트랙도 같은 포맷 사용 |
| MAP-002 | Must | 결정 | 로컬 ENU 원점 metadata와 왕복 변환 | 지도 원점·축·단위를 versioned metadata로 고정하고 기준점의 로컬 ENU↔Unreal 왕복 시험을 통과. Cesium/WGS84 통합은 Future이며 R1 게이트가 아님 | 다른 로컬 코스와 향후 GNSS 연동에 재사용 |
| MAP-003 | Must | 구현 중 | 로컬 주행면·보도·연석 | 별도 저장 맵의 8m 양방향 도로·2.5m 보도·0.24m 연석·4° 경사·정차 공간을 기본 도형과 immutable collision로 구성. 기존 Ground 59개에 보도116·연석 top215를 더한 390개와 Curb marker 이중 저작, dense route/네 바퀴·실제 물리 완주 및 실제 package 연석 회귀 통과. 50cm production full-footprint probe에서 authored marker 중심 215곳 중 도로/보도 연석 중심 214곳이 eligible하고, 전체 along-length QA는 210/215 전 길이와 T-opening 네 끝단·BayEnd의 의도적 gap을 확인. 화면의 접지·비관통·카메라·재Bake와 위치별 연석 PIE 수동 gate는 후속 | 다른 MapPackage에서 자동 생성 |
| MAP-004 | Must | 결정 | 보행 구역과 통행 제한 | 보도·보행 전용 공간을 차량 경로가 통과하지 않고, 지정 횡단보도에서만 차량·보행 경로가 교차 | 통행 제한 속성으로 재사용 |
| MAP-005 | Must | 구현 중 | LaneGraph 자동 생성 | `virtual_city_v1` 25개 차선을 보존하고 `signal_city_v2` 58개 lane·두 교차로·좌/직/우 차로와 같은 방향 차선 변경을 저작 원본에서 생성한다. `lane_changes`의 대상·station·대응·실제 충돌면/차선폭을 검증한다. 저장 맵은 네 collector 커브의 좌우 offset 뒤집힘을 수정한 도색 v3로 보호 갱신했다. collision `86c3103f3e2c7a5b`/traffic `b19c15afba6936d7`은 유지. 최종 주행·시각 정렬 인수는 후속; GIS import는 요구하지 않음 | NPC와 경로계획의 공통 그래프 |
| MAP-006 | Must | 결정 | 핵심 구간 수동 보정 | 작성 중심선에서 생성한 차선·교차로가 시각 도로와 일치하도록 Editor에서 보정 가능 | 수정값을 원본 위의 override로 보존 |
| MAP-007 | Future | 연기 | Cesium 배경·WGS84 통합 | R1에서는 설치·통합·배경 on/off 검증을 요구하지 않으며 실제 지역 확장 시 별도 성능·좌표 게이트로 재검토 | 다른 지역의 지리 좌표와 중·원경 배경 |
| MAP-008 | Must | 결정 | 지도·에셋 출처와 라이선스 기록 | 자체 작성 도로·배치 원본은 작성 주체·버전/hash·변환 이력을 기록하고, 외부 에셋은 원본·버전·취득일·라이선스·attribution을 package metadata와 출처 문서로 추적 | 배포·영상 출처 관리 |
| MAP-009 | Must | 구현 중 | 모듈형 가상 도심 환경 | Engine Cube·자체 색상 재질 10개의 건물·창문·가로등·도로/표시 blockout과 낮 조명 구현. prototype art이며 최종 외형·출처/라이선스·패키지 성능 게이트는 미완료; 실제 랜드마크 에셋은 불필요 | 환경 에셋 교체 가능 |

### 4.2 C++ SimCore와 차량 물리

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| PHY-001 | Must | 결정 | C++ 최종 물리 권한 | Ego 및 충돌 참여 엔티티의 최종 pose를 C++가 확정 | 수동·자율 모드 동일 |
| PHY-002 | Must | 구현 중 | 자체 C++ 차량 동역학 코어 | 외부 차량 SDK 없이 단계별 모델과 회귀 시험이 macOS·Windows에서 동작 | 수식·파라미터·상태를 직접 확장 가능 |
| PHY-003 | Must | 구현 | 로컬 ENU 물리 좌표 | 위·경도를 직접 적분하지 않고 meter 단위 ENU에서 계산 | GNSS 변환과 대규모 월드 대응 |
| PHY-004 | Must | 구현 중 | 고정 시뮬레이션 tick | 렌더 FPS와 무관한 기본 60Hz와 overrun skip은 구현; configurable substep은 후속 | headless·재생·학습에 재사용 |
| PHY-005 | Must | 구현 중 | 기본 차량 동역학 | 네 wheel spring/damper·wheel-local tire force·RWD·Ackermann과 60Hz coupling을 유지한다. 키보드 조향은 짧은 A/D 입력을 제곱 응답으로 완화하고, Space rear side brake는 rear longitudinal-first friction allocation으로 뒤축 횡그립을 해제해 drift를 허용한다. full-body chassis shell은 rollover 지면 관통을 막고 충돌 impulse 기반 damage를 계산한다. 자동 회귀는 통과했지만 조작감 PIE, 완전 airborne 6DoF와 실차 동정은 미완료 | 차량 설정 교체로 다른 차종 지원 |
| PHY-006 | Must | 구현 중 | 정적 충돌 | strict OBB CSV, 수평 SAT+수직 interval, 0.10m/1°·64 microstep fail-closed, impulse와 차량 XY/yaw 통합, adaptive ground index·8m 결정적 broad phase 구현. `virtual_city_v1` marker 228개를 Bake했다. semantic Curb의 current→predicted swept along 구간을 `R` 이하 간격으로 검사하고 near(+1R)·far(+3R) signed delta와 collider 높이 계약이 모두 맞을 때만 해당 step의 planar body solve에서 제외한다. unknown/Wall/Barrier·0.75R 초과 턱·support 누락 위치는 fail-closed한다. 실제 package 연석 통과, 30/50m/s gate stress와 0.30m 턱/Wall 차단 자동 회귀 완료. 실제 PIE 연석·벽 비관통과 화면 일치 확인 후 완료 | 시나리오 지도 공통 |
| PHY-007 | Must | 구현 중 | 동적 충돌 프록시 | Ego와 runtime 유한질량 반작용 및 NPC↔보행자 등 runtime pair 충돌을 구현했다. 보행자는 접촉 강도에 따라 반응하며 전도 시 낮은 OBB와 서버 몸통·UE 부분 래그돌로 전환한다. 지면에 쓰러진 낮은 사람/기둥은 조건부 타이어 지지로 넘을 수 있고, 보닛/지붕은 실제 겹친 근사 표면만 지지한다. 기둥은 기초부 저항 해제 뒤 유한질량으로 반응한다. 관절별 서버 충돌·완전한 차량 6DoF 모델과 PIE 감각 인수는 별도 | 다중 에이전트 |
| PHY-008 | Must | 구현 | 노면 재질·마찰 | `SIMGHF2` cell의 stable default/asphalt/low-friction/rough ID와 terrain multiplier를 strict loader가 전달하고, tire friction×terrain multiplier×현재 차량 설정 v7의 profile scale로 마찰 계수를 구성한다. profile scale은 v5에서 도입돼 v7에서도 유지한다. 지면 v1은 default·1.0으로 호환하며 경계 cell은 보수적으로 최저 마찰을 택한다. 이 계수 경로와 별개인 hard-stop tire load 직접 회귀·최종 물리 검증 상태는 PHY-005를 따른다 | 젖은 노면 등 시나리오 확장 |
| PHY-009 | Must | 구현 | 입력 안전장치 | 입력 clamp·세션·순번·100ms 큐 age를 검증하고, 250ms에는 즉시 SafeStop만 적용해 같은 session의 fresh command로 복구하며 1초 연속 단절에서만 session retire·1008 close | 자율주행 fail-safe |
| PHY-010 | Must | 구현 | 물리 파라미터 파일화 | strict `vehicle_sedan.cfg` v7의 앞/뒤 타이어별 강성·구동·제동·서스펜션 설정을 사용하며 legacy version/key는 거부한다. 이 세단 설정에서 경차·트럭·오토바이 Ego profile을 만들고 새 주행 lifecycle에서 질량·축간거리·조향·출력·제동·타이어·관성·서스펜션·충돌 외곽을 함께 교체한다. 재연결 시 차종/자세를 유지하며 명시적 선택만 새 세션으로 초기화한다. 오토바이는 네 접점 축약 모델이며 실제 이륜 균형·실차 제원 동정은 미포함 | 차량 교체·튜닝·시험 |
| PHY-011 | Must | 구현 중 | 물리 회귀 시험 | 경사·contact·collision·terminal footprint·heightfield/material·manifest-last 회귀를 유지한다. v7 hard-stop 반력·고정 강성 60k/50k와 small-signal, 0.24m Curb의 300N 정지/6000N 등판·60/120/240Hz, 30/50m/s swept gate stress, 0.30m 턱/Wall 차단, 제한된 50cm raster 허용치와 실제 package 연석 계측을 포함해 CTest 18/18, UE Automation 35/35, Game/Editor와 맵/traffic 검증 통과. 고속 stress는 180km/h 승차감 검증이 아니다. 표시 fixture와 실제 solver 주행 증거를 분리한다. 최고속 full-lock 과도 yaw·사용자 PIE·연석 위치별 체감·replay는 후속 | 수식·파라미터 변경 검증 |
| PHY-012 | Future | 연기 | 실차 파라미터 동정 | 대상 차량 계측 데이터와 기준 주행에 맞춰 오차 검증 | 차량별 현실성 향상 |

### 4.3 통신과 동기화

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| NET-001 | Must | 구현 | Unreal↔C++ 직접 연결 | Python relay 없이 명령과 상태가 양방향 전달됨 | 지연과 장애 지점 감소 |
| NET-002 | Must | 구현 | 단일 공통 Protobuf 스키마 | C++와 Unreal wire adapter가 루트 `protocol/`의 하나의 R1 필드 계약을 사용; 동결된 Python 생성물도 같은 원본에서 생성 | 스키마 중복 제거 |
| NET-003 | Must | 구현 | 메시지 Envelope | schema version, sequence, simulation time, source, map checksum, session ID 포함 | 기록·재생·오류 진단 |
| NET-004 | Must | 구현 중 | 재연결·중복·순서 처리 | session별 sequence·큐 age, 250ms soft SafeStop/1초 hard socket 폐기, 같은 session fresh-command 복구, Unreal 자동 재연결과 reset 전 일반 control 차단을 구현했다. map checksum 변경 시 서버가 이전 socket/session을 fence하고 Unreal이 새 PlaySession으로 자동 reset한다. 양방향 `Hello`가 build/source/schema/map checksum과 필수 capability를 검증하며, build/schema/map checksum/sorted capability set의 negotiated fingerprint와 connection generation별 source/session/highest sequence를 고정한다. 같은 sequence는 message kind와 semantic payload가 정확히 같은 재전송만 idempotent하게 허용하고 altered payload는 거부하며, 거부된 높은 sequence는 high-water를 오염시키지 않는다. server는 승인 전 state broadcast를 차단하고 첫 frame 거부는 1008 close하며, 승인 뒤 ordinary payload 거부는 socket을 유지한다. log 노출 identity는 printable ASCII만 허용한다. 실제 PIE 재연결 full snapshot 표시는 후속 | 네트워크 견고성 |
| NET-005 | Must | 구현 중 | MapPackage handshake | 양쪽이 실제 collision payload checksum을 독립 검증하고 `Hello`에서도 checksum을 교차 확인한다. Unreal은 server Hello와 authoritative checksum이 모두 일치한 뒤에만 Reset/Control을 보낸다. host의 background 완전 검증·tick-boundary 원자 swap과 UE 재연결 manifest 재검증·새 session reset을 구현했다. fixture smoke에 이어 실제 checksum `fnv1a64:b8b0a6ccd89614de`를 PID 6692가 same-PID 적용했다. PIE 오류 표시·Sculpt 재Bake 자동 재연결 감각은 후속 | 충돌 불일치 방지 |
| NET-006 | Must | 구현 | WebSocket binary + Protobuf transport | JSON 없이 ControlCommand와 WorldState가 C++↔Unreal 사이에서 전이중 전달되고 HTTP 101보다 payload가 선행하지 않는다. 기본 listener는 `127.0.0.1` 전용이며 initial server Hello만 readiness 예외이고 client Hello 승인 전 WorldState는 전달되지 않는다. server-initiated close가 peer reply 또는 정체 write로 끝나지 않으면 250ms transport grace 뒤 TCP shutdown을 강제한다 | 이후 인증·설정 가능한 LAN 또는 측정 결과에 따른 UDP/IPC로 교체 가능 |
| NET-007 | Future | 연기 | 고대역 센서 transport | 이미지·LiDAR는 control/state와 분리된 shared memory/전용 채널 사용 | FSD 처리량 확보 |
| NET-008 | Must | 구현 중 | 저지연 60Hz 전달 | Windows 로컬 state 간격 p95 18.5ms 이하, 변화 입력 coalescing 후 33ms 이내 전송; packaged build에서 command/state age를 재측정 | LAN·다중 엔티티 확장 시 transport 판단 기준 |
| NET-009 | Must | 구현 중 | 좌표·부호 schema 계약 | schema v2에서 공개 body FLU와 좌회전/좌조향 양수, 별도 시계 방향 heading, C++/UE position·polar/axial vector·quaternion basis adapter와 exact version/Hello gate를 구현했다. UE 5.6 Game·Editor build와 headless GeoTransform Automation 1/1이 성공했다. Python/ZMQ는 동결하며 로컬 지도 원점 metadata·SensorRig frame·PIE 시각 검증은 후속; Cesium geodetic 통합은 Future | 향후 Python·센서·ROS 확장 시 동일 계약 사용 |

### 4.4 Unreal IG와 수동운전

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| UE-001 | Must | 구현 | 수동 입력 경로(현재 Pawn 내장) | W/S/A/D·Space와 게임패드 입력에 deadzone을 적용한다. 키보드 A/D는 `DeltaSeconds` 기반 rise/return으로 frame-independent하며 최신 변화값 30Hz·유지 명령 20Hz heartbeat를 사용한다. `1/2/3/4`로 세단/경차/트럭/오토바이를 요청하며 다른 차종 선택 시 입력을 비우고 새 PlaySession으로 출발점 reset한다. 같은 차종 재선택은 reset하지 않는다 | 향후 ManualInputComponent로 분리해 AI 명령과 같은 포맷 사용 |
| UE-002 | Must | 구현 | ExternalVehiclePawn | C++ Ego pose·wheel state를 표시하며 시각 휠 회전은 부호 있는 종방향 속도와 타이어 반지름에 연동하고 정지 시 0이다. authoritative 차종 수신 뒤 세단/경차/트럭/오토바이 차체·휠 위치/크기·램프·배기 위치를 변경한다. 오토바이는 보이는 바퀴만 2개이며 서버는 네 접점이다. 세단 실내·운전자는 다른 차종에서 비활성화한다. Ego Chaos 동역학은 비활성이고 NPC/보행자는 별도 proxy로 표시한다. 최신 비율·주행 감각 PIE는 별도 인수 | 외부 물리 엔티티 공통 기반 |
| UE-003 | Must | 구현 중 | 상태 표시·제한 외삽 | 최대 50ms dead reckoning, stale wheel 정지, 자동 재연결, checksum/play-session gate와 runtime entity 표시를 구현했다. 재연결마다 manifest를 재검증하며 checksum 변경 시 새 PlaySession/Reset을 자동 수행한다. 경량 HUD가 state rate·sequence·local/server age·max gap과 forward sequence `missing`, duplicate/out-of-order `old`를 분리 집계한다. 실제 재연결 full snapshot과 demo/map-reload PIE 검증은 후속 | 네트워크 지연 완화 |
| UE-004 | Must | 구현 중 | 좌표 변환 어댑터 | C++/UE가 `map_enu`·body FLU↔Unreal FRU position, polar/axial vector와 quaternion basis 계약을 구현했고 Windows headless round-trip Automation 1/1이 통과했다. 로컬 지도 원점 metadata, SensorRig frame과 실제 코스 PIE 시각 검증은 후속; Cesium geodetic 통합은 Future | 지도 원점 변경과 ROS 호환 센서에 재사용 |
| UE-005 | Must | 구현 중 | 운전자 카메라와 외부 카메라 | 8/31 외부 추적 orbit 카메라·마우스 회전·휠 줌·C 복귀·수평 유지·벽 충돌 검사와 실제 입력 QA 구현. 운전석과 외부 고정 camera rig 및 replay 촬영은 후속 | 센서와 촬영 분리 |
| UE-006 | Must | 구현 중 | 핵심 디버그 HUD | 연결·Hello·map·state·Health/SafeStop 진단과 damage/impact 표시를 구현. 실제 PIE 가독성은 후속 | 통합 문제 진단 |
| UE-007 | Should | 구현 중 | 충돌·차선·휠 디버그 오버레이 | `F3`로 chassis box, wheel/contact normal·force와 impact 정보를 켜고 끈다. 실제 PIE 가독성·포트폴리오 캡처는 후속 | 지도·차량 물리 보정과 FSD 디버깅 |
| UE-008 | Must | 구현 중 | 패키지 실행 설정 | C++ 서버는 strict `runtime_server.cfg`와 cfg&lt;CLI 우선순위로 port/Hz/origin/spawn/timeouts/source/demo/vehicle/map을 외부화하고 unknown·duplicate·missing·non-finite·range 오류를 fail-closed한다. 목표 Windows packaged UE의 서버 주소 외부 파일화와 배포 smoke는 후속 | 배포 재사용 |
| UE-009 | Should | 결정 | 운영 퀵 액션과 데모 프리셋 | reset·reconnect·scenario restart를 화면에서 실행하고 대표 데모 시나리오를 선택해 같은 초기 상태로 시작 | 반복 QA와 포트폴리오 시연 |
| UE-010 | Should | 구현 중 | 4종 카메라 모드 | 외부 추적 카메라의 자유 orbit/줌/reset 선행 구현. 운전석·외부 고정·독립 자유 이동 전환과 replay 촬영을 포함한 전체 완료는 후속 | 센서와 촬영 분리 |
| UE-011 | Should | 구현 중 | 통합 운전 대시보드 | Blueprint나 외부 UI 에셋 없이 GameMode가 코드 기반 계기판을 자동 생성한다. fresh·connected authoritative `WorldState`의 속도(km/h)·RPM·기어·연료만 표시하고 stale/reconnect/offline에서는 값을 `---`로 지운다. `SIDE BRAKE`는 서버 확인값이 없어 로컬 command intent만 별도 표시한다. 16:9 반응형 Canvas와 표시 계약·자동 GameMode 설치 Automation 2/2는 통과했다. 기존 diagnostics HUD의 FPS/state age/SafeStop 통합과 실제 PIE 가독성은 후속 | 데모 운전과 성능·지연 진단 |
| UE-012 | Should | 구현 중 | 차량 시청각 피드백 | Pawn이 별도 에셋 없이 exhaust CPU sprite와 procedural synth를 자동 생성한다. 배기가스는 authoritative RPM·종가속도, 엔진음은 RPM, 타이어 마찰음은 차속·접지·published slip만 읽으며 stale/unavailable state에서는 emission을 감쇠하고 소리를 끈다. 물리·제어·protocol에 값을 되먹임하지 않는다. focused Automation은 배기가스 2/2·오디오 1/1 통과했고 실제 PIE 외형·음량·공간감 인수는 후속 | 차량 상태를 체감 가능한 포트폴리오 표현으로 연결 |

### 4.5 신호, 차량 AI, 보행자

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| TRA-001 | Must | 구현 중 | 신호 상태기계 | v1 호환을 보존하고 `signal_city_v2`의 2 controllers·차량8+보행8 heads를 같은 authoritative clock/reset으로 발행한다. 기둥 충돌 전도·고장 시 해당 head 소등과 controller 전체 적색을 NPC·보행자 판단에도 적용한다. 실제 PIE/AT-05는 후속 | controller plan을 추가 교차로에도 재사용 |
| TRA-002 | Must | 구현 중 | TrafficDirector | `npc_autonomous`에서 도달 가능한 목적지 결정·directed successor 최단 경로·전방 80m의 정적/정차 장애물에 대한 동일 목적지 우회·저작된 같은 방향 차선 변경을 계산한다. 신호 대기열은 폐쇄 도로가 아니며 실패 시 정지/대기한다. legacy 고정 route 모드는 보존; traffic demand·Ego FSD는 후속 | C++ 물리와 분리된 rule AI |
| TRA-003 | Must | 구현 중 | NPC 차량 10대 | `signal_city_v2` 10대가 목적지 선택·우회·안전 차선 변경을 수행한다. 4차종 profile, 실제 3차로·좌/직/우 연결, 사고 차량·누운 보행자 분류, 이동 의도의 깜빡이를 사용한다. 너무 가까운 장애물 앞에서는 뒤 공간을 검사해 후진으로 조향 여유를 만들고 우회를 시도한다. 사고 복귀도 이동 경로·조향을 사용하며 안전 틈이 없으면 대기한다. 최종 연속성·회피 PIE는 별도 | 자율주행 상호작용 대상 |
| TRA-004 | Must | 구현 중 | 보행자 6~8명 | 8명이 지정 횡단보도와 독립 WALK 신호를 따른다. Manny/Quinn과 +X 전방을 유지한다. 가벼운 접촉과 전도를 구분하며 접촉 높이의 각충격량·중력·차량 근사 지지면과 UE 부분 래그돌을 연결했다. 낙상 후 접지·기상·걷기 전환도 보완했다. 서버 전신 관절 동역학·실사 민간인 아트는 미포함. 최신 애니메이션·충돌 PIE 인수는 후속 | 센서와 위험 시나리오 대상 |
| TRA-005 | Must | 구현 중 | 동적 엔티티 물리 상태 | NPC10·보행자8의 authoritative identity/shape/velocity/collision과 UE 표시를 통합했다. 충돌 후 정착·대기·주행 경로를 따른 저속 복귀, 강충격 운행 중단을 유지하며 사고 뒤 후속 충돌도 유한질량으로 반응한다. reaction phase·event·impulse·방향을 wire로 연결한다. NPC의 지면 구속 상자 전도와 보수적 충돌 외곽은 유지하며 자유 6자유도 모델은 아니다. 새 Play/reset 복원은 자동 검증했고 실제 PIE 인수는 후속 | C++/Unreal 불일치 제거 |
| TRA-006 | Future | 연기 | 군중 시뮬레이션 | 대규모 보행자 회피·밀도 모델 | R1 제외 |
| TRA-007 | Could | 구현 중 | 사고·파손 | Ego/NPC에 접촉 위치·힘 방향·깊이를 가진 최대16개 국부 dent를 보존하고 최대28cm 실제 CPU 정점 변형으로 표시한다. 면 전체 WPO는 끄며 앞문/뒷문/반대 면을 구분한다. 같은 Play 재연결에서 손상을 복구하고 새 Play는 원복한다. 건물의 국부 흔적·파편 및 신호등 전도는 유지. 차체 collision shell 변형·부품 분리·금속 파괴 역학은 미포함. [구현 범위](./traffic_crash_feedback.md)와 PIE 인수는 구분 | 상세 crash 확장 |

### 4.6 센서와 기록

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| SEN-001 | Must | 구현 중 | SensorRig와 장착 좌표 | config에 `base_link` FLU front camera10Hz·roof LiDAR20Hz mount를 정의한다. 차종 변경의 새 PlaySession에서도 lifecycle을 동기화한다. 실제 RGB payload는 SEN-003 Should, LiDAR payload는 SEN-005 Future | 카메라·LiDAR·Radar 공통 |
| SEN-002 | Must | 구현 중 | 센서 시간·좌표 메타데이터 | authoritative `SimulationTimeNs`, frame/parent/pose/sequence metadata cadence 골격 구현. 새 PlaySession과 차종 변경 때 sequence·센서별 cadence를 초기화하며 자동 회귀 통과, 최종 통합 인수는 후속 | 다중 센서 동기화 |
| SEN-003 | Should | 제안 | 전방 RGB 카메라 smoke test | 낮은 주기로 프레임과 메타데이터 1개를 기록 | 향후 인지 입력 |
| SEN-004 | Future | 연기 | GNSS·IMU 모델 | noise/bias가 구성 가능한 GNSS·IMU 생성 | localization 학습·평가 |
| SEN-005 | Future | 연기 | LiDAR·Radar·분할 카메라 | 개별 주기와 좌표계로 데이터 생성 | perception 입력 |
| REC-001 | Must | 구현 중 | 주행 기록 | `F5` snapshot CSV v2와 서버 `--record-physics`에 적용 입력·reset/lifecycle·차종·동적 충돌 입력·결과 상태를 기록한다. AgentIntent/traffic AI 전체 이벤트 재실행과 사용자 기록 인수는 후속 | 회귀 시험과 데이터셋 |
| REC-002 | Must | 구현 중 | 결정적 재생 | `F6` visual ghost에 더해 서버 `--verify-physics-replay`가 동일 executable/cfg/map/origin/Hz에서 차종별 Ego 물리를 frame별 재계산한다. 외부 동적 proxy는 기록값을 사용하며 위치1cm/yaw0.1도 기준을 검증한다. 차종 없는 legacy RESET/CSV v1은 세단으로 읽는다. 실제 사용자 기록·재생 인수는 후속 | 버그 재현과 영상 촬영 |
| REC-003 | Should | 구현 중 | replay 기반 영상 | snapshot ghost 재생은 가능. 카메라/MRQ 촬영 통합은 후속 | Movie Render Queue 촬영 |
| REC-004 | Should | 결정 | replay 조작 UI | timeline, play/pause, 배속, seek, 카메라를 조작해 기록을 탐색·촬영 가능 | 회귀 분석과 데이터 검수 |

### 4.7 자율주행 확장

| ID | 릴리스 | 상태 | 기능 | 설명 |
|---|---|---|---|---|
| AUTO-001 | R2 | 연기 | 시작점·목적지 설정 | LaneGraph의 유효 지점으로 snap하고 경로 요청 |
| AUTO-002 | R2 | 연기 | 최단·최소비용 경로 | 방향, 통행 제한, 거리, 회전, 신호 비용을 적용한 A* |
| AUTO-003 | R2 | 연기 | 행동 계획 | 차선 유지, 정지, 양보, 회전, 재계획 상태기계 |
| AUTO-004 | R2 | 연기 | 궤적 생성·제어 | 경로를 시간 기반 trajectory로 만들고 steering/throttle/brake 출력 |
| AUTO-005 | R2 | 연기 | Python AutonomyServer | 센서·상태 입력으로 ControlCommand를 생성하며 물리를 계산하지 않음 |
| AUTO-006 | R2 | 연기 | 시나리오·평가 | 충돌, 신호 위반, 경로 이탈, 승차감, 도착률 측정 |
| AUTO-007 | R3 | 연기 | 카메라 기반 인지 학습 | 라벨·센서 데이터셋과 모델 학습·평가 |
| AUTO-008 | R3 | 연기 | imitation learning | 전문가 trajectory와 정책 학습, 폐루프 평가 |
| AUTO-009 | R3 | 연기 | 대규모 데이터 생성 | headless/다중 실행, 날씨·시간·traffic 변형 |

## 5. 비기능 요구사항

| ID | 우선순위 | 요구사항 | R1 검증 기준 |
|---|---|---|---|
| NFR-001 | Must | 성능 | 목표 PC의 패키지 빌드, 1920×1080, uncapped 평균 60fps 이상; p95 frame time 18.5ms 이하를 함께 충족 |
| NFR-002 | Must | 안정성 | 30분 연속 수동 주행 중 crash, deadlock, 무한 재연결 없음 |
| NFR-003 | Must | 안전 정지 | command age가 기본 250ms를 넘으면 throttle 해제 후 제동 |
| NFR-004 | Must | 좌표 일치 | ENU·FLU·Unreal·camera optical 기준축, polar/axial vector, heading/yaw, quaternion 왕복 자동 시험 통과; 기준점 왕복 오차 5cm 이하 |
| NFR-005 | Must | 표시 일치 | 정상 연결 상태에서 Unreal 표시 pose와 최신 C++ 기준 pose 차이 5cm/0.5도 이하 |
| NFR-006 | Must | 충돌 일치 | 동일 collision source checksum을 사용하며 지속 관통이 없음 |
| NFR-007 | Must | 재현성 | 같은 빌드·설정·입력 replay의 최종 위치 1cm, yaw 0.1도 이내 |
| NFR-008 | Must | 설정 가능성 | 서버 주소, 지도, 차량, tick, 센서 장착값을 재컴파일 없이 변경 |
| NFR-009 | Must | 관측 가능성 | tick overrun, packet age, sequence gap, collision, reconnect를 구조화 로그로 기록 |
| NFR-010 | Must | 라이선스 | 지도와 배포 에셋의 출처·라이선스·attribution 기록 |
| NFR-011 | Should | CI 회귀 | C++ 단위 시험과 Proto/MapPackage schema 검증을 명령 하나로 실행 |
| NFR-012 | Must | 로컬 제어 반응 | 목표 Windows PC의 loopback에서 state 간격 p95 18.5ms 이하, 최대 25ms 이하, 입력→첫 상태 변화 50ms 이하 |

## 6. R1 인수 시험

| 시험 ID | 시나리오 | 성공 조건 | 관련 기능 |
|---|---|---|---|
| AT-01 | 서버 실행 및 지도 handshake | 동일 체크섬일 때 시작, 불일치일 때 명확히 거부 | MAP-001, NET-005 |
| AT-02 | 수동 가속·제동·조향·후진 | 입력이 C++ 고정 tick에 반영되고 Unreal에 표시되며 NFR-012 지연 기준을 통과한다. S/W 방향 전환·동시 입력·stale 제동이 안전하고 표시 휠이 차속과 일치한다. 동일 입력·초기 rack·tick 경과의 목표각/응답은 속도와 무관하고 유한 slew·중립 복귀·Ackermann·타이어 힘 예산을 지킨다. 실제 ENU CG 궤적과 종방향/궤적 속도·slip을 구분 기록하며 유효 도로 입력의 반경은 기하학적 CG 참고의 0.85~1.5배 범위로 검증한다. 90km/h full-lock에는 이 반경 범위·무미끄럼을 강요하지 않는다. 이전 speed-cap 기반 반경 범위는 제외하고 수정본 PIE 조작감을 별도 인수한다. | PHY-004~005, UE-001~003, NFR-012 |
| AT-03 | 연석 등판·벽 충돌 | 연속 Ground support와 허용 높이를 가진 Curb는 충분한 운동 상태에서 앞축→뒤축 순으로 오르고, Wall·Barrier·높은 턱은 지속 관통하지 않으며 C++ 결과와 Unreal 표시가 일치 | PHY-005~006, NFR-006 |
| AT-04 | NPC·보행자 상호작용 | 동적 proxy가 충돌 월드와 화면에서 같은 엔티티를 나타냄 | PHY-007, TRA-003~005 |
| AT-05 | 신호 동작 | 차량과 보행자가 자신의 신호에 맞춰 정지·이동 | TRA-001~004 |
| AT-06 | 입력 연결 중단 | 250ms 후 안전 정지하고 HUD와 로그에 원인이 표시 | PHY-009, NFR-003 |
| AT-07 | 기록·재생 | 같은 기록을 재생해 NFR-007 오차 이내 도착 | REC-001~002 |
| AT-08 | 장시간·성능 | 목표 PC에서 30분 안정성과 NFR-001 측정값 기록 | NFR-001~002 |
| AT-09 | 촬영 | replay를 카메라 변경 후 재생하고 영상 출력 가능 | UE-005, REC-003 |
| AT-10 | 좌표·부호 계약 | 좌회전에서 body yaw·steering·wheel lateral 부호가 FLU 계약과 일치하고, heading 관계식 및 FLU↔Unreal 자세 왕복 시험 통과 | NET-009, UE-004, NFR-004 |

9/2 자동 통합의 CTest20/20·UE47/47·Signal City 2,280-state 이력에 이어, 9/3에는 별도
서버 적용 입력 기반 Ego replay와 opt-in 성능 측정 도구를 추가했다. 9/5에는 차종별 기록·재생과
세션 초기화까지 보완했다. 최신 실행 결과는 [9/5 작업일지](./worklogs/2026-09-05.md)를 따른다.
`F5/F6` 자체는 여전히 snapshot/ghost이며
NPC·보행자 AI 재실행과 사용자 실제 기록 인수까지 완료했다는 의미는 아니다.
AT-02/04/05/08의 조작감·HUD·traffic PIE·패키지60fps·30분 시험과 입력 지연은 미완료다.

9/7 현재 최신 차종 선택·커브 표시·충돌/회피 수정본의 최종 PIE 인수는 대기다. 58 lanes·NPC10·
보행자8과 자동 검증 통과를 실제 조작감·패키지 성능·30분 안정성 합격으로 간주하지 않는다.

이전 단계 AT-01의 strict manifest·실제 collision checksum·reset 전 control 차단과 실행 중
완전 검증·tick-boundary swap은 구현됐고 실제 100cm payload same-PID 적용까지 확인했다.
다만 PIE checksum 변경 자동 재연결 표시는 확인 대기다. AT-03은 새 가상 도심에
Ground 390개와 `static_colliders=228`을 Bake하고 저장 정렬·실제 package 연석 등판·
높은 턱/Wall 차단 자동 경로를 확인했지만 실제 PIE 연석·벽 시험 전이므로 완료가 아니다.
기존 Landscape는 0 markers로 보존했다.
AT-04도 demo runtime smoke는 통과했으나 실제 PIE identity/동작/충돌과 최종 traffic
개체 수를 확인하지 않아 아직 통과하지 않았다. 8/28 이력인 C++ 14/14·전체 suite
20회 280/280(48.35s)·핵심 7종 각 20/20·선행 WebSocket 100/100·UE Editor/Game build·GeoTransform
Automation 1/1·same-PID map reload와 실제 pre-Hello state 차단·invalid Hello 1008·동일
sequence altered payload 2회 거부·valid recovery state sequence 5148 smoke는 통과했다.
양방향 Hello와 diagnostics HUD 자동 경로는 구현됐지만 실제 PIE와
Health HUD 시각 확인은 별도 gate다(코드와 자동 경로는 8/31 구현).
8/31 가상 도심의 저장 검증·dense route/물리 완주 선행 시험과 UE Automation 11/11,
Editor/Game 빌드 성공도 같은 구분을 적용한다. 최신 통합 QA 결과는 작업일지를 따른다.

## 7. 결정 입력 및 상태

| 결정 ID | 내용 | 결정 시점 | 결정하지 않을 때 영향 |
|---|---|---|---|
| DEC-F01 | **완료: Unreal Engine 5.6 계열 사용; Cesium 버전 고정은 Future** | UE 기존 결정, Cesium R1 제외 2026-08-31 | R1은 Cesium 플러그인에 의존하지 않음 |
| DEC-F02 | **완료: 현재 C++ 서버에 자체 차량 물리 구현** ([ADR-006](./decisions/ADR-006-custom-vehicle-physics.md)) | 2026-08-14 결정 | 구현량·검증 책임이 증가하므로 단계별 시험 실패 시 후속 일정 재검토 |
| DEC-F03 | **완료: R1 통신은 WebSocket binary + Protobuf** ([ADR-005](./decisions/ADR-005-realtime-transport-protocol.md)) | 2026-08-14 결정 | C++ JSON 입력 파서는 제거, UDP는 측정 후 재검토 |
| DEC-F04 | **선택 기능 구현:** 세단·경차·트럭·오토바이 profile | 2026-09-05 | 물리·충돌 크기는 차종별 적용. 실차 제원 동정과 이륜 균형 모델·차종별 실내는 미포함 |
| DEC-F05 | **방향 확정·blockout 생성**: 명목 240×200m, 현재 50cm valid 지면 239×200m의 약 633.1m 가상 도심 시험 루프 | 사용자 방향 승인 및 첫 구현 2026-08-31; 상세 배치는 조작감·성능에 따라 조정 가능 | 실제 지역/GIS 입력 대기는 해소; 실제 PIE·최종 LaneGraph 연결·시각 도로 정합은 검증 필요 |
| DEC-F06 | 사용할 건물·차량·보행자 에셋과 라이선스 | D8 이전 | 영상 품질 또는 배포 가능성 저하 |
| DEC-F07 | **완료·이력: 8월 20일 이후 주말 제외, AI 협업 2배 기준 적용** | 2026-08-19 결정 | 8월 27일 Core·8월 31일 확장 기준선은 미달성했으며 DEC-F10으로 대체 |
| DEC-F08 | **완료: Core RC 통과 후 운전 UX·카메라·디버그·replay·데모 기능 추가** | 2026-08-19 결정 | Must 회귀 시 해당 Should 확장만 제외하고 핵심 릴리스를 보호 |
| DEC-F09 | **완료: 공통 공개 좌표는 ROS 호환 FLU, Unreal FRU는 경계 adapter에서 변환** ([ADR-011](./decisions/ADR-011-canonical-coordinate-frames.md)) | 2026-08-19 결정, schema v2 이행 2026-08-21, C++/UE quaternion 계약 자동 검증 2026-08-28 | position·polar/axial vector·quaternion 계약은 자동 검증 완료; 로컬 지도 원점 metadata·SensorRig frame·실제 PIE 전에는 R1 좌표 통합 완료를 선언하지 않음. Cesium geodetic 통합만 ADR-013에 따라 Future로 분리 |
| DEC-F10 | **완료: 실제 잔여 범위와 AI 병렬 통합 한계를 반영한 일정 재기준선** | 2026-08-26 결정 | Core RC 9월 7일(9/8 Must 버퍼), Should 확장 9월 11일(9/14 위험 버퍼); 당시 완료 기준을 유지했으며 배경 범위만 8/31 DEC-F11로 대체 |
| DEC-F11 | **완료: 배경을 작은 가상 도심으로 전환** ([ADR-013](./decisions/ADR-013-small-virtual-city-course.md)) | 사용자 승인 2026-08-31 | Wall/Broad·실제 랜드마크·GIS/Cesium 통합 요구를 R1에서 제외하되 교통 개체 수·SensorRig·기록/재생·성능/안정성·AT 게이트는 유지 |

## 8. 변경 이력

3.6(2026-08-31): v7 고정 앞/뒤 per-tire 강성 60k/50k·v5/v6 명시적 migration·hard-stop 반력·
접지 상실 조향 표시 수정의 CTest 16/16(4.92초)·UE 26/26·Editor/Game·Health smoke·서버 적용
통과를 반영. 하중 비례 후보는 지형 회귀 실패로 미채택이고 v6·25/25는 이전 이력이다. 최고속 full-lock 과도 yaw·PIE와
후속 Must·일정은 완료 처리하거나 단축하지 않는다.

3.5(2026-08-31): 승인된 고정 목표 조향각·유한 rack·Ackermann 구조와 strict cfg v6 전환,
speed cap/legacy knob 제거를 반영. 이전 cap 기반 반경 기준은 이력으로 분리하고 새
ENU 실측·최종 CTest 16/16·UE Automation 25/25·Editor/Game 빌드·Health smoke 통과와
사용자 PIE 인수 대기를 구분한다.
NPC 등 잔여 Must와 일정은 유지한다.

3.4(2026-08-31): 차선·차량 신호 기반과 서버 시간 권한·UE fail-safe 표시를 반영.
NPC/보행자/기록재생/수동 인수 및 Core 완료일은 완료 처리하거나 단축하지 않는다.

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| 3.20 | 2026-09-07 | 9/4~5 실적 반영: 58lane·NPC10/보행8·근접 후진 회피·충돌 표현, 커브v3 저장 맵, 플레이어4차종·서버 물리/표시·차종 replay·SensorRig reset. C++34/34+후속4/4·UE84/84(79 경고 없음+5 예상 경고), 최신 PIE·패키징·성능·30분·촬영 미인수와 RGB Should/LiDAR Future를 명시 |
| 3.19 | 2026-09-03 | 충돌 피드백 8건: 실제 정점 dent·보행 몸통 비행/전도와 부분 래그돌·낮은 OBB·NPC 상호 충돌·사고 회피·깜빡이·실제 3차로/NPC10과 보호 신호. 수동 시각 인수와 완전한 전신/차량 동역학 경계는 유지 |
| 3.18 | 2026-09-03 | 사용자 승인 NPC 목적지 선택·동일 목적지 우회·같은 방향 차선 변경과 Signal City 46lane/16head를 MAP-005·TRA-002/003에 반영. 기존 맵/ground와 9/2 검증 이력 유지; Ego FSD·보행자 자유경로·최종 인수는 별도 |
| 3.17 | 2026-09-03 | REC-001/002의 실제 적용 입력 기반 Ego 물리 기록/재생과 NFR-001/012 측정 도구, NFR-011 자동 runner를 반영. CTest21/21·UE48/48·Python30/30·480tick 오차0 자동 통과와 수동/패키지 성능 미인수를 구분 |
| 3.16 | 2026-09-02 | 공개 기능·wire 계약을 유지한 C++/Unreal 책임 분리와 공통 Windows launcher를 반영. 비정상 NPC 각속도의 partial-tick 전파를 차단하고 CTest20/20·UE47/47·launcher 3 profiles·Python Proto1/1/helper10/10·엄격한 Signal City 2,280-state 격리 smoke를 통과했으며 PIE/성능 인수 상태는 변경하지 않음 |
| 3.15 | 2026-09-02 | 키보드 미세 조향·rear side-brake drift, Ego↔NPC/보행자 유한질량 반작용과 유지 상태, 6구역 국부 WPO dent 및 CTest20/20·UE46/46를 PHY-005/007·TRA-005/007에 반영. 실제 PIE 감각·NPC↔NPC·ragdoll·collision-shape 변형은 미완료 유지 |
| 3.14 | 2026-09-02 | frame-independent 조향·F3 overlay·full-body rollover contact·damage/deformation, Signal City 42lane/16head/2controller/NPC4/보행자8, SensorRig metadata와 F5 CSV/F6 ghost, CTest20/20·UE45/45·2,280-state smoke를 반영. REC-002/AT-07·PIE/성능/안정성은 미완료 유지 |
| 3.13 | 2026-09-02 | v1 PID9604 종료, 표준 Release 최신 재빌드, launcher parse 수정과 `signal_city_v2` PID23880·`127.0.0.1:9000` 실제 기동을 TRA-001에 반영. PIE·목표 traffic·성능 인수는 미완료 유지 |
| 3.12 | 2026-09-02 | 별도 `signal_city_v2`와 traffic v2의 42 lanes·8 heads·2 controllers, additive wire/UE fail-closed·3600초 countdown 검증, 확정 package 수치와 두 ValidateOnly·별도 최신 host Health/38초 traffic smoke를 MAP-001/005·TRA-001에 반영. 이 기록 시점에는 잔존 v1 PID 정리·표준 Release 재빌드·port9000 launcher, PIE·목표 traffic·성능 인수가 미완료였음 |
| 3.11 | 2026-09-02 | 보도 지지면 16→24cm 정합, 제한된 raster 허용치, 새 map/traffic checksum과 실제 package 연석 계측을 반영. C++18/18·UE35/35·빌드·두 ValidateOnly·격리 Health/traffic smoke 통과와 수동 PIE 인수 대기를 구분 |
| 3.10 | 2026-09-01 | 외부 에셋/Blueprint 없는 계기판·배기가스·procedural 차량 오디오를 UE-011/012에 반영. authoritative state 읽기·stale fail-closed·물리 비개입 경계와 focused Automation 5/5, 실제 PIE 시청각 인수 대기를 구분 |
| 3.9 | 2026-09-01 | 50cm virtual_city_v1 수치·checksum, swept-body along≤R signed near/far delta, full-footprint 214/215·along-length 210/215+의도적 gap, 실제 package 새 계측과 30/50m/s gate stress를 반영. 사용자 PIE 완료 상태는 올리지 않음 |
| 3.8 | 2026-09-01 | Ground 390개와 semantic Curb 이중 저작, support 검증·suspension 기반 연석 등판, 높은 턱/Wall fail-close와 새 collision/traffic checksum을 MAP-001/003·PHY-005/006/011·AT-03에 반영. C++18/18·UE29/29·빌드·두 ValidateOnly는 통과, 사용자 PIE는 미완료 |
| 3.7 | 2026-08-31 | lane NPC 1대 route/신호/장애물/lifecycle/세단 표시와 C++18/18·UE28/28·120초 통신 검증 반영. 다중 NPC·보행자·최고속 과도 yaw·수동 gate는 미완료 |
| 3.3 | 2026-08-31 | 사용자 주행 피드백에 따라 generic sedan 조향 envelope/응답, 마우스 orbit·줌·reset 및 자체 세단 메시·재질 구현. C++ 15/15·UE 15/15와 실제 입력/렌더 검증, 4종 카메라·실차 동정·최종 수동/성능 인수는 구분 |
| 3.2 | 2026-08-31 | 실제 L_VirtualCity/virtual_city_v1 blockout 생성·SIMGHF2/228 OBB·저장 검증과 dense route/실제 물리 완주 선행 시험, UE Automation 11/11·Editor/Game 빌드를 반영. nominal 240×200m와 valid 238×200m를 구분하고 QA route를 LaneGraph로 간주하지 않으며 수동·시각·성능 인수는 유지 |
| 3.1 | 2026-08-31 | 사용자 승인으로 배경을 작은 가상 도심 코스로 변경. 로컬 ENU 원점·모듈형 환경·작성 중심선 LaneGraph는 Must, 실제 지역·GIS/Cesium 통합은 R1 제외/Future로 분리. 기존 맵 보존과 미생성 목표 자산을 명시하고 교통·센서·기록/재생·인수 기준은 유지 |
| 3.0 | 2026-08-31 | WorldState Health와 cross-play EStop HUD, 별도 Unreal collision/material/marker 자동 QA 및 실제 v2+2 marker C++ 로드·WS 검증을 반영. 사용자 맵 수동 gate와 원래 Must는 유지 |
| 2.9 | 2026-08-28 | NET-004/006에 canonical negotiated Hello fingerprint, generation별 source/session/highest-sequence binding, semantic exact retransmission만 허용하는 동일 sequence 규칙과 250ms forced-close deadline을 반영. 최종 CTest 14/14·20회 280/280(48.35s), pre-Hello 차단·invalid 1008·altered-payload 2회 거부·recovery seq 5148 smoke 통과 |
| 2.8 | 2026-08-28 | NET-004/006에 application Hello readiness, connection-bound identity/order, log-safe validation, localhost-only bind를 반영하고 UE-003/006 HUD를 forward missing과 old 분리로 정정. 전체 CTest 20회 280/280 및 실제 WebSocket negative/positive smoke 통과 |
| 2.7 | 2026-08-28 | `SIMGHF2` material/friction end-to-end와 v1 호환·vehicle cfg v5, C++/UE GeoTransform/quaternion+Automation 1/1, 양방향 Hello capability/schema/map checksum gate, 경량 diagnostics HUD와 strict runtime cfg/CLI를 현재 구현 상태에 반영. 최종 C++ 14/14·UE Editor/Game build를 기록하되 tracked v1 package, authoritative SafeStop/Health HUD, PIE·static marker gate는 미완료 유지 |
| 2.6 | 2026-08-28 | 실제 100cm Bake(257,556 samples/256,542 cells, checksum `b8b0…`)와 PID 6692 same-PID apply를 MAP-001·PHY-011·NET-005·AT 상태에 반영하고, 남은 PIE·marker 수동 gate와 조건부 9/4 Core 병렬 선행 계획을 명시; 현재 완료 상태는 올리지 않음 |
| 2.5 | 2026-08-28 | Unreal 지면 측정과 C++ 차량 계산 책임을 분리한 `SIMGHF1` 100cm compact snapshot, legacy v1 sentinel 호환, strict O(1) heightfield provider·hot reload 회귀, UE Editor/Game build와 C++ 13/13·신규 100/100 반복을 MAP-001·PHY-011에 반영 |
| 2.4 | 2026-08-28 | 실제 19,208-triangle full-bounds package, MapPackage 250ms 감시·완전 검증·tick-boundary 원자 swap과 UE 새 PlaySession 자동 reset, generic sedan 조향 rate/cap·선회 반경 회귀, C++ 12/12·핵심 7종×20회·WebSocket 100/100·UE build·same-PID smoke를 MAP-001·PHY-005/011·NET-004/005·UE-003·AT-02에 반영 |
| 2.3 | 2026-08-28 | Ground Actor 전체 범위 Bake preflight·자동 Fit, triangle 예산 spacing 자동 상향, exporter/host ENU bbox/span 진단과 확대 bake 불변 Landscape 회귀를 MAP-001·PHY-011에 반영 |
| 2.2 | 2026-08-28 | terminal footprint coverage를 단일 missing corner·대각선 two-wheel 허용, 완전 axle/side·centre·0-ray fail-close로 정교화하고 tracked Landscape 600-tick nose-down 재현·통과 회귀를 PHY-011에 반영 |
| 2.1 | 2026-08-27 | 네 독립 suspension reaction 기반 4-corner reduced-order 자세, wheel-local tangent, finite-angle pitch/roll, UE roll 부호, partial support·centre coverage 경계와 자동/수동 검증 범위를 PHY-005·011에 반영 |
| 2.0 | 2026-08-27 | 정지 기반 W/S 전진·후진 전환, 동시 입력·stale-state gear interlock, 차속 연동 표시 휠과 저속 선회 횡력 우선 traction control을 PHY-005·011, UE-001·002와 AT-02에 반영 |
| 1.9 | 2026-08-27 | 차량 설정 v3의 저속 횡그립·속도별 rate-limited 조향·출력/drag와 조작감 회귀, 250ms soft SafeStop/1초 hard reconnect lease, background 파일 로그를 PHY-005·009~011과 NET-004에 반영 |
| 1.8 | 2026-08-26 | adaptive ground index·8m broad phase, `ASimCoreStaticCollider` ground/static 원자적 export, opt-in demo entity lifecycle/WorldState/UE 표시와 최종 C++ 11/11·핵심 4종×20회·UE Editor build·runtime smoke를 반영; WP-03 전체는 tracked `static_colliders=0`·수동 PIE gate 때문에 진행 중, 잔여 Core 38~55h |
| 1.7 | 2026-08-26 | strict 정적 collider loader, OBB-prism SAT·microstep·projection/impulse, VehiclePhysics XY/yaw 통합, NPC OBB·보행자 capsule 최소 API, CTest 11/11·UE Editor·runtime smoke 실적과 남은 WP-03 수동/runtime 경계를 반영 |
| 1.6 | 2026-08-26 | M2 자동 게이트 완료·WP-03 진행 중 상태, strict MapPackage manifest/실제 collision checksum, 첫 WorldState 일치 후 Reset/Control과 reset 전 일반 control 차단, UE 5.6 빌드, Core 9/7·Should 9/11 재기준선을 반영 |
| 1.5 | 2026-08-26 | 외부 SDK 없이 차량 설정 format v2, CG 기반 차축 위치, 경사 force/moment equilibrium 하중 이동, Ackermann·RWD 차동·타이어 implicit coupling과 20° 등판 회귀를 PHY-005·010·011에 반영 |
| 1.4 | 2026-08-26 | Unreal WorldStatic 충돌의 개발용 MapPackage bake 경로와 UE 5.6 Game·Editor 빌드를 MAP-001·PHY-006 구현 중 범위에 반영; 실제 Landscape PIE는 수동 검증 대기 |
| 1.3 | 2026-08-25 | 외부 차량 설정·checksum, MapPackage triangle ground provider, heave·노면 pitch/roll·경사 주행과 Windows CTest 9/9 반영; 새 지형 pose PIE 확인은 후속 |
| 1.2 | 2026-08-21 | ZMQ 기본 OFF·Python observer 동결과 GroundQuery·1D suspension 기반의 부분 완료·후속 범위를 반영 |
| 1.1 | 2026-08-21 | 좌회전·좌조향 양수 schema v2 exact gate와 C++ 회귀 완료, Unreal scalar 경계 소스 적용 및 Windows 검증 대기 상태 반영 |
| 1.0 | 2026-08-19 | ADR-011 FLU canonical 결정, 좌표·부호 schema와 Unreal adapter, 센서 frame metadata, AT-10 이행 기준 추가 |
| 0.9 | 2026-08-19 | SafeStop session/sequence/queue-age 검증, body Y-left 계약 정합화, 횡하중 이동 회귀 수정, Unreal 입력 coalescing·entity 선택 반영 |
| 0.8 | 2026-08-19 | 8월 27일 Core RC 이후 운전 HUD·4종 카메라·복구 퀵 액션·디버그 오버레이·replay 조작·데모 프리셋 확장 범위 확정 |
| 0.7 | 2026-08-19 | UE 5.6 command FIFO 누적 해결, event-loop service와 20Hz lease heartbeat 완료 기준 반영 |
| 0.6 | 2026-08-19 | Unreal 직접 연결·Envelope·binary transport 완료 상태와 저지연 60Hz 전달·제한 외삽·NFR-012 반영 |
| 0.5 | 2026-08-14 | C++ host의 binary Protobuf ControlCommand 수신과 WorldState broadcast 구현 상태 반영 |
| 0.4 | 2026-08-14 | R1 통신 방식을 WebSocket binary + Protobuf로 확정하고 JSON 제거 방향 반영 |
| 0.3 | 2026-08-14 | PHY-002와 DEC-F02를 자체 C++ 물리엔진 결정으로 변경하고 D1 구현 상태 반영 |
| 0.2 | 2026-08-14 | Chrono::Vehicle 스파이크 결정을 기록; 0.3에서 런타임 채택 철회 |
| 0.1 | 2026-08-14 | R1 수동운전과 R2/R3 자율주행 확장 기능을 최초 분리 |
