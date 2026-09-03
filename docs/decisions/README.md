# Architecture Decision Records

| ADR | 결정 |
|---|---|
| [ADR-005](./ADR-005-realtime-transport-protocol.md) | R1 실시간 통신은 localhost-only WebSocket binary + Protobuf와 양방향 Hello readiness/identity gate 사용 |
| [ADR-006](./ADR-006-custom-vehicle-physics.md) | C++ host에 자체 차량 물리를 단계적으로 구현 |
| [ADR-010](./ADR-010-low-latency-control-presentation.md) | Windows 60Hz 상태 전달, UE command FIFO 방지와 제한된 상태 예측 |
| [ADR-011](./ADR-011-canonical-coordinate-frames.md) | 공통 좌표계는 ROS 호환 FLU, Unreal FRU 변환은 경계 adapter; C++/UE quaternion 자동 계약 적용 |
| [ADR-012](./ADR-012-unreal-ground-measurement-boundary.md) | Unreal은 지면 collision·material/friction을 측정하고 SimCore는 immutable snapshot으로 차량 물리만 계산 |
| [ADR-013](./ADR-013-small-virtual-city-course.md) | R1 배경을 작은 가상 도심으로 전환; 실제 지역/Cesium 의존 제외, 로컬 차선·충돌·traffic·기록·인수 기준 유지 |

새로운 기술 결정은 기존 번호를 변경하지 않고 다음 번호로 추가한다. 결정이 대체되면 원본을 삭제하지 않고 상태와 대체 ADR 링크를 기록한다.

- [ADR-014](./ADR-014-server-clock-traffic-signals.md): 차량 신호 위상은 C++ simulation clock이 소유하고 Unreal은 검증된 상태를 표시한다.
