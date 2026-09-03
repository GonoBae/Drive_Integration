# Unreal Landscape 급경사 관통·접촉 상실 해결 사례

## 증상

- 완만한 구간에서는 달리지만 일정 경사부터 차량이 지면을 파고든다.
- 접촉이 끊긴 뒤 `z`가 계속 감소하고 PIE를 다시 시작하기 전까지 복구되지 않는다.
- 관통 방지 보완 뒤에는 네 바퀴 접지가 표시돼도 RWD 차량이 오르막에서 추진력을
  잃는 별도 증상이 나타났다.
- 요철에서는 wheel이 올라간 쪽과 반대로 차체가 기울고, 일정 pitch·roll에서 보이지 않는
  각도 경계에 걸리는 증상도 있었다.
- Landscape는 계속 보이지만 좁게 bake한 CSV 끝에서 차량이 더 나아가지 못했다.
- 좁은 CSV 끝으로 조금 이동하면 차체가 남은 두 spring 쪽으로 회전해 코부터 지면 아래로
  고꾸라졌다.
- Bake 창에는 `Map origin coverage: OK`가 표시돼 원점 문제처럼 보이지 않는다.
- 실행 중 다시 Bake하면 checksum mismatch 뒤 조작이 막혀 서버 재시작이 필요했던 운영
  문제가 있었다.

## 원인 판별

### 1. stale MapPackage와 lifecycle

첫 실패에서는 서버 시작 시각이 Bake보다 빨랐고 네 바퀴 접촉점이 모두 `Z=0`, normal
`(0,0,1)`이었다. 당시 서버는 실행 중 CSV를 hot reload하지 않았다. 이후 PIE마다
`SimulationReset`을 전송하도록 구현해 새 Play는 서버 pose와 simulation clock도 원점으로
되돌리게 했다.

### 2. 급경사 물리 제약

초기 물리는 차체 자세를 세계 절대각 pitch `±6°`, roll `±8°`로 제한했고 suspension
mount에서 아래로만 지면을 찾았다. 경사가 차체 자세보다 빨리 높아지면 ray가 지면 아래에서
시작해 `contact=0 → 중력 낙하 → 영구 query miss`가 됐다.

그 보완 뒤에도 다음 결함이 남아 있었다.

- 최소 mount 간격은 `tire radius + minimum suspension length = 0.52m`인데, 실제 차체
  상승 보정은 mount 간격이 타이어 반경 `0.32m`보다 작을 때만 실행됐다.
- 경사면에서 둥근 타이어가 필요한 수직 여유는 `radius / normal_up`인데 평지와 같은
  `radius`만 사용했다. 약 37°에서는 타이어 여유가 약 8cm 부족했다.
- 접촉이 한 프레임 사라질 때 지면 자세를 즉시 0°로 만들고, 새 지면 상대 제한 범위로
  차체를 한 프레임에 수십 도 이동시켜 접촉 상실을 증폭했다.
- 지면 탐색 깊이와 suspension 최대 extension을 같은 값으로 취급해, 앞축이 올라간
  split-level 전환에서 뒤쪽 지면이 존재해도 coverage 자체를 놓쳤다.

### 3. Exporter가 만든 실제 MapPackage 구멍

문제가 난 `landscape_local_v1`은 1,178/1,891개 collision sample이 유효했지만
`2,070 triangles`만 기록됐다. 유효 사각 cell 1,110개 중 75개가
`MaxCellHeightDeltaCm=100`을 넘어 삭제된 것이다.

1m 격자에서 이 기준의 경사 한계는 진행 방향에 따라 약 `35.264°~45°`다. 실제로
약 `36.08°`인 연속 Landscape cell도 네 모서리 높이 차가 `100.9378cm`라는 이유로
통째로 빠졌다. 누락 영역은 ENU `east=4~14m`, `north=4~14m`의 연결된 쐐기였고,
`(7.99, 7.99)`는 hit지만 `(8.001, 8.001)`은 no-hit으로 재현됐다. 즉 같은 Landscape가
화면에는 있어도 C++ 권한 지면에는 약 75m²의 구멍이 있었다.

후속 실시간 상태에서 차량은 `east=313.936m`, `north=570.506m`,
`z=-1526.328m`, 접촉 0개였다. 기존 Bake 범위 `east=-15~15m`, `north=-7~30m`를 크게
벗어난 뒤 no-hit 상태를 계속 적분한 결과다.

### 4. 접지 상태와 타이어 법선 하중을 같은 것으로 본 오류

관통과 지면 query 문제를 해결한 뒤에는 네 바퀴의 geometry contact가 모두 참인데도
차량이 경사를 오르지 못했다. 상태를 분해하면 앞 차축에만 하중이 몰리고 RWD 구동축인
뒤 차축의 normal load가 0이었다.

기존 reduced model은 개별 suspension spring force를 곧바로 타이어 normal load로
사용했다. 급경사 전환에서 뒤 spring이 full droop이면 지면 hit가 존재해도 `N=0`이 되고,
마찰 원의 한계 `mu*N`도 0이 된다. 모든 drive torque가 뒤 차축으로 가는 RWD에서 이는
곧 추진력 0을 뜻한다. AWD로 바꾸면 증상만 가릴 뿐 힘·모멘트 평형 오류는 남으므로
구동 방식은 RWD 그대로 유지했다.

### 5. 연속 평면에서도 남아 있던 차체 부유와 한 축 접촉 상실

MapPackage 구멍과 법선 하중 문제를 해결한 뒤에도 30~40° 연속 평면에서 차체가 노면보다
높게 떠 있고 한 축 또는 한쪽 바퀴가 접촉을 잃었다. 정량 probe에서는 30°·40° 종경사에서
접촉 비트가 각각 `1100`, `1100`, 40° 횡경사에서 `0101`이었고, 차체 기준점의 노면 법선
높이는 설정 CG 높이보다 최대 약 18cm 높았다. 완화된 40° transition에서도 최소 접촉 수가
2개로 내려갔다. 이 단계의 문제는 exporter나 Unreal Landscape collision이 아니라 서버
차체·suspension 기하와 UE 표시 계층에 함께 있었다.

- wheel mount의 수평 위치는 pitch·roll과 무관하게 고정하고 높이에만 `sin(pitch/roll)`을
  적용했다. 축들이 서로 직교하지 않아 40°에서 2.70m wheelbase의 접촉점 간 3D 거리가
  약 3.52m로 늘어났고, 1.58m track도 횡경사에서 약 2.06m로 늘어났다.
- 정지 제동은 속도만 0으로 고정하고 경사 중력을 상쇄하는 static tire reaction을
  힘·모멘트 합에 넣지 않았다. 그 결과 차체 pitch·roll에는 상쇄되지 않은
  `m*g*sin(slope)*cg_height` 모멘트가 남았다.
- 타이어 마찰 한계용 quasi-static equilibrium load를 차체 heave에도 사용했다. 네 바퀴가
  접촉하면 그 합이 항상 `m*g*normal_up`이므로 실제 spring compression과 무관하게 수직
  가속도가 0이 됐고, reset이나 비관통 보정에서 높아진 차체가 다시 내려오지 않았다.
- reset은 pitch·roll이 0인 pose에서 먼저 비관통 높이를 올린 다음 ground attitude를
  적용했다. 첫 contact solve가 만든 과도한 높이와 spring history가 경사 시작 상태에
  남았다.
- UE Pawn은 non-uniform scale이 걸린 차체 mesh를 root로 사용하고 wheel을 고정 상대 위치에
  붙였다. 서버가 보낸 contact point를 wheel 위치에 사용하지 않아 root scale이 wheel
  offset까지 왜곡했고, 서버 접촉이 정확해도 타이어가 지면에서 뜨거나 파묻힐 수 있었다.
- 후속 요철 시험에서는 네 바퀴가 서로 다른 높이에 있어도 각 접촉면 normal이 모두
  `(0,0,1)`이면 그 평균도 수평이라 차체 pitch·roll 목표가 0으로 남았다. 즉 wheel pivot만
  높이와 법선을 따라가고 차체는 네 wheel center가 만드는 지지면을 계산하지 않았다.
- chase camera도 차체 root의 pitch·roll을 그대로 상속해 실제 차체 회전이 화면에서
  상쇄되어 보였다. 이는 서버 자세 누락과 별개인 시각 판별 문제였다.
- 한쪽 지면만 깊게 다시 나타날 때 unconstrained linearized hard-stop correction이 한
  frame에 큰 roll을 만들 수 있었다. 네 바퀴가 동시에 재접촉할 때도 velocity constraint를
  한 방향으로 한 번만 순회하면 먼저 처리한 wheel의 impulse가 남아 대칭 입력에 불필요한
  pitch·roll rate가 생겼다.

### 6. 유한 MapPackage 끝에서 남은 spring으로 차체가 tip한 오류

첫 wheel ray가 bake 범위를 벗어났을 때 바로 rollback하던 보이지 않는 벽을 없애기 위해
초기 partial-support 정책은 wheel ray가 1~3개이면 모두 계속 계산했다. 그러나 이 모델은
airborne과 전복을 푸는 full 6DoF가 아니라 authored ground에 구속된 reduced model이다.
유한 CSV 끝에서 front/rear axle 또는 left/right side의 두 coverage ray가 함께 사라지면
남은 두 spring만으로 pitch·roll을 계속 적분해 차체가 실제 Landscape가 없는 쪽으로
고꾸라질 수 있었다. centre ray는 아직 삼각형 안에 있어 기존 centre/0-ray gate가 너무
늦게 작동했다.

tracked `landscape_local_v1`과 실제 `vehicle_sedan.cfg`를 사용한 full-throttle 600-tick
회귀는 수정 전 최대 pitch `151.7°`, 최소 z `-0.182m`를 기록하며 이 실패를 재현했다.
이는 정상적인 경사 주행이나 suspension droop가 아니라 유한 authored-surface footprint를
ground-bound 모델의 적용 범위 밖까지 적분한 결과였다.

## 최종 수정

1. 세계각 및 ground-relative pitch `±6°`·roll `±8°` hard clamp를 모두 제거한다.
   ordinary attitude 한계는 네 suspension travel과 unilateral contact가 결정한다.
2. 지면 probe를 mount보다 4m 위에서 시작하고 20m 아래까지 탐색한다. 지면 존재 여부와
   suspension 접촉 범위는 별도로 판정한다.
3. 수직 ray는 넓은 지면 coverage 후보를 찾는 데만 쓰고, 실제 suspension length와 tire
   clearance는 body-up 축과 contact normal로 계산한다. compression hard-stop은 뒤의
   `q={z,roll,pitch}` constraint가 처리하며 차체를 world Z로만 올리지 않는다.
4. 일시적인 no-hit에서는 마지막 유효 지면 정보를 유지한다. 최종 자세 적용 후 접촉과
   비관통을 다시 계산하되 support target이나 각도 clamp로 차체를 순간 이동시키지 않는다.
5. `Ground Actor`가 지정된 경우 해당 Landscape를 하나의 연속 authored surface로 취급해
   corner height-delta 필터를 적용하지 않는다. 필터는 여러 `WorldStatic`을 무필터로
   스캔할 때만 벽·다른 층 연결 방지용으로 남긴다.
6. authored-surface coverage에서 단일 missing corner와 대각선 two-wheel coverage는 실제
   partial support로 계속 계산한다. front/rear axle 또는 left/right side의 두 ray가 모두
   사라지면 ground-bound reduced model이 남은 spring으로 tip하기 전에 step 시작의 이전
   지원 pose로 rollback하고 모든 속도를 0으로 만든다. 별도 centre query miss와 0 wheel
   ray도 기존처럼 같은 fail-close를 적용한다. 첫 wheel 하나가 sampling 경계를 넘은 순간의
   보이지 않는 벽은 제거하되, 완전 axle/side 이탈은 airborne 상태로 잘못 적분하지 않는다.
7. format v4의 `front_static_load_fraction`으로 CG부터 앞·뒤 차축까지의 mount arm을
   정한다. tire load는 quasi-static 배분표가 아니라 각 corner의 실제 spring/damper
   reaction에서 얻고, contact가 없거나 full droop이면 그 corner의 인공 grip을 만들지 않는다.
8. 차량 설정 format v4는 `front_static_load_fraction`,
   `front_drive_torque_fraction`, `front_service_brake_fraction`과 함께 조향 응답,
   구동 출력·drag와 저속 slip 기준을 필수로 한다. 기본 generic sedan의 앞축 하중,
   앞축 구동, 앞축 제동 값은 `0.55`, `0.0`(RWD), `0.65`다.
9. 앞바퀴는 Ackermann 조향을 사용한다. open differential는 좌우 half-shaft에 같은
   토크를 전달하되 각 바퀴 속도는 독립 적분해 선회 반경 차이를 허용한다.
10. 60Hz에서 stiff tire와 wheel angular acceleration을 explicit Euler로 따로 적분해
    발생하던 진동은 종방향 타이어 힘과 각속도의 closed-form implicit coupling으로
    안정화한다.
11. 지면 normal에서 longitudinal/right tangent를 직교 정규화하고, 중력 성분과 차량
    이동을 이 basis에서 계산한다. compound grade에서도 pitch와 roll을 독립 경사처럼
    중복 적용하지 않는다.
12. 차체 forward/right/up을 하나의 orthonormal 3D basis로 만들고 모든 suspension mount를
    이 세 축으로 투영한다. pitch·roll 시 wheelbase와 track의 3D 길이가 늘어나지 않는다.
13. suspension은 body-up 방향으로 길이를 풀고, 타이어 접촉점은 실제 지면 tangent patch,
    wheel center는 `patch + ground_normal * tire_radius`로 계산한다. 수직 ray의
    `radius/normal_up` 근사는 contact 판정의 최종 기하로 사용하지 않는다.
14. 차체 heave는 각 wheel의 실제 spring·damper force를 body-up/ground-normal 방향으로
    합산해 적분한다. 같은 corner reaction을 tire normal load와 마찰 원에도 사용하며,
    spring과 무관한 quasi-static 값이 load나 heave 복원력을 덮어쓰지 않는다.
15. 정지 brake·handbrake는 경사 접선 중력을 마찰 한도 안에서 상쇄하는 static contact
    reaction을 만든다. 각 wheel의 contact normal로 local tangent forward/right를 만들고,
    massless hub의 `CG velocity + yaw×arm`을 world에서 그 tangent로 투영해 slip을 계산한다.
    tire force도 같은 local plane에 둔 뒤 world와 common solver basis로 되돌린다. 이 force의
    `tau_i=(contact_patch_i-CG)×F_i`가 pitch·roll moment를 만든다. suspension/heave 축
    속도는 tire scrub으로 중복하지 않으며 중력·공력·driveline drag도 제외한다.
16. reset은 먼저 지면 normal을 구해 차체 basis를 정렬하고, 그 자세에서 mount·contact와
    차체 높이를 푼 뒤 spring history를 seed한다. 첫 simulation tick 전후에 차체가 별도로
    들리거나 한 축이 full droop으로 시작하지 않는다.
17. UE 표시는 unit-scale scene root 아래에 차체 visual과 네 wheel pivot을 분리했다. wheel
    pivot은 매 상태의 contact point와 normal, tire radius로 배치하고 차체 mesh의
    non-uniform scale을 상속하지 않는다. 회전·조향은 이 contact-driven pivot에만 적용한다.
18. 네 wheel center를 차량 yaw 좌표계로 바꿔 `z=a*forward+b*right+c` 지지 평면을 적합한다.
    이 값은 reset pose와 진단에만 사용하며 runtime 자세를 향해 당기는 목표가 아니다.
19. 타이어 normal 평균으로 만든 terrain basis는 중력·타이어 힘·노면 이동에 사용하고,
    wheel support plane은 reset·진단에만 사용한다. 높이가 다른 수평 발판을 가상의 경사
    중력으로 해석하지 않는다.
20. 접촉한 각 wheel의 실제 spring·damper 반력 `F_i`가 roll moment
    `-sum(y_right_i*F_i)`와 pitch moment `sum(x_forward_i*F_i)`를 만든다. 한 축이나 한쪽만
    접촉해도 그 실제 반력을 지우지 않는다. 차체는 네 reaction, heave와 body inertia로
    연속적으로 움직인다.
21. chase camera는 spring arm을 통해 위치와 yaw만 추종하고 차체 pitch·roll은 상속하지
    않는다. 카메라가 함께 기울어 차체 자세 변화가 가려지는 현상을 제거한다.
22. suspension compression hard-stop position은 일반화 좌표 `q={z,roll,pitch}`에서
    반복당 translation 0.15m·rotation 2° trust region으로 최대 32회 푼다. pitch Jacobian은
    Euler pitch의 실제 horizontal-right axis와 roll-dependent generalized inertia를 사용하고,
    잔차만 최소 pure-Z lift로 처리한다. pose correction 뒤 wheel이 다른 triangle에 들어갈 수
    있으므로 correction→ground re-query를 최대 4 outer pass 수행한다. velocity LCP에는 최종
    clearance가 activation slop 안인 hard-stop만 넣어 travel이 남은 corner를 잠그지 않으며,
    누적 nonnegative impulse를 최대 32회 forward/reverse 교대 sweep한다.
23. `angular_velocity_body`는 Euler derivative 세 개가 아니라 true right-handed FLU
    vector이고 scalar `yaw_rate`는 그 vector의 body-Z component다. compound attitude에서
    navigation Euler yaw rate와 같다고 가정하지 않는다. UE는 vector와 현재 pitch·roll에서
    yaw·pitch·roll Euler rate를 재구성해 extrapolation하고, scalar는 gimbal fallback으로만
    사용한다. FLU positive roll과 Unreal `FRotator.Roll`은 같은 방향이므로 다시 반전하지 않는다.
24. Exporter Bake preflight는 Ground Actor 전체 colliding-component bounds에 기본 100cm
    padding을 더한 범위를 검사하고 sampling volume이 작으면 자동 Fit한다. 수동
    `Fit Sampling Bounds To Ground Actor`는 미리보기 용도다. 새 heightfield exporter는
    기본 100cm로 높이·ImpactNormal·valid cell을 측정하고 최대 2,000,000 samples를 넘으면
    spacing을 자동 저하하지 않고 명시적으로 거부한다. 성공 창과 host 시작/reload 로그가
    sample/cell 수, `ground_format` 및 실제 ENU bbox/span을 표시한다.
25. roll/pitch 적분은 finite-angle generalized axis·inertia, `E-dot*qdot`, gyroscopic
    `omega×(I*omega)`와 planar yaw acceleration coupling을 포함한다. 다만 이는 full 6DoF
    모델이 아니다. XY/heading yaw는 planar solver가 정하고, wheel hub는 massless이며,
    unsprung mass·tire carcass·jump/airborne dynamics와 Euler pitch singularity 근처는 범위 밖이다.
26. host는 `manifest.cfg`를 250ms 주기로 감시한다. ground·static collision·checksum 전체를
    background 검증한 최신 후보만 60Hz tick 경계에서 원자 교체하고 차량·clock·lease·entity를
    reset한다. invalid/작성 중 후보에는 기존 snapshot을 유지한다. Unreal은 닫힌 socket에
    재연결하면서 manifest를 다시 검증하고 checksum이 달라졌다면 새 PlaySession으로 Reset
    handshake를 자동 수행한다. 따라서 정상 Bake 후 서버 프로세스를 재시작하지 않는다.
27. 승용차보다 둔했던 조향은 steering rate/return을 `1.35/1.80rad/s`, speed-aware lateral
    acceleration cap을 `5.5m/s²`로 보완했다. cap은 타이어 friction budget을 넘지 못한다.

## 자동 검증

| 항목 | 결과 |
|---|---|
| 25°, 32.5°, 40° 정지 경사 | 6초 동안 매 frame 차체·네 타이어 최소 여유 통과 |
| 평지→40° 완화 전환 | 이동 중 매 frame 비관통 통과 |
| 20° 평면 정지 하중 | 총 타이어 하중이 `m*g*cos(20°)`의 5% 이내이고 앞·뒤 차축 모두 유효 하중 유지 |
| 20° 기본 RWD 등판 | 뒤 차축 하중을 유지하며 6초 동안 2m 이상 지속 전진하고 최종 속도 0.5m/s 초과 |
| 조향·차동 | Ackermann 안쪽 조향각 우세, 직진 좌우 회전수 대칭, 선회 안쪽·바깥쪽 회전수 차이 통과 |
| 승용차 선회 envelope | full-key 약 5m/s 반경 4.0~6.5m, 약 10m/s 반경 15~25m, bicycle yaw response 75~115%, 마찰 한도 비초과 |
| 경사 부유·접촉 회귀 | 20°·30°·40° 종경사와 30° 횡경사에서 각각 180 tick 동안 네 바퀴 접촉 유지 |
| 요철 차체 자세 회귀 | normal이 모두 수직인 서로 다른 높이의 네 wheel center에서 7초 동안 4접촉 유지, 차체 pitch·roll과 지지면 오차 1° 이내, 틱당 변화 0.75° 이하, 가상 수평 이동 없음 |
| 자세 부호·기존 경계 회귀 | 앞이 높으면 positive pitch, 왼쪽이 높으면 positive roll이며 기존 6°/8°보다 큰 자세도 snap 없이 연속 통과 |
| deep one-side hard-stop | 한쪽 지면이 깊게 복귀한 첫 frame에도 finite pose, 지원 wheel 복구, pitch 5°·roll 25° 이내로 flip 방지 |
| symmetric four-wheel 재접촉 | 네 wheel 동시 hard-stop에서 불필요한 pitch·roll 및 `omega_x/omega_y`가 각각 0.1 이내로 order bias 방지 |
| terminal footprint coverage | 단일 missing corner와 대각선 two-wheel coverage는 유지하고, 완전 front/rear axle·left/right side 또는 centre·0-ray miss는 이전 지원 step으로 fail-close |
| tracked Landscape 600 tick | 실제 package·차량 설정 full-throttle에서 수정 전 최대 pitch 151.7°·최소 z -0.182m nose-down 재현; 수정 후 live bake 크기에 불변인 finite pose·centre coverage·비관통·pitch/roll 안정성 통과 |
| wheel-local tangent | 서로 다른 adjacent contact normal에서 hub velocity와 tire force가 각 wheel tangent를 사용 |
| active hard-stop LCP | 한 corner hard-stop이 travel이 남은 반대 corner의 compression velocity를 잠그지 않음 |
| compound angular contract | `yaw_rate == angular_velocity_body.z`와 vector에서 복원한 navigation yaw/heading 부호 검증 |
| 유한 MapPackage 경계 | 20초 가속 후 마지막 지원 pose 정지, 추가 5초에 수평 누적 이동 없음 |
| C++ full Release | 전체 build와 CTest 13/13, 신규 heightfield query·MapPackage hot reload 각각 50/50, WebSocket 100/100 반복 통과 |
| UE 5.6 Editor | 100cm `SIMGHF1` 측정·full-bounds preflight·자동 Fit을 포함한 Editor/Game target build 성공 |
| background server smoke | 2026-08-27 historical smoke는 2,220-triangle package를 확인했다. 2026-08-28 PID `11152` fixture smoke에 이어 실제 PID `6692`가 legacy package에서 checksum `fnv1a64:b8b0a6ccd89614de`의 packed heightfield로 재시작 없이 교체됐다. 실제 payload는 257,556 samples/256,542 cells다. PID는 시점별 실행 증거다. |
| 사용자 수동 gate | 새 100cm payload 실제 Bake와 서버 PID 유지 자동 apply는 완료. 실제 PIE 조향·경사·요철·차체 자세, Sculpt 재Bake 후 UE 재연결과 `static_colliders>0` 벽 충돌 대기 |

당시 CSV는 75개 구멍이 있는 `2,070 triangles` 파일이었다. Editor 재시작 후
`Ground Actor=Landscape_0`을 지정해 다시 Bake했으며, 2026-08-26 17:20 생성 파일을
서버가 예상값인 `2,220 triangles`로 로드했다. 즉 누락됐던 75개 cell(150 triangles)은
현재 파일에서 복원됐다. 이후 수치 감사에서는 이 파일이 내부 hole 없는 30×37m 연속
직사각형이며, 체감 이동 제한은 이 좁은 CSV 자체에서 왔음을 확인했다.

## 운영 체크리스트

1. Unreal Editor를 새 DLL로 다시 시작한다.
2. Exporter의 `Ground Actor`에 `Landscape_0`을 지정한다.
3. 필요하면 수동 Fit 버튼으로 청록색 sampling box를 미리 본다. Bake 자체도 전체 bounds와
   padding을 검사하고 좁으면 자동 Fit한다.
4. `Heightfield Sample Spacing Cm=100`, `Max Heightfield Sample Count=2000000`을 확인하고
   `Bake Ground + Static Collision To MapPackage`를 누른다. limit을 넘는 경우에만 의도적으로
   spacing을 조정한다.
5. 결과 창에서 `Ground Actor bounds coverage: OK`, `Ground sample step`, sample/valid/
   drivable cell 수, `Exported ENU bounds`,
   `Height discontinuity filter: disabled for explicit Ground Actor`, `discontinuous=0`,
   `Map origin coverage: OK`를 확인한다.
6. 서버가 실행 중이면 종료하지 않는다. `[MapReload] verified candidate` 뒤
   `[MapReload] applied at tick boundary`가 같은 프로세스에 출력되는지 확인한다. 서버가
   꺼져 있을 때만 저장소 루트에서 `.\scripts\run_landscape_server.ps1`을 실행한다.
7. 로그에서 `landscape_local_v1`, `ground_format=packed_heightfield_v1`, sample/cell 수와
   `exported_ground_bbox_enu_m`·
   `exported_span_m`을 확인한다. Unreal은 짧게 disconnect/reconnect한 뒤 새 checksum의
   PlaySession Reset을 자동 수행한다.
8. PIE 재시작은 `SimulationReset`으로 서버 차량을 원점에 되돌린다. process-lifetime
   E-stop latch를 해제할 때만 서버 프로세스를 다시 시작한다. 새 snapshot은 hot reload한다.
9. exported ground 밖에서는 차량이 마지막 지원 위치에 멈춘다. 더 멀리 주행하려면
   Ground Actor 전체 범위를 새로 Bake한다.

현재 tracked collision ground는 header-only CSV sentinel과 `507×508` packed heightfield다.
header 기준 257,556 samples/256,542 cells, column·row step 길이 1m이며 checksum은
`fnv1a64:b8b0a6ccd89614de`다. 범위 끝에서 단일 missing
corner와 대각선 two-wheel support는 계속 계산하지만, 완전 axle/side coverage loss 또는
centre/0-ray miss에서는 이전 지원 step으로 안전 정지한다. 이는 물리 각도 clamp가 아니라
ground-bound reduced model을 유한 MapPackage 밖으로 적분하지 않는 terminal footprint
경계다. 인공적인 범위 끝을 더 멀리 옮기려면 Ground Actor 지정→Bake하면 되며 새 package의
실제 PIE 재검증은 대기 상태다.

수직 벽·턱 측면은 별도의 `SimCore Static Collider` marker와 WP-03 `CollisionWorld`가
처리하고, NPC·보행자는 opt-in runtime proxy 경로가 처리한다. 다층 도로 surface 정책과
실제 marker bake·PIE 정적/동적 충돌 인수 시험은 이 지면 접촉 해결 사례의 범위가 아니다.

20° 등판은 generic sedan 수식의 자동 회귀다. 특정 실차 계측값에 대한 정밀 검증이나
사용자 Landscape의 최종 PIE 등판 확인을 대신하지 않는다.
