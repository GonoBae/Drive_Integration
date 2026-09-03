# ADR-012: Unreal이 지면을 측정하고 SimCore가 차량 물리만 계산한다

## 상태

- 결정: 채택
- 결정일: 2026-08-28
- 구현 상태: `SIMGHF2` material/friction end-to-end, `SIMGHF1`·legacy triangle 호환과 runtime hot reload 구현. 가상 도심은 연석·보도 Ground support 390개와 static marker 228개를 실제 Bake하고 보도 지지면을 연석 상단 24cm와 정렬했다. CTest 18/18·UE Automation 35/35·Game/Editor 빌드·저장 맵/traffic 검증을 통과했으며 연석 등판을 포함한 사용자 PIE 지형·충돌 인수는 후속
- 적용 범위: Unreal `GroundCollisionExporter`, MapPackage collision payload, C++ `GroundQuery`, 차량 물리와 runtime hot reload

## 배경

Unreal만이 현재 레벨의 Landscape, WorldStatic collision, actor transform과 충돌 법선을 직접
알 수 있다. 반대로 C++ SimCore는 차량 질량·서스펜션·타이어 힘·차체 자세와 충돌 응답의
권한을 가진다. 서버가 Unreal scene을 추정하거나 Unreal이 차량 pose를 다시 계산하면 같은
상태에 두 권한이 생겨 재현성과 디버깅 가능성이 낮아진다.

기존 경계도 측정 자체는 Unreal에서 수행했지만, 결과를 전역 삼각형 CSV로만 저장했다.
약 500m × 500m Landscape를 20,000 triangle 제한에 맞추면서 sample spacing이 507cm까지
커졌고, 급경사·요철·wheel contact에 필요한 형상이 사라졌다. 이는 책임 위치보다 교환
표현의 해상도와 결합 문제다.

## 결정

### 1. 책임 경계

| 컴포넌트 | 소유 책임 | 소유하지 않는 책임 |
|---|---|---|
| Unreal Editor exporter | 현재 scene collision을 vertical trace하여 지면 높이·충돌 법선·유효 cell·surface material/friction·정적 marker를 측정하고 immutable MapPackage snapshot으로 commit | 차량 질량, suspension/tire force, body pose, runtime collision response |
| C++ MapPackage loader·`GroundQuery` | payload를 엄격히 검증하고 ENU 수직 query에 높이·법선·material ID·friction multiplier를 반환 | Unreal scene 재측정, actor/mesh 탐색 |
| C++ vehicle physics | wheel contact, suspension, tire force, 차체 heave·pitch·roll·yaw와 collision response 계산 | Landscape authoring과 표시 pose 보정 |
| Unreal runtime Pawn | authoritative `WorldState` 표시와 입력 전송 | Chaos로 Ego pose 재계산 |

측정은 Bake 시점의 editor 작업이다. 60Hz 물리 loop는 immutable snapshot만 조회하므로
render frame trace나 네트워크 왕복 지연이 차량 접촉 계산에 들어오지 않는다.

### 1.1 연석의 지면 support와 장애물 의미

연석은 단순 벽도, 단순 장식 지면도 아니다. Unreal 저작 원본은 연석 top과 그 뒤 보도를
`VirtualCityGround`에 포함해 휠이 읽을 support를 측정하고, 같은 연석의 높이·범위·의미를
semantic `Curb` static marker로 함께 Bake한다. 이중 역할은 `Curb`에만 허용하며 건물·
`Wall`·`Barrier`를 Ground support 예외로 바꾸지 않는다.

C++ vehicle physics는 current→predicted swept body footprint가 닿는 `Curb` along 구간을
`R` 이하 간격으로 나눈다. 각 위치의 half-width+1R near pair와 half-width+3R far pair를
모두 측정하고, `abs(near delta)`가 `[0.02m, 0.75R]`, far delta가 near와 같은 부호,
`abs(collider height - abs(far delta)) <= 0.30R`, collider 높이가 `0.75R` 이하일 때만 해당
step의 planar 차체 collision에서 그 Curb ID를 제외한다. 휠 ray와 네
suspension corner는 계속 measured support를 읽어 heave·pitch와 tire force를 계산한다.
차체 Z·yaw·속도나 충격량을 직접 주입하지 않는다. 존재하지 않는 ID, 다른 semantic,
support 누락과 더 높은 턱은 fail-closed한다.

따라서 Unreal은 여전히 scene 형상을 측정하고 C++는 여전히 최종 차량 물리를 계산한다.
연석 보완은 이 책임 경계를 바꾼 것이 아니라 두 데이터 역할을 명시한 것이다.

### 2. compact heightfield payload

새 Bake는 MapPackage v1 manifest를 유지하면서 다음 collision payload를 선언한다.

```text
ground_surface.csv       # legacy host가 조용히 거친 지면으로 fallback하지 않도록 header-only sentinel
ground_heightfield.bin   # SIMGHF2 compact lattice: 높이, UE 측정 법선, sample/cell valid flags, material/friction
static_colliders.csv
```

`ground_heightfield.bin`은 padding에 의존하는 C++ struct dump가 아니라 명시적 little-endian
field로 직렬화한다. v2는 80-byte header, 20-byte sample, 12-byte cell record를 사용한다.
yaw가 있는 sampling box도 보존하도록 ENU origin과 column/row step vector를 기록한다.
sample은 height와 normalized normal을, cell은 valid flag, stable material ID와 terrain
friction multiplier를 기록한다. 서버는 basis inverse와 고정 diagonal로 해당 cell을 O(1)에
조회하고 측정 normal을 보간한다.

stable material ID는 append-only `default/asphalt/low-friction/rough`다. exporter는 Project
Settings의 Physical Surface 매핑을 읽고 `SimCore.Surface.*` actor/component tag가 있으면
이를 우선한다. 한 대상에 여러 material tag가 있거나 surface mapping이 모호하면 Bake를
fail-closed한다. 네 corner가 다른 재질인 경계 cell은 friction multiplier가 가장 낮은
측정을 선택하고, 동률이면 작은 stable ID를 택해 존재하지 않는 접지력을 만들지 않는다.
C++ 차량 물리는 tire friction × terrain multiplier × vehicle profile surface scale로
접지 한도를 계산한다. scale은 차량 설정 v5에 도입되었으며, 속도 조향 cap을 제거한
현재 v7에서도 유지된다. v7의 앞/뒤 타이어 강성 분리는 서버 물리 설정이며,
지면 측정·서버 물리의 책임 경계는 변경하지 않는다.

기본 측정 간격은 100cm이며 최대 2,000,000 sample을 허용한다. 예산을 넘는 경우 exporter는
몰래 5m 수준으로 저하하지 않고 필요한 설정을 명시적으로 오류로 알린다.

### 3. 호환과 실패 정책

- manifest format과 전체 collision checksum은 v1 규약을 그대로 사용한다.
- `ground_heightfield.bin`이 선언되면 새 host는 binary provider를 선택하고 legacy triangle
  CSV를 물리 입력으로 사용하지 않는다.
- `SIMGHF2`는 12-byte cell의 material ID와 finite `[0.05, 4]` multiplier를 strict 검증한다.
  `SIMGHF1`의 4-byte cell은 default material과 1.0 multiplier로 해석해 기존 Bake를 그대로
  호환한다.
- binary가 선언되지 않은 기존 package는 기존 `ground_surface.csv` triangle provider로
  계속 읽는다.
- header-only sentinel은 구 host에서 empty ground로 거부된다. 따라서 새 package를 거친
  triangle로 잘못 실행하지 않고 fail-closed한다.
- magic/version/stride/dimension/size/finite value/basis/cell flag가 잘못되거나 checksum이
  다르면 후보 전체를 거부하고 현재 active snapshot을 유지한다.

### 4. commit과 runtime 교체

Exporter는 binary와 두 CSV의 temporary file을 모두 완성한 뒤 payload를 교체하고
`manifest.cfg`를 마지막 commit marker로 바꾼다. C++ host는 manifest 변경을 감지하면
worker에서 ground·static collision·최종 checksum 전체를 검증하고, 다음 60Hz tick
경계에서 하나의 snapshot으로 교체한다. Bake 때문에 서버 process를 재시작하지 않는다.

### 5. 실제 적용 증거

2026-08-28 UE 5.6 Editor의 실제 `landscape_local_v1` Bake는 다음 v1 payload를 만들었다.

| 항목 | 실제 값 |
|---|---|
| magic/version | `SIMGHF1` / 1 |
| lattice | 507 columns × 508 rows |
| sample/cell | 257,556 samples / 256,542 cells |
| step | column·row vector 길이 각각 1.0m(100cm) |
| payload 크기 | 6,177,368 bytes |
| collision checksum | `fnv1a64:b8b0a6ccd89614de` |

기존 legacy package로 시작한 서버 PID `6692`는 재시작 없이 후보를 완전 검증한 뒤 60Hz tick
경계에서 위 checksum으로 교체했다. 로그의 `verified candidate`와 `applied at tick boundary`는
동일한 257,556 samples/256,542 cells를 기록했다. 이 결과로 당시 v1 Unreal writer, manifest-last
commit, C++ strict loader와 same-PID swap의 실제 교차 경계는 검증됐다.

v2 writer→strict loader→contact material→tire friction 경로는 automated fixture와 CTest
14/14에서 통과했다. 다만 위 실제 package는 v2 도입 전 Bake한 `SIMGHF1`이다. 따라서 이
결과는 책임 분리와 v1 payload 교체의 실제 증거이며 v2 Physical Material/tag의 실제
Landscape Bake나 차량의 사용자 환경 물리 완료 판정은 아니다. `static_colliders.csv`는
아직 marker 0개이고, v2 재Bake·PIE 경사·요철·차체 자세와 Sculpt 재Bake 후 Unreal
reconnect/Reset 표시는 별도 수동 gate로 남는다.

2026-09-01에는 `virtual_city_v1`의 기존 Ground 59개에 보도 116개와 연석 top 215개를
더해 총 390개를 50cm `SIMGHF2`로 Bake했다. 401×481의 총 192,881 samples 중 192,079가
valid이고 valid·drivable cell은 191,200개, missing cell은 800개다. rough/asphalt는
169,690/21,510 cells다. static collider 228개를 유지하며 collision checksum은
`fnv1a64:942842ea8d76b7a2`, 갱신한 traffic network checksum은
`fnv1a64:32f81819cb6b2b0f`이다. 9/2에는 옛 생성 보도의 16cm support를 연석 top과 같은
24cm로 제한 이행해 타이어가 8cm 낮은 표면에 정착하던 시각·물리 불일치를 제거했다.

실제 package 연석 회귀는 5.2252m/s 접근에서 near rise 0.1536m, 앞·뒤 차축 support 차이
0.2400m, 관측 차체 상승 0.2119m, 한 step 최대 수직 이동 0.0232m, 최대 pitch 5.7943°와 최소 세 바퀴
접지를 기록했다. CTest 18/18, UE Automation 35/35, Game/Editor Development 빌드와
`BuildVirtualCity`·`ExportVirtualCityTraffic`의 두 `ValidateOnly`가 통과했다. 실제 사용자
PIE의 진입 속도·각도별 체감과 화면의 순차 axle 상승·벽/Barrier 비관통은 별도 수동 gate다.

50cm authored Curb marker 중심을 가로지르는 full-body footprint 215곳을 같은 production
near/far 계약으로 검사했을 때 도로/보도 연석 중심 214곳이 eligible했다. 옛 1m Bake에
당시 absolute marker-top 후보 계약을 적용한 결과는 164/215였고, 현재는 50cm+signed
delta다. 옛 bytes를 최종 계약으로 재감사하지 않아 개선분을 해상도에만 귀속하지 않는다.
이는 각 collider 중심의
검사이며 연석 전체 길이의
모든 위치를 보장하지 않는다. endpoint/opening의 raised support 누락 구간은 fail-closed한다.
`Curb_BayEnd` 중심은 near/far delta의 지속 raised-support 계약을 만족하지 않고 뒤의
`Barrier_BayEnd`가 정차 공간 끝을 막으므로 의도적으로 ineligible하다. 모든 Curb
semantic을 통과 대상으로 만들지 않는다.

50cm 전체 along-length QA에서는 215개 중 210개 collider가 전 길이 eligible했다. 전구간
차단은 `Curb_BayEnd` `[-5,5]`이고, 북쪽 T-opening의 네 collider는 opening 쪽 끝단만
차단된다: `Curb_North_-1_-1` `[+33.72,35]`, `Curb_North_-1_1` `[-35,-33.72]`,
`Curb_North_1_-1` `[+34.04,35]`, `Curb_North_1_1` `[-35,-33.72]`. East grade·corner·
South 연석은 전구간 eligible했다. 영구 회귀는 네 opening 쪽 endpoint의 fail-closed와
반대 endpoint·중심의 pass, South uniform 위치를 고정한다. 전체 215개 sweep 수치는
one-off 저작 package QA 근거로 유지한다.

## 결과

### 장점

- Unreal scene에 대한 지식과 차량 계산 권한이 명확히 분리된다.
- 500m급 Landscape를 1m 간격으로 보존하면서 triangle materialization과 20k 제한을 없앤다.
- UE가 측정한 surface normal을 suspension·tire 계산에 전달할 수 있다.
- UE가 식별한 surface material/friction을 versioned snapshot으로 전달하고 차량 설정 v5로
  시나리오별 접지력을 재컴파일 없이 조정할 수 있다.
- 기존 MapPackage와 hot reload/reset/checksum 안전 계약을 유지한다.

### 한계와 후속

- 현재 heightfield는 한 수평 위치에 하나의 상단 지면만 표현하므로 overpass·동굴 같은
  다층 표면은 별도 mesh/tile provider가 필요하다.
- 이번 형식은 하나의 packed global lattice를 전체 교체한다. 변경 tile만 재사용하는
  directory/partial reload는 성능 측정 후 별도 schema로 결정한다.
- 실제 가상 도심 v2 Bake·연석 package 회귀는 통과했다. Unreal 5.6 사용자 PIE에서
  연석 진입 속도·각도, 경사·요철·경계·차체 자세, 재Bake 후 재연결과 벽·Barrier 비관통
  gate를 통과해야 사용자 환경 완료로 판정한다.
