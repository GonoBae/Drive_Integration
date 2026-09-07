# Drive Integration 문서

이 디렉터리는 Drive Integration 프로젝트의 계획과 기술 기준을 관리한다. 1차 릴리스의 목표는 **작은 가상 도심 코스에서 외부 C++ 물리로 동작하는 수동운전 버티컬 슬라이스**다. 2026-08-31 사용자 승인으로 기존 Wall/Broad 실지리 재현 목표를 대체했다. NPC 규칙 AI 확장과 별개로 플레이어 차량 자율주행/FSD는 R1 범위에 포함하지 않는다.

## 문서 구성

| 문서 | 목적 | 주요 내용 |
|---|---|---|
| [01_schedule.md](./01_schedule.md) | 일정과 작업량 관리 | 일별 일정, 마일스톤, 완료 게이트, 지연 대응 |
| [02_feature_matrix.md](./02_feature_matrix.md) | 범위와 완료 조건 관리 | 8월 기능, 향후 자율주행 기능, 기능별 검증 기준 |
| [03_architecture.md](./03_architecture.md) | 시스템 설계 관리 | C++·Unreal·Python 책임, 지도·충돌·통신·센서 구조 |
| [refactoring.md](./refactoring.md) | 2026-09-02 전체 리팩터링 기록 | C++·Unreal 책임 분리, 보존 계약, 공통 서버 launcher와 검증 체크포인트 |
| [core_validation.md](./core_validation.md) | Core 자동 통합과 측정 | 한 명령 검증, 서버 입력 기반 Ego 물리 재생, opt-in UE frame/state 측정과 수동 gate 경계 |
| [04_environment_plan.md](./04_environment_plan.md) | 배경 제작 기준 | 가상 도심 전환 승인, 수정 가능한 코스 초안과 blockout→충돌→외형→성능 순서 |
| [virtual_city_quickstart.md](./virtual_city_quickstart.md) | 가상 도심 실행 | 전용 서버·새 맵·주행·Bake·자동 시험과 남은 수동 확인 |
| [signal_city_quickstart.md](./signal_city_quickstart.md) | 다중 교차로 신호 도심 실행 | 별도 v2 맵·서버, 두 controller 신호, 생성·검증 순서와 PIE 인수 경계 |
| [vehicle_driving_refinement.md](./vehicle_driving_refinement.md) | 차량 주행 피드백 개선 | 조향 설정/선회 반경, 마우스 카메라 조작, 자체 세단 메시·재질, 검증·한계 |
| [traffic_network_signals.md](./traffic_network_signals.md) | 차선·신호 기반 | v1 lane25/head3과 v2 lane58/head16/controller2, 좌·직·우 3차로·보행 전용 WALK·원자 WorldState·hot reload |
| [npc_lane_following.md](./npc_lane_following.md) | NPC 주행 기반 | v1 NPC 1대 이력과 v2 NPC 10대·보행자 8명, 차선·신호·충돌 반작용 및 남은 수동 인수 경계 |
| [traffic_crash_feedback.md](./traffic_crash_feedback.md) | 충돌·교통 피드백 8건 | 실제 dent·즉시 전도/비행·낮은 보행 충돌체·사고 회피·NPC 상호 충돌·깜빡이·3차로 수동 확인 |
| [npc_navigation.md](./npc_navigation.md) | NPC 목적지·우회·차선 변경 | deterministic 목적지 선택, 동일 목적지 우회, 같은 방향 차선 변경·안전 가드와 legacy 호환 |
| [structure_damage.md](./structure_damage.md) | 구조물 충돌 피해 | 건물 외벽 부분 손상, 신호등 기둥 전도·소등과 controller 적색, 자체 계산·표시 한계와 Play 확인 |
| [에셋 등록표](./assets/README.md) | 출처·배포 조건 관리 | 지도·외부 배경 에셋 취득 전 라이선스·사용 경로·검증 기록 |
| [ADR-005](./decisions/ADR-005-realtime-transport-protocol.md) | 실시간 통신 결정 | WebSocket binary + Protobuf, JSON runtime 제거, UDP 재검토 조건 |
| [ADR-006](./decisions/ADR-006-custom-vehicle-physics.md) | 자체 차량 물리 결정 | C++ 직접 구현 근거, 단계별 범위, 비교 시험과 재검토 조건 |
| [ADR-010](./decisions/ADR-010-low-latency-control-presentation.md) | 저지연 제어 결정 | 입력 coalescing, 2단계 control lease, 제한된 상태 예측과 재검토 조건 |
| [ADR-011](./decisions/ADR-011-canonical-coordinate-frames.md) | 공통 좌표계 결정 | ROS 호환 FLU canonical frame, Unreal FRU 경계 변환, 남은 이행 작업 |
| [ADR-012](./decisions/ADR-012-unreal-ground-measurement-boundary.md) | 지면 책임 경계 결정 | Unreal collision 측정·`SIMGHF2` material/friction snapshot과 C++ 차량 물리 계산 분리 |
| [ADR-013](./decisions/ADR-013-small-virtual-city-course.md) | 가상 도심 전환 결정 | 실지리·Cesium·랜드마크 의존 제외, 로컬 차선·충돌·traffic·기록·인수 기준 유지 |
| [ADR-014](./decisions/ADR-014-server-clock-traffic-signals.md) | 신호 시간 권한 결정 | 서버 simulation clock 계산, Unreal 표시·도로 저작, reset·안전 상태·기록 시간 일치 |
| [ADR 목록](./decisions/README.md) | 기술 결정 색인 | 현재 ADR과 후속 번호 관리 |
| [학습 센터](./study/README.md) | 교육 문서의 단일 입구 | 일정, 학습자료, 질문 및 답변의 세 문서로 안내 |
| [2026-08-14 작업일지](./worklogs/2026-08-14.md) | 일별 실행 기록 | D1 구현, 검증 결과, 남은 위험과 다음 작업 |
| [2026-08-19 작업일지](./worklogs/2026-08-19.md) | 일별 실행 기록 | D6 4륜 물리·Unreal 통합, 지연 계측, 좌표계 결정, 전체 코드 리팩터링과 재검증 결과 |
| [2026-08-25 작업일지](./worklogs/2026-08-25.md) | 일별 실행 기록 | AI-D7 보충 실행, 외부 차량 설정, MapPackage 지면, heave·경사 자세와 Windows 회귀 결과 |
| [2026-08-26 작업일지](./worklogs/2026-08-26.md) | 일별 실행 기록 | 급경사·등판 보완, collision checksum/lifecycle, 지면·충돌 index, 정적 authoring/export와 demo runtime entity 통합 |
| [2026-08-27 작업일지](./worklogs/2026-08-27.md) | 일별 실행 기록 | 2단계 lease·background log backpressure, 조작감, 자동 후진, 독립 4-corner suspension·차체 자세, UE 부호·MapPackage 경계 보완 |
| [2026-08-28 작업일지](./worklogs/2026-08-28.md) | 일별 실행 기록 | terminal footprint, 100cm heightfield 책임 분리·실제 Bake, same-PID hot reload, surface friction·GeoTransform·Hello·HUD·runtime 설정 자동 선행 통합 |
| [2026-08-31 작업일지](./worklogs/2026-08-31.md) | 일별 실행 기록 | authoritative Health/HUD, 별도 지면·marker 자동 QA와 배경 제작 준비; 실제 PIE와 기존 Must 잔여 구분 |
| [2026-09-01 작업일지](./worklogs/2026-09-01.md) | 일별 실행 기록 | HUD 속도 단위·50km/h/50m/s 계측과 이후 Ground support 기반 연석 등판 보완, 자동 검증·PIE 인수 구분 |
| [2026-09-02 작업일지](./worklogs/2026-09-02.md) | 일별 실행 기록 | 계기판·배기가스·차량음, Signal City 확장, 충돌·dent와 전체 리팩터링·최종 자동 검증 |
| [2026-09-03 작업일지](./worklogs/2026-09-03.md) | 일별 실행 기록 | 문서 동기화, Core 자동 통합·입력 replay·성능 도구와 재접속 맵 경로 회귀 |
| [2026-09-04 작업일지](./worklogs/2026-09-04.md) | 일별 실행 기록 | 충돌·운전자·보행자 표현, NPC 회피와 차종별 동작, 도로 표시 개선 |
| [2026-09-05 작업일지](./worklogs/2026-09-05.md) | 일별 실행 기록 | 커브 차선 교차 수정, 플레이어 4차종 선택과 물리·재생·센서 초기화 통합 |
| [2026-09-07 현황 정리](./worklogs/2026-09-07.md) | 현재 상태와 일정 점검 | 누적 구현·최신 검증 근거·남은 인수와 문서 동기화 |
| [UE 5.6 WebSocket 입력 지연 해결 사례](./troubleshooting/ue56-websocket-growing-input-delay.md) | 문제 해결 기록 | 60Hz producer/30Hz consumer FIFO 누적, event-loop·20Hz heartbeat 수정과 진단 기준 |
| [UE 5.6 control lease timeout 해결 사례](./troubleshooting/ue56-control-lease-timeout-log-backpressure.md) | 문제 해결 기록 | single-thread stderr backpressure, background 파일 로그와 250ms/1초 2단계 lease |
| [Landscape 급경사 접촉 상실 해결 사례](./troubleshooting/ue-landscape-steep-grade-contact-loss.md) | 문제 해결 기록 | stale MapPackage 판별, 절대 자세 제한·downward ray 영구 낙하 수정과 실제 bake 재시험 |
| [Bake 후 조작 불가·자동 MapPackage 재로딩 해결 사례](./troubleshooting/ue-map-package-hot-reload-after-bake.md) | 문제 해결 기록 | manifest 감시·완전 검증·tick-boundary 원자 교체와 Unreal 새 PlaySession 자동 재연결 |
| [UE 편집 지면 떨림·시간축 AA 우회](./troubleshooting/ue-editor-temporal-aa-surface-flicker.md) | 진단·우회 기록 | 사용자 TemporalAA off 효과, 별도 96장 비교의 한계, 편집 뷰포트만 FXAA로 전환하는 개인 설정 |
| [가상 도심 연석 등판 해결 사례](./troubleshooting/virtual-city-curb-climb-ground-support.md) | 문제 해결 기록 | 연석을 static wall처럼만 처리한 원인, Ground support+semantic Curb 이중 저작과 suspension 기반 등판·검증 절차 |

## 현재 합의된 방향

- 개발 인원: 1명
- 작업 시간: 하루 8시간
- 이전 AI 기준선: 핵심 2026년 8월 27일·확장 8월 31일(미달성 이력)
- 현재 관리 목표: Core RC 2026년 9월 7일(수정 버퍼 9월 8일), Should 확장 9월 11일(위험 버퍼 9월 14일). 9월 4일 조건부 조기 인수는 미달성했고, 9월 7일 점검 시에도 최종 인수는 미완료다.
- 배경 범위: 작은 가상 도심 코스(8/31 승인)와 선택형 2교차로 Signal City blockout, 실제 Wall/Broad 재현 제외
- 보행 공간: 로컬 보도·횡단보도와 차량 진입 제한 구역
- 주행: 직선·코너·교차로·완만한 경사·정차 공간을 갖춘 반복 루프와 Signal City 58-lane graph, 차량·보행 신호 head 16개, 같은 방향 차선 변경과 근접 장애물 후진 회피 구현
- Cesium/실지리: R1 의존에서 제외하고 향후 지도 확장으로 연기; 로컬 ENU·FLU 좌표 수학은 유지
- 물리 권한: C++ SimCore
- 차량 물리: 외부 차량 물리 SDK 없이 C++ 서버에서 직접 개발. Unreal의 보행자 ragdoll 등 표시용 엔진 물리와 서버 차량 계산 권한은 구분한다.
- 실시간 통신: R1은 localhost-only WebSocket binary + Protobuf와 양방향 Hello schema/map checksum/capability, connection identity/order/readiness gate를 사용; JSON runtime protocol은 제거
- 공통 좌표: ROS 호환 right-handed FLU; Unreal의 left-handed FRU는 경계 adapter에서만 변환하고 C++/UE quaternion 계약 Automation 1/1 통과
- 실행 설정: C++ server는 strict `runtime_server.cfg`와 cfg&lt;CLI override를 사용; packaged UE 서버 주소 외부화는 후속
- Unreal 역할: 입력, IG(영상 생성), UI, 센서, 에이전트 표현과 Editor scene collision의 지면·정적 marker 측정
- Python 역할: 향후 자율주행 판단; 기존 relay/ZMQ observer는 default-OFF로 동결하고 수동운전 필수 경로와 R1 검증에서 제외
- 차량 사고 파손 및 변형: 충돌 위치별 vertex dent와 사고 상태 표시 구현. 구조물은 부분 손상·파편·기둥 전도를 표시하지만 차량 금속 파괴·부품 분리 및 변형된 충돌체는 구현하지 않았다.
- 보행자: 8명의 횡단보도 보행·신호 준수, 충돌 강도별 밀림·낙상·ragdoll·회복 표시 구현. 자유 목적지 선택이나 군중 시뮬레이션은 제외한다.
- NPC 주행: 10대가 목적지 선택·같은 목적지 우회·안전한 차선 변경을 수행하며, 근접 장애물 앞에서는 후방 안전을 확인해 후진 공간 확보를 시도한다. 차종별 축약 주행 모델이며 기존 cfg는 fixed route를 호환한다.
- 8월 자율주행 학습: 제외
- 목표 장비: Intel i5-10400, NVIDIA RTX 2060
- 목표 성능: 패키지 빌드, 1920×1080, 60fps
- Google Photorealistic 3D Tiles: 8월 기준 구성에서 제외

## 상태 표기

| 상태 | 의미 |
|---|---|
| 확정 | 현재 계획의 기준이며 변경 시 세 문서를 함께 수정해야 함 |
| 실험 후 결정 | 정해진 스파이크와 완료 게이트를 통과한 뒤 확정 |
| 제안 | 구현 전에 사용자와 합의가 필요한 설계안 |
| 연기 | 8월 릴리스 이후 범위 |

## 변경 관리 규칙

1. 기능 추가·삭제 시 기능표의 ID와 릴리스 범위를 먼저 변경한다.
2. 작업량이 바뀌면 일정표의 작업 패키지, 일별 계획, 버퍼를 함께 변경한다.
3. 데이터 소유권이나 모듈 책임이 바뀌면 아키텍처 결정 기록을 갱신한다.
4. 완료는 “코드가 존재함”이 아니라 기능표의 검증 기준을 통과한 상태를 의미한다.
5. 지도 원본, 차량 파라미터, 프로토콜에는 버전과 체크섬을 부여한다.
6. 문서에서 확정되지 않은 수치나 선택지를 확정된 요구사항처럼 사용하지 않는다.
7. 검토 중 발견한 후속 작업은 대화에만 남기지 않는다. 기능/ADR 작업 ID, 현재 상태, 완료 기준, 목표 일정 게이트를 문서에 함께 기록한다.

## 문서 기준 정보

- 최초 작성일: 2026-08-14
- 문서 정리일: 2026-09-07
- 문서 버전: 3.8
- 현재 상태: Signal City lane58/head16/controller2, NPC10대·보행자8명, 플레이어 4차종 선택과
  차종별 물리·CSV v2/physics replay, 커브 차선 v3를 구현했다. collision checksum은
  `86c3103f3e2c7a5b`, traffic checksum은 `b19c15afba6936d7`다. 9/5 기록 기준 CTest34/34,
  재생 통합 후 관련4/4, UE Automation84/84(경고 없는79개·예상 경고5개)를 통과했다.
  9/7에는 문서를 정리했으며 빌드·시험을 새로 실행하지 않았다. 현재 전체 스냅샷의 단일
  통합 검증과 PIE, 카메라 잔여 모드, 패키지·성능·지연·30분 주행·촬영은 남아 있다.
  RGB 센서는 Should, LiDAR/radar/segmentation은 Future이며 Core 필수 인수와 구분한다.
  [현재 현황과 일정](./worklogs/2026-09-07.md), [9/5 구현·검증](./worklogs/2026-09-05.md).

### 이전 단계 이력

아래 수치와 미완료 표기는 해당 날짜의 기록이며 현재 상태는 위 요약을 기준으로 한다.

- 9/3 NPC 추가 후속: Signal City에 목적지 선택·동일 목적지 우회·같은 방향 차선 변경을
  추가했다. 당시 JSON은 lane46/head16, traffic checksum `92ee2d852eb22dff`이며 기존 map
  checksum `7446108adad3e25b`와 `.umap`/ground를 유지한다. 신규 검증·PIE 인수는 아래
  이전 Core 검증 수치와 구분한다. [NPC 내비게이션](./npc_navigation.md).
- 9/3 최신 후속: 적용 입력 기반 Ego 물리 replay, opt-in UE frame/state 성능 캡처,
  재접속 맵 경로 회귀와 한 명령 Core 검증을 추가했다. CTest21/21·UE48/48·Python30/30,
  실제 wire2,280states·replay480ticks(오차0)를 통과했으며 수동/패키지 인수는 남아 있다.
  [실행 안내](./core_validation.md), [실행 기록](./worklogs/2026-09-03.md).
- 8/31 최신 후속: 정확 회전좌표 적분으로 가짜 에너지 증가를 제거했으나 최고속 full-lock yaw 반전은 남아 있다.
  별도로 lane NPC 1대의 경로·신호·장애물 정지/재출발을 통합했다.
  [NPC 구현·일괄 확인](./npc_lane_following.md), [최신 검증 기록](./worklogs/2026-08-31.md).
  아래 "NPC 미완료"는 이전 단계 이력이며 목표 다중 NPC/보행자·수동 인수는 계속 미완료다.
- 8/31 차선·신호 후속: 방향성 lane 25개·head 3개를 실제 저장 도로에서 검증/export하고
  C++ 30초 신호 주기와 Unreal 표시를 연결했다. CTest 16/16·UE Automation 23/23,
  격리 WebSocket의 32초 주기·새 PIE·lease/reconnect 및 실제 세 색 렌더링을 확인했다.
  NPC/보행자·본선 정지선 외형·센서/기록·성능 인수는 남아 있다. 기존 traffic JSON을
  보존 교체하는 명시적 `-UpdateExisting` 갱신 경로는 9/1에 추가했다.
  [계약과 한계](./traffic_network_signals.md), [최신 검증 기록](./worklogs/2026-08-31.md).
  아래 이전 단계의 "LaneGraph 미완료"와 시험 개수는 당시 상태 기록이다.
- 8/31 주행 피드백 후속: 저·중속 조향과 rack 응답, 마우스 orbit/줌/C 복귀,
  자체 세단 메시·10종 재질 구현. C++ 15/15·UE Automation 15/15와 실제 입력 카메라
  검증 통과. 사용자 조작감·최종 아트·패키지 성능 인수 및 4종 카메라 전체 범위는 남아 있다.
  [조작법·구현 경계](./vehicle_driving_refinement.md).
- 8/31 가상 도심 후속: `L_VirtualCity`/`virtual_city_v1` 기본 코스 생성·실제 Bake 완료.
  약 633m 루프·4° 경사·건물 8개·충돌체 228개, 실제 package 저속 물리 한 바퀴와
  CTest 15/15·UE Automation 11/11·Editor/Game build 통과. 기존 Landscape 실적과
  구분하며, 새 코스 PIE 감각/재Bake·최종 아트·LaneGraph/traffic·센서/기록·성능은 남아 있다.
  [빠른 시작](./virtual_city_quickstart.md). 아래 긴 상태 기록의 Landscape 수치는 보존 이력이다.
- 9/1~2 연석 등판 후속: 기존 Ground 59개에 보도116·연석 top215를 더한 390개를 50cm로
  Bake하고, current→predicted swept footprint를 `R` 이하 along 간격의 near(+1R)/far(+3R)
  상대 높이차로 검증해 wheel/suspension이 앞축→뒤축 순으로 등판하도록 collision 경로를
  보완하고, 16cm였던 보도 support를 연석 top 24cm와 맞췄다. collision/traffic checksum은
  `942842…`/`32f818…`이며 실제 package 회귀, CTest18/18·UE35/35·Game/Editor·맵/traffic
  검증과 격리 WebSocket smoke는 통과했다. authored marker 중심의
  full-footprint 215곳 중 도로/보도 연석 중심 214곳이 eligible하다. 전체 along-length
  QA는 210/215 collider 전 길이와 북쪽 T-opening 네 끝단·Barrier 뒤 BayEnd의 의도적
  gap을 확인했다. support 누락 구간은 fail-closed한다. 사용자 PIE 진입 속도·각도/벽
  비관통 인수는 계속 대기다.
  [해결 사례](./troubleshooting/virtual-city-curb-climb-ground-support.md),
  [9/2 작업일지](./worklogs/2026-09-02.md)를 따른다.
- 9/2 다중 교차로 후속: 기존 v1을 보존한 `L_SignalCity`/`signal_city_v2`에
  42 lanes·8 heads·2 controllers의 traffic v2를 연결했다. collision checksum
  `7446108a…`, 227 boxes/Ground124/static49/route394와 두 ValidateOnly, C++ focused 5/5,
  UE SignalCity 6/6·protocol 2/2·표시/lifecycle 3/3, 별도 최신 host의 격리 Health smoke는
  통과했다. 38초·2,281-state traffic v2 smoke도 8 heads/4 groups와 독립 controller를
  확인했다. 기존 v1 프로세스를 종료하고 표준 Release를 최신 소스로 재빌드했으며,
  당시 background launcher의 `127.0.0.1:9000` listener와 `signal_city_v2` 시작 로그를
  확인했다. 이는 일시 실행 증거이므로 현재 listener는 시험 전에 다시 확인한다.
  사용자 PIE·성능 인수는 대기다.
  [빠른 시작](./signal_city_quickstart.md)과 [9/2 작업일지](./worklogs/2026-09-02.md)를 따른다.
- 9/2 전체 리팩터링 후속: C++ runtime options·traffic watcher·protocol decoder·host session을
  분리하고, Unreal sedan/control/diagnostics/replay/runtime entity/traffic presentation 책임을
  나눴다. 세 map launcher도 공통 수명주기로 통합했다. 비정상 NPC heading-rate가 공통
  collision tick을 줄이던 전파는 유한질량 OBB 입력 `±4π rad/s` 상한과 회귀로 차단했다.
  C++ Release build·CTest20/20, UE Editor/Game·Automation47/47, launcher3 profiles,
  Python Proto1/1·helper10/10, 엄격한 Signal City 2,280-state smoke가 통과했다.
  [리팩터링 구조](./refactoring.md)와 [9/3 동기화 기록](./worklogs/2026-09-03.md)을 따른다.
- 9/3까지의 프로젝트 상태: schema-v2 수동입력, 양방향 `Hello` build/schema/map/capability gate,
  차량 설정 v7, 250ms soft SafeStop/1초 hard reconnect와 MapPackage reset/hot reload를
  구현했다. Unreal은 WorldStatic 높이·normal·material/friction과 static marker를
  `SIMGHF2`로 측정하고 C++는 immutable `GroundQuery`와 자체 타이어·서스펜션·충돌 모델로
  최종 pose를 계산한다. 가상 도심은 50cm Ground390/static228, collision checksum
  `942842…`, traffic checksum `32f818…`이며 lane25/head3와 NPC1대 기반까지 구현했다.
  선택형 Signal City는 50cm `401×481` heightfield/static49, lane46/head16/controller2,
  NPC4대의 목적지·우회·차선 변경과 기존 보행자8명을 구현했다. SensorRig timestamp와 F5 snapshot CSV/F6 visual ghost는
  골격이며 9/3에는 별도 서버 입력 기반 Ego physics replay를 추가했다. 실제 sensor payload와
  사용자 기록 인수는 남아 있다. 위치별
  연석/경사/벽, 조작감·충돌/dent 화면, traffic/HUD, 최종 아트·60fps·30분 안정성도
  수동 gate다. 보존된 `landscape_local_v1`, 기본 `virtual_city_v1`, 선택형
  `signal_city_v2`의 실적을 혼동하지 않는다. 관리 목표는 Core 9/7·수정 버퍼 9/8이다.
- 기준 저장소: `Drive_Integration`
