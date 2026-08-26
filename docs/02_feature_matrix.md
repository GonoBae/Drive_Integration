# 기능표

## 1. 문서 정보

| 항목 | 값 |
|---|---|
| 버전 | 2.1 |
| 작성일 | 2026-08-19 |
| 최종 수정 | 2026-08-27 |
| R1 | 수동운전 버티컬 슬라이스; Core RC 2026-09-07(9/8 수정 버퍼), Should 확장 2026-09-11(9/14 위험 버퍼) |
| R2 | 자율주행 환경·경로계획 기반, 일정 추후 확정 |
| R3 | 학습·평가 기반 FSD 연구, 일정 추후 확정 |
| 관련 문서 | [일정표](./01_schedule.md), [아키텍처](./03_architecture.md) |

## 2. 우선순위와 상태

| 표기 | 의미 |
|---|---|
| Must | R1 완료 선언에 반드시 필요 |
| Should | 시간이 허용되면 R1에 포함하지만 Must를 위험하게 만들면 연기 |
| Could | 선택 기능 또는 품질 개선 |
| Future | R2/R3 범위 |
| 결정 | 구현 방향이 합의됨 |
| 구현 | 현재 단계의 코드와 자동·통합 시험이 완료 기준을 충족 |
| 구현 중 | 일부 코드와 시험이 존재하지만 R1 완료 기준은 아직 미충족 |
| 실험 | 정해진 스파이크 결과로 상세 구현을 확정 |
| 제안 | 사용자 확인이 필요한 항목 |
| 연기 | R1에서 구현하지 않음 |

> 2026-08-21 검증 주석: `NET-001/002/003/006`, `UE-001/002`의 기능 경로는 이전 Windows UE 통합 검증을 통과했다. 이후 적용한 Unreal 연결 수명주기와 schema-v2 좌표·표시 경계는 이 Mac에서 정적 검토까지만 완료했으며, 최신 revision의 Windows UE 5.6 Game/Editor 재빌드·PIE는 R1 최종 게이트로 남아 있다.

> 2026-08-25 검증 주석: 최신 schema-v2 서버와 Unreal 5.6의 직접 수동운전 smoke test를 Windows에서 통과했다. 같은 날 추가한 MapPackage 지면 기반 z·pitch·roll 표시는 후속 PIE 시각 확인이 필요하다.

> 2026-08-26 물리 검증 주석: 외부 차량 SDK 없이 자체 모델의 경사 법선 하중·종횡 하중 이동, Ackermann 조향, RWD open differential와 60Hz 타이어 implicit coupling을 구현했다. 20° RWD 등판 자동 회귀와 CTest 9/9는 통과했으며, 특정 실차 정밀 검증과 사용자 Landscape 최종 PIE 등판은 후속이다.

> 2026-08-26 현재 상태: **M2 자동 게이트 완료·Unreal 수동 확인 대기, WP-03 진행 중**이다. WP-03 자동 범위인 checksum/lifecycle gate, adaptive ground index·8m deterministic broad phase, strict OBB collision, Unreal `ASimCoreStaticCollider` authoring과 ground/static 원자적 export, opt-in `--demo-entities` lifecycle·`WorldState`·Unreal 표시는 구현됐다. 최종 C++ Release 11/11, 핵심 4종 각 20회, UE 5.6 Editor build와 Landscape/demo runtime smoke도 통과했다. 현재 `landscape_local_v1`은 marker가 bake되지 않은 `static_colliders=0`이며 실제 PIE 벽·동적 충돌은 미검증이다. 잔여 Core는 38~55h다.

> 2026-08-27 차량 자세 검증 주석: 네 독립 wheel spring/damper 반력을 tire load와 sprung-body heave·finite-angle pitch/roll에 결합하고, UE roll 이중 반전과 기존 pitch ±6°·roll ±8° hard clamp를 제거했다. wheel ray 1~3개는 partial support로 계속 풀며 centre coverage miss 또는 0 wheel ray에서만 fail-closed한다. 최신 Windows C++ Release CTest 11/11, `vehicle_physics_tests`·`vehicle_config_tests` 각각 20/20 반복과 UE 5.6 Editor build는 통과했지만, fit 적용·재-bake·서버 재시작 뒤 최신 자세 부호·각도 경계·등판을 확인하는 실제 Landscape PIE는 수동 검증 대기다. 이 모델은 massless hub·1D suspension과 planar yaw를 사용하는 reduced-order 구조이며 완전한 airborne/전복 6DoF나 특정 실차 동정 모델이 아니다.

## 3. R1 범위 요약

R1은 다음 장면을 완성하는 릴리스다.

> Wall Street·Broad Street 핵심부는 보행 중심으로 유지한다. 플레이어는 주변의 차량 통행 가능 도로에서 C++ SimCore가 계산한 차량을 수동으로 운전한다. 신호, 소수의 NPC 차량과 단순 보행자가 동작하고, 주행은 기록·재생할 수 있다.

### 3.1 반드시 포함

- 외부 C++ 차량 물리와 충돌 권한
- Unreal↔C++ 직접 양방향 통신
- 공통 지도·충돌 패키지와 LaneGraph
- 제한된 Wall/Broad 로컬 주행 환경
- 신호 1개, NPC 차량 3~4대, 보행자 6~8명
- 센서 장착·시간·좌표 인터페이스 골격
- 1080p 60fps 목표와 30분 안정성 검증
- 주행 기록·재생과 촬영 가능한 패키지

### 3.2 명시적 제외

- 자율주행 학습과 학습 데이터 파이프라인
- 카메라 기반 인지 모델, 객체 검출, 차선 인식 모델
- Manhattan 전체 또는 Lower Manhattan 전체 구현
- 대규모 군중 시뮬레이션
- 차량 파손·변형·사고 재현
- 실차 계측 데이터 기반 정밀 차량 동정
- Google Photorealistic 3D Tiles 의존
- Python relay·ZMQ observer의 기능 확장과 R1 runtime 검증

### 3.3 Should 확장 범위(새 목표 9월 11일, 위험 버퍼 9월 14일)

8월 19일의 8월 27일 Core·8월 31일 확장 기준선은 미달성 이력으로 보존한다. 현재는 9월 7일 Core RC 통과 후에만 다음 Should 기능을 추가하며, 9월 8일 Must 수정 버퍼를 사용하면 Core 복구를 우선한다. 확장 포함 목표는 9월 11일이고 Core 수정으로 밀리면 9월 14일을 위험 버퍼로 사용한다. 핵심 성능·안정성을 깨면 해당 확장 기능만 제외한다.

- 속도·기어·FPS·연결·state age·SafeStop을 한 화면에 표시하는 운전 HUD
- 운전자·추적·고정·자유 카메라 전환
- reset·reconnect·scenario restart 퀵 액션과 데모 시나리오 프리셋
- 충돌·차선·휠 접촉·타이어 힘 디버그 오버레이
- replay timeline, play/pause, 배속, seek, 카메라 조작

## 4. 기능 상세

### 4.1 지도와 환경

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| MAP-001 | Must | 구현 중 | Wall/Broad 범위 MapPackage | bootstrap triangle, WorldStatic ground bake, `ASimCoreStaticCollider` OBB authoring, ground/static staging·교체와 manifest-last checksum, strict loader는 구현; 현재 tracked Landscape는 `static_colliders=0`이며 ENU 원점 metadata·차선·신호·spawn 데이터와 실제 marker bake/PIE 검증은 후속 | 다른 도시·트랙도 같은 포맷 사용 |
| MAP-002 | Must | 결정 | Cesium Georeference 사용 | WGS84 위치와 로컬 ENU 위치가 왕복 변환 시험을 통과 | GNSS와 다른 도시 좌표에 재사용 |
| MAP-003 | Must | 결정 | 로컬 주행면·보도·커브 | 주행 루프의 표면과 충돌이 스트리밍 LOD에 영향받지 않음 | 다른 MapPackage에서 자동 생성 |
| MAP-004 | Must | 결정 | 보행 중심 핵심부 | Wall/Broad 핵심 보행 구역을 차량 경로가 통과하지 않음 | 통행 제한 속성으로 재사용 |
| MAP-005 | Must | 결정 | LaneGraph 자동 생성 | 원본 중심선에서 방향성 lane edge와 교차로 연결이 생성됨 | NPC와 경로계획의 공통 그래프 |
| MAP-006 | Must | 결정 | 핵심 구간 수동 보정 | 자동 생성 결과 중 시각 도로와 어긋난 핵심 교차로만 Editor에서 보정 가능 | 수정값을 원본 위의 override로 보존 |
| MAP-007 | Should | 실험 | 제한된 Cesium 배경 | 목표 장비 성능을 해치지 않을 때 폴리곤 범위의 중·원경 배경 표시 | 다른 지역의 저비용 배경 |
| MAP-008 | Must | 결정 | 지도 라이선스 기록 | 원본, 버전, 취득일, 라이선스, 변환 이력이 manifest에 존재 | 배포·영상 출처 관리 |
| MAP-009 | Must | 제안 | 주요 랜드마크 에셋 | 라이선스 확인된 NYSE/Federal Hall 등의 기존 에셋 사용 | 환경 에셋 교체 가능 |

### 4.2 C++ SimCore와 차량 물리

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| PHY-001 | Must | 결정 | C++ 최종 물리 권한 | Ego 및 충돌 참여 엔티티의 최종 pose를 C++가 확정 | 수동·자율 모드 동일 |
| PHY-002 | Must | 구현 중 | 자체 C++ 차량 동역학 코어 | 외부 차량 SDK 없이 단계별 모델과 회귀 시험이 macOS·Windows에서 동작 | 수식·파라미터·상태를 직접 확장 가능 |
| PHY-003 | Must | 구현 | 로컬 ENU 물리 좌표 | 위·경도를 직접 적분하지 않고 meter 단위 ENU에서 계산 | GNSS 변환과 대규모 월드 대응 |
| PHY-004 | Must | 구현 중 | 고정 시뮬레이션 tick | 렌더 FPS와 무관한 기본 60Hz와 overrun skip은 구현; configurable substep은 후속 | headless·재생·학습에 재사용 |
| PHY-005 | Must | 구현 중 | 기본 차량 동역학 | 네 wheel의 독립 1D spring/damper 반력이 tire load와 sprung-body heave·finite-angle pitch/roll을 함께 결정하고, wheel-local tangent tire force·Ackermann·RWD 차동·60Hz 종타이어 implicit coupling·저속 횡력 우선 traction control·유한 조향/출력/drag를 collision-resolved ENU XY·heading과 결합한다. massless hub, unsprung/tire carcass/airborne 동역학과 planar yaw 역결합이 없는 reduced-order 한계 및 실제 Landscape PIE 감각 검증은 후속 | 차량 설정 교체로 다른 차종 지원 |
| PHY-006 | Must | 구현 중 | 정적 충돌 | strict OBB CSV, 수평 SAT+수직 interval, 0.10m/1°·64 microstep fail-closed, impulse와 차량 XY/yaw 통합, adaptive ground index·8m 결정적 broad phase, Unreal marker authoring/export 자동 경로는 구현; tracked package에 실제 marker를 bake한 뒤 PIE 벽·커브 비관통을 확인해야 완료 | 시나리오 지도 공통 |
| PHY-007 | Must | 구현 중 | 동적 충돌 프록시 | NPC kinematic OBB·보행자 vertical capsule, 상대 접촉속도·결정성 시험과 opt-in demo entity spawn/reset/move lifecycle·`WorldState` 발행·Unreal transient 표시를 구현; 실제 PIE identity/표시/충돌과 목표 traffic 동작은 후속 | 향후 다중 에이전트 |
| PHY-008 | Must | 결정 | 노면 재질·마찰 | MapPackage의 표면 ID로 마찰 파라미터를 선택 | 젖은 노면 등 시나리오 확장 |
| PHY-009 | Must | 구현 | 입력 안전장치 | 입력 clamp·세션·순번·100ms 큐 age를 검증하고, 250ms에는 즉시 SafeStop만 적용해 같은 session의 fresh command로 복구하며 1초 연속 단절에서만 session retire·1008 close | 자율주행 fail-safe |
| PHY-010 | Must | 구현 | 물리 파라미터 파일화 | 전체 SI 차량 수치가 format v4 `vehicle_sedan.cfg`에 있고 하중·구동·제동 비율, 조향·파워트레인 응답, 출력·drag, 타이어 slip regularization, 횡력 우선·traction control·공중 휠 감쇠를 포함한 strict loader·validation·checksum으로 재컴파일 없이 교체 | 차량 교체·튜닝·시험 |
| PHY-011 | Must | 구현 중 | 물리 회귀 시험 | 기존 경사·contact·collision·조작감 회귀에 positive pitch/roll 부호, 기존 6°/8° 초과 연속 자세, deep one-side·대칭 four-wheel hard-stop, inactive corner travel, wheel-local tangent, 1~3 ray partial support·centre/0-ray gate와 compound body-Z yaw 계약을 추가했다. 최신 Windows C++ Release CTest 11/11은 통과했으며, 사용자 Landscape에서 fit·재-bake 후 자세·경계·등판·전후진·저속 선회·휠 표시를 함께 확인하고 replay는 후속 | 수식·파라미터 변경 검증 |
| PHY-012 | Future | 연기 | 실차 파라미터 동정 | 대상 차량 계측 데이터와 기준 주행에 맞춰 오차 검증 | 차량별 현실성 향상 |

### 4.3 통신과 동기화

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| NET-001 | Must | 구현 | Unreal↔C++ 직접 연결 | Python relay 없이 명령과 상태가 양방향 전달됨 | 지연과 장애 지점 감소 |
| NET-002 | Must | 구현 | 단일 공통 Protobuf 스키마 | C++와 Unreal wire adapter가 루트 `protocol/`의 하나의 R1 필드 계약을 사용; 동결된 Python 생성물도 같은 원본에서 생성 | 스키마 중복 제거 |
| NET-003 | Must | 구현 | 메시지 Envelope | schema version, sequence, simulation time, source, map checksum, session ID 포함 | 기록·재생·오류 진단 |
| NET-004 | Must | 구현 중 | 재연결·중복·순서 처리 | session별 sequence·큐 age, 250ms soft SafeStop/1초 hard socket 폐기, 같은 session fresh-command 복구, Unreal 자동 재연결과 reset 전 일반 control 차단은 구현; 명시적 `Hello` capability 교환과 재연결 full snapshot 표시는 후속 | 네트워크 견고성 |
| NET-005 | Must | 구현 중 | MapPackage handshake | 양쪽이 `manifest.cfg`의 실제 collision payload checksum을 독립 검증하고 Unreal은 첫 `WorldState` checksum 일치 후에만 Reset/Control을 보낸다. 불일치 fail-closed와 UE 5.6 빌드는 통과했으며 PIE 오류 표시·`Hello` 통합은 후속 | 충돌 불일치 방지 |
| NET-006 | Must | 구현 | WebSocket binary + Protobuf transport | JSON 없이 ControlCommand와 WorldState가 C++↔Unreal 사이에서 전이중 전달되고 HTTP 101보다 payload가 선행하지 않음 | 이후 측정 결과에 따라 UDP/IPC로 교체 가능 |
| NET-007 | Future | 연기 | 고대역 센서 transport | 이미지·LiDAR는 control/state와 분리된 shared memory/전용 채널 사용 | FSD 처리량 확보 |
| NET-008 | Must | 구현 중 | 저지연 60Hz 전달 | Windows 로컬 state 간격 p95 18.5ms 이하, 변화 입력 coalescing 후 33ms 이내 전송; packaged build에서 command/state age를 재측정 | LAN·다중 엔티티 확장 시 transport 판단 기준 |
| NET-009 | Must | 구현 중 | 좌표·부호 schema 계약 | schema v2에서 공개 body FLU와 좌회전/좌조향 양수, 별도 시계 방향 heading, C++·Unreal exact version gate 구현; Python/ZMQ는 동결, checksum gate 포함 UE 5.6 Game·Editor 빌드 성공, Hello·전체 quaternion PIE 검증은 후속 | 향후 Python·센서·ROS 확장 시 동일 계약 사용 |

### 4.4 Unreal IG와 수동운전

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| UE-001 | Must | 구현 | 수동 입력 경로(현재 Pawn 내장) | W/S/A/D·Space와 게임패드 입력에 deadzone을 적용한다. S는 전진 중 제동 후 정지에서 Reverse, W는 후진 중 제동 후 정지에서 Drive로 전환하고 W+S 동시 입력은 제동한다. 방향 전환은 fresh authoritative state가 정지를 확인할 때만 허용하며, 최신 변화값은 최대 30Hz·유지 명령은 20Hz heartbeat로 전송해 FIFO 지연이 증가하지 않음 | 향후 ManualInputComponent로 분리해 AI 명령과 같은 포맷 사용 |
| UE-002 | Must | 구현 | ExternalVehiclePawn | C++ Ego pose·4개 wheel state를 표시하되 시각 휠 회전은 부호 있는 차체 종방향 속도와 타이어 반지름에 연동하고 정지 시 0으로 고정한다. Ego Chaos 동역학은 비활성; opt-in `WorldState`의 NPC·보행자는 별도 transient proxy로 표시 | 외부 물리 엔티티 공통 기반 |
| UE-003 | Must | 구현 중 | 상태 표시·제한 외삽 | 최대 50ms dead reckoning, stale wheel 정지, 자동 재연결, checksum/play-session gate와 runtime entity 생성·갱신·제거 표시는 구현; packet-gap HUD, 재연결 full snapshot과 실제 demo PIE 검증은 후속 | 네트워크 지연 완화 |
| UE-004 | Must | 구현 중 | 좌표 변환 어댑터 | scalar steering/yaw와 평면 ENU·FLU 경계 소스는 적용; `map_enu`↔Cesium/Unreal, quaternion·axial vector 왕복 automation과 Windows PIE 검증은 후속 | 지도 원점 변경과 ROS 호환 센서에 재사용 |
| UE-005 | Must | 결정 | 운전자 카메라와 외부 카메라 | 운전용 카메라와 영상·검증용 외부 고정 camera rig 제공 | 센서와 촬영 분리 |
| UE-006 | Must | 결정 | 핵심 디버그 HUD | 연결, sim tick, packet age, 속도, map checksum, collision, SafeStop 상태 표시 | 통합 문제 진단 |
| UE-007 | Should | 결정 | 충돌·차선·휠 디버그 오버레이 | C++ 충돌 proxy, LaneGraph, 휠 접촉, 타이어 힘을 Unreal 화면에서 개별로 켜고 끌 수 있음 | 지도·차량 물리 보정과 FSD 디버깅 |
| UE-008 | Must | 결정 | 패키지 실행 설정 | 개발 PC가 아닌 목표 Windows PC에서 서버 주소와 지도 설정을 외부 파일로 지정 | 배포 재사용 |
| UE-009 | Should | 결정 | 운영 퀵 액션과 데모 프리셋 | reset·reconnect·scenario restart를 화면에서 실행하고 대표 데모 시나리오를 선택해 같은 초기 상태로 시작 | 반복 QA와 포트폴리오 시연 |
| UE-010 | Should | 결정 | 4종 카메라 모드 | 운전자·추적·고정·자유 카메라를 실행 중 전환하고 조작과 replay 촬영에 사용 | 센서와 촬영 분리 |
| UE-011 | Should | 결정 | 통합 운전 대시보드 | 속도·기어·FPS·연결·state age·SafeStop을 운전 중 한 화면에서 확인 | 데모 운전과 성능·지연 진단 |

### 4.5 신호, 차량 AI, 보행자

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| TRA-001 | Must | 결정 | 신호 상태기계 | 최소 1개 교차로가 설정된 주기로 차량·보행 신호를 전환 | 지도 데이터로 교차로 추가 |
| TRA-002 | Must | 결정 | TrafficDirector | 차선 추종, 목표 속도, 신호 정지 의도를 생성 | C++ 물리와 분리된 rule AI |
| TRA-003 | Must | 구현 중 | NPC 차량 3~4대 | opt-in 고정 demo NPC 1대의 C++ lifecycle·충돌 상태·Unreal 표시는 구현; LaneGraph 추종, 신호·선행 차량 정지와 목표 3~4대는 후속 | 자율주행 상호작용 대상 |
| TRA-004 | Must | 구현 중 | 보행자 6~8명 | opt-in 고정 demo 보행자 1명의 C++ lifecycle·capsule 상태·Unreal 표시는 구현; 경로·보행 신호와 목표 6~8명은 후속 | 센서와 위험 시나리오 대상 |
| TRA-005 | Must | 구현 중 | 동적 엔티티 물리 상태 | C++ authoritative demo entity spawn/reset/move와 정적·동적 collision snapshot, `WorldState` identity/shape/velocity 발행 및 UE 표시를 구현; 실제 PIE 일치와 최종 TrafficDirector intent 통합은 후속 | C++/Unreal 불일치 제거 |
| TRA-006 | Future | 연기 | 군중 시뮬레이션 | 대규모 보행자 회피·밀도 모델 | R1 제외 |
| TRA-007 | Future | 연기 | 사고·파손 | 차량 변형, 파편, 상세 충격 | R1 제외 |

### 4.6 센서와 기록

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| SEN-001 | Must | 결정 | SensorRig와 장착 좌표 | 센서 이름, FLU `base_link` 기준 transform, 주기, 활성 상태를 설정 파일로 정의 | 카메라·LiDAR·Radar 공통 |
| SEN-002 | Must | 결정 | 센서 시간·좌표 메타데이터 | 모든 센서 메시지가 sim time, frame ID, parent frame, pose, sequence, 좌표 규약 version을 가짐 | 다중 센서 동기화 |
| SEN-003 | Should | 제안 | 전방 RGB 카메라 smoke test | 낮은 주기로 프레임과 메타데이터 1개를 기록 | 향후 인지 입력 |
| SEN-004 | Future | 연기 | GNSS·IMU 모델 | noise/bias가 구성 가능한 GNSS·IMU 생성 | localization 학습·평가 |
| SEN-005 | Future | 연기 | LiDAR·Radar·분할 카메라 | 개별 주기와 좌표계로 데이터 생성 | perception 입력 |
| REC-001 | Must | 결정 | 주행 기록 | 명령, 상태, 이벤트, map·vehicle config checksum 저장 | 회귀 시험과 데이터셋 |
| REC-002 | Must | 결정 | 결정적 재생 | 동일 빌드·설정에서 최종 pose 오차가 허용치 안에 있음 | 버그 재현과 영상 촬영 |
| REC-003 | Should | 결정 | replay 기반 영상 | 실시간 플레이와 촬영을 분리해 동일 주행을 재생 가능 | Movie Render Queue 촬영 |
| REC-004 | Should | 결정 | replay 조작 UI | timeline, play/pause, 배속, seek, 카메라를 조작해 기록을 탐색·촬영 가능 | 회귀 분석과 데이터 검수 |

### 4.7 자율주행 확장

| ID | 릴리스 | 상태 | 기능 | 설명 |
|---|---|---|---|---|
| AUTO-001 | R2 | 연기 | 시작점·목적지 설정 | LaneGraph의 유효 지점으로 snap하고 경로 요청 |
| AUTO-002 | R2 | 연기 | 최단·최소비용 경로 | 방향, 통행 제한, 거리, 회전, 신호 비용을 적용한 A* |
| AUTO-003 | R2 | 연기 | 행동 계획 | 차선 유지, 정지, 양보, 회전, 재계획 상태기계 |
| AUTO-004 | R2 | 연기 | 궤적 생성·제어 | 경로를 시간 기반 trajectory로 만들고 steering/throttle/brake 출력 |
| AUTO-005 | R2 | 연기 | Python AutonomyServer | 센서·상태 입력으로 ControlCommand를 생성하며 물리를 계산하지 않음 |
| AUTO-006 | R2 | 연기 | 시나리오·평가 | 충돌, 신호 위반, 경로 이탈, 승차감, 도착률 측정 |
| AUTO-007 | R3 | 연기 | 카메라 기반 인지 학습 | 라벨·센서 데이터셋과 모델 학습·평가 |
| AUTO-008 | R3 | 연기 | imitation learning | 전문가 trajectory와 정책 학습, 폐루프 평가 |
| AUTO-009 | R3 | 연기 | 대규모 데이터 생성 | headless/다중 실행, 날씨·시간·traffic 변형 |

## 5. 비기능 요구사항

| ID | 우선순위 | 요구사항 | R1 검증 기준 |
|---|---|---|---|
| NFR-001 | Must | 성능 | 목표 PC의 패키지 빌드, 1920×1080, uncapped 평균 60fps 이상; p95 frame time 18.5ms 이하를 함께 충족 |
| NFR-002 | Must | 안정성 | 30분 연속 수동 주행 중 crash, deadlock, 무한 재연결 없음 |
| NFR-003 | Must | 안전 정지 | command age가 기본 250ms를 넘으면 throttle 해제 후 제동 |
| NFR-004 | Must | 좌표 일치 | ENU·FLU·Unreal·camera optical 기준축, polar/axial vector, heading/yaw, quaternion 왕복 자동 시험 통과; 기준점 왕복 오차 5cm 이하 |
| NFR-005 | Must | 표시 일치 | 정상 연결 상태에서 Unreal 표시 pose와 최신 C++ 기준 pose 차이 5cm/0.5도 이하 |
| NFR-006 | Must | 충돌 일치 | 동일 collision source checksum을 사용하며 지속 관통이 없음 |
| NFR-007 | Must | 재현성 | 같은 빌드·설정·입력 replay의 최종 위치 1cm, yaw 0.1도 이내 |
| NFR-008 | Must | 설정 가능성 | 서버 주소, 지도, 차량, tick, 센서 장착값을 재컴파일 없이 변경 |
| NFR-009 | Must | 관측 가능성 | tick overrun, packet age, sequence gap, collision, reconnect를 구조화 로그로 기록 |
| NFR-010 | Must | 라이선스 | 지도와 배포 에셋의 출처·라이선스·attribution 기록 |
| NFR-011 | Should | CI 회귀 | C++ 단위 시험과 Proto/MapPackage schema 검증을 명령 하나로 실행 |
| NFR-012 | Must | 로컬 제어 반응 | 목표 Windows PC의 loopback에서 state 간격 p95 18.5ms 이하, 최대 25ms 이하, 입력→첫 상태 변화 50ms 이하 |

## 6. R1 인수 시험

| 시험 ID | 시나리오 | 성공 조건 | 관련 기능 |
|---|---|---|---|
| AT-01 | 서버 실행 및 지도 handshake | 동일 체크섬일 때 시작, 불일치일 때 명확히 거부 | MAP-001, NET-005 |
| AT-02 | 수동 가속·제동·조향·후진 | 입력이 C++ 고정 tick에 반영되고 Unreal에 표시되며 NFR-012 지연 기준을 통과한다. S/W가 반대 방향 주행 중 먼저 제동하고 정지 뒤 Reverse/Drive로 전환하며, 동시 입력과 stale state에서는 안전하게 제동한다. 저속 선회가 과도하게 횡미끄러지지 않고 표시 휠 회전 방향·속도가 차속과 일치한다. | PHY-004~005, UE-001~003, NFR-012 |
| AT-03 | 커브·벽 충돌 | 지속 관통하지 않고 C++ 결과와 Unreal 표시가 일치 | PHY-006, NFR-006 |
| AT-04 | NPC·보행자 상호작용 | 동적 proxy가 충돌 월드와 화면에서 같은 엔티티를 나타냄 | PHY-007, TRA-003~005 |
| AT-05 | 신호 동작 | 차량과 보행자가 자신의 신호에 맞춰 정지·이동 | TRA-001~004 |
| AT-06 | 입력 연결 중단 | 250ms 후 안전 정지하고 HUD와 로그에 원인이 표시 | PHY-009, NFR-003 |
| AT-07 | 기록·재생 | 같은 기록을 재생해 NFR-007 오차 이내 도착 | REC-001~002 |
| AT-08 | 장시간·성능 | 목표 PC에서 30분 안정성과 NFR-001 측정값 기록 | NFR-001~002 |
| AT-09 | 촬영 | replay를 카메라 변경 후 재생하고 영상 출력 가능 | UE-005, REC-003 |
| AT-10 | 좌표·부호 계약 | 좌회전에서 body yaw·steering·wheel lateral 부호가 FLU 계약과 일치하고, heading 관계식 및 FLU↔Unreal 자세 왕복 시험 통과 | NET-009, UE-004, NFR-004 |

현재 AT-01의 strict manifest·실제 collision checksum·reset 전 control 차단 자동 경계는 구현됐지만 실제 PIE의 checksum 일치·불일치 표시는 확인 대기다. AT-03의 adaptive index·8m broad phase·정적 OBB contact와 Unreal marker/export 자동 경로도 구현됐지만 현재 package가 `static_colliders=0`이고 실제 PIE 벽·커브 시험 전이므로 완료가 아니다. AT-04는 opt-in demo NPC·보행자 lifecycle·`WorldState`·Unreal 표시와 runtime `runtime_count=2` smoke까지 통과했으나 실제 PIE identity/동작/충돌과 최종 traffic 개체 수를 확인하지 않아 아직 통과하지 않았다. 최종 C++ 11/11·핵심 4종×20회·UE Editor build는 통과했다.

## 7. 결정 입력 및 상태

| 결정 ID | 내용 | 결정 시점 | 결정하지 않을 때 영향 |
|---|---|---|---|
| DEC-F01 | 기준 Unreal Engine·Cesium 버전 | D1 | 플러그인·빌드 재작업 가능 |
| DEC-F02 | **완료: 현재 C++ 서버에 자체 차량 물리 구현** ([ADR-006](./decisions/ADR-006-custom-vehicle-physics.md)) | 2026-08-14 결정 | 구현량·검증 책임이 증가하므로 단계별 시험 실패 시 후속 일정 재검토 |
| DEC-F03 | **완료: R1 통신은 WebSocket binary + Protobuf** ([ADR-005](./decisions/ADR-005-realtime-transport-protocol.md)) | 2026-08-14 결정 | C++ JSON 입력 파서는 제거, UDP는 측정 후 재검토 |
| DEC-F04 | 대상 차량 종류와 기본 제원 | D3 | 물리는 동작하지만 현실성 검증 기준이 불명확 |
| DEC-F05 | Wall/Broad 정확한 지도 경계와 주행 루프 | D8 이전 | LaneGraph와 환경 범위 변동 |
| DEC-F06 | 사용할 건물·차량·보행자 에셋과 라이선스 | D8 이전 | 영상 품질 또는 배포 가능성 저하 |
| DEC-F07 | **완료·이력: 8월 20일 이후 주말 제외, AI 협업 2배 기준 적용** | 2026-08-19 결정 | 8월 27일 Core·8월 31일 확장 기준선은 미달성했으며 DEC-F10으로 대체 |
| DEC-F08 | **완료: Core RC 통과 후 운전 UX·카메라·디버그·replay·데모 기능 추가** | 2026-08-19 결정 | Must 회귀 시 해당 Should 확장만 제외하고 핵심 릴리스를 보호 |
| DEC-F09 | **완료: 공통 공개 좌표는 ROS 호환 FLU, Unreal FRU는 경계 adapter에서 변환** ([ADR-011](./decisions/ADR-011-canonical-coordinate-frames.md)) | 2026-08-19 결정, schema v2 이행 2026-08-21 | 전체 GeoTransform·quaternion·센서 frame 검증 전에는 좌표 계약 완료를 선언하지 않음 |
| DEC-F10 | **완료: 실제 잔여 범위와 AI 병렬 통합 한계를 반영한 일정 재기준선** | 2026-08-26 결정 | Core RC 9월 7일(9/8 Must 버퍼), Should 확장 9월 11일(9/14 위험 버퍼); 완료 기준은 축소하지 않음 |

## 8. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| 2.1 | 2026-08-27 | 네 독립 suspension reaction 기반 4-corner reduced-order 자세, wheel-local tangent, finite-angle pitch/roll, UE roll 부호, partial support·centre coverage 경계와 자동/수동 검증 범위를 PHY-005·011에 반영 |
| 2.0 | 2026-08-27 | 정지 기반 W/S 전진·후진 전환, 동시 입력·stale-state gear interlock, 차속 연동 표시 휠과 저속 선회 횡력 우선 traction control을 PHY-005·011, UE-001·002와 AT-02에 반영 |
| 1.9 | 2026-08-27 | 차량 설정 v3의 저속 횡그립·속도별 rate-limited 조향·출력/drag와 조작감 회귀, 250ms soft SafeStop/1초 hard reconnect lease, background 파일 로그를 PHY-005·009~011과 NET-004에 반영 |
| 1.8 | 2026-08-26 | adaptive ground index·8m broad phase, `ASimCoreStaticCollider` ground/static 원자적 export, opt-in demo entity lifecycle/WorldState/UE 표시와 최종 C++ 11/11·핵심 4종×20회·UE Editor build·runtime smoke를 반영; WP-03 전체는 tracked `static_colliders=0`·수동 PIE gate 때문에 진행 중, 잔여 Core 38~55h |
| 1.7 | 2026-08-26 | strict 정적 collider loader, OBB-prism SAT·microstep·projection/impulse, VehiclePhysics XY/yaw 통합, NPC OBB·보행자 capsule 최소 API, CTest 11/11·UE Editor·runtime smoke 실적과 남은 WP-03 수동/runtime 경계를 반영 |
| 1.6 | 2026-08-26 | M2 자동 게이트 완료·WP-03 진행 중 상태, strict MapPackage manifest/실제 collision checksum, 첫 WorldState 일치 후 Reset/Control과 reset 전 일반 control 차단, UE 5.6 빌드, Core 9/7·Should 9/11 재기준선을 반영 |
| 1.5 | 2026-08-26 | 외부 SDK 없이 차량 설정 format v2, CG 기반 차축 위치, 경사 force/moment equilibrium 하중 이동, Ackermann·RWD 차동·타이어 implicit coupling과 20° 등판 회귀를 PHY-005·010·011에 반영 |
| 1.4 | 2026-08-26 | Unreal WorldStatic 충돌의 개발용 MapPackage bake 경로와 UE 5.6 Game·Editor 빌드를 MAP-001·PHY-006 구현 중 범위에 반영; 실제 Landscape PIE는 수동 검증 대기 |
| 1.3 | 2026-08-25 | 외부 차량 설정·checksum, MapPackage triangle ground provider, heave·노면 pitch/roll·경사 주행과 Windows CTest 9/9 반영; 새 지형 pose PIE 확인은 후속 |
| 1.2 | 2026-08-21 | ZMQ 기본 OFF·Python observer 동결과 GroundQuery·1D suspension 기반의 부분 완료·후속 범위를 반영 |
| 1.1 | 2026-08-21 | 좌회전·좌조향 양수 schema v2 exact gate와 C++ 회귀 완료, Unreal scalar 경계 소스 적용 및 Windows 검증 대기 상태 반영 |
| 1.0 | 2026-08-19 | ADR-011 FLU canonical 결정, 좌표·부호 schema와 Unreal adapter, 센서 frame metadata, AT-10 이행 기준 추가 |
| 0.9 | 2026-08-19 | SafeStop session/sequence/queue-age 검증, body Y-left 계약 정합화, 횡하중 이동 회귀 수정, Unreal 입력 coalescing·entity 선택 반영 |
| 0.8 | 2026-08-19 | 8월 27일 Core RC 이후 운전 HUD·4종 카메라·복구 퀵 액션·디버그 오버레이·replay 조작·데모 프리셋 확장 범위 확정 |
| 0.7 | 2026-08-19 | UE 5.6 command FIFO 누적 해결, event-loop service와 20Hz lease heartbeat 완료 기준 반영 |
| 0.6 | 2026-08-19 | Unreal 직접 연결·Envelope·binary transport 완료 상태와 저지연 60Hz 전달·제한 외삽·NFR-012 반영 |
| 0.5 | 2026-08-14 | C++ host의 binary Protobuf ControlCommand 수신과 WorldState broadcast 구현 상태 반영 |
| 0.4 | 2026-08-14 | R1 통신 방식을 WebSocket binary + Protobuf로 확정하고 JSON 제거 방향 반영 |
| 0.3 | 2026-08-14 | PHY-002와 DEC-F02를 자체 C++ 물리엔진 결정으로 변경하고 D1 구현 상태 반영 |
| 0.2 | 2026-08-14 | Chrono::Vehicle 스파이크 결정을 기록; 0.3에서 런타임 채택 철회 |
| 0.1 | 2026-08-14 | R1 수동운전과 R2/R3 자율주행 확장 기능을 최초 분리 |
