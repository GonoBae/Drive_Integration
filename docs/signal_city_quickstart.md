# signal_city_v2 실행·검증

`signal_city_v2`는 기존 `/Game/VirtualCity/Maps/L_VirtualCity`와
`virtual_city_v1`을 덮어쓰지 않는 별도 신호 도심 코스다. 두 교차로의 신호 controller가
서로 다른 phase offset으로 동작하고, 바깥쪽 비정형 loop와 중앙 avenue를 연결한다.
배경은 Engine 기본 도형과 프로젝트 재질로 만든 검증용 blockout이며 최종 도시 아트가 아니다.

## 고정 identity

| 항목 | 값 |
|---|---|
| Unreal map | `/Game/SignalCity/Maps/L_SignalCity` |
| map-local GameMode | `SignalCityGameMode` |
| MapPackage | `map_packages/signal_city_v2` |
| traffic schema | `traffic_network.json` format version 2 |
| map checksum | `fnv1a64:7446108adad3e25b` |
| traffic checksum | `fnv1a64:dfcb5e4541d71adf` |
| host config | `cpp/host/config/signal_city_server.cfg` |
| launcher | `scripts/run_signal_city_server.ps1` |

## 검증된 현재 생성물

2026-09-02 재생성 및 map·traffic `-ValidateOnly` 성공 기준이다.

| 항목 | 값 |
|---|---:|
| collision checksum | `fnv1a64:7446108adad3e25b` |
| traffic network checksum | `fnv1a64:dfcb5e4541d71adf` |
| scene box / Ground component / static collider | 227 / 124 / 49 |
| heightfield | `401 × 481`, 50cm spacing |
| QA route checkpoint | 394 |
| traffic lane / runtime head / controller | 42 / 16(차량 8·보행 8) / 2 |
| server-authoritative NPC / pedestrian | 4 / 8 |

이 수치는 현재 생성 bytes와 저장 map의 검증 기록이다. 재Bake 뒤에는 manifest와 두
commandlet의 성공 로그를 다시 확인한 뒤 문서의 생성 기록도 함께 갱신한다. traffic JSON의
`source_map_checksum`은 위 collision checksum과 일치하고, traffic network checksum은 JSON
전체 바이트의 별도 identity다.
별도 최신 publisher build의 Health와 traffic v2 WebSocket smoke는 통과했다. 서버는 아래
공통 launcher가 시작한 프로세스의 port 9000 소유권과 고유 stdout/stderr 로그를 확인한다.
PID는 일시 상태이므로 시험 때마다 listener와 로그의 map identity를 다시 확인한다.
사용자 PIE는 아직 대기다.

Unreal 기본 시작 맵은 계속 `L_VirtualCity`다. `signal_city_v2`를 시험할 때는 Content Browser에서
`L_SignalCity`를 직접 연다. 일반 `ExternalVehiclePawn`이나 다른 맵의 exporter를 추가 배치하지
않는다. map-local Pawn이 `signal_city_v2`와 `unreal-signal-city` identity를 자동 사용한다.

## 처음 실행

`virtual_city_v1`에서 이 코스로 전환할 때는 **서버와 Unreal map을 한 쌍으로 바꾼다**.
v1 서버에 `L_SignalCity`만 열거나 signal-city 서버에 `L_VirtualCity`만 열면 Hello/map
checksum gate가 조작을 거부하는 것이 정상이다.

1. PIE를 끝내고 Unreal Editor를 종료한다. 이미 port 9000을 사용하는 서버가 있으면 시작
   로그에서 PID와 package를 확인한 뒤 그 프로세스만 직접 종료한다. listener가 없어도 이전
   publisher PID가 잔존하면 먼저 정리한다. launcher는 다른 프로세스를 강제 종료하지 않는다.
   실행 중이던 표준 host 때문에 새 binary link가 막혔던
   작업 복사본이라면 [아래 host build](#빌드와-생성-순서)를 먼저 다시 실행한다.
2. 저장소 루트의 PowerShell에서 signal-city 서버를 실행한다.

   ```powershell
   .\scripts\run_signal_city_server.ps1 -Background
   ```

   foreground 로그가 필요하면 `-Background`를 빼고 종료할 때 `Ctrl+C`를 누른다.
   background 실행은 출력된 PID만 `Stop-Process -Id <PID>`로 종료한다. launcher는 host
   실행 파일, runtime cfg, MapPackage payload와 `traffic_network.json`을 모두 preflight하고
   하나라도 없으면 서버를 시작하지 않는다. 2026-09-02 실제 전환 중 발견한 PowerShell
   multiline `if` parse 오류는 중첩 `if`로 수정했으며, 수정된 launcher로 PID 23880의 bind를
   확인했다.
3. launcher가 출력한 stdout에서 `[Map] ... id=signal_city_v2`, collision checksum
   `fnv1a64:7446108adad3e25b`, traffic checksum `fnv1a64:dfcb5e4541d71adf`,
   `lanes=42 signal_heads=16`을 확인한다. 같은 `[Traffic]` 줄의 controller 진단은
   `id=1,cycle_seconds=36,offset_seconds=0`과
   `id=2,cycle_seconds=36,offset_seconds=9`여야 한다.
   `virtual_city_v1`, lanes25 또는 heads3가 보이면 이전 서버이므로 Play하지 않는다.
4. Unreal Editor에서 `/Game/SignalCity/Maps/L_SignalCity`를 직접 열고 Play한다.
5. HUD의 연결·map 승인과 `Active`를 확인한다. 차량은 중앙 avenue의 ENU `(0,0)`에서
   북쪽을 향해 시작한다. `W/S` 가감속·후진, `A/D` 조향, `Space` 후륜 사이드 브레이크,
   마우스 orbit, 휠 zoom, `C` 카메라 복귀를 사용한다. `F3`는 차량 collision/contact
   디버그 오버레이, `F5`는 권한 snapshot CSV 기록, `F6`는 마지막 CSV의 collision-free
   시각 ghost 재생이다.
6. 새 Play에서는 차량·simulation clock·신호가 reset된다. 같은 PlaySession의 일시적
   socket reconnect는 simulation time을 되감지 않는다.

PowerShell execution policy 때문에 launcher를 사용할 수 없으면 전역 정책을 바꾸지 말고
같은 config를 직접 지정한다.

```powershell
& 'B:\Portfolio\Drive_Integration\cpp\host\build\Release\simcore_publisher.exe' `
  --runtime-config 'B:\Portfolio\Drive_Integration\cpp\host\config\signal_city_server.cfg' `
  --map-package 'B:\Portfolio\Drive_Integration\map_packages\signal_city_v2' `
  --no-demo-entities
```

## virtual_city_v1으로 돌아가기

1. PIE를 끝내고 signal-city launcher가 출력한 PID만 종료한다.
2. `.\scripts\run_virtual_city_server.ps1 -Background`를 실행한다.
3. Unreal에서 `/Game/VirtualCity/Maps/L_VirtualCity`를 열고 Play한다.
4. 시작 로그의 map ID가 `virtual_city_v1`, HUD의 map 승인이 정상인지 확인한다.

두 서버를 동시에 실행하거나 한 서버를 둔 채 map만 바꾸지 않는다. Unreal의 기본 시작 맵과
`run_virtual_city_server.ps1`은 계속 v1 경로이며 signal city가 새 기본값이 된 것이 아니다.

## 신호체계 확인

- 남쪽 교차로는 controller 1, 북쪽 교차로는 controller 2가 담당한다. 각 controller는
  동서/남북 group을 독립적인 data-driven phase plan으로 계산한다.
- controller 2에는 cycle 내부 offset이 있으므로 두 교차로의 색이 항상 같아서는 안 된다.
  서로 다른 controller는 같은 순간에 녹색일 수 있다. 같은 controller 안에서는 서로 다른
  group이 동시에 녹색/황색 permissive 상태가 되면 안 된다.
- 현재 package의 controller 1·2 cycle은 모두 36초이고 offset은 각각 0초·9초다. 이 값은
  canonical v2 JSON과 최신 publisher 시작 진단 양쪽에서 일치해야 한다.
- 서버가 보내는 `controller_id`, `group_id`, aspect와 countdown이 권한 상태다. Unreal은
  로컬 신호 주기를 추측하지 않고 표시만 한다. 연결 해제, stale, 잘못된 snapshot 또는
  안전 정지 상태에서는 녹색을 보존하지 않는다.
- runtime 신호 head는 차량 8개·보행 8개이며 `NoCollision` 표시 actor다.
  정지선·횡단보도는 저장 맵 geometry이고,
  플레이어 차량의 신호 위반을 자동 제동하지 않는다.
- server-authoritative NPC 4대는 고정 route로 두 교차로를 통과하고 보행자 8명은 지정
  횡단보도를 보행 신호에 맞춰 횡단한다. 이는 일반 경로 계획·군중 회피·교통 수요 모델
  완료를 의미하지 않는다.

format version 1의 기존 `virtual_city_v1`은 controller 1과 고정 30초 주기를 그대로 유지한다.
version 2의 controller·phase·wire 검증 계약은
[차선·신호 문서](./traffic_network_signals.md#traffic-network-version-2와-다중-controller)를 따른다.

## 빌드와 생성 순서

Editor commandlet을 사용하기 전에 Unreal Editor target과 host를 빌드한다.

```powershell
& 'B:\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat' `
  DriveIntegrationEditor Win64 Development `
  -Project='B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -WaitMutex -NoHotReloadFromIDE

cmake --build 'B:\Portfolio\Drive_Integration\cpp\host\build' `
  --config Release --target simcore_publisher traffic_network_tests `
  vehicle_protocol_tests simulation_host_tests npc_lane_follower_tests
```

저장 맵과 현재 저작 source·MapPackage가 같은지 읽기 검증한다.

```powershell
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -run=BuildVirtualCity -SignalCity -ValidateOnly `
  -unattended -nop4 -NullRHI -NoSound -NoSplash
```

저장된 `L_SignalCity`의 collision을 다시 측정해야 할 때만 `-ValidateOnly` 대신
`-BakeOnly`를 사용한다. 이 모드는 geometry를 재생성하거나 기존 사용자 편집을 덮어쓰지
않는다. `-SignalCity`에서는 v1 전용 `-Sync*` migration과 `-Replace`가 거부된다.

Ground Bake 뒤에는 `source_map_checksum`을 새 collision identity에 맞춰 traffic sidecar를
갱신해야 한다. `traffic_network.json`이 처음 생성되는 경우에만 option 없이 export하고,
이미 존재하면 `-ValidateOnly` 또는 명시적인 `-UpdateExisting`만 사용한다.

```powershell
# 최초 생성: traffic_network.json이 없을 때만
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -run=ExportVirtualCityTraffic -SignalCity -nowrite `
  -unattended -nop4 -NullRHI -NoSound -NoSplash

# 기존 파일 읽기 검증
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -run=ExportVirtualCityTraffic -SignalCity -ValidateOnly -nowrite `
  -unattended -nop4 -NullRHI -NoSound -NoSplash

# Ground Bake 뒤 검증된 topology를 현재 collision에 맞춰 갱신
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -run=ExportVirtualCityTraffic -SignalCity -UpdateExisting -nowrite `
  -unattended -nop4 -NullRHI -NoSound -NoSplash
```

순서는 **맵 저장 → Ground Bake → traffic export/update → 두 commandlet의 ValidateOnly**다.
manifest checksum을 payload와 무관하게 손으로 고치지 않는다. 문서의 생성 기록도 commandlet
검증이 끝난 값만 반영한다. 각 실행의 성공 로그와 최종 `manifest.cfg`, canonical traffic
bytes를 검증 결과의 근거로 남긴다. traffic exporter는 저장 asphalt의 중심·좌우 폭, pole
base와 collision checksum을 확인한 뒤 JSON 하나만 생성·교체한다.

## 자동 검증

```powershell
ctest --test-dir 'B:\Portfolio\Drive_Integration\cpp\host\build' -C Release `
  -R '(traffic_network|vehicle_protocol|simulation_host|npc_lane_follower|npc_host)_tests' `
  --output-on-failure

python -m unittest discover -s 'B:\Portfolio\Drive_Integration\scripts' `
  -p test_smoke_traffic.py
python 'B:\Portfolio\Drive_Integration\scripts\smoke_traffic.py' `
  --map-package 'B:\Portfolio\Drive_Integration\map_packages\signal_city_v2' `
  --traffic-network 'B:\Portfolio\Drive_Integration\map_packages\signal_city_v2\traffic_network.json'

& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -unattended -nop4 -NullRHI -NoSound -NoSplash `
  -ExecCmds='Automation RunTests DriveIntegration.SignalCity;Quit' `
  -TestExit='Automation Test Queue Empty'
```

추가 wire·표시 회귀는 `DriveIntegration.Protocol.TrafficSignals`와
`DriveIntegration.TrafficSignals` filter로 각각 실행한다. 자동 검증의 실제 통과 개수,
생성된 lane/head 개수와 checksum은 해당 실행 로그에서 확인해 작업일지에 기록한다.
2026-09-02 최종 자동 검증은 `signal_city_course`의 5개 검증군, 전체 CTest **20/20**,
UE 전체 Automation **47/47**를 통과했다. 전용 WebSocket smoke는 **2,280 states**에서
NPC 4대·보행자 8명, 차량/보행 신호 8개씩, 순항속도·route/횡단 경계, SafeStop 동결·all-red와
같은 PIE 복구 및 새 PIE reset을 확인했다. 이 smoke는 임시 로컬 port의 자식 서버를 사용해
port 9000 서버를 계속 띄우지 않는다.

SensorRig는 `Config/sensors.json`의 `base_link` FLU mount를 읽어 front camera 10Hz와
roof LiDAR 20Hz의 권한 `SimulationTimeNs` metadata cadence만 만든다. 실제 image/point cloud
payload 캡처는 아직 없다. `F5` 기록은
`Saved/DriveReplays/last_drive.csv`에 권한 snapshot을 저장하고 `F6`는 시각 ghost로
재생한다. command/event를 C++ 물리에 다시 적용하는 결정적 re-simulation은 아니므로
REC-002/AT-07 완료 증거로 사용하지 않는다.

## 사용자 PIE 인수

자동 검사는 실제 화면·입력·성능 인수를 대신하지 않는다. 다음 항목은 PIE에서 별도로
확인하고, 확인 전에는 완료로 표시하지 않는다.

1. 두 교차로의 차량·보행 head가 접근 방향, pole base, 정지선·횡단보도와 맞고 실제
   HUD/낮 화면에서 읽히는지 확인한다.
2. 두 controller가 서로 다른 offset으로 변하고, 같은 controller의 상충 group이 동시에
   permissive 상태가 되지 않는지 한 cycle 이상 관찰한다.
3. NPC 4대가 적색 정지선 전에 멈췄다가 녹색에 재출발하고, 보행자 8명이 자신의 녹색에만
   횡단하며 지정 횡단보도를 벗어나지 않는지 확인한다.
4. Ego로 바깥 loop와 중앙 avenue를 주행해 연석·보도·건물 충돌, 카메라, HUD와 차량
   presentation을 확인한다.
5. End PIE→Play reset, 일시 reconnect와 stale/all-red, `F3` overlay와 `F5/F6` 실제 표시,
   1920×1080 60fps, 30분 안정성을 각각 확인한다.

## 현재 한계

- 신호 도시는 로컬 ENU blockout이다. 실지리, 내비게이션 지도, 최종 메시·조명·랜드마크를
  제공하지 않는다.
- NPC 4대·보행자 8명은 정해진 route/crosswalk를 따르는 제한된 deterministic agent다.
  일반 traffic demand·경로 탐색·군중 회피와 Ego 자동 신호 준수는 범위 밖이다.
- SensorRig는 metadata-only이고 실제 image/point cloud를 만들지 않는다. F5/F6는 상태
  snapshot/시각 ghost이므로 command/event 기반 결정적 물리 재시뮬레이션을 제공하지 않는다.
- 도로는 외곽 벽으로 둘러싸인 직사각형이 아니다. 저작 도로 밖을 무한 지면으로 간주하지
  않으며 MapPackage support가 없는 곳은 물리가 fail-closed할 수 있다.
- heightfield는 단일 상단 표면 snapshot이므로 overpass·터널 같은 다층 도로를 표현하지
  않는다.
- `-BakeOnly`와 traffic hot reload는 최종 아트 변경, 수동 주행 합격이나 서버 binary 교체를
  대신하지 않는다.
