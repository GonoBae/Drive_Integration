# Unreal project

Unreal Engine 5.6 C++ 프로젝트는 `unreal/DriveIntegration/`에 있다.

현재 구현:

- `SimCoreClientComponent`: binary Protobuf WebSocket 연결·재연결과 명령/상태 경계
- `SimCoreProtocol`: schema-v2 공통 Proto와 호환되는 Unreal wire adapter
- `ExternalVehiclePawn`: right-positive 입력축을 canonical left-positive steering으로 한 번 변환하고 외부 차량을 시각화
- `SimCoreCoordinateFrames`, `SimCorePresentation`: FLU yaw·steering 경계 변환과 제한 외삽·wheel 표시

2026-08-21 schema-v2 변경은 Mac에서 소스 정합성만 확인했다. Windows Unreal
Engine 5.6의 Game/Editor 빌드와 PIE 좌·우 조향 검증은 후속 필수 게이트다.

향후 구현·분리:

- `ManualInputComponent`: Pawn에서 키보드·게임패드 입력 정규화 분리
- `MapPackageImporter`: 공통 지도·차선·충돌 데이터 로드
- `TrafficDirector`, `SignalController`, `PedestrianDirector`: 규칙 기반 환경 동작
- `SensorRig`: 카메라 등 향후 자율주행 센서

`Binaries`, `Build`, `DerivedDataCache`, `Intermediate`, `Saved` 등 Unreal 생성물은 루트 `.gitignore`에서 제외한다. `.uproject`, `Config`, `Content`, `Plugins`, `Source`만 추적한다.
