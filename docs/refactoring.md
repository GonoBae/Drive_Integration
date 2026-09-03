# 2026-09-02 전체 리팩터링 구조

이 문서는 기존 동작과 외부 계약을 유지하면서 커진 구현 파일의 책임을 나눈 2026-09-02
리팩터링을 기록한다. 기능 추가나 물리 모델 교체가 목적이 아니며, 구조 변경 뒤에도 같은
입력·설정·MapPackage·프로토콜이 같은 런타임 의미를 가져야 한다.

## 상태와 범위

- 상태: 구조 분리와 최종 자동 통합 검증 완료.
- C++ 범위: runtime 설정, traffic hot reload, Protobuf 메시지 encode/decode 경계.
- Unreal 범위: 세단 표시 계약, 수동 입력 해석, client diagnostics, DriveReplay 형식과
  runtime component 경계.
- 실행 도구 범위: Signal City, Virtual City, Landscape 서버 스크립트의 공통 실행 수명주기.
- 제외: 차량 물리 수식 재설계, WebSocket wire format 변경, UCLASS 공개 계약 변경,
  MapPackage/traffic schema 변경, 외부 물리 SDK 도입.

## 지켜야 하는 호환 계약

리팩터링은 아래 계약을 변경하지 않는다.

1. 루트 `protocol/vehicle.proto`와 schema version, 필드 번호, 직렬화 의미.
2. 양방향 Hello, map checksum, connection generation, control lease, sequence와
   SimulationReset 순서.
3. C++ fixed tick에서 snapshot 교체·reset·control·physics·publish가 적용되는 순서.
4. `runtime_server.cfg`의 `built-in defaults < cfg < CLI` 우선순위와 strict validation.
5. Unreal의 UCLASS/UPROPERTY, default subobject 이름, 입력 action 이름과 저장 에셋 경로.
6. C++의 authoritative pose/physics와 Unreal의 입력·표시·진단·센서 책임 경계.

## C++ 모듈 분리

### Runtime options

| 파일 | 책임 |
|---|---|
| `cpp/host/src/runtime_options.hpp` | 외부에서 사용하는 `RuntimeOptions`와 공개 함수 선언 |
| `cpp/host/src/runtime_options.cpp` | 기본값, 최종 validation, 공개 facade와 help text |
| `cpp/host/src/runtime_options_internal.hpp` | config/CLI 구현 사이의 내부 파싱 계약 |
| `cpp/host/src/runtime_config_loader.cpp` | strict cfg 읽기, 중복·미지원 key 거부, cfg 상대 경로 해석 |
| `cpp/host/src/runtime_cli_parser.cpp` | CLI override, switch와 값 파싱, cfg 이후 override 적용 |

공개 호출자는 기존 `runtime_options.hpp`만 사용한다. 내부 파일 분리는 설정 우선순위나
오류 조건을 완화하지 않는다.

### Traffic hot reload

`cpp/host/src/traffic/traffic_network_hot_reloader.hpp/.cpp`의
`TrafficNetworkHotReloader`가 traffic sidecar와 소유 MapPackage manifest 감시를 담당한다.
worker는 기본 250ms 간격으로 후보를 찾고, MapPackage와 traffic network의 전체 검증을
통과한 immutable snapshot만 callback으로 전달한다. `main.cpp`는 watcher 구성과 수명주기만
조정하며 tick-boundary 적용 책임은 기존 simulation 경계에 남는다.

### Protocol encoder/decoder

| 파일 | 책임 |
|---|---|
| `cpp/host/src/protocol/vehicle_messages.hpp` | 공개 host 메시지 타입과 encode/decode API 계약 |
| `cpp/host/src/protocol/vehicle_messages.cpp` | WorldState·Hello 등 server outbound Protobuf 직렬화 |
| `cpp/host/src/protocol/vehicle_message_decoder.cpp` | client inbound Envelope parse, schema와 message-kind 검증 |

encoder와 decoder는 같은 공개 헤더와 generated `vehicle.pb.h`를 사용한다. 분리는 wire bytes,
에러 문자열, E-stop fail-safe 해석이나 허용 message kind를 바꾸기 위한 것이 아니다.

### Simulation host session

| 파일 | 책임 |
|---|---|
| `cpp/host/src/simulation_host.cpp` | fixed tick, 상태 전개, runtime entity와 publish 조정 |
| `cpp/host/src/simulation_host_session.cpp` | client Envelope dispatch, Hello 협상, control과 SimulationReset 수명주기 |
| `cpp/host/src/simulation_host_session_detail.hpp` | lifecycle identifier의 길이·printable ASCII·log-safe 내부 검증 |

세션 분리는 `SimulationHost` 공개 API와 보유 상태를 유지한다. Hello negotiation fingerprint,
connection generation에 묶인 source/session/sequence, 동일 sequence 재전송 판정, map checksum,
E-stop 예외와 reset/lease gate의 적용 순서도 기존 계약 그대로다.

## Unreal 모듈 분리

| 모듈 | 책임 | 주요 소비자 |
|---|---|---|
| `SimCoreSedanVisualContract` | body/wheel 저장 에셋 identity와 FL/FR/RL/RR wheel origin의 단일 원본 | Ego Pawn, NPC presentation, visual QA |
| `SimCoreVehicleControlResolver` | W/S pedal arbitration, 정지 근처 Drive/Reverse 전환, steering·side-brake 전달을 pure function으로 계산 | `ExternalVehiclePawn` |
| `SimCoreClientDiagnostics` | server health 표시 정책과 generation/map/sequence에 묶인 global E-stop health cache | `SimCoreClientComponent`, health tests |
| `SimCoreClientRuntimeEntities.cpp` | accepted authoritative NPC·보행자 snapshot cache, transient actor 생성·예측 표시·정리 | `SimCoreClientComponent` |
| `SimCoreClientTrafficSignals.cpp` | authoritative traffic head 표시, stale/unverified fail-safe와 actor 수명주기·진단 문자열 | `SimCoreClientComponent` |
| `SimCoreDriveReplayFormat.cpp` | track capture, sample, CSV serialize/parse와 fail-closed 형식 검증 | replay tests, runtime component |
| `SimCoreDriveReplay.cpp` | F5/F6 모드, 파일 I/O, ghost actor 생성·갱신·종료 | `USimCoreDriveReplayComponent` |

세단 표시 계약을 한 곳에 모아 Pawn·NPC·QA 사이의 경로와 wheel pivot 복제를 제거했다.
control resolver는 네트워크 전송이나 authoritative state 보관을 하지 않는다. diagnostics는
표시 전용이며 outgoing control 또는 lease recovery를 허용하는 권한 gate로 사용하지 않는다.
DriveReplay format/runtime 분리는 기존 `simcore-drive-replay-v1` CSV와
`Saved/DriveReplays/last_drive.csv` 경로를 유지한다.

## 공통 Windows 서버 launcher

`scripts/server_launcher_common.ps1`가 세 wrapper의 공통 동작을 제공한다.

- `run_signal_city_server.ps1`
- `run_virtual_city_server.ps1`
- `run_landscape_server.ps1`

각 wrapper는 기존 profile별 인자와 foreground/`-Background` 인터페이스를 유지한다. 공통
launcher는 필수 파일과 `ws_port`를 먼저 검증하고, 이미 사용 중인 포트를 임의로 종료하지
않는다. background 실행에서는 숨김 프로세스와 고유 stdout/stderr 로그를 만들고, 생성한
PID가 10초 안에 해당 포트를 listen하는지 확인한다. 시작 실패 시에도 자신이 만든 프로세스만
종료한다. 따라서 map 전환은 기존 listener를 명시적으로 종료한 뒤 수행해야 한다.

## 최종 검증

| 검증 | 결과 |
|---|---|
| C++ Release 전체 build와 CTest | build 성공, **20/20 성공** |
| UE 5.6 Editor/Game Development build와 전체 Automation | 두 build 성공, **47/47 성공** |
| Windows launcher self-test | **3 profiles 성공**: Signal City, Virtual City, Landscape |
| Python generated Proto 확인 | **1/1 성공** |
| Python smoke helper | **10/10 성공** |
| Signal City 실제 WebSocket smoke | 격리 자식 서버, **2,280 states 성공** |

위 결과는 protocol decoder, SimulationHost session, DriveReplay format/runtime, Unreal runtime
entity/traffic 표시와 공통 launcher를 포함한 최종 통합 체크포인트다. Signal City smoke는
NPC·보행자의 순항속도·경로 경계와 SafeStop/reset도 함께 검사한다.
자동 검증 완료가 실제 PIE 조작감·표시·60fps·30분 안정성의 수동 인수를 대신하지는 않는다.

통합 과정에서 collector 차선의 비연속 heading이 유한질량 NPC에 큰 입력 각속도를 만들고,
공통 collision substep 시간을 줄여 무관한 보행자 속도까지 오염시키는 문제를 발견했다.
`CollisionWorld`가 유한질량 OBB의 입력 각속도를 기존 `±4π rad/s` 상한으로 제한하도록 하고,
164rad/s NPC 옆의 무관한 보행자가 정확한 60Hz 보행 거리를 유지하는 회귀를 추가했다.
스모크의 기존 속도·경로 허용치는 넓히지 않았다.

## 변경 시 점검 순서

1. 공개 헤더와 Unreal reflection/subobject 계약의 변경 여부를 먼저 확인한다.
2. C++ Release build와 전체 CTest를 실행한다.
3. UE 5.6 Game/Editor build와 전체 Automation을 실행한다.
4. 각 launcher의 PowerShell parse, profile별 port/config 해석과 background readiness를 확인한다.
5. Signal City에서 Hello/reset/reconnect, 입력, HUD, replay와 runtime entity를 실제 PIE로 확인한다.

구체적인 시스템 책임은 [시스템 아키텍처](./03_architecture.md), 당일 실행 순서는
[2026-09-02 작업일지](./worklogs/2026-09-02.md)를 함께 본다. 2026-09-03의 문서 간
현재 상태 정합화는 [동기화 기록](./worklogs/2026-09-03.md)에 남겼다.
