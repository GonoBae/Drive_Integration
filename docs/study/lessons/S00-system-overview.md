# S00: 저장소와 수동운전 폐루프

## 문서 기준

| 항목 | 값 |
|---|---|
| 최초 작성 | 2026-08-19 |
| 기준 코드 | 2026-08-21 R1 기본 경로·선택형 observer 분리 기준 |
| 재검토 조건 | 실행 프로세스, 포트, 물리 권한 또는 필수 데이터 경로 변경 |

## 목표

세부 구현 전에 저장소에서 실제 실행되는 프로세스와 수동운전 데이터 경로를 설명한다. `main.cpp`는 플랫폼·transport 조립 지도, `SimulationHost`는 고정 주기 물리 실행의 조정자로만 보고 아직 한 줄씩 읽지는 않는다.

## 읽을 자료

- 프로젝트 [README](../../../README.md)
- [시스템 아키텍처](../../03_architecture.md)
- C++ [CMakeLists.txt](../../../cpp/host/CMakeLists.txt)
- C++ [main.cpp](../../../cpp/host/src/main.cpp)는 플랫폼 timer, transport 객체, 세 callback, 시작·종료 조립만 훑기
- C++ [simulation_host.hpp](../../../cpp/host/src/simulation_host.hpp)는 public API, 설정, callback 책임만 훑기
- Unreal [Build.cs](../../../unreal/DriveIntegration/Source/DriveIntegration/DriveIntegration.Build.cs)

## 기준 폐루프

```text
W/S/A/D 또는 gamepad
→ Unreal ExternalVehiclePawn
→ SimCoreClientComponent
→ binary Protobuf ControlCommand
→ WebSocket :9000
→ C++ parse / lease / VehiclePhysics
→ 60Hz WorldState
→ Unreal parse / ExternalVehiclePawn 표시
```

과거 관찰 경로인 `C++ → ZMQ :5555 → Python relay → JSON debug client`는
기본 빌드에서 꺼져 있으며 R1에서 동결했다. `release-zmq-observer`를 명시적으로
선택했을 때만 열리는 보존 경로이고 수동운전 필수 경로가 아니다.

## 직접 작성할 표

| 프로세스 | 입력 | 출력 | 포트 | 종료 시 수동운전 영향 |
|---|---|---|---|---|
| C++ SimCore | 작성 필요 | 작성 필요 | 작성 필요 | 작성 필요 |
| Unreal | 작성 필요 | 작성 필요 | 작성 필요 | 작성 필요 |
| Python relay | 작성 필요 | 작성 필요 | 작성 필요 | 작성 필요 |

## Teach-back

파일을 보지 않고 다음을 3분 안에 설명한다.

> Unreal에서 키를 한 번 누른 뒤 차량이 화면에서 움직이기까지 어떤 프로세스와 데이터가 지나가며, 어느 컴포넌트가 최종 물리 권한을 갖는지 설명해 주세요.

## 완료 기준

- 프로세스·포트·입력·출력 표 완성
- 필수 수동운전 폐루프와 관찰 경로 분리
- 기본 실행이 Python relay와 ZMQ 없이 수동운전을 유지하는 이유 설명
- `main.cpp`와 `SimulationHost`를 지금 정독하지 않는 이유 설명
- 일정표에 완료 여부와 필요한 복습 항목 반영
