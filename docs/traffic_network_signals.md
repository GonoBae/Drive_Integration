# 가상 도심 LaneGraph·신호 기반

## 범위와 현재 상태

- 기준일: 2026-08-31, 다중 controller 확장 2026-09-02. 이 문서는 구현 계약과 검증 절차를 기록한다. 실행 시험의 통과 여부와 실제 사용자 인수 결과는 작업일지에 별도로 기록한다.
- `virtual_city_v1`의 현재 범위는 **25개 방향성 lane segment, 신호 head 3개·phase group 2개, 30초 신호 상태기계와 Unreal 표시**다.
- `signal_city_v2`는 별도 맵·MapPackage와 traffic format version 2를 사용해 42개 lane,
  8개 runtime head와 두 교차로 controller의 phase plan·offset을 data-driven으로 확장한다.
  2026-09-02 재생성·exact validation 기준이며, 기존 v1의 생성물·고정 주기·호환 동작을
  바꾸지 않는다. 실행 순서는 [signal city 빠른 시작](./signal_city_quickstart.md)을 따른다.
- `drive_route.csv`는 차량 물리 완주를 확인하는 QA 경로다. 이번 `traffic_network.json`은 별도로 작성한 차선·교차로 연결 데이터이며 두 파일을 같은 기능으로 취급하지 않는다.
- 후속으로 **NPC 1대의 차선 추종·신호 정지/재출발·전방 장애물 정지**를 추가했다([NPC 계약](./npc_lane_following.md)). NPC 3~4대와 보행자 6~8명·전체 TrafficDirector는 아직 완료가 아니다. 기존 opt-in 고정 demo entity와 구분한다.
- 플레이어 차량은 계속 사용자가 조작한다. **적색 신호에 Ego 차량을 자동 제동하거나 신호 위반을 강제로 막지 않는다.** 신호가 바뀌는 것과 차량 AI가 신호를 준수하는 것은 별도 구현이다.

## 책임 분리

| 영역 | 담당 | 현재 동작 |
|---|---|---|
| 차선·교차로 저작 | Unreal `SimCoreVirtualCityTrafficLayout`, `SimCoreSignalCityTrafficLayout` | 저장 도로와 같은 저작 좌표에서 방향성 중심선·폭·속도·successor·stopline group 생성 |
| 실제 도로 정합성 | Unreal export + C++ `GroundQuery` | Unreal은 저장 맵의 asphalt 충돌을 확인하고, C++는 실제 MapPackage에서 중심·좌우 폭·선분 중간의 지면 존재와 높이를 재검증 |
| 신호 시간·상태 | C++ `TrafficNetwork::signals_at()` | authoritative simulation time과 활성/안전 상태로 v1 고정 주기 또는 v2 phase plan의 신호·남은 시간 계산 |
| 신호 전달 | 공통 Protobuf `WorldState` | Ego pose·Health·신호 head·network checksum이 하나의 sequence/map/play identity를 공유 |
| 화면 표시 | Unreal `ASimCoreTrafficSignalActor` | 서버 aspect와 pose를 표시. 독립적인 로컬 신호 주기로 녹색을 추측하지 않음 |

신호 actor는 `NoCollision`이다. 표시 추가가 기존 ground/static collider checksum이나 차량 충돌 형상을 바꾸지 않는다. 신호 actor는 게임 world에서 수신 상태에 따라 생성되는 표시용 객체이며, 저장 맵에 새 영구 충돌체를 배치하는 기능이 아니다. 이번 정지선은 lane 끝의 제어 metadata이며, 본선의 새 정지선 페인트는 아직 추가하지 않았다. 기존 맵의 지선 정지선 표시는 그대로 보존했다.

## 저작 topology

주행 루프의 양방향을 별도 방향성 lane으로 표현하고, T 교차로의 접근 구간과 outgoing connector를 분리한다. 총 25개는 차량 대수나 물리 도로 개수가 아니라 **방향·분기·정지선으로 나눈 graph segment 수**다.

| 제어 지점 | 접근 lane | group | head | 허용 연결 |
|---|---:|---:|---:|---|
| 본선 동쪽 진행 | 100 | 1 | 1 | 직진 connector 110 |
| 본선 서쪽 진행 | 200 | 1 | 2 | 직진 210, 지선 우회전 300 |
| 지선에서 본선 진입 | 310 | 2 | 3 | 우회전 320, 좌회전 330 |

- `signal_group_id`는 해당 lane **끝 정지선을 넘어가는 진입**을 제어한다. 교차로를 이미 지난 connector에서 뒤늦게 멈추라는 뜻이 아니다. Connector의 group은 0이다.
- 본선 동쪽 진행의 지선 좌회전은 group 1의 대향 직진과 충돌하므로 현재 저작하지 않는다.
- 지선의 도착 lane 340은 명시적인 terminal이다. 막다른 bay에서 들어오는 lane 310으로 임의의 U-turn 연결을 만들지 않는다. 향후 NPC spawn·경로 선택은 이러한 진입/종료 조건을 따로 처리해야 한다.
- Signal pose는 ENU meter 단위의 **기둥 바닥 위치**다. `heading_deg`는 North=0°, East=90°의 접근 차량 진행 방향이며, 렌즈가 바라보는 방향과 혼동하지 않는다.

## 30초 신호 주기

시작 시간은 새 PIE의 simulation reset으로 0이 된다. 구간 끝은 다음 구간에 속한다.

| 주기 내 시간 | 본선 group 1 | 지선 group 2 | 길이 |
|---|---|---|---:|
| `[0, 2)`초 | 적색 | 적색 | 2초 clearance |
| `[2, 14)`초 | 녹색 | 적색 | 12초 |
| `[14, 17)`초 | 황색 | 적색 | 3초 |
| `[17, 19)`초 | 적색 | 적색 | 2초 clearance |
| `[19, 27)`초 | 적색 | 녹색 | 8초 |
| `[27, 30)`초 | 적색 | 황색 | 3초 |

- 주기는 integer nanosecond에서 먼저 `elapsed_ns % 30,000,000,000`으로 계산한다. 긴 uptime을 먼저 실수로 바꿔 경계 시간이 반올림되는 문제를 피한다.
- `remaining_seconds`는 **해당 head의 현재 색이 바뀌기까지** 남은 시간이다. 예를 들어 17초의 group 1은 다음 주기 2초까지 적색이므로 15초를 표시한다.
- 새 PIE는 시간 0·전부 적색으로 시작한다. 같은 PIE에서 소켓만 재연결하면 물리/신호 시간을 되감지 않는다.
- 아직 reset/control이 없거나 `safe_stop`, `reconnect_required`, EStop 상태이면 서버는 신호를 전부 적색으로 내보낸다. 이 비활성 적색의 countdown은 0으로, 재개 시간을 임의로 예고하지 않는다.
- Unreal은 잘못된 패킷, 오래된 상태, 연결 해제 또는 알 수 없는 aspect를 녹색으로 표시하지 않는다. Parser는 같은 controller의 서로 다른 group이 동시에 permissive인 상태와 같은 `(controller, group)` head 간 불일치를 거부한다.
- 100ms stale는 Unreal 게임스레드의 수신 처리 시각부터 측정한다. 종단 전송 지연의 상한 보장은 아니며, tick/수신 callback이 멈춘 pause·hitch 중 표시가 즉시 갱신된다는 보장도 아니다.
- JSON의 double 방향값이 float wire field에서 360°로 반올림되는 경우에는 같은 방향인 0°로 내보내 `[0,360)` 계약을 유지한다. 원본 360°나 음수는 계속 거부한다.

## Traffic network version 2와 다중 controller

`signal_city_v2/traffic_network.json`은 root에 `signal_plans`를 추가하고 각 signal head에
`controller_id`를 명시한다. Plan은 `id`, `offset_ms`, 소유 `groups`, 순서가 있는 `phases`를
가지며, phase는 `duration_ms`, `green_groups`, `yellow_groups`를 가진다. 목록에 없는 소유
group은 해당 phase에서 적색이다. server simulation clock에 plan offset을 적용한 결과가
권한 상태이며 Unreal은 JSON을 직접 읽거나 phase를 다시 계산하지 않는다.

- v2 controller ID는 `1..64`, group ID는 `1..4096`이다. v1은 controller 1과 group 1·2의
  기존 30초 주기를 유지한다.
- v2 저작 JSON에서는 각 group을 하나의 plan이 전역 소유한다. controlled lane의 group과
  signal head의 `(controller_id, group_id)`는 그 소유 관계와 일치해야 한다.
- 각 plan에는 최소 한 group과 phase가 필요하다. phase duration은 `100..120000ms`, plan
  offset은 그 plan의 cycle보다 작아야 한다. 녹색·황색 목록은 plan 소유 group만 참조하고
  한 phase에서 같은 group을 둘 다 포함할 수 없다.
- generic v2 loader의 plan cycle 상한은 3,600,000ms(1시간)다. 이에 맞춰 C++ serializer와
  Unreal parser는 finite `remaining_seconds`를 `0..3600`초에서만 허용한다. v1 generator의
  실제 countdown은 기존 30초 주기 안의 값으로 계속 제한되며 v1 주기를 늘린 것이 아니다.
- controller마다 cycle과 offset을 독립 계산하므로 서로 다른 controller는 동시에 녹색일 수
  있다. 같은 controller 안에서 서로 다른 group이 동시에 녹색/황색이면 fail-closed다.
- Protobuf `TrafficSignalState.controller_id`는 additive field 7이다. 기존 v1 snapshot 또는
  해당 field가 없는 legacy wire 값은 controller 1로 해석한다. 이 추가만으로 Hello schema나
  capability 문자열을 변경하지 않는다.
- wire signal ID는 controller와 무관하게 전체 snapshot에서 고유해야 한다. 모든 head의
  `(controller_id, group_id)` 단위 aspect와 countdown은 일치해야 한다. wire validator는
  controller가 다른 동일 숫자 group을 독립 key로 처리하지만, 현재 v2 JSON loader의 저작
  규칙은 group의 plan 간 전역 중복을 더 엄격하게 거부한다.
- parse, ownership, 범위, 동시 permissive 또는 countdown 일관성 검증에 실패하면 snapshot
  전체를 적용하지 않는다. stale·disabled 상태의 표시도 기존처럼 적색/countdown 0이다.

## 파일과 검증 계약

기존 기본 파일은 `map_packages/virtual_city_v1/traffic_network.json`이다. JSON v1의 root는
`format_version`, `source_map_checksum`, `lanes`, `signals` 네 field만 허용하고
`format_version`은 1이다. 문서에는 예시 checksum을 넣지 않는다. 실제 값은 검증된
MapPackage manifest에서 exporter가 가져오며, 실제 loader는 lane이 하나 이상 있어야 한다.

두 version의 Lane은 `id`, `width_m`, `speed_limit_mps`, `signal_group_id`, `terminal`, `points`, `successors`를 가진다. `points`는 `[East, North, Up]`의 배열이다. v1 Signal은 `id`, `group_id`, `position_enu`, `heading_deg`, v2 Signal은 여기에 필수 `controller_id`를 가진다. v2 root에는 필수 `signal_plans`가 추가된다. v1에 v2 field를 섞거나 v2에서 이를 누락하면 거부한다.

주요 fail-closed 검증은 다음과 같다.

- unknown/missing/duplicate 필드, 잘못된 JSON primitive/container 타입, NaN·Inf·범위 초과 숫자를 거부한다. Boost property_tree가 `"1"`과 `1`, `{}`와 `[]`를 혼동하지 않도록 별도 JSON type/shape 검증을 먼저 수행한다.
- 파일 최대 4MiB, lane 최대 256개, signal 최대 32개, lane당 point 2~2,048개·전체 16,384개, lane당 successor 최대 8개로 제한한다. 깊이/node 예산과 ground query 총 100,000회 예산도 별도로 둔다.
- Lane·signal ID는 0이 아닌 uint32이고 각 종류에서 중복될 수 없다. v2 controller/group 범위와 ownership은 위 다중 controller 계약을 따른다. Lane 폭은 2~8m, 제한속도는 `0 < v <= 55.6m/s`, ENU 좌표 절댓값은 1,000,000m 이하다.
- 선분은 수평 길이가 0.05m 이상이고 3D 길이가 0.05~100m여야 한다. successor ID가 존재해야 하며 앞 lane 끝과 다음 lane 시작은 3D 거리 0.15m 이내여야 한다.
- terminal은 successor가 없을 때만 true다. Controlled stopline은 같은 group의 실제 head와 수평 거리 15m 이내여야 하며 고아 head도 거부한다.
- C++는 각 선분을 최대 약 1m 간격으로 나눠 중심과 좌우 반폭 위치를 실제 `GroundQuery`로 확인한다. 지면 누락·잘못된 hit·높이 차이 0.20m 초과를 거부한다. 이는 표본 기반 corridor 검증이며 임의의 세밀한 메시 전체를 해석하는 연속 충돌 증명은 아니다.
- Unreal exporter는 이보다 앞서 저장 맵의 asphalt 충돌과 0.08m 높이 허용치로 같은 중심/폭 표본을 확인한다. 기둥 바닥도 저장 ground에 맞춘다.

두 checksum은 목적이 다르다.

| 값 | 의미 |
|---|---|
| JSON의 `source_map_checksum` | 저작 lane이 어느 collision MapPackage에 정합되는지 지정 |
| 서버가 계산하는 `TrafficNetwork.checksum` | traffic JSON **파일 전체 바이트**의 FNV-1a64. 신호 snapshot의 `traffic_network_checksum`으로 전달 |

공백/줄바꿈을 바꾸면 network checksum도 달라진다. Export는 ID/key 순서·소수 표현·LF·UTF-8 without BOM을 고정하고 timestamp를 넣지 않는다. FNV는 재현 가능한 변경 식별용이며 암호학적 서명/인증을 대신하지 않는다.

## 실행 설정과 hot reload

Runtime cfg의 `traffic_network`는 선택 키다. 기존 cfg에 없어도 기존 수동운전 경로는 유지된다. 가상 도심 cfg에는 다음 상대 경로가 들어간다. cfg 상대 경로는 **cfg 파일 디렉터리 기준**이다.

```ini
traffic_network=../../../map_packages/virtual_city_v1/traffic_network.json
```

CLI의 `--traffic-network PATH`가 cfg보다 우선한다. 일반적인 가상 도심 서버 실행은 다음과 같다. 이미 서버가 실행 중이면 중복 실행하지 않는다.

```powershell
& 'B:\Portfolio\Drive_Integration\cpp\host\build\Release\simcore_publisher.exe' --runtime-config 'B:\Portfolio\Drive_Integration\cpp\host\config\virtual_city_server.cfg'
```

직접 실행 파일을 호출하므로 PowerShell 스크립트 execution policy를 바꿀 필요가 없다.

다중 교차로 코스는 `cpp/host/config/signal_city_server.cfg`와
`scripts/run_signal_city_server.ps1`을 사용한다. 이 launcher는 `signal_city_v2`의 collision
payload와 v2 traffic JSON을 모두 preflight하고, port를 점유한 기존 서버를 임의로 종료하지
않는다. 맵·생성·실행의 전체 순서는 [signal city 빠른 시작](./signal_city_quickstart.md)을
따른다. v1 config에 v2 traffic을 섞거나 반대로 사용하면 map checksum gate가 거부해야 한다.

서버는 traffic 파일과 MapPackage manifest를 약 250ms 간격으로 감시한다. 별도 worker에서 map payload·traffic JSON·ground corridor를 완전히 검증하고, 완성된 immutable network를 큐에 넣는다. Physics tick에서는 활성 map checksum과 맞는 후보만 교체한다. JSON 파싱이나 수만 회의 지면 query를 고정 tick 안에서 실행하지 않는다.

파일의 수정 시각/크기가 달라지면 새 후보를 확인하고 **성공한 load만** 처리 완료로 기록한다. Windows 공유 위반이나 교체 경합 같은 일시 실패는 파일 signature가 그대로여도 250ms부터 최대 5초의 backoff로 재시도한다. 같은 오류 로그는 반복 출력하지 않는다. 따라서 일시적인 읽기 실패 한 번이 다음 파일 변경 전까지 복구를 막아서는 안 된다.

| 상황 | 처리 |
|---|---|
| 시작할 때 traffic 파일이 없거나 무효 | traffic만 비활성화하고 수동운전 서버는 시작. 유효한 파일의 등장/변경을 감시 |
| 실행 중 traffic 교체본이 무효 | 무효 후보는 적용하지 않음. 기존 검증 객체를 보존 |
| 지면 재Bake로 활성 collision checksum 변경 | 기존 traffic과 map identity가 달라지므로 기존 head를 적색/countdown 0으로 유지 |
| 새 map에 정합된 유효 traffic 도착 | worker 검증 후 tick 경계에 적용. 활성 control 등 정상 조건을 만족하면 simulation time에 따른 신호 재개 |

**Traffic 불일치가 수동운전을 영구 차단하거나 서버 재시작을 요구하지 않는다.** 다만 지면 교체 자체의 기존 안전 절차인 socket fence·자동 재연결·새 PlaySession reset은 유지된다. “재Bake 중에도 한 프레임의 중단 없이 계속 달린다”는 의미는 아니다. 기존 파일의 삭제/무효 교체가 현재 map과 여전히 일치하는 마지막 검증 network를 자동 폐기하는 기능도 아니다.

## 최초 export·검증·명시적 갱신

`ExportVirtualCityTraffic`는 기본적으로 기존 `L_VirtualCity` 저장 맵과 검증된 `virtual_city_v1`을 읽어
traffic JSON만 최초 생성·검증·명시적으로 갱신한다. 맵 생성·저장·ground 재Bake·static
collider 변경을 수행하지 않는다.

`-SignalCity`를 추가하면 같은 안전 규칙으로 `L_SignalCity`와 `signal_city_v2`를 선택하고
format version 2를 canonical export한다. option 순서와 최초 생성/검증/갱신 명령은
[signal city 빠른 시작](./signal_city_quickstart.md#빌드와-생성-순서)에 따로 적는다.

최초 생성은 `traffic_network.json`이 **없는 경우에만** 허용한다.

```powershell
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' -run=ExportVirtualCityTraffic -nowrite -unattended -nop4 -NullRHI -NoSound -NoSplash
```

이미 파일이 있으면 다음 읽기 검증만 사용한다.

```powershell
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' -run=ExportVirtualCityTraffic -ValidateOnly -nowrite -unattended -nop4 -NullRHI -NoSound -NoSplash
```

Ground Bake로 collision checksum이 바뀌고 저장 도로·topology 정렬을 다시 확인했다면 기존
파일을 삭제하지 않고 다음 명시적 갱신을 사용한다.

```powershell
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' -run=ExportVirtualCityTraffic -UpdateExisting -nowrite -unattended -nop4 -NullRHI -NoSound -NoSplash
```

- `-nowrite`는 Unreal config의 부수적 저장을 방지하기 위한 필수 인자다. 최초 export의 명시적인 JSON 생성까지 금지한다는 뜻은 아니다.
- 기존 JSON은 `-ValidateOnly`에서 저장 맵으로부터 계산한 canonical bytes와 정확히 비교한다. 무효 파일을 고쳐 덮어쓰지 않는다.
- `-UpdateExisting`은 기존 파일이 있을 때만 허용한다. canonical UTF-8 JSON을 같은 directory의
  임시 파일에 완성한 뒤 traffic JSON만 교체하며 맵·Ground package·static collision을
  변경하지 않는다. `-ValidateOnly`와 동시에 사용할 수 없다.
- `-Replace`는 지원하지 않고 거부한다. 기존 파일을 삭제하여 강제로 다시 생성하는 절차를 사용자 안내로 사용하지 않는다.
- 저장 `VirtualCityGround`가 저작 source의 component 이름/개수/transform/tag와 다르면 export를 거부한다. 임의로 변경한 도로를 자동으로 다시 추론하는 도구가 아니다.
- **일반 Bake Ground는 traffic sidecar를 자동 갱신하지 않는다.** Collision checksum이 바뀐
  뒤 현재 traffic은 적색이 될 수 있다. 변경 도로와 topology를 다시 확인한 뒤에만 위
  `-UpdateExisting`을 사용한다. Runtime hot reload와 저작 데이터 갱신을 혼동하지 않는다.

2026-09-02 보도 지지면을 연석 상단 24cm와 맞춰 재Bake한 뒤 collision checksum은
`fnv1a64:942842ea8d76b7a2`, traffic network checksum은
`fnv1a64:32f81819cb6b2b0f`이며 `-ValidateOnly` exact 비교를 통과했다.

## 검증 절차와 인수 경계

아래는 실행 방법/확인 항목이다. **명령이 존재하거나 코드가 작성되었다는 사실만으로 통과 처리하지 않는다.** 실제 결과·로그 경로는 작업일지에 남긴다.

### 자동 검사

```powershell
ctest --test-dir 'B:\Portfolio\Drive_Integration\cpp\host\build' -C Release -R traffic_network_tests --output-on-failure
python -m unittest discover -s 'B:\Portfolio\Drive_Integration\scripts' -p test_smoke_traffic.py
python 'B:\Portfolio\Drive_Integration\scripts\smoke_traffic.py'
python 'B:\Portfolio\Drive_Integration\scripts\smoke_traffic.py' `
  --map-package 'B:\Portfolio\Drive_Integration\map_packages\signal_city_v2' `
  --traffic-network 'B:\Portfolio\Drive_Integration\map_packages\signal_city_v2\traffic_network.json'
```

- C++ `traffic_network_tests`: strict schema·중복·숫자·topology·stopline·GroundQuery 중심/폭/중간·resource budget·phase 경계·큰 timestamp·reset/비활성 상태의 fixture 검사.
- Host의 `test_traffic_lifecycle_and_map_reload`: 새 PIE·map mismatch 적색·tick 경계 교체·stale 후보·EStop 동작 검사. WorldState serializer/Unreal parser는 신호 ID·pose·checksum·group 동시 진행·중복/잘못된 field를 검사한다.
- Python `test_smoke_traffic.py`: 다른 서버 nonce/누락 capability에는 제어 프레임을 보내지
  않는 방어, v1/v2 expected JSON/phase와 원자 WorldState helper를 검사하며 최신 7/7이
  통과했다. 이 단위 검사는 서버 프로세스나 소켓을 생성하지 않는다.
- 실제 `smoke_traffic.py`: 랜덤 localhost port의 **자체 생성 child**만 사용한다. 사용자 기본
  port 9000에 연결하지 않으며, server nonce·map·capability 확인 전 Hello/Reset/control을
  보내지 않는다. 기본 v1은 3 heads/두 groups의 고정 주기를, v2 option은 JSON의 모든
  controller/group plan과 offset을 직접 읽어 검증한다. 최신 `signal_city_v2` 실행은 38초,
  2,281 atomic WorldStates, 8 heads/4 groups에서 controller 내부 충돌 없음과 서로 다른
  controller의 simultaneous permission을 실제 관측했다. 두 mode 모두 새 PIE all-red/시간
  초기화, soft timeout 적색, hard timeout 1008, 동일 PIE reconnect 시간 보존을 확인하고
  자신의 child만 종료한다.
- 최종 v2 근거는 `runtime_logs/traffic-smoke-20260902-110742-ed34c190.*.log`다. 시작 진단은
  controller 1·2를 각각 `cycle_seconds=36`, `offset_seconds=0/9`로 출력했다. 같은 최신
  publisher의 v1 회귀 `runtime_logs/traffic-smoke-20260902-110839-be182f55.*.log`도 기존
  `cycle_seconds=30`과 lifecycle을 통과했다.
- Smoke의 새 PIE 첫 수신 시간은 latest-wins 전송으로 정확한 0초 publication이 대체될 수 있음을 고려해 `<100ms`를 요구한다. C++ phase 시험의 nanosecond 경계와 구분한다.

Unreal Automation은 다음 이름으로 구분한다. 전체 실행은 `Automation RunTests DriveIntegration`을 사용한다.

- `DriveIntegration.VirtualCity.Traffic.DirectionalTopology`
- `DriveIntegration.VirtualCity.Traffic.RejectInvalidTopology`
- `DriveIntegration.VirtualCity.Traffic.DeterministicJson`
- `DriveIntegration.SignalCity.GeometryContract`
- `DriveIntegration.SignalCity.IntersectionsAndMarkings`
- `DriveIntegration.SignalCity.NonRectangularDriveRoute`
- `DriveIntegration.SignalCity.Traffic.TwoControllerTopology`
- `DriveIntegration.SignalCity.Traffic.RejectUnsafePlans`
- `DriveIntegration.SignalCity.Traffic.DeterministicV2Json`
- `DriveIntegration.Protocol.TrafficSignals`
- `DriveIntegration.Protocol.TrafficSignalsValidation`
- `DriveIntegration.TrafficSignals.FreshnessAndOneHot`
- `DriveIntegration.TrafficSignals.ActorPresentation`
- `DriveIntegration.TrafficSignals.ClientLifecycle`

### 사용자 PIE 확인

1. 북쪽 본선/지선 T 교차로에서 접근 방향에 맞는 세 개 head가 보이는지 확인한다. Editor 정지 화면에는 없고 Play 때 생성된다. 본선의 추가 정지선 페인트는 후속 작업이며, 현재 lane 끝의 제어 metadata와 구분한다.
2. Play 시작 후 입력 lease가 활성인 상태에서 약 30초 한 주기를 관찰한다. 본선 두 head가 같은 색인지, 본선과 지선이 동시에 녹색/황색이 아닌지 확인한다.
3. 카메라를 돌려 렌즈 방향·낮의 가독성·기둥 바닥 위치를 확인한다. 메시가 도로에 떠 있거나 표면과 겹쳐 깜빡이면 별도의 시각 결함으로 기록한다.
4. 테스트를 위해 사용자 서버를 임의로 종료하지 않는다. 연결 중단/timeout 검증은 우선 isolated smoke에서 수행하고, 수동 재연결 시험은 실행자와 범위를 확인한 뒤 진행한다.

위 항목은 `virtual_city_v1`의 단일 교차로 인수다. 두 교차로의 phase offset, independent
controller, NPC 통과와 새 맵 전체 주행은
[signal_city_v2 사용자 PIE 인수](./signal_city_quickstart.md#사용자-pie-인수)를 별도로 수행한다.

이 단계의 성공은 **차선/신호 기반과 표시 검증**이다. NPC·보행자 신호 준수, 교통량 통합, 최종 패키지 성능과 30분 주행 인수를 완료했다는 의미가 아니다.

## 주요 구현 위치

- `unreal/DriveIntegration/Source/DriveIntegration/SimCoreVirtualCityTrafficLayout.*`
- `unreal/DriveIntegration/Source/DriveIntegration/SimCoreSignalCityTrafficLayout.*`, `SimCoreSignalCityLayout.*`, `SignalCityGameMode.*`
- `unreal/DriveIntegration/Source/DriveIntegrationEditor/ExportVirtualCityTrafficCommandlet.*`
- `cpp/host/src/traffic/traffic_network.*`
- `cpp/host/src/main.cpp`, `cpp/host/src/simulation_host.cpp`, `cpp/host/src/runtime_options.cpp`
- `protocol/vehicle.proto`, `cpp/host/src/protocol/vehicle_messages.cpp`
- `unreal/DriveIntegration/Source/DriveIntegration/SimCoreProtocol.*`, `SimCoreClientComponent.*`, `SimCoreTrafficSignalActor.*`
- `scripts/smoke_traffic.py`, `scripts/test_smoke_traffic.py`

## 다음 작업

1. 저작 lane을 따라가는 NPC 1대로 경로 추종·정지선 정지·재출발·앞차 간격을 검증한 뒤 3~4대로 확장한다. 4m 교차로 connector 반경과 terminal 처리는 실제 NPC 차량 모델로 추가 확인해야 한다.
2. 보행자 6~8명과 횡단 구간·보행 신호·차량 양보 규칙을 연결한다. 지금의 차량용 2-group 신호를 보행 신호 구현 완료로 간주하지 않는다.
3. 본선 정지선 외형을 추가하고 저작 lane·시각 도로·collision 정렬을 계속 검증한다.
4. 최종 아트·패키지 성능·30분 안정성과 AT-04/05 사용자 인수를 진행한다. [일정표](./01_schedule.md)의 관리 목표와 버퍼는 유지한다.
