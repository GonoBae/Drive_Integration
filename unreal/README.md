# Unreal project

Unreal 프로젝트는 `unreal/DriveIntegration/`에 생성한다.

예정된 주요 모듈은 다음과 같다.

- `SimCoreClient`: C++ host와 binary Protobuf WebSocket 통신
- `ManualInputComponent`: 키보드·게임패드 입력 정규화
- `ExternalVehiclePawn`: C++ 상태 보간과 차량 시각화
- `MapPackageImporter`: 공통 지도·차선·충돌 데이터 로드
- `TrafficDirector`, `SignalController`, `PedestrianDirector`: 규칙 기반 환경 동작
- `SensorRig`: 카메라 등 향후 자율주행 센서

`Binaries`, `Build`, `DerivedDataCache`, `Intermediate`, `Saved` 등 Unreal 생성물은 루트 `.gitignore`에서 제외한다. `.uproject`, `Config`, `Content`, `Plugins`, `Source`는 프로젝트 생성 후 추적한다.
