# ADR-011: 공통 좌표계는 ROS 호환 FLU를 사용하고 Unreal 변환은 경계에서 수행한다

## 상태

- 결정: 채택
- 결정일: 2026-08-19
- 구현 상태: schema v2 scalar 부호와 C++ 경계는 적용 완료, 전체 자세·센서·Windows Unreal 검증은 후속
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

항법 `heading`은 body 회전 벡터가 아닌 별도 scalar다. 기존 사용자 표기와 지도 규약을 위해 `0°=North`, 시계 방향 양수를 유지한다. 따라서 평면 운동에서는 다음 관계를 명시적으로 적용한다.

```text
heading_rate_clockwise = -body_yaw_rate_left_positive
enu_yaw_ccw_from_east = pi/2 - heading_clockwise_from_north
```

물리 계산의 각도와 각속도는 radian을 사용하고, 기존 `EntityState.heading/pitch/roll` 표시 필드만 degree를 사용한다.

### 3. Unreal 경계 변환

`base_link`의 polar vector를 Unreal Actor 로컬 좌표로 표현할 때 축 변환은 다음과 같다.

```text
position_or_linear_vector_ue_cm = 100 * (x_flu, -y_flu, z_flu)
```

방향은 성분별 quaternion 부호를 추측하지 않고 basis matrix `B=diag(1,-1,1)`를 이용해 `R_ue = B * R_flu * B^-1`로 변환한 뒤 Unreal quaternion을 만든다. angular velocity는 axial vector이므로 polar vector와 같은 방식으로 처리하지 않으며, 같은 basis에서 `(wx, wy, wz)_ue = (-wx, wy, -wz)_flu`가 된다.

ENU↔Unreal world 변환은 Cesium Georeference와 원점까지 포함하므로 `GeoTransformAdapter`가 전담한다. 코드 곳곳에서 Y 부호나 meter↔centimeter를 개별 보정하지 않는다.

## 현재 구현과 남은 이행 작업

| ID | 상태 | 작업 | 목표 게이트 |
|---|---|---|---|
| COORD-001 | 완료 | 공개 body 선·각속도와 wheel lateral 값의 FLU/Y-left 계약 및 C++ 회귀 유지 | D7 적용·검증 |
| COORD-002 | 완료 | `yaw_rate`를 좌회전 양수 body yaw로 정리하고 시계 방향 heading과 부호 관계를 분리 | schema v2·C++ 회귀 통과 |
| COORD-003 | 소스 적용 | `steering_angle`과 `ControlCommand.steering`을 좌조향 양수로 통일하고 Unreal 입력에서 한 번만 변환 | C++·Proto 통과, Windows UE 검증 대기 |
| COORD-004 | 부분 완료 | R1 WebSocket의 C++·Unreal v2 exact version 검사로 구 계약 차단; `Hello` handshake는 후속 | packet gate 완료, application handshake D9 |
| COORD-005 | 부분 완료 | C++ `BodyFrameAdapter`와 Unreal scalar 경계 helper 적용; 완전한 ENU↔Cesium·quaternion adapter는 후속 | C++ 완료, Unreal D9 / M3 |
| COORD-006 | 부분 완료 | C++ steering/yaw/heading·wheel force/load·reverse·protocol 시험 완료; vector basis·단위축·quaternion·Unreal automation은 후속 | scalar Mac 자동 시험 통과, Unreal D9 / M3 |
| COORD-007 | 해야 함 | SensorRig frame tree·optical frame과 Recorder/MapPackage의 frame convention version 기록 | D11 / M5 |
| COORD-008 | 해야 함 | 목표 Windows UE에서 좌·우 조향, 회전 방향, wheel pose, ENU 위치를 시각·수치로 재검증 | D9 / M3 |

2026-08-21의 semantic sign 변경은 R1의 공통 Proto, C++ host, Unreal wire 경계와
시험을 schema v2 한 변경 묶음으로 적용했다. C++는 v1 ControlCommand를 거부하고
Unreal 소스는 v1 WorldState를 incompatible 상태로 처리한다. Python relay와 ZMQ는
현재 개발 대상이 아니므로 기본 OFF로 동결했다. opt-in observer는 버전 없는 구
`EntityStatePacket`을 사용하므로 schema-v2 호환성 증거가 아니며, 향후 Python을
재개할 때 이 ADR의 canonical 부호와 version handshake를 새 경계에 적용한다. 다만
Protobuf `Hello`는 아직 정의만 있으므로 이를 application handshake 완료로 기록하지
않는다. 최신 Unreal 변경의 빌드·PIE 검증은 Windows 환경에서 수행한다.

## 완료 기준

- `base_link` 단위축과 임의 vector의 FLU↔solver↔FLU 왕복 오차가 허용 오차 안이다.
- 좌회전에서 body yaw rate, steering state, steering command, lateral force의 부호가 계약과 일치한다.
- 같은 회전에서 `heading` 증가 방향과 body yaw rate가 위 관계식으로 일치한다.
- quaternion/basis 왕복과 angular velocity axial-vector 시험이 통과한다.
- Unreal에서 좌·우 조향 및 회전이 C++ 기준 상태와 반대로 보이지 않는다.
- 센서 메시지와 기록 데이터가 `frame_id`와 frame convention version을 가진다.

## 결과

- 현재 C++·Unreal 경계와 향후 Python·ROS/자율주행 코드는 하나의 right-handed 좌표 계약을 사용한다. 동결된 legacy observer는 이 완료 범위에 포함하지 않는다.
- Unreal과 solver의 좌표 차이는 제거 대상이 아니라 adapter가 책임지는 명시적 경계가 된다.
- 변환 코드와 시험이 추가되지만, 센서·경로계획·데이터셋 단계에서 반복되는 부호 오류와 재작업을 줄인다.

## 참고 자료

- [ROS REP-103: Standard Units of Measure and Coordinate Conventions](https://reps.openrobotics.org/rep-0103/)
- [Unreal Engine: Coordinate System and Spaces](https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinate-system-and-spaces-in-unreal-engine)
