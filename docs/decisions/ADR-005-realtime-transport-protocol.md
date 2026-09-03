# ADR-005: R1 실시간 통신은 WebSocket binary + Protobuf 사용

## 상태

- 결정: 채택
- 결정일: 2026-08-14
- 적용 범위: R1 수동운전의 control/state 통신
- 기본 transport: WebSocket binary
- 기본 listener: `127.0.0.1` 전용
- 기본 serialization: Protobuf
- UDP: 측정 결과가 나쁠 때 재검토
- JSON: runtime control/state 경로에서 사용하지 않음

## 배경

R1 수동운전은 Unreal에서 운전자 입력을 만들고 C++ SimCore가 차량 물리를 계산한 뒤, 그 결과를 다시 Unreal이 표시하는 닫힌 루프다. 물리와 상태는 60Hz를 목표로 하고, 입력 변화는 latest-wins로 합쳐 최대 30Hz, 동일 입력 lease는 20Hz heartbeat로 유지한다. 메시지는 작지만 지연, 순서, timestamp, replay 가능성이 중요하다.

결정 당시(2026-08-14) 저장소의 C++ host는 Unreal 입력을 JSON WebSocket으로 받고, 상태는 Protobuf/ZMQ로 Python relay에 발행했다. 이 구조는 초기 검증에는 편하지만 최종 수동운전 경로로 쓰기에는 두 가지 문제가 있었다.

- 입력은 JSON이고 상태는 Protobuf라 통신 규약이 둘로 나뉜다.
- 수동운전 상태가 Python relay를 거치면 지연과 장애 지점이 늘어난다.

사용자는 JSON을 단계적으로 축소하기보다 한 번에 제거하는 방식을 선호한다고 결정했다. 이는 개발 중 혼란을 줄이고, Unreal과 C++가 처음부터 같은 스키마를 공유하게 만든다.

## 결정

R1 수동운전의 실시간 control/state 경로는 `WebSocket binary + Protobuf`로 구현한다.

- Unreal -> C++: `ControlCommand`를 Protobuf binary로 전송한다.
- C++ -> Unreal: `WorldState`를 Protobuf binary로 전송한다.
- 모든 runtime 메시지는 `Envelope`를 통해 schema version, sequence, simulation time, source id, session id, map checksum을 가진다.
- C++ host의 JSON 입력 파서는 제거했다.
- 연결 직후 양쪽이 `Hello`로 build/source/schema/MapPackage checksum/capability를 교환한다.
  initial server Hello만 readiness 예외이며, 첫 client Hello 승인 전에는 WorldState를
  broadcast하지 않는다.
- negotiated Hello fingerprint는 build/schema/MapPackage checksum과 정렬한 capability
  set으로 만든다. 승인된 connection generation에는 client source/session/highest sequence와
  마지막 message kind·semantic payload fingerprint를 고정한다.
- 같은 sequence는 message kind와 sequence를 제외한 semantic payload가 정확히 같은 재전송일
  때만 idempotent하게 허용한다. altered payload reuse와 identity/order mismatch는 거부하며,
  거부된 높은 sequence는 high-water를 오염시키지 않는다.
- invalid 첫 application frame은 1008 policy close하고, 승인 뒤 ordinary payload 거부는
  socket을 유지한다. server-initiated close가 peer reply나 정체 write로 끝나지 않으면
  250ms grace deadline 뒤 TCP shutdown을 강제한다.
- Python relay는 수동운전 필수 경로에서 제외한다.
- Python은 R2 이후 AI/FSD가 `ControlCommand`를 생성하거나 로그·분석·학습 데이터를 처리할 때 붙인다.
- UDP는 바로 도입하지 않고, WebSocket binary의 지연·jitter·drop 대응 측정 결과가 목표를 넘을 때 재검토한다.
- 센서 영상, LiDAR, 대용량 frame payload는 이 채널에 싣지 않는다.

## 왜 WebSocket을 먼저 쓰는가

R1은 한 로컬 PC에서 Unreal과 C++를 실행한다. 입력과 상태 메시지는 작고, 물리·상태 목표 주기는 60Hz다. 이 조건에서는 WebSocket 자체가 먼저 병목이 될 가능성이 낮다. 기존 Boost.Beast WebSocket 서버도 이미 존재하므로 구현 비용이 낮다. 단, heartbeat 생산률은 client transport service 소비율을 넘지 않아야 한다. LAN 노출은 인증·명시적 bind 설정을 함께 설계할 때 별도 기능으로 추가하며, 무인증 control socket을 기본 공개하지 않는다.

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
5. 완료: Python relay는 수동운전 필수 경로에서 빠지고 Protobuf/ZMQ observer로만 둔다.
6. 완료: 250ms command timeout, SafeStop, source/session/sequence/queue-age 소유권과 map checksum 필드 검증을 연결한다.
7. 완료: Unreal 5.6 client가 입력과 WorldState·wheel state를 직접 송수신한다.
8. 완료: loopback latency·jitter를 측정하고 Windows timer와 Unreal 표시 경로를 개선한다.
9. 완료: 실제 MapPackage checksum·양방향 Hello handshake, canonical negotiated fingerprint,
   application readiness와 connection identity/order·semantic retransmission gate,
   250ms forced-close deadline, packet age·forward missing·old HUD를 구현한다.
10. 남음: packaged render 시점 state age와 실제 PIE 재연결·SafeStop 표시를 검증한다.

## 구현 기록

2026-08-14에 C++ host 쪽 구현을 시작했다. `protocol/vehicle.proto`에 `Envelope`, `ControlCommand`, `WorldState`를 추가했고, C++ WebSocket 서버는 binary frame으로 `ControlCommand`를 수신하고 매 tick `WorldState`를 연결된 client에 broadcast한다. 기존 C++ JSON 입력 파서는 제거했다.

2026-08-19에는 Unreal client 직접 연결, 250ms SafeStop, source/session/sequence 검증과 4개 wheel state를 연결했다. WebSocket accept 전에 state frame이 전송되던 경쟁 상태를 수정하고 회귀 시험을 추가했다. Windows loopback state 간격은 개선 전 p95 30.05ms·최대 49.82ms에서 p95 17.08ms·최대 17.20ms로 안정됐다. 실제 PIE에서는 UE 5.6의 60Hz heartbeat 생산과 약 30Hz socket service 소비 불균형으로 command FIFO 지연이 누적됐으며, event-loop service와 20Hz lease heartbeat로 해결했다. 종료 재검토에서는 입력을 deadzone/epsilon 처리 후 최대 30Hz latest-wins로 합치고, 100ms를 넘게 대기한 것으로 추정되는 패킷을 폐기한다. timeout session과 소켓을 함께 닫고 Unreal이 새 session으로 자동 재연결하게 하여 기존 큐가 SafeStop을 해제하지 못하게 했다. 이 수치와 결과에서는 UDP 전환 근거가 없으므로 WebSocket binary를 유지한다. 저지연 표시 결정과 측정 조건은 [ADR-010](./ADR-010-low-latency-control-presentation.md)에 기록한다.

2026-08-28에는 실제 collision checksum을 양쪽이 독립 검증하고 양방향 `Hello`에서 다시
교차 확인하도록 완성했다. transport는 client Hello 승인 전 WorldState를 차단하고 첫
application frame 거부를 1008 close한다. host는 capability 순서를 정규화한
build/schema/map checksum/sorted-capability fingerprint와 generation별 source/session/highest
sequence를 고정한다. 같은 sequence의 exact semantic retransmission만 허용하고 altered
payload reuse는 거부하며, 거부된 높은 sequence는 high-water를 오염시키지 않는다. 1008
close가 peer handshake 또는 write 정체로 완료되지 않으면 250ms 뒤 TCP를 강제 종료한다.
로그에 노출되는 identity는 printable ASCII만 허용하고 기본 bind는 `127.0.0.1`로 제한했다.
UE HUD는 forward sequence `missing`과 duplicate/out-of-order `old`를 분리한다. 전체 CTest
14/14와 20회 반복 280/280(48.35s)이 통과했다. 실제 WebSocket smoke에서는 invalid Hello
1008, pre-Hello WorldState 차단, 같은 sequence의 altered payload 2회 거부 후 valid recovery
WorldState sequence 5148(checksum `fnv1a64:b8b0a6ccd89614de`, entity 1개)를 확인했다.

2026-08-31에는 authoritative `Health`를 `WorldState.health`(field 2)에 추가했다.
별도 `Envelope.health` frame을 번갈아 전송하면 기존 latest-wins 단일 pending queue에서
차량 상태와 Health가 서로 대체되고, WorldState의 sequence 누락 통계에도 영향을 준다.
따라서 같은 tick의 차량 상태와 Health를 하나의 snapshot으로 보낸다. 기존
`Envelope.health` 정의는 보존하지만 현재 live 경로에서는 발행하지 않는다.

- schema version은 2를 유지하는 additive 변경이며 server Hello는 선택 capability
  `world-health.v1`을 알린다. 기존 v2 client는 새 필드를 건너뛸 수 있다.
- 상태는 `awaiting_reset`, `awaiting_control`, `active`, `safe_stop`,
  `reconnect_required`, `estop_latched`다. 서버가 실제로 적용한 lease/lifecycle/EStop
  상태를 사용하며 EStop이 최우선이다.
- `tick_overrun_count`, `last_command_age_ns`, `message`에
  `has_control_command`를 추가한다. command가 아직 없으면 age=0/has=false이며,
  age=0을 최근 정상 입력으로 오인하지 않는다.
- Unreal은 승인된 checksum·PlaySession·sequence의 snapshot만 표시한다. Health 누락,
  알 수 없는 상태, stale snapshot, 연결/handshake 실패는 정상 Active로 표시하지 않는다.
- 전역 EStop만 진단 예외다. 이전 PIE에서 이미 latch돼 새 Reset이 거부된 경우 현재
  generation·검증 Hello·map·증가 sequence로 확인한 `estop_latched` reason은 play ID가
  달라도 읽기 전용 캐시에 표시한다. 연결 변경 시 cache를 비우고 같은 100ms freshness를
  적용한다. cross-play pose나 Active 수용은 계속 금지한다.
- Health는 진단용이다. `safe_stop` 표시 때문에 fresh control 송신을 막지 않는다.
  hard timeout은 socket을 닫으므로 해당 controller가 `reconnect_required` frame을 반드시
  수신하는 계약은 아니다. 이때 Unreal은 연결 상태/Unknown을 표시하고 재연결한다.

검증 기록과 남은 사용자 gate는 [8/31 작업일지](../worklogs/2026-08-31.md)를 따른다.
남은 작업은 packaged Unreal end-to-end state age, 실제 PIE 재연결 full snapshot과
Health HUD 시각 확인이다. transport 자동 재연결, stale socket generation과
application identity/order 방어는 구현됐다.

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
