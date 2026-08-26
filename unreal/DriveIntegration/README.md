# DriveIntegration Unreal project

Unreal Engine 5.6.1 C++ 프로젝트다. C++ SimCore가 차량 물리의 권한을 가지며 Unreal은 수동 입력과 외부 상태 표시를 담당한다.

## 현재 최소 통합

- `SimCoreClientComponent`: binary WebSocket 연결 상태·socket generation·자동 재연결과
  opt-in NPC·보행자 transient proxy 생성·갱신·제거 관리
- `SimCoreProtocol`: 루트 `protocol/vehicle.proto`의 schema-v2
  ControlCommand/WorldState/SimulationReset 호환 wire adapter
- `ExternalVehiclePawn`: W/S/A/D·Space 입력 전송과 ENU 상태 표시
- `SimCoreCoordinateFrames`: ENU/FLU에서 Unreal 표시 좌표로 변환한다. yaw/steering은
  handedness 경계에서 한 번 반전하지만 positive roll은 UE에서도 왼쪽이 올라가므로 다시
  반전하지 않는다. `angular_velocity_body`는 true RH-FLU vector이므로 compound attitude에서
  vector와 현재 pitch·roll로 navigation yaw·pitch·roll Euler rate를 역복원한 뒤 제한
  예측한다. scalar `yaw_rate`는 true body-Z component이며 gimbal fallback으로만 사용한다.
- `SimCorePresentation`: 최대 50ms 제한 외삽과 stale wheel 정지 정책
- `DriveIntegrationGameModeBase`: 외부 차량 Pawn을 기본 Pawn으로 사용
- `GroundCollisionExporter`: Editor의 WorldStatic 지면과 명시적 `SimCore Static
  Collider` OBB를 MapPackage의 `ground_surface.csv`·`static_colliders.csv`로 bake하고
  실제 payload checksum의 `manifest.cfg`를 마지막에 commit

## 로컬 저지연 동작

- 동일 frame의 입력 변화는 latest-wins로 합치고 deadzone/epsilon 적용 후 최대 30Hz로 `ControlCommand`를 전송한다.
- 입력을 유지하는 동안에는 20Hz heartbeat로 250ms command lease를 갱신한다.
- 연결마다 고유 session ID를 사용하며 C++는 sequence와 추정 queue age를 검증한다.
- PIE를 시작할 때마다 새 play session ID를 만든다. 연결 전 로컬 `manifest.cfg`와 실제
  collision payload checksum을 검증하고, 연결 후 첫 C++ `WorldState`의 authoritative
  checksum이 같은 경우에만 `SimulationReset`과 첫 일반 `ControlCommand`를 보내 차량
  pose·simulation clock·control lease를 설정된 spawn으로 초기화한다.
- 같은 PIE의 transport 재연결은 같은 play session ID를 다시 보내므로 서버가 중복
  reset을 무시하고 새 socket으로 lease만 안전하게 넘긴다.
- C++ `WorldState`가 현재 play session ID를 되돌려 보내며 Unreal은 로컬 PIE ID와
  다르거나 비어 있는 상태를 폐기한다. 따라서 새 PIE 접속 직후 도착한 이전 PIE 위치를
  한 프레임도 표시하지 않는다.
- 250ms timeout으로 C++가 기존 socket을 닫으면 기본 0.5초 후 새 session으로 자동 재연결한다.
- UE 5.6 WebSocket은 Windows event-loop service로 실행해 새 입력이 socket thread를 즉시 깨우며, 반복 명령 FIFO가 누적되지 않게 한다.
- Editor PIE의 background CPU throttling을 꺼 focus 변화로 heartbeat가 끊기지 않게 한다.
- 수신한 body velocity로 최대 50ms만 pose를 예측하고 그 이후에는 위치를 고정한다.
- C++ host는 Windows에서 1ms timer resolution을 요청해 60Hz state 간격의 jitter를 줄인다.
- `ControlledEntityId`로 Ego state를 선택하므로 WorldState에 NPC가 먼저 포함돼도 다른 엔티티를 표시하지 않는다.
- schema v1, checksum 없는 WorldState, 로컬 MapPackage와 다른 checksum을 받으면
  incompatible 상태로 전환하고 자동 재연결을 멈춘다. checksum-valid reset 전의 일반
  control은 서버도 거부하며 E-stop만 안전 예외다. 명시적 `Hello` capability handshake는
  아직 구현 전이다.
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

## 2026-08-27 최신 변경 검증 상태

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
- wheel ray 1~3개는 partial support로 처리하고 centre query miss 또는 0 wheel ray만
  fail-closed한다. deep one-side, symmetric four-wheel, inactive hard-stop travel과 compound
  body-Z yaw 계약도 C++ 회귀에 포함했다.
- 최신 C++ full Release build와 CTest는 **11/11**, `vehicle_physics_tests`와
  `vehicle_config_tests` 반복은 각각 20/20 통과했다.
- 최신 UE 5.6 `DriveIntegrationEditor` build도 `UBT Result: Succeeded`로 통과했다.
- 최종 문서 감사 시 background server smoke도 성공했다. PID `17604`가
  `0.0.0.0:9000 LISTENING`, `127.0.0.1:9000` 연결 가능 상태였고 config checksum은
  `fnv1a64:2759c831f3bc939a`, MapPackage checksum은 `fnv1a64:bc3ae81aa738d339`이다.
  로드 결과는 `ground_triangles=2220`, `ground_cells=1872`, `ground_global=0`,
  `ground_max_candidates=8`, `static_colliders=0`이다. 시작 확인 때 stderr는 0 bytes였고
  연결 프로브 종료 뒤에는 정상 close 진단 한 줄만 추가됐다. PID는
  지속 상태를 보장하는 값이 아니므로 실제 테스트 전 listener를 확인한다.
- `Fit Sampling Bounds To Ground Actor` 적용·재-bake와 실제 PIE 주행은 사용자 수동 검증
  대기 상태다. 현재 smoke는 기존 tracked package 검증이며 fit 적용 후 새 package 검증을
  뜻하지 않는다. 위 8월 26일 DLL·runtime 결과도 당시 build 기록으로 보존한다.

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

1. Landscape와 레벨을 저장하고 Play를 정지한다.
2. Place Actors의 All Classes에서 `SimCore Ground Collision Exporter`를 찾아 레벨에 둔다.
3. `Ground Actor`에 Landscape를 지정한다. 지정한 actor는 하나의 연속 surface로 취급해
   급경사 cell을 corner 높이 차만으로 삭제하지 않는다.
4. `Fit Sampling Bounds To Ground Actor`를 누른다. actor의 전체 component bounds에
   `Ground Bounds Padding Cm` 기본 100cm를 더해 sampling box 중심·yaw·extent를 맞춘다.
5. 차량 출발점과 시험할 전체 Landscape가 청록색 sampling box 안에 있고 `Map Origin World
   Cm`을 포함하는지 확인한다. Ground Actor 없이 수동 범위를 쓸 때만
   `Center Sampling On Map Origin`을 먼저 사용한다.
6. 급경사·요철은 `Sample Spacing Cm=50`을 권장한다. 넓힌 면적에 따른 예상 triangle 수가
   exporter 제한 안인지 확인한다.
7. 벽·커브·barrier가 있으면 Place Actors의 All Classes에서 `SimCore Static Collider`를
   하나씩 배치한다. 청록색 ground sampling box와 별개이며, marker box를 실제 장애물에
   맞게 이동·Yaw 회전·크기 조절한다. Pitch/Roll은 지원하지 않아 bake 시 거부된다.
8. 각 marker의 `Collider Id`에 `wall_main_01`처럼 128자 이하의 고유 ASCII ID를 넣고
   `Semantic`을 Wall/Curb/Barrier 중 하나로 정한다. 필요하면 Friction과 Restitution을
   조정한다. `Export Enabled`를 끄면 레벨에는 남기되 package에서는 제외된다.
9. Details에서 `Bake Ground + Static Collision To MapPackage`를 누르고
   `Map origin coverage: OK`, `Height discontinuity filter: disabled for explicit Ground
   Actor`, `discontinuous=0`, `explicit static OBB colliders` 개수를 확인한다.
10. 기존 서버를 종료하고 저장소 루트에서 아래처럼 다시 실행한다.

```powershell
.\scripts\run_landscape_server.ps1
```

기본 출력은 `map_packages/landscape_local_v1/ground_surface.csv`,
`static_colliders.csv`, `manifest.cfg`다. marker가 하나도 없으면 strict header만 있는
`static_colliders.csv`를 정상 출력하므로 이전 bake의 stale 벽이 남지 않는다. exporter는
두 CSV를 모두 임시 파일로 staging한 뒤 교체하고, 실제 bytes checksum이 든 manifest를
마지막에 commit해 부분 저장을 다음 시작에서 fail-closed한다. UE world 원점은
SimCore ENU 원점으로 취급하며 `north=UE X`, `east=UE Y`, `up=UE Z`, `100cm=1m`로
변환한다. marker의 UE +Yaw도 North에서 East로 도는 clockwise-positive
`heading_rad`가 된다. 다른 원점을 사용할 때만 `Map Origin World Cm`을 바꾼다.

현 exporter는 위에서 아래로 보이는 연속 Landscape·정적 메시의 요철과 경사를
`ground_surface.csv`로 만들고, editor-only `SimCore Static Collider` box는 strict
12-field OBB 행으로 만든다. marker 자체의 Unreal collision은 꺼져 있으며 C++ SimCore만
Ego 충돌 response를 결정한다. 따라서 시각 wall mesh에 Unreal collision이 있더라도
marker box를 맞추고 다시 bake해야 한다. GroundQuery는 adaptive grid index를 사용하며,
CollisionWorld는 8m deterministic broad phase를 사용한다. opt-in NPC/보행자 lifecycle·
`WorldState`·표시 경로도 구현됐지만 현재 package에 실제 marker가 없고 PIE 정적/동적
충돌을 확인하지 않았으므로 WP-03 전체는 진행 중이다. 서버는 실행 중 파일을 다시 읽지
않으므로 Sculpt, marker, collision을 수정한 뒤에는 항상 다시 bake하고 서버를 재시작해야
한다.

sampling 범위 끝이나 누락 cell에서 wheel coverage ray가 1~3개만 남으면 실제 partial
support로 계속 계산한다. 별도 centre coverage가 사라지거나 wheel ray가 모두 0개일 때만
서버는 step 시작의 지원 pose로 되돌리고 속도를 0으로 만들어 무한 낙하를 막는다. 이는
정상 suspension contact 이탈과 구분되는 MapPackage 안전 경계다.
PIE를 정지하고 다시 Play하면
`SimulationReset`이 C++ 차량 pose와 simulation clock을 원점 spawn으로 되돌리므로 서버를
재시작할 필요가 없다. 이전 socket session은 영구 폐기해 늦게 도착한 command가 새 주행을
다시 움직이지 못하게 한다. 단, process-lifetime E-stop latch는 PIE reset으로 해제하지
않으므로 E-stop 이후에는 서버를 재시작해야 한다. 새 CSV를 로드할 때도 서버 재시작이
필요하다.

현재 tracked `ground_surface.csv`의 범위는 `east=-15~15m`, `north=-7~30m`다. 새 fit
버튼을 누른 뒤 실제로 다시 Bake하기 전에는 이 범위가 유지된다. 따라서 그 밖에서 차량이
마지막 지원 pose에 정지하는 것은 차체 자세 clamp가 아니라 유한 MapPackage coverage
안전 경계다.

현재 bake 범위 밖도 주행하려면 exporter의 sampling box와 Landscape 충돌 영역을 함께
넓힌 뒤 다시 bake한다. 급경사 접촉 상실의 진단과 수정 근거는
[해결 사례](../../docs/troubleshooting/ue-landscape-steep-grade-contact-loss.md)에 기록한다.

차량이 Landscape를 무시하고 평평한 `Z=0` 지면을 달리면 서버가 기본
`wall_broad_v1` package로 실행된 것이다. 서버 시작 로그의 `[Map] package=...`가
`landscape_local_v1`인지 확인한다.

## 조작

- `W`: Drive 가속. 후진 중에는 먼저 제동하고 거의 정지한 뒤 Drive로 전환한다.
- `S`: 전진 중 service brake. 계속 누르면 거의 정지한 뒤 Reverse로 전환해 후진한다.
- `W`+`S`: 구동 없이 service brake. 상태가 없거나 stale이면 방향 전환도 하지 않는다.
- `A` / `D`: steering
- `Space`: handbrake

표시 휠은 접촉 바퀴의 물리 각속도를 사용하되 `signed speed / tire radius`의 ±3%
범위로 제한한다. 비접촉 바퀴는 차축 평균에서 제외하고 정지·stale 상태에서는 0으로
고정하므로 출발 순간의 slip이 화면에서 헛바퀴 버그처럼 보이지 않는다.
