# DriveIntegration Unreal project

Unreal Engine 5.6.1 C++ 프로젝트다. C++ SimCore가 차량 물리의 권한을 가지며 Unreal은 수동 입력과 외부 상태 표시를 담당한다.

## 현재 최소 통합

- `SimCoreClientComponent`: binary WebSocket 연결 상태·socket generation·자동 재연결 관리
- `SimCoreProtocol`: 루트 `protocol/vehicle.proto`의 schema-v2 ControlCommand/WorldState 호환 wire adapter
- `ExternalVehiclePawn`: W/S/A/D·Space 입력 전송과 ENU 상태 표시
- `SimCoreCoordinateFrames`: ENU/FLU에서 Unreal 표시 좌표로 변환하고 left-positive steering/body yaw를 Unreal 경계에서 한 번 반전
- `SimCorePresentation`: 최대 50ms 제한 외삽과 stale wheel 정지 정책
- `DriveIntegrationGameModeBase`: 외부 차량 Pawn을 기본 Pawn으로 사용

## 로컬 저지연 동작

- 동일 frame의 입력 변화는 latest-wins로 합치고 deadzone/epsilon 적용 후 최대 30Hz로 `ControlCommand`를 전송한다.
- 입력을 유지하는 동안에는 20Hz heartbeat로 250ms command lease를 갱신한다.
- 연결마다 고유 session ID를 사용하며 C++는 sequence와 추정 queue age를 검증한다.
- 250ms timeout으로 C++가 기존 socket을 닫으면 기본 0.5초 후 새 session으로 자동 재연결한다.
- UE 5.6 WebSocket은 Windows event-loop service로 실행해 새 입력이 socket thread를 즉시 깨우며, 반복 명령 FIFO가 누적되지 않게 한다.
- Editor PIE의 background CPU throttling을 꺼 focus 변화로 heartbeat가 끊기지 않게 한다.
- 수신한 body velocity로 최대 50ms만 pose를 예측하고 그 이후에는 위치를 고정한다.
- C++ host는 Windows에서 1ms timer resolution을 요청해 60Hz state 간격의 jitter를 줄인다.
- `ControlledEntityId`로 Ego state를 선택하므로 WorldState에 NPC가 먼저 포함돼도 다른 엔티티를 표시하지 않는다.
- schema v1 WorldState를 받으면 incompatible 상태로 전환하고 자동 재연결을 멈춘다. `Hello` application handshake는 아직 구현 전이다.
- 2026-08-19 Python loopback probe에서 state 간격은 p95 17.08ms, 최대 17.20ms였고 command 전송 후 첫 speed 변화는 약 27ms였다.

schema-v2 좌표 경계 변경은 Mac에서 정적 검토만 완료했다. Windows UE 5.6
Game/Editor 빌드와 PIE에서 A/D, wheel yaw, heading 예측을 다시 확인해야 한다.

측정과 설계 근거는 [ADR-010](../../docs/decisions/ADR-010-low-latency-control-presentation.md), [D6 작업 로그](../../docs/worklogs/2026-08-19.md), [UE 5.6 WebSocket 입력 지연 누적 해결 사례](../../docs/troubleshooting/ue56-websocket-growing-input-delay.md)에 기록한다.

## 에디터 설정

기존 `SimBlank` 템플릿 맵은 자체 `BP_SimGameMode`를 World Settings에서 지정할 수 있다. 이 경우 다음 중 하나를 선택해야 C++ 외부 차량 Pawn이 생성된다.

1. World Settings의 GameMode Override를 `DriveIntegrationGameModeBase`로 변경한다.
2. 기존 `BP_SimGameMode`의 Default Pawn Class를 `ExternalVehiclePawn`으로 변경한다.

실행 전에 C++ host를 먼저 시작한다. 기본 주소는 `ws://127.0.0.1:9000`이다.

## 조작

- `W`: throttle
- `S`: brake
- `A` / `D`: steering
- `Space`: handbrake
