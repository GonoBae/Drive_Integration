# 가상 도심 연석을 오르지 못한 문제 — Ground support와 Curb collision 분리

## 증상

`L_VirtualCity`에서 속도를 높여 연석에 접근해도 차량이 연석 측면에서 막혔다. 벽처럼
막히는 동작 자체는 static collision 결과였지만, 실제 승용차처럼 앞바퀴가 턱을 타고
차체와 뒤축이 이어서 올라가는 경로가 없었다.

## 원인

첫 blockout의 `VirtualCityGround`는 기본 지면과 도로 **59개 component**만 포함했다.
높이 0.24m·폭 0.30m의 연석 215개는 `Curb` static OBB였고, 보도 116개는 시각
geometry였다. C++의 planar 차체 proxy는 연석 OBB와 충돌했지만 휠 `GroundQuery`에는
도로에서 연석 top과 보도로 이어지는 높은 support가 없었다. 따라서 더 큰 속도만으로는
서스펜션과 차체가 올라갈 수 없었다.

첫 Ground 이행 뒤에는 연석 top이 0.24m인데 보도 support가 0.16m여서, 차량이 턱을
넘은 뒤 바퀴 중심이 8cm 낮은 보도에 정착했다. 타이어 옆면은 계속 연석 mesh와 겹쳐
화면에서 바퀴가 연석에 파묻힌 것처럼 보였다. 이는 표시 메시 문제가 아니라 저장 맵의
가시 표면과 물리 지지면 높이가 서로 달랐던 저작 오류였다.

## 채택하지 않은 수정

- 속도 임계값을 넘으면 차체 Z·속도·충격량을 직접 더하지 않는다. 이는 접근 각도와
  tick rate에 따라 순간이동을 만들고 기존 서스펜션 권한을 우회한다.
- 모든 `Curb`를 전역적으로 무시하지 않는다. Ground support가 없거나 너무 높은 턱도
  관통할 수 있기 때문이다.
- `Wall`과 `Barrier`를 연석 예외에 포함하지 않는다.

## 해결

### Unreal 저작과 Bake

연석 top 215개와 보도 116개를 `VirtualCityGround` 측정 대상으로 이행했다. 현재 Ground는
**390개 = 기존 59 + 보도 116 + 연석 215**다. 연석은 다음 두 역할을 동시에 가진다.

1. 휠 ray가 읽는 연속 Ground support
2. 장애물 의미와 높이를 검증하는 `Curb` static marker

총 도형 594개와 static collider 228개는 유지한다. 저장 맵은 전체 재생성으로 덮어쓰지
않고 다음 전용 이행을 사용한다.

```powershell
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -run=BuildVirtualCity -SyncGeneratedGround -unattended -nop4 -NullRHI -NoSound -NoSplash
```

옛 생성 맵의 16cm 보도는 아래 제한된 migration으로 8cm 올려 연석 상단 24cm와
정렬한다. 명령은 116개 보도가 정확히 legacy 또는 canonical transform인지 모두 먼저
검사하며, 수동 편집된 component가 있으면 아무것도 바꾸지 않는다. 저장·Bake·재로드 후
엄격 검증까지 한 작업으로 수행한다.

```powershell
${env:UE-LocalDataCachePath}='B:\Portfolio\Drive_Integration\runtime_cache\ue-ddc'
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -run=BuildVirtualCity -SyncSidewalkCurbHeight -unattended -nop4 -NullRHI -NoSound -NoSplash `
  -DDC=InstalledNoZenLocalFallback
```

Ground Bake로 collision checksum이 바뀌면 traffic의 source map identity도 명시적으로
갱신한다. 이 명령은 맵·Ground package·static collision을 다시 저장하지 않고 기존 traffic
JSON만 canonical 임시 파일을 거쳐 교체한다.

```powershell
& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -run=ExportVirtualCityTraffic -UpdateExisting -nowrite -unattended -nop4 -NullRHI -NoSound -NoSplash
```

처음 1m 간격 Bake에 당시의 **absolute marker-top** full-footprint 후보 계약을 적용하면
경사 동쪽 연석과 코너 안쪽의 raster/절대 높이 민감성 때문에 164/215만 승인됐다. 이후
실제 저작 지형은 유지한 채 가상 도심 package를 50cm로 다시 Bake하고 계약도 위의 signed
near/far delta로 바꿨다. 최종 조합의 marker 중심 full-footprint는 214/215다. 덮어쓴 옛
1m bytes에 최종 signed-delta 계약을 다시 적용한 audit은 없으므로, 개선분을 해상도와 계약
각각에 따로 귀속하지 않는다. exporter의 일반 기본값 100cm를 전역 변경한 것도 아니다.

### C++ collision과 차량 물리

서버는 current→predicted pose의 swept body footprint와 겹칠 수 있는 static object 가운데
semantic이 정확히 `Curb`인 항목만 후보로 삼는다. current CG 거리만 보면 60Hz 한 tick에
연석을 가로지르는 고속 접근을 놓칠 수 있으므로 시작→예측 끝 구간과 회전 projection까지
보수적으로 포함한다.

유한 연석을 실제로 가로지르는 swept footprint의 along 구간 전체를 타이어 반지름 `R` 이하
간격으로 나눈다. 각 위치에서 marker 양쪽의 near/far **네 지점**을 `GroundQuery`로 측정한다.

- near pair: `curb half-width + 1R`
- far pair: `curb half-width + 3R`
- 승인 조건: 네 support가 모두 유효하고 `abs(near delta)`가 최소 0.02m, collider와 far
  plateau는 `0.75R` 이하이며 같은 쪽으로 상승해야 한다. 50cm cell 대각선에서 road-side
  near sample이 조금 낮아질 수 있으므로 near 상한에만 최대 `0.30R`의 제한된 raster
  허용치를 더하고, `abs(collider height - abs(far delta)) <= 0.30R`로 실제 plateau 높이를
  별도 검증한다.

한 sample이라도 실패하면 그 위치의 `Curb`는 planar collision에서 제외하지 않는다.
`Wall`/`Barrier`, 높은 턱, endpoint·교차로 opening의 raised support 누락은 fail-closed한다.
near만 사용하면 heightfield 경계 보간을 실제 턱으로 오인할 수 있고 far만 사용하면 연석
바로 옆의 rise를 놓칠 수 있어 네 지점 band를 함께 쓴다. 절대 marker top 높이를 far 한
점과 맞추지 않고 near/far 높이차를 비교하므로 완만한 경사 위 연석도 같은 상대 계약을 쓴다.

예외가 승인돼도 휠 ray는 도로→연석 top→보도 support를 계속 읽는다. 네 독립
spring/damper와 hard-stop 반력, 타이어 힘, heave/pitch가 앞축과 뒤축을 순서대로 올린다.
차체 Z·yaw·속도나 충격량을 직접 주입하지 않는다. 이 때문에 “빠르면 오른다”는 결과는
별도 속도 분기가 아니라 충분한 운동 상태와 구동력이 기존 물리를 통해 만든 결과다.

## 검증 결과

| 항목 | 결과 |
|---|---:|
| collision / traffic checksum | `fnv1a64:942842ea8d76b7a2` / `fnv1a64:32f81819cb6b2b0f` |
| heightfield | 50cm, 401×481 / 192,881 samples / 192,079 valid samples |
| cell / missing | 191,200 valid·drivable / 800 missing |
| cell 재질 | asphalt 21,510 / rough 169,690 |
| 실제 package 접근 속도 / near rise | 5.2252m/s / 0.1536m |
| 앞·뒤 차축 support 차이 | 0.2400m |
| 관측 차체 상승 / 최대 step 상승 | 0.2119m / 0.0232m |
| 최대 pitch / 최소 접지 | 5.7943° / 3개 |
| authored Curb 중심 full-footprint probe | marker 중심 215곳 중 도로/보도 연석 중심 214곳 eligible |

Synthetic 0.24m 회귀는 300N 설정에서 실제 ramp까지 도달한 뒤 20초 시점 차체
N=3.5096m·앞축 N=4.8596m·속도 0.0555m/s로 stall했고, 6,000N 설정에서는 앞축→뒤축
순으로 통과했다. 60/120/240Hz crossing time 차이는 0.12초 이내다. 30/50m/s
`swept-curb-stress`는 고속 tick의 후보 누락·tunneling·velocity erasure를 막는 gate 계약
시험이다. support가 있는 Curb의 첫 swept tick은 각각
Δz 0.1153/0.1901m, pitch 약 5.2°, 접지 4개, 최종 Z 0.7888m였고 같은 tick의 0.30m 턱과
0.24m `Wall`은 막혔다. 이 stress 수치를 180km/h 실차 승차감이나 부드러운 연석 등판의
근거로 사용하지 않는다. CTest 18/18, UE Automation 35/35, Game/Editor Development 빌드와
맵·traffic 두 `ValidateOnly`가 통과했다.

위 214/215는 50cm heightfield에서 각 collider **중심을 가로지르는 full vehicle footprint**
검사이며 연석 전체 길이의 모든 접근 위치를 보장하지 않는다. endpoint나 교차로
opening처럼 raised support가 없는 구간은 fail-closed로
막힌다. 남은 1개 `Curb_BayEnd` 중심은 signed near/far delta의 지속 raised-support 계약을
만족하지 않고 바로 뒤의 `Barrier_BayEnd`가 정차 공간 끝을 막는다. 의도적으로
ineligible이며 고속으로도 통과해야
하는 대상이 아니다. “연석 215개 모두 등판 가능”을 완료 기준으로 사용하지 않는다.

전체 along-length QA에서는 215개 중 210개 collider가 전 길이 eligible했다. 전구간이
차단되는 것은 `Curb_BayEnd` `[-5,5]` 하나다. 북쪽 T-opening의 네 collider는 opening 쪽
끝단만 차단된다: `Curb_North_-1_-1` `[+33.72,35]`, `Curb_North_-1_1`
`[-35,-33.72]`, `Curb_North_1_-1` `[+34.04,35]`, `Curb_North_1_1`
`[-35,-33.72]`. East grade·corner·South 연석은 전구간이 eligible했다. 따라서 opening
끝단에서 막히는 것은 support 없는 구간을 marker 중심 결과로 관통시키지 않는 정상 동작이다.
영구 회귀는 네 opening 쪽 endpoint가 fail-closed하고 각각의 반대 endpoint·중심은 pass하는
조건과 South uniform 위치를 고정한다. 전체 215개 sweep은 one-off package QA 근거다.

자동 검증은 실제 사용자 PIE 감각을 대신하지 않는다. 진입 속도·각도, 화면의 차체/휠
순차 상승, 보도 위 안정과 벽·Barrier 비관통은 수동 인수 대기다.

## PIE 확인과 재진단

1. 최신 C++ 바이너리로 가상 도심 서버를 실행하고 `L_VirtualCity`를 Play한다. C++ 코드가
   바뀐 이번 한 번은 이전 바이너리 프로세스를 재시작해야 한다. 같은 바이너리에서 이후
   Ground만 다시 Bake할 때는 기존 same-process MapPackage hot reload 경로를 사용한다.
2. HUD의 Hello/map 승인과 `Active`를 확인한다. map mismatch는 조작을 막는 정상 안전
   동작이다. traffic mismatch는 신호를 적색으로 유지하지만 수동운전을 영구 차단하지 않는다.
3. 남쪽 직선 `Curb_South_1`의 가운데 구간에서 가능한 한 직각으로 접근한다. 매우 저속
   coast와 충분히 가속한 접근을 분리하고 HUD의 실제 `vehicle speed`와 진입 각도를 기록한다.
4. 충분한 접근에서는 앞축과 차체가 먼저 올라가고 뒤축이 이어지며, 한 frame 순간이동이나
   지면 관통 없이 보도 위에서 안정되는지 확인한다.
5. 같은 조건으로 건물 벽과 주황색 Barrier가 통과되지 않는지 확인한다.
   북쪽 정차 공간 끝의 `Curb_BayEnd`/`Barrier_BayEnd`도 등판 대상이 아니라 경계로 막혀야 한다.

여전히 오르지 못하면 순서대로 Ground 390개, 50cm/401×481 heightfield, collision checksum
`fnv1a64:942842ea8d76b7a2`, traffic checksum `fnv1a64:32f81819cb6b2b0f`, HUD map 승인,
실행 중 서버 바이너리의 갱신 여부를 확인한다. 수동으로 manifest checksum만 고치거나
traffic 파일을 삭제해 우회하지 않는다.
