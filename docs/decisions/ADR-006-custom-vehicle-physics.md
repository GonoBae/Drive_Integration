# ADR-006: 현재 C++ 서버에 자체 차량 물리엔진 구현

## 상태

- 결정: 채택
- 결정일: 2026-08-14
- 적용 범위: R1 수동운전 및 이후 R2/R3 자율주행 환경
- 런타임 물리 SDK: 사용하지 않음
- 비교 기준: Project Chrono, PhysX Vehicle2, Unreal Chaos Vehicle

## 배경

C++ SimCore는 Unreal과 별도로 차량 상태를 계산하는 현재 프로젝트의 물리엔진이다. 처음에는 완성 차량 모델과 충돌계를 빠르게 확보하기 위해 Chrono::Vehicle과 PhysX Vehicle2를 비교했다. Chrono 10.0.0은 macOS ARM64에서 차량 1대·4대, 정적 벽 충돌, 반복성 시험을 통과했다.

프로젝트의 주목적은 상용 정확도 인증이 아니라 차량 동역학, 실시간 서버, Unreal 연동, 이후 자율주행 구조를 직접 설계하고 설명할 수 있는 포트폴리오와 학습 결과를 만드는 것이다. 사용자는 완성 SDK를 제품 물리로 채택하기보다 현재 C++ 서버를 자체 물리엔진으로 발전시키기로 결정했다.

## 결정

R1 차량 물리는 기존 `VehiclePhysics`를 기반으로 직접 구현한다.

- C++ SimCore가 차량 pose, 속도, 가속도와 이후 충돌 결과의 최종 권한을 가진다.
- Chrono, PhysX, Chaos는 런타임 의존성이 아니라 수식·동작·성능을 비교하는 참고 기준으로만 사용한다.
- 차량 모델은 작은 단계로 확장하고 각 단계마다 자동 회귀 시험을 추가한다.
- Unreal은 C++ 상태를 표시하며 Ego에 별도의 Chaos 차량 동역학을 적용하지 않는다.
- Python은 R2 이후 동일한 `ControlCommand`를 생성하며 차량 물리를 계산하지 않는다.
- 외부 라이브러리는 네트워크·직렬화·수학 같은 기반 기능에 사용할 수 있지만 차량 힘, 조향, 타이어, 서스펜션 모델은 프로젝트 코드로 구현한다.

## R1 물리 범위

단계별 구현 순서는 다음과 같다.

1. 종방향 힘, 제동, 기어, 공기저항, 구름저항
2. 휠베이스 기반 kinematic bicycle 조향
3. 강체의 3D 위치·자세·선속도·각속도
4. 바퀴별 접촉, 종·횡방향 타이어 힘
5. 스프링·댐퍼 서스펜션과 하중 이동
6. 노면 마찰과 경사
7. 정적·동적 충돌 및 관통 회귀 시험

R1은 특정 실차와 수치적으로 일치한다고 주장하지 않는다. 실차 정확도를 선언하려면 대상 차량의 질량 분포, 타이어, 서스펜션, 동력계 파라미터와 계측 데이터가 추가로 필요하다.

## D1 구현 증거

현재 `VehiclePhysics`에 다음 1단계 모델을 구현했다.

- `F = ma` 기반 질량·구동력·제동력
- `0.5 × rho × Cd × A × v²` 공기저항
- `Crr × m × g` 구름저항
- Drive, Neutral, Reverse와 브레이크 무후진 처리
- `yaw_rate = speed / wheelbase × tan(steering_angle)` bicycle 조향
- Local ENU 위치 적분과 WGS84 출력 변환
- 입력 clamp와 비정상 수치 방어
- 조정 가능한 `VehicleParameters`

자동 시험은 정지 안정성, 입력 clamp, 가속, 제동, 후진, 우회전을 검증한다. macOS ARM64 Release 빌드와 CTest 10회 반복이 통과했다.

## 후보 비교와 판단

| 후보 | 장점 | 이 프로젝트에서 채택하지 않은 이유 |
|---|---|---|
| 자체 C++ 물리 | 모든 수식·상태·오차를 설명하고 수정 가능, 현재 서버와 직접 결합 | 개발량과 검증 책임이 가장 큼 |
| Chrono::Vehicle 10 | 완성 차량·타이어·서스펜션·terrain, 개발기 실행 증거 확보 | 핵심 학습 범위를 SDK 내부에 맡기며 빌드·배포 비용이 큼 |
| PhysX Vehicle2 | 비교적 가벼운 차량·충돌 기반 | macOS 개발기에서 현재 vcpkg 포트 실행 증거를 확보하지 못했고 구성 요소 학습이 SDK API 중심이 됨 |
| Unreal Chaos Vehicle | Unreal 통합이 빠름 | 외부 C++ 단일 물리 권한과 headless 서버 목표에 맞지 않음 |

Chrono 스파이크는 선택 실패물이 아니라 비교 검증 자료로 유지한다. 개발기 결과는 [실행 기록](../../cpp/host/spikes/chrono_vehicle/results/2026-08-14-macos-arm64.md)에 보관한다.

## 비용과 대응

| 비용·위험 | 대응 |
|---|---|
| 타이어·서스펜션 구현량 증가 | 단순 모델부터 인터페이스와 시험을 고정하고 단계적으로 교체 |
| 물리적으로 그럴듯하지만 실제 차량과 다를 가능성 | 단위·수식·파라미터 출처 기록, 기준 시나리오와 비교 그래프 작성 |
| 충돌 구현 지연 | R1에서 파손을 제외하고 convex/OBB·정적 mesh 접촉부터 제한 구현 |
| 수치 불안정 | 고정 tick, 제한된 substep, NaN·에너지·관통 회귀 시험 추가 |
| 1인 일정 위험 | 각 단계의 종료 조건을 지키고 고급 모델을 기본 모델과 교체 가능한 구조로 구현 |
| 플랫폼 차이 | macOS와 Windows Release 빌드·동일 입력 허용 오차 시험 수행 |

## 완료 게이트

자체 물리엔진의 R1 완료는 다음을 모두 만족해야 한다.

1. macOS ARM64와 Windows x64에서 CMake Release 빌드 성공
2. 렌더링과 분리된 고정 tick 및 30분 실행 중 backlog·NaN·crash 없음
3. 직진·가속·제동·후진·회전·경사 시험 통과
4. 타이어·서스펜션·노면 접촉 상태를 telemetry로 확인 가능
5. 정적 커브·벽과 동적 proxy에 지속 관통 없음
6. 같은 빌드·설정·입력 replay가 NFR-007 허용 오차 이내
7. Unreal이 C++ pose를 임의의 차량 물리로 다시 계산하지 않음

## 재검토 조건

다음 조건이 발생하면 외부 SDK 사용을 다시 검토할 수 있다.

- R1 이후 실차 계측 데이터에 대한 정량 인증이 필요함
- 자체 충돌·타이어 모델이 목표 성능이나 안정성 기준을 반복해서 충족하지 못함
- HIL·ECU 검증 등 상용 도구 호환성이 프로젝트 목표에 추가됨

재검토하더라도 transport, protocol, recorder가 특정 SDK 타입에 의존하지 않도록 차량 동역학 경계를 유지한다.

## 참고 자료

- [Project Chrono 10.0.0](https://github.com/projectchrono/chrono/tree/10.0.0)
- [Chrono::Vehicle 공식 문서](https://api.projectchrono.org/manual_vehicle.html)
- [NVIDIA PhysX Vehicle2 공식 문서](https://nvidia-omniverse.github.io/PhysX/physx/5.1.0/docs/Vehicles.html)
