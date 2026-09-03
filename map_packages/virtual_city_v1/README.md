# virtual_city_v1 — 가상 도심 blockout

2026-08-31 Unreal 5.6의 실제 저장 맵
`/Game/VirtualCity/Maps/L_VirtualCity`에서 측정한 충돌 package다.
충돌 package와 별도로 방향성 lane 25개·신호 head 3개의 `traffic_network.json`을
추가했다. 완성 도시 아트·NPC/보행자·자율주행 기능은 포함하지 않는다.
실행과 편집은 [가상 도심 빠른 시작](../../docs/virtual_city_quickstart.md)을 따른다.

## 제작 원본과 좌표

- 원본: `unreal/DriveIntegration/Source/DriveIntegration/SimCoreVirtualCityLayout.h/.cpp`.
- 변환: editor-only `BuildVirtualCity` commandlet이 Engine 기본 Cube와 저장형 자체
  색상 재질 10개로 도형 594개를 생성한다. 기본 지면·도로 59개, 보도 116개와
  연석 top 215개, 총 **390개 component**를 `VirtualCityGround` 한 actor에 모은다.
  건물·외곽 벽은 별도 geometry이고, 연석은 Ground support와 semantic `Curb` marker를
  함께 가진다. 전체 `SimCoreStaticCollider`는 228개다.
- 지면은 Unreal production `GroundCollisionExporter`가 실제 WorldStatic collision을
  측정한다. C++가 시각 메시에서 독립적으로 지면을 추정하거나 생성하지 않는다.
- `map_enu`는 `(East,North,Up)` meter, Unreal은 `(North,East,Up)×100` centimeter다.
  local origin은 Unreal `(0,0,0)`이고 차량 spawn ENU `(0,0)`, heading 90°(동쪽)다.
  실제 지리 위치를 재현하지 않는다.
- 폭 8m 양방향 도로, 2.5m 보도, 완만한 코너, T자 갈림길·횡단보도·정지선·정차 공간,
  약 4° 오르내리막과 약 1.40m 평탄부를 가진 prototype art다.

## 실제 측정값

| 항목 | 생성·검증값 |
|---|---|
| format | manifest v1, `SIMGHF2` compact heightfield |
| spacing / lattice | 50cm / columns 401 × rows 481 |
| samples | 총 192,881 / valid 192,079 |
| cells | valid·drivable 191,200 / missing 800 |
| cell 재질 | asphalt 21,510 / rough 169,690 |
| nominal 영역 | E `[-120,120]`, N `[-20,180]`m = 240×200m |
| valid ground 영역 | E `[-119.5,119.5]`, N `[-20,180]`m = **239×200m** |
| valid 높이 | 약 U `[-0.08,1.64]`m |
| static colliders | 228 upright OBB prisms |
| collision checksum | `fnv1a64:942842ea8d76b7a2` |

cube 끝면의 수직 ray hit 누락 때문에 동·서 각 0.5m의 마지막 구간은 valid 지면으로
보장하지 않는다. missing cell은 800개다. 코스는 E 약 ±100m 이내이며 외곽 벽은
E ±119m 중심/안쪽 면 ±118.75m라 주행 영역을 valid 지면 안쪽에 둔다. 명목 크기를
전체 drivable 크기와 혼동하지 않는다.

`manifest.cfg`는 `ground_surface.csv`(header-only sentinel), `ground_heightfield.bin`,
`static_colliders.csv`의 실제 bytes checksum을 선언한다. 내용을 바꿨다면 checksum만
수정하지 말고 exporter로 다시 Bake한다. manifest를 마지막에 commit하고 서버는
검증된 새 snapshot만 고정 tick 경계에 적용한다.

## QA route와 검증 경계

`traffic_network.json`은 별도 저작한 lane 폭·속도·successor·정지선 group과 head
pose를 가진다. 현재 collision checksum에 결합되며 자체 network checksum은
`fnv1a64:32f81819cb6b2b0f`이다. collision manifest에는 포함하지 않는다.
C++는 실제 지면과 topology를 검증한 뒤 30초 신호 주기를 계산하고 Unreal은
수신 결과만 표시한다. [구조·최초 export·hot reload·제한](../../docs/traffic_network_signals.md)을 따른다.

일반 Ground Bake는 traffic JSON을 자동 갱신하지 않는다. collision checksum이
달라지면 기존 head는 안전하게 적색이 되지만 수동운전 자체를 차단하지 않는다.
도로와 topology를 확인한 뒤 `ExportVirtualCityTraffic -UpdateExisting -nowrite`를
명시적으로 실행하면 기존 파일을 보존 교체 방식으로 canonical JSON에 갱신한다.

`drive_route.csv`의 header는 정확히 다음과 같다.

```csv
east_m,north_m,up_m,heading_deg
```

364개 checkpoint가 약 633.1m 루프를 따라 처음과 끝 ENU `(0,0,0)`, heading 90°로
연결된다. 이 파일은 자동 QA 입력일 뿐이며 collision manifest/checksum에 포함하지 않는다.
LaneGraph, NPC AI, 통행 제한·신호·제품 navigation 계약이 아니다.

저장 맵 재로드 검증은 도형 transform과 marker ID·pose·extent·semantic·활성 상태,
route의 asphalt·높이·경사를 확인했다. C++ dense route/네 바퀴 coverage 및 pose를
덮어쓰지 않는 실제 조향·가속 입력 기반 한 바퀴의 선행 시험이 통과했다.
최신 UE Automation 35/35와 Editor/Game 빌드도 통과했다. 실제 package 연석 회귀는
5.2252m/s 접근, near rise 0.1536m, 차축 support 차이 0.2400m, 차체 상승 0.2119m,
최대 step 상승 0.0232m, 최대 pitch 5.7943°와 최소 세 바퀴 접지를
확인했다. 최종 회귀 재실행 결과·알려진 문제는
[9/2 작업일지](../../docs/worklogs/2026-09-02.md)를 기준으로 확인한다.

50cm heightfield에서 semantic Curb marker 중심 215곳을 가로지르는 production full-body
footprint를 검사하면 도로/보도 연석 중심 214곳이 near(+1R)/far(+3R) 후보 조건을 만족한다.
옛 1m Bake에 당시 absolute marker-top 후보 계약을 적용한 결과는 164/215였고, 이후 50cm
재Bake와 signed-delta 계약을 함께 적용했다. 최종 계약을 옛 1m bytes에 재감사하지 않았으므로
개선분을 해상도 하나에만 귀속하지 않는다. 이 수치는 marker
중심 검사이며 연석 전체 길이를 보장하지 않는다. runtime은 current→predicted swept
footprint의 along 구간을 `R` 이하 간격으로 검사하므로 endpoint/opening의 raised support
누락 구간은 fail-closed한다. `Curb_BayEnd` 중심은 뒤 support가 없고
`Barrier_BayEnd`가 정차 공간 끝을 막으므로 의도적으로 후보가 아니다.

50cm 전체 along-length QA에서는 215개 중 210개 collider가 전 길이 eligible했다.
`Curb_BayEnd`는 전구간 `[-5,5]`이 차단된다. 북쪽 T-opening의 네 collider는 opening 쪽
끝단만 차단된다: `Curb_North_-1_-1` `[+33.72,35]`, `Curb_North_-1_1`
`[-35,-33.72]`, `Curb_North_1_-1` `[+34.04,35]`, `Curb_North_1_1`
`[-35,-33.72]`. East grade·corner·South 연석은 전구간이 eligible했다. 영구 회귀는 네
opening 쪽 endpoint의 fail-closed와 반대 endpoint·중심의 pass, South uniform 위치를
고정한다. 전체 215개 sweep은 one-off package QA 근거다.

실제 PIE 조작감, 화면의 차체·휠 접지와 벽 비관통, 변경 재Bake→자동 재연결, 목표 PC
1080p 성능과 30분 안정성은 별도 인수 항목이다. 자동 시험 성공만으로 완료 처리하지 않는다.

## 수정과 보존

- `-run=BuildVirtualCity -ValidateOnly`: 저장 맵과 현재 생성 원본·manifest 검증.
- `-run=BuildVirtualCity -BakeOnly`: 저장한 현재 맵의 geometry를 재생성하지 않고 Bake.
- `-run=BuildVirtualCity -SyncGeneratedGround`: 생성 원본과 일치하는 기존 연석·보도만
  `VirtualCityGround`로 이행하고 Bake하는 제한된 migration.
- `-run=BuildVirtualCity -SyncSidewalkCurbHeight`: 정확히 옛 생성 상태인 16cm 보도만
  8cm 올려 연석 상단 24cm와 맞추고 다시 Bake한다. 편집된 component가 하나라도 있으면
  변경 전에 전체 작업을 거부하며 이미 정렬된 맵에는 멱등이다.
- `-run=ExportVirtualCityTraffic -UpdateExisting -nowrite`: 검증된 현재 collision checksum에
  맞춘 canonical traffic JSON만 명시적으로 보존 교체.
- 옵션 없는 생성은 대상 맵이 없을 때만 가능하다. `-Replace`는 지원하지 않는다.
- `NewMap`, `landscape_local_v1`, `wall_broad_v1`은 비교·회귀 자료로 보존한다.
- 외부 도시 팩을 구매·다운로드하지 않았다. Engine 참조와 자체 생성 재질/배치의
  출처·배포 검토는 [에셋 등록표](../../docs/assets/README.md)를 따른다.
