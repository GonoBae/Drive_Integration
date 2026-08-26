# Drive Integration

Unreal Engine을 영상 생성·입력·센서 환경으로 사용하고, 외부 C++ SimCore가 차량 물리를 계산하는 운전 시뮬레이션 프로젝트다. 1차 목표는 Wall Street·Broad Street 주변 수동운전이며, 이후 동일한 제어 프로토콜 위에 Python 자율주행 서버를 추가한다.

## 현재 상태

- C++ 자체 차량 물리 모델: 외부 SDK 없이 MapPackage 지면, 네 바퀴의 독립 1D
  spring/damper 반력과 wheel-local tangent tire force를 sprung-body
  heave·pitch·roll에 결합하고 Ackermann, RWD open differential와 60Hz 타이어
  implicit coupling까지 구현
- 공통 충돌 월드: 지면 삼각형 adaptive grid, 정적·동적 8m broad phase,
  정적 OBB와 NPC OBB·보행자 capsule 충돌 및 결정적 microstep 구현
- WebSocket binary + Protobuf 제어·상태 통신: C++ host와 Unreal client 구현
- Python relay: R1에서 동결한 선택 기능; ZMQ 기본 OFF, 구 `EntityStatePacket` observer만 보존
- Unreal 외부 차량 Pawn·수동 입력·상태 표시와 정적 collider authoring,
  정지 기반 전진/후진 전환·차속 연동 표시 휠, opt-in NPC·보행자 런타임 프록시 표시 구현
- Wall/Broad MapPackage: 구현 전
- 자율주행 학습·추론: 8월 범위 제외

상세 범위와 일정은 [프로젝트 문서](./docs/README.md)를 기준으로 한다.

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

저장소 루트에서 인자 없이 실행하면 버전 관리되는 기본 차량 설정과 Wall/Broad
MapPackage 지면을 로드한다.

```powershell
.\cpp\host\build\Release\simcore_publisher.exe
```

다른 설정은 재컴파일 없이 명시할 수 있다.

```powershell
.\cpp\host\build\Release\simcore_publisher.exe `
  --vehicle-config .\cpp\host\config\vehicle_sedan.cfg `
  --map-package .\map_packages\wall_broad_v1
```

차량 설정은 알 수 없는 key, 중복, 누락, 비정상 수치를 허용하지 않으며 시작 로그에
설정 checksum과 로드한 지면 삼각형 수를 출력한다.

현재 차량 설정 format v4는 하중·구동·제동 비율, 조향 rack과 속도별 조향 한계,
구동 출력·응답 속도, driveline drag, 저속 slip 기준, 횡력 우선 마찰 배분과 traction
control·공중 휠 감쇠를 모두 필수 값으로 검증한다. 기본 generic sedan은 full-scale
키보드 입력을 유한한 파워트레인·조향 응답으로 바꾸고, 구동륜 slip 8%부터 토크를
줄여 12%에서 완전히 개입한다. 20° RWD 등판과 차량 감각 회귀는 통과했지만 특정 실차
계측 데이터에 대한 정밀 동정·검증을 완료한 모델은 아니다.

현재 차량 구조는 wheel 회전 관성, 네 독립 1D suspension과 차체 질량·관성의
heave·pitch·roll을 결합한 **4-corner reduced-order 모델**이다. hub vertical unsprung
mass, tire carcass 변형, jump·airborne·전복을 포함한 완전한 6DoF는 아직 별도 자유도로
풀지 않는다. 최신 Windows C++ Release CTest 11/11, `vehicle_physics_tests`와
`vehicle_config_tests` 각각 20/20 반복 및 UE 5.6 Editor build는 통과했지만,
이번 차체 기울기 부호·각도 경계·부분 지면 변경의 실제 Landscape PIE 주행은 수동 검증
대기 상태다.

## 기본 조작

- `W`: 전진 가속이다. 후진 중에는 먼저 제동하고, 거의 정지한 뒤 Drive로 전환해
  전진한다.
- `S`: 전진 중에는 제동한다. 계속 누르면 거의 정지한 뒤 Reverse로 전환해 후진한다.
- `W`와 `S`를 동시에 누르면 구동력을 내지 않고 제동한다.
- `A`/`D`는 좌/우 조향, `Space`는 주차 브레이크다. 게임패드 trigger와 left stick도
  같은 규칙을 사용한다.
- 방향 전환은 지연되지 않은 최신 차량 상태로 정지를 확인할 수 있을 때만 허용한다.
  상태가 없거나 오래됐으면 현재 기어를 유지하고 제동한다.
- Unreal의 표시 휠 회전은 부호 있는 차체 종방향 속도에 맞춘다. 출발 순간의 물리
  wheel slip이 화면에서 과도한 헛바퀴처럼 보이지 않으며, 정지하면 표시 휠도 멈춘다.
- 저속 선회에서는 타이어 마찰 한도 안에서 횡력을 우선 배분한다. 가속과 조향을 함께
  입력해도 옆으로 미끄러지기보다 차체가 노면을 붙잡는 일반 승용차 감각을 우선한다.

## Unreal 지면·정적 충돌 Bake

`GroundCollisionExporter`는 Unreal Editor의 실제 `WorldStatic` 충돌을 지정한 영역에서
수직 raycast로 샘플링하고 `map_packages/landscape_local_v1/ground_surface.csv`로
내보낸다. 벽·커브·barrier는 `SimCore Static Collider` actor의 box로 명시한다.
Landscape나 정적 장애물을 수정한 뒤 Details의
`Ground Actor`를 지정하고 `Fit Sampling Bounds To Ground Actor`로 주행할 actor 전체
bounds를 포함시킨 다음 `Bake Ground + Static Collision To MapPackage` 버튼을 누르고
서버를 재시작한다. Fit 버튼은 sampling box만 바꾸므로 Bake 전에는 CSV 범위가 바뀌지
않고, 서버는 실행 중인 MapPackage를 hot reload하지 않는다.

```powershell
.\scripts\run_landscape_server.ps1
```

NPC 차량 `1001`과 보행자 `2001` 프록시까지 함께 시험할 때만 다음 opt-in 옵션을 쓴다.

```powershell
.\scripts\run_landscape_server.ps1 -DemoEntities
```

Codex나 다른 자동화에서 서버를 오래 유지할 때는 동기식 콘솔 pipe가 60Hz 처리
thread를 막지 않도록 stdout/stderr를 `runtime_logs/` 파일로 분리하는 background 모드를
사용한다.

```powershell
.\scripts\run_landscape_server.ps1 -DemoEntities -Background
```

추적 중인 현재 Landscape 패키지는 아직 정적 marker가 없는 header-only payload라
서버 로그의 `static_colliders=0`이 정상이다. 실제 벽 시험 전에는 marker를 하나 이상
배치하고 다시 Bake해 `static_colliders`가 1 이상인지 확인한다.

이는 편집기 충돌을 주행 중 네트워크로 되먹이는 기능이 아니라 동일한 정적 충돌
snapshot을 C++ 권한 물리에 공급하는 개발용 authoring bridge다. 자세한 설정과 제한은
[Unreal 프로젝트 안내](./unreal/DriveIntegration/README.md)를 따른다.

급경사 Landscape는 exporter의 `Ground Actor`에 명시하고 다시 Bake한다. 명시한 연속
surface는 단순 corner 높이 차 때문에 삭제하지 않으며, 서버 물리는 경사 법선 기준
타이어 여유를 강제한다. wheel ray가 1~3개 남으면 부분 지지로 계속 계산하고, 별도
vehicle-centre coverage가 사라지거나 네 wheel ray가 모두 사라질 때만 차량을 마지막
지원 pose로 되돌려 안전 정지한다. 이 최종 경계는 물리 각도 제한이 아니라 유한한 bake
범위이므로 더 멀리 주행하려면 sampling box와 Landscape collision을 넓혀 다시 Bake해야 한다.

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

Unreal은 C++ host의 `ws://<host>:9000`에 직접 연결한다. 선택적 relay는
`tcp://127.0.0.1:5555`의 버전 없는 구 `EntityStatePacket`을 읽는 보존 코드이며,
schema-v2 `Envelope{WorldState}` 계약이나 R1 완료 게이트로 간주하지 않는다. Python
자율주행은 R2에서 별도 통신 계약을 결정한 뒤 다시 시작한다.

## 개발 원칙

- C++ SimCore만 최종 차량 물리 상태를 결정한다.
- 수동 입력과 향후 자율주행 명령은 같은 `ControlCommand`를 사용한다.
- 지도별 데이터는 코드에 넣지 않고 버전이 있는 MapPackage로 분리한다.
- 빌드 결과, vcpkg checkout, Python 가상환경, Unreal 생성물은 커밋하지 않는다.
- 기능 완료와 기술 결정은 `docs/`의 기능표·아키텍처·ADR·작업일지에 반영한다.
