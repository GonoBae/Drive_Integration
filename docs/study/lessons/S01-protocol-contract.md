# S01: 공통 Protobuf 계약 읽기

## 문서 기준

| 항목 | 값 |
|---|---|
| 최초 작성 | 2026-08-19 |
| 기준 코드 | schema v2, 2026-08-21 좌표·부호 계약 기준 |
| 기준 symbol | `VehicleGear`, `ControlMode`, `Vector3d`, `WheelState`, `EntityState`, `ControlCommand`, `Envelope` |
| 재검토 조건 | 위 symbol, field number, schema version 또는 좌표·부호 의미 변경 |

## 1. 이 파일을 먼저 읽는 이유

[vehicle.proto](../../../protocol/vehicle.proto)는 R1의 C++·Unreal과 향후 Python 자율주행이 공유할 데이터 계약의 원본이다. 현재 Python relay는 동결된 선택 기능이므로 R1 흐름을 공부할 때는 C++↔Unreal 계약을 기준으로 읽는다. 각 언어의 구현을 먼저 읽으면 같은 데이터가 서로 다른 struct와 naming으로 보여 전체 흐름을 놓치기 쉽다. 공통 계약을 먼저 이해하면 이후 코드에서 “무슨 데이터를 왜 변환하는가”를 기준으로 읽을 수 있다.

transport와 protocol은 구분한다.

- WebSocket과 ZMQ: byte를 어디로 어떻게 전달할지 결정하는 transport
- Protobuf: byte 안에 어떤 필드와 의미를 담을지 결정하는 protocol/serialization

## 2. 세 구간

| 구간 | 행 | 주제 | 상태 |
|---|---:|---|---|
| P1 | 1~21 | proto3, package, enum default, `Vector3d`, field number | 복습 필요 |
| P2 | 23~90 | wheel/entity state, command, 단위와 좌표 계약 | 복습 필요 |
| P3 | 92~119 | Hello, Health, Envelope, `oneof`, version/session/sequence | **진행 중** |

P1~P3를 이해한 뒤 C++ [vehicle_protocol_test.cpp](../../../cpp/host/tests/vehicle_protocol_test.cpp)에서 실제 직렬화·역직렬화 계약을 확인한다. C++ adapter 구현은 S07에서 다시 정독한다.

## 전체 message 지도

| message | 한 줄 역할 | 현재 사용 상태 |
|---|---|---|
| `Vector3d` | X/Y/Z 세 값을 묶는 공통 3차원 vector | 위치·속도·접촉점 등에 사용 |
| `WheelState` | 바퀴 하나의 접촉·조향·회전·slip·힘 상태 | C++가 4개 생성, Unreal이 수신 |
| `EntityState` | 차량/NPC 등 엔티티 하나의 pose·동역학·wheel 상태 | 현재 주로 Ego 차량 상태 |
| `EntityStatePacket` | EntityState 목록을 직접 담는 버전 없는 과거 observer packet | default-OFF ZMQ·동결 Python relay에서만 보존 |
| `WorldState` | 한 tick의 EntityState 목록 | Envelope 안에서 C++→Unreal WebSocket으로 전송 |
| `ControlCommand` | throttle·brake·steering·gear·E-stop 제어 명령 | Unreal→C++ 수동운전 입력 |
| `Hello` | build·schema·capability 호환성 협상 | schema에 정의됐지만 handshake는 미완성 |
| `Health` | tick overrun·command age·상태 메시지 진단 | schema에 정의됐지만 runtime 발행은 미완성 |
| `Envelope` | version·sequence·time·source·session과 payload를 감싸는 공통 외피 | R1 ControlCommand·WorldState WebSocket에서 사용 |

`EntityStatePacket`과 `WorldState`는 모두 EntityState 목록을 담지만 경로와 안전성이
다르다. R1 기본 runtime은 metadata와 version이 있는 `Envelope{WorldState}`만 Unreal에
보낸다. `SIMCORE_ENABLE_ZMQ_OBSERVER`를 명시적으로 켠 별도 빌드만 버전 없는
`EntityStatePacket`을 발행한다. 동결된 Python relay는 이 구 packet을 읽지만 schema
version을 검사할 수 없으므로 R1 계약이나 자율주행 기반으로 간주하지 않는다.

### 수동운전 왕복에서의 message 흐름

```text
Unreal key input
→ FControlCommand
→ Envelope{ control_command }
→ WebSocket binary
→ C++ protocol parser
→ VehicleInput
→ VehiclePhysics::set_input / update
→ VehicleState
→ WorldState
→ Envelope{ world_state }
→ WebSocket binary
→ Unreal parser / Pawn 표시
```

`Envelope`의 `oneof payload`에는 한 번에 ControlCommand 또는 WorldState 중 하나만 들어간다. `VehiclePhysics`는 Protobuf를 직접 알지 않으며, protocol adapter가 `ControlCommand↔VehicleInput`, `VehicleState↔WorldState`를 변환한다.

선택적 legacy observer는 위 수동운전 왕복에서 갈라지는 필수 단계가 아니다. 켜면 C++가
별도로 `EntityStatePacket`을 만들어 ZMQ로 발행하며, 기본 빌드에서는 해당 코드와 포트가
비활성화된다.

## 3. P1 학습 목표

P1이 끝나면 다음 질문에 코드 없이 답할 수 있어야 한다.

- `proto3`가 무엇이며 값이 오지 않았을 때 scalar와 enum이 어떻게 보이는가?
- `package simcore`는 생성된 언어별 코드에 어떤 영향을 주는가?
- enum의 첫 값이 0이어야 하는 이유는 무엇인가?
- `Vector3d`가 `float`가 아닌 `double`을 사용하는 이유는 무엇인가?
- `= 1`, `= 2`, `= 3`은 값이나 배열 순서가 아니라 무엇인가?
- 공개된 field number를 변경하거나 재사용하면 왜 위험한가?

## 4. P1 한 줄씩 읽기

### 1행: `syntax = "proto3";`

이 파일이 proto3 문법과 기본값 규칙을 사용한다고 선언한다. 수신 데이터에 일반 scalar가 없으면 숫자는 0, bool은 false, string은 빈 문자열, enum은 0번 값으로 관찰된다. “전송되지 않음”과 “기본값을 전송함”을 구분해야 하는 필드는 `optional` 또는 message presence를 사용해야 한다.

### 3행: `package simcore;`

메시지 이름의 충돌을 막는 논리적 namespace다. C++ 생성 코드에서는 `simcore::Envelope`처럼 사용된다. 이 package는 네트워크 주소나 실행 프로세스 이름이 아니다.

### 5~9행: `VehicleGear`

기어를 문자열 대신 고정된 정수 계약으로 표현한다. 수신 시 값이 없으면 proto3 enum의 기본값인 0, 즉 `NEUTRAL`로 보인다. field 이름보다 숫자가 wire 호환성의 핵심이므로 이미 사용한 enum 번호의 의미를 바꾸지 않는다.

### 11~15행: `ControlMode`

수동·자율·비상정지 제어 의도를 구분한다. 현재 0번은 `MANUAL`이므로 mode가 누락된 command도 생성 코드에서는 manual로 보일 수 있다. 이것이 안전한지, `UNSPECIFIED=0`을 따로 둘지는 P3의 검증 경계와 함께 검토할 질문이다.

### 17~21행: `Vector3d`

세 축을 반복 사용하기 위한 message다. `double`은 위치·좌표를 `float`보다 높은 정밀도로 전달한다. `x = 1`의 1은 x값이 아니라 wire tag인 field number다. 수신자는 이름이 아니라 이 번호와 wire type으로 필드를 식별한다.

### `message`와 C++ struct의 차이

Protobuf의 `message`는 특정 언어의 `struct`가 아니라 언어 중립적인 구조화 데이터 타입 선언이다. `protoc`가 이를 C++로 생성하면 일반적으로 public field를 가진 단순 struct가 아니라 getter, setter, presence, parsing, serialization 기능을 가진 class가 된다.

예를 들어 `message Vector3d`에서 생성된 C++ 타입은 개념적으로 다음처럼 사용한다.

```cpp
simcore::Vector3d value;
value.set_x(1.0);
const double x = value.x();
const std::string bytes = value.SerializeAsString();
```

프로젝트의 물리 domain struct와 Protobuf 생성 class는 별도 타입이다. `vehicle_messages.cpp`의 adapter가 두 타입 사이를 변환한다.

### field number가 필요한 이유

`uint32 wheel_index = 1`의 `1`은 기본값이나 바퀴 번호가 아니라 Protobuf wire tag에 포함되는 field number다. binary payload에는 보통 `wheel_index`라는 문자열 이름을 넣지 않고 `field number + wire type + value`를 넣는다. 수신자는 같은 schema의 field number를 보고 값을 어느 멤버에 넣을지 결정한다.

- field number는 한 `message` 안에서 서로 달라야 한다.
- 다른 `message`에서는 같은 번호를 다시 사용할 수 있다.
- 이미 배포한 번호의 의미를 바꾸거나 다른 필드에 재사용하지 않는다.
- 새 필드는 새 번호로 추가한다. 과거 수신자는 모르는 번호를 건너뛸 수 있다.
- 삭제한 번호는 `reserved`로 막아 실수로 재사용하지 않는 것이 안전하다.
- 자주 쓰는 1~15번은 tag가 일반적으로 1byte라서 조금 더 작다.

이 프로젝트의 Unreal parser는 `WheelField`를 읽고 `case 1`은 `WheelIndex`, `case 3`은 `SteeringAngleRad`, `case 10`과 `case 11`은 각각 `ContactPointEnu`와 `ContactNormalEnu`로 변환한다. 향후 추가된 모르는 field는 `Skip()`하므로 additive 변경이 기존 message 파싱을 반드시 깨뜨리지는 않는다. 다만 Unreal은 현재 wire field를 수동으로 대응하므로 루트 Proto와 계속 동기화해야 한다.

`wheel_index = 1`에는 서로 다른 의미의 숫자가 함께 보인다.

```text
wheel_index = 1
     │        └─ schema의 field number: 항상 1
     └─ runtime value: 각 WheelState마다 0, 1, 2, 3 중 하나
```

주석의 `0=front-left ... 3=rear-right`는 `uint32` 값에 프로젝트가 부여한 의미다. C++ 물리 루프는 배열 index를 그대로 `wheel_index`에 넣고, Unreal은 이 값을 `WheelMeshes[WheelIndex]`의 선택과 앞/뒤 차축 판정에 사용한다. 따라서 네 개의 WheelState가 모두 wire tag 1을 사용하지만 그 안의 실제 값은 각 바퀴에 따라 다르다.

## 5. P2 시작: `repeated`

`repeated T name = N`은 같은 타입 `T`의 값을 0개 이상 담을 수 있는 목록을 뜻한다. C++ 관점에서는 `std::vector<T>`와 비슷하지만, 실제 타입과 API는 Protobuf가 생성한다.

```proto
repeated WheelState wheels = 21;
```

위 선언은 하나의 `EntityState` 안에 WheelState를 여러 개 넣을 수 있다는 뜻이다. 이 프로젝트는 정상 차량 상태에서 4개를 보내지만, Proto schema 자체는 정확히 4개라고 강제하지 않으므로 0개, 1개 또는 5개도 wire 형식상 가능하다. 정확히 4개라는 조건은 host 로직과 시험이 검증해야 한다.

생성된 C++ API는 개념적으로 다음처럼 사용한다.

```cpp
WheelState* wheel = entity.add_wheels();
const int count = entity.wheels_size();
const WheelState& first = entity.wheels(0);
```

message 타입의 repeated field는 각 원소가 같은 field number 21로 반복해서 나타난다. 원소 순서는 유지되지만 중복을 금지하지 않으며, 이 프로젝트는 순서만 믿지 않고 각 원소의 `wheel_index`로 바퀴 위치를 명시한다.

프로젝트의 다른 예는 `WorldState.entities`, legacy
`EntityStatePacket.entities`, `Hello.capabilities`다.

## 6. P3 시작: `Hello`, `Health`, `oneof`

`Hello`와 `Health`도 `Envelope`의 payload로 들어갈 수 있다. `oneof payload`이므로 한 Envelope에는 아래 네 종류 중 하나만 들어간다.

```text
Envelope{ hello }
Envelope{ control_command }
Envelope{ world_state }
Envelope{ health }
```

현재 흐름에서는 연결 직후 양방향 `Hello`로 source/build·schema·map checksum·capability를
검증한다. handshake 뒤 ControlCommand와 WorldState가 각각 별도 Envelope frame으로 오간다.
실행 중 tick overrun·command age·오류 상태를 전달할 authoritative `Health`는 목표 상태다.

현재 runtime 구현은 이 목표 중 일부만 완료됐다.

| payload | schema 정의 | 현재 송수신 구현 |
|---|---|---|
| `ControlCommand` | 완료 | Unreal→C++ 구현 |
| `WorldState` | 완료 | C++→Unreal WebSocket 구현 |
| `Hello` | 완료 | C++↔Unreal 양방향 application handshake 구현 |
| `Health` | 완료 | runtime 발행·표시 미구현 |

WebSocket의 HTTP 101 handshake는 transport 연결을 여는 절차일 뿐 Protobuf `Hello`
handshake가 아니다. HTTP 101 뒤 C++는 server Hello를, Unreal은 client Hello를 보내고 양쪽이
이를 승인한 뒤에만 ordinary state/reset/control이 흐른다. `Health` runtime 발행·표시는 아직
없다.

현재 schema v2 runtime은 R1 WebSocket의 `Envelope.schema_version`을 정확히 검사한다.
C++는 v1 ControlCommand를 적용하지 않고 Unreal 소스는 v1 WorldState를 incompatible
상태로 처리하도록 맞춰져 있다. 동결된 Python relay는 version field가 없는 legacy
`EntityStatePacket`을 읽으므로 이 exact gate에 포함되지 않는다. 이는 R1 packet
호환성 차단선에 더해 현재 Hello가 source/build·capability·map checksum을 교차 검증한다.

현재 학습에서는 다음을 실제 구현과 시험으로 추적한다.

- `Hello`와 `Health`가 Envelope의 선택 가능한 payload라는 점
- `Hello`는 application-level 호환성 협상, `Health`는 runtime 진단이라는 책임
- HTTP 101 handshake와 Protobuf Hello의 차이
- server/client Hello 순서, 필수 capability·schema·map checksum 실패와 pre-Hello payload
  차단 시험

Hello handshake 상태기계·capability 협상과 diagnostics HUD는 현재 구현 코드와 시험으로
학습한다. Health 발행 주기와 authoritative SafeStop/Health HUD는 구현 뒤 다시 학습하며,
존재하지 않는 Health 흐름을 추측하지 않는다.

## 7. 첫 손 추적

다음 두 메시지를 받았다고 가정한다.

```text
A: VehicleGear 필드가 아예 오지 않음
B: VehicleGear 값으로 숫자 0이 옴
```

일반 enum field에서는 생성 코드가 둘 다 `VEHICLE_GEAR_NEUTRAL`로 보여줄 수 있다. 누락과 명시적 Neutral을 구분해야 하는 command의 gear가 `optional`인 이유는 P2에서 이어서 확인한다.

## 8. Teach-back 질문

파일을 보지 않고 1분 안에 다음을 설명한다.

> `vehicle.proto` 1~21행이 정의하는 것과, enum 0번 및 field number가 중요한 이유를 설명해 주세요.

## 9. Quiz — 먼저 답한 뒤 해설 확인

[S01-P1 Quiz 정답 및 해설](../qa.md#s01-p1-quiz)에 각 문제의 정답을 항상 접어서 제공한다.

1. `VehicleGear` 이름을 `Gear`로 바꾸되 field number와 enum number를 유지하면 wire byte를 읽는 데 반드시 실패할까?
2. `VEHICLE_GEAR_DRIVE`의 번호를 1에서 2로 바꾸면 기존 기록 데이터에 어떤 문제가 생길까?
3. `ControlMode`가 누락된 command는 현재 생성 코드에서 어떤 mode로 보일 가능성이 큰가?
4. `Vector3d.x = 1`에서 1은 x의 기본값인가?
5. 위도·경도나 큰 ENU 좌표에 `double`을 쓰는 이유를 `float`의 정밀도와 연결해 설명할 수 있는가?

## 10. 첫 체크포인트

이번 구간에서는 production 코드를 수정하지 않는다. 위 다섯 문제와 Teach-back에 답한 뒤 다음 구간으로 넘어간다. 헷갈린 개념은 [질문 및 답변](../qa.md)의 한 줄 답변과 일정의 복습 큐에만 반영한다.
