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

## 2026-08-26 경사 등판 모델 보완

급경사에서 네 바퀴가 지면을 찾고도 구동축인 뒤 차축의 법선 하중이 0이 되어
RWD 차량이 오르지 못하는 사례가 확인됐다. 외부 차량 SDK를 도입하지 않고 자체
`VehiclePhysics`의 힘·모멘트 평형과 수치 적분을 다음과 같이 보완했다.

- 차량 설정 형식을 v2로 올리고 `front_static_load_fraction`,
  `front_drive_torque_fraction`, `front_service_brake_fraction`을 필수 값으로 추가했다.
  기본 generic sedan은 각각 `0.55`, `0.0`(RWD), `0.65`다.
- 정적 전축 하중 비율로 CG 위치를 계산한다. CG에서 뒤 차축까지 거리는
  `wheelbase × front_static_load_fraction`, 앞 차축까지 거리는 나머지 wheelbase이며,
  같은 위치를 접촉 질의와 타이어 힘 모멘트에 사용한다.
- 당시에는 합성 경사의 총 법선 하중 `m × g × n_up`과 quasi-static double-track
  평형으로 앞·뒤 및 좌·우 tire load를 배분했다. 이 방식은 등판 회귀를 복구했지만 실제
  spring compression과 분리된 하중이어서, 2026-08-27 네 독립 spring/damper reaction
  모델로 대체했다.
- 경사 normal에서 직교 정규화한 longitudinal/right tangent basis를 구성해 중력과
  차체 이동을 같은 노면 좌표계에서 계산한다. 합성 pitch·roll을 서로 독립인 두 경사로
  중복 계산하지 않는다.
- 앞바퀴 조향은 Ackermann 기하를 사용한다. 구동축은 open differential의 양쪽
  half-shaft에 같은 토크를 전달하지만 바퀴 각속도는 강제로 동기화하지 않아 회전할 때
  안쪽과 바깥쪽 바퀴가 서로 다른 거리와 회전수를 가질 수 있다.
- 60Hz에서 강성이 큰 종방향 타이어를 explicit Euler로 적분할 때 발생하던 마찰 한계
  왕복 진동은 바퀴 각가속도와 타이어 힘의 closed-form implicit coupling으로
  안정화했다.

자동 회귀에는 20° 평면에서 `m × g × cos(20°)` 총하중과 양 차축의 유효 하중,
기본 RWD의 지속 등판, 직진 좌우 회전수 대칭과 선회 시 차동 회전, Ackermann 조향을
포함했다. Windows Release CTest는 9/9를 통과했다.

이 보완은 자체 reduced-order 차량 모델의 물리 일관성과 경사 등판 가능성을 높인
것이지 특정 실차의 정밀 검증을 완료한 것이 아니다. 대상 차종의 계측 파라미터 동정,
타이어 데이터 대조, 완전한 6DoF 강체·접촉 검증은 계속 후속 범위다.

## 2026-08-27 일반 승용차 조작감 보완

PIE 수동 주행에서 차가 마찰 없이 미끄러지고 키보드 조향이 가볍게 튀는 현상을
확인했다. 마찰계수 하나만 크게 만드는 대신 원인이 된 힘·입력 모델을 다음처럼
수정했다.

- `|vx| < 0.5m/s`에서 횡타이어 힘을 0으로 만들던 hard cutoff를 제거했다. 바퀴별
  `max(|wheel_vx|, 2.5m/s)`를 slip-angle 기준 속도로 사용해 0 근처 특이점을 피하면서
  저속 횡속도와 yaw도 네 타이어 힘으로 감쇠한다.
- 키보드 입력을 즉시 최대 32° road-wheel angle로 적용하지 않는다. 목표 횡가속도
  `5.0m/s²`와 현재 속도로 허용 조향각을 계산하고, 조향 증가 `1.75rad/s`, 복귀
  `2.5rad/s`의 rack rate로 접근한다. 약 50km/h의 full-key 목표는 약 4°다.
- 기본 RWD 구동력을 `8000N`에서 `6500N`으로 낮추고 종방향 타이어 강성을
  `14000`에서 `45000N/slip`, 마찰계수를 `1.05`, yaw inertia를 `2850kg·m²`로 조정했다.
  정지 출발부터 뒤 타이어 마찰원을 계속 포화시키던 조건을 제거했다.
- 구동 출력은 `100kW`로 제한하고 throttle 0인 Drive/Reverse에는
  `45N/(m/s)` driveline drag를 적용한다. Neutral은 기존 rolling/aero drag만 받아 두
  상태를 물리적으로 구분한다.
- strict 차량 설정을 format v3로 올리고 위 조향·출력·drag·slip 값을 필수 key와
  checksum에 포함했다.

자동 회귀는 고속 full-key 조향각·rack step/복귀, Drive 대 Neutral 5초 coastdown,
2초 full-throttle 뒤축 slip, 무제동 저속 횡/yaw 감쇠를 검증한다. 기존 20° 등판,
Ackermann, 좌우 독립 회전, 제동, 경사·충돌 회귀도 함께 통과했다. Windows Release
CTest 11/11과 차량·설정·lease·host 4종 각 20회 반복을 통과했으며, 최종 조작감은
동일 Landscape PIE에서 사용자가 확인한다.

## 2026-08-27 후진·구동륜 slip·저속 복합 마찰 보완

평지 출발에서 바퀴 mesh가 차속보다 지나치게 빨리 돌고, 저속 `throttle + steering`
입력에서 차체가 옆으로 흐르는 사례를 다시 분리해 확인했다. 단위 변환 문제가 아니라
구동륜의 순간 접지 상실, 즉시 최대가 되는 키보드 구동력, 종·횡력을 함께 축소하던
friction-circle 처리의 조합이었다. 외부 물리 SDK 없이 자체 모델을 다음처럼 보완했다.

- 목표 구동력은 `18000N/s`로 증가하고 `36000N/s`로 감소한다. 기본 6000N 전진
  구동력이 한 tick에 적용되지 않고 약 0.33초에 걸쳐 형성된다.
- 타이어별 횡력은 먼저 `|Fy| <= 0.95 * mu * Fz`로 제한한다. 종력에는
  `Fx_reserve = sqrt((mu*Fz)^2 - Fy^2)`만 허용해 가속 요구가 조향력을 함께 줄이지
  않도록 한다.
- 구동륜 slip은 8%부터 traction control이 토크를 줄이고 12%에서 완전히 차단한다.
  같은 차축 양쪽에는 더 작은 `Fx_reserve`가 허용하는 동일 토크를 전달한다.
- 구동축 한쪽 바퀴라도 접지를 잃으면 해당 차축의 구동 토크를 0으로 만든다. 공중에
  남은 각속도에는 `3.6N*m*s` 수동 감쇠를 적용해 hidden wheel-speed windup을 막는다.
- generic sedan의 최대 후진력은 3600N, 후진 제한속도는 8m/s로 낮췄다. Unreal 입력은
  반대 방향 페달을 먼저 service brake로 사용하고 signed speed가 0.20m/s 이내이며
  authoritative state가 fresh일 때만 Drive/Reverse를 바꾼다.
- 물리 `WheelState.angular_speed`와 slip은 진단·힘 계산용 권한 상태로 유지한다. Unreal
  axle mesh 회전은 비접촉 휠을 제외한 평균을 `body_speed/tire_radius`의 ±3% 안으로
  제한해 실제 작은 slip은 남기고 비현실적인 시각 과회전은 숨긴다.

strict 차량 설정은 format v4로 올려 위 파라미터를 모두 필수 key·validation·checksum에
포함했다. 회귀 시험은 무접촉 뒤 재접지 hidden omega, 한쪽 구동륜 접촉, 평지 launch
`kappa <= 0.125`, 5/10m/s full-throttle turn의 구동륜 slip·후륜 축 slip angle, 후진
wheel 부호·road coupling을 검증한다. 이 하위 변경 당시 Windows Release CTest 11/11,
차량·설정 각 20회와 UE 5.6 Editor build가 통과했다. 아래 4-corner·좌표 변경 직후에는
UE build를 별도 재검증 대상으로 남겼으며, 최종 검증 결과는 아래에 기록한다.

이 보완은 일반 승용차 범위의 결정적 reduced-order 동작을 목표로 한다. 특정 제조사
차량의 타이어 곡선, 변속기, ABS/ESC ECU와 계측값까지 동정한 모델이라는 뜻은 아니다.

## 2026-08-27 네 바퀴·서스펜션·차체 구조와 자세 부호 수정

요철에서 wheel만 지면을 따라가고 차체가 반대로 기울거나, 일정 각도에서 보이지 않는
경계에 걸리는 현상을 분리했다. 원인은 UE roll 이중 반전, support plane을 향한 인공
attitude spring/damper, 세계각 pitch `±6°`·roll `±8°` hard clamp, 그리고 서스펜션과
분리된 tire load였다. 다음 reduced-order 4-corner 구조로 교체했다.

- 각 wheel의 body-up suspension length에서 독립 spring/damper reaction을 계산한다. 이
  네 반력이 corner tire load, sprung-body heave와 mount arm에 의한 pitch·roll moment를
  함께 결정한다. quasi-static table이 spring 반력을 덮어쓰지 않는다.
- 각 wheel의 contact normal로 local tangent forward/right를 만든다. massless hub의
  `CG velocity + yaw×arm`을 world로 만든 뒤 해당 tangent에 투영해 slip을 계산하고,
  longitudinal/lateral tire force도 같은 평면에 둔 뒤 common solver basis로 되돌린다.
  `tau_i=(contact_patch_i-CG)×F_i`도 이 world force로 계산한다. 따라서 서로 다른 네
  normal을 하나의 평균 평면 velocity·force로 바꾸지 않는다. suspension/heave 축 속도는
  tire scrub으로 중복 계산하지 않는다.
- suspension compression hard-stop은 unilateral contact constraint다. position은
  `q={z,roll,pitch}`에서 반복당 0.15m·2° trust region으로 최대 32회 보정하고, 남은 잔차만
  최소 pure-Z lift로 끝낸다. pitch Jacobian에는 Euler pitch가 실제 회전하는 horizontal-right
  axis와 roll-dependent generalized pitch inertia를 사용한다. 보정으로 mount가 다른 triangle에
  들어갈 수 있으므로 correction→ground re-query 외부 pass를 최대 4회 반복한다. velocity
  LCP에는 최종 pose에서 clearance가 hard-stop activation slop 안인 corner만 넣고, corner별
  누적 nonnegative impulse를 최대 32회 forward/reverse 교대 sweep한다. 유효 ray가 있다는
  이유만으로 아직 travel이 남은 suspension을 잠그지 않는다.
- runtime roll/pitch는 finite-angle Euler generalized dynamics다. `qdot`을 true body angular
  velocity로 보내는 `E`, `E-dot*qdot`, diagonal principal inertia의 gyroscopic
  `omega×(I*omega)`, yaw angular acceleration coupling을 포함한다. 이는 full 6DoF vehicle
  solver가 아니라 planar XY/heading solver가 yaw trajectory를 정하고 sprung-body
  heave/roll/pitch를 푸는 reduced model이다. massless hub, 단순 1D suspension, tire carcass·
  unsprung mass·jump/airborne dynamics 부재와 drivable pitch singularity 제한은 남는다.
- ground coverage ray 1~3개는 wheel별 partial support로 계속 푼다. fail-closed rollback은
  별도 centre query가 authored ground를 잃거나 네 wheel ray가 모두 0개일 때만 적용한다.
  첫 wheel이 sampling box를 벗어났다는 이유만으로 보이지 않는 벽을 만들지 않는다.
- fit한 four-wheel support plane은 reset pose와 진단에만 사용한다. legacy
  `attitude_spring_n_m_rad`, `attitude_damping_n_m_s_rad` key는 v4 호환을 위해 남지만
  validation이 모두 0을 요구하고 solver moment에는 들어가지 않는다. 기존 support-target
  인공 K/C와 pitch/roll hard clamp는 제거했다.
- protocol `angular_velocity_body`는 Euler derivative 묶음이 아니라 true right-handed FLU
  vector다. scalar `yaw_rate`도 navigation Euler yaw rate가 아니라 이 vector의 true body-Z
  component다. UE는 `angular_velocity_body`와 현재 pitch·roll로 navigation yaw·pitch·roll
  Euler rate를 재구성해 제한 extrapolation하고, scalar는 pitch gimbal 부근 fallback으로만
  사용한다. FLU positive roll과 Unreal `FRotator.Roll`은 같은 방향이므로 추가 반전하지 않는다.

회귀 시험은 앞이 높은 지면의 positive pitch, 왼쪽이 높은 지면의 positive roll, 기존
6°/8° 제한을 넘는 연속 자세, deep one-side hard-stop에서 한 frame flip이 없는 bounded
attitude, symmetric four-wheel 재접촉에서 wheel-order pitch/roll impulse가 없는 경우를
포함한다. partial support/centre fail-closed, wheel-local hub velocity·force, inactive
hard-stop travel, compound angular contract도 함께 검증한다. Windows full Release build,
CTest 11/11과 `vehicle_physics_tests`·`vehicle_config_tests` 각각 20/20 반복이 통과했고,
UE 5.6 `DriveIntegrationEditor` build도 `UBT Result: Succeeded`로 통과했다. 최종 문서 감사
시점의 background server smoke도 성공했으며 PID `17604`가 `0.0.0.0:9000 LISTENING`,
`127.0.0.1:9000` 연결 가능 상태임을 확인했다. 이 smoke의 vehicle config는
`fnv1a64:2759c831f3bc939a`, MapPackage는 `fnv1a64:bc3ae81aa738d339`이고,
`ground_triangles=2220`, `ground_cells=1872`, `ground_global=0`,
`ground_max_candidates=8`, `static_colliders=0`이었다. 시작 확인 때 stderr는 0 bytes였고,
연결 프로브 종료 뒤에는 `gracefully closed at both endpoints` 진단 한 줄만 추가됐다. PID는
검증 시점의 실행 증거이며 이후 실행에서도 같다는 계약이 아니다. 다만
`Fit Sampling Bounds To Ground Actor` 적용·재-bake와 실제 PIE 주행은 사용자 수동 검증
대기 상태다.

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
