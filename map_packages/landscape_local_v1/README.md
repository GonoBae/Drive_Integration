# Landscape Local MapPackage

Unreal Editor에서 만든 Landscape와 정적 지면 충돌을 로컬 물리 시험에 사용하는 개발용
MapPackage다.

현재 tracked package collision contract:

- `manifest.cfg`: `ground_surface.csv,ground_heightfield.bin,static_colliders.csv`를 선언하고
  세 파일의 실제 bytes checksum `fnv1a64:b8b0a6ccd89614de`를 보관한다.
- `ground_surface.csv`: comments와 strict header만 가진 `SIMGHF1` heightfield sentinel이다.
- `ground_heightfield.bin`: Unreal Landscape에서 100cm spacing으로 측정한 507×508 lattice,
  257,556 samples, 256,542 cells의 packed heightfield다.
- `static_colliders.csv`: strict OBB schema의 header-only 파일이며 현재 authored
  벽·커브가 없어 정상 로드 수는 `static_colliders=0`이다.

`ground_surface.csv`는 `SimCore Ground Collision Exporter` actor의
`Bake Ground + Static Collision To MapPackage` 버튼으로 생성한다. 파일을 직접 편집하지 않는다.
현재 exporter는 Unreal `WorldStatic` 충돌을 수직 raycast로 측정해
`ground_heightfield.bin`에 높이·ImpactNormal·sample/cell validity와 cell별
재질·마찰 metadata를 `SIMGHF2`로 기록한다. `ground_surface.csv`는 comments와 strict
header만 남긴 호환 sentinel로 유지된다. 현재 tracked binary는 재질 확장 전
`SIMGHF1`이며 다음 Bake에서 `SIMGHF2`로 갱신된다.
다음 좌표 변환을
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
actor를 지정한 뒤 Bake하면 preflight가 actor의 전체 colliding-component bounds에
`Ground Bounds Padding Cm` 기본 100cm를 더한 범위를 검사하고, sampling volume이 작으면
자동 Fit한다. `Fit Sampling Bounds To Ground Actor`는 Bake 전에 box를 미리 보는 수동
버튼이다. 보이는 Landscape보다 좁게 bake해 런타임에서만 막히는 경계를 예방하는 절차다.
결과 창의 `Height discontinuity filter: disabled for explicit Ground Actor`와
`discontinuous=0`, 실제 `Exported ENU bounds`를 확인한다. 새
`Heightfield Sample Spacing Cm`의 기본값은 100cm이고 2,000,000 samples까지 지원한다.
예산을 넘으면 해상도를 자동으로 낮추지 않고 필요한 spacing 조정을 명시적으로 알린다.
현재 약 506m bounds는 100cm 기준 대략 `507×507=257,049` samples와 5.9MiB라 제한 안이다.

현재 tracked payload는 spacing `100cm`, checksum
`fnv1a64:b8b0a6ccd89614de`, `ground_format=packed_heightfield_v1`이다. 실제 범위는
`east=-14.604~487.404m`, `north=-7.500~496.500m`, `up=0~22.146m`이고 span은 약
`502.008×504.000×22.146m`다. 서버 시작 로그 또는 실행 중 `[MapReload]` 로그의
`ground_format`·`ground_samples`·`ground_cells`·`exported_ground_bbox_enu_m`·
`exported_span_m`으로 실제 로드 범위를 확인한다. Physical Surface 또는
`SimCore.Surface.*` tag를 지정한 다음 Bake 뒤에는 binary magic과 sentinel이
`SIMGHF2`이고 material별 cell count가 표시되어야 한다.

runtime은 authored-surface coverage와 suspension contact를 별도로 판정한다. 단일 missing
corner와 대각선으로 마주 보는 두 wheel ray만 남은 경우는 실제 partial support로 계속
계산하므로 첫 wheel 하나가 범위를 벗어났다고 즉시 벽처럼 멈추지 않는다. 반면 front/rear
axle 또는 left/right side 중 하나의 두 coverage ray가 모두 사라지면 ground-bound reduced
model이 남은 spring으로 map 끝에서 tip하지 않도록 이전 지원 step으로 fail-close하고
속도를 0으로 만든다. 별도 vehicle-centre coverage miss와 네 wheel ray 모두 miss도 기존처럼
같은 안전 정지를 적용한다.

벽·커브·barrier는 Place Actors의 `SimCore Static Collider` Box marker로 명시한다. 각
marker에 고유 ASCII `Collider Id`와 semantic을 넣고 실제 장애물에 box 위치·크기·Yaw를
맞춘다. exporter는 ground sentinel·heightfield·static CSV를 모두 임시 파일에 완성해
교체하고 세 collision payload의 실제 raw bytes checksum을 계산한 뒤 `manifest.cfg`를
마지막에 commit한다.
marker가 없으면 header-only static CSV를 새로 써 이전 장애물이 남지 않는다. CSV만 직접
수정하면 checksum mismatch로 C++와 Unreal이 package를 fail-closed하는 것이 정상이다.

서버는 `manifest.cfg`를 250ms 주기로 감시한다. exporter가 payload를 먼저 교체하고
manifest를 마지막에 commit하면 서버가 ground·static collision·checksum 전체를
백그라운드에서 검증한다. 검증된 snapshot만 다음 고정 tick 경계에서 원자적으로 적용하고
차량·clock·lease·runtime entity를 reset한다. 검증 실패 또는 작성 중인 후보는 버리고 기존
snapshot을 유지한다. 따라서 Bake 뒤 프로세스를 재시작하지 않아도 된다. 서버가 꺼져 있을
때 시작하거나 필요에 따라 수동 실행할 명령은 다음과 같다.

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
뒤 `[MapReload]`의 양수 collider count, PIE 벽·커브 비관통, demo entity identity/표시/
충돌을 확인해야 WP-03 수동 gate를 통과한다. 이 package는 개발용 authoring bridge이며
최종 Wall/Broad collision source를 대체하지 않는다.

8월 26일 작업분의 최종 자동 검증은 CMake Release publisher build, CTest **11/11**,
핵심 4종 각 20회, UE 5.6 Editor build를 통과했다. 이 package runtime smoke는 checksum
`fnv1a64:239e5e39f3706396`, `ground_triangles=2220`, `ground_cells=1872`,
`ground_global=0`, `ground_max_candidates=8`, `static_colliders=0`, demo
`runtime_count=2`, WebSocket `:9000` 시작을 확인했다. 종료 뒤 publisher process와
9000/8765/8000 listener도 남지 않았다. 이는 loader/index/demo runtime 자동 검증이며
실제 authored wall 또는 PIE 충돌 성공을 뜻하지 않는다.

## 2026-08-27 4-corner 변경 검증 상태

네 wheel spring/damper 기반 차체 자세, 당시 1~3 ray partial support·centre/0-ray gate,
deep one-side·symmetric four-wheel·final-clearance active hard-stop 회귀를 포함한 C++
full Release build와 CTest는 **11/11**, `vehicle_physics_tests`와 `vehicle_config_tests` 반복은
각각 20/20 통과했다. exporter bounds fit이 들어간 최신 Unreal
source의 UE 5.6 `DriveIntegrationEditor` build도 `UBT Result: Succeeded`로 통과했다.
2026-08-27 historical 문서 감사에서 tracked package의 background server smoke도 성공했으며 PID `17604`가
`0.0.0.0:9000 LISTENING`, `127.0.0.1:9000` 연결 가능 상태였다. config checksum은
`fnv1a64:2759c831f3bc939a`, MapPackage checksum은 `fnv1a64:bc3ae81aa738d339`이고,
`ground_triangles=2220`, `ground_cells=1872`, `ground_global=0`,
`ground_max_candidates=8`, `static_colliders=0`이었다. 시작 확인 때 stderr는 0 bytes였고
연결 프로브 종료 뒤에는 정상 close 진단 한 줄만 추가됐다. PID는 이후 실행에서도 유지된다는
계약이 아니다. 그 뒤의 bbox/span smoke도 정상 종료까지 확인했다. 이 문단의 프로세스
상태는 historical 기록이며 현재 인계 상태는 아래 최신 절을 따른다.

위 2026-08-27 항목은 당시 좁은 package에 대한 historical 기록이다. 2026-08-28에는
full-bounds Bake와 확대 package 자동 hot reload smoke를 완료했다. 실제 PIE 경사·조향·
marker 충돌은 사용자 수동 검증 대기 상태이며, 위 2026-08-26 결과도 당시
package loader/index 검증 기록으로 보존한다.

## 2026-08-28 terminal footprint 회귀

tracked `landscape_local_v1`과 실제 차량 설정을 로드해 full-throttle로 600 tick 진행하는
회귀를 추가했다. 수정 전에는 유한 bake 북쪽 끝에서 완전한 차축 또는 측면 coverage를 잃은
뒤 남은 spring만으로 차체가 회전해 최대 pitch `151.7°`, 최소 z `-0.182m`로 nose-down했다.
terminal footprint gate를 적용한 뒤 같은 회귀는 차체가 지면 아래로 내려가거나 전복하지
않고 이전 지원 step에서 안전 정지해 통과했다.

최신 Windows Release 전체 build와 CTest는 **12/12**, 차량 물리·차량 설정·MapPackage
hot reload·ground query·정적/동적 충돌·host 핵심 7종 반복은 각각 **20/20** 통과했다.
WebSocket 회귀도 **100/100** 통과했다. server는 실제 vertex bbox/span을 출력하며, 그
시점 파일은 `495.880×495.880×22.019m`로 확인됐다.

같은 서버 PID에서 historical Wall/Broad snapshot을 현재 Landscape snapshot으로 바꾸는
runtime smoke도 통과했다. 로그는 후보 검증과 tick-boundary 적용을 차례로 남겼고 stderr는
비어 있었다. 이 결과는 자동 package 교체와 유한 경계 안전성을 검증한 것이며, 확대된
Landscape의 실제 PIE 조향·경사 주행과 authored wall collision은 아직 수동 확인 대상이다.

## 2026-08-28 현재 인계 상태

Unreal-owned 100cm `SIMGHF1` payload를 실제 Bake해 257,556 samples/256,542 cells와
checksum `fnv1a64:b8b0a6ccd89614de`를 확정했고, 같은 PID의 C++ host가 재시작 없이
검증·tick-boundary 적용하는 경로를 통과했다. 이후 source는 cell material/friction을
포함한 `SIMGHF2` writer와 v1 호환 reader로 확장됐으며, 다음 수동 Bake에서 실제 v2
payload와 노면별 체감을 확인한다.

최종 Windows Release build, CTest 14/14, 전체 suite 20회 280/280, UE 5.6 Editor/Game
build와 GeoTransform Automation 1/1이 통과했다. 최종 검증 서버 PID `6648`은
`127.0.0.1:9000`에만 실행 중이고, client Hello 전 WorldState 차단·invalid Hello 1008·
동일 sequence altered-payload 거부·정상 Reset/Control 복구 smoke를 통과했다. 현재
`static_colliders=0`이므로 marker Bake와 실제 PIE 경사·요철·차체 자세·벽/커브 충돌은
완료 선언 전 수동 gate로 남는다.
