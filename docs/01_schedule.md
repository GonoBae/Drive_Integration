# 2026년 8~9월 개발 일정표

## 1. 문서 정보

| 항목 | 값 |
|---|---|
| 버전 | 1.6 |
| 작성일 | 2026-08-19 |
| 최종 수정 | 2026-08-27 |
| 대상 릴리스 | R1 Manual Driving Vertical Slice |
| 기존 목표일 | 핵심 R1 2026-08-27, 확장 포함 2026-08-31(8/19 AI 기준선, 미달성) |
| 현재 관리 목표 | Core RC 2026-09-07(수정 버퍼 9/8), Should 확장 포함 2026-09-11(위험 버퍼 9/14) |
| 개발 인원 | 1명 |
| 기준 작업량 | 1일 8시간 |
| 관련 문서 | [기능표](./02_feature_matrix.md), [아키텍처](./03_architecture.md) |

## 2. 일정 전제

### 2.1 현재 실적과 남은 평일

- 8월 14~19일 D1~D6는 실제 완료한 이력으로 보존한다. 이 구간에는 주말 작업 실적이 포함되어 있다.
- 8월 20일부터는 **주말 8월 22~23일, 8월 29~30일을 작업일에서 제외**한다.
- 8월 20일은 개발하지 못했으며, 해당 작업은 8월 21일로 이월됐다.
- 8월 21일 기준 남은 평일은 당일, 24~28일, 31일의 **최대 7일·56시간**이다.
- D6 현재 기존 R1의 남은 기준 공수는 D7~D18의 12일·96시간이다. 일반 속도로 재배치하면 9월 4일 완료가 예상된다.
- 2026년 8월 26일 종료 기준 실제 상태는 **M2 자동 게이트 완료·Unreal 수동 확인 대기, WP-03 진행 중**이다. WP-03의 자동 구현 범위인 MapPackage 충돌 식별·lifecycle gate, `GroundQuery` adaptive grid와 한정 fallback, strict 정적 collider loader, 8m 결정적 broad phase, 정적 OBB-prism contact, Unreal `ASimCoreStaticCollider` authoring·ground/static 원자적 export, opt-in `--demo-entities` NPC·보행자 lifecycle·`WorldState`·Unreal 표시까지 구현했다. 다만 현재 `landscape_local_v1`은 marker가 bake되지 않아 `static_colliders=0`이며 실제 PIE 벽·동적 충돌 확인은 남아 있으므로 WP-03 전체는 완료가 아니다.

### 2.2 이전 AI 협업 일정 전제(이력)

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
현재 잔여량은 아래 2.3의 **38~55시간** 기준으로 대체한다.

### 2.3 2026-08-26 AI 협업 재기준선

AI가 독립적인 코드·시험·문서 작업을 2~3개 흐름으로 병렬 처리하는 것을 전제로 한다. 다만 공통 schema 확정, 동일 파일 통합, Unreal 빌드, PIE 시각 확인, 목표 PC 성능·30분 안정성 시험은 순차 게이트로 남는다. 현재 코드에는 LaneGraph, Traffic/Pedestrian, SensorRig, Recorder/Replay 완성 구현이 없으므로 이를 단순 마감 작업으로 계산하지 않는다.

| 범위 | AI 협업 잔여 공수 | 관리 완료일 | 위험 버퍼 | 완료 기준 |
|---|---:|---|---|---|
| Must/Core RC | 38~55h(병렬화 전 합산) | 2026-09-07 | 2026-09-08 | 모든 R1 Must, AT-01~10, 1080p 성능, 30분 안정성, 패키지 후보 |
| Should 확장 | 15~21h | 2026-09-11 | 2026-09-14 | Core RC를 유지하면서 운전 UX·4종 카메라·디버그·replay UI·데모 프리셋 통과 |

Core 기능 구현의 무결점 최단 후보는 9월 4일이지만, 그날은 자동 회귀 후보일이지 완료 약속일이 아니다. 사용자 PIE와 목표 PC 인수 시험을 포함한 관리 목표는 9월 7일로 둔다. 9월 8일은 Must 수정만 수행하는 버퍼이며, 이 버퍼를 사용하면 Should 최종 회귀가 9월 14일까지 이동할 수 있다.

잔여 작업의 우선순위와 의존성은 다음과 같다.

| 우선순위 | 잔여 패키지 | AI 협업 공수 | 선행 조건 | Codex 단독 진행 | 사용자 게이트 |
|---:|---|---:|---|---|---|
| 완료 | WP-03 자동 구현 범위 | 0h 잔여 | 없음 | checksum·lifecycle gate, adaptive ground index, strict loader, SAT·microstep·impulse, 8m broad phase, `ASimCoreStaticCollider` export, opt-in demo entity lifecycle·WorldState·UE 표시 구현; C++ 11/11·핵심 4종×20회·UE Editor build·runtime smoke 통과 | 실제 PIE는 아래 수동 게이트 |
| 0 | 최신 차량 물리·Landscape M2 PIE 확인 | 1h | 현재 빌드 | 자동 회귀·실행 준비 | 등판·선회 감각 20~30분 |
| 1 | WP-03 수동 게이트: marker bake·PIE 정적/동적 충돌 | 1~2h | 현재 자동 구현·최종 빌드 | 실행 절차·로그 진단 준비 | marker 배치 후 `static_colliders>0`, checksum gate, 벽·커브·demo entity 충돌 30~60분 |
| 2 | GeoTransform·quaternion·핵심 HUD·외부 실행 설정 | 5~7h | handshake | adapter·설정·자동 시험 | 좌표·표시 30분 |
| 3 | Wall/Broad 로컬 도로·LaneGraph | 9~13h | MapPackage 포맷 | importer·graph·검증 | 지도 범위·정렬·에셋 1~2h |
| 4 | 신호·LaneGraph 기반 NPC·보행자 동작 | 6~8h | LaneGraph, 구현된 demo runtime 경로 | 상태기계·intent·경로 추종 | 목표 개체 수와 신호 반복 동작 45~60분 |
| 5 | SensorRig 골격·기록·결정적 재생 | 7~9h | 안정된 schema/state | config·recorder·replay 시험 | replay 확인 30분 |
| 6 | 패키징·성능·30분 안정성·전체 인수·수정 | 9~15h | 모든 Must | 빌드·측정 도구·문서 | 목표 PC 60~90분 |

정확한 Wall/Broad 경계·주행 루프와 라이선스 확인 에셋이 9월 1일까지 정해지지 않으면 기능형 Core 후보는 만들 수 있어도 전체 R1 완료 선언은 해당 입력이 제공된 평일 수만큼 이동한다.

### 2.4 외부 전제

- Unreal Engine과 Cesium for Unreal의 사용할 버전을 첫날 확정한다.
- 차량, 사람, 신호등, 주요 건물은 라이선스가 확인된 기존 에셋을 사용한다.
- NYSE나 Federal Hall을 처음부터 직접 모델링하는 시간은 포함하지 않는다.
- 개발 및 최종 실행은 Windows 기준으로 검증한다.
- 최종 성능 검증은 i5-10400, RTX 2060 장비에서 수행한다.
- 8월에는 실제 차량 계측 데이터 기반의 정밀 파라미터 식별과 자율주행 학습을 하지 않는다.

## 3. 작업 패키지

아래 144시간은 AI 적용 전 기존 R1의 **기준 공수**다. D6 이후 남은 96시간을 AI 협업 48시간으로 압축하되 산출물과 종료 조건은 유지한다.

| ID | 작업 패키지 | 시간 | 산출물 | 종료 조건 |
|---|---|---:|---|---|
| WP-00 | 물리 구현 전략·기준 스파이크 | 8h | 자체 구현 결정, 후보 비교 결과, 기준 샘플 | 자체 물리 경계와 단계별 검증 기준이 기록되고 개발기 기본 모델 시험 통과 |
| WP-01 | 좌표·시간·프로토콜 기반 | 16h | ENU 좌표 변환, 고정 tick, 공통 Proto 초안 | 렌더 FPS와 무관한 고정 tick 및 메시지 버전 검증 |
| WP-02 | C++ 차량 동역학 | 24h | 차체, 휠, 조향, 제동, 구동계, 타이어·서스펜션 | 평면과 경사면에서 제어 가능하고 기본 물리 시험 통과 |
| WP-03 | 공통 충돌 월드 | 20h | MapPackage 충돌 소스, 정적·동적 충돌 로더 | C++가 커브·벽·차량·보행자 프록시 충돌을 최종 해결 |
| WP-04 | Unreal 수동운전 연결 | 20h | SimCoreClient, ExternalVehiclePawn, 입력·보간 | Python 없이 Unreal↔C++ 수동운전 왕복 성공 |
| WP-05 | Wall/Broad 지도·차선 | 20h | LaneGraph, 로컬 도로·보도·충돌, 제한된 배경 | 주행 루프와 보행 중심 구역이 시각·기능적으로 구분됨 |
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

### 4.2 8월 26일 재산정 실행 계획(AI 병렬 작업·주말 제외)

| 날짜 | 우선 작업 | Codex 산출물 | 사용자 확인·종료 조건 |
|---|---|---|---|
| 8/27 목 | **WP-03 자동 구현·최종 자동 검증 선행 달성**; 4-corner 차량 자세 긴급 보완 완료 | 네 독립 suspension reaction·wheel-local tangent·finite-angle pitch/roll, UE roll 부호, 1~3 ray partial support·centre/0-ray fail-closed, `Fit Sampling Bounds To Ground Actor`; C++ CTest 11/11·차량/설정 각 20/20·UE 5.6 Editor build·runtime smoke 통과 | Fit 적용→재-bake→서버 재시작 후 실제 PIE에서 차체 pitch/roll 방향, 기존 각도 경계 통과, hard-stop·20° 등판 확인; marker 배치·정적/동적 충돌은 계속 수동 gate |
| 8/28 금 | WP-03 수동 충돌 gate·실패 보완 | `static_colliders>0` package 실행 로그와 정적/동적 collision 진단 | 실제 PIE 벽·커브 비관통과 `--demo-entities` NPC·보행자 표시·충돌 확인 |
| 8/29~30 | 주말 | 작업 없음 | 일정·완료일 산정에서 제외 |
| 8/31 월 | WP-03 수동 gate 보완, C++/Unreal 충돌 통합 마감 | 수동 결과에 따른 blocker 수정과 authoritative 표시 경계 | 벽·커브·동적 entity 반복 충돌에서 지속 관통 없음 확인; M3 후보 |
| 9/1 화 | GeoTransform·quaternion·핵심 HUD·외부 설정, LaneGraph importer 병렬 작업 | 좌표 왕복·packet/checksum/SafeStop 표시, MapPackage lane schema | Wall/Broad 경계·주행 루프와 사용 에셋·라이선스 확정 |
| 9/2 수 | Wall/Broad 로컬 도로·LaneGraph | 방향성 lane edge, 교차로 연결, 통행 제한·시각 정렬 | 핵심 구간 정렬과 보행 중심 구역 진입 제한 확인; M4 |
| 9/3 목 | 신호·TrafficDirector·NPC·보행자·동적 proxy | 신호 1개, NPC 3~4대, 보행자 6~8명의 반복 상태 | 신호 준수와 화면/충돌 entity 일치 확인 |
| 9/4 금 | SensorRig·기록·결정적 재생·전체 자동 회귀 | frame metadata, input/state replay, AT 자동 후보 | 자동 게이트 통과 시 Core 기능 후보이며 완료 선언은 보류; M5 |
| 9/5~6 | 주말 | 작업 없음 | 일정·완료일 산정에서 제외 |
| 9/7 월 | WP-08/09 패키징·성능·안정성·인수 시험 | 패키지 후보, 1080p·지연 측정, 알려진 문제·문서 | 목표 PC 30분 주행과 AT-01~10 통과 시 **Core RC** |
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
| M3 충돌 일치 수동운전 | 8/31 | 동일 충돌 소스와 체크섬, 지속 관통 없음, Unreal 임의 보정 없음, FLU↔FRU 자세·회전 시각 검증, 재연결·SafeStop 표시 | NPC 범위를 보류하고 정적 충돌·좌표 adapter부터 수정 |
| M4 Wall/Broad 주행 루프 | 9/2 | 로컬 주행면과 차선 그래프가 정렬되고 보행 구역 진입 제한 | Cesium 배경보다 로컬 주행면 정확도를 우선 |
| M5 도시 동작·기록 골격 | 9/4 | 신호 1개, NPC 3~4대, 보행자 6~8명 반복 동작, 기록·재생 데이터 생성 | 개체 수만 낮출 수 있으며 신호 준수와 기록은 유지 |
| M6 핵심 R1 Core RC | 9/7(9/8 수정 버퍼) | 기능표의 모든 R1 Must와 1080p 성능·30분 안정성·패키지 후보 통과 | 추가 기능을 시작하지 않고 Must 게이트 복구에 전력 사용 |
| M6.5 확장 기능 후보 | 9/10 | 운전 UX·카메라·디버그·replay·데모 프리셋이 Core RC 회귀를 통과 | 실패한 Should 확장만 제외하고 Core RC 유지 |
| M7 확장 포함 R1 릴리스 | 9/11(위험 버퍼 9/14) | 최종 패키지, 시험 기록, replay, 영상, 알려진 문제, 문서 확보 | 완료되지 않은 Must가 있으면 완료 선언 대신 일정 변경 |

### 5.1 기존 계획과 수정 계획의 완료 기준

| 구분 | 완료일 | 완성 기준 기능 |
|---|---|---|
| 기존 범위·일반 속도 재예측(8/19 이력) | 2026-09-04 | 자체 C++ 차량 물리·서스펜션·충돌, Unreal 직접 수동운전, Wall/Broad MapPackage·LaneGraph, 신호·NPC·보행자, 센서 골격, 기록·재생, 1080p 성능, 30분 안정성, 패키지·문서 |
| 이전 AI 핵심 R1 기준선(8/19, 미달성) | 2026-08-27 | 기존 계획과 **동일한 Must 기능과 인수 시험**을 Core RC로 완료하며 범위를 줄이지 않음 |
| 이전 AI 확장 R1 기준선(8/19, 미달성) | 2026-08-31 | 핵심 R1 + 운전 HUD·4종 카메라·복구 조작·디버그·replay UI·데모 프리셋·최종 영상·문서 |
| **현재 AI 핵심 R1 관리 계획** | **2026-09-07, 수정 버퍼 9/8** | 기존 계획과 동일한 Must 기능, AT-01~10, 1080p 성능·30분 안정성·패키지 후보를 통과한 Core RC |
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
| 현재 게이트 | UE 5.6 Game·Editor 빌드는 통과; 실제 Landscape export→서버 재시작→PIE 요철 주행 확인 대기 |
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
- Google Photorealistic Tiles 최적화에 별도 시간을 사용하지 않는다.
- 라이선스가 불분명한 지도·건물·차량 에셋은 릴리스 패키지에 포함하지 않는다.
- 기능을 줄여야 할 경우 Could, Should 순으로 조정한다. Must 항목을 제거하려면 사용자 합의와 일정·기능표 변경이 필요하다.

## 8. 주요 일정 위험

| 위험 | 조기 경보 | 대응 |
|---|---|---|
| 자체 물리 구현 지연 | D6까지 타이어·휠 접촉 시험 미통과 | 실패 수식과 기준 시험부터 수정하고 지도·NPC 작업 확대 금지 |
| 주요 에셋 미확보 | D8까지 라이선스 확인 에셋 없음 | 기능 검증용 대체 에셋 사용, 시각 완료일 별도 표시 |
| 도로 데이터와 시각 도로 오프셋 | D12 import 후 0.5m 이상 반복 오차 | 원점·투영 확인 후 핵심 구간만 수동 보정 |
| C++/Unreal 충돌 불일치 | 체크섬 불일치 또는 Unreal 위치 보정 발생 | 실행 차단, 공통 소스 재생성, 권한 경로 점검 |
| RTX 2060 성능 부족 | D16 p95 frame time 목표 초과 | Cesium 배경 제거/거리 축소, 그림자·반사·NPC LOD 순 최적화 |
| 통신 지연·끊김 | 입력 age 또는 seq gap 증가 | timeout 안전 정지, 상태 버퍼·재연결, 고대역 센서 채널 분리 |
| AI 병렬 통합 충돌 | 공통 schema·동일 파일의 동시 변경 또는 UE 빌드 대기 증가 | 패키지 경계를 나누고 schema·통합 빌드·PIE 게이트는 한 흐름에서 순차 검증 |
| 지도 범위·에셋 결정 지연 | 9/1까지 Wall/Broad 경계·주행 루프·라이선스가 미확정 | 기능형 placeholder와 릴리스 완료를 분리하고 입력 지연 평일만큼 완료일 이동 |

## 9. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
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
