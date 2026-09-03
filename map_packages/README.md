# Map packages

C++ SimCore와 Unreal이 같은 `map_enu` 충돌 snapshot을 사용하기 위한 버전형 데이터
경로다. R1의 현재 package 구조는 다음과 같다.

2026-08-31 R1 대상은 작은 가상 도심으로 변경됐고, 실제 Unreal blockout을 측정한
[`virtual_city_v1`](./virtual_city_v1/README.md)을 생성했다. `landscape_local_v1`은 기존
Landscape 회귀용, `wall_broad_v1`은 코드/테스트가 참조하는 이전 bootstrap fixture로
보존한다. 가상 도심 실행은 [빠른 시작](../docs/virtual_city_quickstart.md), 범위는
[전환 결정](../docs/decisions/ADR-013-small-virtual-city-course.md)을 따른다.

2026-09-02에는 기존 package를 덮어쓰지 않는 두 교차로 확장
[`signal_city_v2`](./signal_city_v2/README.md)을 추가했다. 별도 map·collision snapshot과
traffic format version 2를 사용하며, 실행·재생성은
[signal city 빠른 시작](../docs/signal_city_quickstart.md)을 따른다.

```text
<map_id>/
  manifest.cfg
  ground_surface.csv       # legacy triangle source 또는 heightfield sentinel
  ground_heightfield.bin   # 새 Unreal 측정 snapshot에서만 존재
  static_colliders.csv
  drive_route.csv         # virtual_city_v1의 QA 전용; collision manifest 밖
  README.md
```

## `manifest.cfg`

현재 format version은 1이며 아래 다섯 key만 허용한다.

```ini
format_version=1
map_id=<package directory identity>
coordinate_frame=map_enu
collision_files=ground_surface.csv,ground_heightfield.bin,static_colliders.csv
collision_checksum=fnv1a64:<16 lowercase hex digits>
```

`collision_checksum`은 선언된 순서대로 각 UTF-8 파일명, NUL, 파일 raw bytes, NUL을
FNV-1a 64로 누적한 실제 collision identity다. C++와 Unreal은 manifest 누락,
unknown/duplicate/missing key, unsafe filename, payload 누락·변조, checksum 불일치를
fail-closed 처리한다. collision payload를 바꿀 때 checksum 문자열만 직접 고치지 않는다.

Unreal `GroundCollisionExporter`는 ground sentinel·heightfield·static CSV를 모두 임시
파일에 완성한 뒤 각 payload를 교체하고, 세 파일의 실제 bytes checksum을 가진
`manifest.cfg`를 마지막에
commit한다. marker가 0개여도 strict header-only `static_colliders.csv`를 새로 써 stale
장애물을 제거한다. 저장이 중간에 끊기면 이전 manifest가 새 payload를 승인하지 않는다.

실행 중 C++ host는 `manifest.cfg`를 250ms 주기로 감시한다. ground·static collision·최종
checksum을 전부 검증한 후보만 simulation thread로 넘기고, 다음 60Hz tick 경계에서 한
snapshot으로 교체한다. 작성 중이거나 invalid인 후보는 거부하고 기존 snapshot을 유지한다.
교체 시 차량·clock·control lifecycle·runtime entity를 reset하고 이전 WebSocket/session을
fence한다. Unreal은 재연결마다 로컬 package를 검증하며 checksum이 달라졌다면 새
PlaySession으로 Reset handshake를 수행한다. 동일 payload는 checksum이 같으므로 불필요한
reload를 만들지 않는다.

## 지면 payload 선택

새 package는 `ground_heightfield.bin`을 선언한다. C++ loader는 이 파일이 선언된 경우
compact heightfield provider를 선택하고 `ground_surface.csv`가 comments와 아래 header만
가진 sentinel인지 확인한다. 구 host가 새 package를 거친 triangle로 조용히 실행하지 않고
empty ground로 fail-closed하게 하기 위한 v1 호환 장치다.

기존 package처럼 `ground_heightfield.bin`을 선언하지 않으면 `ground_surface.csv`의 ENU
triangle provider를 그대로 사용한다. 따라서 manifest version을 올리지 않고도 두 형식을
명시적으로 구분하며, stray binary 파일은 선언되지 않은 한 물리 입력이 아니다.

## `ground_heightfield.bin` (`SIMGHF2`, `SIMGHF1` 호환)

Unreal이 editor scene에서 직접 측정한 regular lattice다. little-endian 80-byte header에
columns/rows와 ENU origin, column/row step vector가 있고, row-major sample마다 높이·
ImpactNormal·valid flag가 있다. `SIMGHF2` cell은 drivable flag에 append-only `material_id`와
finite `[0.05, 4]` terrain friction multiplier를 더한 12-byte record다. yaw가 있는 sampling actor도 affine
basis로 보존한다. server는 basis를 역변환해 해당 cell을 O(1)로 찾고 고정 diagonal에서
높이와 측정 normal을 보간하며 cell 재질을 그대로 `GroundHit`에 전달한다. 기존 4-byte cell
`SIMGHF1`도 계속 읽고 `default(0)`/`1.0` metadata로 해석한다.

stable 기본 ID는 `default=0`, `asphalt=1`, `low-friction=2`, `rough=3`이다. 알 수 없는 미래
ID도 parser가 보존하고 차량 설정의 default profile scale을 사용한다. C++ tire capacity는
`tire_friction × terrain multiplier × vehicle material profile scale × normal load`로 계산한다.

- 기본 spacing: 100cm
- hard maximum: 2,000,000 samples
- sample budget을 넘으면 exporter는 해상도를 자동 저하하지 않고 명시적으로 거부
- single-valued 상단 지면 형식이므로 overpass·동굴 같은 다층 표면은 후속 provider 대상

Unreal은 지면 collision을 측정해 snapshot으로 고정하고, C++만 wheel contact·suspension·
tire·차체 물리를 계산한다. 이 책임 경계는
[ADR-012](../docs/decisions/ADR-012-unreal-ground-measurement-boundary.md)에 기록한다.

## `ground_surface.csv` legacy 형식

고정 header 뒤 각 행이 ENU meter 삼각형 하나다.

```csv
surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2
```

heightfield가 선언되지 않은 package에서 이 파일은 휠 ground hit와 차량 Z·pitch·roll을
위한 표면이다. provider는 package 범위와
분포에 맞춘 adaptive grid로 ray 후보를 줄이고, 겹침이 과도해 안전하게 index할 수 없는
입력에만 한정된 global fallback을 사용한다.

## `static_colliders.csv`

정적 벽·커브·barrier를 수평 OBB와 수직 interval로 나타낸다.

```csv
collider_id,semantic,shape,center_e_m,center_n_m,center_u_m,heading_rad,half_length_m,half_width_m,half_height_m,friction,restitution
```

- `semantic`: `wall`, `curb`, `barrier`
- `shape`: 현재 `obb`만 지원
- `heading_rad`: North=0, clockwise-positive navigation heading
- 세 half extent는 양수여야 하고 friction은 0 이상, restitution은 0~1이어야 한다.
- header-only 파일은 유효하며 authored blocker가 0개임을 뜻한다.

Unreal Editor에서는 `SimCore Static Collider` actor를 wall/curb/barrier마다 배치하고
고유 ASCII `Collider Id`, semantic, box 크기·Yaw, friction/restitution을 지정한다.
`GroundCollisionExporter`의 `Bake Ground + Static Collision To MapPackage`가 활성 marker를
자동 수집해 `map_enu` OBB 행으로 쓴다. marker의 Pitch/Roll, 빈·중복 ID와 잘못된 값은
bake를 실패시킨다.

C++는 manifest에 선언된 strict CSV만 로드하고 OBB-prism SAT, 최대 0.10m/1° microstep,
64-step fail-closed clamp, projection과 normal/friction impulse로 Ego의 ENU XY·yaw를
해결한다. 8m deterministic uniform-grid broad phase가 정적·동적 후보를 ID 순으로
선택하며, cell 예산을 넘는 큰 collider만 안전 fallback으로 처리한다. NPC kinematic OBB와
보행자 vertical capsule은 opt-in `--demo-entities`에서 lifecycle·collision snapshot·
`WorldState` 발행까지 연결돼 있고 Unreal은 이를 transient proxy로 표시한다. 이 demo
fixture는 LaneGraph·신호·R1 목표 개체 수를 갖춘 최종 traffic 기능을 뜻하지 않는다.

## 현재 범위와 후속 데이터

`virtual_city_v1`은 `SIMGHF2`, 50cm spacing, `401×481` lattice의 총 192,881 samples 중
192,079 valid samples, 191,200 valid·drivable cells, static OBB 228개를 담는다. rough/asphalt는
169,690/21,510 cells이고 실제 checksum은 `fnv1a64:e1805b9005157146`다. 명목 영역은
240×200m지만 cube 끝면의 ray hit 누락으로 valid ground는 E `[-119.5,119.5]`,
N `[-20,180]`m의 **239×200m**다. 동·서 끝 각 0.5m는 valid 지면으로 보장하지 않으며
경계 충돌체와 주행 루프는 그 안쪽이다.
594개 도형 가운데 기본 지면·도로 59개, 보도 116개와 연석 top 215개, 총 390개를
Ground component로 측정한다. 연석은 semantic Curb marker도 함께 유지한다. 저장 맵 재로드,
C++ dense route·실제 입력 기반 한 바퀴와 실제 package 연석 등판 회귀를 통과했다.
50cm heightfield의 production full-footprint near(+1R)/far(+3R) probe에서 authored marker
중심 215곳 중 도로/보도 연석 중심 214곳이 후보 조건을 만족하고, Barrier 뒤의 BayEnd
중심 1곳은 의도적으로 제외된다. 옛 1m Bake+absolute marker-top 후보 계약은 164/215였고,
현재는 50cm Bake+signed-delta 계약이다. 옛 bytes를 최종 계약으로 재감사하지 않아 개선분을
해상도 하나에만 귀속하지 않는다. 이는 marker 중심 결과이며 runtime은 실제 swept along
구간을 `R` 이하 간격으로 다시 검사한다. 전체 along-length QA는 210/215 collider 전 길이
eligible, 북쪽 T-opening 네 끝단과 BayEnd만 의도적 gap으로 확인했다.
최종 회귀 결과는 작업일지에 기록하며,
실제 PIE 충돌·접지·재Bake/reconnect·성능·30분 인수는 별도다.

이 package의 `drive_route.csv`는 약 632.6m 코스의 QA용 checkpoint 364개다.
`east_m,north_m,up_m,heading_deg`만 포함하고 LaneGraph·교통 규칙·NPC AI·제품 경로계획을
구현한 것이 아니다. collision checksum에 포함하지 않으며 host는 collision source로
읽지 않는다. 변경 후 필요한 회귀 시험은 [package 설명](./virtual_city_v1/README.md)을 따른다.

기존 package의 history도 유지한다. 특히
`landscape_local_v1/static_colliders.csv`는 marker가 아직 bake되지 않은 header-only
파일이므로 정상 로드 수는 `static_colliders=0`이다. 코드 경로의 자동 구현과 별개로
marker 배치·재bake 뒤 자동 hot reload와 실제 PIE 벽/동적 충돌 확인이 WP-03 수동 gate로
남는다. tracked `landscape_local_v1` ground는 spacing 100cm의 `SIMGHF1`
heightfield이며 507×508 lattice, 257,556 samples, 256,542 cells를 가진다. checksum은
`fnv1a64:b8b0a6ccd89614de`, ENU 범위는 east `-14.604~487.404m`, north
`-7.500~496.500m`, up `0~22.146m`이고 span은 약 `502.008×504.000×22.146m`다.
현재 exporter source로 Physical Surface 또는 `SimCore.Surface.*` tag를 지정해 다시
Bake하면 지면 재질·마찰 metadata를 포함한 `SIMGHF2`로 갱신된다.
LaneGraph, versioned 로컬 원점 metadata,
신호, 보행 경로, spawn, 원본·라이선스 metadata는 WP-05~07에서 manifest format을
버전 상승시켜 추가한다. `manifest.json`이나 임의 `collision.*` 파일은 현재 runtime
규약이 아니다.

2026-08-26 최종 자동 검증에서 Release CTest 11/11, 핵심 4종 각 20회와 UE 5.6 Editor
build가 통과했다. `landscape_local_v1` smoke는 checksum
`fnv1a64:239e5e39f3706396`, ground 2,220 triangles/1,872 cells, global fallback 0,
최대 후보 8, `static_colliders=0`, demo `runtime_count=2`, WebSocket `:9000` 시작을
확인했다. 이 결과는 실제 marker 또는 PIE 충돌 성공 근거가 아니다.

대용량 원본 에셋을 추가하기 전에 라이선스와 Git LFS/외부 저장 정책을 먼저 결정한다.
