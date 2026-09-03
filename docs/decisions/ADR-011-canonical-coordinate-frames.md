# ADR-011: 공통 좌표계는 ROS 호환 FLU를 사용하고 Unreal 변환은 경계에서 수행한다

## 상태

- 결정: 채택
- 결정일: 2026-08-19
- 구현 상태: schema v2 scalar/vector 부호, C++ `GeoTransformAdapter`, Unreal polar/axial·quaternion 경계와 양방향 `Hello` gate 적용. UE 5.6 headless GeoTransform Automation 1/1 `Success`; 실제 PIE 좌표 시각·로컬 원점 metadata·센서 frame 검증은 후속; Cesium geodetic 통합은 ADR-013에 따라 Future
- 적용 범위: 현재 C++ SimCore·공통 Protobuf·Unreal, 향후 Python AutonomyServer·MapPackage·SensorRig·Recorder

## 배경

Unreal의 로컬 차량 축은 일반적으로 `X=forward, Y=right, Z=up`인 left-handed 좌표계를 사용한다. 반면 ROS REP-103의 차량 body frame은 `X=forward, Y=left, Z=up`인 right-handed 좌표계다. 프로젝트의 공통 규약을 Unreal에 맞추면 Unreal 표시는 단순해지지만, 향후 Python 자율주행·센서·로봇 도구와 연결할 때 모든 외부 경계에서 다시 변환해야 한다.

초기 C++ solver는 내부적으로 `Y=right`를 사용했고 공개 scalar에도 우회전 양수 규약이 남아 있었다. schema v2부터 solver 규약은 구현 세부사항으로 격리하고, 공개 `yaw_rate`, `steering_angle`, `ControlCommand.steering`을 아래 canonical 부호로 통일한다. 같은 회전이 필드마다 다른 부호가 되지 않도록 모든 변환은 명명된 경계에서만 수행한다.

## 검토한 선택지

| 선택지 | 장점 | 단점 | 결론 |
|---|---|---|---|
| Unreal 기준 FRU를 전체 공통 규약으로 사용 | Unreal Actor 적용이 단순함 | left-handed이며 ROS·센서·자율주행 도구 연결마다 변환 필요 | 채택하지 않음 |
| ROS 호환 FLU를 전체 공통 규약으로 사용 | right-handed 수학, ROS REP-103 및 향후 자율주행 확장과 일치 | Unreal과 현재 solver 경계에 명시적 변환 필요 | **채택** |
| 컴포넌트별 좌표계를 그대로 노출 | 초기 수정량이 적음 | 필드마다 부호가 달라지고 재사용·검증이 어려움 | 채택하지 않음 |

## 결정

### 1. 공개 좌표 frame

| frame ID | handedness·축 | 단위 | 용도 |
|---|---|---|---|
| `map_enu` | right-handed, X=East, Y=North, Z=Up | m | C++ 월드 위치·속도·충돌, MapPackage |
| `base_link` | right-handed, X=forward, Y=left, Z=up(FLU) | m | 차량 body 상태, 휠·센서 장착 기준, 향후 Python 입력 |
| `unreal_actor` | left-handed, X=forward, Y=right, Z=up(FRU) | cm | Unreal 표시 전용 |
| `<sensor>_optical` | right-handed, X=right, Y=down, Z=forward | m | 향후 카메라·광학 센서 데이터 |

- C++ solver 내부 FRU는 구현 세부사항이며 모듈·프로세스 경계를 넘기지 않는다.
- Unreal 좌표는 공통 Protobuf와 기록 파일에 저장하지 않는다. Unreal adapter가 표시 직전에 변환한다.
- 모든 frame은 메시지 또는 설정의 `frame_id`로 식별하고, 암묵적으로 추정하지 않는다.

### 2. canonical 부호

| 물리량 | 양의 방향 |
|---|---|
| body linear X/Y/Z | forward / left / up |
| body roll rate | FLU X축 right-hand rule |
| body pitch rate | FLU Y축 right-hand rule |
| body yaw rate | FLU Z축 right-hand rule, **좌회전 양수** |
| road-wheel steering angle | **좌조향 양수** |
| normalized steering command | **좌조향 양수** |
| wheel slip angle·lateral force | wheel-local left 양수 |

항법 `heading`은 body 회전 벡터가 아닌 별도 scalar다. 기존 사용자 표기와 지도 규약을 위해 `0°=North`, 시계 방향 양수를 유지한다. `yaw_rate`는 true FLU angular velocity의 body-Z 성분이다. 따라서 수평 자세의 평면 운동에서는 다음 관계를 적용하지만, compound pitch·roll에서는 둘을 같은 scalar로 취급하지 않는다.

```text
heading_rate_clockwise = -body_yaw_rate_left_positive
enu_yaw_ccw_from_east = pi/2 - heading_clockwise_from_north
```

compound 자세에서 canonical Euler yaw rate는 true body vector에서 다음처럼 복원한다.

```text
canonical_euler_yaw_rate =
    (omega_body_y * sin(roll) + omega_body_z * cos(roll)) / cos(pitch)
heading_rate_clockwise = -canonical_euler_yaw_rate
yaw_rate = omega_body_z
```

현재 drivable ground 계약은 Euler singularity를 피하도록 `|pitch| <= 60°` 범위다.

물리 계산의 각도와 각속도는 radian을 사용하고, 기존 `EntityState.heading/pitch/roll` 표시 필드만 degree를 사용한다.

### 3. Unreal 경계 변환

현재 prototype Unreal world는 `X=North, Y=East, Z=Up`이고 map origin에 해당하는
presentation offset `o_ue_cm`를 가진다. `map_enu` polar vector를 Unreal world에
표현하는 basis는 다음과 같이 하나로 고정한다.

```text
A = [0 1 0; 1 0 0; 0 0 1], det(A) = -1
position_ue_cm = 100 A position_enu_m + o_ue_cm
velocity_ue_cmps = 100 A velocity_enu_mps
acceleration_ue_cmps2 = 100 A acceleration_enu_mps2
omega_ue_radps = det(A) A omega_enu_radps = -A omega_enu_radps
```

위치는 offset을 포함하지만 displacement·direction·속도·가속도는 포함하지 않는다.
각속도는 axial vector이므로 reflection인 `A`를 polar vector처럼 적용하지 않는다.
역변환은 `A^-1=A`와 동일한 단위 역변환을 사용한다.

`base_link` FLU에서 Unreal Actor FRU 로컬 좌표로 가는 polar basis는
`B=diag(1,-1,1)`, `det(B)=-1`이다. 따라서 local polar vector는
`(x,y,z)_actor=(x,-y,z)_flu`, axial angular velocity는
`(wx,wy,wz)_actor=(-wx,wy,-wz)_flu`다.

schema-v2 Euler 자세를 proper RH rotation으로 나타낼 때 quaternion은 scalar-last
`(x,y,z,w)`인 `R_map_enu_from_base_link`이며 다음 식으로 만든다.

```text
enu_yaw_ccw = pi/2 - heading_clockwise
R_map_enu_from_base_link = Rz(enu_yaw_ccw) Ry(-pitch_nose_up) Rx(roll_left_up)
R_unreal_world_from_actor = A R_map_enu_from_base_link B^-1
```

두 reflection이 상쇄되므로 마지막 행렬은 proper rotation이다. quaternion 성분 부호를
개별 추측하지 않고 행렬 basis change 뒤 quaternion을 생성하며 `q`와 `-q`는 같은
자세로 비교한다. wire payload는 계속 기존 heading/pitch/roll과 vector 필드를 사용하고
quaternion을 새 필드로 추가하지 않는다.

현 단계의 `GeoTransformAdapter`는 prototype local map origin과 위 basis/단위를
전담한다. 이후 Cesium Georeference가 들어와도 geodetic origin 계산만 adapter 내부에
추가하고 polar/axial·quaternion 계약은 바꾸지 않는다. 코드 곳곳에서 축 교환, Y 부호,
meter↔centimeter를 개별 보정하지 않는다.

## 현재 구현과 남은 이행 작업

2026-08-31 [ADR-013](./ADR-013-small-virtual-city-course.md)에 따라 R1은 작은 가상
도심으로 전환됐다. 이 ADR의 FLU·ENU·polar/axial·quaternion 계약은 유지하지만
Cesium/geodetic 통합은 Future로 분리한다. R1 잔여 좌표 gate는 로컬 코스 원점 metadata,
SensorRig frame과 실제 PIE 표시이며, 실제 지도 정렬을 기다리지 않는다.

| ID | 상태 | 작업 | 목표 게이트 |
|---|---|---|---|
| COORD-001 | 완료 | 공개 body 선·각속도와 wheel lateral 값의 FLU/Y-left 계약 및 C++ 회귀 유지 | D7 적용·검증 |
| COORD-002 | 완료 | `yaw_rate`를 좌회전 양수 true FLU body-Z로 정리하고 compound 자세의 navigation Euler yaw 복원을 분리 | schema v2·C++ 회귀 통과 |
| COORD-003 | 자동 범위 완료 | `steering_angle`과 `ControlCommand.steering`을 좌조향 양수로 통일하고 Unreal 입력에서 한 번만 변환 | C++·Proto·UE Editor/Game build 통과; 실제 PIE는 COORD-008 |
| COORD-004 | 완료 | C++·Unreal v2 exact version 검사와 양방향 `Hello`의 source/build/schema/map checksum/capability 검증으로 구 계약을 ordinary state/reset/control 전에 차단 | server/client handshake 자동 회귀·C++ 14/14 통과 |
| COORD-005 | 자동 범위 완료 | C++ `BodyFrameAdapter`와 `GeoTransformAdapter`, Unreal polar/axial·위치/속도/가속도·quaternion 경계 helper 적용; 로컬 원점 metadata 통합은 후속, Cesium geodetic origin은 ADR-013에 따라 Future | C++ 정밀 시험·UE headless Automation 1/1 통과 |
| COORD-006 | 완료 | C++ steering/yaw/heading·vector basis·단위·quaternion golden/round-trip과 Unreal Automation 실행 | C++ 정밀 시험 및 UE `GeoTransformContract` 1/1 `Success` |
| COORD-007 | 해야 함 | SensorRig frame tree·optical frame과 Recorder/MapPackage의 frame convention version 기록 | D11 / M5 |
| COORD-008 | 해야 함 | 목표 Windows UE에서 좌·우 조향, 회전 방향, wheel pose, ENU 위치를 시각·수치로 재검증 | D9 / M3 |

2026-08-21의 semantic sign 변경은 R1의 공통 Proto, C++ host, Unreal wire 경계와
시험을 schema v2 한 변경 묶음으로 적용했다. C++는 v1 ControlCommand를 거부하고
Unreal 소스는 v1 WorldState를 incompatible 상태로 처리한다. Python relay와 ZMQ는
현재 개발 대상이 아니므로 기본 OFF로 동결했다. opt-in observer는 버전 없는 구
`EntityStatePacket`을 사용하므로 schema-v2 호환성 증거가 아니며, 향후 Python을
재개할 때 이 ADR의 canonical 부호와 version handshake를 새 경계에 적용한다. Protobuf
`Hello`는 server/client 양쪽에서 source/build/schema/map checksum과 필수 capability를
검증하며 성공 전 ordinary payload를 fail-closed한다. 최신 Windows UE 5.6 Editor/Game
build와 headless `DriveIntegration.Coordinates.GeoTransformContract` Automation 1/1이
통과했고, 실제 PIE 좌표 시각·로컬 원점 metadata·SensorRig frame 검증은 남아 있다.
Cesium 연동은 8/31 이후 R1 인수의 선행 조건이 아니다.

## 완료 기준

- `base_link` 단위축과 임의 vector의 FLU↔solver↔FLU 왕복 오차가 허용 오차 안이다.
- 좌회전에서 body yaw rate, steering state, steering command, lateral force의 부호가 계약과 일치한다.
- 같은 회전에서 `heading` 증가 방향과 `angular_velocity_body`에서 복원한 navigation Euler yaw rate가 위 관계식으로 일치하며, scalar `yaw_rate`는 body-Z와 일치한다.
- quaternion/basis 왕복과 angular velocity axial-vector 시험이 통과한다.
- local ENU 위치 왕복은 `1e-8m` 이하, quaternion 왕복은 `1e-7°`보다 작은 자동 시험 기준을 사용해 `5cm/0.5°` 표시 허용치보다 충분히 엄격하게 유지한다.
- Unreal에서 좌·우 조향 및 회전이 C++ 기준 상태와 반대로 보이지 않는다.
- 센서 메시지와 기록 데이터가 `frame_id`와 frame convention version을 가진다.

## 결과

- 현재 C++·Unreal 경계와 향후 Python·ROS/자율주행 코드는 하나의 right-handed 좌표 계약을 사용한다. 동결된 legacy observer는 이 완료 범위에 포함하지 않는다.
- Unreal과 solver의 좌표 차이는 제거 대상이 아니라 adapter가 책임지는 명시적 경계가 된다.
- 변환 코드와 시험이 추가되지만, 센서·경로계획·데이터셋 단계에서 반복되는 부호 오류와 재작업을 줄인다.

## 참고 자료

- [ROS REP-103: Standard Units of Measure and Coordinate Conventions](https://reps.openrobotics.org/rep-0103/)
- [Unreal Engine: Coordinate System and Spaces](https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinate-system-and-spaces-in-unreal-engine)
