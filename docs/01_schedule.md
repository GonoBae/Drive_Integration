# 2026년 8~9월 개발 일정표

## 1. 문서 정보

| 항목 | 값 |
|---|---|
| 버전 | 2.24 |
| 작성일 | 2026-08-19 |
| 최종 수정 | 2026-09-07 |
| 대상 릴리스 | R1 Manual Driving Vertical Slice |
| 기존 목표일 | 핵심 R1 2026-08-27, 확장 포함 2026-08-31(8/19 AI 기준선, 미달성) |
| 현재 관리 목표 | Core RC 2026-09-07(수정 버퍼 9/8), Should 확장 포함 2026-09-11(위험 버퍼 9/14); 9/4 조건부 조기 인수는 미달성 |
| 개발 인원 | 1명 |
| 기준 작업량 | 1일 8시간 |
| 관련 문서 | [기능표](./02_feature_matrix.md), [아키텍처](./03_architecture.md), [배경 제작안](./04_environment_plan.md) |

## 2. 일정 전제

### 9/7 현재 상태

9/4~5 구현과 검증 기록을 반영했다. `L_SignalCity`는 58 lanes·신호 head 16개·교차로 2곳,
NPC 10대·보행자 8명이며, NPC의 근접 장애물 후진·회피, 충돌 후 주행 복귀, 보행자 낙상과
구조물·차체 파손 표현을 보완했다. 네 collector 커브의 차선 교차를 v3로 수정해 저장 맵에
적용했고, 플레이어도 `1/2/3/4`로 세단·경차·트럭·오토바이를 선택할 수 있다. 차종 변경은
출발점 reset과 차종별 물리·충돌 외곽 변경을 동반한다. 기록·재생에는 차종을 남기며
SensorRig의 sequence/cadence도 새 주행 세션에 맞춰 초기화한다.

9/7 후속 검증은 C++ **34/34**, Unreal **89/89**, Python 검사 도구 **48/48** 통과다.
Unreal은 경고 없는 성공 84개·기존 예상 경고 포함 성공 5개이며 실패는 없다.
서버 통신·물리 재생·저장 맵 검사도 통과했다. 운전석·고정 후방을 포함한 카메라 3종 전환,
외부 접속 INI와 Windows 패키지 제작 절차를 추가했다. 최신 수정본의 PIE 통합 인수,
패키지의 다른 경로/PC 배포·실제 화면, 목표 PC 1080p 60fps·입력 지연·30분 안정성,
촬영 인수는 남아 있다. Windows 패키지 생성과 NullRHI 서버 연결·상태 수신은 통과했다.
SensorRig 좌표·시간 계약은 Core 범위지만 RGB 캡처는 Should, LiDAR payload는 Future다.
오토바이는 네 접점 축약 물리이며 실제 이륜 균형 모델이 아니다.

9/4 조기 인수 목표는 충족하지 못했다. 오늘 9/7은 Core 관리 목표 당일이며 아직 완료로
판정하지 않는다. 9/8 수정 버퍼, 9/11 확장 목표와 9/14 위험 버퍼를 유지한다. 잔여 시간을
새로 계측하지 않았으므로 완료일을 보장하지 않는다. 9/5는 주말 계획에서 제외하되 실제
수행한 작업은 실적으로 기록한다. 자세한 내용은 [9/4](./worklogs/2026-09-04.md)·
[9/5 작업일지](./worklogs/2026-09-05.md)를 따른다. 아래 날짜별 설명의 이전 수치는 당시 이력이다.

### 2.1 현재 실적과 남은 평일

- 8월 14~19일 D1~D6는 실제 완료한 이력으로 보존한다. 이 구간에는 주말 작업 실적이 포함되어 있다.
- 8월 20일부터는 **주말 8월 22~23일, 8월 29~30일을 작업일에서 제외**한다.
- 8월 20일은 개발하지 못했으며, 해당 작업은 8월 21일로 이월됐다.
- 8월 21일 기준 남은 평일은 당일, 24~28일, 31일의 **최대 7일·56시간**이다.
- D6 현재 기존 R1의 남은 기준 공수는 D7~D18의 12일·96시간이다. 일반 속도로 재배치하면 9월 4일 완료가 예상된다.
- 2026년 8월 28일 종료 기준 실제 상태는 **M2 자동 게이트 완료·Unreal 수동 확인 대기,
  WP-03 자동 구현 완료·수동 충돌 gate 대기**다. MapPackage 충돌 식별·lifecycle gate,
  adaptive ground index와 terminal footprint, strict 정적 collider loader, 8m broad phase,
  정적 OBB contact, Unreal ground/static 원자적 export, opt-in demo entity 경로에 이어
  full-bounds Bake와 실행 중 manifest 감시·tick-boundary 원자 hot reload, Unreal-owned
  100cm heightfield와 C++ 물리 계산 책임 분리까지 구현했다. 실제 UE Bake는
  `257,556 samples/256,542 cells`, checksum `fnv1a64:b8b0a6ccd89614de`를 만들었고 서버
  PID `6692`가 재시작 없이 적용했다. generic sedan 조향 rate/cap도 보완했다. 이어 자동
  선행 P2 5~7시간 범위였던 `SIMGHF2` 노면 재질/마찰 계약, vehicle config v5,
  C++/UE GeoTransform·quaternion 계약, 양방향 `Hello` capability gate와 연결별
  identity/sequence·전송 readiness, 경량 진단 HUD와
  strict `runtime_server.cfg`/CLI 외부 설정까지 통합했다. Windows Release 전체 build와
  CTest 14/14 및 전체 suite 20회 280/280, UE 5.6 Editor/Game build, headless coordinate
  Automation 1/1이 통과했다. 다만 현재
  `static_colliders=0`이고 실제 PIE 벽·동적 충돌·경사·조향 감각 확인은 남아 있으므로
  WP-03 전체와 M2 수동 gate는 완료가 아니다.
- 8/28 종료 당시 9/7 Core·9/11 Should 재계획 대비 확정 영업일 지연은 **0일**이다. 이전 AI 기준선과
  비교한 지연은 Core **+7영업일**, 확장 **+9영업일**로 별도 유지한다. 자동 P2 5~7시간을
  선행 완료한 뒤 잔여 Core는 약 **33.5~49시간**이다. 8/31~9/7의 48시간을 넘는 최악
  경우에만 9/8 수정 버퍼가 필요하다고 추정했다. 이는 8/28 이력이며, 8/31 승인된
  배경 변경 당시 추정과 이후 구현 상태는 2.3을 따른다. 최신 잔여 공수는 아직 재계측하지 않았다.

### 2.1.1 8/31 실행 반영

오늘 초반에는 사용자 요청에 따라 미완료 M3의 authoritative Health/HUD와 지면·정적 marker
자동 QA를 우선 진행하고 배경 제작안을 정리했다. Health는 차량 WorldState에 원자적으로
포함해 상태·안전 표시 간 불일치와 별도 frame의 큐 간섭을 피한다. 검증 결과는
[8/31 작업일지](./worklogs/2026-08-31.md)를 따른다.

**이 작업만으로 오늘 계획 전체 또는 M3를 완료한 것은 아니다.** 실제 `NewMap`의
v2 재Bake·marker 배치·PIE 주행/충돌/재연결 확인은 남아 있다. 후속 B 흐름의 blockout·
LaneGraph·신호 기반 구현은 2.1.3에 기록하며, NPC/보행자와 C의
SensorRig/record/replay는 남아 있다. 자동 QA용 별도 package는 사용자 맵 gate를
대체하지 않는다. 미완료 수동 gate가 있으므로 9/4 선행 인수는 계속 조건부이며 확정
완료일로 보고하지 않는다. 33.5~49h는 8/28 잔여 추정 이력으로, 오늘 새로 계측한
잔여 공수가 아니다. 관리 목표 9/7·버퍼 9/8은 유지하며, 배경 변경 이후의 계획 가정은
아래 2.1.2와 2.3에 별도로 기록한다.

### 2.1.2 8/31 승인된 배경 범위 변경

사용자가 **작은 가상 도심 코스로 변경**하는 것을 승인했다([ADR-013](./decisions/ADR-013-small-virtual-city-course.md)). 기존 Wall Street/Broad
Street 실지역 재현·지리 데이터 import/정렬·NYSE/Federal Hall 특정 랜드마크와
Cesium 배경 요구는 가상 도심의 짧은 주행 루프·교차로·보행 구역·제한된 모듈형
배경으로 대체한다. 따라서 지리·배경 관련 Must의 완료 기준은 변경되었으며,
기존 R1의 모든 Must가 그대로 유지된다고 표현하지 않는다.

자체 차량 물리, Unreal 입력·Health/HUD, MapPackage 지면·충돌, 직접 저작한
centerline 기반 LaneGraph, 신호 교차로 1곳·NPC 3~4대·보행자 6~8명, SensorRig·기록/재생,
1080p 60fps·30분 안정성과 이에 맞춘 AT-01~10은 유지한다. 배경 축소를 이유로
이 기술 기능이나 인수 시험을 완료 처리하거나 제외하지 않는다.

**승인 당시의 다음 단계는 별도 가상 도심 맵의 blockout이었다.** 기존 `NewMap`과
`landscape_local_v1`, legacy `wall_broad_v1` bootstrap을 보존하고
`virtual_city_v1` package를 별도로 만드는 결정이었으며, 결정 시점에는 새 맵·package를
생성하지 않았다. 같은 날의 실제 후속 구현은 아래 2.1.3과 2.2에 구분한다. 코스 세부 배치·분위기·에셋 라이선스는
[배경 제작안](./04_environment_plan.md)에 따라 검토하되, 실지도 경계나 GIS
데이터 결정을 기다리는 의존성은 제거한다. 에셋 구매·import는 별도 확인 대상이다.

### 2.1.3 8/31 후속 LaneGraph·신호 구현

별도 가상 도심 blockout 생성·Bake에 이어 **저작 방향성 lane 25개와 차량 신호 head 3개
(교차로 1곳·phase group 2개)**를 구현했다. 제품 `traffic_network.json`은 자동 QA용
`drive_route.csv`와 별개이며, 양방향 루프·북측 T분기·정지선 connector·명시적 terminal을
포함한다. C++ 서버의 simulation clock이 신호 위상·남은 시간을 결정하고 WorldState로
전달하며, Unreal은 그 상태를 표시한다. [구현 계약](./traffic_network_signals.md)과
[ADR-014](./decisions/ADR-014-server-clock-traffic-signals.md)를 따른다.

현재 CTest **16/16**, UE Automation **23/23**, Editor 빌드가 통과했고 실제 32초/
1,921-state 신호 smoke와 새 PIE/all-red·soft/hard timeout·동일 PIE 재연결도 통과했다.
정상 tick의 RED/YELLOW/GREEN 3색 렌더 QA와 이미지 확인도 통과했다. 이후 발견한
heading 360° 경계값 보정도 완료했으며 최종 C++ `ALL_BUILD`, CTest **16/16**,
UE Game Development·Editor 빌드와 `ValidateOnly`의 **25 lanes·3 heads·0 errors·0 warnings**를 확인했다.
NPC/보행자 동작, SensorRig·record/replay, 수정본 수동 주행·신호 인수와
목표 PC 성능·30분 안정성은 미완료다. 따라서 B 전체·M4 또는 릴리스 완료로 계산하지
않으며 Core **9/7·버퍼 9/8**, Should **9/11·위험 버퍼 9/14**를 유지한다.

### 2.1.4 8/31 선행 NPC 1대 구현

사용자가 테스트를 한 번에 진행하기로 하여 다음 개발 단계를 선행했다.
회전좌표계 Euler 적분의 가짜 에너지는 제거했지만 **180km/h full-lock 과도 yaw 반전은
미해결**이며 실차 타당성도 미검증이다. 조향각이나 마찰을 임의로 바꿔 해결 처리하지 않았다.

별도로 NPC 1대의 서버 차선 추종·유한 가감속·신호 정지/녹색 재출발·전방 장애물 정지,
새 Play/lease/map·traffic reload lifecycle과 Unreal 세단 표시를 구현했다.
[NPC 계약과 일괄 테스트](./npc_lane_following.md), [8/31 최신 검증 기록](./worklogs/2026-08-31.md)을 따른다.
C++18/18·UE28/28·Editor/Game 빌드와 실제120.43초/7,225-state NPC 주행 smoke를 통과했다.
다음 자동 개발은 고속 물리 모델 보완과 NPC 3~4대/보행자 확장, SensorRig·기록/재생이다.
수동 주행 인수·목표 PC 성능·30분 안정성은 남아 있으므로 Core **9/7(버퍼9/8)**,
Should **9/11(위험9/14)** 목표는 변경하지 않는다. 1대 선행 구현을 B/M4 전체 완료로 계산하지 않는다.

### 2.1.5 9/1 연석 등판 P0 보완

사용자 PIE 피드백에서 속도를 높여도 가상 도심 연석을 오르지 못하는 결함을 확인했다.
기존 맵은 연석을 static OBB로만 Bake하고 보도를 Ground support에서 제외해, 차체는 막지만
휠·서스펜션이 더 높은 지면을 읽지 못했다. 이는 신규 범위가 아니라 기존
WP-03·PHY-005/006·AT-03 완료 기준에서 발견한 blocker다.

기존 Ground 59개에 보도116·연석 top215를 더한 390개를 전용 migration으로 저장하고 50cm로 Bake했다.
semantic `Curb`의 current→predicted swept along 구간을 `R` 이하 간격으로 검사하고,
near(+1R)·far(+3R) signed delta와 collider 높이 계약이 검증될 때만 planar body collision을
제외했다. authored Curb marker 중심 full-footprint 215곳 중 도로/보도 연석 중심
214곳이 eligible하다. 전체 along-length QA는 210/215 collider 전 길이와 북쪽 T-opening
네 끝단·BayEnd의 의도적 gap을 확인했다. 휠 ray와 서스펜션이
앞축→뒤축·차체 상승을 계산하며 pose나 속도를 직접
주입하지 않는다. 새 map/traffic checksum은 `e1805b…`/`95845d…`이다.

실제 package 연석 회귀와 높은 턱/Wall 차단, CTest **18/18**, UE Automation **29/29**,
Game/Editor Development 빌드와 두 `ValidateOnly`가 통과했다. 그러나 사용자 PIE에서
진입 속도·각도, 앞/뒤축·차체 움직임과 Wall/Barrier 비관통을 확인하기 전에는
WP-03·M2·AT-03을 완료 처리하지 않는다. 기존 Core 9/7·버퍼 9/8 관리 목표도 유지한다.

### 2.1.6 9/1 운전 프레젠테이션 선행 구현

사용자 요청에 따라 Should 확장 중 계기판·배기가스·엔진/타이어 마찰음을 선행했다.
Unreal GameMode는 외부 UI 에셋이나 Blueprint 설정 없이 코드 기반 계기판을 자동 생성하고,
fresh·connected authoritative `WorldState`의 속도(km/h)·RPM·기어·연료만 표시한다.
stale·reconnect·offline이면 이전 수치를 즉시 숨기며, `SIDE BRAKE`는 protocol에 서버
확인값이 없으므로 로컬 command intent임을 경계로 유지한다. 배기가스와 procedural audio도
각각 RPM·종가속도와 RPM·차속·접지·published slip을 읽기만 하며 SimCore 물리나 제어로
값을 되먹임하지 않는다.

UE 5.6 Editor 통합 빌드와 계기판 2/2·배기가스 2/2·오디오 1/1 focused Automation은
통과했다. 이 결과는 계산 경계·자동 생성·stale fail-closed 계약의 증거이며 실제 PIE에서
계기판 가독성, 16:9 배치, 배기 위치/농도, 엔진음 음정·음량과 노면별 마찰음 공간감을
확인한 결과가 아니다. 따라서 `UE-011/012`는 **구현 중**이고 기존 Core/Should 일정과
수동·성능 gate는 유지한다.

### 2.1.7 9/2 다중 교차로 signal_city_v2 선행 구현

기존 `L_VirtualCity`/`virtual_city_v1`을 보존하고 별도 `L_SignalCity`와
`signal_city_v2`를 생성했다. 확정 생성물은 227 boxes, Ground124, static49,
50cm `401×481` heightfield, route394이며 collision checksum은
`fnv1a64:7446108adad3e25b`다. traffic format version 2는 42 lanes·8 heads·2 controllers의
data-driven phase/offset을 제공하고, v1 fixed-cycle 호환과 Unreal fail-closed 표시를 유지한다.

관련 C++ focused CTest 5/5, UE SignalCity 6/6·protocol 2/2·표시/lifecycle 3/3,
countdown focused 1/1, Editor/Game build, 새 map/traffic 생성과 두 `-ValidateOnly`가
통과했다. 임시 `build_signal_city`에서 최신 publisher를 link해 새 package 격리 Health
WebSocket smoke도 통과했고, 검증 뒤 약 975MB의 임시 build만 정리했다. 이어 v2 traffic
helper 7/7과 38초·2,281-state 격리 smoke에서 8 heads/4 groups,
controller 간 simultaneous permission, reset/timeout/reconnect를 확인했다. 다만 기존
`virtual_city_v1` PID 9604 종료→표준 host 최신 재link→전용 launcher 실행까지 완료했다.
PowerShell multiline `if` parse 오류는 중첩 `if`로 수정했고, 2026-09-02 11:25:06부터
PID 23880이 `127.0.0.1:9000`에서 v2 package를 제공한다. PIE는 아직 수행하지 않았으므로
두 교차로/NPC/전체 loop 수동 확인이 다음 gate다. NPC 3~4대·보행자,
SensorRig·기록/재생, 최종 아트,
1080p 60fps·30분 안정성이 남아 있으므로 Core 9/7·버퍼 9/8과 `TRA-001/AT-05` 상태는
완료로 올리지 않는다. [빠른 시작](./signal_city_quickstart.md)과
[9/2 작업일지](./worklogs/2026-09-02.md)를 따른다.

### 2.1.8 9/2 최종 자동 통합 상태

2.1.7 이후 frame-independent 키보드 조향, `F3` 차량 물리 디버그 오버레이,
full-body chassis/rollover 지면 접촉과 충돌 damage/deformation을 구현했다.
`signal_city_v2`는 42 lanes, 신호 head 총16(차량8·보행8), controller2, NPC4대,
보행자8명으로 확장됐다. map checksum은 `fnv1a64:7446108adad3e25b`, traffic checksum은
`fnv1a64:dfcb5e4541d71adf`다.

SensorRig는 `base_link` FLU의 front camera10Hz·roof LiDAR20Hz config와 authoritative
`SimulationTimeNs` metadata 골격까지 구현했다. 실제 image/point cloud 캡처는 아니다.
Unreal `R`은 authoritative snapshot을 `Saved/DriveReplays/last_drive.csv`에 기록하고,
`F6`는 collision-free visual ghost를 재생한다. 이는 command/event 기반 deterministic physics
re-simulation이 아니므로 `REC-002`·`AT-07`을 완전 완료로 올리지 않는다.

실제 `signal_city_course` 5개 검증군, 전체 CTest **20/20**, UE 전체 Automation **47/47**,
**2,280-state** WebSocket smoke의 SafeStop/reset이 통과했다. keyboard 미세 조향·rear
side-brake drift, Ego↔NPC/보행자 유한질량 반작용과 6구역 국부 WPO dent 후속까지 반영한
서버는 공통 launcher가 port9000 listener PID와 실행 로그를 확인한다. 사용자 PIE의
조작감·충돌/dent 화면 일치·실제 HUD 가독성·60fps·30분 안정성은 계속 수동 gate다.

### 2.1.9 9/3 Core 자동 검증·입력 replay·성능 도구

오늘 계획의 자동 범위를 `scripts/check_core.py` 한 명령으로 묶었다. C++/UE build,
전체 회귀, Proto, map/traffic ValidateOnly와 격리된 실제 WebSocket 시험 결과를 단계별
로그와 JSON 보고서로 남긴다. 시험 생략·빈 시험은 전체 자동 통과로 인정하지 않는다.

서버 `--record-physics`가 정확한 적용 입력·reset/lifecycle·외부 동적 충돌 입력을 기록하고,
`--verify-physics-replay`가 같은 executable/cfg/map/origin/Hz에서 Ego 물리를 다시 계산한다.
기존 R/F6 visual ghost와 구분하며 NPC·보행자 AI 자체를 재실행하는 기능은 아니다.
Unreal 성능 캡처는 opt-in이며 frame/state 간격·실행 모드·해상도·제한 설정을 남긴다.
자동 실행 중 발견한 반복 재접속의 상대 맵 경로 누적도 절대 경로 정규화와 회귀로 수정했다.

구체적인 최종 수치·로그는 [9/3 작업일지](./worklogs/2026-09-03.md), 사용법은
[Core 검증 안내](./core_validation.md)를 따른다. 실제 사용자 주행/충돌/신호/녹화 확인,
sensor payload 잔여 범위, 패키징·목표 PC 성능·입력 지연·30분 안정성은 완료로 올리지 않는다.
**오늘 자동 작업 진척만으로 9/4 Core RC를 확정하거나 9/7 관리 목표를 변경하지 않는다.**

### 2.1.10 9/3 사용자 승인 NPC 경로 판단 확장

추가 요청한 **NPC 차량의 목적지 선택·장애물 우회·차선 변경**을 진행했다.
기존 신호 도심 외형·지면은 보존하고, 4개 접근도로에 같은 방향 인접 차선 정보를
추가해 총 46 lanes로 확장했다. Signal City의 기존 두 route는 spawn 배치에 사용하고
이후에는 서버가 도달 가능한 목적지를 선택한다. 보행자 목적지 탐색·전체 NPC 차량
동역학·FSD는 이번 확장에 포함하지 않는다. 구현 계약과 제한은
[NPC 경로 판단](./npc_navigation.md), 검증 결과는 [9/3 작업일지](./worklogs/2026-09-03.md)를 따른다.

이는 당초 자동 통합 작업 이후 승인된 추가 범위다. PIE에서는 차선 변경의 연속성,
주변 간격 부족 시 대기, 신호 준수와 재Play 초기화를 추가 확인한다. 기존 수동·성능·
패키지 인수는 생략하지 않으며, 이 추가만으로 관리 완료일을 앞당기거나 확정하지 않는다.

### 2.2 이전 AI 협업 일정 전제(이력)

> 8/31 후속 구현: 위 2.1.2의 "제작 예정" 단계 이후 실제 `L_VirtualCity`와
> `virtual_city_v1` blockout을 생성·Bake했다. 약 633m 루프, 4° 경사, 건물 8개,
> static collider 228개가 있으며, 실제 package의 C++ 저속 한 바퀴·접지·벽 충돌 자동
> 시험과 저장 맵 재로드를 통과했다. blockout 생성 당시 Editor/Game build, CTest 15/15,
> UE Automation 11/11도 통과했다. 이후 최신 검증 범위는 2.1.3,
> [실행 안내](./virtual_city_quickstart.md)와
> [8/31 작업일지](./worklogs/2026-08-31.md)를 따른다.
>
> 고정 목표각 v6 검증은 이전 이력이다. 후속 v7·hard-stop 지지력 수정의 CTest 16/16·UE 26/26은 통과했고
> 당시 계획은 아래 순서였으며, 후속 NPC 1대 구현은 2.1.4에 반영했다.
> **극한 조향 과도응답·사용자 PIE 확인 → NPC 1대의 차선 추종·신호 정지/재출발 → NPC 3~4대·보행자 6~8명
> 확장 → SensorRig·기록/재생** 순서다. 새 코스 PIE 주행/재Bake 수동 확인도 별도 gate로
> 유지한다. 단색 건물은 blockout이며
> 완성 외형·수동 인수·성능·30분 gate는 남아 있다. 아래 잔여 30.5~45h는 blockout 착수 전
> 조건부 추정 이력으로, 오늘의 실측 잔여 시간이나 완료일 확약으로 재사용하지 않는다.
> Core 9/7·버퍼 9/8 목표는 유지하며 수동 gate 이후 잔여 작업을 다시 산정한다.

| 구분 | 일반 개발 기준 | AI 협업 수정 기준 | 완료일·완성 기준 |
|---|---:|---:|---|
| 기존 핵심 R1 잔여 범위 | 12일·96시간 | 약 2배 생산성, 6일·48시간 | 8월 27일, R1 Must·성능·안정성 게이트를 만족한 Core RC |
| 승인된 추가 기능 | 기존 계획에 없음 | 1.5일·12시간 | 8월 28일과 31일 오전, 운전 HUD·카메라·복구 조작·디버그·replay UI·데모 프리셋 |
| 최종 회귀·릴리스 | 핵심 범위에 포함 | 0.5일·4시간 | 8월 31일 오후, 전체 회귀·30분 시험·패키지·영상·문서 |
| **AI 수정 계획 합계** |  | **8일·64시간** | **주말 작업 없이 8월 31일 최종 완료** |

AI는 코드 초안, 반복 수정, 자동 시험, 빌드 오류 분석, 문서 동기화를 가속한다. Unreal 시각 확인, 조작감 판단, 에셋·라이선스 선택, 목표 PC 성능 측정과 최종 인수는 사람이 직접 확인한다. 2배는 일정 산정 가정이며, 완료 기준을 줄인다는 의미가 아니다.

이 절의 8월 27일·31일 완료일은 8월 19일에 만든 **이전 기준선**이며 실제 완료일로 사용하지 않는다. 당시에도 AI 협업을 이미 약 2배 생산성으로 계산했으므로, 현재 잔여 작업을 다시 일괄 절반으로 줄이지 않는다.

위 64시간 표는 8월 19일에 세운 원래 기준선이며 이미 AI 협업 생산성을 반영했다.
8월 20일 AI-D7 8시간과 8월 21일의 원래 AI-D8 8시간을 다시 하루 8시간으로
압축하지 않는다. 8월 21일에는 좌표·부호 계약에 이어 사용자가 승인한 Python/ZMQ
범위 교정과 `GroundQuery` 기반을 진행했다. Python/ZMQ 교정 1~1.5시간은 추가분이고,
`GroundQuery`는 AI-D7 지형 접촉·서스펜션 8시간에 포함하므로 중복 합산하지 않는다.
따라서 어제 이월 8시간 + 오늘 원래 작업 8시간 + 범위 교정 1~1.5시간의 총량은
**17~17.5시간**이며, 1인 하루 8시간 안에 전부 완료할 수 있는 분량이 아니다.
승인 작업 이후에도 AI-D7 3~4시간과 AI-D8 7.5~8시간, 합계 **최소 11~12시간**을
이월한다. 8월 24일 이후 일정과 8월 31일 목표는 이 잔량을 반영해 별도로 재산정한다.

8월 25일에 AI-D7 잔여를 보충 실행해 외부 차량 설정, MapPackage 삼각형 지면,
차체 heave·노면 pitch/roll, 실제 경사·저속 회귀와 Windows CTest를 완료했다.
8월 25일 종료 당시에는 AI-D7 3~4시간이 해소되고 원래 AI-D8 충돌 패키지
7.5~8시간과 8월 24일 이후 범위가 남아 있었다. 8월 26일 D8-2/3에서 충돌 식별,
정적 core와 동적 proxy 최소 API 자동 기반을 선행했다. 이어 spatial index, Unreal
정적 collider authoring·원자적 export, demo entity runtime·표시 경로까지 구현했으며
당시 잔여량 38~55시간은 8월 28일 통합 결과에 따라 **33.5~49시간**으로
다시 추정했다. 이는 이전 배경 범위의 이력이고 현재 가정은 아래 2.3으로 대체한다.

### 2.3 AI 협업 재기준선(8/26 수립·8/31 배경 변경 반영)

AI가 독립적인 코드·시험·문서 작업을 2~3개 흐름으로 병렬 처리하는 것을 전제로 한다. 다만 공통 schema 확정, 동일 파일 통합, Unreal 빌드, PIE 시각 확인, 목표 PC 성능·30분 안정성 시험은 순차 게이트로 남는다. 9/7 현재 58-lane Signal City·차량/보행 신호, NPC10·보행자8, SensorRig metadata, snapshot CSV/visual ghost와 적용 입력 기반 Ego 물리 재생을 구현했다. 사용자 PIE·카메라·패키징·성능·30분 gate는 남아 있다. 실제 RGB payload는 Should, LiDAR payload는 Future로 유지한다.

8/31 사용자 주행 뒤 코너링 조향 profile 보완, 마우스 orbit/줌/C 복귀,
자체 세단 메시·재질을 선행 구현했다. C++ 15/15·UE Automation 15/15와 실제 카메라
입력/렌더 시험은 통과했지만, 수정본 사용자 운전 감각은 재인수해야 한다.
`UE-005/UE-010`의 외부 추적 카메라 일부를 당겨 작업한 것이며 4종 모드·replay 전체를
완료 처리하지 않는다. 아래 잔여 공수는 이전 추정이고 이번 작업 시간의 실측 재산정이
아니다. 새 수동 gate 확인 전 관리 완료일/버퍼는 그대로 유지한다.
[변경 상세](./vehicle_driving_refinement.md).

**2차 튜닝 이력:** "속도가 조금만 빨라도 회전이 너무 안돼" 피드백에 따라 조향 보완을 수행했다.
속도별 조향 요청 상한을 6.8→8.5m/s²로 조정했으며 마찰/물리 계산은 유지했다.
실제 ENU 궤적 측정과 좌우·고속·저마찰 회귀, CTest 16/16·격리 Health 통신 시험을
통과했다. 이는 속도 cap이 남아 있던 이전 구현의 결과이며 최신 조향 구조의 합격이 아니다.

**v6 조향 수정 — 이전 자동 검증 이력:** 속도 cap과 `comfortable_lateral_accel_mps2`를 제거하고
`input × max_steering_angle` 고정 목표각 → 유한 rack → Ackermann → 기존 타이어 힘/부하에
따른 실제 궤적 구조로 수정했다. 선택형 보조를 추가하지 않았으며 strict 차량 설정은
v6로 전환했다. 동일 입력의 속도 무관 목표각, 힘 예산, 실제 ENU 궤적·slip을 새로
검증하고 고속 full-lock에서 무미끄럼을 강요하지 않는다. 구현과 직접 physics/config
시험은 통과했으며 진입 속도별 동일 입력의 매 tick 각도 차이 1e-6rad 미만, 정속
입력 0.15의 30/40/50km/h 중앙각 5.25° 및 실제 ENU 궤적을 확인했다. 고속 full-lock은
slip·감속을 허용하는 한계 시험으로 특정 실차 검증이 아니다. 강화된 반경 기준을 포함한
최종 C++ `ALL_BUILD`·CTest **16/16(5.73초, skip 0)**, UE Editor/Game Development 빌드와
Automation **25/25**(신규 Steering 2개), 격리 Health smoke가 통과했다. 기존 신호 시험의
cleanup 경고 3개는 남고 신규 조향 시험의 경고/실패는 없다. 새 서버 적용까지 확인했으며
사용자 조작감 인수는 대기다. 그다음 NPC 1대, 목표 traffic 개체 수와 SensorRig·
record/replay 순으로 진행하며 아직 어느 항목도 완료 처리하지 않는다. 공수 재계측이나
Core/Should 목표일·버퍼 단축은 하지 않는다.

**최신 추가 수정 — 자동 검증 통과·극한 응답과 인수 잔여:** 차량 설정 v7에서 앞/뒤 per-tire cornering stiffness를
고정 60k/50k N/rad로 분리한다. v6는 구 강성을 새 두 항목에 각각 복사하고, v5는 cap 항목도 삭제하는 명시적
이행이 필요하다. Unreal의 접지 상실 시 조향 표시 고정은 재현 FAIL 뒤 수정했고 실제
Pawn Tick 표시 회귀를 포함한 **UE Automation 26/26·Editor/Game 빌드**가 통과했다.
이는 검증용 snapshot의 표시 시험이며 실제 서버 최고속 주행 시험과는 구분한다.
서버에서는 50km/h·입력 0.15 진단의 spring 13,422.963N과 FR hard-stop
21.4502003N·s×60Hz가 중량 14,709.975N을 지지하지만 hard-stop 기여가 타이어 마찰
계산에서 누락됨을 확인했고, 반력 반영 수정은 soft spring·`dt` 변경·reset·no-contact와
50km/h `ΣFz` 직접 회귀와 기존 물리 시험을 통과했다. 하중 비례 강성 후보는 기존
Landscape 회귀의 roll 65.681°>45° 실패로 되돌렸고 최신 구성에 포함하지 않는다.
실제 solver 계측에서 30/40/50km/h·입력 0.15의 반경은 이전보다 약 3~5% 줄었지만
180km/h full-lock에서는 실제 heading 변화율의 과도 부호 반전이 남았다.
최종 C++ `ALL_BUILD`·**CTest 16/16(4.92초, 실패 0·skip 0)**, UE Editor/Game 빌드·
**Automation 26/26**과 격리 Health smoke가 통과했고 18:00:12에 새 서버 PID 8520으로
적용했다. **극한 과도응답·사용자 인수는 미완료**이며 자동 통과와 구분한다. 이후 NPC·센서·
기록재생과 사용자 PIE/패키징/성능 gate 및 관리 목표·버퍼는 그대로 유지한다.

| 범위 | AI 협업 공수 추정 이력 | 관리 완료일 | 위험 버퍼 | 완료 기준 |
|---|---:|---|---|---|
| Must/Core RC | 30.5~45h(8/31 blockout·lane/신호 선행 구현 전 추정; 재산정 대기) | 2026-09-07 | 2026-09-08 | 가상 도심으로 변경한 R1 Must, AT-01~10, 1080p 60fps, 30분 안정성, 패키지 후보 |
| Should 확장 | 15~21h(기존 추정; 선행 카메라 작업 후 재산정 대기) | 2026-09-11 | 2026-09-14 | Core RC를 유지하면서 운전 UX·4종 카메라·디버그·replay UI·데모 프리셋 통과 |

다음은 8/31 당시 선행 계획 이력이다. 9/4 조기 인수는 미달성했으며 현재 상태는 상단 요약을 따른다.
수동 gate와 독립인 세 작업 흐름을 선행하면 9월 3일 자동 통합 후보, 9월 4일 사용자 인수
후보가 가능하다. 9월 4일에 모든 Must·AT-01~10·1080p·30분 시험을 통과하면 Core RC
완료일을 그날로 앞당긴다. 이는 조건부 stretch target이며, 사용자 PIE와 목표 PC 인수
시험을 포함한 관리 목표는 통과 전까지 9월 7일로 둔다. 9월 8일은 Must 수정만 수행하는
버퍼이며, 이 버퍼를 사용하면 Should 최종 회귀가 9월 14일까지 이동할 수 있다.

잔여 작업의 우선순위와 의존성은 다음과 같다. 공수 열은 선행 구현 전 패키지 추정
이력이며, 완료 부분을 공제한 최신 잔여 시간을 뜻하지 않는다.

| 우선순위 | 잔여 패키지 | AI 협업 공수 | 선행 조건 | 자동화 선행 범위 | 사용자 게이트 |
|---:|---|---:|---|---|---|
| 완료 | WP-03 자동 구현·실제 payload 적용 범위 | 0h 잔여 | 없음 | checksum·lifecycle gate, strict collision, 8m broad phase, `ASimCoreStaticCollider`, demo entity, full-bounds/hot reload에 이어 Unreal-owned 100cm height/normal/cell snapshot과 C++ query/물리 책임 분리 구현; 실제 257,556-sample Bake와 same-PID apply, 최종 C++ 14/14·신규 provider/reload 100/100·WebSocket 100/100·UE Editor/Game build 통과 | 실제 PIE와 marker 충돌은 아래 수동 게이트 |
| 0 | 극한 조향 과도응답·M2 PIE 확인 | 기존 수동 1h; 추가 수정 잔여 미계측 | v7 CTest 16/16·UE 26/26·Editor/Game 빌드·Health smoke·서버 적용 통과 | 180km/h full-lock yaw 과도 진동 원인·모델 한계 확인, 필요한 수정·회귀 | 등판·요철·차체 자세·저속/고속 조향 감각 재인수 |
| 1 | WP-03 수동 게이트: 연석 등판·PIE 정적/동적 충돌 | 1~2h | Ground390/static228 Bake·연석 package 회귀·최종 빌드 통과 | 실행 절차·로그 진단·새 checksum 준비 | 속도/각도별 연석 앞축→뒤축 상승, 벽·Barrier·demo entity 비관통과 화면 일치 30~60분 |
| 완료 | GeoTransform·quaternion·`Hello`·HUD·외부 실행 설정 자동 범위 | 0h 잔여 | 없음 | 8/28 좌표/통신/HUD 자동 범위에 이어 8/31 WorldState Health와 authoritative SafeStop HUD·지면 QA 구현; C++ 14/14·UE 8/8·Editor/Game build 통과 | 실제 좌표·Health·재연결 확인은 아래 별도 수동 gate |
| 2 | Health/HUD·좌표·재연결 PIE | 0.5~1h | 최신 C++/Unreal build | 상태 로그·재현 절차 준비 | 정상·SafeStop·끊김/복구 표시, FLU↔FRU 자세 확인 |
| 3 | 가상 도심·LaneGraph 수동 정렬 확인·최소 배경 마감 | 기존 전체 6~9h; 잔여 미계측 | 58-lane graph/export/validator와 네 collector 커브 v3 저장 맵 적용 | 실제 검증 피드백 수정, 제한된 반복 모듈·기본 조명 | 주행 루프·도로/lane/충돌 정렬·보행 구역·사용 에셋 라이선스 확인 |
| 4 | LaneGraph 기반 NPC·보행자 동작 | 기존 전체 6~8h; 잔여 미계측 | `signal_city_v2` 58lane/16head/2controller·NPC10·보행자8, 목적지·우회·근접 후진 회피와 충돌 반작용 구현 | 실제 PIE에서 위치·신호·간격·충돌·반복 동작 수정 | 목표 개체 수와 신호 반복 동작 45~60분 |
| 5 | SensorRig·기록/재생 잔여 | 기존 7~9h; 잔여 미계측 | Sensor metadata·세션 reset, R/F6 ghost와 서버 Ego replay의 차종 기록 구현 | 사용자 기록 replay와 environment gate 회귀; RGB는 Should, LiDAR는 Future | AT-07 사용자 기록·재생 확인 30분 |
| 6 | 패키징·성능·30분 안정성·전체 인수·수정 | 9~15h | 모든 Must | 빌드·측정 도구·문서 | 목표 PC 60~90분 |

30.5~45h는 실제 투입 시간 계측이 아니라 **8/31 선행 구현 전 계획 추정 이력**이다.
당시 가상 도심 6~9h는 blockout 2~3h, 저작 LaneGraph·정렬/검증 2~3h, 제한된 재사용
모듈·소품·기본 조명 2~3h를 가정한다. 기본 primitive로 주행 구조를 먼저 확인하고,
이미 사용 가능한 라이선스 확인 에셋 또는 단순 자체 모듈로 배경 범위를 제한할 때의
예산이다. 새 고유 건물 제작·대규모 에셋 탐색/구매·고품질 아트 polish는 포함하지
않으며, 필요한 경우 별도 추정 후 합의한다. graybox만으로 시각 완료를 선언하지 않는다.

실지도 경계·GIS 정렬의 불확실성이 줄어든 만큼만 반영했으며, 이전 33.5~49h를
AI 사용이나 배경 변경 이유로 다시 일괄 절반으로 줄인 값이 아니다. blockout·lane/신호
기반 구현은 진행됐지만 실제 투입·절감 시간을 새로 계측하지 않았다. 실제 PIE·신호
검증과 모듈 에셋 확보·라이선스 확인 뒤 잔여를 다시 추정한다. 구현 건수만으로 Core
날짜를 앞당기거나 완료를 보장하지 않는다.

### 2.4 Core 완료일 단축을 위한 병렬 선행 계획(9/4 조기 인수 미달성 이력)

아래 흐름은 8/31 승인된 가상 도심 완료 기준을 유지하면서 wall-clock을 단축한다. 공통 schema, 동일 파일 병합,
C++/UE 최종 빌드와 PIE는 한 통합 흐름에서 순차 처리한다. A 흐름의 자동 P2 범위는 8월
28일에 선행 완료했고 B는 blockout·lane/신호·NPC4·보행자8 자동 범위까지 진행했다.
C는 metadata/snapshot ghost와 9/3 적용 입력 기반 Ego physics replay까지 구현됐다.
B/C의 사용자 PIE와 수동 gate는 완료로 계산하지 않는다. 실제 RGB payload는 Should,
LiDAR payload는 Future이며 Core 필수 잔여로 포함하지 않는다.

| 흐름 | 8/28·8/31 선행 범위 | 9/1~3 통합 범위 | 사용자 병렬 gate | 완료 조건 |
|---|---|---|---|---|
| P0 차량·지면·충돌 | v7 강성·hard-stop·표시 보완에 이어 Ground390/static228·support 기반 Curb 등판, 보도/연석 top 24cm 정합과 실제 package 회귀, CTest18/18·UE35/35·Editor/Game·두 ValidateOnly·격리 smoke 통과 | 극한 full-lock yaw 원인·모델 한계 확인, 사용자 PIE 결과 blocker 수정·회귀 | 조향 감각·경사·요철, 속도/각도별 연석 등판, 벽·Barrier·재Bake 재연결 PIE | 기존 자동 계약 유지·극한 응답 확인·M2 수동 gate와 AT-03 통과 |
| A 좌표·통신·UI | **자동 범위 완료:** GeoTransform/quaternion round-trip, 양방향 `Hello`, strict 외부 실행 설정, 진단 HUD와 WorldState Health/SafeStop 표시 | 실제 PIE 좌표·Health·재연결 확인과 발견 문제 수정 | FLU↔FRU 자세와 HUD 30~60분 | 자동 계약은 통과; AT-01·06·10 수동/통합 gate 통과 시 완료 |
| B 지도·traffic | **자동 범위 구현:** `signal_city_v2` 42lane/차량8+보행8 head/2controller, NPC4·보행자8와 route/crosswalk·신호 lifecycle. 실제 package CTest 5그룹과 2,280-state smoke 통과 | 사용자 PIE에서 위치·신호·충돌·반복 동작 수정 | 주행 루프·도로/lane/충돌 정렬·모듈 배경/라이선스와 신호 반복 확인 | M4와 AT-04·05 수동 통과 |
| C 센서·재생 | **구현:** FLU sensor metadata, R/F6 ghost와 9/3 서버 적용 입력 기반 Ego physics replay | 사용자 기록과 동일 환경/checksum 회귀; RGB는 Should, LiDAR는 Future | 동일 기록 재생과 ghost/HUD 확인 30분 | M5와 AT-07 통과; ghost만으로 완료 금지, 서버 자동 replay와 사용자 인수 구분 |
| I 통합·릴리스 | 각 흐름의 작은 자동 회귀를 계속 실행 | 9/3 전체 자동 후보, 9/4 목표 PC 인수 후보 | 1080p·지연·30분 연속 주행 60~90분 | 모든 Must·AT-01~10 통과 시 9/4 Core RC |

이전 선행 계획의 **8월 31일 P0 수동 gate 완료 조건은 아직 충족되지 않았다.** 따라서
9월 4일은 실행이 확정된 인수일이 아니다. 9월 1일 수동 gate와 가상 코스 blockout
배치를 확인한 뒤 잔량을 다시 판단하고, 9월 3일 전체 자동 회귀 통합과 목표 PC 인수
준비가 모두 성립할 때만 9월 4일 선행 인수를 시도한다. 미충족 시에는 9월 7일 관리
목표를 유지하고 필요할 때 9월 8일 수정 버퍼를 사용한다.

### 2.5 외부 전제

- Unreal Engine은 5.6을 사용한다. 승인된 가상 도심에는 Cesium·실지도 서비스가 필수가 아니다.
- 차량, 사람, 신호등, 주요 건물은 라이선스가 확인된 기존 에셋을 사용한다.
- 배경은 단순 자체 모듈 또는 라이선스 확인 재사용 에셋으로 제한한다. NYSE·Federal Hall 재현은 현재 범위에서 제외하며, 새로운 고유 건물 제작·에셋 구매는 이번 승인에 포함되지 않는다.
- 개발 및 최종 실행은 Windows 기준으로 검증한다.
- 최종 성능 검증은 i5-10400, RTX 2060 장비에서 수행한다.
- 8월에는 실제 차량 계측 데이터 기반의 정밀 파라미터 식별과 자율주행 학습을 하지 않는다.

## 3. 작업 패키지

아래 144시간은 AI 적용 전 기존 R1의 **기준 공수 이력**이며 현재 잔여 공수가 아니다.
D6 이후 96시간을 AI 협업 48시간으로 계산했던 기준선은 2.2에 보존한다. WP-05의
명칭·산출물·종료 조건만 8/31 승인 범위로 교체하며, 표의 기존 배정 20h를 새 예측으로
사용하지 않는다. 현재 패키지별 잔여 추정은 2.3을 따른다.

| ID | 작업 패키지 | 시간 | 산출물 | 종료 조건 |
|---|---|---:|---|---|
| WP-00 | 물리 구현 전략·기준 스파이크 | 8h | 자체 구현 결정, 후보 비교 결과, 기준 샘플 | 자체 물리 경계와 단계별 검증 기준이 기록되고 개발기 기본 모델 시험 통과 |
| WP-01 | 좌표·시간·프로토콜 기반 | 16h | ENU 좌표 변환, 고정 tick, 공통 Proto 초안 | 렌더 FPS와 무관한 고정 tick 및 메시지 버전 검증 |
| WP-02 | C++ 차량 동역학 | 24h | 차체, 휠, 조향, 제동, 구동계, 타이어·서스펜션 | 평면과 경사면에서 제어 가능하고 기본 물리 시험 통과 |
| WP-03 | 공통 충돌 월드 | 20h | MapPackage 충돌 소스, 정적·동적 충돌 로더 | C++가 연석·벽·차량·보행자 프록시 충돌을 최종 해결 |
| WP-04 | Unreal 수동운전 연결 | 20h | SimCoreClient, ExternalVehiclePawn, 입력·보간 | Python 없이 Unreal↔C++ 수동운전 왕복 성공 |
| WP-05 | 작은 가상 도심 코스·차선(8/31 범위 변경) | 20h(기존 배정) | 저작 centerline 기반 LaneGraph, 로컬 도로·보도·충돌, 제한된 모듈 배경 | 짧은 주행 루프·교차로·보행 구역이 시각·기능적으로 구분되고 도로/lane/collision이 정렬됨 |
| WP-06 | 신호·NPC·보행자 | 12h | 신호 1개, NPC 차량 3~4대, 보행자 6~8명 | 각 에이전트가 규칙과 신호에 따라 반복 동작 |
| WP-07 | 센서·기록 골격 | 6h | SensorRig, 좌표·시간 메타데이터, 주행 기록 | 향후 센서를 추가할 인터페이스와 재생 데이터 생성 |
| WP-08 | 성능·통합 QA | 10h | 성능 캡처, 안정성 시험, 패키지 후보 | 1080p 성능 및 30분 안정성 기준 검증 |
| WP-09 | 기술·릴리스 버퍼 | 8h | 차단 문제 수정, 최종 패키지, 영상용 주행 기록, 문서 | R1 인수 기준 통과 및 결과 보관 |
|  | **합계** | **144h** |  |  |

### 3.1 승인된 확장 패키지(기존 기준선)

| ID | 작업 패키지 | AI 협업 배정 | 산출물 | 종료 조건 |
|---|---|---:|---|---|
| EXP-01 | 운전·운영 UX (`UE-009`, `UE-011`) | 4h | 통합 운전 HUD, reset·reconnect·scenario restart | 연결·SafeStop·상태 age를 운전 중 확인하고 복구 조작을 화면에서 실행 |
| EXP-02 | 카메라·디버그 도구 (`UE-007`, `UE-010`) | 4h | 운전자·추적·고정·자유 카메라, 충돌·차선·휠·힘 오버레이 | 카메라와 검증 표시를 실행 중 전환하고 기본값에서 성능 영향 없음 |
| EXP-03 | replay·데모 조작 (`REC-004`, `UE-009`) | 4h | timeline, play/pause, 배속, seek, 카메라, 데모 프리셋 | 같은 기록을 조작·재생하고 대표 시나리오를 한 번에 시작 |
| EXP-04 | 확장 회귀·최종 릴리스 | 4h | 최종 시험 기록, 패키지, 영상, 문서 | 추가 기능이 R1 Must·성능·30분 안정성 기준을 깨지 않음 |
|  | **합계** | **16h** |  |  |

## 4. 일별 실행 계획

### 4.1 기존 기준선과 실제 실적

아래 8월 24일 이후의 계획 행은 8월 19일에 만든 이전 기준선이다. 실제 실행 행만
실적으로 사용하며, 미달성한 8월 27일·31일 목표는 4.2의 새 기준선으로 대체한다.

| 날짜 | 구분 | 작업 | 당일 산출물·확인점 |
|---|---:|---|---|
| 8/14~19 | 완료 실적 D1~D6 | WP-00/01과 WP-02/04 선행 | 자체 물리, 공통 Proto, 60Hz tick, 직접 WebSocket 왕복, 4륜 평면 타이어, SafeStop, 저지연 입력을 구현·검증 |
| 8/20 목 | 8/25 보충 실행 | WP-01 좌표 계약 마감 + WP-02 완료 | schema v2, 외부 차량 설정, MapPackage 지면, 1D suspension, heave·노면 자세, 평면·경사·저속·회전 자동 시험 완료; 최신 지형 pose Unreal 시각 확인 대기 |
| 8/21 금 | 원래 AI-D8 8h, 미착수 | WP-03 충돌 패키지 | MapPackage schema·checksum, 정적 cooking/loading, 동적 OBB/capsule, 관통 회귀 7.5~8시간 이월 |
| 8/21 금 | 실제 실행 | 좌표 계약 + 범위 교정 + WP-02 기반 | schema v2 C++/Unreal 경계, ZMQ 기본 OFF·Python 동결, `GroundQuery`/`FlatGroundQuery`와 바퀴별 1D spring/damper 기반; Windows Unreal 검증은 후속 |
| 8/22~23 | 주말 | 작업 없음 | 일정·완료일 산정에서 제외 |
| 8/24 월 | AI-D9 | WP-03/04 통합 | Unreal `GeoTransformAdapter`, FLU↔FRU 시각 검증, 충돌 표시, handshake 불일치 차단, reconnect·packet gap·SafeStop HUD; M3 |
| 8/25 화 | 원래 AI-D10, 미착수 | WP-05 지도·LaneGraph | Wall/Broad crop, importer, 방향성 LaneGraph, spline 주행면·보도·커브, 핵심 교차로 정렬; M4 이월 |
| 8/25 화 | 실제 실행 | AI-D7 잔여·M2 자동 게이트 | 추적 차량 설정과 checksum, `MapPackageGroundQuery`, 차체 z/heave·노면 pitch/roll·경사 중력, Windows Release·CTest 9/9, 서버 기본 설정 로드 smoke test |
| 8/26 수 | AI-D11 | WP-06/07 도시 동작·기록 | 신호, TrafficDirector, NPC 3~4대, 보행자 6~8명, SensorRig frame tree·좌표 규약 version, 기록·결정적 재생 골격; M5 |
| 8/26 수 | 실제 실행 | M2 보완 + WP-03 자동 구현 | 급경사 접촉·등판 수식, checksum/lifecycle gate, adaptive ground index·8m broad phase, strict 정적 OBB collision, VehiclePhysics 통합, `ASimCoreStaticCollider`와 ground/static 원자적 export, opt-in demo NPC·보행자 lifecycle·WorldState·UE 표시 구현. 자정을 넘겨 C++ 11/11·핵심 4종×20회·UE Editor build·Landscape/demo smoke까지 통과; tracked Landscape는 `static_colliders=0`이며 marker bake·PIE 충돌은 미완료 |
| 8/27 목 | AI-D12 | WP-08/09 Core RC | 1080p 성능, 30분 안정성, 전체 Must 회귀, 패키지 후보와 알려진 문제; **핵심 R1 기능 동결** |
| 8/27 목 | 실제 실행(진행 중) | WP-02/04 안정화 | 1008 lease timeout의 stderr backpressure를 찾아 background 파일 로그·overrun 1Hz 요약과 250ms soft/1초 hard lease를 구현. 차량 설정 v3에 저속 횡그립, 속도별 rate-limited 조향, 출력 제한·driveline drag를 반영하고 C++ 11/11·핵심 4종×20회를 통과했다. PIE 조작감·장시간 재발 확인은 사용자 게이트 |
| 8/28 금 | EXP-D1 | EXP-01/02 | 통합 운전 HUD, 4종 카메라, reset·reconnect·scenario restart, 충돌·차선·휠·힘 오버레이 |
| 8/29~30 | 주말 | 작업 없음 | 일정·완료일 산정에서 제외 |
| 8/31 월 오전 | EXP-D2 | EXP-03 | replay timeline·play/pause·배속·seek·카메라, 대표 데모 시나리오 프리셋 |
| 8/31 월 오후 | Release | EXP-04 | 기능 동결, 전체 회귀, 목표 PC 30분 시험, 최종 패키지·영상·문서와 R1 태그 |

### 4.2 재산정 실행 계획(8/26 수립·8/31 범위 반영, AI 병렬 작업·주말 제외)

| 날짜 | 우선 작업 | 구현·검증 산출물 | 사용자 확인·종료 조건 |
|---|---|---|---|
| 8/27 목 | **WP-03 자동 구현·최종 자동 검증 선행 달성**; 4-corner 차량 자세 긴급 보완 완료 | 네 독립 suspension reaction·wheel-local tangent·finite-angle pitch/roll, UE roll 부호, 당시 1~3 ray partial support·centre/0-ray gate, 수동 `Fit Sampling Bounds To Ground Actor`; C++ CTest 11/11·차량/설정 각 20/20·UE 5.6 Editor build·runtime smoke 통과 | 이 단계 당시 계획은 Ground Actor 지정→Bake 자동 Fit→서버 재시작→PIE였다. 다음 8/28 행의 hot reload 구현으로 재시작 절차는 대체됐고, marker 정적/동적 충돌은 계속 수동 gate다. |
| 8/28 금 | WP-03 terminal footprint·Bake 운영·조향·지면 책임 분리 보완 | terminal footprint, legacy full-bounds Bake, 250ms 완전 검증/tick swap/새 PlaySession, steering rate/cap에 이어 Unreal-owned 100cm `SIMGHF1` height/ImpactNormal/cell snapshot과 C++ `GroundQuery` 물리 경계를 구현. 실제 257,556-sample Bake와 checksum `b8b0…`의 PID 6692 same-PID apply, Windows Release·CTest 13/13, 신규 provider/reload 100/100, WebSocket 100/100, UE 5.6 Editor/Game build 통과 | PIE 조향·경사·요철·차체 자세·reconnect 확인과 marker Bake 후 벽·커브 비관통은 미완료 |
| 8/29~30 | 주말 | 작업 없음 | 일정·완료일 산정에서 제외 |
| 8/31 월 | 실제: Health/HUD·지면 QA, 가상 도심/lane/신호 기반, v6 조향 후 v7·지지력/표시 수정 자동 검증 | 기존 맵과 25 lanes·3 heads 유지. v6·25/25는 이전 이력. v7 고정 강성 60k/50k·hard-stop 반력·접지 상실 표시 수정의 CTest 16/16(4.92초)·UE 26/26·Editor/Game·Health smoke와 새 서버 적용 통과 | 하중 비례 후보는 지형 회귀 실패로 미채택. 180km/h full-lock 과도 yaw·사용자 PIE·NPC/보행자·SensorRig/replay·패키징/성능은 미완료 |
| 9/1 화 | 극한 조향 모델·일괄 PIE 인수, NPC 다중화 후속 | NPC 1대는 8/31 선행 구현(2.1.4). 고속 yaw 모델 보완과 차선/신호/장애물 통합 인수 후 NPC 3~4대·보행자 6~8명, SensorRig·recorder로 진행 | 조향각/궤적/타이어 힘 구분, NPC 신호·간격·lifecycle 인수. 잔여량·9/4 조건 재검토 |
| 9/2 수 | B NPC/보행자와 C replay 통합 | 기존 도로/lane/marker 정렬 피드백 수정, 제한된 모듈 배경, 서버 신호를 따르는 NPC·보행자 intent, frame metadata·기록 재생 | 코스 주행성·도로/lane/collision 정렬·신호 준수·replay 확인; M4 후보 |
| 9/3 목 | 전체 Core 자동 통합 후보 | 한 명령 C++/UE·Proto/맵·wire 검증, 적용 입력 기반 Ego replay, opt-in 성능 캡처/분석과 재접속 경로 회귀 구현; 실적은 2.1.9와 작업일지 참조 | 수동 gate 미해결 항목이 없어야 9/4 인수 진입; M5/RC 완료 선언 아님 |
| 9/4 금 | **조건부 선행 Core RC 인수 미달성** | 실제: 충돌 후 진행·사고차 회피·근접 후진, 운전자/실내·조명·NPC 차종과 차선 표시 보완 | 최신 PIE·카메라·패키징·1080p·지연·30분 인수 미완료, Core RC 선언 없음 |
| 9/5~6 | 주말(계획 제외) | 9/5 실제: 커브 차선 v3 저장 맵 반영, 플레이어 4차종 선택·물리 profile·기록 호환, C++34/34+후속4/4·UE84/84 | 주말 실적만 기록하며 계획 작업일·완료일 산정에서 제외; 최신 PIE 확인은 대기 |
| 9/7 월 | **Core RC 관리 목표 당일·미인수** | 문서 정리·푸시 완료 후 카메라 3종·외부 접속 INI·Windows 패키지 제작 절차 구현. 구현 종료 후 통합 자동검증, PIE·패키지·1080p·지연 확인 순서 | 목표 PC 30분 주행과 AT-01~10 등 잔여 인수 통과 시 **Core RC**; 현재 완료 확정 아님 |
| 9/8 화 | Must 수정 버퍼 또는 EXP-01 시작 | 실패한 Must만 수정; 통과했다면 운전 UX·복구 조작 | Core 미통과 시 Should 시작 금지 |
| 9/9 수 | EXP-01/02 | 통합 대시보드, 카메라·디버그 오버레이 | 실행 중 전환과 기본값 성능 확인 |
| 9/10 목 | EXP-03 | replay UI·데모 프리셋 | 같은 기록의 play/pause·배속·seek 확인 |
| 9/11 금 | EXP-04 | 확장 회귀, 최종 패키지·영상·문서 | Core RC를 깨지 않으면 **Should 확장 포함 릴리스** |
| 9/12~13 | 주말 | 작업 없음 | 일정·완료일 산정에서 제외 |
| 9/14 월 | 위험 버퍼 | 9/8 Core 수정으로 밀린 확장 회귀·릴리스 | 미완료 Should만 제외할 수 있으며 Must는 유지 |

## 5. 마일스톤과 완료 게이트

| 마일스톤 | 목표일 | 통과 기준 | 미통과 시 조치 |
|---|---|---|---|
| M0 물리 구현 전략 결정 | 8/14 | 자체 구현 범위와 후보 비교 기록, 개발기 기본 모델 빌드·시험 통과 | 자동 시험과 단계별 종료 조건 없이 다음 물리 단계 진행 금지 |
| M1 최소 왕복 수직 절단 | 8/17 | Unreal 입력이 C++ tick에 반영되고 상태가 Unreal에 직접 표시 | 지도·NPC보다 통신과 시간 동기화를 우선 복구 |
| M2 C++ 차량 기반 완료 | 자동 게이트 8/25 완료, Unreal 확인 대기 | ADR-011 schema·좌표 회귀, 외부 차량 설정, MapPackage 지면, 조향·가감속·경사·서스펜션·저속 안정성 시험 통과 | 최신 지형 등판·선회·z·pitch·roll을 Unreal PIE에서 확인하면 최종 통과 |
| M3 충돌 일치 수동운전 | 8/31 목표·수동 gate 미완료, 9/1 우선 확인 | 동일 충돌 소스와 체크섬, 지속 관통 없음, Unreal 임의 보정 없음, FLU↔FRU 자세·회전 시각 검증, 재연결·authoritative SafeStop/Health 표시 | NPC 통합보다 정적 충돌·좌표 adapter·Health 수동 gate부터 복구 |
| M4 가상 도심 주행 루프 | 9/2 | 짧은 코스의 로컬 주행면·저작 LaneGraph·충돌이 정렬되고 교차로·보행 구역 진입 제한·제한된 모듈 배경이 확인됨 | 장식 확대보다 주행면·lane·충돌 정렬과 루프 주행성을 우선 |
| M5 도시 동작·기록 골격 | 9/4 목표·9/7 수동 인수 대기 | 58lane·신호head16·controller2·NPC10·보행자8, SensorRig metadata/reset·R/F6 ghost·차종별 입력 기반 Ego replay 자동 범위 구현. 수동 반복/녹화 확인은 미완료. RGB는 Should, LiDAR는 Future | 신호 준수·기록을 유지하며 PIE 수정; `REC-002/AT-07`은 자동 오차 검증과 사용자 실제 기록 확인을 함께 요구 |
| M6 핵심 R1 Core RC | 9/7(9/8 수정 버퍼) | 기능표의 모든 R1 Must와 1080p 성능·30분 안정성·패키지 후보 통과 | 추가 기능을 시작하지 않고 Must 게이트 복구에 전력 사용 |
| M6.5 확장 기능 후보 | 9/10 | 운전 UX·카메라·디버그·replay·데모 프리셋이 Core RC 회귀를 통과 | 실패한 Should 확장만 제외하고 Core RC 유지 |
| M7 확장 포함 R1 릴리스 | 9/11(위험 버퍼 9/14) | 최종 패키지, 시험 기록, replay, 영상, 알려진 문제, 문서 확보 | 완료되지 않은 Must가 있으면 완료 선언 대신 일정 변경 |

### 5.1 기존 계획과 수정 계획의 완료 기준

| 구분 | 완료일 | 완성 기준 기능 |
|---|---|---|
| 기존 범위·일반 속도 재예측(8/19 이력) | 2026-09-04 | 자체 C++ 차량 물리·서스펜션·충돌, Unreal 직접 수동운전, Wall/Broad MapPackage·LaneGraph, 신호·NPC·보행자, 센서 골격, 기록·재생, 1080p 성능, 30분 안정성, 패키지·문서 |
| 이전 AI 핵심 R1 기준선(8/19, 미달성) | 2026-08-27 | 기존 계획과 **동일한 Must 기능과 인수 시험**을 Core RC로 완료하며 범위를 줄이지 않음 |
| 이전 AI 확장 R1 기준선(8/19, 미달성) | 2026-08-31 | 핵심 R1 + 운전 HUD·4종 카메라·복구 조작·디버그·replay UI·데모 프리셋·최종 영상·문서 |
| **현재 AI 핵심 R1 관리 계획(8/31 승인 반영)** | **2026-09-07, 수정 버퍼 9/8** | Wall/Broad·Cesium·특정 랜드마크를 작은 가상 도심/저작 LaneGraph로 대체. 자체 물리·Unreal 입력/Health·지면/충돌·신호/NPC/보행자·SensorRig·기록/재생과 수정 범위의 AT-01~10, 1080p 60fps·30분 안정성·패키지 후보를 통과한 Core RC |
| **현재 AI 확장 R1 관리 계획** | **2026-09-11, 위험 버퍼 9/14** | Core RC + 통합 운전 HUD + 4종 카메라 + 복구 퀵 액션 + 토글형 디버그 오버레이 + replay 조작 UI + 데모 시나리오 프리셋 + 최종 영상·문서 |

## 6. 일일 운영 방식

하루 8시간은 다음처럼 사용한다. AI가 만든 변경도 자동 시험과 Unreal 수동 확인을 모두 통과해야 완료로 계산한다.

| 활동 | 기준 시간 | 설명 |
|---|---:|---|
| AI 협업 구현·수정 | 4.5h | 코드 초안, 반복 수정, 설정·문서 동기화 |
| 자동 검증 | 1.5h | 단위 시험, schema 검증, 빌드와 회귀 |
| Unreal 수동 검증 | 1h | PIE·패키지 조작, 시각·반응·성능 확인 |
| 문서·로그 | 0.5h | 결정, 파라미터, 알려진 문제 기록 |
| 다음 날 준비 | 0.5h | 의존성 확인과 차단 문제 정리 |

매일 종료 시 다음 네 항목을 남긴다.

1. 완료한 기능 ID
2. 실행한 시험과 결과
3. 다음 마일스톤을 막는 문제
4. 실제 사용 시간과 잔여 추정치

### 6.1 D1 실행 기록

| 항목 | 결과 |
|---|---|
| 완료 기능 | PHY-002 자체 구현 결정, PHY-003/005 1단계, NET-002 공통 스키마 1단계 |
| 선택 | 현재 C++ 서버의 `VehiclePhysics`를 자체 물리엔진으로 단계적 확장; 외부 SDK는 비교 기준으로만 사용 |
| 개발기 시험 | 공통 Proto 생성·왕복, 종방향 힘·기어·bicycle 모델, 정지·가속·제동·후진·우회전 시험 |
| 결과 | macOS ARM64 Release 빌드 성공, CTest 10회 반복 모두 통과 |
| 미완료 게이트 | 고정 SimulationClock, Unreal 직접 왕복, 6DoF·타이어·서스펜션·충돌, Windows x64 시험 |
| 일정 영향 | WP-01 Proto와 WP-02 기본 모델을 선행했으나 자체 구현으로 상세 물리의 검증 책임과 일정 위험 증가 |
| 실제 사용 시간 | 별도 타이머 미사용으로 미계측; D2부터 시작·종료·차단 시간 기록 |
| 상세 기록 | [ADR-006](./decisions/ADR-006-custom-vehicle-physics.md), [D1 작업 로그](./worklogs/2026-08-14.md) |

### 6.2 D6 실행 기록

| 항목 | 결과 |
|---|---|
| 완료 기능 | 절대 deadline SimulationClock, session·순번·queue-age 기반 250ms SafeStop, 4륜 평면 접촉과 종횡 타이어 힘, 공개 body vector Y-left 1차 계약, Unreal wheel state·entity 선택 표시, WebSocket handshake 순서 보장, 입력 coalescing과 제한된 dead reckoning |
| Windows 검증 | MSVC Release 빌드와 CTest 4종 통과, 저지연 변경을 포함한 UE 5.6 Game·Editor target 통과 |
| 통합 검증 | binary Protobuf 왕복, wheel 4개와 종방향 slip 수신, 입력 중단 후 0.6683m/s에서 0m/s SafeStop, HTTP 101 선행 응답 확인 |
| 지연 개선 | state 간격 p95 30.05ms·최대 49.82ms에서 p95 17.08ms·최대 17.20ms, probe command 전송→첫 물리 반응 약 36ms에서 약 27ms |
| 일정 영향 | D11의 입력 반응·상태 지연 처리 일부를 D6에 선행 구현했으며, reconnect·packet gap HUD는 D11에 유지 |
| 남은 한계(당시) | legacy `yaw_rate`·steering 우회전 양수 규약을 ADR-011 FLU 계약으로 이행해야 함; 평면 접촉을 MapPackage 지형 raycast로 교체하고 실제 suspension stroke·경사 시험은 D7에서 진행; 이번 Unreal 변경은 목표 Windows에서 Game/Editor target 재빌드 필요 |
| 8/21 후속 상태 | legacy scalar 부호는 schema v2로 이행하고 `GroundQuery`·`FlatGroundQuery`·바퀴별 1D spring/damper 기반을 추가; 실제 MapPackage 지형, 차체 heave/6DoF와 최신 Windows Unreal 검증은 미완료 |
| 상세 기록 | [D6 작업 로그](./worklogs/2026-08-19.md) |

### 6.3 D7-1 좌표·부호 계약 실행 기록

| 항목 | 결과 |
|---|---|
| 완료 범위 | 공개 FLU에서 `yaw_rate` 좌회전 양수, steering 좌조향 양수, 항법 heading 시계 방향 양수 계약 고정 |
| 경계 구현 | C++ solver 부호를 `BodyFrameAdapter`에 격리하고 Unreal 입력·wheel yaw·heading 예측 경계 소스를 schema v2에 맞춤 |
| 전송 계약 | R1 WebSocket은 v2 `Envelope{WorldState}`를 사용하고 C++·Unreal에서 exact version을 검사; Python relay와 ZMQ는 R1 범위에서 동결 |
| Mac 검증 | C++ Release 빌드와 좌표·protocol 회귀 통과; Python 회귀는 현재 R1 완료 근거에서 제외 |
| 완료 아님 | Protobuf `Hello` handshake, 전체 GeoTransform·quaternion, SensorRig frame, Windows UE 5.6 빌드·PIE |
| 일정 영향 | 좌표·부호 작업이 원래 8/21 AI-D8 충돌 작업을 대체했으므로 충돌 패키지는 이월 |

### 6.4 D7-2 Python/ZMQ 범위 교정과 지면 접촉 기반

| 항목 | 결과 |
|---|---|
| R1 실행 경로 | `Unreal ↔ WebSocket binary + Protobuf ↔ C++`로 한정; 기본 CMake에서 ZMQ dependency·5555 bind를 비활성화 |
| 보존 범위 | Python relay와 구 `EntityStatePacket` observer는 삭제하지 않고 opt-in `release-zmq-observer` preset에 동결; R1 시험·완료 조건에서 제외 |
| 물리 기반 | `GroundQuery`와 기본 `FlatGroundQuery`, 바퀴별 hit point·normal·no-hit 처리, 제한된 1D spring/damper stroke·force 계산 추가 |
| 자동 시험 범위 | 평지 초기 접촉, 기울어진 테스트 plane의 hit·normal, 전체/부분 no-hit, stroke·force clamp와 결정성 검증 |
| 완료 아님 | MapPackage ground/raycast provider, 노면 normal을 이용한 3D tire force, 차체 높이·heave·6DoF constraint, 차량 파라미터 외부 파일, Windows Unreal 검증 |
| 공수 관계 | Python/ZMQ 교정 1~1.5h는 추가분; GroundQuery 기반은 미실행 AI-D7 8h의 일부이며 별도 가산하지 않음 |
| 이월 | AI-D7 잔여 3~4h + 원래 AI-D8 잔여 7.5~8h = 최소 11~12h; Windows 검증은 Mac에서 수행하지 않고 별도 대기 |

### 6.5 D7-3 M2 보충 실행 기록

| 항목 | 결과 |
|---|---|
| 차량 설정 | `cpp/host/config/vehicle_sedan.cfg`에 전체 SI 파라미터를 이동하고 unknown·duplicate·missing·non-finite·물리적으로 잘못된 값은 시작 실패 처리; FNV-1a checksum 로그 출력 |
| MapPackage 지면 | `ground_surface.csv` ENU 삼각형을 읽어 수직 ray의 가장 가까운 높이·정규화 법선을 반환하는 `MapPackageGroundQuery` 구현 |
| 차량 자세 | suspension normal load로 z/heave를 적분하고 노면 normal에 맞춘 pitch·roll 목표 및 경사 중력 성분을 적용; 기존 east/north/yaw와 합쳐 6축 pose를 C++가 발행 |
| 자동 시험 | 설정 fail-closed, MapPackage load·경사 보간·nearest hit, 오르막 heave·pitch, 경사 저속 handbrake, 횡경사 roll, 기존 직진·제동·회전·wheel 회귀 포함 CTest 9/9 통과 |
| Windows 실행 | MSVC Release 빌드 통과, 기본 실행에서 차량 설정 checksum과 Wall/Broad bootstrap ground triangle 2개 로드 확인 |
| 수동 확인 대기 | 새로 추가된 지형 z·pitch·roll이 Unreal 5.6 PIE에서 기대 방향으로 표시되는지 확인 필요; 해당 확인 전 M2 최종 완료 선언은 보류 |
| 당시 다음 작업 | 원래 AI-D8인 MapPackage manifest·checksum, 정적·동적 충돌 cooking/loading과 관통 회귀 |
| 상세 기록 | [2026-08-25 작업 로그](./worklogs/2026-08-25.md) |

### 6.6 D8-1 Unreal 충돌 지면 authoring bridge

| 항목 | 결과 |
|---|---|
| 문제 | Unreal Landscape collision과 C++ `GroundQuery`가 별도여서 Editor에서 만든 요철을 Ego가 인식하지 못함 |
| 구현 | 지정 영역의 실제 `WorldStatic` 충돌을 수직 raycast로 sampling하고 ENU `ground_surface.csv`로 bake하는 `GroundCollisionExporter` 추가 |
| 권한 유지 | runtime Unreal hit feedback은 사용하지 않고 C++가 bake snapshot을 로드해 최종 pose를 계속 결정 |
| 안전·성능 경계 | actor filter, hole/miss cell 제외, 1m 기본 spacing, 과도한 cell 높이 단차 제외, 20,000 triangle 상한, `map_packages/` 이외 출력 거부 |
| 당시 게이트 | UE 5.6 Game·Editor 빌드는 통과; 실제 Landscape export→서버 재시작→PIE 요철 주행 확인 대기(후속 hot reload 구현 전 기록) |
| 당시 후속 | manifest·collision checksum과 collision core 자동 기반은 같은 날 D8-2/3에서 진행; authoring·runtime·PIE gate는 잔여 |

### 6.7 D8-2 MapPackage 충돌 식별과 lifecycle gate

| 항목 | 결과 |
|---|---|
| 실제 충돌 식별 | `manifest.cfg`가 `ground_surface.csv` 등 `collision_files`의 파일명과 실제 raw bytes를 FNV-1a 64로 계산한 `collision_checksum`을 가진다. C++와 Unreal 모두 manifest 누락·unknown/duplicate key·unsafe filename·파일 누락·checksum 불일치를 fail-closed 처리한다. |
| authoring 원자성 | Unreal exporter는 collision payload를 먼저 교체하고 그 실제 bytes에서 새 checksum을 계산한 뒤 `manifest.cfg`를 마지막에 commit한다. 중간 실패 시 다음 실행에서 stale identity를 정상 package로 승인하지 않는다. |
| Unreal 시작 gate | 로컬 manifest와 payload 검증에 성공한 뒤 socket을 연결하고, 첫 `WorldState`의 authoritative checksum이 로컬 값과 일치할 때만 `SimulationReset`과 첫 일반 `ControlCommand`를 전송한다. 불일치는 incompatible 상태로 연결을 닫고 자동 재연결을 중단한다. |
| 서버 lifecycle gate | checksum-valid `SimulationReset`이 현재 source/session/play lifecycle을 열기 전에는 일반 control을 거부한다. E-stop만 초기화 전에도 적용 가능한 안전 예외다. |
| Windows 검증 | 새 MapPackage loader·Unreal client·exporter 변경을 포함한 UE 5.6 Game·Editor target 빌드가 성공했다. 실제 checksum 일치·불일치 PIE 표시는 수동 확인 대기다. |
| 당시 상태 경계 | 이 단계 직후에는 stale ground bake 자동 차단까지만 구현됐고 충돌 core는 후속이었다. 같은 날 D8-3에서 정적·동적 최소 collision core 자동 기반을 추가했다. |

### 6.8 D8-3 WP-03 자동 구현

| 항목 | 결과 |
|---|---|
| strict 정적 loader | manifest가 명시하고 checksum에 포함한 `static_colliders.csv`만 읽는다. 고정 12-field header, ID·semantic(`wall/curb/barrier`)·`obb` shape·유한 수치·양수 half extent·friction/restitution 범위를 검증하고 잘못된 입력은 시작 실패 처리한다. |
| 정적 narrow phase | 수평 OBB 네 축 SAT와 수직 interval을 결합한 OBB-prism manifold를 생성한다. 초기 overlap은 projection으로 분리하고 normal impulse, restitution, Coulomb friction tangent impulse로 ENU 선속도와 yaw rate를 갱신한다. |
| 결정적 microstep | tick 이동량 0.10m, 회전량 1° 이하가 되도록 나누고 최대 64 substep을 허용한다. 예산 초과 운동은 unchecked 적분하지 않고 안전 구간까지만 clamp해 fail-closed하며 collider ID 정렬과 고정 iteration으로 순서를 결정화한다. |
| VehiclePhysics 연결 | 타이어·중력에서 계산한 수평 ENU 속도를 `CollisionWorld`에 전달하고 해결된 east/north·heading·yaw rate를 다시 body 상태로 가져온다. 지면 Z·pitch·roll·접지는 기존 `GroundQuery` 경로가 계속 소유한다. |
| 동적 proxy 최소 API | NPC는 kinematic OBB, 보행자는 vertical capsule로 표현한다. proxy의 선·각속도를 microstep마다 진행하고 contact point 상대속도로 Ego impulse를 계산하며 proxy 자체 authoritative state는 변경하지 않는다. ID·수치·shape 검증과 입력 순서 결정성 시험을 포함한다. |
| 지면 index | `MapPackageGroundQuery`는 adaptive grid로 후보를 줄이고, 겹침이 과도한 입력만 한정된 global fallback으로 보존한다. indexed/brute-force hit 일치와 후보 감소·경계 회귀를 포함한다. |
| 결정적 broad phase | `CollisionWorld`는 8m uniform grid와 ID 정렬·중복 제거로 정적·동적 후보를 선택하며, cell 예산을 넘는 큰 collider만 안전 fallback으로 처리한다. indexed/brute-force 결과 일치 회귀를 포함한다. |
| Unreal authoring·export | editor-only `ASimCoreStaticCollider` Box marker가 wall/curb/barrier ID·재질·OBB를 명시한다. exporter는 ground/static CSV를 staging·교체하고 실제 bytes checksum manifest를 마지막에 commit한다. marker가 없으면 header-only가 정상이다. |
| demo runtime 경로 | `--demo-entities`를 켰을 때 NPC OBB·보행자 capsule의 spawn/reset/move lifecycle을 host가 소유하고 `WorldState`로 발행하며 Unreal이 transient proxy로 표시한다. 이는 LaneGraph·신호·목표 개체 수를 갖춘 최종 traffic 기능은 아니다. |
| 자동·빌드 검증 | CMake Release publisher 최종 build와 CTest **11/11**이 통과했고, 선정한 핵심 4개 test executable을 각각 20회 반복해 모두 통과했다. UE 5.6 Editor DLL은 2026-08-26 23:56:32, 445,952 bytes, SHA-256 `530FC0AC516CC86265AE182410B0F2E0DF7EE5F3CE656CD5F8E248989D68DCF9`다. |
| runtime smoke | `landscape_local_v1` checksum `fnv1a64:239e5e39f3706396`, `ground_triangles=2220`, `ground_cells=1872`, `ground_global=0`, `ground_max_candidates=8`, `static_colliders=0`, demo `runtime_count=2`와 WebSocket `:9000` 시작을 확인했다. 종료 뒤 `simcore_publisher`와 9000/8765/8000 listener가 없음을 확인했다. |
| 미완료 경계 | WP-03 자동 구현은 완료했지만 실제 `landscape_local_v1`은 marker 미배치 상태라 `static_colliders=0`이다. marker 배치·bake 뒤 실제 PIE 벽/커브와 demo 동적 entity를 검증하지 않았으므로 WP-03·PHY-006·PHY-007·AT-03·AT-04는 **진행 중**이다. |
| 일정 영향 | 기존 8/27~28 자동 범위를 8/26에 선행 달성해 잔여 Core 공수는 **38~55h**로 줄었다. 수동 게이트 전까지 9/7 Core RC·9/8 버퍼, Should 9/11·위험 9/14는 유지한다. |

## 7. 일정 보호 규칙

- D1 이후 자체 물리 범위 또는 외부 SDK 사용 방침을 변경하려면 비용과 마일스톤 영향을 먼저 기록한다.
- D7/M2에서 ADR-011 좌표·부호 계약을 동결한다. 이후에는 schema version·handshake와 대체 ADR 없이 의미를 바꾸지 않는다.
- 9월 7일 M6를 통과하기 전에는 EXP 기능을 시작하지 않는다. 9월 8일 Must 수정 버퍼를 사용하면 그날도 EXP보다 Core 복구를 우선한다.
- 확장 기능은 독립 commit과 설정 가능한 기능 단위로 구현하며, 성능·안정성 회귀 시 해당 확장만 제외한다.
- 9월 11일에는 새 기능을 추가하지 않고 성능, 안정성, 패키징, 영상, 문서만 수정한다. Core 수정으로 확장 일정이 이동하면 같은 동결 규칙을 9월 14일에 적용한다.
- 가상 도심 Core에 실지도·Cesium·Google Photorealistic Tiles를 추가하지 않는다. 재도입하려면 별도 범위·라이선스·성능 예산을 합의한다.
- 라이선스가 불분명한 지도·건물·차량 에셋은 릴리스 패키지에 포함하지 않는다.
- 기능을 줄여야 할 경우 Could, Should 순으로 조정한다. Must 항목을 제거하려면 사용자 합의와 일정·기능표 변경이 필요하다.

## 8. 주요 일정 위험

| 위험 | 조기 경보 | 대응 |
|---|---|---|
| 자체 물리 구현 지연 | D6까지 타이어·휠 접촉 시험 미통과 | 실패 수식과 기준 시험부터 수정하고 지도·NPC 작업 확대 금지 |
| 주요 에셋 미확보 | 9/1까지 사용할 모듈·라이선스 확인 불가 | primitive blockout으로 주행 구조 검증을 진행하되, 시각 완료는 분리; 단순 자체 모듈 또는 확보 에셋으로 범위 재합의 |
| 저작 LaneGraph와 시각 도로/충돌 오프셋 | 코스 bake 후 차선과 주행면·marker가 반복적으로 어긋남 | 같은 ENU 원점·저작 centerline을 기준으로 정렬을 검증하고 장식보다 주행 구조를 우선 수정 |
| C++/Unreal 충돌 불일치 | 체크섬 불일치 또는 Unreal 위치 보정 발생 | 실행 차단, 공통 소스 재생성, 권한 경로 점검 |
| RTX 2060 성능 부족 | 목표 PC 측정에서 p95 frame time 목표 초과 | 배경 가시 거리·모듈 수, 그림자·반사·NPC LOD 순 최적화; 1080p 60fps 인수 기준은 유지 |
| 통신 지연·끊김 | 입력 age 또는 seq gap 증가 | timeout 안전 정지, 상태 버퍼·재연결, 고대역 센서 채널 분리 |
| AI 병렬 통합 충돌 | 공통 schema·동일 파일의 동시 변경 또는 UE 빌드 대기 증가 | 패키지 경계를 나누고 schema·통합 빌드·PIE 게이트는 한 흐름에서 순차 검증 |
| 가상 코스 배경 범위 확대 | 9/1 blockout 검토에서 큰 맵·다수 고유 건물·야간/날씨 추가 요청 | 짧은 루프·제한된 모듈 배경 예산을 유지하고 추가 아트는 별도 추정; 승인 없이 Core 기능이나 수동 gate를 생략하지 않음 |

## 9. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| 2.24 | 2026-09-07 | 카메라 3종·외부 접속 설정·Windows 배포 절차 구현, CTest34/34·UE89/89·도구48/48과 통신·재생·맵 검사 반영. 실제 화면·성능·30분 주행 인수와 기존 목표일은 유지 |
| 2.23 | 2026-09-07 | 9/4~5 실적과 9/7 인수 대기를 동기화. 58lane/NPC10·근접 후진 회피·충돌 보완·커브v3·플레이어4차종·차종 replay/SensorRig reset, C++34/34+후속4/4·UE84/84(79 경고 없음+5 예상 경고) 반영. 9/4 조기목표 미달성·9/5 주말 실제 작업을 구분하고 9/7·9/8·9/11·9/14 목표와 센서 우선순위를 유지 |
| 2.22 | 2026-09-03 | 사용자 승인 NPC 목적지 선택·우회·같은 방향 차선 변경과 46-lane 데이터 확장을 추가. 기존 맵과 보행자 경로는 유지하며 PIE 추가 확인·기존 Core 수동 인수는 별도 |
| 2.21 | 2026-09-03 | 한 명령 Core 자동 검증, 적용 입력 기반 Ego 물리 replay, opt-in frame/state 측정 및 재접속 경로 누적 회귀. CTest21/21·UE48/48·Python30/30·wire2,280states·replay480ticks(오차0) 통과. 수동·패키지 인수와 관리 목표는 유지 |
| 2.20 | 2026-09-02 | 기능·wire 계약을 유지한 C++/Unreal 책임 분리와 공통 Windows launcher를 반영. 비정상 NPC 각속도의 partial-tick 전파도 차단하고 C++ build·CTest20/20, UE Editor/Game·Automation47/47, launcher 3 profiles, Python Proto1/1·helper10/10 및 엄격한 Signal City 2,280-state 격리 smoke를 통과했으며 실제 PIE·성능 gate는 유지 |
| 2.19 | 2026-09-02 | keyboard 미세 조향·rear side-brake drift, Ego↔NPC/보행자 유한질량 반작용과 6구역 국부 WPO dent를 반영. CTest20/20·UE46/46·Editor/Game 빌드 통과와 PID24136/port9000 최신 실행을 확인했으며 실제 PIE 조작감·ragdoll·collision-shape 변형·성능 gate는 미완료 유지 |
| 2.18 | 2026-09-02 | frame-independent 조향·F3 overlay·full-body rollover contact·damage/deformation, Signal City 42lane/16head/2controller/NPC4/보행자8, SensorRig metadata·F5 snapshot CSV/F6 visual ghost와 최종 CTest20/20·UE45/45·2,280-state smoke를 반영. 최종 테스트 인계용 PID3840/port9000 실행을 확인했으며 실제 sensor payload·결정적 physics replay·PIE/60fps/30분 gate는 미완료 유지 |
| 2.17 | 2026-09-02 | 기존 v1 PID9604 종료, 표준 Release 최신 재빌드, launcher multiline `if` parse 수정과 `signal_city_v2` PID23880·`127.0.0.1:9000` 실제 시작을 반영. 사용자 PIE·목표 traffic·성능 gate는 유지 |
| 2.16 | 2026-09-02 | 기존 v1을 보존한 `signal_city_v2` 227 boxes/Ground124/static49/route394와 traffic v2 42 lanes/8 heads/2 controllers를 반영. C++/UE 자동 검증·두 ValidateOnly·별도 최신 host Health 및 38초 traffic smoke는 통과했지만, 이 기록 시점에는 v1 PID 정리·표준 Release 재빌드·port9000 launcher·PIE·목표 traffic/성능 gate가 미완료였음 |
| 2.15 | 2026-09-02 | 계기판·procedural 차량음·배기가스 통합, 배기 `-game` assertion 수정, 보도 support 16→24cm 정합과 새 package/traffic checksum을 반영. C++18/18·UE35/35·Editor/Game·ValidateOnly·격리 smoke·오프스크린 실행을 통과했으나 사용자 시청각/연석 PIE gate는 유지 |
| 2.15 | 2026-09-01 | 계기판·배기가스·procedural 엔진/타이어음을 Should 선행 구현으로 기록. authoritative state 읽기·stale 비표시·물리 비개입 경계, UE Editor 빌드와 focused Automation 5/5 통과 및 실제 PIE 시청각 인수 대기를 구분 |
| 2.14 | 2026-09-01 | 가상 도심을 50cm로 재Bake하고 swept-body along≤R signed near/far delta 계약, 30/50m/s tunneling stress, center full-footprint 214/215와 전체 along-length 210/215+의도적 opening/BayEnd gap, 최종 checksum·실제 package 계측을 반영. 수동 PIE gate와 기존 일정은 유지 |
| 2.13 | 2026-09-01 | WP-03/PHY-005/006/AT-03 blocker였던 연석 등판을 Ground390·semantic Curb support 검증과 suspension 경로로 보완. 새 map/traffic checksum, 실제 package 회귀·C++18/18·UE29/29·빌드·두 ValidateOnly 통과를 반영하되 사용자 PIE 전에는 완료·일정 단축으로 계산하지 않음 |
| 2.12 | 2026-08-31 | NPC 1대 차선·신호·장애물 정지/재출발·lifecycle·세단 표시 선행. C++18/18·UE28/28·실제120초 NPC smoke 통과. 회전좌표 에너지 오류 수정과 미해결 고슬립 yaw를 구분. NPC 다중화/보행자/수동 인수 및 일정 목표 유지 |
| 2.11 | 2026-08-31 | v6·25/25를 이전 이력으로 구분하고 v7 고정 강성 60k/50k·migration·hard-stop 반력·접지 상실 표시 수정의 CTest 16/16(4.92초)·UE 26/26·Editor/Game·Health smoke·서버 적용 통과를 기록. 하중 비례 후보는 지형 회귀 실패로 미채택. 최고속 solver 실측의 극한 과도 yaw와 PIE·후속 Must는 미완료, 일정·버퍼 유지 |
| 2.10 | 2026-08-31 | 속도 cap/legacy knob 제거·고정 목표각/유한 rack/Ackermann과 strict cfg v6 구현, ENU 실측·최종 CTest 16/16·UE 25/25·Editor/Game 빌드·Health smoke·서버 적용을 반영. 8.5 cap 튜닝은 이력으로 분리. 조향 사용자 PIE와 이후 NPC·센서·기록재생·패키징·성능 gate는 미완료이며 관리 목표/버퍼 유지 |
| 2.9 | 2026-08-31 | 2차 속도별 조향 요청 상한 8.5 보완·실제 궤적 측정·좌우/고속/저마찰 회귀와 서버 적용을 기록. 사용자 감각 인수와 기존 관리 목표/버퍼 유지 |
| 2.8 | 2026-08-31 | 25개 authored lane·3-head/2-group 서버 신호와 UE 표시 구현, CTest 16/16·UE 23/23·32초/1,921-state 및 lifecycle smoke·3색 실제 렌더, heading 보정 후 최종 C++/UE Game·Editor 빌드·ValidateOnly 통과를 반영. B는 부분 구현이며 NPC 1대 차선 추종·정지/재출발을 다음 우선 작업으로 설정. NPC/보행자 전체·C·수동/성능 gate는 남고 이전 공수·관리 목표/버퍼는 유지 |
| 2.7 | 2026-08-31 | 주행 피드백에 따른 조향 보완·orbit 카메라·자체 세단 외형 선행 실적을 기록. 수정본 수동 감각·4종 카메라 전체·기존 Must는 남아 있으며 관리 목표/버퍼를 유지 |
| 2.6 | 2026-08-31 | 별도 가상 도심 blockout 생성·Bake와 실제 package 주행 회귀/UE 저장 검증 완료를 반영. LaneGraph·traffic 및 수동·성능 gate는 미완료 유지 |
| 2.5 | 2026-08-31 | 사용자 승인에 따라 Wall/Broad·Cesium·특정 랜드마크 지리/배경 Must를 작은 가상 도심으로 대체하고 WP-05·M4·향후 일정·위험을 갱신. 자체 물리·지면/충돌·저작 LaneGraph·traffic·SensorRig/기록재생·인수 gate는 유지. 조건부 잔여 30.5~45h를 패키지별 재합산했으며 Core 9/7·버퍼 9/8, Should 9/11·위험 9/14와 9/4 조건부 상태 유지; 기존 맵 보존·신규 맵 미생성 명시 |
| 2.4 | 2026-08-31 | Health/HUD·지면/marker QA 우선 실행과 배경 제작 제안을 반영. 실제 PIE와 B/C 미착수를 구분하고 M3·당일 전체 완료를 선언하지 않음; 기존 Must/관리 목표 유지 |
| 2.3 | 2026-08-28 | Hello 승인 전 state broadcast 차단·첫 frame 거부 1008 close, connection generation별 source/session/sequence binding, log-safe identity, localhost-only 기본 bind와 HUD missing/old 분리를 추가하고 전체 CTest 20회 280/280 및 실제 negative/positive WebSocket smoke를 기록. 일정·완료 기준은 유지 |
| 2.2 | 2026-08-28 | 자동 P2 5~7h 범위인 `SIMGHF2` 노면 재질/마찰·vehicle cfg v5, GeoTransform/quaternion 계약과 headless Automation 1/1, 양방향 `Hello`, 경량 진단 HUD, strict runtime cfg/CLI를 선행 완료하고 최종 C++ 14/14·UE Editor/Game build를 반영. 잔여 Core를 33.5~49h로 재산정하되 PIE·static marker 수동 gate, 조건부 9/4 Core RC·9/7 backstop·9/8 버퍼를 유지 |
| 2.1 | 2026-08-28 | 실제 100cm `SIMGHF1` Bake(257,556 samples/256,542 cells, checksum `b8b0…`)와 PID 6692 same-PID apply 성공, 남은 PIE·marker 수동 gate, 재계획 지연 0일을 반영하고 A/B/C 병렬 선행·9/3 자동 후보·조건부 9/4 Core RC 인수 계획을 추가 |
| 2.0 | 2026-08-28 | Unreal 지면 측정과 C++ 물리 계산 책임을 100cm `SIMGHF1` snapshot/`GroundQuery`로 분리하고 legacy v1 호환, 2M sample 명시 상한, UE Editor/Game·C++ 13/13·신규 100/100 검증과 당시 실제 새 Bake 대기 gate를 반영 |
| 1.9 | 2026-08-28 | 실제 full-bounds Bake(507cm·19,208 triangles·495.88m span), MapPackage 250ms 감시·완전 검증·tick-boundary hot reload·UE 새 session 자동 재연결, 일반 승용차 조향 응답과 Windows WebSocket 반복 시험 보완을 반영. C++ 12/12·핵심 7종×20회·WebSocket 100/100·UE build·same-PID smoke 통과, 재계획 지연 0일/이전 기준선 Core +7·확장 +9영업일과 잔여 38~55h를 명시 |
| 1.8 | 2026-08-28 | Ground Actor 전체 범위 Bake preflight·자동 Fit, 20k 예산 spacing 자동 상향, exporter/host ENU bbox 진단과 확대 bake 불변 회귀를 추가하고 당시 11/11·핵심 5종×20회·UE 5.6 build 및 남은 재-bake→restart→PIE gate를 반영 |
| 1.7 | 2026-08-28 | tracked Landscape terminal footprint nose-down 재현·수정, 단일 corner/대각선 support 허용과 완전 axle/side fail-close, 당시 11/11·20/20 자동 검증 및 남은 Fit→Bake→restart→PIE gate를 8/28 실적에 반영 |
| 1.6 | 2026-08-27 | 네 독립 suspension reaction 기반 4-corner reduced-order 자세, UE roll 부호, partial support·centre coverage 경계, Ground Actor bounds fit과 최신 C++/UE/runtime 자동 검증 및 남은 재-bake·PIE 수동 gate를 8/27 실행 행에 반영 |
| 1.5 | 2026-08-27 | 1008 timeout·log backpressure와 차량 조작감 우선 안정화 실적, C++ 11/11·핵심 4종×20회, 남은 PIE 재발·감각 게이트를 반영; Core/Should 관리 목표는 유지 |
| 1.4 | 2026-08-26 | WP-03 adaptive ground index·8m broad phase·`ASimCoreStaticCollider` 원자적 export·opt-in demo entity lifecycle/WorldState/UE 표시 자동 구현과 최종 C++ 11/11·핵심 4종×20회·UE Editor build·Landscape/demo smoke를 반영; tracked package `static_colliders=0` 수동 PIE gate, 잔여 Core 38~55h, Core 9/7·버퍼 9/8, Should 9/11·위험 9/14 유지 |
| 1.3 | 2026-08-26 | strict `static_colliders.csv`, OBB-prism SAT·microstep·projection/impulse, VehiclePhysics XY/yaw 통합, NPC OBB·보행자 capsule 최소 API, CTest 11/11·UE Editor 빌드·runtime smoke를 반영하고 8/27~28 선행 달성 및 잔여 Core 44~64h로 갱신 |
| 1.2 | 2026-08-26 | M2 자동 게이트 완료·WP-03 진행 중 실적, `manifest.cfg` 실제 collision checksum과 WorldState→Reset/Control lifecycle gate, AI 병렬 작업·주말 제외 재산정(Core 9/7·9/8 버퍼, Should 9/11·9/14 버퍼)을 반영 |
| 1.1 | 2026-08-26 | Unreal WorldStatic 충돌을 개발용 MapPackage 지면으로 bake하는 D8-1 authoring bridge, UE 5.6 Game·Editor 빌드 통과와 남은 PIE 게이트 반영 |
| 1.0 | 2026-08-25 | AI-D7 보충 실행의 외부 차량 설정·MapPackage 지면·차체 heave/노면 자세·경사 회귀·Windows CTest 9/9를 반영하고 M2를 Unreal 시각 확인 대기로 전환 |
| 0.9 | 2026-08-21 | Python/ZMQ를 R1 기본 경로에서 동결하고 GroundQuery·1D suspension 기반과 11~12시간 최소 이월량을 반영 |
| 0.8 | 2026-08-21 | 8/20 미작업을 반영하고 8/21 실행 범위를 좌표·부호 schema v2 이행으로 제한; 이후 일정 재산정 필요 상태 기록 |
| 0.7 | 2026-08-19 | ADR-011 FLU 좌표 계약의 legacy yaw/steering·schema·adapter·시험 작업을 D7/D9/D11 게이트에 배치하고 D6 부분 완료 상태를 명시 |
| 0.6 | 2026-08-19 | D6 종료 재검토의 SafeStop session/queue-age, body 좌표계, 횡하중 이동, Unreal 입력·entity 선택 보완과 Windows 재검증 항목 반영 |
| 0.5 | 2026-08-19 | 향후 주말 작업 제외, AI 협업 2배 기준으로 핵심 R1을 8월 27일에 배치하고 운전 UX·카메라·디버그·replay·데모 확장을 포함한 8월 31일 최종 일정으로 개편 |
| 0.4 | 2026-08-19 | D6 휠·타이어·안전 경로와 로컬 60Hz jitter·표시 지연 개선 결과 반영 |
| 0.3 | 2026-08-14 | 자체 C++ 물리엔진 결정, D1 기본 모델·공통 Proto 구현과 시험 결과 반영 |
| 0.2 | 2026-08-14 | Chrono::Vehicle 스파이크 결과와 목표 PC 대기 게이트 기록; 0.3에서 런타임 채택 철회 |
| 0.1 | 2026-08-14 | C++ SimCore 완성을 포함한 144시간 일별 계획 최초 작성 |
