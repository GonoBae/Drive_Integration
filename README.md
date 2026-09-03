# Drive Integration

Unreal Engine을 영상 생성·입력·센서 환경으로 사용하고, 외부 C++ SimCore가 차량 물리를 계산하는 운전 시뮬레이션 프로젝트다. 1차 목표는 작은 가상 도심 코스의 수동운전이며, 이후 동일한 제어 프로토콜 위에 Python 자율주행 서버를 추가한다. 2026-08-31 사용자 승인으로 기존 Wall Street·Broad Street 재현을 가상 도심으로 변경했다.

## 현재 상태

- C++ 자체 차량 물리 모델: 외부 SDK 없이 MapPackage 지면, 네 바퀴의 독립 1D
  spring/damper 반력과 wheel-local tangent tire force를 sprung-body
  heave·pitch·roll에 결합하고 Ackermann, RWD open differential와 60Hz 타이어
  implicit coupling까지 구현. 차체 전체 shell의 지면 접촉으로 옆면·지붕까지 판정해
  rollover 시 지면 관통을 막고, 충돌 impulse 기반 누적 damage와 Unreal 차체 변형을 표시한다.
- 공통 충돌 월드: Unreal-owned `SIMGHF2` height/normal/material/friction heightfield(v1 호환)
  또는 legacy 지면 삼각형 provider,
  정적·동적 8m broad phase,
  정적 OBB와 NPC OBB·보행자 capsule 충돌 및 결정적 microstep 구현
- WebSocket binary + Protobuf 제어·상태 통신: C++ host와 Unreal client, 양방향
  Hello build/schema/map checksum/capability gate, 연결별 source/session/sequence 고정,
  client Hello 승인 전 state 차단과 localhost-only 기본 listener 구현
- Python relay: R1에서 동결한 선택 기능; ZMQ 기본 OFF, 구 `EntityStatePacket` observer만 보존
- Unreal 외부 차량 Pawn·수동 입력·상태 표시와 정적 collider authoring, C++/UE
  GeoTransform·quaternion 계약, connection/hello/map/control/state rate/sequence/age/gap/missing/old/
  checksum/entity 및 authoritative Health(SafeStop·reason·command age·overrun) 경량 HUD,
  정지 기반 전진/후진 전환·차속 연동 표시 휠, frame-independent 키보드 조향과 Space
  rear side-brake drift, 유한질량 NPC·보행자 반작용, 6구역 국부 dent 및 `F3` 차량 물리
  디버그 오버레이 구현
- 가상 도심 `L_VirtualCity`/`virtual_city_v1`: 약 633m 기본 도형 주행 루프,
  4° 경사·T자 분기·건물 8개·충돌체 228개. 기본 지면/도로59+보도116+연석top215=
  Ground390개를 50cm로 Bake하고 semantic Curb의 swept-footprint support 기반 등판 자동
  회귀를 통과했다. unsupported endpoint/opening과 Wall·Barrier는 계속 fail-closed한다.
  사용자 PIE 연석·벽 인수, 완성 아트·다중 NPC/보행 동작은 후속
- 기본 차선·신호 기반: `virtual_city_v1` 저작 방향 차선 25개·차량 신호등 3개,
  서버 고정 시간의 30초 신호 주기와
  Unreal 수신·표시·stale 적색 처리. [사용법과 남은 범위](./docs/traffic_network_signals.md)
- 선택형 다중 교차로 `L_SignalCity`/`signal_city_v2`: 별도 42-lane graph, 신호 head
  총 16개(차량 8·보행 8), controller 2개, 서버 권한 NPC 4대·보행자 8명을 구현했다.
  map checksum은 `fnv1a64:7446108adad3e25b`, traffic checksum은
  `fnv1a64:dfcb5e4541d71adf`다. `signal_city_course` CTest 5그룹, 전체 CTest 20/20,
  UE Automation 47/47와 2,280-state WebSocket smoke의 SafeStop/reset을 통과했다.
  테스트 서버는 `scripts/run_signal_city_server.ps1 -Background`로 시작하며 launcher가
  실제 listener PID와 고유 로그를 확인한다. 사용자 PIE와 성능 인수는 후속이다.
  [별도 실행·생성 절차](./docs/signal_city_quickstart.md)
- 차량 후속 개선: 저·중속 조향 envelope/응답 보완, 마우스 orbit·휠 줌·C 복귀,
  자체 제작 세단 차체/원형 휠·10종 재질과 Ground support 기반 연석 등판 적용.
  [조작법과 검증 경계](./docs/vehicle_driving_refinement.md)
- `virtual_city_v1`의 선행 Lane NPC 1대 구현 이력과 `signal_city_v2`의 NPC 4대·보행자
  8명 확장을 함께 보존한다. [선행 구현의 운동학적 모델 한계](./docs/npc_lane_following.md)
- SensorRig 골격: `base_link` FLU 기준 front camera 10Hz·roof LiDAR 20Hz mount config와
  권한 `SimulationTimeNs` metadata cadence를 구현했다. 실제 image/point cloud 캡처는 아직 없다.
- 기록·재생 골격: `F5`가 권한 차량 snapshot을
  `Saved/DriveReplays/last_drive.csv`에 기록하고 `F6`가 collision-free 시각 ghost로 재생한다.
  command/event를 다시 물리에 적용하는 결정적 re-simulation은 아직 구현하지 않았다.
- 자율주행 학습·추론: 8월 범위 제외

상세 범위와 일정은 [프로젝트 문서](./docs/README.md)를 기준으로 한다.
완료한 책임 분리와 검증 근거는 [전체 리팩터링 구조](./docs/refactoring.md)에 정리했다.

## 디렉터리

```text
Drive_Integration/
├── protocol/                 # 언어 간 공통 Protobuf 원본
├── cpp/host/                 # 권한 물리 서버와 통신
├── python/relay_server/      # observer/debug relay
├── python/autonomy_server/   # 향후 자율주행 서버 위치
├── unreal/DriveIntegration/  # Unreal Engine 5.6 C++ 프로젝트
├── map_packages/             # 공통 지도·차선·충돌 패키지
└── docs/                     # 일정, 기능표, 아키텍처, ADR, 작업일지
```

`protocol/vehicle.proto`가 유일한 Proto 원본이다. C++ 생성물은 빌드 디렉터리에 만들고 Python 생성물은 `python/relay_server/generated/`에 둔다.

## C++ host

필수 도구는 Git, CMake 3.21 이상, C++20 컴파일러다. 의존성은 프로젝트 로컬 vcpkg로 준비한다.

macOS/Linux:

```bash
bash cpp/host/scripts/setup.sh
bash cpp/host/scripts/build.sh
cd cpp/host && ctest --preset release
```

Windows PowerShell:

```powershell
cpp\host\scripts\setup.ps1
cpp\host\scripts\build.ps1
cd cpp\host
ctest --preset release
```

실행 파일은 macOS/Linux에서 `cpp/host/build/simcore_publisher`, Visual Studio 기반 Windows 빌드에서 `cpp/host/build/Release/simcore_publisher.exe`에 생성된다.

저장소 루트에서 인자 없이 실행하면 버전 관리되는 기본 차량 설정과 기존
`wall_broad_v1` bootstrap 지면을 로드한다. 새 가상 도심은 전용 launcher를 사용한다.
기존 Landscape 테스트 launcher와 package도 그대로 보존한다.

```powershell
.\scripts\run_virtual_city_server.ps1 -Background
```

Unreal에서 `/Game/VirtualCity/Maps/L_VirtualCity`를 열고 Play한다.
기존 서버가 port 9000을 사용 중이면 먼저 종료해야 한다.
[새 코스 실행·주행·Bake 안내](./docs/virtual_city_quickstart.md)를 따른다.
직접 host 무인자 실행은 이전 fixture 검증용이다.

다중 교차로 확장은 기본 시작 맵을 바꾸지 않는다. 명시적으로 선택할 때만 별도 launcher를
실행하고 Unreal에서 `/Game/SignalCity/Maps/L_SignalCity`를 연다.

```powershell
.\scripts\run_signal_city_server.ps1 -Background
```

두 launcher는 같은 port 9000을 사용하므로 동시에 실행하지 않는다. v2 package 생성,
검증과 남은 PIE gate는 [signal city 빠른 시작](./docs/signal_city_quickstart.md)을 따른다.

```powershell
.\cpp\host\build\Release\simcore_publisher.exe
```

전체 서버 runtime 설정은 strict `key=value` 형식의
`cpp/host/config/runtime_server.cfg`로 재컴파일 없이 바꿀 수 있다. cfg를 지정하지 않은
인자 없는 실행은 기존 기본값과 호환된다.

```powershell
.\cpp\host\build\Release\simcore_publisher.exe `
  --runtime-config .\cpp\host\config\runtime_server.cfg
```

우선순위는 `내장 기본값 < runtime cfg < CLI`다. 예를 들어 같은 cfg를 사용하면서
Landscape MapPackage, WebSocket port와 tick만 바꿀 수 있다.

```powershell
.\cpp\host\build\Release\simcore_publisher.exe `
  --runtime-config .\cpp\host\config\runtime_server.cfg `
  --vehicle-config .\cpp\host\config\vehicle_sedan.cfg `
  --map-package .\map_packages\landscape_local_v1 `
  --ws-port 9100 `
  --physics-hz 60
```

runtime cfg는 차량/MapPackage 경로, WebSocket port, physics Hz, ENU origin
latitude/longitude/altitude, spawn heading, soft·queue-age·hard timeout, source ID와 demo entity
활성화를 모두 필수 key로 가진다. 알 수 없는 key, 중복, 누락, non-finite·범위 밖 수치와
`hard timeout <= soft timeout`을 fail-closed한다. cfg의 상대 경로는 cfg 파일 위치를 기준으로
해석한다. 모든 CLI option과 단위·범위는 다음 명령으로 확인한다.

```powershell
.\cpp\host\build\Release\simcore_publisher.exe --help
```

차량 설정도 알 수 없는 key, 중복, 누락, 비정상 수치를 허용하지 않으며 시작 로그에
설정 checksum과 로드한 지면 정보를 출력한다.

현재 차량 설정 format v7은 하중·구동·제동 비율, 앞/뒤 타이어별 코너링 강성, 최대 조향각과 조향 rack 응답,
구동 출력·응답 속도, driveline drag, 저속 slip 기준, 횡력 우선 마찰 배분과 traction
control·공중 휠 감쇠, default/asphalt/low-friction/rough surface friction scale을 모두 필수
값으로 검증한다. 기본 generic sedan은 full-scale
키보드 입력을 유한한 파워트레인·조향 응답으로 바꾸고, 구동륜 slip 8%부터 토크를
줄여 12%에서 완전히 개입한다. 2026-08-31 속도별 조향각 축소를 제거하여 목표 중앙
조향각은 입력 × 35°, 증가/복귀는 1.80/2.20rad/s다. 개별 앞바퀴에는 Ackermann 각도를
적용하고 실제 선회는 타이어 힘과 차체 적분으로 계산한다. v6의 단일
`tire_corner_stiffness_n_rad`는 앞/뒤 이름의 두 필드로 나누고 기존 바퀴당 값을 각각
복사한 뒤 `format_version=7`로 바꾼다. v5라면 `comfortable_lateral_accel_mps2`도
삭제해야 한다. 이전 설정을 조용히 무시하지 않는다. 자세한 기준은
[조향 수정 기록](docs/vehicle_driving_refinement.md)을 참고한다.
20° RWD 등판과 차량 감각 회귀는 통과했지만 특정 실차
계측 데이터에 대한 정밀 동정·검증을 완료한 모델은 아니다.

현재 차량 구조는 wheel 회전 관성, 네 독립 1D suspension과 차체 질량·관성의
heave·pitch·roll을 결합한 **4-corner reduced-order 모델**이다. hub vertical unsprung
mass, tire carcass 변형, jump·airborne·전복을 포함한 완전한 6DoF는 아직 별도 자유도로
풀지 않는다. 2026-08-28 최신 Windows C++ Release CTest **14/14**와 전체 suite 20회
**280/280**, 신규 heightfield query와 MapPackage hot reload 각 **50/50**, 선행 WebSocket
회귀 **100/100** 반복을 통과했다. 같은 날
Unreal exporter를 Ground Actor 전체-bounds 100cm height/ImpactNormal/cell snapshot으로
분리하고 `SIMGHF2` material/friction과 v1 호환을 통합했다. UE 5.6 Editor/Game build와
headless `DriveIntegration.Coordinates.GeoTransformContract` Automation **1/1**도 성공했다.
확대 Landscape package의 자동 교체, 양방향 Hello negative/positive 실제 WebSocket smoke와
서버 PID 유지 smoke도 통과했으며 실제 PIE 조향·
경사·자동 재연결 감각은 수동 검증 대기 상태다.
세부 PID·로그·checksum 증거는 [2026-08-28 작업일지](./docs/worklogs/2026-08-28.md)에 기록했다.

2026-08-31에는 서버 authoritative Health와 Unreal SafeStop/EStop/Unknown/Stale HUD,
실제 WorldStatic 기반 지면·material·marker 자동 QA를 추가했다. 최신 C++ **14/14**,
핵심 통신 4종 각 10회 **40/40**, UE Editor/Game build와 headless Automation **8/8**,
실제 v2 지면·marker 2개 package를 사용한 WebSocket Health smoke가 통과했다.
사용자 `NewMap`은 수정하지 않았고 실제 PIE gate는 남아 있다.
[최신 작업·검증 기록](./docs/worklogs/2026-08-31.md)과
[배경 제작안](./docs/04_environment_plan.md)을 참고한다.

## 기본 조작

- `W`: 전진 가속이다. 후진 중에는 먼저 제동하고, 거의 정지한 뒤 Drive로 전환해
  전진한다.
- `S`: 전진 중에는 제동한다. 계속 누르면 거의 정지한 뒤 Reverse로 전환해 후진한다.
- `W`와 `S`를 동시에 누르면 구동력을 내지 않고 제동한다.
- `A`/`D`는 좌/우 조향이다. 짧은 키 입력은 중앙 부근을 정밀하게 쓰는 제곱 응답이며,
  계속 누르면 기존과 같은 최대 조향각에 도달한다. 상승·복귀는 `DeltaSeconds` 기반이라
  렌더 프레임률과 무관하고 게임패드 left stick은 선형 아날로그 입력을 유지한다.
- `Space`는 후륜 사이드 브레이크다. 후륜 종방향 마찰을 우선 사용해 바퀴가 잠기고
  횡그립이 줄어드는 서버 물리이므로 조향과 함께 드리프트 진입에 사용할 수 있다.
- `F3`는 collision box·접지/힘 진단 오버레이, `F5`는 권한 snapshot 기록 시작/종료,
  `F6`는 마지막 CSV의 collision-free 시각 ghost 재생 시작/종료다.
- 방향 전환은 지연되지 않은 최신 차량 상태로 정지를 확인할 수 있을 때만 허용한다.
  상태가 없거나 오래됐으면 현재 기어를 유지하고 제동한다.
- Unreal의 표시 휠 회전은 부호 있는 차체 종방향 속도에 맞춘다. 출발 순간의 물리
  wheel slip이 화면에서 과도한 헛바퀴처럼 보이지 않으며, 정지하면 표시 휠도 멈춘다.
- 저속 선회에서는 타이어 마찰 한도 안에서 횡력을 우선 배분한다. 가속과 조향을 함께
  입력해도 옆으로 미끄러지기보다 차체가 노면을 붙잡는 일반 승용차 감각을 우선한다.
- 2026-08-28 조향 보완 기준 full-key 자동 선회 반경은 약 5m/s에서 `4.0~6.5m`,
  약 10m/s에서 `15~25m`다. 최종 체감은 넓고 평탄한 Landscape PIE에서 확인한다.

## Unreal 지면·정적 충돌 Bake

`GroundCollisionExporter`는 Unreal Editor의 실제 `WorldStatic` 충돌을 지정한 영역에서
수직 raycast로 측정하고 높이·ImpactNormal·유효 cell을
`map_packages/landscape_local_v1/ground_heightfield.bin`으로 내보낸다. Unreal은 scene
측정을 소유하고 C++는 이 immutable snapshot으로 차량 물리만 계산한다. 벽·커브·barrier는
`SimCore Static Collider` actor의 box로 명시한다.
Landscape나 정적 장애물을 수정한 뒤 Details의 `Ground Actor`를 지정하고
`Bake Ground + Static Collision To MapPackage` 버튼을 누른다. Bake preflight는 지정 actor의
전체 colliding-component bounds와 기본 padding을 검사하고 기존 sampling box가 작으면
자동으로 Fit한다. `Fit Sampling Bounds To Ground Actor`는 Bake 전에 결과 box를 미리 보는
수동 버튼으로도 사용할 수 있다. 새 `Heightfield Sample Spacing Cm` 기본값은 100cm이며
2,000,000 samples를 넘으면 해상도를 자동 저하하지 않고 명시적으로 거부한다. 성공 창의
sample/valid/drivable cell 수와 실제 ENU bounds를 확인한다. 실행 중인 서버는
`manifest.cfg`를 250ms 주기로 감시한다.
ground·static collision·최종 checksum을 모두 검증한 새 snapshot만 고정 tick 경계에서
원자적으로 교체하고 차량·clock·control lifecycle을 reset한다. 작성 중이거나 잘못된
package는 거부하고 기존 snapshot을 계속 사용하므로 Bake 뒤 서버를 수동 재시작할 필요가
없다. checksum이 실제로 바뀌면 Unreal은 짧게 재연결하고 새 PlaySession으로 reset한다.

```powershell
.\scripts\run_landscape_server.ps1
```

launcher는 기본적으로 `cpp/host/config/runtime_server.cfg`를 먼저 읽고 Landscape용
차량/MapPackage 경로를 CLI로 덮어쓴다. 다른 runtime cfg를 사용하려면 다음처럼 지정한다.

```powershell
.\scripts\run_landscape_server.ps1 `
  -RuntimeConfigPath .\cpp\host\config\runtime_server.cfg
```

NPC 차량 `1001`과 보행자 `2001` 프록시까지 함께 시험할 때만 다음 opt-in 옵션을 쓴다.

```powershell
.\scripts\run_landscape_server.ps1 -DemoEntities
```

cfg가 `demo_entities=true`일 때 명시적으로 끄려면 `-NoDemoEntities`를 사용한다.

Codex나 다른 자동화에서 서버를 오래 유지할 때는 동기식 콘솔 pipe가 60Hz 처리
thread를 막지 않도록 stdout/stderr를 `runtime_logs/` 파일로 분리하는 background 모드를
사용한다.

```powershell
.\scripts\run_landscape_server.ps1 -DemoEntities -Background
```

추적 중인 현재 Landscape 패키지는 v2 material/friction 도입 전 실제 Bake한 `SIMGHF1`
heightfield다. `507×508`, 257,556 samples/256,542 cells, 100cm step, checksum
`fnv1a64:b8b0a6ccd89614de`이며 같은 서버 PID의 hot reload를 통과했다. 다음 Bake 뒤에는
`SIMGHF2`와 surface cell 분류·friction multiplier, 시작 로그의 heightfield sample/cell 수를
확인한다. v1도 default material·1.0 multiplier로 계속 호환한다. 정적 marker가 없는
header-only payload라 서버 로그의 `static_colliders=0`이 정상이다. 실제 벽 시험 전에는
marker를 하나 이상 배치하고 다시 Bake해 `static_colliders`가 1 이상인지 확인한다.

이는 편집기 충돌을 주행 중 네트워크로 되먹이는 기능이 아니라 동일한 정적 충돌
snapshot을 C++ 권한 물리에 공급하는 개발용 authoring bridge다. 자세한 설정과 제한은
[Unreal 프로젝트 안내](./unreal/DriveIntegration/README.md)를 따른다.

급경사 Landscape는 exporter의 `Ground Actor`에 명시하고 다시 Bake한다. 명시한 연속
surface는 단순 corner 높이 차 때문에 삭제하지 않으며, 서버 물리는 경사 법선 기준
타이어 여유를 강제한다. authored-surface coverage에서 단일 missing corner와 대각선
two-wheel support는 계속 계산한다. 완전 front/rear axle 또는 left/right side가 사라지거나,
vehicle-centre coverage가 사라지거나, 네 wheel ray가 모두 사라지면 차량을 이전 지원
step으로 되돌려 안전 정지한다. 이는 ground-bound reduced model이 유한 snapshot 끝에서 남은
spring으로 nose-down하지 않게 하는 terminal footprint 경계다. 더 멀리 주행하려면
`Ground Actor`를 지정해 다시 Bake한다. 실행 중 서버의 `[MapReload] verified candidate`와
`[MapReload] applied at tick boundary` 로그, 또는 다음 시작 로그의
`exported_ground_bbox_enu_m`·`exported_span_m`으로 기대 범위를 확인한다. 파일 내용이
같아 checksum이 바뀌지 않은 Bake에는 재로딩이 발생하지 않는 것이 정상이다.

동작 원리와 장애 판별은
[Bake 후 조작 불가·자동 재로딩 해결 사례](./docs/troubleshooting/ue-map-package-hot-reload-after-bake.md)에
정리했다.

## 선택적 Python observer(동결)

Python relay는 R1 수동운전의 개발·실행·시험 대상이 아니다. 기본 C++ 빌드는 ZeroMQ를
컴파일하거나 5555 포트를 열지 않는다. 과거 디버그 observer를 명시적으로 확인할 때만
`release-zmq-observer` preset으로 C++ host를 별도 빌드한 뒤 아래 명령을 사용한다.

```bash
cmake --preset release-zmq-observer -S cpp/host
cmake --build cpp/host/build-zmq-observer --config Release --parallel
```

macOS/Linux:

```bash
python3 -m venv python/relay_server/.venv
python/relay_server/.venv/bin/python -m pip install -r python/relay_server/requirements.txt
python/relay_server/.venv/bin/python -m python.relay_server
```

Windows PowerShell:

```powershell
py -3.10 -m venv python\relay_server\.venv
python\relay_server\.venv\Scripts\python.exe -m pip install -r python\relay_server\requirements.txt
python\relay_server\.venv\Scripts\python.exe -m python.relay_server
```

Proto를 변경한 경우에만 macOS/Linux에서
`bash python/relay_server/scripts/generate_proto.sh`, Windows에서
`python\relay_server\scripts\generate_proto.ps1`을 실행한다.
`make test-python`은 relay 회귀 시험과 생성물 동기화를 함께 검증한다.

Unreal은 C++ host의 `ws://127.0.0.1:9000`에 직접 연결한다. 현재 control listener는
localhost-only이며 LAN 연결은 인증과 명시적 bind 설정을 설계한 뒤 추가한다. 선택적 relay는
`tcp://127.0.0.1:5555`의 버전 없는 구 `EntityStatePacket`을 읽는 보존 코드이며,
schema-v2 `Envelope{WorldState}` 계약이나 R1 완료 게이트로 간주하지 않는다. Python
자율주행은 R2에서 별도 통신 계약을 결정한 뒤 다시 시작한다.

## 개발 원칙

- C++ SimCore만 최종 차량 물리 상태를 결정한다.
- 수동 입력과 향후 자율주행 명령은 같은 `ControlCommand`를 사용한다.
- 지도별 데이터는 코드에 넣지 않고 버전이 있는 MapPackage로 분리한다.
- 빌드 결과, vcpkg checkout, Python 가상환경, Unreal 생성물은 커밋하지 않는다.
- 기능 완료와 기술 결정은 `docs/`의 기능표·아키텍처·ADR·작업일지에 반영한다.
