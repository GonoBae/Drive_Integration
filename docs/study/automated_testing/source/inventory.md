# 부록 F. 자동 검사 등록 목록과 실행 근거

기준: **2026-09-08 현재 작업 공간의 소스와 이미 남아 있는 로그**를 읽어 정리했습니다. 이 부록을 작성하며 빌드, 테스트, 서버를 새로 실행하지 않았습니다.

**등록 개수는 실행 성공 개수가 아닙니다.** CTest의 등록 항목, 각 실행 파일 안의 검사 함수, Unreal Automation의 등록 이름, Python unittest의 실행 사례는 서로 다른 집계 단위입니다. 과거 로그의 숫자는 그때의 코드·빌드·선택 범위에만 해당합니다.

## 1. C++ CTest — 45개 등록, 33개 실행 파일

[CMake의 CTest 활성화와 등록](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:189)에는 BUILD_TESTING 조건 아래 add_test 45개가 있습니다. 이 45개는 CTest가 실행하고 결과를 집계하는 **이름 있는 실행 명령**의 수입니다. 33개 실행 파일 중 두 개를 여러 인수로 재사용하므로 실행 파일 수와 다릅니다. 등록 순서는 편집에 따라 변하므로 로그의 번호보다 이름을 비교해야 합니다.

CTest는 실행을 관리하고 결과를 모으는 도구입니다. 이 저장소의 C++ 검사는 자체 main과 require 같은 보조 함수로 판정합니다. 예를 들어 [control_lease_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/control_lease_test.cpp:12)의 require는 조건 실패 시 예외를 던지고, [같은 파일의 main](B:/Portfolio/Drive_Integration/cpp/host/tests/control_lease_test.cpp:268)은 여러 검사 함수를 호출한 뒤 실패를 종료 코드 1로 전달합니다. 이를 Catch2 또는 GoogleTest 기반이라고 부르지 않습니다. 한 CTest 항목이 내부 검사 함수·반복 시나리오·여러 assertion을 포함하므로, 아래 45를 “assertion 45개”로 해석해서도 안 됩니다.

### 한 실행 파일에 한 항목을 등록한 31개

추가 인수가 없으며 CTest 이름과 실행 파일 기본 이름이 같습니다. Windows 빌드 결과에는 실행 파일 확장자 .exe가 붙습니다. 소스 열은 해당 add_executable에 직접 지정된 파일이며, 연결하는 라이브러리의 전체 소스 목록은 아닙니다.

| CTest 이름 = 실행 파일 | 직접 지정한 소스 | 등록 근거 |
|---|---|---|
| `runtime_collision_reaction_tests` | [runtime_collision_reaction_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/runtime_collision_reaction_test.cpp) | [CMake 194행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:194) |
| `physics_replay_tests` | [physics_replay_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/physics_replay_test.cpp) | [CMake 197행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:197) |
| `npc_search_budget_tests` | [npc_search_budget_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_search_budget_test.cpp) | [CMake 219행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:219) |
| `motorcycle_physics_tests` | [motorcycle_physics_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/motorcycle_physics_test.cpp) | [CMake 240행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:240) |
| `npc_lane_follower_tests` | [npc_lane_follower_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_lane_follower_test.cpp) | [CMake 243행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:243) |
| `npc_horn_policy_tests` | [npc_horn_policy_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_horn_policy_test.cpp) | [CMake 246행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:246) |
| `npc_route_planner_tests` | [npc_route_planner_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_route_planner_test.cpp) | [CMake 249행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:249) |
| `npc_lane_change_tests` | [npc_lane_change_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_lane_change_test.cpp) | [CMake 252행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:252) |
| `npc_local_bypass_tests` | [npc_local_bypass_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_local_bypass_test.cpp) | [CMake 255행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:255) |
| `impact_recovery_tests` | [impact_recovery_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/impact_recovery_test.cpp) | [CMake 258행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:258) |
| `pedestrian_impact_tests` | [pedestrian_impact_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/pedestrian_impact_test.cpp) | [CMake 261행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:261) |
| `impact_tumble_tests` | [impact_tumble_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/impact_tumble_test.cpp) | [CMake 264행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:264) |
| `structure_damage_tests` | [structure_damage_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/structure_damage_test.cpp) | [CMake 267행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:267) |
| `traffic_network_tests` | [traffic_network_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/traffic_network_test.cpp) | [CMake 273행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:273) |
| `vehicle_physics_tests` | [vehicle_physics_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/vehicle_physics_test.cpp) | [CMake 288행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:288) |
| `chassis_ground_contact_tests` | [chassis_ground_contact_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/chassis_ground_contact_test.cpp) | [CMake 298행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:298) |
| `virtual_city_course_tests` | [virtual_city_course_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/virtual_city_course_test.cpp) | [CMake 321행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:321) |
| `signal_city_course_tests` | [signal_city_course_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/signal_city_course_test.cpp)<br>[src/player_vehicle_profile.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/player_vehicle_profile.cpp) | [CMake 341행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:341) |
| `vehicle_config_tests` | [vehicle_config_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/vehicle_config_test.cpp) | [CMake 359행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:359) |
| `runtime_options_tests` | [runtime_options_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/runtime_options_test.cpp) | [CMake 373행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:373) |
| `map_package_ground_query_tests` | [map_package_ground_query_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/map_package_ground_query_test.cpp) | [CMake 387행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:387) |
| `heightfield_ground_query_tests` | [heightfield_ground_query_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/heightfield_ground_query_test.cpp) | [CMake 397행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:397) |
| `map_package_runtime_tests` | [map_package_runtime_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/map_package_runtime_test.cpp) | [CMake 414행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:414) |
| `collision_world_tests` | [collision_world_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/collision_world_test.cpp) | [CMake 424행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:424) |
| `map_package_static_collision_tests` | [map_package_static_collision_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/map_package_static_collision_test.cpp) | [CMake 439행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:439) |
| `vehicle_protocol_tests` | [vehicle_protocol_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/vehicle_protocol_test.cpp) | [CMake 452행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:452) |
| `simulation_clock_tests` | [simulation_clock_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/simulation_clock_test.cpp) | [CMake 462행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:462) |
| `control_lease_tests` | [control_lease_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/control_lease_test.cpp) | [CMake 472행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:472) |
| `body_frame_adapter_tests` | [body_frame_adapter_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/body_frame_adapter_test.cpp) | [CMake 482행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:482) |
| `simulation_host_tests` | [simulation_host_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/simulation_host_test.cpp) | [CMake 492행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:492) |
| `websocket_server_tests` | [websocket_server_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/websocket_server_test.cpp) | [CMake 502행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:502) |

### npc_host_tests — 5개 등록

실행 파일: `npc_host_tests`. 소스: [tests/npc_host_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_host_test.cpp). 같은 바이너리라도 아래 인수가 선택하는 경로가 달라 CTest는 별도 항목으로 집계합니다.

| CTest 등록 이름 | 실행 파일 뒤의 인수 | 등록 근거 |
|---|---|---|
| `npc_host_tests` | 없음 | [CMake 204행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:204) |
| `npc_return_curve_tests` | `--return-curve` | [CMake 205행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:205) |
| `npc_stopped_yaw_tests` | `--stationary-yaw` | [CMake 206행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:206) |
| `npc_continuous_yaw_tests` | `--continuous-yaw` | [CMake 207행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:207) |
| `npc_lateral_recovery_tests` | `--lateral-recovery` | [CMake 209행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:209) |

### npc_navigation_host_tests — 9개 등록

실행 파일: `npc_navigation_host_tests`. 소스: [tests/npc_navigation_host_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_navigation_host_test.cpp). 같은 바이너리라도 아래 인수가 선택하는 경로가 달라 CTest는 별도 항목으로 집계합니다.

| CTest 등록 이름 | 실행 파일 뒤의 인수 | 등록 근거 |
|---|---|---|
| `npc_navigation_host_tests` | 없음 | [CMake 216행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:216) |
| `npc_delayed_crash_bypass_tests` | `--delayed-crash` | [CMake 222행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:222) |
| `npc_mixed_fleet_tests` | `--mixed-fleet` | [CMake 224행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:224) |
| `npc_close_escape_tests` | `--close-escape` | [CMake 226행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:226) |
| `npc_limited_close_escape_tests` | `--limited-close-escape` | [CMake 228행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:228) |
| `npc_downed_stopline_escape_tests` | `--downed-stopline` | [CMake 230행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:230) |
| `npc_downed_rear_blocked_tests` | `--downed-rear-blocked` | [CMake 232행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:232) |
| `npc_crashed_stopline_escape_tests` | `--crashed-stopline` | [CMake 234행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:234) |
| `npc_local_curve_truck_tests` | `--local-bypass` | [CMake 235행](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:235) |

### 집계와 결과 해석의 주의점

- 합계는 **31 + 5 + 9 = 45개 등록**, 실행 파일은 **31 + 1 + 1 = 33개**입니다.
- [vehicle_cornering_probe](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:302)는 빌드 대상이지만 add_test 등록이 없어 위 45개에 포함하지 않았습니다.
- [virtual_city_course_tests 설정](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:321)과 [signal_city_course_tests 설정](B:/Portfolio/Drive_Integration/cpp/host/CMakeLists.txt:341)은 종료 코드 77을 건너뜀으로 처리합니다. 실제 [Virtual City의 패키지 누락 처리](B:/Portfolio/Drive_Integration/cpp/host/tests/virtual_city_course_test.cpp:1092)와 [Signal City의 패키지 누락 처리](B:/Portfolio/Drive_Integration/cpp/host/tests/signal_city_course_test.cpp:688)가 이 코드를 반환합니다. “실패 없음”만 보고 누락된 맵까지 검증했다고 해석하지 말고 실행·건너뜀 수를 함께 봅니다.
- TIMEOUT 속성은 매달린 테스트를 중단하는 실행 제한입니다. 게임의 프레임 시간 또는 성능 목표가 아닙니다.
- 이 목록은 현재 CMake 소스의 정의입니다. 기존 빌드 디렉터리가 다시 구성됐는지, 그 바이너리가 현재 소스와 일치하는지는 이 목록만으로 증명하지 않습니다.

## 2. Unreal Automation — 112개 등록, 31개 소스 파일

전체 Source 디렉터리에서 현재의 IMPLEMENT_SIMPLE_AUTOMATION_TEST 등록 이름을 추출했습니다. 런타임 모듈의 *Tests.cpp만 찾으면 Editor 모듈의 세 검사를 놓치므로 두 모듈을 함께 집계합니다. 아래는 **소스에 정의된 등록 목록**이지 현재 실행 보고서가 아닙니다. 실제 사용 가능 여부와 실행 범위는 빌드 조건, 로드한 모듈, 테스트 플래그, 선택 필터에 따릅니다.

| 소스 모듈 | 등록이 있는 파일 | 등록 이름 |
|---|---:|---:|
| DriveIntegration | 30개 | 109개 |
| DriveIntegrationEditor | 1개 | 3개 |
| 합계 | 31개 | 112개 |

개별 항목을 펼치면 정확한 Automation 이름과 등록 매크로의 시작 행을 볼 수 있습니다. 한 등록의 RunTest 안에는 여러 TestTrue·TestEqual 등의 판정이 들어갈 수 있어, 112는 assertion 개수가 아닙니다.

<details>
<summary>SimCoreClientSettingsTests.cpp — 3개</summary>

소스: [SimCoreClientSettingsTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientSettingsTests.cpp:11)

- `DriveIntegration.Configuration.LoopbackEndpoint` — 11행
- `DriveIntegration.Configuration.FilePrecedenceAndRelocation` — 38행
- `DriveIntegration.Configuration.InvalidConfigBlocksConnectionAndSelection` — 100행

</details>

<details>
<summary>SimCoreCoordinateFramesTests.cpp — 1개</summary>

소스: [SimCoreCoordinateFramesTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreCoordinateFramesTests.cpp:56)

- `DriveIntegration.Coordinates.GeoTransformContract` — 56행

</details>

<details>
<summary>SimCoreDamagePresentationTests.cpp — 4개</summary>

소스: [SimCoreDamagePresentationTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreDamagePresentationTests.cpp:9)

- `DriveIntegration.Presentation.VehicleDamage.LocalDentAccumulation` — 9행
- `DriveIntegration.Presentation.VehicleDamage.ActualVertexDisplacement` — 92행
- `DriveIntegration.Presentation.VehicleDamage.ContactLocalShapeAndHistory` — 117행
- `DriveIntegration.Presentation.TurnSignals.DirectionAndCadence` — 152행

</details>

<details>
<summary>SimCoreDriveReplayTests.cpp — 3개</summary>

소스: [SimCoreDriveReplayTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreDriveReplayTests.cpp:14)

- `DriveIntegration.Replay.RecordKeyAvoidsEditorF5` — 14행
- `DriveIntegration.Replay.RecordCsvRoundTrip` — 35행
- `DriveIntegration.Replay.RecordedPlayerPoseMatchesModel` — 94행

</details>

<details>
<summary>SimCoreDriverPresentationTests.cpp — 4개</summary>

소스: [SimCoreDriverPresentationTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreDriverPresentationTests.cpp:53)

- `DriveIntegration.Presentation.Driver.AuthoritativeInjuryTarget` — 53행
- `DriveIntegration.Presentation.Driver.SeatedAndSlumpedPose` — 137행
- `DriveIntegration.Presentation.Driver.FleetCabinVisibilityAndDoorDamage` — 281행
- `DriveIntegration.Presentation.Driver.MotorcycleEjectionAndReset` — 379행

</details>

<details>
<summary>SimCoreExhaustTests.cpp — 2개</summary>

소스: [SimCoreExhaustTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreExhaustTests.cpp:9)

- `DriveIntegration.VehicleEffects.ExhaustPresentation` — 9행
- `DriveIntegration.VehicleEffects.ExhaustRuntimeComponent` — 39행

</details>

<details>
<summary>SimCoreGroundSnapshotTests.cpp — 3개</summary>

소스: [SimCoreGroundSnapshotTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreGroundSnapshotTests.cpp:145)

- `DriveIntegration.Ground.WorldCollisionMaterials` — 145행
- `DriveIntegration.Ground.SlopeAndCoverage` — 216행
- `DriveIntegration.Ground.StaticMarkerPackage` — 283행

</details>

<details>
<summary>SimCoreHealthTests.cpp — 5개</summary>

소스: [SimCoreHealthTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreHealthTests.cpp:159)

- `DriveIntegration.Protocol.WorldStateHealth` — 159행
- `DriveIntegration.Protocol.WorldStateHealthValidation` — 236행
- `DriveIntegration.Diagnostics.ServerHealth` — 280행
- `DriveIntegration.Diagnostics.DebugHudVehicleState` — 357행
- `DriveIntegration.Diagnostics.CrossPlayEstopHealth` — 447행

</details>

<details>
<summary>SimCoreInstrumentClusterTests.cpp — 3개</summary>

소스: [SimCoreInstrumentClusterTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreInstrumentClusterTests.cpp:10)

- `DriveIntegration.Presentation.InstrumentCluster.DisplayContract` — 10행
- `DriveIntegration.Presentation.InstrumentCluster.AutomaticGameModeInstall` — 73행
- `DriveIntegration.Presentation.InstrumentCluster.CompactViewportLayout` — 86행

</details>

<details>
<summary>SimCoreMapPackageTests.cpp — 1개</summary>

소스: [SimCoreMapPackageTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreMapPackageTests.cpp:8)

- `DriveIntegration.MapPackage.ReconnectPathIsAbsoluteAndIdempotent` — 8행

</details>

<details>
<summary>SimCoreNpcPresentationTests.cpp — 7개</summary>

소스: [SimCoreNpcPresentationTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreNpcPresentationTests.cpp:155)

- `DriveIntegration.NpcPresentation.ActorGeometryAndMotion` — 155행
- `DriveIntegration.NpcPresentation.FrontWheelSteering` — 263행
- `DriveIntegration.NpcPresentation.VehicleClassModels` — 338행
- `DriveIntegration.NpcPresentation.AcceptedSnapshotLifecycle` — 421행
- `DriveIntegration.NpcPresentation.AuthoritativeRollover` — 521행
- `DriveIntegration.NpcPresentation.DamageMaterialsAndWire` — 566행
- `DriveIntegration.NpcPresentation.DownedBodyAndIndicatorWire` — 632행

</details>

<details>
<summary>SimCoreOrbitCameraTests.cpp — 5개</summary>

소스: [SimCoreOrbitCameraTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreOrbitCameraTests.cpp:21)

- `DriveIntegration.Camera.FrameIndependentOrbit` — 21행
- `DriveIntegration.Camera.LimitsResetAndHorizon` — 52행
- `DriveIntegration.Camera.InputBindingsAndOfflinePawn` — 97행
- `DriveIntegration.Camera.DriverLimitsMountsAndModeCycle` — 233행
- `DriveIntegration.Camera.ModeSwitchClassMountsAndOwnerVisibility` — 294행

</details>

<details>
<summary>SimCorePedestrianPresentationTests.cpp — 9개</summary>

소스: [SimCorePedestrianPresentationTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCorePedestrianPresentationTests.cpp:84)

- `DriveIntegration.PedestrianPresentation.HumanoidGeometryAndAuthority` — 84행
- `DriveIntegration.PedestrianPresentation.ServerHeadingMakesActorXForward` — 144행
- `DriveIntegration.PedestrianPresentation.ArticulatedImpactAuthorityAndRecovery` — 176행
- `DriveIntegration.PedestrianPresentation.PopulationProfilesMatchCapsules` — 323행
- `DriveIntegration.PedestrianPresentation.GravityLimitedGroundAnchor` — 365행
- `DriveIntegration.PedestrianPresentation.LocalSlopeCurbAndUnevenGroundQueries` — 412행
- `DriveIntegration.PedestrianPresentation.GentleShoveAndSameEventKnockdown` — 464행
- `DriveIntegration.PedestrianPresentation.AcceptedVelocityDrivesLocomotion` — 529행
- `DriveIntegration.PedestrianPresentation.ImmediateMovingImpactAndAuthoritativeDownedBody` — 573행

</details>

<details>
<summary>SimCorePlayerVehicleSelectionTests.cpp — 2개</summary>

소스: [SimCorePlayerVehicleSelectionTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCorePlayerVehicleSelectionTests.cpp:27)

- `DriveIntegration.Presentation.PlayerVehicleSelection` — 27행
- `DriveIntegration.Network.PlayerVehicleSelectionHello` — 109행

</details>

<details>
<summary>SimCoreSensorRigTests.cpp — 1개</summary>

소스: [SimCoreSensorRigTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreSensorRigTests.cpp:11)

- `DriveIntegration.Sensors.ConfigAndAuthoritativeSchedule` — 11행

</details>

<details>
<summary>SimCoreSignalCityLayoutTests.cpp — 8개</summary>

소스: [SimCoreSignalCityLayoutTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreSignalCityLayoutTests.cpp:103)

- `DriveIntegration.SignalCity.GeometryContract` — 103행
- `DriveIntegration.SignalCity.IntersectionsAndMarkings` — 168행
- `DriveIntegration.SignalCity.ContinuousLaneMarkings` — 228행
- `DriveIntegration.SignalCity.CollectorLaneOrdering` — 367행
- `DriveIntegration.SignalCity.SharedCollectorSurface` — 423행
- `DriveIntegration.SignalCity.NonRectangularDriveRoute` — 534행
- `DriveIntegration.SignalCity.ContinuousCollectorLaneMerges` — 576행
- `DriveIntegration.SignalCity.KoreanNorthDistrictAndDashedMerges` — 675행

</details>

<details>
<summary>SimCoreSignalCityTrafficLayoutTests.cpp — 6개</summary>

소스: [SimCoreSignalCityTrafficLayoutTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreSignalCityTrafficLayoutTests.cpp:36)

- `DriveIntegration.SignalCity.Traffic.TwoControllerTopology` — 36행
- `DriveIntegration.SignalCity.Traffic.RejectUnsafePlans` — 84행
- `DriveIntegration.SignalCity.Traffic.DeterministicV2Json` — 132행
- `DriveIntegration.SignalCity.Traffic.SameDirectionPassingApproaches` — 172행
- `DriveIntegration.SignalCity.Traffic.SidewalkWaitingAndProtectedCrossings` — 231행
- `DriveIntegration.SignalCity.Traffic.CollectorTangentContinuity` — 326행

</details>

<details>
<summary>SimCoreSteeringContractTests.cpp — 3개</summary>

소스: [SimCoreSteeringContractTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreSteeringContractTests.cpp:162)

- `DriveIntegration.Steering.InputWireContract` — 162행
- `DriveIntegration.Steering.WheelPresentationContract` — 194행
- `DriveIntegration.Steering.PawnTickFullLockAndContactLoss` — 283행

</details>

<details>
<summary>SimCoreSteeringInputTests.cpp — 1개</summary>

소스: [SimCoreSteeringInputTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreSteeringInputTests.cpp:7)

- `DriveIntegration.Control.KeyboardSteering.FrameIndependentRamp` — 7행

</details>

<details>
<summary>SimCoreStructureDamageTests.cpp — 4개</summary>

소스: [SimCoreStructureDamageTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreStructureDamageTests.cpp:85)

- `DriveIntegration.StructureDamage.PoleGeometryAndOutage` — 85행
- `DriveIntegration.StructureDamage.FacadeIdempotenceAndBudget` — 137행
- `DriveIntegration.StructureDamage.LocalFootprintHistoryAndVisibleDebris` — 183행
- `DriveIntegration.StructureDamage.InvalidInputAndPreview` — 267행

</details>

<details>
<summary>SimCoreStructureProtocolTests.cpp — 2개</summary>

소스: [SimCoreStructureProtocolTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreStructureProtocolTests.cpp:93)

- `DriveIntegration.Protocol.StructureDamage` — 93행
- `DriveIntegration.Protocol.StructureDamageValidation` — 167행

</details>

<details>
<summary>SimCoreSuspensionPresentationTests.cpp — 1개</summary>

소스: [SimCoreSuspensionPresentationTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreSuspensionPresentationTests.cpp:9)

- `DriveIntegration.Presentation.SuspensionFrameAndWheelHubs` — 9행

</details>

<details>
<summary>SimCoreTrafficSignalTests.cpp — 8개</summary>

소스: [SimCoreTrafficSignalTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreTrafficSignalTests.cpp:177)

- `DriveIntegration.Protocol.TrafficSignals` — 177행
- `DriveIntegration.Protocol.TrafficSignalsValidation` — 244행
- `DriveIntegration.TrafficSignals.FreshnessAndOneHot` — 332행
- `DriveIntegration.TrafficSignals.ActorPresentation` — 357행
- `DriveIntegration.TrafficSignals.ProtectedLeftFourLens` — 430행
- `DriveIntegration.TrafficSignals.ClientLifecycle` — 481행
- `DriveIntegration.TrafficSignals.BinaryMessageFragmentAssembly` — 543행
- `DriveIntegration.TrafficSignals.BinaryMessageBoundarySafety` — 585행

</details>

<details>
<summary>SimCoreTurnSignalsTests.cpp — 5개</summary>

소스: [SimCoreTurnSignalsTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreTurnSignalsTests.cpp:54)

- `DriveIntegration.Presentation.TurnSignals.AuthoredLensesAndDamage` — 54행
- `DriveIntegration.Presentation.TurnSignals.SelectionRelativeFullCycle` — 175행
- `DriveIntegration.Presentation.TurnSignals.PlayerTurnThenCentreCancels` — 213행
- `DriveIntegration.Presentation.TurnSignals.PlayerAutoCancelMotionGuards` — 262행
- `DriveIntegration.Presentation.TurnSignals.PlayerAutoCancelLifecycleGuards` — 289행

</details>

<details>
<summary>SimCoreVehicleAudioTests.cpp — 4개</summary>

소스: [SimCoreVehicleAudioTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreVehicleAudioTests.cpp:27)

- `DriveIntegration.Presentation.VehicleAudio.AuthoritativeInputs` — 27행
- `DriveIntegration.Presentation.VehicleAudio.IndicatorLampSynchronization` — 69행
- `DriveIntegration.Presentation.VehicleAudio.TireSlipAndOfflineRender` — 121행
- `DriveIntegration.Presentation.VehicleAudio.SharpCornerOnly` — 202행

</details>

<details>
<summary>SimCoreVehicleControlResolverTests.cpp — 1개</summary>

소스: [SimCoreVehicleControlResolverTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreVehicleControlResolverTests.cpp:6)

- `DriveIntegration.Control.VehicleDirectionResolver` — 6행

</details>

<details>
<summary>SimCoreVehicleHornTests.cpp — 4개</summary>

소스: [SimCoreVehicleHornTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreVehicleHornTests.cpp:108)

- `DriveIntegration.Presentation.VehicleHorn.EnvelopeAndAntiSpam` — 108행
- `DriveIntegration.Presentation.VehicleHorn.InputAndSpatialization` — 149행
- `DriveIntegration.Presentation.VehicleHorn.ProtocolAndPlayLifecycle` — 225행
- `DriveIntegration.Presentation.VehicleHorn.HoldReleaseAndNpcPulseIsolation` — 283행

</details>

<details>
<summary>SimCoreVehicleVisualProfileTests.cpp — 2개</summary>

소스: [SimCoreVehicleVisualProfileTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreVehicleVisualProfileTests.cpp:8)

- `DriveIntegration.Presentation.VehicleVisualProfiles` — 8행
- `DriveIntegration.Presentation.TruckCollisionBounds` — 69행

</details>

<details>
<summary>SimCoreVirtualCityLayoutTests.cpp — 4개</summary>

소스: [SimCoreVirtualCityLayoutTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreVirtualCityLayoutTests.cpp:74)

- `DriveIntegration.VirtualCity.GeometryContract` — 74행
- `DriveIntegration.VirtualCity.CurbSidewalkTopAlignment` — 148행
- `DriveIntegration.VirtualCity.ClosedDriveRoute` — 175행
- `DriveIntegration.VirtualCity.GradeSurfaceAndJunction` — 244행

</details>

<details>
<summary>SimCoreVirtualCityTrafficLayoutTests.cpp — 3개</summary>

소스: [SimCoreVirtualCityTrafficLayoutTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreVirtualCityTrafficLayoutTests.cpp:37)

- `DriveIntegration.VirtualCity.Traffic.DirectionalTopology` — 37행
- `DriveIntegration.VirtualCity.Traffic.RejectInvalidTopology` — 93행
- `DriveIntegration.VirtualCity.Traffic.DeterministicJson` — 124행

</details>

<details>
<summary>BuildSedanVisualCommandlet.cpp — 3개</summary>

소스: [BuildSedanVisualCommandlet.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegrationEditor/BuildSedanVisualCommandlet.cpp:1990)

- `DriveIntegration.NpcFleet.TruckCabinVisibility` — 1990행
- `DriveIntegration.SedanVisual.GeometryIntegrity` — 2016행
- `DriveIntegration.SedanVisual.DamageMaterialIntegrity` — 2121행

</details>

## 3. 주요 스크립트 — 어디에 쓰는가

아래는 역할을 찾기 위한 짧은 지도입니다. 실행 순서와 명령 인수의 상세 설명은 본문을 따릅니다. 파일이 존재한다는 사실과 해당 검사가 최근 실행됐다는 증거를 구분합니다.

| 진입점 또는 묶음 | 사용하는 이유 | 범위와 구분 |
|---|---|---|
| [check_core.py](B:/Portfolio/Drive_Integration/scripts/check_core.py:71) | C++·생성 protobuf·Python 도구·PowerShell·실제 통신·Unreal 검사를 같은 실행 기록 아래 모읍니다. | 옵션에 따라 빌드/Unreal을 생략합니다. 자동 결과가 패키지 성능·30분 수동 주행·아트 승인을 대신하지 않습니다. |
| [Python unit discovery 정의](B:/Portfolio/Drive_Integration/scripts/check_core.py:87) | scripts의 test_*.py를 찾아 도구의 판정·격리·분석 로직을 확인합니다. | 현재 발견 대상은 아래 6개 파일입니다. 도구 단위검사 성공과 실제 서버를 사용하는 wire 검사 성공은 별개입니다. |
| [test_generated_proto.py](B:/Portfolio/Drive_Integration/python/relay_server/tests/test_generated_proto.py:8) | 현재 스키마로 임시 생성한 Python 바인딩과 저장소 파일을 비교해 갱신 누락을 찾습니다. | check_core가 discovery와 별도 단계로 호출합니다. 네트워크 메시지의 모든 의미 규칙까지 증명하지는 않습니다. |
| [test_server_launcher.ps1](B:/Portfolio/Drive_Integration/scripts/test_server_launcher.ps1) / [test_package_windows.ps1](B:/Portfolio/Drive_Integration/scripts/test_package_windows.ps1:14) | 실행 경로·설정·패키지 계획의 계약을 작은 검사로 확인합니다. | 특히 package-plan 통과는 실제 패키징이나 패키지 실행 성공이 아닙니다. |
| [smoke_signal_city.py](B:/Portfolio/Drive_Integration/scripts/smoke_signal_city.py:1) | 독립 자식 서버와 실제 WebSocket으로 신호 전체 주기, 상태, 제어 세션과 복구를 연결해 검사합니다. | check_core의 signal-city-wire 단계입니다. 사전 입력 점검과 실제 통신 실행은 서로 다른 범위입니다. |
| [smoke_health.py](B:/Portfolio/Drive_Integration/scripts/smoke_health.py:1), [smoke_traffic.py](B:/Portfolio/Drive_Integration/scripts/smoke_traffic.py:1), [smoke_npc.py](B:/Portfolio/Drive_Integration/scripts/smoke_npc.py:1) | 건강 상태·신호 관계·NPC 경로 등 특정 통신 시나리오를 좁혀 확인합니다. | 각각 존재하는 독립 도구입니다. check_core가 이 세 파일을 모두 별도 단계로 실행한다고 쓰지 않습니다. |
| [smoke_host.py](B:/Portfolio/Drive_Integration/scripts/smoke_host.py:13) | 여러 wire 도구가 자신이 만든 자식 프로세스만 정리하도록 생명주기를 공유합니다. | 독립 검사 목록이 아니라 공용 보조 모듈입니다. 기존 사용자의 서버를 검사 대상으로 바꾸지 않는 경계입니다. |
| [smoke_physics_replay.py](B:/Portfolio/Drive_Integration/scripts/smoke_physics_replay.py:1) | 실제 통신에서 적용된 입력을 기록하고, NPC·보행자 충돌 입력까지 재사용해 각 물리 틱을 오프라인 재계산합니다. | check_core의 physics-replay-wire 단계입니다. 동일 기록의 수치 재현성을 검사하며 모든 입력에 대한 물리 정확성을 보장하지는 않습니다. |
| [package_windows.ps1](B:/Portfolio/Drive_Integration/scripts/package_windows.ps1:23) | 배포 디렉터리·설정·빌드/패키지 계획을 만들고 패키징을 진행합니다. | PrepareOnly의 준비 범위와 실제 패키징을 구분합니다. 검사 전용 스크립트만은 아닙니다. |
| [smoke_windows_package.ps1](B:/Portfolio/Drive_Integration/scripts/smoke_windows_package.ps1:1) | 이미 만들어진 실제 패키지의 서버·클라이언트를 띄워 설정, Hello/맵 합의, 유효 상태 수신을 확인합니다. | NullRHI 시작 검사입니다. [스크립트의 한계 선언](B:/Portfolio/Drive_Integration/scripts/smoke_windows_package.ps1:58)처럼 렌더링·조작감·목표 PC 성능·30분 주행은 대상이 아닙니다. |
| [analyze_performance.py](B:/Portfolio/Drive_Integration/scripts/analyze_performance.py:1) | 선택적으로 저장한 UE CSV와 짝 JSON 메타데이터를 해석해 프레임 간격과 수신 상태 간격을 분리합니다. | 측정 기록을 분석하는 도구입니다. 서버 tick을 렌더링 FPS로 대체하지 않으며, smoke 결과가 릴리스 성능 승인도 아닙니다. |

현재 Python 도구 discovery 대상 파일:

- [test_analyze_performance.py](B:/Portfolio/Drive_Integration/scripts/test_analyze_performance.py)
- [test_check_core.py](B:/Portfolio/Drive_Integration/scripts/test_check_core.py)
- [test_smoke_health.py](B:/Portfolio/Drive_Integration/scripts/test_smoke_health.py)
- [test_smoke_host.py](B:/Portfolio/Drive_Integration/scripts/test_smoke_host.py)
- [test_smoke_signal_city.py](B:/Portfolio/Drive_Integration/scripts/test_smoke_signal_city.py)
- [test_smoke_traffic.py](B:/Portfolio/Drive_Integration/scripts/test_smoke_traffic.py)

## 4. 이미 존재하는 최근 실행 근거

아래는 **새로 실행한 결과가 아니라 보관 로그를 읽은 결과**입니다. 날짜는 2026-09-08 로컬 파일 기록 시각을 기준으로 요약했습니다. 당시 로그의 분모는 당시 등록 또는 선택 범위이며, 현재 소스의 CTest 45·Unreal 112에 자동으로 적용하지 않습니다.

| 날짜·시각 | 실제 기록된 범위 | 결과 | 근거 로그 |
|---|---|---|---|
| 09-08 01:32 | 당시 C++ 전체 42개 | 42/42 통과, 234.98초 | [local-bypass 전체 로그](B:/Portfolio/Drive_Integration/runtime_logs/20260908-final-local-bypass-cpp-tests.log:87) |
| 09-08 01:59 | 당시 Unreal Automation 110개 | 성공 104 + 경고 포함 성공 6, 실패 0, 미실행 0 | [Automation JSON](B:/Portfolio/Drive_Integration/runtime_logs/driving-stability-20260908/ue-automation/index.json:18) |
| 09-08 02:02 | 생성 protobuf 1개 / Python 도구 69개 | 각각 OK | [generated-proto](B:/Portfolio/Drive_Integration/runtime_logs/driving-stability-20260908/generated-proto.log:4), [python-tools](B:/Portfolio/Drive_Integration/runtime_logs/driving-stability-20260908/python-tools.log:80) |
| 09-08 02:13 | Signal City 실제 WebSocket | 상태 7,080개, 신호 전체 주기, SafeStop·동일 Play 복구·새 Play 초기화 PASS | [signal-city-wire](B:/Portfolio/Drive_Integration/runtime_logs/driving-stability-20260908/signal-city-wire.log:9) |
| 09-08 02:13 | 실제 입력 기록의 물리 재생 | 480프레임·7이벤트·2초기화, 위치/yaw 최대 오차 0, PASS | [physics-replay-wire](B:/Portfolio/Drive_Integration/runtime_logs/driving-stability-20260908/physics-replay-wire.log:4) |
| 09-08 02:14 | 당시 C++ 전체 44개 | 42개 통과, 2개 실패, 215.53초 | [yaw-budget 전체 로그](B:/Portfolio/Drive_Integration/runtime_logs/20260908-final-npc-yaw-budget-cpp-tests.log:184) |
| 09-08 02:16 | 위 실패 항목 2개만 재검사 | npc_navigation_host_tests와 npc_delayed_crash_bypass_tests 2/2 통과, 50.96초 | [scheduled-fixtures 대상 로그](B:/Portfolio/Drive_Integration/runtime_logs/20260908-final-npc-scheduled-fixtures-tests.log:1) |
| 09-08 14:33 | 트럭·충돌 관련 선택 9개 | 9/9 통과, 82.13초 | [truck_collision_final_tests](B:/Portfolio/Drive_Integration/runtime_logs/truck_collision_final_tests.log:21) |
| 09-08 14:47 | NPC 회전 관련 선택 5개 | 5/5 통과, 36.24초 | [npc_rotation_targeted_tests](B:/Portfolio/Drive_Integration/runtime_logs/npc_rotation_targeted_tests.log:13) |
| 09-08 14:48–14:49 | npc_host_tests 단일 실행 | Test Passed. 헤더의 3/45는 등록 번호/전체 등록 수 | [LastTest 헤더](B:/Portfolio/Drive_Integration/cpp/host/build/Testing/Temporary/LastTest.log:3), [단일 항목 통과](B:/Portfolio/Drive_Integration/cpp/host/build/Testing/Temporary/LastTest.log:277) |

전체 44개 실행 후 두 실패 항목의 “첫 틱에 즉시 검색한다”는 검사 가정을 예약 검색 순서에 맞게 조정하고, 두 항목을 다시 통과시켰다는 설명은 [당일 작업 기록](B:/Portfolio/Drive_Integration/docs/worklogs/2026-09-08.md:217)에 있습니다. 이것은 **전체 44개 재실행 통과** 또는 **현재 45개 전체 통과**와 같은 주장이 아닙니다. 최신 LastTest.log도 위와 같이 한 항목의 실행만 담고 있습니다.

Unreal의 과거 110개 결과에는 경고 포함 성공 6개가 별도 집계되어 있습니다. 이 부록은 이를 “경고 없이 모두 통과”로 표현하지 않으며, 현재 추가된 등록을 포함한 112개 전체 실행의 성공 근거로 확장하지 않습니다. 서로 다른 시각과 범위의 통과 로그를 더해 하나의 전체 성공률을 만들지 않습니다.

이 표에 특정 패키지 또는 성능 실행 결과가 없다는 것은 도구가 없거나 실행된 적이 없다는 뜻이 아니라, **이 부록에서 제시하는 확인 근거의 범위 밖**이라는 뜻입니다. 자동 검사가 통과해도 실제 화면의 품질, 목표 PC의 렌더링·입력 지연, 장시간 주행은 각각 해당 환경의 별도 검증이 필요합니다.
