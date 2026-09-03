# 차량 조작감·카메라·세단 외형

2026-08-31 사용자 주행 피드백 세 가지(뻑뻑한 코너링, 고정 시점, 블록형 차량)를
반영했다. 같은 날 v6에서 속도별 입력 제한을 제거했고 당시 C++/Unreal 자동 검증을
통과했다. 이후 최고속·최대 조향 추가 진단에서 표시 버그와 타이어 지지력 계산 누락을
발견했다. **최신 v7 반력·강성·표시 수정의 자동 검증은 통과**했지만 극한 조향 과도응답과
사용자 PIE 인수는 남아 있다. 이전 v6 수치·25개 UE 시험은 별도 이력으로 보존한다.
C++ 물리 권한과 Unreal 표시/입력 책임은 유지한다. 2026-09-01 초반의 속도 단위 HUD와
50km/h/50m/s step-turn 계측은 production 물리를 바꾸지 않았다. 이후 같은 날 별도
피드백으로 Ground support 기반 연석 등판을 추가했으며 이 후속은 맵 저작과 production
collision/차량 물리를 변경했다.

## 최신 v7 보완 — 자동 검증 통과·극한 응답과 사용자 인수 잔여

### 앞·뒤 타이어 강성 분리와 설정 이행

현재 소스의 strict 차량 설정은 `format_version=7`이며 cornering stiffness를
`front_tire_corner_stiffness_n_rad`와 `rear_tire_corner_stiffness_n_rad`로 분리한다.
두 값은 **각 축 합계가 아닌 타이어 1개당 N/rad**다. 앞/뒤 하중 비율과 강성 배분은
선회 특성에 영향을 주며, 같은 목표 바퀴 각도에서 속도별 반경이 달라진다는 사실만으로
속도 cap이 다시 생겼다고 판단하지 않는다. 최신 선정값은 앞 **60,000N/rad**, 뒤
**50,000N/rad**로, 기존 앞/뒤 55,000N/rad와 네 타이어 강성 총합 220,000N/rad는 같다.
각 값은 하중과 무관하게 고정되며, 실제 하중에 비례시키는 강성 법칙은 채택하지 않았다.

- v6 파일은 `tire_corner_stiffness_n_rad`를 삭제하고 그 기존 값을 앞/뒤 새 항목에
  **각각 그대로 복사**한 뒤 버전을 7로 올린다. 둘로 나누거나 축당 값으로 두 배 하지 않는다.
- v5 파일은 같은 강성 항목 이행과 함께 `comfortable_lateral_accel_mps2`도 삭제한다.
- v5/v6는 명시적 migration 안내 오류로 거부한다. v7에 구 항목을 남기면 unknown key로
  거부하며 자동 변환·무시하지 않는다. v6의 속도 무관 목표각·유한 rack·Ackermann은 유지한다.

### Unreal 접지 상실 시 조향 표시 고정 수정

실제 `AExternalVehiclePawn::Tick`에서 앞바퀴 contact가 사라진 뒤 새 조향각이 도착해도
이전 pivot 회전이 남는 버그를 재현했다. 재현 시험 **FAIL → 수정 → Automation 26/26**,
Unreal 5.6 **Editor/Game Development 빌드 통과**를 확인했다. 접지를 잃으면 마지막
지지 위치는 보존하지만 조향은 body-up 기준으로 최신 서버 각도를 표시하며, 다시
접지하면 contact plane 기준 표시로 돌아간다.

이 시험은 실제 Pawn Tick에 검증용 wire snapshot을 넣어 표시 경로를 검사한다.
socket·실제 주행 입력·서버 control lease는 사용하지 않으므로, synthetic 상태로
최고속 물리 주행에 성공했다는 뜻이 아니다. C++ 실제 solver 주행 계측과 분리한다.

### 서버 타이어 지지력 누락 — 반력 복원·자동 회귀 통과

수정 전 `runtime_logs/cornering-probe-20260831-support-before.csv`의
50km/h·입력 0.15 정속 진단에서 spring 지지력 합은 **13,422.96287N**이었고
FR hard-stop impulse는 **21.4502003N·s**였다. 60Hz에서
`13,422.96287 + 21.4502003 × 60 ≈ 14,709.97489N`으로 1,500kg 중량을 지지하지만,
그 impulse의 지지력 기여가 타이어 마찰 계산에서 빠진 문제를 확인했다.
이는 지지 제약이 담당한 반력을 tire normal-load/힘 예산에 일관되게 반영해야 하는
문제다. 반력 반영 수정은 soft spring·`dt` 변경·reset·no-contact와 신규 50km/h
`ΣFz` 직접 회귀와 기존 물리 시험을 통과했다. 최신 고정 강성의 physics/config/probe
직접 실행도 통과했고 최종 C++ 통합 검증은 **CTest 16/16**이었다. 실제 solver 계측과
남은 극한 과도응답은 아래와 같다.
다만 이 구간 FR의 횡력 이용률은 약 0.52로 비포화였고 FL은 약 0.95의 당시 횡력
한계에 도달했다. 따라서 이 누락 수정만으로 50km/h 선회 반경이 크게 줄어든다고
예측하지 않는다. 별도 수정 전 최고속 Drive/full-lock의 0.5~1초 구간에서는 앞바퀴
양쪽이 포화됐다. 최신 고정 강성 60k/50k N/rad 조정과 반력 복원을 함께 적용한
50km/h·입력 0.15의 반경은 약 32.48m로, 이전 33.947m에서의 완만한 개선이다.
모든 속도에서 같은 반경이나 사용자 체감 전체 해결을 의미하지 않는다.
기존 v6 반경·slip·force-utilization 값은 이 누락을 고친 뒤의 결과가 아니다.
최종 수치·서버 적용은 [작업일지](./worklogs/2026-08-31.md)에서 별도 확정한다.

낮은 slip의 선형 참고값에서 정적 앞 하중 55%·기존 55k/55k의 understeer 계수는
약 `0.0013636rad/(m/s²)` 또는 `0.7662°/g`이고, 60k/50k는 약
`0.000125rad/(m/s²)` 또는 `0.070235°/g`로 완만한 양의 understeer를 유지한다.
이는 작은 slip 이론값이지 고속 full-lock 궤적의 실측이나 보장이 아니다. 180km/h
full-lock의 큰 반경과 과도 yaw 진동은 현재 reduced-order 모델의 잔여 한계다.

**채택하지 않은 실험:** normal load에 비례하는 선형 강성 근사도 평가했다.
normal/nominal load로 선형 횡력 모델을 조정하는
[MathWorks Vehicle Body 3DOF](https://www.mathworks.com/help/vdynblks/ref/vehiclebody3dof.html)의
접근을 참고했지만, 기존 `landscape_local_v1`에서 10초 full-throttle·zero-steer 회귀의
최대 roll이 **65.681°**로 기존 **45°** 한도를 넘어 실패했다. 원인 추가 분석이 필요해
후보를 되돌렸으며 최신 v7은 **고정 per-tire 강성**을 사용한다. 해당 실험은 실제 타이어의
비선형 하중 민감도·실차 동정 검증이 아니다. 유지한 hard-stop 반력 복원도 `μFz`를
임의로 늘리거나 `ΣFz`를 `mg`에 맞춰 정규화하지 않는다.

추가 offline probe는 3개 강성 profile×7개 속도×정속 입력 3개/과도 pedal mode 3개·양방향의
**189개 시나리오·441개 행**을 기록했다. 실제 public pedal 입력으로 가속하며 pose를
주입하지 않는다. 정속 행은 20초 시험 끝 3초 평균, 과도 행은 각 구간 마지막 0.5초 평균이고
heading 변화만 누적값이다. 0.015 입력의 작은 신호 해석은 UE deadzone 0.02 미만이므로
실제 사용자 입력 시험으로 간주하지 않는다. 이는 실제 solver 진단이지 Unreal 주행 영상이 아니다.

### 최신 v7 실제 solver 계측

`runtime_logs/cornering-probe-20260831-support-final-mild.csv`에서 선택된 `mild60_50`
profile의 입력 0.15 정속 결과다. 중앙각은 모두 **5.25°**, 앞바퀴 각도는
**5.3942°/5.1133°**이며 네 바퀴 접지를 유지했다. 측정 종방향 계기판 속도는 각 목표와
거의 같고, ENU CG 선속도는 별도 `path_kph`로 구분한다. 이전 값은 누락 반력·55k/55k,
수정값은 반력 복원·고정 60k/50k를 함께 반영하므로 강성 조정만의 개선율로 해석하지 않는다.

| 목표 종방향 속도 | 측정 CG 궤적 속도 | 이전 반경 | 수정 반경 | 반경 변화 |
|---|---:|---:|---:|---:|
| 30km/h | 30.0177km/h | 30.4765m | 29.5373m | -3.08% |
| 40km/h | 40.0096km/h | 31.2917m | 29.6222m | -5.34% |
| 50km/h | 50.0008km/h | 33.9474m | 32.4800m | -4.32% |

별도 유효 도로 입력 90km/h·0.04 회귀에서는 중앙각 **1.4°**, 종방향 계기판 속도
**90.0007km/h**, CG 궤적 속도 **90.0264km/h**와 네 바퀴 접지를 유지하며 ENU 반경이
이전 약 **145.377m → 114.520m**로 변했다. 작은 신호 0.015 입력의 30/90km/h 회귀도
반경 **295.610/303.179m**와 독립 계산 understeer 계수 `0.000125rad/(m/s²)`를 검증했다.
후자는 UE deadzone 0.02 미만의 solver 진단이며 실제 사용자 입력 시험으로 간주하지 않는다.

수정 후 50km/h·입력 0.15의 spring 합 **13,331.78327N**과 tire load에 반영한
직전 step hard-stop 기여 **1,378.19186N**의 합은 약 **14,709.975N**이다.
하중 예산을 실제 지지 반력에 맞춘 결과이지, 합이 중량과 같아지도록 정규화한 값이 아니다.

180km/h까지 실제 pedal 입력으로 가속한 뒤 Drive/full-lock +1을 유지한 경우에도
중앙 **35°**, 앞바퀴 **41.368°/30.163°**가 유지됐다. 아래는 각 구간 마지막 0.5초의
평균이며 `navigation_yaw_deg_s`는 실제 ENU heading 변화에서 구한 값이다.

| 계측 구간 | 종방향 계기판 속도 | CG 궤적 속도 | 실제 heading 변화율 |
|---|---:|---:|---:|
| 0.5~1.0초 | 169.055km/h | 172.297km/h | +19.4866°/s |
| 1.5~2.0초 | 154.484km/h | 155.284km/h | -9.6588°/s |
| 2.5~3.0초 | 142.768km/h | 143.917km/h | +15.4879°/s |

2초 구간의 부호 반전은 일정 입력에서도 과도 yaw 진동이 남았음을 보여준다.
이 시험을 단순히 “최고속 선회 통과”로 보고하지 않는다. 힘 한도·유한 상태·목표각
계약을 통과하는 것과 타당한 극한 과도응답·사용자 조작감 인수는 다른 게이트다.

### 최종 자동 검증·서버 적용

C++ `ALL_BUILD`와 **CTest 16/16(4.92초, 실패 0·skip 0)**, Unreal 5.6
**Editor/Game Development 빌드·Automation 26/26**, 격리 Health smoke가 통과했다.
신규 small-signal 및 hard-stop 지지력 회귀를 포함하며, Unreal Pawn Tick 표시 시험과
C++ 실제 solver 주행 계측은 서로 다른 증거로 유지한다. Health smoke 실행 기록은
`runtime_logs/health-smoke-20260831-175948-cf042312.*.log`다.

18:00:12에 서버 PID **8520**, 포트 **9000**, 차량 checksum
`fnv1a64:447c250cc148e9b3`으로 적용했다. map checksum `17293781b645eac0`와
traffic checksum `b8e840af96a68990`는 유지했다. 실행 기록은
`runtime_logs/simcore-virtual-city-20260831-180012-008f3867.*.log`이며 PID는 이 시점의
이력이지 영구 고정값이 아니다. **180km/h full-lock 과도 yaw, 사용자 PIE 조작감 인수,
신규 NPC·보행자·SensorRig/record/replay, 패키징·성능·30분 시험과 실차 검증은 완료가 아니다.**

## 바로 테스트하기

가상 도심 서버가 실행 중이면 Unreal에서 `L_VirtualCity`를 열고 Play한다.
화면 안을 한 번 클릭해야 마우스가 게임 입력으로 전달된다.
이번 변경만 적용할 때는 지면 Bake가 필요하지 않다.

| 입력 | 동작 |
|---|---|
| W / S | 전진 / 감속 후 후진 |
| A / D | 좌우 조향 |
| Space | 후륜 사이드 브레이크 / 드리프트 진입 |
| 마우스 이동 | 차량 주위 좌우·상하 시점 회전 |
| 마우스 휠 | 줌 인·아웃 |
| C | 차량 뒤쪽 기본 시점으로 복귀 |
| 게임패드 오른쪽 스틱 / 클릭 | 시점 회전 / 기본 시점 복귀 |
| Shift+F1 | PIE 마우스 캡처 해제 |

주행 조작은 기존 키보드 입력이며 오른쪽 스틱 지원을 전체 게임패드 운전 지원으로
간주하지 않는다. 마우스 회전 후 시점은 C를 누르기 전까지 자동 복귀하지 않는다.
서버 실행 방법과 코스는 [가상 도심 빠른 시작](./virtual_city_quickstart.md)을 따른다.

## 유지되는 조향 계약 — 고정 목표각·유한 rack·Ackermann

사용자가 승인한 기준은 **같은 조향 입력이면 속도와 무관하게 같은 목표 바퀴 각도를
요청**하는 것이다. 속도에 따라 목표각을 줄이던 cap과
`comfortable_lateral_accel_mps2`를 완전히 제거했다. 선택형 조향 보조도 추가하지 않았다.

1. 정규화 입력을 `[-1, 1]`로 clamp하고 `input × max_steering_angle`을 중앙 목표각으로 만든다.
2. 실제 중앙각은 기존 유한 rack 증가/복귀 속도로 목표에 접근한다. 따라서 입력 순간의
   각도는 rack 초기 상태와 경과 시간에 따라 다르지만 차속이 목표각을 다시 줄이지 않는다.
3. 중앙각을 Ackermann 기하로 좌우 앞바퀴 각도에 배분한다. 최대 중앙각 35°와
   rack 증가/복귀 `1.80/2.20rad/s`는 유지하는 설계값이다.
4. 실제 선회 궤적은 바퀴 slip, 노면 마찰, normal load와 기존 타이어 힘·yaw 적분·충돌
   계산에서 나온다. 바퀴가 같은 각도로 꺾였다고 모든 속도에서 같은 반경을 강제하거나
   차량 heading·위치·횡가속도를 목표 곡률에 맞춰 덮어쓰지 않는다.

### 이전 차량 설정 v6 전환 이력

아래는 cap 제거 당시 이행 기록이다. 현재 파일 이행은 위 v7 절차를 따른다.
`cpp/host/config/vehicle_sedan.cfg`의 strict 형식을 당시 `format_version=5`에서 **6**으로 올렸다.
기존 profile은 버전을 올리고 `comfortable_lateral_accel_mps2` 항목을 삭제해야 한다.
v5 입력은 v6 전환과 legacy 항목 삭제를 안내하는 명시적 오류로 거부하고, v6에 legacy
항목이 남으면 unknown key로 거부한다. 기존 knob를 무시하거나 0으로 두어 호환하지 않는다.
최대각·rack rate/return과 타이어/노면 설정은 서로 다른 책임으로 유지한다.
차량 profile은 서버 시작 시 읽으며 지면 hot reload와는 별개다. 이 구조 수정 자체로
Unreal 맵이나 지면을 다시 Bake할 필요는 없다. 실제 새 바이너리 적용·실행 증거는
[작업일지](./worklogs/2026-08-31.md)를 따른다.

### 새 검증 원칙

- 동일 입력·동일 초기 rack 상태·동일 tick 경과에서 속도를 바꿔도 목표각과 rack 응답이
  같아야 한다. 좌우 부호·중립 복귀·유한 slew·Ackermann 내외륜 각도도 검사한다.
- 정상/저마찰 노면에서 각 바퀴의 힘이 해당 `mu × surface_scale × normal_load` 예산을
  지키는지 확인한다. 조향각을 줄여 이 검증을 우회하지 않는다.
- 실제 ENU 위치 증분과 진행 방향 변화로 궤적을 측정하고 실측 종/횡속도·slip·yaw
  응답을 함께 기록한다. `speed / yaw_rate`나 이상적 bicycle 반경만으로 실제 궤적을
  대신하지 않으며, 감속으로 작아진 반경을 조향 자체의 개선으로 보고하지 않는다.
- 유효한 도로 입력 회귀에서는 ENU CG 반경이 기하학적 CG 참고 반경의 **0.85~1.5배**
  범위인지도 확인한다. 이는 해당 저속/보통 입력의 느슨한 경로 타당성 기준이며,
  90km/h full-lock 한계 시험에 적용하거나 실제 경로를 기하학적 원으로 강제하지 않는다.
- 고속 full-lock에서는 타이어 한계에 따른 slip·understeer·감속을 허용한다. 저속과 같은
  작은 반경이나 무미끄럼을 강요하지 않고 유한 상태·힘 한도·기존 안전 경계를 확인한다.
- 자동 회귀와 사용자 PIE 조작감 인수는 구분한다. 최신 보완, 아래 이전 v6 시험과 과거 cap 튜닝
  결과는 별개이며, 전체 R1·사용자 인수까지 완료한 것으로 계산하지 않는다.

### 이전 v6 시험 결과 — 당시 자동 검증 이력

아래 측정과 CTest 16/16·UE 25/25는 hard-stop 지지력 수정 전 결과다.
당시 physics/config test binary를 직접 실행해 통과했다. 같은 입력 이력
`[1, 0.25, 0, -1, 0.5, 0]`에 대해 진입 속도 `0/3/10/25/-5m/s`를 비교했으며,
중앙 rack과 각 wheel 각도의 매 tick 차이가 **1e-6rad 미만**이었다. 최대 중앙각
35°에는 **350ms 이내** 도달했다. 90km/h로 진입한 경우도 도달 시 속도는 20m/s보다
높아, 감속 후에야 최대각이 허용된 결과가 아님을 확인했다.

| 정규화 입력 | 목표 종방향 속도 | 측정 CG 궤적 속도 | 측정 중앙각 | ENU CG 궤적 반경 |
|---|---:|---:|---:|---:|
| 0.15 | 30km/h | 30.0181km/h | 5.25° | 30.477m |
| 0.15 | 40km/h | 40.0107km/h | 5.25° | 31.292m |
| 0.15 | 50km/h | 50.0018km/h | 5.25° | 33.947m |
| 1.0 | 10.8km/h | 11.4880km/h | 35° | 4.189m |
| 1.0 | 18km/h | 18.8149km/h | 35° | 4.484m |

입력 0.15의 정속 비교는 종방향 속도 평균 오차 0.20m/s 미만·변동 범위 0.50m/s 미만,
양방향 반경 차이 2% 이내·네 바퀴 접지·roll 8° 미만 조건을 만족했다. 이는 서로 다른
속도에서도 바퀴 각도가 같고 실제 경로는 타이어 반응에 따라 달라진다는 검증이다.
저속 최대각 시험의 실제 종방향 속도는 각각 10.7999/18.0001km/h였다. CG 궤적 속도는
횡속도도 포함하므로 위 표처럼 종방향 계기판 속도보다 높을 수 있다.

종방향 90km/h 진입 후 입력 1.0·Neutral로 3초 coast한 한계 시험에서는 중앙각 35°를
유지하며 종방향 계기판 속도가 **61.928km/h**로 감소했고 peak tire slip은 **43.878°**, 최대 force utilization은
**1.0**이었다. 고속 무미끄럼 선회가 아니라 cap 없는 한계 조향에서의 slip·힘 포화·감속을
보는 시험이다. **특정 승용차의 실측과 일치한다고 검증한 결과는 아니다.**

당시 `cpp/host/build/Release/vehicle_physics_tests.exe --steering-sweep`는 고정 입력
`0.04/0.15/1.0`과 목표 속도 `10.8/18/30/40/50/90km/h`를 조합한다. 요청 속도,
`longitudinal_kph`(body vx 기반 계기판 속도), `path_kph`(ENU CG 궤적 선속도)를
구분해서 기록한다. 입력 1.0으로 30/40/50km/h를 요청한 경우 **종방향 계기판 속도**는
약 **19.278km/h**, CG 궤적 속도는 약 **20.039km/h**였다. 둘을 혼동하거나,
그때의 작은 반경을 "30/40/50km/h에서의 반경"으로 보고하지 않는다.

당시 최종 C++ `ALL_BUILD`와 강화된 CG 반경 기준을 포함한 **CTest 16/16(5.73초, skip 0)**,
Unreal 5.6 **Editor/Game Development 빌드**, **Automation 25/25**가 통과했다.
Automation에는 신규 Steering 시험 2개가 포함됐고 이 두 시험의 경고는 없었다.
기존 TrafficSignals isolated-world cleanup 경고 3개는 남아 있지만 시험 실패는 0개다.
격리 Health smoke도 통과했다. 로컬 근거는
`runtime_logs/ue-steering-automation-20260831.log`와
`runtime_logs/health-smoke-20260831-171130-8f4dcc99.stdout.log` 실행 기록이다.

당시 17:14:27에 서버 PID `14324`, 포트 `9000`, 차량 checksum `ec2a11a2da7e6a10`으로
적용했으며 map/traffic 데이터는 기존과 동일하다. 이는 그 시점의 실행 이력이지 PID의
영구 고정을 뜻하지 않는다. **사용자 PIE 조작감 인수, 신규 NPC, 패키징·성능·30분 시험과
특정 실차 검증은 완료가 아니다.**

## 2차 도심 속도 조향 보완 — 폐기된 속도 cap 튜닝 이력

아래는 고정 목표각 구조를 승인하기 전 6.8→8.5m/s² cap을 비교한 기록이다.
이 상한과 속도별 반경 개선율은 최신 구현의 설정 또는 합격 기준이 아니다.

1차 수정 후 사용자에게서 "속도가 조금만 빨라도 회전이 너무 안돼"라는 후속 피드백을
받았다. A/D 최대 입력은 서버에 ±1로 정상 도착했고 최근 로그의 적용 지연은 약
5~7ms였다. Unreal 입력이나 바퀴 표시에 별도의 속도 감쇠는 없었다. 정확히 불편한
계기판 속도는 아직 사용자 확인 전이며, 이번 비교는 도심 20~50km/h를 우선했다.

기존 `min(35°, atan(2.7 × 6.8 / planar_speed²))` 제한은 약 18.4km/h부터 조향을
줄였다. 정속 시험의 실제 중앙 조향각은 30km/h 약 14.7°, 40km/h 약 8.45°였다.
제한값을 6.8 → **8.5m/s²**로 완화하고 설정 파일과 내장 기본값을 맞췄다.
35° 최대 조향각, rack rate 1.8/2.2rad/s, 타이어 마찰 계수·강성·마찰 원,
서스펜션·yaw 적분·충돌 계산은 바꾸지 않았다. 이 값은 조향 요청의 상한이며
실제 횡가속도를 강제로 설정하는 값이 아니었다. 당시 profile의 설정상 lateral tire
budget 약 9.79m/s²보다 낮으며, 저마찰 지면에서는 원래의 더 낮은 힘 한도가 적용된다.

이번에는 **실제 ENU 위치 증분과 진행 방향 변화**로 CG 궤적 반경을 측정했다.
pedal-only controller로 가속·제동하고 차체 위치·속도를 덮어쓰지 않았다. 직선
안정화 25초·선회 안정화 12초 후 3초 평균이며, 아래 실측 속도는 body-X 기반
종방향 probe 값이다. 당시 경량 HUD의 `state ... Hz`는 속도가 아니라 네트워크 상태
수신률이었고 차량 속도는 표시하지 않았다. 2026-09-01 추가 HUD는 같은 종방향 속도를
km/h와 m/s로 명시한다. 위치에서 측정한 궤적 속도와는 횡속도 때문에 약간 다를 수 있다.

| 목표 속도 | 이전 실측 속도 | 수정 실측 속도 | 이전 반경 | 수정 반경 |
|---|---:|---:|---:|---:|
| 30km/h | 30.00km/h | 30.43km/h | 11.43m | 10.09m |
| 40km/h | 40.00km/h | 40.00km/h | 20.80m | 17.32m |
| 50km/h | 50.00km/h | 50.00km/h | 33.36m | 28.24m |
| 90km/h | 90.00km/h | 90.00km/h | 121.00m | 109.30m |

당시 30/40/50km/h 비교에서는 속도 오차 0.20m/s 이내·반경 10% 이상 감소를 회귀로
검사했다. 20km/h/full-lock 후보는 수정본 평균 속도가 약 19.28km/h로 낮아져 엄격한
정속 비교에서 제외했다. 이 감속으로 줄어든 반경을 순수 조향 개선으로 보고하지 않는다.
저속 10.8/18km/h 회귀는 별도로 유지한다. 고속도 좁은 코너를 저속처럼 돌도록
만든 것은 아니며, 여전히 코너 반경에 맞는 감속이 필요하다.

좌/우 궤적 반경 대칭·네 바퀴 접지·유한 rack/중립 복귀·후진·full throttle traction
회귀를 유지했다. 90km/h 시험은 body slip 최대 1.324°, tire slip 최대 2.824°,
roll 최대 2.944°였다. 별도 지면 마찰 배율 0.35의 양방향 선회에서도 각 바퀴 힘이
원래 `mu × surface_scale × normal_load` 한도를 넘지 않도록 검사한다.
이는 generic sedan 자동 회귀이지 특정 실차 계측 검증이나 사용자 조작감 인수는 아니다.

당시 `cpp/host/build/Release/vehicle_physics_tests.exe --steering-sweep`는
6.8/8.0/8.5/9.0 후보를 메모리에서만 비교했다. 최신 cap 제거 구조에서는 이 후보 비교를
현재 검증 절차로 사용하지 않는다. 당시 수정은 서버 설정/내장 기본값과 시험만의
변경이었으며 Unreal 재빌드나 지면 Bake는 필요하지 않았다.

## 1차 코너링 변경 — 이력

키보드의 최대 조향 요청을 제한하던 설정과 rack 응답을 조정했다. 타이어 마찰을
없애거나 차량 heading을 강제로 회전시키지 않았다. Ackermann, 네 바퀴 접지,
spring/damper, tire force와 충돌 계산은 기존 C++ 모델을 사용한다.

| generic sedan 설정 | 이전 | 변경 |
|---|---:|---:|
| 최대 중앙 조향각 | 32° | 35° |
| rack 증가 속도 | 1.35rad/s | 1.80rad/s |
| rack 복귀 속도 | 1.80rad/s | 2.20rad/s |
| 속도별 조향 요청의 횡가속도 envelope | 5.5m/s² | 6.8m/s² |

마지막 값은 조향 요청을 제한하는 파라미터다. 차가 항상 6.8m/s²로 회전한다는 뜻이
아니며 타이어의 실제 마찰 예산, 속도와 slip에 의해 실제 반응이 결정된다.
설정 파일 `cpp/host/config/vehicle_sedan.cfg`와 내장 기본값을 함께 갱신했다.
차량 profile 변경은 현재 서버 재시작 때 읽으며, 지면의 자동 hot reload와는 별개다.

당시 평면 최대 조향 시험은 `state.speed / abs(state.yaw_rate)`를 반경 지표로
기록했다. `speed`는 종방향 속도이고 `yaw_rate`는 body-Z 각속도이므로 아래는 엄밀한
CG 궤적 반경이 아닌 **이전 측정 지표 이력**이다. 위 2차 비교에서는 측정법을
보완했다. 바깥 타이어의 최소 회전 반경/회전 직경과도 혼동하지 않는다.

| 속도 | 이전 반경 | 변경 반경 |
|---|---:|---:|
| 10.8km/h | 4.398m | 3.940m |
| 18km/h | 5.072m | 4.414m |
| 30km/h | 13.255m | 11.375m |
| 40km/h | 23.948m | 20.769m |
| 90km/h | 149.489m | 120.891m |

300ms 조향 응답은 약 23.6°→31.3°, 저속 full rack 도달은 약 417ms→350ms다.
90km/h 시험에서는 횡가속도 5.162m/s², 최대 tire slip angle 2.247°,
body sideslip 1.117°, roll 2.636°와 네 바퀴 접지를 확인했다.
특정 실차 계측 데이터를 동정한 결과가 아닌 **자체 generic sedan 회귀 시험**이다.
주관적인 묵직함/조향 감각은 사용자 PIE 주행으로 최종 조정한다.

## 카메라 책임과 제한

`SimCoreOrbitCamera`는 표시 전용 상태다. 마우스는 이동량, 게임패드는 초당 회전률로
계산하므로 마우스에 delta time을 다시 곱하지 않는다. 30/60/120/240fps 수학 회귀와
실제 Pawn 입력 delegate를 검사한다. 카메라 입력은 `ControlCommand`나 차량 pose를
변경하지 않으며 서버 상태가 없을 때도 작동한다.

- 거리: 기본 600cm, 350~1000cm. 기본 pitch -15°, 범위 -65~-5°.
- 차량 yaw를 기준으로 orbit offset을 유지하고 차체 pitch/roll은 카메라 수평에 전달하지 않는다.
- SpringArm의 `ECC_Camera` 충돌 검사로 벽 앞에서 거리를 줄인다.
- 실제 지면/차량 물리용 collision과 카메라 probe는 서로 다른 책임이다.
- 현재는 자유 회전 가능한 외부 추적 카메라다. 운전석·외부 고정·독립 자유 이동과
  replay 촬영까지 포함한 `UE-005/UE-010` 전체 완료는 아니다.

## 자체 세단 에셋

기본 Cube/Cylinder 대신 프로젝트 소스로 생성한 저장형 StaticMesh를 사용한다.

- 차체: `/Game/Vehicles/Sedan/SM_SedanBody`.
- 바퀴: `/Game/Vehicles/Sedan/SM_SedanWheel`, 네 독립 wheel pivot에 같은 메시 사용.
- 재질: `/Game/Vehicles/Sedan/Materials/M_Sedan_*`, 도장·유리·고무·합금·트림·등화류 등 10종.
- 형상: 곡면 보닛/트렁크, 휠 아치, 기울어진 창문, 필러, 미러, 손잡이, 그릴과 등화류.
- 삼각형: 차체 21,336 + 바퀴 4,480×4 = 총 39,256. 최종 램프/범퍼 보완 후 수치다.
- 단위/축: cm, +X 전방, 바퀴 축 +Y. radius 32cm와 기존 wheelbase/track/pivot을 유지한다.
- 정지 시 바퀴 정지·차속 연동 rolling, 서버 suspension/차체 자세 표시 경로를 보존한다.

물리 root는 unit scale, presentation mesh는 `NoCollision`이다. 상세 시각 형상이
서버의 보수적인 차체 충돌 proxy를 대체하지 않는다. 미러 같은 장식 돌출부는 정확한
충돌 형상이 아니다. Unreal Chaos 차량 SDK를 도입하지 않았다.

외부 차량 팩, 사진, bitmap texture, 유료 모델을 취득하지 않은 **자체 제작 일반 세단**이다.
상용 실차 스캔/포토리얼 에셋이 아니며 실내, 손상, 동작하는 제동등·방향지시등은 범위 밖이다.
현재 유리는 실내를 보이는 투명 유리 대신 불투명 짙은 반사 재질을 사용한다.
[에셋 기록](./assets/README.md)에 제작 경계를 남긴다.

### 에셋 생성·검증

Editor target 빌드 후, Unreal을 종료한 상태에서 이 PC 경로 기준으로 실행한다.

```powershell
& 'B:/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'B:/Portfolio/Drive_Integration/unreal/DriveIntegration/DriveIntegration.uproject' `
  -run=BuildSedanVisual -ValidateOnly -unattended -NullRHI -NoSound -NoSplash
```

`-ValidateOnly`를 빼면 없는 에셋을 생성하며 기존 에셋을 무조건 덮어쓰지 않는다.
개발용 `-Regenerate`는 `SimCore.GeneratedVisual=SelfAuthoredSedanV1` 표식이 있는
생성 메시만 갱신할 수 있다. 수동 편집 에셋에는 사용하지 말고 먼저 별도 보존한다.
일반 실행/주행에는 commandlet이나 재생성이 필요 없다.

## 카메라·외형과 이전 조향 검증 이력, 남은 인수

아래 자동/렌더 결과는 고정 목표각 구조 변경 전 기록이다. 새 조향 구조의 직접
시험·실측은 위 절에 분리했으며 기존 카메라/외형 PASS가 조향 변경의 합격을 뜻하지 않는다.

- C++ Release 전체 CTest **15/15**, 관련 physics/config/citycourse 반복 **30/30**.
- UE 5.6 Editor/Game Development 빌드, Automation **15/15**.
- 별도 process의 저장 세단 `-ValidateOnly` 성공.
- 실제 offscreen 게임에서 body/네 wheel mesh와 NoCollision 확인, 마우스 회전·휠·C의
  실제 `APlayerController::InputKey` 입력, 임시 벽에서 카메라 축소/복원, 차량 pose 비변경 확인.
- `VehicleVisual.QAViews`는 Editor 모듈의 개발용 명령이다. 셰이더 완료와 정상 게임 틱을
  기다려 rear/front-three-quarter/front PNG를 `Saved/Screenshots/VehicleVisual-*`에 남긴다.
  프로세스 정상 종료만으로 합격 처리하지 않고 로그의 명시적 PASS를 확인한다.
- 새 profile의 격리 WebSocket Health 시험에서 Reset/Active/SafeStop/복구/
  1008 hard reconnect/EStop 유지 통과. 사용자의 port 9000 서버와 분리해서 시험했다.

남은 수동 인수: 10~20km/h 좁은 코너와 30~40km/h 루프 조향 감각, 경사에서 차체/휠
접지 외형, 운전 중 카메라 편의성. 현재 자동/짧은 렌더 검증은 패키징, 1920×1080
60fps 성능 또는 30분 안정성 합격을 의미하지 않는다.

당시 최종 로컬 증거: `runtime_logs/ue-sedan-camera-automation-final-20260831.log`,
`runtime_logs/ue-sedan-camera-render-final-20260831.log`와
`unreal/DriveIntegration/Saved/Screenshots/VehicleVisual-20260831-151022-835EDC30/`의
3개 PNG다. 화면을 직접 열어 세단 형상/재질과 램프·범퍼 이음새 수정 반영을 확인했다.
`runtime_logs`/`Saved`는 버전 관리 대상이 아닌 재현 가능한 로컬 QA 산출물이다.

## 2026-08-31 최고속 과도 추가 진단 — 이번 턴 후속

이 절은 앞선 v7 60k/50k·hard-stop 반력 복원 **이후**의 추가 작업이다.
앞선 결과를 덮어쓰거나, 남아 있던 최고속 yaw 반전을 해결된 것으로 재분류하지 않는다.
기준은 per-tire 앞/뒤 고정 강성 60,000/50,000N/rad, 실제 접촉 반력 기반 Fz,
속도와 무관한 중앙 조향각 35°다. 하중 비례 강성 후보는 여전히 미채택이다.

### 틱별 힘으로 분리한 원인

`vehicle_cornering_probe --yaw-transient`를 추가했다. 실제 페달로 50m/s에 도달한 뒤
전타각을 넣고 4초 동안 매 틱을 기록한다. 60/120/240Hz × Drive-full/Drive-coast/
Neutral × 좌우 입력, 총 18개 조건이다. 무한 평지에서 속도·pose·yaw를 직접 주입하지
않는다. 기존 189개 시나리오 요약과 달리 매 초 마지막 0.5초만 취하는 표본이 아니다.

기록 항목은 실제 navigation yaw rate, heading 증분, body sideslip, 각 타이어의
Fx/Fy·slip·Fz·spring/stop 반력·yaw moment, 횡방향 slip power, 운동 에너지다.
body-Z 각속도를 navigation yaw로 오인하지 않도록 pitch/roll을 사용해 변환한다.

수정 전 180km/h·왼쪽 전타각·Drive-full의 1.5초에는 앞축 모멘트가 약 +6,790Nm,
뒤축이 −8,487Nm여서 합계 약 −1,697Nm였다. 조향된 앞바퀴 횡력의 후방 성분도
`x*Fy-y*Fx`에 들어가며, 큰 외측 하중과 좌우 Ackermann 각도 차이 때문에 이 성분은
무시할 수 없다. 뒤 타이어도 포화한 상태에서 반대 yaw 모멘트가 유지되다가,
뒤 타이어 slip이 줄어들면 모멘트가 다시 바뀐다. 1.9초의 합계는 약 +2,609Nm였다.

18조건의 각 틱에서 `Izz*Δnavigation_yaw_rate/dt`와 타이어 `Σ(r×F)`의 차이는
최대 약 0.084Nm였다. 횡력의 `Fy*v_lateral_patch`도 모두 음수였다. 따라서 이 관찰을
숨은 속도 조향 cap, heading 부호 오류, 횡력이 slip 방향으로 에너지를 공급하는
오류로 설명할 수 없다. 현재 포화 타이어·하중 모델에서 계산되는 과도응답이다.

### 채택한 최소 수치 보완

별도로 기존 body-frame 평면 속도 적분에 에너지 오류를 확인했다. 힘이 없어도
명시적 Euler의 회전좌표 항은 `|v_next|²=|v|²*(1+(r*dt)²)`를 만들어 속도를 키운다.
샘플링된 힘과 기존 yaw rate를 한 step 동안 고정하는 1차 근사는 유지하되,
회전좌표 항과 그 고정 힘의 적분을 sin/cos 기반 정확 회전으로 바꿨다.
작은 각도에는 sinc/cosc 급수를 사용해 0 나눗셈·상쇄 오차를 피한다.

이 변경은 전체 차량 ODE의 정확해나 완전한 동시 접촉 솔버가 아니다. 타이어 힘,
조향 요청, Ackermann, yaw 모멘트 적분, μ·terrain multiplier·마찰원, traction
control, v7 설정과 hard-stop `J/producing_dt` 처리는 변경하지 않았다. 가짜 yaw,
반전 부호 clamp, 고속 조향각 축소도 추가하지 않았다.

새 `test_rotating_body_frame_does_not_create_force_free_energy`는 실제 페달로 만든
선회 상태에서 샘플링된 타이어 힘의 적분분을 빼고, 남은 좌표 회전이 에너지를
보존하는지 검사한다. 기존 Euler 코드에서는 잔차 절댓값 1.279386J로 1J 기준을
넘어 실패함을 확인했다. 수정 후 최대 잔차는 0.100139J였다. 같은 회귀에서 계산한
기존 Euler의 힘 없는 회전 오차 식은 최대 115.955J/tick이다. 이는 해당 상태의
이론 오차 지표이지, 실제 차량 전체 에너지가 매 틱 그만큼 증가했다는 뜻은 아니다.

### 시간 간격 비교와 남은 P0

아래는 180km/h 진입·왼쪽 전타각·Drive-full의 4초 전체 표본이다. 역방향 회전량은
음수 navigation yaw rate를 시간 적분한 값이며, 순 heading 변화와 다르다.

| 물리 step | 기존 최소 yaw | 수정 후 최소 yaw | 기존 역방향 회전량 | 수정 후 역방향 회전량 |
|---|---:|---:|---:|---:|
| 1/60초 | −13.739°/s | −13.716°/s | −5.644° | −5.613° |
| 1/120초 | −11.889°/s | −11.878°/s | −4.243° | −4.233° |
| 1/240초 | −10.102°/s | −10.098°/s | −3.268° | −3.273° |

수치 에너지 오류는 수정했지만 yaw 반전 개선은 작고, 모든 시간 간격에서 반전이
남는다. **최고속 전타각 P0를 완료로 처리하지 않는다.** 큰 slip에서 현재 선형 강성+
포화·마찰원 모델의 과도응답 타당성, 시간 간격 민감도, 필요시 더 적절한 타이어
모델과 결합 적분을 추가 검증해야 한다. 반전을 없애려는 임의 yaw clamp는 해법이 아니다.

앞축 포화·뒤축 선형인 간단한 2DOF 모델도 감쇠 진동을 가질 수 있지만, 현재 상수의
그 축약 모델만으로 동일한 yaw 반전을 예측하지는 않는다. 실제 probe는 뒤축까지
포화하고 하중이 변한다. 따라서 "문헌이나 실차에서도 똑같이 뒤집히므로 정상"이라고
주장하지 않는다. 특정 실차 계측 동정·극한 조작감 인수는 미완료다.

### 이번 후속 작업의 검증·재현 범위

- 개별 Release `vehicle_physics_tests`, `vehicle_config_tests` 빌드·실행 PASS.
  기존 Landscape·등판·충돌·에너지·지지력·조향 검증 기준을 완화하지 않았다.
- 기존 90/180km/h × 세 페달 모드 × 좌우 12개 시험에 매 틱 모멘트 회계 오차
  0.5Nm 미만, 횡력 slip power 0.1W 이하 검사를 추가해 PASS했다.
- 기존 도시/포화 probe 189개 시나리오·441행, 새 틱별 probe 18조건 PASS.
  30/40/50km/h·중앙각 5.25°의 회귀 반경은 약 29.5373/29.6222/32.4798m로 유지됐다.
- 이 결과는 개별 물리 target 검증이다. 이번 턴의 NPC 등과 합친 `ALL_BUILD`/전체
  CTest·Unreal·실제 서버 접속 인수 결과로 대신 계산하지 않는다.
- 이 물리 하위 작업에서는 실행 중이던 PID8520·port9000 서버를 중지/재시작하지
  않았고, 지면 Bake·맵·에셋·외부 물리 SDK도 변경하지 않았다.

```powershell
& .\cpp\host\build\Release\vehicle_physics_tests.exe
& .\cpp\host\build\Release\vehicle_cornering_probe.exe --yaw-transient `
  runtime_logs/yaw-transient-new.csv
& .\cpp\host\build\Release\vehicle_cornering_probe.exe `
  runtime_logs/cornering-probe-new.csv
```

새 report 경로를 사용해야 하며 기존 파일은 덮어쓰지 않는다. 로컬 증거는
`runtime_logs/yaw-transient-20260831-baseline.csv`,
`runtime_logs/yaw-transient-20260831-exact-frame.csv`,
`runtime_logs/cornering-probe-20260831-exact-frame.csv`다. 이 CSV들은 버전 관리 대상이
아닌 재현 가능한 QA 산출물이며, 사용자 PIE 테스트는 후속 통합 이후 별도 인수다.

## 2026-09-01 속도 단위 HUD와 50km/h step-turn 진단

### HUD에서 수신률과 차량 속도 분리

기존 HUD의 `state 50 Hz`는 차량 속도가 아니라 서버 `WorldState`의 **네트워크 수신률**이다.
현재 표시는 이를 `network state rate=... Hz`로 명시하고, 별도 `vehicle` 행에 승인된
최신 상태의 signed body-X 종방향 속도를 `speed=... km/h (... m/s)`로 표시한다.
`gear`는 D/N/R, `steering`은 실제 중앙 rack 각도 deg, `yaw`는 서버 yaw rate deg/s다.
stale·disconnect·Hello/map/play/sequence gate 실패에서는 마지막 차량 값을 재사용하지
않고 `--`로 숨긴다. 따라서 설정의 `50m/s`는 **180km/h**이고, **50km/h는 13.89m/s**다.

### 동일 full-lock step의 실제 solver 계측

새 probe는 public throttle로 목표 속도까지 가속한 뒤 throttle `1`, brake `0`을 유지하며
조향 입력 `+1`을 step으로 인가한다. pose·속도·yaw·brake·steering 상태를 주입하지 않고
60/120/240Hz에서 반복했다. “90°”는 rack 각도가 아니라 실제 ENU heading 변화다.

| 목표 해석 | 진입 속도 | 중앙 rack 35° 도달 | heading 90° 도달 | 90° 시 종방향 속도 | 90° 유효 반경 |
|---|---:|---:|---:|---:|---:|
| 50km/h = 13.89m/s | 50.01~50.16km/h | 0.342~0.350초 | 2.400~2.421초 | 32.27~32.38km/h | 18.09~18.27m |
| 50m/s = 180km/h | 180.00km/h | 0.342~0.350초 | 6.950~7.013초 | 92.26~93.19km/h | 165.85~169.11m |

요약하면 50km/h에서 전타까지 약 **0.34초**, heading 90°까지 약 **2.4초**이고,
50m/s에서는 같은 전타 시간 뒤 heading 90°까지 약 **7초**다. 두 조건 모두 선회하며
크게 감속했으므로 정속 원선회나 특정 실차의 정확도를 검증한 값으로 사용하지 않는다.

35°의 순수 기하 CG 반경 약 4.13m를 50km/h로 유지하려면 약 **4.76g**가 필요하지만,
현재 계측 횡한계는 약 **0.9975g**이고 앞축은 이미 `mu*Fz` 한도에 도달한다. 따라서
50km/h 정속 최소 반경의 물리 기준은 약 **19.7m**다. 60Hz probe의 첫 1초 반경은
약 **20.43m**, 감속이 누적된 90° 전체 유효 반경은 **18.09m**였다. 작은 코너는
조향각을 더 요구하기보다 진입 전에 감속해야 한다. 현 모델의 거친 진입 가이드는
반경 8m 약 **33km/h**, 반경 5m 약 **26km/h**이며 노면·전이·조작감 보장은 아니다.

이 HUD·step-turn 후속 자체는 diagnostics/probe·자동 시험을 개선했으며 **production
physics는 변경하지 않았다**. 이후 같은 날의 연석 등판 물리 변경은 아래 절과 구분한다.
CTest **18/18**, Unreal 5.6 Editor/Game Development 빌드와
Automation **29/29**가 성공했다. 로컬 근거는
`runtime_logs/step-turn-50-unit-final-20260901.csv`와
`runtime_logs/ue-speed-hud-final-20260901.log`다. 자동 검증은 사용자 PIE 가독성·조작감,
고속 full-lock 과도응답과 특정 실차 동정 인수를 대체하지 않는다. 요약 기록은
[2026-09-01 작업일지](./worklogs/2026-09-01.md)를 따른다.

## 2026-09-01 Ground support 기반 연석 등판

### 속도 분기가 아니라 접촉 가능한 지면을 복원

가상 도심의 0.24m 연석은 이전에 static `Curb` OBB로만 존재했고 보도는 Ground Bake에
없었다. 차체 collision은 연석에서 멈췄지만 휠 ray가 더 높은 support를 읽지 못해,
진입 속도를 올려도 서스펜션과 차체가 올라갈 수 없었다.

현재는 기존 Ground 59개에 보도 116개와 연석 top 215개를 추가한 **390개**를 50cm로
Bake한다. 연석은 휠 support와 semantic static marker 두 역할을 가진다. 서버는
current→predicted swept body footprint와 겹치는 유한 `Curb`의 실제 along 구간을 `R` 이하
간격으로 나눈다. 각 위치에서 near pair(half-width+1R)와 far pair(half-width+3R)의
네 support를 읽고, `abs(near delta)`가 `[0.02m, 0.75R]`, far delta가 near와 같은 부호,
`abs(collider height - abs(far delta)) <= 0.30R`, collider 높이가 `0.75R` 이하일 때만
planar 차체 solve의 해당 연석을 제외한다.
`Wall`·`Barrier`, support 누락과 0.75R 초과 턱은 계속 막힌다.

승인된 연석에서도 휠 ray와 네 spring/damper·hard-stop·타이어 힘이 접촉을 계속 계산한다.
앞축 support가 먼저 오르고 뒤축이 이어지면서 heave와 pitch가 생긴다. 차체 Z·yaw·속도나
충격량을 직접 주입하지 않으므로, 충분한 속도에서 오르는 결과는 별도 속도 임계 분기가
아니라 기존 구동/타이어/서스펜션 계산의 결과다.

### 실제 package 회귀와 한계

새 `virtual_city_v1` checksum은 `fnv1a64:942842ea8d76b7a2`이고 traffic network checksum은
`fnv1a64:32f81819cb6b2b0f`이다. 16cm였던 보도 support를 연석 top과 같은 24cm로 정렬한
실제 package 회귀는 접근 속도 5.2252m/s, near rise 0.1536m, 차축 support 차이 0.2400m,
관측 차체 상승 0.2119m, 한 step 최대 수직 이동 0.0232m, 최대 pitch 5.7943°, 최소 세 바퀴
접지를 기록했다. 0.30m 턱과 0.24m `Wall`은
통과하지 않았다. 50cm heightfield의 authored Curb marker 중심을 가로지르는 full-body
footprint 215곳 중 도로/보도 연석 중심 214곳이 production near/far 계약을 만족했다.
옛 1m Bake+absolute marker-top 후보 계약의 164/215와 달리 현재는 50cm+signed delta다.
옛 1m bytes에 최종 계약을 재적용한 audit은 없어 개선분을 해상도에만 귀속하지 않는다.
이는 연석 전체 길이 보장이 아니며 runtime은 실제 swept along 위치를 다시 검사해 endpoint/opening의 raised support
누락을 fail-closed한다. `Curb_BayEnd` 중심은 뒤 support가 없고 `Barrier_BayEnd`가 경계를
막아 의도적으로 ineligible하다.

30/50m/s `swept-curb-stress`는 current-only 후보 누락·tunneling·velocity erasure를 막는
gate 시험이다. supported
Curb의 첫 tick Δz는 0.115297/0.190091m, pitch는 5.21725/5.18652°, 최소 접지는 4개였고
둘 다 최종 Z 0.788786m였다. 같은 tick의 높은 턱과 Wall은 차단됐다. 이 stress는
180km/h에서의 실차 승차감이나 부드러운 등판을 검증한 결과가 아니다.

C++ CTest **18/18**, UE Automation **35/35**, UE 5.6 Game/Editor Development 빌드와
맵·traffic 두 `ValidateOnly`가 통과했다. 이 모델은 여전히 massless hub·독립 1D
suspension의 reduced-order 모델이며 타이어 carcass 변형, 충격 파손이나 사고 변형을
계산하지 않는다. 실제 사용자 PIE에서 진입 속도·각도별 체감, 순차 axle/차체 움직임,
보도 위 안정과 벽·Barrier 비관통은 수동 인수 대기다.

재Bake와 진단 순서는 [연석 등판 해결 사례](./troubleshooting/virtual-city-curb-climb-ground-support.md),
전체 실행은 [가상 도심 빠른 시작](./virtual_city_quickstart.md)을 따른다.

## 2026-09-01 코드 기반 계기판·배기가스·차량음

세 기능은 모두 **Unreal presentation layer**다. 서버의 `WorldState`를 읽어 화면과 소리로
표현하지만 물리 상태·입력·MapPackage·protocol을 수정하거나 다시 서버에 보내지 않는다.
Blueprint, Widget Blueprint, Niagara/Cascade 저작 에셋, 외부 사운드 파일 없이 새 clone의
PIE에서 자동 생성되는 것을 현재 최소 재현 기준으로 삼았다.

### 계기판

- `ASimCoreInstrumentClusterHud`를 기본 GameMode의 `HUDClass`로 지정해 PIE마다 자동 생성한다.
- fresh·connected snapshot일 때만 authoritative 속도 절댓값을 km/h로 변환하고 RPM·기어·
  연료를 함께 표시한다. Reverse는 기어 `R`로 방향을 표현하며 속도계 숫자는 일반 자동차처럼
  양의 크기로 표시한다.
- stale, reconnect, incompatible, offline, snapshot 누락·비유한 값에서는 이전 telemetry를
  재사용하지 않고 속도/RPM/기어/연료를 `---`/`----`/`-`/`--`로 지운다.
- 사이드 브레이크는 현재 `EntityState`에 authoritative 확인 필드가 없다. 따라서 계기판의
  `SIDE BRAKE`는 `PendingControl`의 **로컬 command intent** 표시이며 서버 적용 확인값으로
  문서화하거나 평가하지 않는다.
- Canvas 기준 1920×1080에서 설계하고 뷰포트의 가로·세로 비율 중 작은 축으로 균일 scale해
  1280×720~4K 16:9에서 화면 아래 중앙에 유지한다. 실제 safe area·가독성은 PIE 수동 gate다.

### 배기가스와 오디오

- `USimCoreExhaustComponent`는 authoritative RPM·종가속도를 배출 강도로 변환하고 Engine
  기본 texture로 작은 CPU sprite emitter를 런타임 구성한다. stale/unavailable이면 목표
  spawn rate를 0으로 감쇠한다.
- `USimCoreVehicleAudioComponent`는 `USynthComponent`에서 RPM 기반 엔진 tone과 차속·접지·
  published longitudinal slip/slip angle 기반 타이어 noise를 합성한다. snapshot이 stale이면
  silence target으로 전환한다.
- procedural 표현은 기능 연결의 재현 가능한 기준이다. 실제 차량 녹음·노면별 샘플·실내/
  외부 mix·occlusion·doppler·배기 온도/바람 시뮬레이션을 구현했다는 뜻은 아니다.

Focused Automation은 계기판 표시/자동 설치 **2/2**, 배기가스 presentation/runtime **2/2**,
오디오 authoritative input **1/1**을 통과했다. UE 5.6 Editor 통합 빌드도 통과했다.
사용자 PIE에서는 다음을 별도로 확인한다.

1. 16:9에서 계기판이 카메라·기존 diagnostics HUD를 가리지 않고 속도·RPM·기어·연료를 읽을 수 있다.
2. End PIE→Play와 연결 끊김→복구에서 old telemetry가 남지 않고 `STALE/RECONNECTING/LIVE`가 전환된다.
3. 정지 idle·가속·coast에서 배기 농도와 엔진 pitch가 연속적이고 배기 위치가 차체 뒤에 붙어 있다.
4. 정상 노면 저속에서는 타이어음이 과하지 않고, 속도·slip 증가 때만 마찰음이 커지며 stale이면 무음이다.
5. 표시·오디오·particle을 켠 상태에서도 authoritative 주행 궤적과 입력 지연이 달라지지 않는다.

## 2026-09-02 키보드 미세 조향과 rear side-brake drift

### 짧은 A/D 입력

키보드 steering ramp와 서버의 속도 무관 최대 rack 35° 계약은 유지하되, keyboard 경로에만
`sign(x) * x²` 응답을 적용했다. 따라서 키를 오래 누르면 여전히 전타에 도달하지만 짧은 tap이
즉시 큰 slip angle을 만들지는 않는다. 0.1초 hold의 예시는 raw 0.125, command 0.015625,
중앙 rack 약 0.55°다. gamepad/analog command는 선형으로 유지한다. 타이어 slip과 friction이
실제 궤적을 결정하므로 이 입력 곡선을 speed-sensitive steering angle로 해석하지 않는다.

### Space rear side brake

Space는 서비스 브레이크가 아니라 뒤축 mechanical side brake다. side brake 자체가 엔진
구동력을 전부 차단하던 조건을 제거하고, rear tire는 longitudinal braking force를 friction
circle에 먼저 배분한 뒤 남은 capacity만 lateral force에 사용한다. front tire는 일반 횡력
배분을 유지한다. 이 조합은 뒤축을 먼저 미끄러뜨려 yaw/sideslip을 만들며 Space를 놓으면
normal combined-friction 경로로 돌아간다.

자동 시험은 다음 계약을 확인한다.

- handbrake rear surface-speed ratio가 정상 주행보다 작고 rear lateral force가 감소한다.
- front wheel surface-speed ratio는 유지되고 각 wheel force는 friction circle 안에 남는다.
- side brake 상태에서 sideslip이 증가하며 service brake 동작은 바꾸지 않는다.
- keyboard 제곱 응답의 좌우 대칭, 짧은 tap과 full-lock 도달을 검증한다.

C++ 전체 CTest **20/20**, UE 5.6 전체 Automation **47/47**과 Editor/Game Development
빌드가 통과했다. 이 수치는 실제 drift 조작감의 합격 판정이 아니다. PIE에서는 약
30~50km/h에서 먼저 선회하고 Space를 0.3~0.7초 눌러 rear slide가 생기는지, 앞축이 계속
조향하는지, 해제 후 throttle/조향으로 회복되는지 확인한다. 짧은 A/D tap만으로 차량 속도가
갑자기 고정·감소하지 않는지도 함께 확인한다.
