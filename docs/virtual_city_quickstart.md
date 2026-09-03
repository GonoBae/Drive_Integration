# 가상 도심 코스 실행·검증

2026-08-31. Unreal 5.6의 Engine 기본 Cube와 프로젝트 자체 색상 material로 만든
**주행 검증용 blockout**이다. 차선·신호를 따르는 NPC 1대는 추가했지만 완성 도시 아트,
다중 NPC·보행자 또는 Ego 자동운전 기능으로 간주하지 않는다. 자세한 검증 결과는
[작업일지](./worklogs/2026-08-31.md)의 가상 도심 구현 절을 따른다.

## 실행

1. 기존 Landscape 서버가 실행 중이면 그 서버를 먼저 종료한다. 두 서버가 같은
   `9000` port를 동시에 사용할 수 없다. 새 launcher는 다른 프로세스를 강제 종료하지 않는다.
2. 저장소 루트의 PowerShell에서 실행한다.

   ```powershell
   .\scripts\run_virtual_city_server.ps1 -Background
   ```

   창에서 로그를 계속 보며 실행하려면 `-Background`를 빼고, 종료할 때 `Ctrl+C`를 누른다.
   백그라운드 실행은 출력된 PID를 확인해 `Stop-Process -Id <표시된 PID>`로 종료한다.
   Windows PowerShell에서 스크립트 실행 정책 오류가 나면 전역 정책을 바꾸지 말고,
   아래 실행 파일을 직접 실행한다. 절대 경로이므로 현재 폴더와 무관하며 종료는 `Ctrl+C`다.

   ```powershell
   & 'B:\Portfolio\Drive_Integration\cpp\host\build\Release\simcore_publisher.exe' --runtime-config 'B:\Portfolio\Drive_Integration\cpp\host\config\virtual_city_server.cfg'
   ```
3. Unreal을 실행하면 기본 시작 맵 `/Game/VirtualCity/Maps/L_VirtualCity`가 열린다.
   다른 맵이 열려 있다면 이 맵을 직접 연 뒤 Play한다. 기존 `NewMap` 파일은 보존했다.
   World Settings의 map-local GameMode는 `VirtualCityGameMode`다.
   일반 `ExternalVehiclePawn`을 추가 배치하거나 기존 `NewMap`의 exporter를 복사하지 않는다.
4. HUD의 Hello/map 승인과 `Active`를 확인한다. 차량은 남쪽 직선의 ENU `(0,0)`에서
   동쪽을 바라본다. `W` 전진, `S` 감속 후 후진, `A/D` 조향, `Space` rear side brake다.
5. Play 화면을 클릭한 뒤 **마우스로 시점 회전**, **휠로 줌**, **C로 뒤쪽 시점 복귀**한다.
   `Shift+F1`로 마우스를 해제한다. 8/31 후속으로 블록 차량을 자체 제작 세단 메시와
   도장·유리·고무·휠 재질로 교체했다. 이번 조향·카메라·외형 변경에는 Bake가 필요 없다.
   상세 변경과 확인 기준은 [차량 조작감·카메라·외형](./vehicle_driving_refinement.md)을 따른다.

`cpp/host/config/virtual_city_server.cfg`와 전용 Pawn이 동일한 `virtual_city_v1`을 사용한다.
시작 방향은 90°, local ENU metadata의 위경도는 0이다. 실제 지리 위치를 의미하지 않는다.
직접 실행하는 host의 무인자 기본값은 기존 `wall_broad_v1`이므로 새 코스에는 위 launcher를
사용하거나 `virtual_city_server.cfg`를 명시한다. 실행 중인 서버의 package가 다르면
Hello/checksum gate가 조작을 막는 것이 정상이다.

## 먼저 확인할 주행

- 직선: 출발·정지·저속 후진, 멈춘 바퀴가 계속 회전하지 않는지.
- 한 바퀴: 동쪽으로 출발해 왼쪽 코너를 따라 동측→북측→서측→남측으로 복귀한다.
  첫 검증은 저속으로 진행한다. 북쪽 T자 갈림길은 정차 공간으로 이어진다.
- 동측 경사: 약 4° 오르막→높이 약 1.40m의 평탄부→4° 내리막에서 차체 방향과
  네 바퀴 접지를 확인한다.
- 장애물: 연석·건물·주황색 외곽 벽에 저속으로 접근해 시각 메시와 충돌 위치를 확인한다.
  외곽 벽은 실제 코스 범위를 보여 주는 명시적 경계다.
- End PIE→Play: 원점·동쪽 방향으로 재시작하는지 확인한다.

### 연석 등판 수동 인수

현재 package는 연석 top과 보도를 50cm Ground support로 함께 Bake한다. 연석은 지면이면서
semantic `Curb` marker이고, 벽·Barrier와 같은 무조건 통과 대상이 아니다.
도로/보도를 잇는 authored 연석 marker 중심을 가로지르는 full-body footprint 214곳은
production swept near/far support 계약을 만족한다. 전체 along-length QA는 210/215 collider
전 길이와 북쪽 T-opening 네 끝단·BayEnd의 의도적 gap을 확인했다. raised support 누락
구간은 막힌다.
북쪽 정차 공간 끝의 `Curb_BayEnd` 중심은 뒤에 높은 보도가 없고 `Barrier_BayEnd`가 있으므로
등판 대상이 아니다.

1. 남쪽 직선 `Curb_South_1`의 **가운데 구간**에서 연석에 가능한 한 직각으로 정렬한다.
   endpoint/opening은 의도적으로 fail-closed할 수 있다. 매우 저속 coast 접근과 충분히
   가속한 접근을 분리하고 HUD의 `vehicle speed`와 대략적인 진입 각도를 기록한다. 자동
   회귀의 참고 접근 속도는 5.2252m/s(약 18.81km/h)지만 고정 속도 gate나 PIE 합격 보장은 아니다.
2. 충분한 접근에서는 앞바퀴와 차체가 먼저 올라가고 뒤축이 이어지는지 확인한다.
   한 frame 순간이동, 지면 관통 또는 보도 위에서의 지속 진동은 실패다.
3. 같은 방식으로 건물과 주황색 Barrier에 접근해 계속 막히는지 확인한다.
4. End PIE→Play 뒤 같은 위치·조건을 다시 확인한다.

실제 package 자동 회귀는 5.2252m/s 접근, near rise 0.1536m, 앞·뒤 차축 support 차이
0.2400m, 차체 상승 0.2119m, 한 step 최대 수직 이동 0.0232m, 최대 pitch 5.7943°와 최소
세 바퀴 접지를 기록했다. 이는 사용자 조작감 인수가 아니며 위 PIE 확인은 아직 대기다.

도로는 약 8m의 양방향 도로, 전체 측정 영역은 동서 240m×남북 200m다.
정확한 Cube 외곽에 쏜 ray 일부가 hit하지 않아 실제 유효 지면은 동서 239m×남북 200m다.
동서 양 끝 0.5m가 이 차이이며, 경계벽의 안쪽 면에서 먼저 차체가 막히도록 구성했다.
루프와 네 바퀴 경로 및 네 방향 외벽 접근 시 지면 유지도 별도 자동 검증한다.
중앙선·횡단보도는 현재 **표시용**이다. 8/31 후속으로 북측 T자 교차로에 서버 상태를
표시하는 신호등 3개와 방향 차선 25개를 추가했다. 후속으로 기본 spawn 20m 앞의 NPC 1대가
차선·신호를 따라 주행한다. [NPC 포함 일괄 테스트](./npc_lane_following.md)를 따른다.
보행자와 Ego 자동 신호 제동은 아직 없다. 신호 앞에서는 사용자가 직접 감속·정지한다.

## 신호등 확인

- 최신 서버와 Editor 빌드로 Play하고, 루프를 따라 북측 T자 교차로까지 이동한다.
- 신호등은 실행 중 생성되므로 편집 모드에는 보이지 않는다. 첫 Reset 뒤 2초 전부 적색,
  동서 녹색 12초/황색 3초, 전부 적색 2초, 분기 녹색 8초/황색 3초의 30초 주기다.
- HUD `signals:`와 신호 위 상태/초수를 확인한다. 프레임이 오래되거나 연결이 끊기면
  녹색을 유지하지 않으며, 새 Play는 신호 시간도 다시 시작한다.
- 기존 지도/지면을 수정할 필요가 없으므로 이번 신호 추가를 위해 Bake하지 않는다.
  신호의 기둥/외형은 비충돌 prototype이며 충돌 Bake에 포함되지 않는다.
- 차선 JSON 생성·검증·실행 중 교체와 현재 제한은 [차선·신호 문서](./traffic_network_signals.md)를 따른다.
- 편집 화면 떨림을 피하는 뷰포트 설정은 [TSR 진단 기록](./troubleshooting/ue-editor-temporal-aa-surface-flicker.md)을 따른다.

## 편집과 Bake

Outliner에서 `VirtualCity/Ground`의 `VirtualCityGround`에 속한 **390개** mesh
component만 지면 측정 대상으로 사용한다. 구성은 기존 기본 지면·도로 59개, 보도 116개,
연석 top 215개다. 건물은 Ground에 넣지 않는다. 연석은 Ground support와 별도 시각 actor,
`VirtualCity/SimCoreMarkers`의 semantic `Curb`를 함께 수정해야 한다.
현재 static proxy는 pitch/roll 없는 수직 OBB다. 경사 위 연석은 작은 수직 상자들로 나눴다.

`VirtualCityGroundExporter`의 **Bake Ground + Static Collision To MapPackage**를 누른다.
출력은 `map_packages/virtual_city_v1`이며 `NewMap`/`landscape_local_v1`은 바뀌지 않는다.
이 package는 **50cm sample 간격**을 유지한다. 옛 1m+absolute marker-top 후보 계약은
경사·코너에 민감했고, 현재는 50cm+signed-delta 계약이다. 옛 bytes를 최종 계약으로
재감사하지 않았으므로 개선분을 spacing 하나에만 귀속하지 않는다. 얇은 벽을 heightfield만으로 표현하지 않는다.
내용이 바뀐 정상 Bake는 기존 hot reload 경로로 서버에 반영된다. 동일 데이터 재Bake는
checksum이 같아 reload하지 않는 것이 정상이다. 새 코스의 실제 PIE 변경→재Bake→조작 복구는
수동 확인 항목이다.

Ground Bake로 collision checksum이 바뀌면 기존 traffic JSON의 `source_map_checksum`도
명시적으로 갱신한다. 일반 Ground Bake가 traffic sidecar를 자동 변경하지는 않는다.

```powershell
& 'B:/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'B:/Portfolio/Drive_Integration/unreal/DriveIntegration/DriveIntegration.uproject' `
  -run=ExportVirtualCityTraffic -UpdateExisting -nowrite -unattended -nop4 -NullRHI -NoSound -NoSplash
```

현재 collision checksum은 `fnv1a64:942842ea8d76b7a2`, traffic network checksum은
`fnv1a64:32f81819cb6b2b0f`이다. C++ 물리 코드가 바뀐 이번 적용에는 실행 중인 이전 서버
바이너리를 한 번 재시작해야 한다. 같은 새 바이너리에서 이후 Ground만 Bake할 때는 서버
process 재시작 없이 MapPackage hot reload를 사용한다.

## 자동 검증·재생성

```powershell
ctest --test-dir cpp/host/build -C Release --output-on-failure
.\cpp\host\build\Release\virtual_city_course_tests.exe
python scripts/smoke_health.py --map-package map_packages/virtual_city_v1
```

`virtual_city_course_tests`는 저장된 실제 package의 checksum, 재질, 경사, 네 바퀴 경로,
정지/후진/reset·벽 충돌과 자체 물리의 저속 한 바퀴를 검사한다. 시험용 steering/throttle
입력만 주며 차량 pose를 매 tick 덮어쓰지 않는다. `drive_route.csv`는 이 시험의 checkpoint
목록으로, collision checksum에 포함되지 않으며 제품 LaneGraph·NPC AI가 아니다.

Editor target을 빌드하면 `BuildVirtualCity` commandlet을 사용할 수 있다. 저장소 루트,
이 PC의 UE 설치 경로 기준:

```powershell
& 'B:/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'B:/Portfolio/Drive_Integration/unreal/DriveIntegration/DriveIntegration.uproject' `
  -run=BuildVirtualCity -ValidateOnly -unattended -NullRHI -NoSound -NoSplash
```

- `-ValidateOnly`: 저장된 생성 맵의 geometry·지면 collision·manifest를 검증한다.
  생성 원본과 배치가 달라진 사용자 편집 맵에는 실패할 수 있다.
- `-BakeOnly`: 저장된 현재 맵을 열어 기존 exporter로 다시 Bake한다. geometry를 재생성하지 않는다.
  실행 전 Editor에서 작업을 저장하고 종료한다.
- `-SyncGeneratedGround`: 생성 원본과 일치하는 기존 연석·보도 actor만
  `VirtualCityGround` component로 이행하고 저장·Bake한다. 다른 사용자 편집 geometry를
  일반 재생성하거나 덮어쓰는 모드가 아니다.
- 둘 다 생략: **대상 맵이 없을 때만** 새 맵·재질·package를 생성한다. 기존 맵을 덮어쓰는
  `-Replace`는 지원하지 않는다. 기존 사용자 변경을 삭제해 재생성하지 않는다.

NullRHI·자동 물리 시험은 실제 화면 품질·입력 감각·1080p 성능·30분 안정성의 합격 증거가 아니다.

### 실제 렌더 캡처 도구

Editor 빌드의 `VirtualCity.CaptureAfter 10`은 정상 게임 틱 10초와 셰이더 완료를
기다린 뒤 `Saved/Screenshots/VirtualCity-*.png`에 화면을 저장한다. `-game`에서는
Editor 모듈을 먼저 로드해야 하므로 `-ExecCmds="Module Load DriveIntegrationEditor,VirtualCity.CaptureAfter 10"`
형식을 사용한다. `-RenderOffScreen -NoRemoteShaderCompile -seconds=75`와 함께 검증했다.
이는 에디터 전용 QA 명령이며 출시 게임에 캡처 동작을 추가하지 않는다.
초기 `HighResShot`의 render-only 대기는 셰이더 결과를 반영하는 정상 게임 틱 대체가 아니다.
