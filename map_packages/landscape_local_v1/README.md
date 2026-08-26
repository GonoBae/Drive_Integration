# Landscape Local MapPackage

Unreal Editor에서 만든 Landscape와 정적 지면 충돌을 로컬 물리 시험에 사용하는 개발용
MapPackage다.

현재 package collision contract:

- `manifest.cfg`: `ground_surface.csv,static_colliders.csv`를 선언하고 두 파일의 실제 bytes
  checksum을 보관한다.
- `ground_surface.csv`: Unreal Landscape에서 bake한 ENU 삼각형 2,220개다.
- `static_colliders.csv`: strict OBB schema의 header-only 파일이며 현재 authored
  벽·커브가 없어 정상 로드 수는 `static_colliders=0`이다.

`ground_surface.csv`는 `SimCore Ground Collision Exporter` actor의
`Bake Ground + Static Collision To MapPackage` 버튼으로 생성한다. 파일을 직접 편집하지 않는다.
exporter는 Unreal `WorldStatic` 충돌을 수직 raycast로 샘플링하고 다음 좌표 변환을
적용한다.

- east = `(UE world Y - map origin Y) / 100`
- north = `(UE world X - map origin X) / 100`
- up = `(UE world Z - map origin Z) / 100`

차량은 ENU `(0, 0)`에서 시작한다. 따라서 sampling box는 반드시
`Map Origin World Cm`(기본값 UE world `(0, 0, 0)`)을 포함해야 한다. exporter를
viewport의 임의 위치에 떨어뜨렸다면 Details의 `Center Sampling On Map Origin`을 먼저
누른 뒤, Landscape 충돌이 청록색 box 아래에 있는지 확인한다. Bake 성공 창에서
`Map origin coverage: OK`가 표시되어야 한다.

`Ground Actor`에는 주행할 Landscape를 명시한다. 이 경우 exporter는 해당 actor를 하나의
연속 surface로 처리해 35~45° 급경사 cell을 고정 높이 차 기준으로 삭제하지 않는다.
actor를 지정한 뒤 `Fit Sampling Bounds To Ground Actor`를 누르면 actor의 전체 component
bounds에 `Ground Bounds Padding Cm` 기본 100cm를 더해 sampling box 중심·yaw·extent를
맞춘다. 보이는 Landscape보다 좁게 bake해 런타임에서만 막히는 경계를 예방하는 절차다.
결과 창의 `Height discontinuity filter: disabled for explicit Ground Actor`와
`discontinuous=0`을 확인한다. 급경사 형상 정밀도가 필요하면 `Sample Spacing Cm=50`을
권장한다.

현재 tracked `ground_surface.csv` 범위는 `east=-15~15m`, `north=-7~30m`다. fit 버튼은
Editor 설정만 바꾸므로 버튼 적용 후 다시 Bake하기 전에는 CSV와 이 범위가 넓어지지 않는다.
새 CSV를 읽으려면 SimCore 서버도 재시작해야 한다.

runtime은 wheel ground ray가 1~3개만 남은 경우 이를 실제 partial support로 계산한다. 첫
wheel ray가 범위를 벗어났다고 즉시 벽처럼 멈추지 않는다. 별도 vehicle-centre coverage가
없거나 네 wheel ray가 모두 0개일 때만 이전 지원 pose로 fail-closed한다.

벽·커브·barrier는 Place Actors의 `SimCore Static Collider` Box marker로 명시한다. 각
marker에 고유 ASCII `Collider Id`와 semantic을 넣고 실제 장애물에 box 위치·크기·Yaw를
맞춘다. exporter는 ground/static CSV를 모두 임시 파일에 완성해 교체하고 두 collision
payload의 실제 raw bytes checksum을 계산한 뒤 `manifest.cfg`를 마지막에 commit한다.
marker가 없으면 header-only static CSV를 새로 써 이전 장애물이 남지 않는다. CSV만 직접
수정하면 checksum mismatch로 C++와 Unreal이 package를 fail-closed하는 것이 정상이다.

서버는 실행 중 MapPackage를 hot reload하지 않는다. bake한 뒤 다음처럼 재시작한다.

```powershell
.\scripts\run_landscape_server.ps1
```

2026-08-26 runtime smoke에서는 이 package checksum을 검증하고 `2,220 triangles`,
`static_colliders=0`, WebSocket 9000 시작을 확인한 뒤 서버를 종료했다. 이 결과는 loader
자동·실행 검증이며 실제 PIE 벽 충돌 시험을 뜻하지 않는다.

코드에는 adaptive ground grid·한정 fallback, 8m deterministic collision broad phase,
`ASimCoreStaticCollider` authoring/export와 opt-in `--demo-entities` NPC·보행자 lifecycle·
`WorldState`·Unreal 표시까지 구현됐다. 다만 **현재 이 tracked package에는 marker를 실제로
배치·bake하지 않았으며 `static_colliders=0`**이다. 사용자가 marker를 배치하고 다시 bake한
뒤 서버를 재시작해 양수 collider count, PIE 벽·커브 비관통, demo entity identity/표시/
충돌을 확인해야 WP-03 수동 gate를 통과한다. 이 package는 개발용 authoring bridge이며
최종 Wall/Broad collision source를 대체하지 않는다.

8월 26일 작업분의 최종 자동 검증은 CMake Release publisher build, CTest **11/11**,
핵심 4종 각 20회, UE 5.6 Editor build를 통과했다. 이 package runtime smoke는 checksum
`fnv1a64:239e5e39f3706396`, `ground_triangles=2220`, `ground_cells=1872`,
`ground_global=0`, `ground_max_candidates=8`, `static_colliders=0`, demo
`runtime_count=2`, WebSocket `:9000` 시작을 확인했다. 종료 뒤 publisher process와
9000/8765/8000 listener도 남지 않았다. 이는 loader/index/demo runtime 자동 검증이며
실제 authored wall 또는 PIE 충돌 성공을 뜻하지 않는다.

## 2026-08-27 변경 검증 상태

네 wheel spring/damper 기반 차체 자세, 1~3 ray partial support/centre·0-ray fail-closed,
deep one-side·symmetric four-wheel·final-clearance active hard-stop 회귀를 포함한 최신 C++
full Release build와 CTest는 **11/11**, `vehicle_physics_tests`와 `vehicle_config_tests` 반복은
각각 20/20 통과했다. exporter bounds fit이 들어간 최신 Unreal
source의 UE 5.6 `DriveIntegrationEditor` build도 `UBT Result: Succeeded`로 통과했다.
최종 문서 감사 시 tracked package의 background server smoke도 성공했으며 PID `17604`가
`0.0.0.0:9000 LISTENING`, `127.0.0.1:9000` 연결 가능 상태였다. config checksum은
`fnv1a64:2759c831f3bc939a`, MapPackage checksum은 `fnv1a64:bc3ae81aa738d339`이고,
`ground_triangles=2220`, `ground_cells=1872`, `ground_global=0`,
`ground_max_candidates=8`, `static_colliders=0`이었다. 시작 확인 때 stderr는 0 bytes였고
연결 프로브 종료 뒤에는 정상 close 진단 한 줄만 추가됐다. PID는 이후
실행에서도 유지된다는 계약이 아니므로 테스트 전 listener를 확인한다.

`Fit Sampling Bounds To Ground Actor` 버튼 적용과 확장 범위 재-bake, 그 package를 읽는 서버
재시작 및 실제 PIE 주행은 사용자 수동 검증 대기 상태다. 따라서 현재 smoke는 fit 적용 후
새 package 검증으로 간주하지 않는다. 위 2026-08-26 결과도 당시 package loader/index 검증
기록으로 보존하며 이번 변경의 성공 근거로 사용하지 않는다.
