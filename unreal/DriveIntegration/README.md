# DriveIntegration Unreal project

Unreal Engine 5.6.1 C++ 프로젝트다. C++ SimCore가 차량 물리의 권한을 가지며 Unreal은 수동 입력과 외부 상태 표시를 담당한다.

## 현재 최소 통합

- `SimCoreClientComponent`: `ws://127.0.0.1:9000` binary WebSocket 연결
- `SimCoreProtocol`: 루트 `protocol/vehicle.proto`의 ControlCommand/WorldState 호환 wire adapter
- `ExternalVehiclePawn`: W/S/A/D·Space 입력 전송과 ENU 상태 표시
- `DriveIntegrationGameModeBase`: 외부 차량 Pawn을 기본 Pawn으로 사용

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
