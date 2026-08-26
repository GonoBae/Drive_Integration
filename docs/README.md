# Drive Integration 문서

이 디렉터리는 Drive Integration 프로젝트의 계획과 기술 기준을 관리한다. 2026년 8월 1차 릴리스의 목표는 **Wall Street·Broad Street 주변에서 외부 C++ 물리로 동작하는 수동운전 버티컬 슬라이스**다. 자율주행은 같은 환경과 인터페이스를 재사용하되 8월 범위에는 포함하지 않는다.

## 문서 구성

| 문서 | 목적 | 주요 내용 |
|---|---|---|
| [01_schedule.md](./01_schedule.md) | 일정과 작업량 관리 | 일별 일정, 마일스톤, 완료 게이트, 지연 대응 |
| [02_feature_matrix.md](./02_feature_matrix.md) | 범위와 완료 조건 관리 | 8월 기능, 향후 자율주행 기능, 기능별 검증 기준 |
| [03_architecture.md](./03_architecture.md) | 시스템 설계 관리 | C++·Unreal·Python 책임, 지도·충돌·통신·센서 구조 |
| [ADR-005](./decisions/ADR-005-realtime-transport-protocol.md) | 실시간 통신 결정 | WebSocket binary + Protobuf, JSON runtime 제거, UDP 재검토 조건 |
| [ADR-006](./decisions/ADR-006-custom-vehicle-physics.md) | 자체 차량 물리 결정 | C++ 직접 구현 근거, 단계별 범위, 비교 시험과 재검토 조건 |
| [ADR-010](./decisions/ADR-010-low-latency-control-presentation.md) | 저지연 제어 결정 | 입력 coalescing, 2단계 control lease, 제한된 상태 예측과 재검토 조건 |
| [ADR-011](./decisions/ADR-011-canonical-coordinate-frames.md) | 공통 좌표계 결정 | ROS 호환 FLU canonical frame, Unreal FRU 경계 변환, 남은 이행 작업 |
| [ADR 목록](./decisions/README.md) | 기술 결정 색인 | 현재 ADR과 후속 번호 관리 |
| [학습 센터](./study/README.md) | 교육 문서의 단일 입구 | 일정, 학습자료, 질문 및 답변의 세 문서로 안내 |
| [2026-08-14 작업일지](./worklogs/2026-08-14.md) | 일별 실행 기록 | D1 구현, 검증 결과, 남은 위험과 다음 작업 |
| [2026-08-19 작업일지](./worklogs/2026-08-19.md) | 일별 실행 기록 | D6 4륜 물리·Unreal 통합, 지연 계측, 좌표계 결정, 전체 코드 리팩터링과 재검증 결과 |
| [2026-08-25 작업일지](./worklogs/2026-08-25.md) | 일별 실행 기록 | AI-D7 보충 실행, 외부 차량 설정, MapPackage 지면, heave·경사 자세와 Windows 회귀 결과 |
| [2026-08-26 작업일지](./worklogs/2026-08-26.md) | 일별 실행 기록 | 급경사·등판 보완, collision checksum/lifecycle, 지면·충돌 index, 정적 authoring/export와 demo runtime entity 통합 |
| [2026-08-27 작업일지](./worklogs/2026-08-27.md) | 일별 실행 기록 | 2단계 lease·background log backpressure, 조작감, 자동 후진, 독립 4-corner suspension·차체 자세, UE 부호·MapPackage 경계 보완 |
| [UE 5.6 WebSocket 입력 지연 해결 사례](./troubleshooting/ue56-websocket-growing-input-delay.md) | 문제 해결 기록 | 60Hz producer/30Hz consumer FIFO 누적, event-loop·20Hz heartbeat 수정과 진단 기준 |
| [UE 5.6 control lease timeout 해결 사례](./troubleshooting/ue56-control-lease-timeout-log-backpressure.md) | 문제 해결 기록 | single-thread stderr backpressure, background 파일 로그와 250ms/1초 2단계 lease |
| [Landscape 급경사 접촉 상실 해결 사례](./troubleshooting/ue-landscape-steep-grade-contact-loss.md) | 문제 해결 기록 | stale MapPackage 판별, 절대 자세 제한·downward ray 영구 낙하 수정과 실제 bake 재시험 |

## 현재 합의된 방향

- 개발 인원: 1명
- 작업 시간: 하루 8시간
- 이전 AI 기준선: 핵심 2026년 8월 27일·확장 8월 31일(미달성 이력)
- 현재 관리 목표: Core RC 2026년 9월 7일(수정 버퍼 9월 8일), Should 확장 9월 11일(위험 버퍼 9월 14일)
- 배경 범위: Manhattan Downtown의 Wall Street·Broad Street 중심 구역
- Wall Street·Broad Street 핵심부: 실제 특성을 반영해 보행 중심으로 유지
- 주행: 주변 차량 통행 도로에 제한된 주행 루프 구성
- 물리 권한: C++ SimCore
- 차량 물리: 현재 C++ 서버에서 직접 개발; Chrono·PhysX·Chaos는 비교 기준으로만 사용
- 실시간 통신: R1은 WebSocket binary + Protobuf, JSON runtime protocol은 제거
- 공통 좌표: ROS 호환 right-handed FLU; Unreal의 left-handed FRU는 경계 adapter에서만 변환
- Unreal 역할: 입력, IG(영상 생성), UI, 센서, 에이전트 표현
- Python 역할: 향후 자율주행 판단; 기존 relay/ZMQ observer는 default-OFF로 동결하고 수동운전 필수 경로와 R1 검증에서 제외
- 차량 사고 파손 및 변형: 제외
- 보행자 군중 시뮬레이션: 제외; 단순 보행과 신호 준수만 구현
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
- 문서 버전: 1.9
- 프로젝트 상태: Unreal↔C++ schema-v2 수동입력, 차량 설정 format v4, 250ms soft SafeStop/1초 hard reconnect lease, MapPackage 실제 collision checksum과 reset gate, adaptive ground index, 8m deterministic collision broad phase, 정적 OBB 및 NPC OBB·보행자 capsule core, Unreal `ASimCoreStaticCollider` ground/static 원자적 export, opt-in demo entity lifecycle·`WorldState`·표시까지 자동 구현 완료. 자동 후진·차속 연동 표시 휠·저속 traction 보완에 더해 네 독립 wheel spring/damper 반력, wheel-local tangent force, sprung-body heave·finite-angle pitch/roll, UE roll 부호와 1~3 ray partial support 경계를 반영했다. 최신 C++ Release CTest 11/11, `vehicle_physics_tests`·`vehicle_config_tests` 각각 20/20 반복, UE 5.6 Editor build와 기존 tracked package runtime smoke는 통과했다. 다만 현재 모델은 massless hub·1D suspension 기반 reduced-order 구조이고, `Fit Sampling Bounds To Ground Actor` 적용·재-bake·서버 재시작 뒤 최신 자세와 경계를 확인하는 실제 Landscape PIE 주행은 대기 상태다. tracked Landscape도 `static_colliders=0`이므로 marker bake와 실제 PIE 정적·동적 충돌 전까지 WP-03과 관련 인수 항목은 진행 중이다. LaneGraph·신호 기반 traffic, 센서 frame·record/replay, 성능·30분 안정성은 후속 추적한다.
- 기준 저장소: `Drive_Integration`
