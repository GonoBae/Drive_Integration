# 기능표

## 1. 문서 정보

| 항목 | 값 |
|---|---|
| 버전 | 0.5 |
| 작성일 | 2026-08-14 |
| R1 | 2026-08-31 수동운전 버티컬 슬라이스 |
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
| 구현 중 | 일부 코드와 시험이 존재하지만 R1 완료 기준은 아직 미충족 |
| 실험 | 정해진 스파이크 결과로 상세 구현을 확정 |
| 제안 | 사용자 확인이 필요한 항목 |
| 연기 | R1에서 구현하지 않음 |

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

## 4. 기능 상세

### 4.1 지도와 환경

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| MAP-001 | Must | 결정 | Wall/Broad 범위 MapPackage | 원점, 버전, 체크섬, 차선, 충돌, 신호, 스폰 데이터가 한 패키지로 로드됨 | 다른 도시·트랙도 같은 포맷 사용 |
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
| PHY-003 | Must | 구현 중 | 로컬 ENU 물리 좌표 | 위·경도를 직접 적분하지 않고 meter 단위 ENU에서 계산 | GNSS 변환과 대규모 월드 대응 |
| PHY-004 | Must | 결정 | 고정 시뮬레이션 tick | 렌더 FPS 변화와 무관하게 기본 60Hz로 진행; substep 설정 가능 | headless·재생·학습에 재사용 |
| PHY-005 | Must | 구현 중 | 기본 차량 동역학 | 현재 종방향 힘·기어·bicycle 모델에서 6DoF, 바퀴별 타이어, 서스펜션까지 확장 | 차량 설정 교체로 다른 차종 지원 |
| PHY-006 | Must | 결정 | 정적 충돌 | 지면, 커브, 벽과 지속 관통하지 않고 C++에서 접촉 해결 | 시나리오 지도 공통 |
| PHY-007 | Must | 결정 | 동적 충돌 프록시 | NPC OBB와 보행자 capsule이 같은 tick 기준으로 충돌 월드에 존재 | 향후 다중 에이전트 |
| PHY-008 | Must | 결정 | 노면 재질·마찰 | MapPackage의 표면 ID로 마찰 파라미터를 선택 | 젖은 노면 등 시나리오 확장 |
| PHY-009 | Must | 구현 중 | 입력 안전장치 | 입력 범위 clamp, 250ms 기본 timeout, 연결 중단 시 감속·정지 | 자율주행 fail-safe |
| PHY-010 | Must | 결정 | 물리 파라미터 파일화 | 차량 수치가 코드가 아닌 버전 관리 설정 파일에 존재 | 차량 교체·튜닝·시험 |
| PHY-011 | Must | 구현 중 | 물리 회귀 시험 | 직진, 정지, 회전, 경사, 충돌, replay 시험 자동 실행 | 수식·파라미터 변경 검증 |
| PHY-012 | Future | 연기 | 실차 파라미터 동정 | 대상 차량 계측 데이터와 기준 주행에 맞춰 오차 검증 | 차량별 현실성 향상 |

### 4.3 통신과 동기화

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| NET-001 | Must | 구현 중 | Unreal↔C++ 직접 연결 | Python relay 없이 명령과 상태가 양방향 전달됨 | 지연과 장애 지점 감소 |
| NET-002 | Must | 구현 중 | 단일 공통 Protobuf 스키마 | C++·Python 생성물이 루트 `protocol/`의 하나의 원본에서 생성되고 Unreal 생성 대기 | 스키마 중복 제거 |
| NET-003 | Must | 구현 중 | 메시지 Envelope | schema version, sequence, simulation time, source, map checksum 포함 | 기록·재생·오류 진단 |
| NET-004 | Must | 결정 | 재연결·중복·순서 처리 | 오래되거나 중복된 command를 버리고 재연결 후 handshake 수행 | 네트워크 견고성 |
| NET-005 | Must | 결정 | MapPackage handshake | Unreal과 C++ 체크섬이 다르면 주행 시작을 거부하고 이유 표시 | 충돌 불일치 방지 |
| NET-006 | Must | 구현 중 | WebSocket binary + Protobuf transport | JSON 없이 ControlCommand와 WorldState가 C++↔Unreal 사이에서 전이중 전달됨 | 이후 측정 결과에 따라 UDP/IPC로 교체 가능 |
| NET-007 | Future | 연기 | 고대역 센서 transport | 이미지·LiDAR는 control/state와 분리된 shared memory/전용 채널 사용 | FSD 처리량 확보 |

### 4.4 Unreal IG와 수동운전

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| UE-001 | Must | 구현 중 | ManualInputComponent | 키보드 또는 게임패드 입력을 정규화된 ControlCommand로 전송 | AI 명령과 같은 포맷 사용 |
| UE-002 | Must | 구현 중 | ExternalVehiclePawn | C++ pose와 wheel state를 표시하고 Ego Chaos 동역학은 비활성 | 외부 물리 엔티티 공통 기반 |
| UE-003 | Must | 결정 | 상태 보간 | 상태 패킷 사이를 렌더링 보간하고 제한 없이 장시간 외삽하지 않음 | 네트워크 지연 완화 |
| UE-004 | Must | 결정 | 좌표 변환 어댑터 | ENU pose를 Cesium/Unreal transform으로 일관되게 변환 | 지도 원점 변경 대응 |
| UE-005 | Must | 결정 | 운전자 카메라와 외부 카메라 | 운전·검증·영상 촬영에 필요한 고정 camera rig 제공 | 센서와 촬영 분리 |
| UE-006 | Must | 결정 | 디버그 HUD | 연결, sim tick, packet age, 속도, map checksum, collision 상태 표시 | 통합 문제 진단 |
| UE-007 | Should | 제안 | 충돌·차선 디버그 오버레이 | C++ 충돌 proxy와 LaneGraph를 Unreal 화면에서 켜고 끌 수 있음 | 지도 보정과 FSD 디버깅 |
| UE-008 | Must | 결정 | 패키지 실행 설정 | 개발 PC가 아닌 목표 Windows PC에서 서버 주소와 지도 설정을 외부 파일로 지정 | 배포 재사용 |

### 4.5 신호, 차량 AI, 보행자

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| TRA-001 | Must | 결정 | 신호 상태기계 | 최소 1개 교차로가 설정된 주기로 차량·보행 신호를 전환 | 지도 데이터로 교차로 추가 |
| TRA-002 | Must | 결정 | TrafficDirector | 차선 추종, 목표 속도, 신호 정지 의도를 생성 | C++ 물리와 분리된 rule AI |
| TRA-003 | Must | 결정 | NPC 차량 3~4대 | LaneGraph를 따라가고 신호와 선행 차량에 정지 | 자율주행 상호작용 대상 |
| TRA-004 | Must | 결정 | 보행자 6~8명 | 정해진 경로를 걷고 보행 신호에 따라 횡단·대기 | 센서와 위험 시나리오 대상 |
| TRA-005 | Must | 결정 | 동적 엔티티 물리 상태 | AI는 intent를 만들고 C++가 충돌에 쓰는 최종 상태를 계산 | C++/Unreal 불일치 제거 |
| TRA-006 | Future | 연기 | 군중 시뮬레이션 | 대규모 보행자 회피·밀도 모델 | R1 제외 |
| TRA-007 | Future | 연기 | 사고·파손 | 차량 변형, 파편, 상세 충격 | R1 제외 |

### 4.6 센서와 기록

| ID | 우선순위 | 상태 | 기능 | R1 완료 기준 | 향후 재사용 |
|---|---|---|---|---|---|
| SEN-001 | Must | 결정 | SensorRig와 장착 좌표 | 센서 이름, 차체 기준 transform, 주기, 활성 상태를 설정 파일로 정의 | 카메라·LiDAR·Radar 공통 |
| SEN-002 | Must | 결정 | 센서 시간·좌표 메타데이터 | 모든 센서 메시지가 sim time, frame ID, pose, sequence를 가짐 | 다중 센서 동기화 |
| SEN-003 | Should | 제안 | 전방 RGB 카메라 smoke test | 낮은 주기로 프레임과 메타데이터 1개를 기록 | 향후 인지 입력 |
| SEN-004 | Future | 연기 | GNSS·IMU 모델 | noise/bias가 구성 가능한 GNSS·IMU 생성 | localization 학습·평가 |
| SEN-005 | Future | 연기 | LiDAR·Radar·분할 카메라 | 개별 주기와 좌표계로 데이터 생성 | perception 입력 |
| REC-001 | Must | 결정 | 주행 기록 | 명령, 상태, 이벤트, map·vehicle config checksum 저장 | 회귀 시험과 데이터셋 |
| REC-002 | Must | 결정 | 결정적 재생 | 동일 빌드·설정에서 최종 pose 오차가 허용치 안에 있음 | 버그 재현과 영상 촬영 |
| REC-003 | Should | 결정 | replay 기반 영상 | 실시간 플레이와 촬영을 분리해 동일 주행을 재생 가능 | Movie Render Queue 촬영 |

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
| NFR-004 | Must | 좌표 일치 | 기준점 왕복 변환 오차 5cm 이하; 단위·축 변환 자동 시험 |
| NFR-005 | Must | 표시 일치 | 정상 연결 상태에서 Unreal 표시 pose와 최신 C++ 기준 pose 차이 5cm/0.5도 이하 |
| NFR-006 | Must | 충돌 일치 | 동일 collision source checksum을 사용하며 지속 관통이 없음 |
| NFR-007 | Must | 재현성 | 같은 빌드·설정·입력 replay의 최종 위치 1cm, yaw 0.1도 이내 |
| NFR-008 | Must | 설정 가능성 | 서버 주소, 지도, 차량, tick, 센서 장착값을 재컴파일 없이 변경 |
| NFR-009 | Must | 관측 가능성 | tick overrun, packet age, sequence gap, collision, reconnect를 구조화 로그로 기록 |
| NFR-010 | Must | 라이선스 | 지도와 배포 에셋의 출처·라이선스·attribution 기록 |
| NFR-011 | Should | CI 회귀 | C++ 단위 시험과 Proto/MapPackage schema 검증을 명령 하나로 실행 |

## 6. R1 인수 시험

| 시험 ID | 시나리오 | 성공 조건 | 관련 기능 |
|---|---|---|---|
| AT-01 | 서버 실행 및 지도 handshake | 동일 체크섬일 때 시작, 불일치일 때 명확히 거부 | MAP-001, NET-005 |
| AT-02 | 수동 가속·제동·조향 | 입력이 C++ 고정 tick에 반영되고 Unreal에 표시 | PHY-004~005, UE-001~003 |
| AT-03 | 커브·벽 충돌 | 지속 관통하지 않고 C++ 결과와 Unreal 표시가 일치 | PHY-006, NFR-006 |
| AT-04 | NPC·보행자 상호작용 | 동적 proxy가 충돌 월드와 화면에서 같은 엔티티를 나타냄 | PHY-007, TRA-003~005 |
| AT-05 | 신호 동작 | 차량과 보행자가 자신의 신호에 맞춰 정지·이동 | TRA-001~004 |
| AT-06 | 입력 연결 중단 | 250ms 후 안전 정지하고 HUD와 로그에 원인이 표시 | PHY-009, NFR-003 |
| AT-07 | 기록·재생 | 같은 기록을 재생해 NFR-007 오차 이내 도착 | REC-001~002 |
| AT-08 | 장시간·성능 | 목표 PC에서 30분 안정성과 NFR-001 측정값 기록 | NFR-001~002 |
| AT-09 | 촬영 | replay를 카메라 변경 후 재생하고 영상 출력 가능 | UE-005, REC-003 |

## 7. 결정 입력 및 상태

| 결정 ID | 내용 | 결정 시점 | 결정하지 않을 때 영향 |
|---|---|---|---|
| DEC-F01 | 기준 Unreal Engine·Cesium 버전 | D1 | 플러그인·빌드 재작업 가능 |
| DEC-F02 | **완료: 현재 C++ 서버에 자체 차량 물리 구현** ([ADR-006](./decisions/ADR-006-custom-vehicle-physics.md)) | 2026-08-14 결정 | 구현량·검증 책임이 증가하므로 단계별 시험 실패 시 후속 일정 재검토 |
| DEC-F03 | **완료: R1 통신은 WebSocket binary + Protobuf** ([ADR-005](./decisions/ADR-005-realtime-transport-protocol.md)) | 2026-08-14 결정 | C++ JSON 입력 파서는 제거, UDP는 측정 후 재검토 |
| DEC-F04 | 대상 차량 종류와 기본 제원 | D3 | 물리는 동작하지만 현실성 검증 기준이 불명확 |
| DEC-F05 | Wall/Broad 정확한 지도 경계와 주행 루프 | D8 이전 | LaneGraph와 환경 범위 변동 |
| DEC-F06 | 사용할 건물·차량·보행자 에셋과 라이선스 | D8 이전 | 영상 품질 또는 배포 가능성 저하 |
| DEC-F07 | 주말·공휴일 포함 실제 작업 가능 여부 | 즉시 | 8월 31일 또는 9월 9일로 완료일 변동 |

## 8. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| 0.5 | 2026-08-14 | C++ host의 binary Protobuf ControlCommand 수신과 WorldState broadcast 구현 상태 반영 |
| 0.4 | 2026-08-14 | R1 통신 방식을 WebSocket binary + Protobuf로 확정하고 JSON 제거 방향 반영 |
| 0.3 | 2026-08-14 | PHY-002와 DEC-F02를 자체 C++ 물리엔진 결정으로 변경하고 D1 구현 상태 반영 |
| 0.2 | 2026-08-14 | Chrono::Vehicle 스파이크 결정을 기록; 0.3에서 런타임 채택 철회 |
| 0.1 | 2026-08-14 | R1 수동운전과 R2/R3 자율주행 확장 기능을 최초 분리 |
