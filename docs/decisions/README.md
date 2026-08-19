# Architecture Decision Records

| ADR | 결정 |
|---|---|
| [ADR-005](./ADR-005-realtime-transport-protocol.md) | R1 실시간 통신은 WebSocket binary + Protobuf 사용 |
| [ADR-006](./ADR-006-custom-vehicle-physics.md) | C++ host에 자체 차량 물리를 단계적으로 구현 |
| [ADR-010](./ADR-010-low-latency-control-presentation.md) | Windows 60Hz 상태 전달, UE command FIFO 방지와 제한된 상태 예측 |

새로운 기술 결정은 기존 번호를 변경하지 않고 다음 번호로 추가한다. 결정이 대체되면 원본을 삭제하지 않고 상태와 대체 ADR 링크를 기록한다.
