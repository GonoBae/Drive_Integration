# DriveIntegration Unreal project

Unreal Engine 5.6.1 C++ 프로젝트다. C++ SimCore가 차량 물리의 권한을 가지며 Unreal은 수동 입력과 외부 상태 표시를 담당한다.

## 가상 도심 코스

`/Game/VirtualCity/Maps/L_VirtualCity`에 기본 도형 도심 코스를 생성했다.
EditorStartupMap과 GameDefaultMap도 이 맵을 사용한다. 기존 `NewMap`은 직접 열어 회귀 시험할 수 있다.
약 633m 루프, 4° 경사, 북쪽 T자 분기·정차 공간, 건물 8개와 명시적 충돌체 228개다.
기본 지면·도로 59개, 보도 116개와 연석 top 215개, 총 390개 component를
`VirtualCityGround`로 측정한다. 연석은 semantic `Curb` marker 역할도 유지한다.
후속으로 방향성 lane 25개와 서버 제어 신호 head 3개의 기반을 연결했다.
후속 lane NPC 1대는 추가했지만 완성 아트·다중 NPC/보행자 AI 구현은 아니다.

저장소 루트에서 `.\scripts\run_virtual_city_server.ps1 -Background`로 전용 서버를
실행하고 이 맵을 Play한다. map-local `VirtualCityGameMode`가 전용 Pawn의
`virtual_city_v1` 경로를 선택한다. 기존 `NewMap`과 Landscape Pawn의 설정은 보존했다.
주행/Bake·재현 명령과 자동/수동 검증 구분은
[가상 도심 빠른 시작](../../docs/virtual_city_quickstart.md)을 따른다.

## 선택형 다중 교차로 signal city (9/2 추가)

`/Game/SignalCity/Maps/L_SignalCity`는 기존 기본 맵을 교체하지 않는 별도 blockout이다.
`SignalCityGameMode`와 map-local Pawn이 `map_packages/signal_city_v2` identity를 선택한다.
현재 생성물은 227 boxes, Ground124, static OBB49, 50cm `401×481` heightfield와
42 lanes·신호 head16(차량8·보행8)·controller2, NPC4대·보행자8명을 가지며 map·traffic
exact validation을 통과했다. map checksum은 `fnv1a64:7446108adad3e25b`, traffic
checksum은 `fnv1a64:dfcb5e4541d71adf`다.

저장소 루트에서 아래 launcher를 실행한 뒤 `L_SignalCity`를 직접 열어 Play한다.

```powershell
.\scripts\run_signal_city_server.ps1 -Background
```

기본 `L_VirtualCity` 서버와 같은 port 9000을 사용하므로 두 서버를 동시에 실행하지 않는다.
`signal_city_course` CTest 5그룹, 전체 CTest 20/20, UE Automation 47/47와
2,280-state WebSocket smoke의 SafeStop/reset은 통과했다. 서버 PID는 실행할 때마다
launcher가 port 9000 listener와 고유 로그를 확인하며 문서에는 일시 PID를 고정하지 않는다.
실제 PIE·60fps·30분 안정성 인수는 아직 후속이다.
생성·Bake·traffic export·수동 gate는
[signal city 빠른 시작](../../docs/signal_city_quickstart.md), controller/wire 계약은
[차선·신호 문서](../../docs/traffic_network_signals.md)를 따른다.

## NPC 1대 주행 (8/31 추가)

가상 도심 전용 서버 cfg는 20m 앞에 lane NPC 1대를 생성한다. 서버가 차선 추종·신호
정지/재출발·장애물 정지와 lifecycle을 담당하고 Unreal은 자체 세단/네 바퀴를 표시한다.
Ego처럼 NPC에도 타이어/서스펜션 동역학을 실행하는 단계는 아니며 ground 높이/yaw만 추종한다.
매 tick 최대50ms 예측, 100ms local stale/연결 종료/reset 시 표시 정리, NoCollision을 유지한다.
신규 actor/수신 lifecycle 회귀를 포함해 Unreal28/28·Editor/Game 빌드를 통과했다.
이 절은 8월 31일의 선행 구현 이력이다. 이후 `signal_city_v2`에서 NPC 4대·보행자 8명과
보행 신호까지 확장했지만 실제 PIE 인수는 남아 있다. 이번 변경에 Bake는 필요 없다.
[일괄 테스트와 한계](../../docs/npc_lane_following.md)를 따른다.

## 차선·신호 기반 (8/31 후속)

Unreal은 저장 도로의 실제 asphalt 충돌을 검사해 `traffic_network.json`을 최초
export하고, C++는 정합성·방향성 topology를 다시 검증해 같은 simulation clock으로
신호를 계산한다. `WorldState`의 차량·Health·신호는 동일한 sequence/map/play identity를
공유한다. Play 중 북쪽 T 교차로의 본선 두 head와 지선 한 head가 표시된다.
게임 전용 표시 actor는 `NoCollision`이며 저장 맵·Bake 결과를 변경하지 않는다.

신호는 30초 주기이며 연결 끊김·stale·SafeStop에는 적색으로 표시한다.
새 Play는 시간 0부터 시작하고 같은 Play의 소켓 재연결은 시간을 유지한다.
이 절의 `virtual_city_v1` 단일 교차로 이력과 별도로 `signal_city_v2`에는 NPC 차선 추종,
보행자 횡단과 차량·보행 신호 준수가 구현됐다. Ego 자동 신호 제동은 아직 구현하지 않았다.
[계약·검증·남은 작업](../../docs/traffic_network_signals.md)을 참고한다.

## 차량 외형·카메라 (8/31 후속)

기본 도형 차량을 `/Game/Vehicles/Sedan`의 자체 제작 세단 차체와 원형 타이어/합금 휠,
도장·유리·고무 등 10종 재질로 교체했다. 기존 wheel pivot·서버 자세·차속 연동 rolling은
유지하며 시각 메시는 물리 충돌을 하지 않는다. 외부 차량 SDK/유료 에셋을 추가하지 않았다.

Play 화면을 클릭하고 마우스를 움직이면 시점 회전, 휠은 줌, C는 뒤쪽 기본 시점 복귀다.
게임패드 오른쪽 스틱/클릭도 orbit/reset을 지원한다. `Shift+F1`은 PIE 마우스 캡처 해제다.
카메라는 서버 연결이 없어도 동작하고 차체 pitch/roll을 따라 수평이 흔들리지 않으며
벽에서는 SpringArm을 줄인다. 운전석·고정·자유 이동·replay 4종 모드 전체 구현은 아니다.

Editor 전용 `BuildSedanVisual` commandlet과 `VehicleVisual.QAViews`가 생성/검증을
담당한다. [조작감·외형·자동/수동 검증 상세](../../docs/vehicle_driving_refinement.md)를 따른다.
이번 변경에는 맵 Bake가 필요 없다. PowerShell `.ps1` 실행 정책 오류 시 빠른 시작의
절대 경로 `.exe --runtime-config ...` 명령을 사용하고 전역 정책을 바꾸지 않는다.

## 현재 최소 통합

- `SimCoreClientComponent`: binary WebSocket 연결 상태·socket generation·자동 재연결,
  양방향 Hello gate와 경량 diagnostics HUD, opt-in NPC·보행자 transient proxy 관리
- `SimCoreProtocol`: 루트 `protocol/vehicle.proto`의 schema-v2
  Hello/ControlCommand/WorldState/SimulationReset 호환 wire adapter
- `ExternalVehiclePawn`: W/S/A/D·Space 입력 전송과 ENU 상태 표시. 키보드 조향은
  `DeltaSeconds` 기반 상승·복귀를 사용하며 `F3` 디버그, `F5` 기록, `F6` 재생을 연결한다.
- `SimCoreDriveReplay`: fresh authoritative snapshot을 CSV로 저장하고 collision-free 세단
  ghost를 표시한다. 저장 경로는 `Saved/DriveReplays/last_drive.csv`다. command/event 기반
  결정적 physics re-simulation은 아니다.
- `SimCoreSensorRig`: `base_link` FLU 기준 front camera 10Hz·roof LiDAR 20Hz mount config와
  authoritative `SimulationTimeNs` metadata cadence만 제공한다. 실제 image/point cloud payload는
  아직 캡처하지 않는다.
- `SimCoreCoordinateFrames`: ENU/FLU에서 Unreal 표시 좌표로 변환한다. yaw/steering은
  handedness 경계에서 한 번 반전하지만 positive roll은 UE에서도 왼쪽이 올라가므로 다시
  반전하지 않는다. `angular_velocity_body`는 true RH-FLU vector이므로 compound attitude에서
  vector와 현재 pitch·roll로 navigation yaw·pitch·roll Euler rate를 역복원한 뒤 제한
  예측한다. scalar `yaw_rate`는 true body-Z component이며 gimbal fallback으로만 사용한다.
- `SimCorePresentation`: 최대 50ms 제한 외삽과 stale wheel 정지 정책
- `DriveIntegrationGameModeBase`: 외부 차량 Pawn을 기본 Pawn으로 사용
- `GroundCollisionExporter`·`SimCoreGroundSnapshot`: Editor의 WorldStatic 지면
  높이·ImpactNormal·유효 cell·surface material/friction과 명시적 `SimCore Static Collider` OBB를 MapPackage의
  `ground_heightfield.bin`·`static_colliders.csv`로 bake하고 실제 payload checksum의
  `manifest.cfg`를 마지막에 commit. C++는 이 immutable 측정값으로 차량 물리만 계산

## 로컬 저지연 동작

- 동일 frame의 입력 변화는 latest-wins로 합치고 deadzone/epsilon 적용 후 최대 30Hz로 `ControlCommand`를 전송한다.
- 입력을 유지하는 동안에는 20Hz heartbeat로 250ms command lease를 갱신한다.
- 연결마다 고유 session ID를 사용하며 C++는 sequence와 추정 queue age를 검증한다.
- PIE를 시작할 때마다 새 play session ID를 만든다. 연결 전 로컬 `manifest.cfg`와 실제
  collision payload checksum을 검증하고 client `Hello`를 보낸다. server `Hello`의
  source/build/schema/map checksum/capability와 authoritative checksum이 같은 경우에만
  `SimulationReset`과 첫 일반 `ControlCommand`를 보내 차량
  pose·simulation clock·control lease를 설정된 spawn으로 초기화한다.
- 같은 PIE의 transport 재연결은 같은 play session ID를 다시 보내므로 서버가 중복
  reset을 무시하고 새 socket으로 lease만 안전하게 넘긴다.
- 연결이 끊긴 사이 로컬 `manifest.cfg` checksum이 바뀌면 재연결 시 package를 다시
  검증하고 새 play session ID를 만든다. 서버의 새 authoritative checksum과 일치한 뒤
  `SimulationReset` handshake를 자동으로 다시 수행하므로 Bake 후 수동 서버 재시작이나
  Editor 재시작이 필요하지 않다.
- C++ `WorldState`가 현재 play session ID를 되돌려 보내며 Unreal은 로컬 PIE ID와
  다르거나 비어 있는 상태를 폐기한다. 따라서 새 PIE 접속 직후 도착한 이전 PIE 위치를
  한 프레임도 표시하지 않는다.
- 250ms command 공백에는 C++가 SafeStop을 적용하되 같은 session fresh command로 복구한다.
  1초 hard timeout에서 socket을 닫으면 기본 0.5초 후 새 session으로 자동 재연결한다.
- UE 5.6 WebSocket은 Windows event-loop service로 실행해 새 입력이 socket thread를 즉시 깨우며, 반복 명령 FIFO가 누적되지 않게 한다.
- Editor PIE의 background CPU throttling을 꺼 focus 변화로 heartbeat가 끊기지 않게 한다.
- 수신한 body velocity로 최대 50ms만 pose를 예측하고 그 이후에는 위치를 고정한다.
- C++ host는 Windows에서 1ms timer resolution을 요청해 60Hz state 간격의 jitter를 줄인다.
- `ControlledEntityId`로 Ego state를 선택하므로 WorldState에 NPC가 먼저 포함돼도 다른 엔티티를 표시하지 않는다.
- schema v1, checksum 없는 WorldState, 로컬 MapPackage와 다른 checksum을 받으면
  incompatible 상태로 전환하고 자동 재연결을 멈춘다. server/client 양방향 `Hello`는
  source/build/schema/map checksum과 빈 값·중복이 없는 필수 capability를 검증한다. host는
  승인된 socket generation에 source/session/highest sequence를 고정하고 client Hello 전
  WorldState broadcast를 차단한다. Hello와 checksum-valid reset 전의 ordinary state/control은
  적용하지 않으며 E-stop만 안전 예외다. 기본 server listener는 localhost 전용이다.
- 기본 4Hz 경량 HUD는 connection, hello, map, control, network state rate, sequence, local/server
  age, max gap, forward sequence missing, duplicate/out-of-order old, 짧은 checksum과
  controlled entity를 표시한다.
  여기서 `network state rate=50.0 Hz`는 차량 속도가 아니라 서버 `WorldState` 수신률이다.
  authoritative SafeStop/Health와 command age·reason·overrun도 표시하며 Unknown/Stale을 구분한다.
  별도 vehicle 행은 fresh 상태의 signed 종방향 속도를 km/h(m/s), gear, 실제 중앙 steering
  deg, yaw deg/s로 표시하고 freshness/handshake gate 실패에서는 `--`로 숨긴다.
- 2026-08-19 Python loopback probe에서 state 간격은 p95 17.08ms, 최대 17.20ms였고 command 전송 후 첫 speed 변화는 약 27ms였다.

C++의 차체는 네 독립 wheel contact와 body-up spring/damper reaction으로 tire load, heave,
pitch와 roll을 계산한다. fit한 wheel support plane은 reset·진단 전용이며 Unreal visual을
그 목표로 기울이지 않는다. suspension compression hard-stop도 `q={z,roll,pitch}`
unilateral constraint로 C++가 푼다. 각 tire contact force의 자세 moment는 C++에서
wheel별 contact normal의 local tangent에 hub velocity와 force를 함께 투영하고
`(contact_patch-CG)×F`로 계산한다. 중력·공력·driveline drag는 섞지 않는다. Unreal
Chaos는 Ego pose를 다시 계산하지 않는다.

schema-v2 좌표 경계와 직접 수동운전은 2026-08-25 Windows UE 5.6 PIE smoke test를
통과했다. 같은 날 서버에 추가한 MapPackage 지면 기반 차체 z·pitch·roll은 PIE에서
평면 정지 높이와 향후 경사 package의 자세 방향을 다시 확인해야 한다.

측정과 설계 근거는 [ADR-010](../../docs/decisions/ADR-010-low-latency-control-presentation.md), [D6 작업 로그](../../docs/worklogs/2026-08-19.md), [UE 5.6 WebSocket 입력 지연 누적 해결 사례](../../docs/troubleshooting/ue56-websocket-growing-input-delay.md)에 기록한다.

## Demo NPC·보행자 통합 fixture

서버의 평상시 동작은 demo entity를 생성하지 않는다. 정적/동적 WP-03 통합 경로를
시험할 때만 기존 서버 명령 끝에 `--demo-entities`를 붙인다. host는 지면이 존재하는
고정 ENU 위치에 NPC OBB 1대와 보행자 capsule 1명을 만들고 move/reset lifecycle과 Ego
collision snapshot을 소유한다. 두 entity의 identity·kind·shape·pose·velocity는 같은
`WorldState`로 전송되고 Unreal은 collision이 꺼진 transient mesh로 표시한다.

이 옵션은 protocol·lifecycle·collision·표시를 검증하는 fixture다. LaneGraph 추종,
신호 준수, R1 목표인 NPC 3~4대·보행자 6~8명의 최종 traffic 기능은 아니다.

저장소 루트의 Windows PowerShell에서 다음처럼 실행한다.

```powershell
.\scripts\run_landscape_server.ps1 -DemoEntities
```

## 2026-08-26 자동 검증 기록

- CMake Release `simcore_publisher` 최종 build와 CTest **11/11**이 통과했다.
- 선정한 핵심 4개 test executable을 각각 20회 반복해 전부 통과했다.
- UE 5.6 Editor target 빌드 산출물 `UnrealEditor-DriveIntegration.dll`은 445,952 bytes,
  2026-08-26 23:56:32, SHA-256
  `530FC0AC516CC86265AE182410B0F2E0DF7EE5F3CE656CD5F8E248989D68DCF9`다.
- `landscape_local_v1` smoke에서 checksum `fnv1a64:239e5e39f3706396`,
  `ground_triangles=2220`, `ground_cells=1872`, `ground_global=0`,
  `ground_max_candidates=8`, `static_colliders=0`, demo `runtime_count=2`와 WebSocket
  `:9000` 시작을 확인했다.
- smoke 종료 뒤 `simcore_publisher` process와 9000/8765/8000 listener가 없음을 확인했다.

자동 검증은 8월 26일 작업분으로 자정을 넘어 마감했다. 현재 tracked Landscape의
`static_colliders=0`은 marker를 아직 배치·bake하지 않았다는 뜻이며, 실제 PIE 벽·커브와
demo entity identity/표시/충돌 성공을 뜻하지 않는다.

## 2026-08-27~28 단계별 변경 검증 기록(최종 통합 전 이력)

- UE roll 이중 반전, RH FLU pitch angular velocity, 네 suspension reaction 기반 차체 자세,
  ordinary pitch/roll hard clamp 제거와 Ground Actor bounds fit source를 반영했다.
- legacy attitude K/C 설정은 모두 0이어야 validation을 통과하며 solver 자세 moment에는
  사용하지 않는다.
- hard-stop position은 0.15m·2° trust-region 반복과 잔차 pure-Z fallback, velocity는
  32회 교대 sweep accumulated-impulse LCP를 사용한다. correction→ground re-query는 최대
  4 outer pass이며 최종 clearance가 activation slop 안인 corner만 velocity LCP에 넣는다.
- finite-angle generalized axes/inertia, `E-dot*qdot`, gyroscopic term을 포함하지만 full 6DoF가
  아닌 planar yaw + sprung-body heave/roll/pitch reduced model이다. massless hub·1D suspension,
  unsprung/tire carcass/airborne dynamics 부재는 현재 범위다.
- authored-surface coverage에서 단일 missing corner와 대각선 two-wheel support는 계속
  계산한다. 완전 front/rear axle 또는 left/right side의 coverage가 사라지면 ground-bound
  reduced model이 남은 spring으로 map 끝에서 tip하지 않도록 이전 지원 step으로
  fail-close한다. centre query miss와 0 wheel ray도 기존처럼 fail-close한다. tracked
  Landscape full-throttle 600-tick 회귀는 수정 전 최대 pitch 151.7°·최소 z -0.182m 실패를
  재현했고 수정 후 통과했다.
- 이 단계 당시 C++ full Release build와 CTest는 **12/12**, 차량 물리·차량 설정·
  MapPackage hot reload·ground query·정적/동적 충돌·host 핵심 7종 반복은 각각
  **20/20**, WebSocket 회귀는 **100/100** 통과했다.
- 같은 날 exporter Bake에 Ground Actor 전체 colliding-component bounds preflight·자동 Fit,
  20k 예산의 spacing 자동 상향과 실제 ENU bbox 결과를 추가했다. UE 5.6
  `DriveIntegrationEditor` build도 `UBT Result: Succeeded`로 통과했다.
- 실제 full-bounds Bake는 spacing `507cm`, `19,208` triangles, ENU span 약
  `495.88×495.88×22.02m`로 완료했다. 같은 서버 PID에서 새 checksum을 자동 적용하는
  runtime smoke와 UE 5.6 Editor 재빌드는 통과했다. 실제 PIE 조향·경사·자동 재연결 감각은
  사용자 수동 검증 대기 상태다.

## 2026-08-28 최종 통합 검증 상태

- `SIMGHF2` surface material ID/terrain friction multiplier writer와 C++ v1/v2 loader,
  vehicle config v5 surface scale까지 자동 end-to-end 경로가 통과했다. 실제 tracked package는
  아래에 기록한 v1 Bake이므로 Physical Material/tag를 담은 v2 재Bake와 PIE 체감은 후속이다.
- C++/UE ENU·FLU↔Unreal FRU position, polar/axial vector와 quaternion basis 계약을 구현했고
  headless `DriveIntegration.Coordinates.GeoTransformContract` Automation **1/1**이 `Success`로
  끝났다. SensorRig frame·Landscape PIE 시각 확인은 후속이다. 8/31 가상 도심 전환으로
  실제 Cesium origin 연동은 R1에서 제외하고 향후 지도 확장으로 연기했다.
- 양방향 Hello gate, connection-bound identity/order·transport readiness·localhost-only bind,
  위 경량 diagnostics HUD와 strict server runtime cfg&lt;CLI를 통합했다.
- Windows C++ Release 전체 build와 CTest **14/14**, 전체 suite 20회 **280/280**, UE 5.6
  Editor/Game build가 통과했다. invalid 첫 Hello의 1008 close와 valid
  Hello→Reset→Control→WorldState 실제 WebSocket smoke도 성공했다.
- 실제 PIE 경사·요철·차체 자세·Sculpt 재Bake 재연결, v2 material 체감과
  `static_colliders>0` 벽·커브·동적 entity 충돌은 아직 수동 gate다.

## 에디터 설정

기존 `SimBlank` 템플릿 맵은 자체 `BP_SimGameMode`를 World Settings에서 지정할 수 있다. 이 경우 다음 중 하나를 선택해야 C++ 외부 차량 Pawn이 생성된다.

1. World Settings의 GameMode Override를 `DriveIntegrationGameModeBase`로 변경한다.
2. 기존 `BP_SimGameMode`의 Default Pawn Class를 `ExternalVehiclePawn`으로 변경한다.

실행 전에 C++ host를 먼저 시작한다. 기본 주소는 `ws://127.0.0.1:9000`이다.
인자 없이 실행한 host는 `cpp/host/config/vehicle_sedan.cfg`와
`map_packages/wall_broad_v1/manifest.cfg`를 검증한 뒤 그 manifest가 지정한
`ground_surface.csv`를 기본으로 로드한다.

## Landscape와 정적 충돌 지면 연결

`ExternalVehiclePawn`은 C++ 상태를 표시하는 Pawn이므로 Unreal collision response가
차량 pose를 직접 변경하지 않는다. 대신 Editor에서 실제 `WorldStatic` 충돌을 한 번
스캔해 C++가 읽는 MapPackage 지면으로 bake한다.

새 bake는 `SIMGHF2` cell에 stable 재질 ID와 terrain friction multiplier를 함께 기록한다.
각 Landscape layer/mesh의 Physical Material을 Project Settings의 Physical Surface에 연결하고,
exporter의 Asphalt/Low Friction/Rough Physical Surface 지정과 일치시킨다. actor 또는 component
tag `SimCore.Surface.Default`, `.Asphalt`, `.LowFriction`, `.Rough`는 Physical Material보다
우선하며, 같은 대상에 둘 이상의 재질 tag가 있으면 bake가 fail-closed한다. 경계 cell은 네
corner 중 friction이 가장 낮은 측정을 선택해 서버가 존재하지 않는 접지력을 만들지 않는다.

1. Landscape와 레벨을 저장하고 Play를 정지한다.
2. Place Actors의 All Classes에서 `SimCore Ground Collision Exporter`를 찾아 레벨에 둔다.
3. `Ground Actor`에 Landscape를 지정한다. 지정한 actor는 하나의 연속 surface로 취급해
   급경사 cell을 corner 높이 차만으로 삭제하지 않는다.
   현재 exporter는 지정한 단일 actor의 component bounds만 모은다. World Partition에서 지형이
   여러 `LandscapeStreamingProxy` actor로 분리되어 있다면 필요한 전체 proxy를 하나로 자동
   집계하지 않으므로, 통합 Landscape actor를 지정하거나 sampling 범위를 수동으로 확인한다.
4. 필요하면 `Fit Sampling Bounds To Ground Actor`를 눌러 청록색 box를 미리 본다. Bake
   자체도 actor 전체 colliding-component bounds와 기본 100cm padding을 검사해 부족하면
   자동 Fit한다.
5. 차량 출발점과 시험할 전체 Landscape가 sampling box 안에 있고 `Map Origin World Cm`을
   포함하는지 확인한다. Ground Actor 없이 수동 범위를 쓸 때만
   `Center Sampling On Map Origin`을 먼저 사용한다.
6. 급경사·요철은 작은 spacing이 유리하다. 새 `Heightfield Sample Spacing Cm` 기본값은
   100cm이며 `Max Heightfield Sample Count` 기본/상한은 2,000,000이다. 예산을 넘으면
   exporter가 해상도를 몰래 낮추지 않고 명시적으로 거부하므로, 의도한 경우에만 spacing을
   조정한다. 약 506m full bounds는 100cm에서 `507×507=257,049` samples라 제한 안이다.
7. 벽·커브·barrier가 있으면 Place Actors의 All Classes에서 `SimCore Static Collider`를
   하나씩 배치한다. 청록색 ground sampling box와 별개이며, marker box를 실제 장애물에
   맞게 이동·Yaw 회전·크기 조절한다. Pitch/Roll은 지원하지 않아 bake 시 거부된다.
8. 각 marker의 `Collider Id`에 `wall_main_01`처럼 128자 이하의 고유 ASCII ID를 넣고
   `Semantic`을 Wall/Curb/Barrier 중 하나로 정한다. 필요하면 Friction과 Restitution을
   조정한다. `Export Enabled`를 끄면 레벨에는 남기되 package에서는 제외된다.
9. Details에서 `Bake Ground + Static Collision To MapPackage`를 누르고
   `Ground Actor bounds coverage: OK`, `Exported ENU bounds`, sample/valid/drivable cell 수,
   `Surface cells: default/asphalt/low-friction/rough` 분류 수,
   `Map origin coverage: OK`, `Height discontinuity filter: disabled for explicit Ground
   Actor`, `discontinuous=0`, `explicit static OBB colliders` 개수를 확인한다.
10. 서버가 이미 실행 중이면 그대로 둔다. 새 snapshot 검증과 tick-boundary 적용 로그를
    확인한다. 서버가 꺼져 있을 때만 저장소 루트에서 아래처럼 실행한다.

```powershell
.\scripts\run_landscape_server.ps1
```

기본 출력은 `map_packages/landscape_local_v1/ground_surface.csv`(legacy host용
header-only sentinel), `ground_heightfield.bin`, `static_colliders.csv`, `manifest.cfg`다.
marker가 하나도 없으면 strict header만 있는
`static_colliders.csv`를 정상 출력하므로 이전 bake의 stale 벽이 남지 않는다. exporter는
세 payload를 모두 임시 파일로 staging한 뒤 교체하고, 실제 bytes checksum이 든 manifest를
마지막에 commit해 부분 저장을 다음 시작에서 fail-closed한다. UE world 원점은
SimCore ENU 원점으로 취급하며 `north=UE X`, `east=UE Y`, `up=UE Z`, `100cm=1m`로
변환한다. marker의 UE +Yaw도 North에서 East로 도는 clockwise-positive
`heading_rad`가 된다. 다른 원점을 사용할 때만 `Map Origin World Cm`을 바꾼다.

현 exporter는 위에서 아래로 보이는 연속 Landscape·정적 메시의 요철과 경사를 100cm
기본 lattice의 높이·ImpactNormal·유효 cell·Physical Material/재질 tag를 측정해
`ground_heightfield.bin` (`SIMGHF2`)으로 만들고,
editor-only `SimCore Static Collider` box는 strict 12-field OBB 행으로 만든다. marker
자체의 Unreal collision은 꺼져 있으며 C++ SimCore만
Ego 충돌 response를 결정한다. 따라서 시각 wall mesh에 Unreal collision이 있더라도
marker box를 맞추고 다시 bake해야 한다. GroundQuery는 새 package에서 O(1) heightfield
lattice를, 기존 package에서 adaptive triangle grid를 사용하며,
CollisionWorld는 8m deterministic broad phase를 사용한다. opt-in NPC/보행자 lifecycle·
`WorldState`·표시 경로도 구현됐지만 현재 package에 실제 marker가 없고 PIE 정적/동적
충돌을 확인하지 않았으므로 WP-03 전체는 진행 중이다. 서버는 `manifest.cfg`를 250ms마다
감시하고 완전히 검증한 새 snapshot만 다음 60Hz tick 경계에서 원자적으로 교체한다.
작성 중이거나 checksum이 잘못된 후보는 무시하고 기존 ground/collision을 유지한다.
Sculpt, marker, collision을 수정한 뒤 다시 Bake하면 서버가 차량·clock·control lifecycle을
reset하고 기존 WebSocket을 닫으며, Unreal은 새 checksum과 play session으로 자동
재연결한다. 잠깐의 연결 끊김은 이 안전 교체 절차의 정상 동작이다.

sampling 범위 끝이나 누락 cell에서는 ground query의 authored-surface coverage를 정상
suspension contact 이탈과 구분한다. 단일 missing corner와 대각선으로 마주 보는 two-wheel
coverage는 partial support로 계속 계산한다. 완전 front/rear axle 또는 left/right side가
coverage를 잃거나, centre coverage가 사라지거나, wheel ray가 모두 0개이면 서버는 step
시작의 이전 지원 pose로 되돌리고 속도를 0으로 만들어 reduced model의 nose-down과 무한
낙하를 막는다. 이는 물리 벽이 아니라 유한 MapPackage의 terminal footprint 안전 경계다.
PIE를 정지하고 다시 Play하면
`SimulationReset`이 C++ 차량 pose와 simulation clock을 원점 spawn으로 되돌리므로 서버를
재시작할 필요가 없다. 이전 socket session은 영구 폐기해 늦게 도착한 command가 새 주행을
다시 움직이지 못하게 한다. 단, process-lifetime E-stop latch는 PIE reset으로 해제하지
않으므로 E-stop 이후에는 서버를 재시작해야 한다. MapPackage 변경은 위 hot reload 경로로
처리하므로 별도 재시작 대상이 아니다.

현재 tracked package는 v2 material/friction 도입 전 실제 Bake한 `SIMGHF1`이다.
`507×508`, 257,556 samples/256,542 cells, 100cm step, checksum
`fnv1a64:b8b0a6ccd89614de`이며 동일 PID hot reload를 통과했다. `SIMGHF1`은 default
material·1.0 multiplier로 계속 호환한다. 이 범위 밖에서 차량이 마지막 지원 pose에
정지하는 것은 차체 자세 clamp가 아니라 유한 MapPackage coverage 안전 경계다.

현재 bake 범위 밖도 주행하려면 Ground Actor를 지정해 다시 Bake한다. 새 Bake 뒤 서버
시작 로그에는 기존 bake의 `ground_format=packed_heightfield_v1` 또는 새 재질 bake의
`ground_format=packed_heightfield_v2`와 sample/cell 수가 표시된다. 실행 중 서버의
`[MapReload] verified candidate`·`[MapReload] applied at tick boundary` 로그 또는 다음 시작
로그의 `exported_ground_bbox_enu_m`·`exported_span_m`이 성공 창과 일치해야 한다. 같은
내용을 다시 Bake해 checksum이 바뀌지 않으면 reload가 생기지 않는 것이 정상이다.
급경사 접촉 상실의 진단과 수정 근거는
[해결 사례](../../docs/troubleshooting/ue-landscape-steep-grade-contact-loss.md)에 기록한다.
Bake 뒤 checksum 변경·자동 재연결의 진단은
[MapPackage 자동 재로딩 해결 사례](../../docs/troubleshooting/ue-map-package-hot-reload-after-bake.md)를
따른다.

차량이 Landscape를 무시하고 평평한 `Z=0` 지면을 달리면 서버가 기본
`wall_broad_v1` package로 실행된 것이다. 서버 시작 로그의 `[Map] package=...`가
`landscape_local_v1`인지 확인한다.

## 조작

### 안전 상태 HUD (2026-08-31)

기존 경량 diagnostics HUD에 서버 `WorldState.health`의 안전 상태와 reason,
마지막 승인 command age, tick overrun 누적 수가 추가됐다. local state age는 Unreal이
마지막 상태를 받은 뒤 지난 시간이며, command age는 서버가 마지막 입력을 승인한 뒤
지난 시간이므로 서로 다른 값이다.

- `awaiting_reset` / `awaiting_control`: 초기화 또는 첫 입력 대기.
- `active`: 서버가 정상 control을 적용 중.
- `safe_stop`: soft timeout으로 안전 제동 중. fresh 입력으로 같은 연결에서 복구 가능.
- `reconnect_required`: hard timeout으로 lease 폐기. socket이 먼저 닫힐 수 있으므로
  화면에는 연결 끊김/재연결 대기가 먼저 보일 수 있다.
- `estop_latched`: 비상 정지 latch. 기존 정책대로 서버 재시작 전 해제되지 않는다.
  이전 PIE에서 걸린 전역 EStop은 현재 연결·Hello·map·순번을 검증한 뒤 `(global)`로
  표시한다. `healthLocal`은 이 진단을 받은 뒤 지난 시간이며 다른 PIE의 차량 pose를
  가져오는 동작은 아니다.
- Health 누락·알 수 없는 값·100ms 초과 stale·연결/handshake 실패에서는 Active로
  표시하지 않는다. 구 서버와 연결해 Health가 없으면 Unknown이 정상이다.

최신 Editor build에서 기존 `NewMap`을 Play해 표시를 확인한다. 자동 테스트는 실제
운전 감각이나 HUD 가독성을 대신하지 않는다. 자세한 시험 결과와 남은 수동 항목은
[8/31 작업일지](../../docs/worklogs/2026-08-31.md)를 따른다.

### 차량 계기값과 step-turn 단위 확인 (2026-09-01)

HUD의 `vehicle speed=... km/h (... m/s)`는 서버 차량 상태의 signed body-X 종방향
속도다. ENU CG 궤적 속도와는 다를 수 있다. `gear`는 D/N/R, `steering`은 정규화
키 입력이나 개별 앞바퀴 각도가 아닌 실제 중앙 rack 각도, `yaw`는 서버 yaw rate다.
반면 `network state rate=... Hz`는 상태 수신률이며 속도계가 아니다.

단위상 **50km/h는 13.89m/s**, 차량 설정의 **50m/s는 180km/h**다. 실제 solver가
public throttle로 가속한 뒤 full throttle/full-lock을 유지한 step-turn probe에서
50km/h는 약 0.34초에 중앙 35° 전타, 약 2.4초에 heading 90°에 도달했다. 50m/s는
전타 시간은 같고 heading 90°까지 약 7초였다. 여기서 90°는 rack이 아니라 실제 ENU
진행 방향 변화이며, pose·속도·yaw·brake·steering 상태를 주입하지 않았다.

50km/h에서 35° 기하 반경 4.13m를 정속 유지하려면 약 4.76g가 필요하지만 현재 계측
횡한계는 약 0.9975g이고 앞축이 이미 마찰 한도에 도달한다. 따라서 작은 코너는 먼저
감속한다. 현 모델의 거친 진입 가이드는 반경 8m 약 33km/h, 5m 약 26km/h이며
노면·전이·사용자 감각에 대한 보장은 아니다. 이 HUD·step-turn 작업 자체는 진단을
개선했으며 production physics는 바꾸지 않았다. 같은 날 이후의 연석 등판 물리 변경과
구분한다. CTest 18/18, Editor/Game Development 빌드와
UE Automation 29/29가 성공했다. 상세 수치와 한계는
[9/1 작업일지](../../docs/worklogs/2026-09-01.md)를 따른다.

### Ground support 기반 연석 등판 (2026-09-01)

이전 연석은 static OBB로만 Bake되어 차체를 막았고 보도 support가 없어서, 속도가 있어도
휠·서스펜션이 올라갈 수 없었다. 현재는 연석 top과 보도를 50cm Ground에 포함하고, 서버가
semantic `Curb`의 current→predicted swept along 구간을 `R` 이하 간격으로 검사해 near(+1R)·
far(+3R) signed delta와 collider 높이 계약이 맞는 경우에만 해당 step의 planar body
collision에서 제외한다. authored marker 중심 full-footprint 215곳 중 도로/보도
연석 중심 214곳이 eligible하다. 전체 along-length QA는 210/215 collider 전 길이와 북쪽
T-opening 네 끝단·`Curb_BayEnd`의 의도적 gap을 확인했다. 휠 ray와 네 suspension corner가 앞축→뒤축·차체 상승을 계산하며
차체 Z·속도·충격량을 직접 주입하지 않는다. `Wall`·`Barrier`와 높은 턱은 계속 막힌다.

현재 map/traffic checksum은 `fnv1a64:942842ea8d76b7a2` /
`fnv1a64:32f81819cb6b2b0f`이다. 실제 package 연석 회귀, CTest18/18, UE35/35,
Game/Editor Development와 맵/traffic `ValidateOnly`는 통과했다. 사용자 PIE에서 진입
속도·각도별 차체/휠 상승과 벽·Barrier 비관통은 수동 인수 대기다.
[진단과 재Bake 절차](../../docs/troubleshooting/virtual-city-curb-climb-ground-support.md)를 따른다.

### 차량 입력

- `W`: Drive 가속. 후진 중에는 먼저 제동하고 거의 정지한 뒤 Drive로 전환한다.
- `S`: 전진 중 service brake. 계속 누르면 거의 정지한 뒤 Reverse로 전환해 후진한다.
- `W`+`S`: 구동 없이 service brake. 상태가 없거나 stale이면 방향 전환도 하지 않는다.
- `A` / `D`: steering. digital command는 `DeltaSeconds` 기반 rate와 중앙 부근 제곱
  응답으로 상승·복귀한다. 짧은 탭은 작은 각도, 계속 누르면 full lock이다.
- `Space`: rear-axle side brake; steering과 함께 rear grip을 풀어 drift 진입
- `F3`: presentation-only 차량 물리 디버그 오버레이 on/off
- `F5`: 권한 snapshot CSV 기록 시작/종료
- `F6`: 마지막 CSV의 collision-free 시각 ghost 재생 시작/종료

표시 휠은 접촉 바퀴의 물리 각속도를 사용하되 `signed speed / tire radius`의 ±3%
범위로 제한한다. 비접촉 바퀴는 차축 평균에서 제외하고 정지·stale 상태에서는 0으로
고정하므로 출발 순간의 slip이 화면에서 헛바퀴 버그처럼 보이지 않는다.

generic sedan의 v7 조향 목표는 속도와 무관하게 `입력 × 중앙 최대각 35°`이며,
증가 `1.80rad/s`, 복귀 `2.20rad/s`로 움직인다. 앞바퀴는 개별 Ackermann 각도로 표시한다.
이전 speed-aware 조향 cap은 제거했다. 같은 입력에서 각도는 같아도 타이어의 마찰과
slip 때문에 실제 선회 반경은 속도에 따라 달라질 수 있다.
접지가 끊겼을 때도 최신 조향각은 계속 표시하며 바퀴 중심만 마지막 위치를 유지한다.
실제 Pawn Tick의 정지/90/180km/h·평지/경사·접지 상실 자동 시험으로 검증했다.

PIE에서는 정지·저속부터 좌우 입력/복귀와 안쪽 바퀴의 더 큰 각도를 확인하고,
30~50km/h에서는 짧은 입력부터 시험한다. A/D를 약 0.35초 이상 유지하면 중앙각이
최대에 도달하므로 고속에서 계속 누르는 것은 완만한 코너가 아닌 급조향 시험이다.
고속 full-lock의 무미끄럼을 합격조건으로 삼지 않는다. 이번 서버 조향 수정에는
지면 Bake가 필요 없다. [검증 기준과 한계](../../docs/vehicle_driving_refinement.md)를 참고한다.
