# ADR-005: R1 실시간 통신은 WebSocket binary + Protobuf 사용

## 상태

- 결정: 채택
- 결정일: 2026-08-14
- 적용 범위: R1 수동운전의 control/state 통신
- 기본 transport: WebSocket binary
- 기본 serialization: Protobuf
- UDP: 측정 결과가 나쁠 때 재검토
- JSON: runtime control/state 경로에서 사용하지 않음

## 배경

R1 수동운전은 Unreal에서 운전자 입력을 만들고 C++ SimCore가 차량 물리를 계산한 뒤, 그 결과를 다시 Unreal이 표시하는 닫힌 루프다. 이 루프는 60Hz 주기를 목표로 하며, 입력과 상태 메시지는 크기가 작지만 지연, 순서, timestamp, replay 가능성이 중요하다.

현재 저장소의 C++ host는 Unreal 입력을 JSON WebSocket으로 받고, 상태는 Protobuf/ZMQ로 Python relay에 발행한다. 이 구조는 초기 검증에는 편하지만 최종 수동운전 경로로 쓰기에는 두 가지 문제가 있다.

- 입력은 JSON이고 상태는 Protobuf라 통신 규약이 둘로 나뉜다.
- 수동운전 상태가 Python relay를 거치면 지연과 장애 지점이 늘어난다.

사용자는 JSON을 단계적으로 축소하기보다 한 번에 제거하는 방식을 선호한다고 결정했다. 이는 개발 중 혼란을 줄이고, Unreal과 C++가 처음부터 같은 스키마를 공유하게 만든다.

## 결정

R1 수동운전의 실시간 control/state 경로는 `WebSocket binary + Protobuf`로 구현한다.

- Unreal -> C++: `ControlCommand`를 Protobuf binary로 전송한다.
- C++ -> Unreal: `WorldState`를 Protobuf binary로 전송한다.
- 모든 runtime 메시지는 `Envelope`를 통해 schema version, sequence, simulation time, source id, map checksum을 가진다.
- C++ host의 JSON 입력 파서는 제거했다.
- Python relay는 수동운전 필수 경로에서 제외한다.
- Python은 R2 이후 AI/FSD가 `ControlCommand`를 생성하거나 로그·분석·학습 데이터를 처리할 때 붙인다.
- UDP는 바로 도입하지 않고, WebSocket binary의 지연·jitter·drop 대응 측정 결과가 목표를 넘을 때 재검토한다.
- 센서 영상, LiDAR, 대용량 frame payload는 이 채널에 싣지 않는다.

## 왜 WebSocket을 먼저 쓰는가

R1은 로컬 PC 또는 같은 LAN에서 Unreal과 C++를 실행한다. 입력과 상태 메시지는 작고, 목표 주기는 60Hz다. 이 조건에서는 WebSocket 자체가 먼저 병목이 될 가능성이 낮다. 기존 Boost.Beast WebSocket 서버도 이미 존재하므로 구현 비용이 낮다.

WebSocket은 TCP 기반이라 순서 보장과 재전송이 있다. 네트워크가 나쁘면 오래된 패킷 때문에 최신 상태 반영이 밀릴 수 있지만, R1은 인터넷 멀티플레이가 아니라 로컬 포트폴리오 데모다. 따라서 먼저 WebSocket으로 완성하고, 실제 측정값이 나쁠 때 UDP로 바꾸는 것이 일정과 품질의 균형이 좋다.

## UDP 재검토 조건

다음 조건 중 하나가 반복되면 UDP 또는 다른 low-latency transport를 재검토한다.

| 조건 | 기준 |
|---|---|
| command age | 로컬 실행에서 p95 20ms 초과 |
| state age | Unreal render 시점에서 p95 33ms 초과 |
| jitter | 보간 버퍼로도 체감 흔들림이 해결되지 않음 |
| TCP head-of-line blocking | 오래된 state가 최신 state 적용을 막는 현상이 재현됨 |
| 다중 엔티티 확장 | NPC·보행자 상태까지 같은 채널에 실어 state payload가 커짐 |

UDP를 도입하더라도 Protobuf 메시지와 transport interface는 유지한다. 바뀌는 것은 전송 계층이어야 하며, 물리·Unreal 표현·recorder가 메시지 내부 타입에 직접 묶이면 안 된다.

## JSON 사용 금지 범위

다음 경로에서는 JSON을 사용하지 않는다.

- Unreal manual input -> C++ SimCore
- Python autonomy command -> C++ SimCore
- C++ authoritative state -> Unreal IG
- recorder가 저장하는 원본 runtime packet
- replay 입력 packet

JSON은 필요할 경우 별도 debug export, 사람이 읽는 로그, 개발용 상태 덤프에만 사용한다. 이 파일들은 runtime driving protocol로 취급하지 않는다.

## 구현 순서와 상태

1. 완료: `protocol/vehicle.proto`에 `Envelope`, `ControlCommand`, `WorldState` 초안을 추가한다.
2. 완료: C++ WebSocket 서버를 binary frame 송수신으로 바꾼다.
3. 완료: 기존 JSON 입력 파서를 제거한다.
4. 완료: C++가 Unreal 연결에도 state packet을 직접 broadcast한다.
5. 진행 중: Python relay는 수동운전 필수 경로에서 빠지고, 필요 시 Protobuf binary observer로만 둔다.
6. 남음: command timeout, SafeStop, 재연결 handshake, map checksum 검증을 연결한다.
7. 남음: latency·jitter 측정 로그를 남기고 UDP 재검토 조건과 비교한다.

## 구현 기록

2026-08-14에 C++ host 쪽 구현을 시작했다. `protocol/vehicle.proto`에 `Envelope`, `ControlCommand`, `WorldState`를 추가했고, C++ WebSocket 서버는 binary frame으로 `ControlCommand`를 수신하고 매 tick `WorldState`를 연결된 client에 broadcast한다. 기존 C++ JSON 입력 파서는 제거했다.

아직 남은 작업은 Unreal client 생성·연결, command timeout, 재연결 handshake, map checksum 검증, latency·jitter 측정이다.

## 결과

이 결정으로 통신 설명은 단순해진다.

```text
R1 manual driving:
Unreal <-> C++ SimCore
WebSocket binary + Protobuf Envelope

R2 autonomy:
Python AutonomyServer -> C++ SimCore
same ControlCommand schema
```

즉, 수동운전과 자율주행은 같은 제어 메시지를 사용하고, 차이는 명령을 만드는 주체만 바뀐다.
