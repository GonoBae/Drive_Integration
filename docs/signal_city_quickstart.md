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
| map checksum | `map_packages/signal_city_v2/manifest.cfg`의 `collision_checksum` |
| traffic checksum | 서버 시작 로그의 traffic checksum, 최신 값은 [9/7 작업일지](./worklogs/2026-09-07.md) |
| host config | `cpp/host/config/signal_city_server.cfg` |
| launcher | `scripts/run_signal_city_server.ps1` |

## 현재 생성물과 검증 기준

8개 접근도로에 좌회전·직진·우회전 전용 3차로가 있다. 9월 7일에는 곡선 연결도로의
아스팔트·표시·보도·NPC 경로를 같은 중심곡선으로 맞췄다. 현재 생성물의 checksum과
검증 결과·백업 위치는 [9/7 작업일지](./worklogs/2026-09-07.md)에서 관리한다.

| 항목 | 값 |
|---|---:|
| static collider | 453(연석 446·건물 벽 7) |
| heightfield spacing | 50cm |
| QA route checkpoint | 453 |
| traffic lane / runtime head / controller | 58 / 16(차량 8·보행 8) / 2 |
| server-authoritative NPC / pedestrian | 10 / 8 |
| 접근도로 | 왕복 20m, 방향별 3개 차로(각 3.2m) |

재Bake 뒤에는 manifest와 두 commandlet의 성공 로그를 다시 확인한 뒤 작업일지의 생성
기록도 함께 갱신한다. traffic JSON의 `source_map_checksum`은 manifest의 collision checksum과
일치하고, traffic network checksum은 JSON
전체 바이트의 별도 identity다.
9/2 당시 publisher build의 Health와 traffic v2 WebSocket smoke는 통과했다. 서버는 아래
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
   `fnv1a64:86c3103f3e2c7a5b`, traffic checksum `fnv1a64:b19c15afba6936d7`,
   `lanes=58 signal_heads=16`을 확인한다. 같은 `[Traffic]` 줄의 controller 진단은
   `id=1,cycle_seconds=72,offset_seconds=0`과
   `id=2,cycle_seconds=72,offset_seconds=14`여야 한다.
   `virtual_city_v1`, lanes25 또는 heads3가 보이면 이전 서버이므로 Play하지 않는다.
4. Unreal Editor에서 `/Game/SignalCity/Maps/L_SignalCity`를 직접 열고 Play한다.
5. HUD의 연결·map 승인과 `Active`를 확인한다. 차량은 중앙 avenue의 ENU `(0,0)`에서
   북쪽을 향해 시작한다. `W/S` 가감속·후진, `A/D` 조향, `Space` 후륜 사이드 브레이크,
   `H` 클락션, 마우스 orbit, 휠 zoom, `C` 카메라 복귀를 사용한다. `1/2/3/4`는 각각
   세단/경차/트럭/오토바이를 선택한다. 다른 차종을 선택하면 현재 입력을 비우고 새
   PlaySession으로 연결해 출발점에서 다시 시작한다. `F3`는 차량 collision/contact 디버그
   오버레이, `R`은 권한 snapshot CSV 기록, `F6`는 마지막 CSV의 collision-free 시각 ghost
   재생이다.
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

- 남쪽 교차로는 controller 1, 북쪽 교차로는 controller 2가 담당한다. 각 진입방향의
  좌/직/우 전용 차로는 그 방향의 공통 보호 녹색을 사용한다. 다른 세 방향은 적색이다.
  좌회전 화살표 신호만 별도 허용하는 체계는 아니며, `L/S/R` 공동 보호 표시를 사용한다.
- controller 2에는 cycle 내부 offset이 있으므로 두 교차로의 색이 항상 같아서는 안 된다.
  서로 다른 controller는 같은 순간에 녹색일 수 있다. 같은 controller 안에서는 서로 다른
  차량 group이 동시에 녹색/황색 permissive 상태가 되면 안 된다. 전부 보행자 group인
  독립 WALK 현시만 동시에 허용하며 이때 같은 교차로의 모든 차량 신호는 적색이다.
- 현재 package의 controller 1·2 cycle은 모두 72초이고 offset은 각각 0초·14초다. 이 값은
  canonical v2 JSON과 최신 publisher 시작 진단 양쪽에서 일치해야 한다.
- 서버가 보내는 `controller_id`, `group_id`, aspect와 countdown이 권한 상태다. Unreal은
  로컬 신호 주기를 추측하지 않고 표시만 한다. 연결 해제, stale, 잘못된 snapshot 또는
  안전 정지 상태에서는 녹색을 보존하지 않는다.
- runtime 신호 head는 차량 8개·보행 8개이며 `NoCollision` 표시 actor다.
  정지선·횡단보도는 저장 맵 geometry이고,
  플레이어 차량의 신호 위반을 자동 제동하지 않는다.
- 횡단보도는 교차로 중심에서 15m 밖, 정지선은 18.5m 밖에 있다. 서버의 controlled
  endpoint도 18m 앞이므로 NPC가 횡단보도 위에 정차하지 않는다. 보행 대기점은 실제
  보도 위이며, 왕복 offset ±0.45m와 몸 반경 0.35m까지 보도 안에 있어야 한다.
  23m 횡단에는 약 17.04초가 필요하다. WALK 잔여 시간이 전체 횡단+0.2초보다 짧으면
  새 출발하지 않으며, 이미 시작한 횡단만 완주한다.
- server-authoritative NPC 10대는 `npc_autonomous=true`에서 목적지를 선택하고 합법적인
  successor 경로·같은 방향 차선 변경 구간을 사용한다. 막힌 미래 lane은 같은 목적지로
  우회하며 신호 대기열을 폐쇄 도로로 취급하지 않는다. 보행자 8명은 지정 횡단보도·보행
  신호를 유지한다. [NPC 내비게이션 범위·안전 계약](./npc_navigation.md)을 따른다.
- 최신 `L_SignalCity`와 MapPackage를 함께 사용하면 추가 Bake 없이 시험한다. 이전의 한 차로
  저장 맵을 계속 열면 안 된다. 저작 원본에서 다시 적용할 때만 아래 보호 migration을 실행한다.
  cfg의 두 route는 초기 배치 경로이며 시작 offset 19m, 90m 간격으로 최대 10대를 배치한다.
  연결곡선 보정으로 route 길이가 바뀌어 종전 20m offset은 NPC1009를 정지선 안전 구간
  안에 생성하려 했다. 현재 offset은 10대 모두의 정지 여유를 preflight에서 검사한다.

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
단, 이번 3차로 변경에는 다음 전용 명령을 사용한다.

```powershell
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -run=BuildVirtualCity -SignalCity -SyncTrafficLanes `
  -unattended -nop4 -NullRHI -NoSound -NoSplash
```

이 모드는 기존 generated tag와 이전 도로·충돌체의 정확한 geometry를 먼저 검증한다.
사용자 편집이나 이름 충돌이 발견되면 중단하며, 성공 시 `.umap`과 MapPackage 6개 파일을
`Saved/Backups/SignalCityTraffic-<날짜>-<GUID>`에 복사한 뒤 생성 도로·연석·표시만 갱신한다.
사용자 actor를 삭제하지 않는다. 최초 3차로 변경 전 백업은
`unreal/DriveIntegration/Saved/Backups/SignalCityTraffic-20260903-181259-460BE41D`,
보도 대기점·정지선 보정 전 백업은
`unreal/DriveIntegration/Saved/Backups/SignalCityTraffic-20260903-183949-6E097719`다.
갱신 후에는 아래 `-UpdateExisting` traffic export를 이어서 실행해야 한다.

collector 전환부는 저장된 생성 버전에 맞는 원본과 비교한 뒤 백업·갱신한다. 수동으로
옮긴 표시나 충돌하는 사용자 actor가 있으면 덮어쓰지 않고 중단한다. 최신 합류 표시에서는
내부 구분선 두 개가 기존 가장자리 선으로 차례로 모인다. 실행 뒤에는 네 collector 접속부의
중앙선·구분선·가장자리 선이 끊기거나 서로 교차하지 않는지 PIE에서도 확인한다.

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
  -R '(traffic_network|vehicle_protocol|simulation_host|npc_.*)_tests' `
  --output-on-failure

python -m unittest discover -s 'B:\Portfolio\Drive_Integration\scripts' `
  -p test_smoke_traffic.py
python -m unittest discover -s 'B:\Portfolio\Drive_Integration\scripts' `
  -p test_smoke_signal_city.py
python 'B:\Portfolio\Drive_Integration\scripts\smoke_signal_city.py'
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
payload 캡처는 아직 없다. `R` 기록은
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
3. NPC 10대가 적색 정지선 전에 멈췄다가 녹색에 재출발하고, 보행자 8명이 자신의 녹색에만
   횡단하며 지정 횡단보도를 벗어나지 않는지 확인한다.
4. Ego로 바깥 loop와 중앙 avenue를 주행해 연석·보도·건물 충돌, 카메라, HUD와 차량
   presentation을 확인한다.
5. End PIE→Play reset, 일시 reconnect와 stale/all-red, `F3` overlay와 `R/F6` 실제 표시,
   1920×1080 60fps, 30분 안정성을 각각 확인한다.
6. 운행 가능한 NPC에 중간 정도 충돌을 내고 차체색 문 열림 → 양발을 차례로 문턱 밖
   지면에 디딤 → 하차 → 문 닫힘 → 한 손 항의 순서를 확인한다. 경사·연석에서도 발이
   뜨지 않아야 하며, 복구 때는 반대 순서로 좌석에 돌아가야 한다.
7. 보행자를 저속·중속으로 충돌해 임의로 위로 발사되지 않고 중력으로 낙하하는지,
   완전히 넘어진 뒤 평지·경사·연석에서 각 신체 부위가 지면 위에 떠 있거나 관통하지
   않는지 확인한다.
8. 경미한 사고 NPC가 복구할 때 옆으로 미끄러지거나 순간이동하지 않고 차체가 향한
   방향으로 전진 또는 후진하며 경로에 합류하는지 확인한다.
9. 긴 다차로 접근로에서 Ego로 앞길을 막고 뒤 NPC가 안전한 인접 차로로 변경해 실제로
   지나가는지 확인한다. 단일 차로와 교차로 충돌 영역에서는 중앙선을 넘지 않는 것이 정상이다.
   쓰러진 보행자가 있을 때도 확인한다. 옆 차로와 후방을 비운 경우 실제로 우회해야 하며,
   옆 차로나 후방까지 막은 경우에는 사람을 밀어붙이지 않고 대기해야 한다.
   같은 사고 보행자에게 경적을 반복하는지도 함께 확인한다.
10. 모든 직선·곡선·collector 전환 도로 구간에 중앙선과 양쪽 가장자리 표시가 이어지는지
    확인한다. 다차로 접근로에는 점선 차로 구분이 있어야 하며 교차로 중심 충돌 영역에는
    연속 중앙선이 없는 것이 정상이다.
    특히 3차로에서 1차로로 줄어드는 곳은 내부 구분선이 갑자기 끝나지 않고 합류 구간을
    따라 모여야 한다. 곡선에서 반대편 선을 가로지르거나 노면 밖으로 나가면 안 된다.
11. `1/2/3/4`로 세단·경차·트럭·오토바이를 차례로 선택한다. 선택할 때마다 출발점으로
    초기화되고, 화면 차체뿐 아니라 가속·제동·조향과 충돌 크기도 해당 차종에 맞게 바뀌는지
    확인한다.

## 현재 한계

9/3 충돌·교통 후속 조작: `Q` 좌측 깜빡이, `E` 우측 깜빡이, `X` 비상등(재입력으로 끔).
[8개 피드백 확인표와 구현 경계](./traffic_crash_feedback.md)를 함께 따른다.
NPC 깜빡이는 서버가 차선 변경·회전 의도에 맞춰 켜며 운행 불가 차량은 비상등을 켠다.
9/4 후속 조작: `H`는 플레이어 차량의 위치 기반 클락션이다. NPC는 10m 안의 고정 장애물
앞에서 1.5초 이상 정체되거나 충돌 예상시간이 0.8초 이하일 때만 울리며, 신호 대기와
일반 차량 행렬에서는 울리지 않는다. 같은 위험에는 5초 쿨다운과 재발 방지 latch를 쓴다.
쓰러진 보행자는 별도로 구분해 동일 대상에 경고 한 번만 허용한다. 이후에는 안전한
우회·후진을 검토하고, 통과할 공간이 없으면 반복 경적 없이 대기한다.
차량 창문은 실제 개구부와 투명 유리를 사용하며 Ego/NPC 운전자가 보인다. 큰 충격이나
손상에서는 운전자 머리와 상체가 핸들 쪽으로 숙여지는 경량 부상 자세를 표시한다.
방향지시등은 렌즈 발광과 국부 조명을 함께 사용한다. 작은 접촉만으로 운전자가 숙여지지
않으며, 운행 가능한 NPC가 중간 정도 사고 뒤 정차하면 운전석 밖으로 나와 항의하는 경량
표현을 한다. 이 동작은 별도 보행 AI가 아니라 사고 정차 상태에 종속된 표시다. 실내에는
앞·뒤 좌석, 바닥, 센터 콘솔, 양쪽 도어 트림과 계기판 블록아웃이 포함된다.
`F3`에서 Ego뿐 아니라 NPC·보행자의 서버 충돌체와 몸통 중심을 확인할 수 있다.
누운 사람은 낮은 OBB여야 하며 서 있는 캡슐이 남으면 안 된다. 앞선 도로 생성과 traffic
export는 검증된 상태이므로 별도 Ground Bake 없이 Play한다. 저장 맵 갱신이 필요한 경우에만
위 `-SyncTrafficLanes`의 보호 검증·백업을 거친다.

플레이어 차종은 Unreal 화면만 바꾸는 옵션이 아니다. `SimulationReset`에서 요청한 차종을
서버가 검증하고 물리 profile을 교체한 뒤, 권한 `WorldState`의 Ego 차종을 Unreal이 받아
차체·바퀴·램프 배치를 바꾼다. 같은 PlaySession의 재연결은 기존 자세와 차종을 보존하며,
차종 변경은 새 PlaySession을 사용한다. 오토바이는 앞·뒤 중심선 접점과 회전 안쪽으로의
균형 제어를 쓰는 축약 모델이다. 조향축·자이로까지 계산하는 완전한 이륜 동역학은 아니다.

운전석 문은 별도 authored static mesh와 실제 차체 개구부를 사용하지만 Chaos 관절 문은
아니라 표시용 힌지 회전이다. 하차·항의는 양발 IK를 포함한 절차적 포즈다. 보행 충돌은
서버의 축약 몸통과 UE 래그돌 조합이며 완전한
생체역학은 아니다. 사고차 복귀는 충돌 검사를 거친 저속 전·후진 재합류이고, 우회는
저작된 안전한 인접 차로에서만 허용한다. 도로 표시는 모든 저작 직선·곡선 구간을 덮지만
교차로 충돌 영역에는 의도적으로 연속선을 그리지 않는다.

- 신호 도시는 로컬 ENU blockout이다. 실지리, 내비게이션 지도, 최종 메시·조명·랜드마크를
  제공하지 않는다.
- NPC 10대는 저작된 그래프 안에서 목적지·우회·차선 변경을 결정하는 제한된 규칙 AI다.
  보행자 8명은 지정 crosswalk를 유지한다. 실제 센서 인식·traffic demand·군중 회피,
  Ego 자율주행과 NPC 전체 타이어/서스펜션 동역학은 범위 밖이다.
- SensorRig는 metadata-only이고 실제 image/point cloud를 만들지 않는다. R/F6는 상태
  snapshot/시각 ghost이므로 command/event 기반 결정적 물리 재시뮬레이션을 제공하지 않는다.
- 도로는 외곽 벽으로 둘러싸인 직사각형이 아니다. 저작 도로 밖을 무한 지면으로 간주하지
  않으며 MapPackage support가 없는 곳은 물리가 fail-closed할 수 있다.
- heightfield는 단일 상단 표면 snapshot이므로 overpass·터널 같은 다층 도로를 표현하지
  않는다.
- `-BakeOnly`와 traffic hot reload는 최종 아트 변경, 수동 주행 합격이나 서버 binary 교체를
  대신하지 않는다.
