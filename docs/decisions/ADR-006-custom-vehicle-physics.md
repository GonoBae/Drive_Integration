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

이 절의 수치는 2026-08-27 format-v3 중간 튜닝 기록이다. 2026-08-28 실제 선회 반경
회귀를 추가하며 아래 “승용차 조향 envelope 재보정” 값으로 대체했다.

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
- ground coverage는 suspension contact와 별개로 wheel footprint가 authored surface 안에
  있는지 판정한다. 단일 missing corner와 대각선 two-wheel coverage는 partial support로
  계속 푼다. front/rear axle 또는 left/right side의 두 coverage ray가 모두 사라지면
  ground-bound reduced model이 남은 spring만으로 map 끝에서 tip하지 않도록 이전 지원
  step으로 fail-close하고 속도를 0으로 만든다. 별도 centre query miss와 0 wheel ray도
  기존처럼 같은 rollback을 적용한다. 따라서 첫 wheel 하나가 sampling box를 벗어난
  순간의 보이지 않는 벽은 만들지 않으면서, 완전 axle/side 이탈은 airborne 동작으로
  잘못 해석하지 않는다.
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
포함한다. 단일 missing corner·대각선 two-wheel partial support, 완전 axle/side·centre·
0-ray fail-close, wheel-local hub velocity·force, inactive hard-stop travel, compound angular
contract도 함께 검증한다. tracked `landscape_local_v1`을 실제 설정으로 로드해 full-throttle
600 tick 진행하는 회귀는 보완 전 최대 pitch `151.7°`, 최소 z `-0.182m`로 nose-down하는
실패를 재현했고 terminal footprint gate 적용 후 통과했다. Windows full Release build,
CTest 13/13과 차량 물리·차량 설정·MapPackage hot reload·ground query·정적/동적 충돌·
host 핵심 7종 각각 20/20 반복, WebSocket 100/100 반복이
통과했고, UE 5.6 `DriveIntegrationEditor` build도 `UBT Result: Succeeded`로 통과했다. 2026-08-27
문서 감사 시점의 historical background server smoke도 성공했으며 PID `17604`가
`0.0.0.0:9000 LISTENING`,
`127.0.0.1:9000` 연결 가능 상태임을 확인했다. 이 smoke의 vehicle config는
`fnv1a64:2759c831f3bc939a`, MapPackage는 `fnv1a64:bc3ae81aa738d339`이고,
`ground_triangles=2220`, `ground_cells=1872`, `ground_global=0`,
`ground_max_candidates=8`, `static_colliders=0`이었다. 시작 확인 때 stderr는 0 bytes였고,
연결 프로브 종료 뒤에는 `gracefully closed at both endpoints` 진단 한 줄만 추가됐다. PID는
그 시점의 실행 증거일 뿐이다. 같은 날 좁은 30×37m bake 재발을 막기 위해 Ground Actor
전체-bounds Bake preflight·자동 Fit, 20k 예산 spacing 자동 상향과 exporter/host ENU
bbox/span 진단을 추가했다. 실제 Bake는 spacing 507cm, 19,208 triangles, 약
495.88×495.88×22.02m span으로 완료됐고, 같은 PID에서 완전 검증한 새 snapshot을
tick boundary에 적용하는 hot-reload smoke도 통과했다. 실제 PIE 주행은 사용자 수동 검증
대기 상태다.

## 2026-08-28 유한 MapPackage terminal footprint 결정

이 모델은 jump·airborne·완전 전복을 푸는 full 6DoF가 아니라 authored ground에 구속된
reduced-order 차량이다. 유한 CSV 끝에서 한 차축이나 한쪽 측면의 두 ground ray가 동시에
사라진 뒤에도 남은 두 spring만 적분하면, 실제 Landscape 가장자리를 벗어난 차량 운동이
아니라 모델 바깥 상태가 큰 pitch·roll로 누적된다. tracked Landscape full-throttle
600-tick 회귀가 이 경로에서 최대 pitch `151.7°`, 최소 z `-0.182m`를 재현했다.

따라서 surface coverage mask를 다음처럼 판정한다.

- wheel 하나만 miss한 3-corner footprint는 계속 계산한다.
- 서로 대각선인 두 wheel만 hit한 footprint도 계속 계산한다.
- front, rear, left, right 중 어느 완전한 axle/side라도 두 ray가 모두 miss하면 이전 지원
  step으로 fail-close한다.
- centre query miss 또는 네 wheel ray가 모두 miss하면 기존처럼 fail-close한다.

이 gate는 타이어가 droop 범위 밖에 있어 `in_contact=false`인 정상 suspension 상태와
다르며, query가 authored triangle을 찾았는지만 사용한다. 수정 후 같은 tracked-package
600-tick 회귀는 차체가 지면 아래로 내려가거나 뒤집히지 않고 terminal edge에서 정지해
통과했다. 이 안전 경계는 넓은 Landscape를 대신하지 않는다. 실제 주행 범위의 인공적인
끝을 제거하려면 Editor에서 Ground Actor를 지정해 다시 Bake한다. Bake가 full bounds를
자동 Fit하고 실행 중 host가 완전 검증한 snapshot만 tick boundary에 교체한다. 성공 창과
`[MapReload]` 또는 다음 시작 로그의 ENU bbox/span으로 결과를 확인한다. 새 package를 사용한
실제 PIE 주행 검증은 아직 대기 상태다.

## 2026-08-28 승용차 조향 envelope 재보정

입력과 네트워크 latency가 정상인데도 실제 승용차보다 덜 도는 원인은
`comfortable_lateral_accel_mps2=3.2`를 speed-aware 최대 조향 cap으로 재사용하고 rack
증가 rate를 `0.80rad/s`로 둔 데 있었다. comfort 목표와 물리 한계를 분리하고 다음 generic
passenger sedan 기본값을 적용한다.

- steering 증가 rate `1.35rad/s`, 복귀 rate `1.80rad/s`
- speed-aware lateral acceleration cap `5.5m/s²`
- cap은 `tire friction × lateral grip priority × g` 이하라는 strict validation

full-key 자동 계측은 300ms road-wheel angle `23.6°`, 약 5m/s 선회 반경 `4.52m`, 약
10m/s 선회 반경 `20.08m`와 yaw rate `0.508rad/s`를 얻었다. 회귀 허용 범위는 각각
4.0~6.5m, 15~25m이고 bicycle model yaw response 75~115%, Ackermann 안쪽 조향각 우세와
마찰 한도 비초과를 함께 강제한다. 이는 특정 실차 인증값이 아니라 현재 generic sedan의
일관된 검증 envelope다.

## 2026-08-28 MapPackage 교체 시 물리 snapshot 원자성

Bake 뒤 프로세스를 재시작해야만 새 ground를 읽는 계약은 반복 튜닝과 안전 lifecycle에
부적합했다. host는 manifest를 background 감시하되 ground·static collision·checksum 전체가
유효한 후보만 받아 60Hz tick 시작점에서 한 번에 교체한다. 같은 경계에서 차량·clock·lease·
runtime entity를 reset하고 이전 socket/session을 fence한다. Unreal은 재연결 때 manifest를
다시 검증하고 checksum이 달라졌다면 새 PlaySession으로 Reset handshake를 수행한다.

이 결정은 차량 수식을 외부 SDK에 맡기는 변경이 아니다. 자체 물리 solver가 한 tick 안에서
한 버전의 ground/collision만 관측하도록 데이터 수명주기를 강화한 것이다. invalid 또는 작성
중인 후보에는 기존 snapshot을 유지하며, 같은 checksum에는 불필요한 reset을 만들지 않는다.

## 2026-08-31 속도 독립 조향 계약으로 변경

위 8/27~8/28 및 8/31 초반의 speed-aware envelope 조정은 과거 보조 입력 매핑이다.
사용자의 "속도가 높아져도 같은 입력이면 바퀴 각도는 같아야 한다"는 요구에 따라,
속도로 목표 조향각을 줄이던 식과 `comfortable_lateral_accel_mps2` 필드를 제거했다.
키보드 보조를 타이어의 물리적 언더스티어로 설명했던 기준을 바로잡는다.

- 명령은 부호 변환 후 `steering × max_steering_angle_rad`를 목표로 한다.
- 최대 중앙각 35°, rack 증가/복귀 1.80/2.20rad/s와 개별 Ackermann 각도를 유지한다.
  지면 경계 안전정지 분기도 중앙각으로 앞바퀴 두 개를 덮지 않는다.
- 타이어 마찰·slip·하중 및 yaw 적분식은 변경하지 않는다. 실제 선회 반경은 목표각과
  별개 결과이며 고속 full-lock을 무미끄럼으로 만들지 않는다. 선택형 속도 보조는 미구현이다.
- 차량 설정은 v6이다. v5는 명시 전환 안내로 거부하고, v6에 제거된 key가 남아 있어도
  unknown key로 거부한다. 기존 파일을 자동 덮어쓰거나 설정을 조용히 무시하지 않는다.
- 검증은 같은 랙 이력의 속도/기어별 각도 불변성, 유한 응답·복귀, 좌우 Ackermann,
  실제 ENU 경로, 마찰 원과 고속 포화·에너지 한도를 분리한다. 과거 speed-cap에 맞춘
  반경·고속 무미끄럼 기준은 새 물리의 합격조건으로 재사용하지 않는다.

실행 결과와 정확한 비교 조건은 [조향 수정 기록](../vehicle_driving_refinement.md),
[8/31 작업일지](../worklogs/2026-08-31.md)에 기록한다. 외부 물리 SDK 미사용 원칙과
reduced-order 모델의 한계는 유지하며 특정 실차의 계측 인증을 의미하지 않는다.

## 2026-08-31 후속: 실제 지지력 회계와 앞/뒤 강성 분리

최고속도 180km/h를 실제 페달 입력으로 만들어 검사했다. 같은 중앙 35°가 유지되는
것과 차체의 실제 선회 응답은 별개였다. 평지 정속 선회에서도 압축 하드스톱이 차체를
지지하는 실제 velocity-constraint impulse를 타이어 `normal_load`에서 누락한 것을
확인했다. 50km/h·입력 .15에서 spring/damper 13,422.963N과 하드스톱
21.4502Ns/1/60s를 합하면 1,500kg 차량 중량 14,709.975N과 일치한다.

- accepted tick의 바퀴별 실제 최종 impulse를 합산한다. 위치 보정 lambda나 solver
  반복 중간값의 절댓값 합은 쓰지 않는다.
- 직전 tick의 `J / 그 tick의 dt`를 현재도 접지·압축 한계에 있는 타이어의 힘 예산에만
  더한다. 이미 차체 속도에 적용한 impulse를 heave/roll/pitch 힘으로 재적용하지 않는다.
  one-tick 명시적 결합 근사이며 완전한 동시 제약 해법은 아니다.
- 접지/압축 한계 해제, reset, 환경 교체, coverage rollback에서 과거 반력을 폐기한다.
  `mu × 실제 normal_load`는 유지하며 하중 합을 mg로 강제 정규화하지 않는다.
- strict cfg v7은 앞/뒤 바퀴당 고정 코너링 강성을 분리한다. generic sedan은
  60,000/50,000N/rad로 선택했다. v6 단일 값은 양쪽 필드에 같은 값을 복사해 migration하며
  이는 자동 튜닝과 다르다. v5라면 옛 speed-cap 필드도 삭제한다.
- 정상 입력/기어·속도별 rack 계약은 보존하고 Unreal은 비접지 시에도 최신 조향을
  표시한다. 지면은 여전히 Unreal 측정, 차량 물리는 서버 단일 권한이다.

`C = C_nominal × Fz/Fz_nominal` 선형 하중 의존 후보도 비교했으나 기존 Landscape의
10초 직진 가속 회귀에서 roll 65.681°로 45° 상한을 넘었다. 후보식만 되돌렸으며
하중 의존 타이어를 완성한 것으로 기록하지 않는다. 해당 선형 접근은
[MathWorks의 차량 동역학 공식 식](https://www.mathworks.com/help/vdynblks/ref/vehiclebody3dof.html)을
참고했지만, 문헌 식의 존재가 현재 reduced-order 결합의 안정성·실차 정확성을 보장하지 않는다.
최고속 전타각의 큰 slip과 yaw 과도진동은 잔여 검증 항목이다.
최종 결과/시험 조건/미채택 후보는 [조향 기록](../vehicle_driving_refinement.md)과
[작업일지](../worklogs/2026-08-31.md)를 따른다.

## 2026-09-01 후속: 연석은 지면 지지로 등판하고 의미 충돌은 보존

가상 도심의 0.24m 연석은 차체를 막는 `Curb` OBB만 있고 휠·서스펜션이 읽을 보도와
연석 top Ground support가 없었다. 속도나 구동력이 충분해도 서스펜션이 차체를 들어 올릴
입력이 없었으므로, 임계 속도에서 차체 Z를 직접 올리는 보정이나 모든 `Curb` 충돌 무시는
채택하지 않았다.

- Unreal 저작에서는 기존 Ground 59개에 보도 116개와 연석 top 215개를 더해 390개를
  측정하고, 같은 연석의 `Curb` marker는 semantic collision으로 함께 유지한다.
- C++는 current→predicted swept body footprint가 닿는 유한 marker의 along 구간 전체를
  `R` 이하 간격으로 검사한다. 각 위치의 near pair(`half-width + 1R`)와 far pair
  (`half-width + 3R`) 네 support가 모두 유효해야 한다.
- `abs(near delta)`가 `[0.02m, 0.75R]`, far delta가 near delta와 같은 부호,
  `abs(collider height - abs(far delta)) <= 0.30R`, collider 높이가 `0.75R` 이하일 때만
  그 step의 planar body collision에서 해당 `Curb`를 제외한다. `Wall`·`Barrier`, 높은 턱,
  support 누락과 의미가 불명확한 collider는 계속 fail-closed한다.
- 50cm heightfield에서 실제 authored Curb marker 중심을 가로지르는 full-body footprint
  215곳 중 도로와 보도를 잇는 연석 중심 214곳이 이 계약을 만족한다. 옛 1m Bake에 당시
  absolute marker-top 후보 계약을 적용한 결과는 164/215였고, 현재는 50cm+signed delta다.
  옛 bytes를 최종 계약으로 재감사하지 않아 개선분을 해상도에만 귀속하지 않는다. 이는 collider 중심 검사이며 연석 전체 길이의 모든
  위치를 보장하지 않는다. 전체 along-length QA는 210/215 collider 전 길이와 북쪽
  T-opening 네 끝단·BayEnd의 의도적 gap을 확인했고 support 누락 구간은 fail-closed한다.
  `Curb_BayEnd` 중심은 near/far delta의 지속 raised-support 계약을 만족하지 않고 뒤에
  `Barrier_BayEnd`가 있으므로 의도적으로 등판 대상이 아니다.
- 통과 과정은 기존 휠 ray, spring/damper, hard-stop과 타이어 힘이 Ground support를 읽어
  앞축과 뒤축을 순서대로 올리는 결과다. pose·수직 속도·충격량을 별도로 주입하지 않는다.

50cm 저장 `virtual_city_v1` 회귀에서는 5.2252m/s로 접근해 near rise 0.1536m, 차축
support 차이 0.2400m, 차체 상승 0.2119m, 한 step 최대 수직 이동 0.0232m,
최대 pitch 5.7943°, 최소 접지 3개로 통과했다. 30/50m/s `swept-curb-stress`는 current-only
후보 누락·tunneling·velocity erasure를 막는 gate 시험이며 180km/h 실차 승차감 검증이 아니다.
CTest 18/18, UE Automation 35/35, Unreal Game/Editor Development와 맵·교통
`ValidateOnly`를 통과했다. 이는 외부 SDK를 쓰지 않는 reduced-order 물리의 자동 회귀이며
타이어 변형이나 실차 인증을 뜻하지 않는다. 사용자가 직접 조작하는 PIE의 진입 각도·속도,
화면 움직임과 벽·Barrier 비관통 인수는 아직 남아 있다.

저작/서버 경계는 [ADR-012](ADR-012-unreal-ground-measurement-boundary.md), 재현과 해결
절차는 [연석 등판 문제 해결 사례](../troubleshooting/virtual-city-curb-climb-ground-support.md),
실행 근거는 [9/1 작업일지](../worklogs/2026-09-01.md)를 따른다.

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
5. 연속 support가 있는 허용 연석은 서스펜션으로 등판하고, 높은 턱·벽·동적 proxy에는 지속 관통 없음
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
