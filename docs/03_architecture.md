# 시스템 아키텍처

## 1. 문서 정보

| 항목 | 값 |
|---|---|
| 버전 | 0.3 |
| 작성일 | 2026-08-14 |
| 대상 | R1 수동운전 및 R2/R3 자율주행 확장 기반 |
| 관련 문서 | [일정표](./01_schedule.md), [기능표](./02_feature_matrix.md) |

## 2. 설계 목표

1. C++ SimCore가 차량과 충돌의 최종 물리 상태를 정확하고 일관되게 계산한다.
2. Unreal은 C++ 결과를 고품질로 표현하고 입력·센서·교통 표현을 담당한다.
3. Python은 향후 FSD 판단만 담당하며 수동운전의 필수 데이터 경로에서 제외한다.
4. 지도·차선·충돌 데이터를 한 번 생성하여 C++·Unreal·Python이 함께 사용한다.
5. 수동 입력과 자율주행 명령이 동일한 제어 인터페이스를 사용한다.
6. Wall/Broad에 종속된 코드는 데이터 패키지로 격리하여 다른 지역과 트랙에 재사용한다.

## 3. 현재 구조와 목표 구조

### 3.1 현재 저장소 기준 구조

```mermaid
flowchart LR
    UE["Unreal 입력"] -->|"JSON WebSocket :9000"| CPP["C++ Host\n종방향 힘 + Bicycle 1단계"]
    CPP -->|"Protobuf / ZMQ :5555"| PY["Python Relay"]
    PY -->|"JSON WebSocket :8000/ws"| UE2["Unreal IG"]
```

현재 구조의 한계는 다음과 같다.

- C++ 물리는 ENU에서 종방향 힘과 kinematic bicycle 조향을 계산하지만 6DoF·타이어 슬립·서스펜션·충돌은 아직 없다.
- Proto는 루트 `protocol/vehicle.proto`로 통합됐지만 Envelope, ControlCommand와 Unreal 코드 생성은 아직 없다.
- 수동운전 상태가 Python relay를 반드시 거쳐 지연과 장애 지점이 늘어난다.
- 메시지에 simulation time, sequence, map checksum, control mode가 없다.
- 저장소에 추적되는 Unreal 프로젝트 구현이 아직 없다.

### 3.2 목표 구조

```mermaid
flowchart LR
    MI["Unreal ManualInput"] --> MUX["C++ ControlMux"]
    AI["Python AutonomyServer\nR2 이후"] --> MUX
    TD["Unreal TrafficDirector\nrule-based intent"] --> CORE
    MUX --> CORE["C++ SimCore\nClock + Vehicle + Collision"]
    MAP["Versioned MapPackage"] --> CORE
    MAP --> UE["Unreal IG"]
    MAP --> AI
    CORE -->|"WorldState 60Hz"| UE
    CORE -->|"State/Events"| AI
    UE -->|"Sensor streams, R2 이후"| AI
    CORE --> REC["Recorder / Replay"]
    UE --> REC
```

수동운전에서 Python 프로세스는 실행하지 않아도 된다. 자율주행 모드에서는 Python이 `ControlCommand`를 생성하지만 최종 물리 상태는 계속 C++가 결정한다.

## 4. 컴포넌트 책임

### 4.1 C++ SimCore

| 모듈 | 책임 | 소유 데이터 |
|---|---|---|
| `SimulationClock` | 고정 tick, substep, pause/reset, overrun 계측 | sim time, tick index |
| `ControlMux` | 수동/자율 명령 선택, timeout, E-stop | 현재 control lease와 command |
| `VehicleDynamics` | 자체 강체·조향·구동계·타이어·서스펜션 계산 | 차체·휠·타이어·구동계 상태 |
| `CollisionWorld` | 정적·동적 충돌 검출과 해결 | collision shapes, contact state |
| `EntityWorld` | Ego, NPC 차량, 보행자 proxy 생명주기 | authoritative entity state |
| `MapLoader` | MapPackage 검증과 물리용 데이터 로드 | map checksum, cooked collision |
| `TransportServer` | 명령 수신, 상태·이벤트 발행, handshake | connection/session state |
| `Recorder` | 명령·상태·이벤트와 설정 체크섬 기록·재생 | replay stream |
| `Diagnostics` | tick, latency, collision, queue 로그 | metrics와 structured log |

C++는 렌더링, 영상 센서 생성, 자율주행 의사결정을 담당하지 않는다.

### 4.2 Unreal

| 모듈 | 책임 | 금지되는 책임 |
|---|---|---|
| `SimCoreClient` | handshake, command 전송, WorldState 수신·버퍼링 | 최종 차량 pose 결정 |
| `ManualInputComponent` | 키보드/게임패드 입력 정규화 | 물리 계산 |
| `ExternalVehiclePawn` | pose 보간, wheel·steering 시각화 | Ego에 Chaos 힘을 적용해 C++ 상태 수정 |
| `GeoTransformAdapter` | ENU↔Cesium/Unreal 좌표 변환 | 자체 위·경도 적분 |
| `MapPackageImporter` | LaneGraph와 로컬 에셋 생성·검증 | 독립된 별도 차선 데이터 유지 |
| `TrafficDirector` | NPC 차선·속도·신호 intent 생성 | 충돌 후 최종 pose 해결 |
| `SignalController` | 차량·보행 신호 상태 | C++ 차량 물리 |
| `PedestrianDirector` | 단순 경로와 보행 intent, 애니메이션 | 대규모 군중 해석 |
| `SensorRig` | 카메라 등 센서 생성과 timestamp/frame metadata | AI 판단 |
| `ReplayPresenter` | 기록 상태 재생과 촬영 카메라 운용 | 기록된 물리 결과 재계산 |

### 4.3 Python AutonomyServer

R1에서는 필수가 아니며 R2부터 다음을 담당한다.

- 시작점·목적지와 LaneGraph 기반 route planning
- localization과 sensor preprocessing
- behavior planning과 trajectory generation
- steering, throttle, brake 명령 생성
- 자율주행 평가 지표와 실험 관리
- R3의 학습·추론 모델 실행

Python은 차량 동역학과 월드 충돌을 계산하지 않는다. 자율주행 중에도 C++ SimCore가 물리 권한을 유지한다.

## 5. 권한 모델

### 5.1 단일 물리 권한

| 상태 | 권한 소유자 | 다른 컴포넌트의 사용 방식 |
|---|---|---|
| Ego pose/velocity/wheel | C++ | Unreal과 Python은 읽기 전용 |
| NPC 물리 pose | C++ | Unreal AI가 intent를 보내고 결과를 표시 |
| 보행자 collision pose | C++ | Unreal이 경로 intent·animation을 제공하고 결과를 표시 |
| 신호 상태 | Unreal `SignalController` | tick이 붙은 상태를 C++와 Python에 공유 |
| LaneGraph·통행 제한 | MapPackage | 세 프로세스가 같은 버전 읽기 |
| 정적 collision source | MapPackage | C++가 authoritative collision로 cooking |
| 센서 영상 | Unreal | Python이 timestamp와 frame ID로 소비 |
| 운전 명령 | 선택된 controller | C++ `ControlMux`만 적용 여부 결정 |

### 5.2 제어 우선순위

```text
EmergencyStop > Active control lease(Manual 또는 Autonomous) > SafeStop
```

- Manual과 Autonomous가 동시에 차량을 제어하지 않는다.
- 모드 전환은 명시적인 요청·승인 handshake를 거친다.
- 유효 command가 250ms 동안 없으면 기본적으로 throttle을 해제하고 안전 제동한다.
- reset, spawn, map 변경은 일반 ControlCommand와 분리한다.

## 6. 좌표·단위·시간 규약

### 6.1 좌표계

| 계층 | 좌표계 | 사용처 |
|---|---|---|
| 지리 원본 | WGS84 longitude/latitude/ellipsoidal height | Cesium 위치, GNSS 출력, 지도 import |
| 물리 기준 | Local ENU, X=East, Y=North, Z=Up | C++ 위치·속도·충돌 계산 |
| Unreal 표시 | Unreal local world + Cesium transform | 렌더링, 카메라, 센서 |
| 차량 body frame | X=forward, Y=left, Z=up을 canonical로 정의 | 휠·센서 장착·차량 상태 |

- `MapOrigin`은 WGS84 원점과 ENU frame을 정의한다.
- Unreal의 축·handedness 차이는 `GeoTransformAdapter` 한 곳에서만 변환한다.
- 위치는 meter, 속도는 m/s, 가속도는 m/s², 질량은 kg, 시간은 second를 사용한다.
- 물리 내부 각도와 각속도는 radian 기반으로 통일하고 UI에서만 degree로 표현한다.
- 위·경도는 물리 적분에 사용하지 않고 ENU 상태에서 필요할 때 변환한다.
- 원점, 축, quaternion 회전은 왕복 자동 시험을 작성한다.

### 6.2 시간 모델

- 기본 physics tick: 고정 60Hz
- 자체 물리 모델의 수치 안정성에 필요하면 한 tick 안에서 설정 가능한 substep 사용
- 기본 WorldState 발행: 60Hz
- Unreal render tick: physics와 독립
- 모든 명령·상태·센서에 `tick_index` 또는 `simulation_time_ns` 포함
- wall-clock Unix timestamp는 로그 상관관계에만 사용하고 물리 적분에 사용하지 않음
- pause와 replay 시 sim time을 임의로 wall time에 맞추지 않음

## 7. 공통 MapPackage

### 7.1 목적

Cesium의 시각 타일, Unreal spline, C++ 충돌맵을 각각 손으로 관리하면 반드시 어긋난다. 하나의 지도 원본을 전처리하여 모든 런타임 산출물을 생성한다.

```mermaid
flowchart LR
    SRC["NYC DCM / 허용된 OSM·로컬 에셋"] --> PRE["Offline Map Builder"]
    PRE --> PKG["MapPackage source"]
    PKG --> COOK["C++ collision cooking"]
    PKG --> IMP["Unreal Editor Importer"]
    PKG --> ROUTE["Python route graph loader"]
    COOK --> CORE["C++ CollisionWorld"]
    IMP --> LEVEL["Local road·sidewalk·spline"]
```

NYC Digital City Map의 street centerline은 도로명과 폭을 가진 공식 지도 기반 데이터로 사용할 수 있다. 실제 lane 수, 방향, 회전 연결, 신호, 통행 제한은 별도 속성과 수동 override가 필요하다. Cesium for Unreal은 GeoJSON/SHP를 직접 lane spline으로 변환하지 않으므로 offline builder와 Unreal importer를 둔다.

### 7.2 소스 패키지 예시

```text
maps/wall_broad/
  manifest.json
  origin.json
  lane_graph.json
  road_semantics.json
  collision_mesh.glb
  signals.json
  pedestrian_paths.json
  spawn_points.json
  overrides.json
  attribution.md
```

`manifest.json`에는 다음을 기록한다.

- package ID와 semantic version
- 작성 도구와 schema version
- WGS84 원점과 축 규약
- 각 파일의 SHA-256
- 원본 데이터의 이름·버전·취득일·라이선스
- Unreal용 및 C++용 cooked artifact가 참조한 source checksum

### 7.3 LaneGraph

LaneGraph는 다음 정보를 가진 방향 그래프다.

- lane node와 directed edge
- center polyline과 폭
- 제한 속도와 진행 방향
- 차량·보행 통행 가능 여부
- predecessor/successor와 좌·우 인접 lane
- 교차로 진입·진출 연결
- 정지선, 횡단보도, 신호 그룹
- 수동 보정 provenance

초기 생성은 도로 중심선을 기반으로 하되 lane offset을 자동 생성한다. 개발자는 모든 spline을 직접 깔지 않고 Wall/Broad 핵심 교차로와 통행 제한만 보정한다. 동일 LaneGraph를 Unreal NPC, Python A*, C++ 도로 경계 검증에 사용한다.

### 7.4 충돌 일치 절차

1. 로컬 주행면과 충돌 source mesh를 Map Builder가 생성한다.
2. C++는 source checksum을 확인하고 자체 충돌 월드용 표현과 cache를 생성한다.
3. Unreal은 같은 source를 디버그 표현 및 query geometry로 import한다.
4. 실행 handshake에서 `map_package_checksum`과 `collision_source_checksum`을 비교한다.
5. 다르면 물리 시뮬레이션을 시작하지 않는다.
6. Ego의 실제 contact solving은 C++만 수행한다.

Cesium 스트리밍 타일의 collision은 타일 LOD와 로딩 상태가 변할 수 있으므로 authoritative 주행 충돌로 사용하지 않는다.

## 8. Cesium과 로컬 환경의 경계

### 8.1 Cesium이 담당하는 부분

- Georeference와 WGS84 변환
- 지도 원점 배치
- 목표 장비 성능을 통과할 경우 제한된 중·원경 배경
- 지리 데이터 정렬을 위한 Editor 기준

### 8.2 로컬 Unreal 환경이 담당하는 부분

- 차량이 실제로 닿는 도로 표면
- 보도, 커브, 정지선, 횡단보도
- 핵심 촬영 구간의 재질과 소품
- 고정 충돌 source와 시각적으로 맞는 주행면
- 주요 랜드마크의 촬영 품질

Cartographic Polygon의 invert/exclude 기능으로 목표 구역 바깥의 완전한 타일을 제외할 수 있지만, 경계에 걸친 타일은 로딩될 수 있고 시각 클리핑이 물리 충돌을 자동으로 자르지는 않는다. 따라서 이는 배경 최적화 수단이지 물리 데이터 생성 수단이 아니다.

Google Photorealistic 3D Tiles는 현재 목표 PC에서 측정된 성능 문제와 데이터 추출·저장 제약 때문에 R1에서 제외한다. 사용자 소유 데이터나 합법적으로 내려받을 수 있는 지리 데이터를 crop하여 로컬 에셋 또는 자체 3D Tiles로 만드는 방식은 허용한다.

## 9. 차량 물리 구조

### 9.1 자체 물리 경계

```cpp
class IVehicleDynamics {
public:
    virtual ~IVehicleDynamics() = default;
    virtual void initialize(const VehicleConfig&, const VehicleSpawn&) = 0;
    virtual void apply_control(const ControlCommand&) = 0;
    virtual void step(double fixed_dt, const CollisionWorld&) = 0;
    virtual VehicleState snapshot() const = 0;
};
```

실제 인터페이스는 구현 중 조정할 수 있지만, SimCore의 transport·recording·entity 코드는 구체적인 차량 수식과 내부 상태 타입을 직접 노출하지 않는다. 이후 비교 검증이나 요구 변경으로 외부 SDK를 시험하더라도 이 경계 밖의 코드를 바꾸지 않는 것이 목적이다.

### 9.2 구현 결정과 현재 단계

D1에 외부 차량 SDK를 런타임으로 채택하지 않고 현재 C++ 서버에 차량 물리를 직접 구현하기로 결정했다. Chrono::Vehicle 10.0.0의 차량 1대·4대, 정적 벽 충돌, 반복성 결과는 비교 기준으로 보존하며 제품 의존성에는 포함하지 않는다. 상세 근거는 [ADR-006](./decisions/ADR-006-custom-vehicle-physics.md)에 기록한다.

현재 1단계 모델은 다음을 계산한다.

- 질량과 구동력·제동력으로 종방향 가속도 계산
- 공기저항과 구름저항
- Drive, Neutral, Reverse와 단일 기어비 RPM 근사
- 휠베이스와 road wheel angle을 사용하는 kinematic bicycle yaw rate
- Local ENU east/north 위치와 WGS84 출력
- 입력 clamp, 비정상 `dt` 방어, 정지·가속·제동·후진·회전 회귀 시험

다음 단계에서 3D 강체, 바퀴별 접촉, 타이어 힘, 서스펜션, 노면과 충돌을 같은 인터페이스 안에 추가한다. Unreal은 자체 물리 결과를 표시하며 Chaos로 Ego pose를 다시 해결하지 않는다.

### 9.3 정확도의 정의

R1에서 “정확한 C++ 물리”는 다음을 의미한다.

- 시간, 좌표, 단위, 충돌 결과가 Unreal과 일관됨
- 차량 모델에 차체·휠·타이어·서스펜션·구동·제동이 존재함
- 설정된 파라미터에 대해 직진·제동·회전·경사·접촉 시험을 반복 통과함
- replay로 회귀 여부를 측정할 수 있음

특정 실차와 수치적으로 일치한다는 의미는 아니다. 실차 정확도를 선언하려면 대상 차종, 질량 분포, 타이어, 서스펜션, 동력계와 기준 주행 계측 데이터가 추가로 필요하다.

## 10. 통신 아키텍처

### 10.1 R1 transport 후보

현재 1순위 후보는 기존 Boost WebSocket 서버를 전이중 binary 채널로 확장하고, 최신성이 중요한 상태 채널을 분리하는 것이다. WebSocket binary, UDP, Protobuf, FlatBuffers의 최종 조합은 Unreal 통합 난이도·지연·패킷 크기 측정 후 별도 결정한다.

- 신뢰성 채널에서 handshake·reset·설정·생명주기 메시지를 교환
- 실시간 채널에서 Unreal→C++ command와 C++→Unreal state를 교환
- JSON은 개발용 콘솔·디버깅에서만 허용
- Python relay는 수동운전 경로에서 제거
- transport와 serialization은 각각 인터페이스 뒤에 두어 측정 결과에 따라 교체 가능
- 센서 영상은 같은 socket에 싣지 않음

### 10.2 메시지 계층

```text
Envelope
  schema_version
  sequence
  simulation_time_ns
  source_id
  map_package_checksum
  payload
```

| 메시지 | 방향 | 핵심 필드 |
|---|---|---|
| `Hello/Handshake` | 양방향 | build, schema, map checksum, capabilities |
| `ControlCommand` | Controller→C++ | mode, throttle, brake, steering, handbrake, gear |
| `AgentIntent` | Unreal→C++ | entity, target lane, target speed, stop/continue |
| `WorldState` | C++→소비자 | entity pose, velocity, acceleration, wheel, contact |
| `SignalState` | Unreal→C++/Python | signal group, phase, effective tick |
| `LifecycleCommand` | Unreal→C++ | spawn, despawn, reset, map load |
| `Health` | 양방향 | tick overrun, packet age, queue depth, errors |
| `SensorMetadata` | Unreal→Python/Recorder | sensor, frame, pose, sim time, payload reference |

### 10.3 상태 표시

- C++는 매 physics tick에 immutable snapshot을 생성한다.
- Unreal은 짧은 상태 버퍼를 사용해 render frame에서 보간한다.
- 최신 상태 이후의 외삽은 짧은 제한 시간만 허용하고, 제한을 넘으면 상태를 고정하고 연결 경고를 표시한다.
- Unreal collision response가 Ego transform을 변경하지 않는다.
- 시각 오차는 C++ pose와 보간 기준 pose를 함께 HUD에 표시하여 측정한다.

## 11. Traffic과 보행자

규칙 기반 AI와 물리를 분리한다.

```mermaid
sequenceDiagram
    participant Signal as SignalController
    participant AI as Traffic/Pedestrian Director
    participant Core as C++ SimCore
    participant IG as Unreal IG
    Signal->>AI: phase + effective tick
    AI->>Core: AgentIntent
    Core->>Core: fixed tick + collision solve
    Core->>IG: authoritative WorldState
    IG->>IG: pose interpolation + animation
```

- TrafficDirector는 LaneGraph, 신호, 선행 차량을 보고 target lane과 target speed를 정한다.
- C++는 NPC 차량을 동역학 또는 R1용 제한된 kinematic body로 진행시키고 충돌 상태를 확정한다.
- PedestrianDirector는 경로와 대기/횡단 intent를 정한다.
- C++는 보행자의 capsule pose를 tick에 맞춰 진행시켜 Ego 충돌과 화면 위치가 같은 상태를 참조하게 한다.
- 복잡한 군중 회피, 충돌 파손, 교통 수요 모델은 R1에 포함하지 않는다.

## 12. 센서 확장 구조

모든 센서는 차량 body frame에 상대적인 `SensorMount`를 가진다.

```text
SensorMount
  sensor_id
  sensor_type
  parent_frame_id
  translation_m
  rotation_quaternion
  update_rate_hz
  latency_ms
  enabled
  parameters
```

- RGB/Depth/Segmentation은 Unreal `SceneCaptureComponent2D` 계열로 구현한다.
- GNSS/IMU는 C++ ground-truth 상태에서 noise/bias 모델을 적용한다.
- LiDAR/Radar는 구현 방식과 성능 예산을 R2에서 별도 결정한다.
- 모든 센서는 캡처 시점의 sim time과 world/body/sensor transform을 기록한다.
- GPU readback과 인코딩은 game thread를 차단하지 않는 비동기 파이프라인으로 설계한다.
- 센서 payload와 control/state 메시지 transport를 분리한다.

R1은 SensorRig, mount config, timestamp/frame metadata와 선택적인 저주기 RGB smoke test까지만 수행한다.

## 13. 기록·재생·촬영

### 13.1 기록 대상

- build ID, schema version
- MapPackage와 collision source checksum
- vehicle config checksum
- tick 설정과 random seed
- 모든 적용된 ControlCommand와 AgentIntent
- authoritative WorldState와 collision event
- 신호 상태 변화
- 연결·timeout·reset 이벤트

### 13.2 재생 모드

| 모드 | 용도 |
|---|---|
| Input replay | 같은 입력을 C++에서 다시 계산하여 결정성과 회귀 검증 |
| State replay | 기록된 상태를 Unreal에서 그대로 재생하여 영상 촬영 |

영상은 실시간 플레이 캡처보다 State replay를 우선 사용한다. 동일한 주행에 카메라와 품질 설정을 바꿔 Movie Render Queue로 촬영할 수 있고, 녹화 부하가 물리 결과에 영향을 주지 않는다.

## 14. 실패 처리

| 실패 | C++ 동작 | Unreal 표시 |
|---|---|---|
| command timeout | throttle 해제, 안전 제동 | timeout 경고와 command age |
| map checksum 불일치 | simulation start 거부 | 양쪽 checksum과 해결 방법 표시 |
| state packet gap | 계속 시뮬레이션, gap 계측 | 제한된 보간 후 freeze·경고 |
| Unreal 연결 종료 | 설정에 따라 안전 정지 또는 headless 지속 | 재연결 후 full snapshot 요청 |
| tick overrun | 누적 wall time 보정으로 dt를 키우지 않고 overrun 기록 | 성능 HUD 표시 |
| invalid numeric state | 해당 tick 적용 중단, snapshot 보존, 치명 로그 | 시뮬레이션 중지 안내 |

## 15. 시험 구조

### 15.1 C++ 단위·회귀 시험

- 좌표·quaternion 왕복
- command clamp와 timeout
- fixed tick 독립성
- 정지·가속·제동·회전·경사
- 정적 mesh 및 동적 OBB/capsule 충돌
- 동일 input replay
- Proto backward compatibility와 invalid packet
- MapPackage checksum과 schema validation

### 15.2 Unreal 자동·통합 시험

- MapPackage import 결과 수와 bounds
- ENU↔Unreal transform 왕복
- handshake 성공·실패
- state buffer의 보간·gap·reconnect
- Ego physics 권한 위반 탐지
- TrafficDirector 신호 준수
- SensorMount transform과 timestamp

### 15.3 성능 시험

- 목표 Windows PC의 패키지 빌드에서 수행
- 동일한 replay, 카메라, 해상도, 그래픽 설정 사용
- Game thread, Render thread, GPU, frame p50/p95, streaming hitch 기록
- NPC 수, 보행자 수, 센서 활성 상태를 결과에 함께 기록
- Cesium 배경 on/off를 별도 측정해 R1 포함 여부 결정

## 16. 배포 구성

```text
release/
  SimCore.exe
  DriveIG/
  config/
    runtime.json
    vehicle.json
    sensors.json
  maps/
    wall_broad/
  proto/
    schema_version.txt
  logs/
  replays/
  THIRD_PARTY_NOTICES.md
  RUNBOOK.md
```

권장 실행 순서는 다음과 같다.

1. SimCore가 config와 MapPackage를 검증하고 대기한다.
2. Unreal 패키지가 연결해 schema·map handshake를 수행한다.
3. 사용자 선택으로 Manual control lease를 획득한다.
4. C++가 tick을 시작하고 WorldState를 발행한다.
5. Unreal이 IG, traffic intent, sensor, recording을 시작한다.
6. 종료 시 replay와 성능·오류 로그를 flush한다.

## 17. 아키텍처 결정 기록

| ADR | 상태 | 결정 | 이유 |
|---|---|---|---|
| ADR-001 | 확정 | Cesium+로컬 에셋 하이브리드 | 실제 좌표를 유지하면서 주행면 품질·성능·고정 충돌 확보 |
| ADR-002 | 확정 | C++가 단일 물리 권한 | C++/Unreal 충돌 보정 경쟁 제거와 headless 확장 |
| ADR-003 | 확정 | Python을 수동운전 필수 경로에서 제거 | 지연·장애 지점 감소, Python을 FSD 역할로 한정 |
| ADR-004 | 확정 | MapPackage를 지도 single source of truth로 사용 | 차선·충돌·경로 데이터 불일치 방지 |
| ADR-005 | 제안 | R1은 전이중 WebSocket+binary Protobuf | 현재 코드 재사용과 직접 연결의 구현량 균형 |
| ADR-006 | 확정 | 현재 C++ 서버에 자체 차량 물리 구현 | 차량 수식·상태·오차를 직접 설명하고 수정하는 학습·포트폴리오 목표 |
| ADR-007 | 확정 | R1은 수동운전, 학습은 연기 | 8월 품질과 기반 구조에 집중 |
| ADR-008 | 확정 | 핵심부 보행 중심, 주변 도로 주행 | Wall/Broad의 실제 공간 특성과 사용자 요구 반영 |
| ADR-009 | 확정 | 센서 payload를 제어 채널과 분리 | 이미지 readback이 physics/control 지연을 만들지 않도록 함 |

## 18. 결정 대기 사항

| 항목 | 현재안 | 기한 |
|---|---|---|
| Unreal/Cesium 버전 | 호환성과 목표 PC 안정성이 확인된 고정 버전 | D1 |
| 자체 물리 Windows 게이트 | MSVC Release 빌드와 동일 입력 회귀 시험 | D5 전 |
| 실시간 transport·serialization | WebSocket binary/UDP와 Protobuf/FlatBuffers 비교 측정 | D3 전 |
| 기준 차량 | 확보한 차량 에셋과 제원이 일치하는 일반 승용차 | D3 |
| 지도 경계 | NYSE·Federal Hall과 주변 차량 루프를 포함하는 4~6블록 | D8 전 |
| 배경 방식 | 로컬 핵심부 + 성능 통과 시 제한된 Cesium 중·원경 | D16 프로파일링 후 최종 |
| 라이선스 에셋 | 기존 구매·보유 에셋 목록 확인 필요 | D8 전 |

## 19. 참고 자료

- [Cesium for Unreal clipping](https://cesium.com/learn/unreal/unreal-clipping/)
- [Cesium for Unreal FAQ: 데이터 형식·다운로드·오프라인](https://cesium.com/learn/unreal/unreal-faq/)
- [NYC Digital City Map](https://www.nyc.gov/content/planning/pages/resources/datasets/digital-city-map)
- [NVIDIA PhysX Vehicle 문서](https://nvidia-omniverse.github.io/PhysX/physx/5.1.0/docs/Vehicles.html)
- [Project Chrono Vehicle 문서](https://api.projectchrono.org/manual_vehicle.html)
- [Unreal Engine Set Actor Transform](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/Transformation/SetActorTransform)
- [Google Map Tiles API 정책](https://developers.google.com/maps/documentation/tile/policies)

## 20. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| 0.3 | 2026-08-14 | ADR-006을 자체 C++ 물리엔진 결정으로 변경하고 D1 기본 모델·공통 Proto 상태 반영 |
| 0.2 | 2026-08-14 | Chrono::Vehicle 스파이크와 통합안을 기록; 0.3에서 런타임 채택 철회 |
| 0.1 | 2026-08-14 | C++ 단일 물리 권한, 공통 MapPackage, 직접 통신, 센서 확장 구조 최초 작성 |
