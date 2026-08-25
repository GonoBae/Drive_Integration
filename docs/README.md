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
| [ADR-011](./decisions/ADR-011-canonical-coordinate-frames.md) | 공통 좌표계 결정 | ROS 호환 FLU canonical frame, Unreal FRU 경계 변환, 남은 이행 작업 |
| [ADR 목록](./decisions/README.md) | 기술 결정 색인 | 현재 ADR과 후속 번호 관리 |
| [학습 센터](./study/README.md) | 교육 문서의 단일 입구 | 일정, 학습자료, 질문 및 답변의 세 문서로 안내 |
| [2026-08-14 작업일지](./worklogs/2026-08-14.md) | 일별 실행 기록 | D1 구현, 검증 결과, 남은 위험과 다음 작업 |
| [2026-08-19 작업일지](./worklogs/2026-08-19.md) | 일별 실행 기록 | D6 4륜 물리·Unreal 통합, 지연 계측, 좌표계 결정, 전체 코드 리팩터링과 재검증 결과 |
| [UE 5.6 WebSocket 입력 지연 해결 사례](./troubleshooting/ue56-websocket-growing-input-delay.md) | 문제 해결 기록 | 60Hz producer/30Hz consumer FIFO 누적, event-loop·20Hz heartbeat 수정과 진단 기준 |

## 현재 합의된 방향

- 개발 인원: 1명
- 작업 시간: 하루 8시간
- 기존 목표일: 2026년 8월 31일, 8월 20일 미작업 반영 후 재산정 대기
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
- 문서 버전: 1.1
- 프로젝트 상태: Unreal↔C++ 직접 수동입력과 schema-v2 좌회전·좌조향 C++/Unreal 경계, `GroundQuery`·기본 평지·1D suspension 기반까지 구현; MapPackage terrain provider·차체 6DoF·전체 GeoTransform·센서 frame·Windows UE 재검증은 후속 추적
- 기준 저장소: `Drive_Integration`
