# 학습자료

이 문서는 단계별 강의자료의 입구다. 먼저 [일정의 현재 단계](../schedule.md#한눈에-보기)를 확인한 뒤 해당 자료 하나만 연다. Quiz 정답과 해설은 [질문 및 답변](../qa.md#quiz-정답-및-해설)에서 접어서 제공한다.

## 지금 볼 자료

| 현재 범위 | 바로 열기 | 이번 범위에서 확인할 것 |
|---|---|---|
| S01-P3 진행 중 | [공통 Protobuf 계약 — P3](./S01-protocol-contract.md#6-p3-시작-hello-health-oneof) | `Hello`, `Health`, `Envelope.oneof`, 현재 구현 여부 |
| S00·S01-P1/P2 복습 | [S00 전체 흐름](./S00-system-overview.md) · [S01 P1/P2](./S01-protocol-contract.md) | 폐루프, field number, runtime value, `repeated` |

## 전체 자료 목록

| 단계 | 수업 자료 | 대응 코드·시험 |
|---|---|---|
| S00 | [저장소와 수동운전 폐루프](./S00-system-overview.md) | README, CMake, `main.cpp`·`SimulationHost` 조립 구조 |
| S01 | [공통 Protobuf 계약](./S01-protocol-contract.md) | `protocol/vehicle.proto`, `vehicle_protocol_test.cpp` |
| S02 | 작성 예정 | `config.hpp`, `simulation_clock.hpp/test` |
| S03 | 작성 예정 | `control_lease.hpp/test` |
| S04 | 작성 예정 | `vehicle_physics.hpp`, 물리 시험 구조 |
| S05 | 작성 예정 | `vehicle_physics.cpp` 종방향, 물리 시험 |
| S06 | 작성 예정 | 횡방향·휠·FLU/FRU, ADR-011 |
| S07 | 작성 예정 | `vehicle_messages.hpp/.cpp`, 프로토콜 시험 |
| S08 | 작성 예정 | `ws_server.hpp/.cpp`, ZMQ, WebSocket 시험 |
| S09 | 작성 예정 | `simulation_host.hpp/.cpp`, `main.cpp` runtime 조립 |
| S10 | 작성 예정 | Unreal `SimCoreProtocol`, `SimCoreClientComponent` |
| S11 | 작성 예정 | Unreal `ExternalVehiclePawn`, 입력 설정 |
| S12 | 작성 예정 | Python relay, autonomy 경계 |

수업 자료를 미리 대량 생성하지 않는다. 직전 단계의 실제 오개념과 질문을 다음 수업 자료에 반영한다.

## 학습 방식

각 단계는 `예측 → 요구사항과 시험 확인 → 계약 읽기 → 정상·실패 흐름 추적 → Teach-back → Quiz → 작은 변경 → Review` 순서로 진행한다. 정답을 외우는 것보다 다음 상태와 실패 증상을 자신의 말로 설명할 수 있는지를 확인한다.

## 공통 통과 기준

- 파일 책임을 한 문장으로 설명한다.
- 입력·상태·출력·불변식을 구분한다.
- 정상 흐름과 실패 흐름을 하나씩 추적한다.
- 노트 없이 3분 Teach-back을 한다.
- Quiz 80% 이상을 달성한다.
- 관련 시험이 방지하는 장애를 설명한다.
- 변경형 단계는 실패 시험→최소 수정→전체 회귀를 남긴다.
- 위 기준을 충족하면 [일정](../schedule.md)의 단계 상태를 완료로 갱신한다.
