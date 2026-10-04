# Core 자동 통합·재생·성능 검증

현재 코드의 자동 검증 절차입니다. **자동 통과와 실제 주행 인수 완료는 다릅니다.**
운전 감각·화면/충돌 일치·패키지 성능과 30분 주행은 별도로 확인합니다.
이미 수행한 결과와 남은 항목은 [현재 상태](../status.md)에 모았습니다.

![자동 검사, 실제 Play 확인, 배포와 장시간 측정을 구분한 검증 단계](../images/validation-gates.svg)

읽는 순서: [자동 검사](#한-번에-자동-검증) → [실시간 지면 비교](#실시간-지면-측정-비교) → [재생 비교](#입력-기반-물리-재생) →
[성능 캡처](#성능-캡처) → [연구 실험](#연구-실험-절차).

## 한 번에 자동 검증

Unreal Editor를 종료하고 저장되지 않은 작업이 없는지 확인한다. 표준 Release 실행 파일을
재빌드하므로 해당 실행 파일로 실행 중인 서버도 먼저 종료한다. 스크립트는 기존 서버나
사용자 Editor 프로세스를 강제 종료하지 않는다.

```powershell
Set-Location 'B:\Portfolio\Drive_Integration'
python scripts/check_core.py --engine-root 'B:\Epic Games\UE_5.6'
```

현재 Python에 `grpcio-tools`, `protobuf`, `websockets`, PATH에 CMake/CTest와 Windows
PowerShell이 필요하다. C++의 프로젝트 로컬 vcpkg와 UE 5.6 설치는 기존 빌드 환경을 사용한다.
다른 PC에서는 프로젝트 경로와 `--engine-root`만 실제 위치로 바꾼다.

Python은 개발 검사 도구용이며 게임·서버 실행 의존성이 아니다. 9/19에 미사용
`python/relay_server`를 제거하고 필요한 생성 바인딩을 `scripts/generated/`로 옮겼다.
검사 의존성은 `python -m pip install -r scripts/requirements.txt`로 설치한다.
Proto만 확인하려면 `python scripts/check_generated_proto.py -v`를 실행한다.
전체 설치·생성 절차는 [개발 검증 도구](../../scripts/README.md)를 참고한다.

부품 조합은 아래 별도 실제 통신 검사로 확인합니다. 기본 부품형→재연결→앞 타이어만 교체한
개별 조합→재연결→승차감형→기본값 복구의 6단계에서 선택 ID와 초기 연료·재연결 시 연료 보존을
검사합니다. 임시 포트의 자체 서버만 사용하므로 실행 중인 플레이 서버를 초기화하지 않습니다.

```powershell
python scripts/smoke_health.py --map-package map_packages/signal_city_v2 --vehicle-loadouts
```

포함 범위:

1. C++ Release configure/build와 전체 CTest. 빈 시험 또는 건너뛴 시험은 통과가 아니다.
2. 생성된 Python Proto 정합성, Python 검증 도구 unit test, 세 server launcher profile와 Windows 패키지 계획/실행기 검사.
3. 격리된 임시 loopback port의 Signal City 실제 WebSocket 시험과 물리 replay 시험.
4. UE Editor/Game Development build와 `DriveIntegration` 전체 Automation.
5. Virtual City/Signal City의 map·traffic `ValidateOnly`. 맵 생성·Bake·덮어쓰기는 하지 않는다.

각 단계 로그와 `report.json`은 충돌하지 않는
`runtime_logs/core-check-날짜-시간-ID/`에 저장한다. 명령 실패, timeout, UE 시험 발견/완료
개수 불일치가 있으면 즉시 실패하며 이후 단계는 실행하지 않는다. UE는 NullRHI로 기능을
검증하므로 이 결과를 화면 렌더링 성능으로 사용하지 않는다.

`--preflight-only`는 준비물과 실행 순서만 확인한다. `--skip-build`는 기존 바이너리만,
`--skip-unreal`은 서버/도구만 검사하며 성공해도 보고서 상태는 `partial_pass`다.
전부 실행한 `automatic_pass`에도 수동 gate는 `not run`으로 남는다.

## 실시간 지면 측정 비교

`GroundQueryShadow`는 실시간 전환의 첫 비교 기능입니다. **실제 물리는 계속 파일 기반**입니다.
측정값이 달라도 차량을 보정하거나 제어권을 갱신하지 않습니다. 문제 발생 시 옵션 없이 서버를 다시 실행하면 비교를 끕니다.

```powershell
.\scripts\run_signal_city_server.ps1 -Background -GroundQueryShadow
```

기존 서버를 먼저 종료하고 최신 빌드의 Unreal에서 Play합니다. 지면 측정을 시험하기 위해 다시 Bake할 필요는 없지만,
현재 실행은 여전히 기존 MapPackage와 교통 파일을 필요로 합니다. 서버/클라이언트 모두 새 기능을 지원해야 측정합니다.

- 요청: 최신 양쪽이 `ground-query-shadow.v2`를 협상하면 최대 10Hz로 최근 물리 단계의 실제 조회를 보냅니다.
  위치와 개별 깊이를 보존하고 각 단계에서 최대 512개 고유 요청을 수집합니다. 전송은 최대 64개이며 더 많으면 순환 선택합니다.
  현재 계산기의 24m/4m 깊이를 그대로 사용하며, 프로토콜 상한은 32m입니다. v1 클라이언트에는 이전 5지점·8m 비교만 보냅니다.
- 수집: 원래 공급자의 반환값을 변경하지 않습니다. 같은 위치라도 깊이가 다르면 별도 요청이며, 접촉 없음도 보존합니다.
  초기화·NPC 조회는 수집하지 않습니다. 10Hz 표본 비교이므로 매 60Hz 계산의 모든 지점을 검증했다는 뜻이 아닙니다.
- 경계: 요청 1개만 진행, 500ms 만료, 같은 요청을 WorldState와 함께 재게시합니다. Unreal은 이미 처리한 요청을 다시 측정하지 않습니다.
  조작 명령을 먼저 보내고 게임 스레드에서 측정합니다. 전체 프레임에 걸친 데이터 대기열은 만들지 않습니다.
- 대상: WorldStatic 충돌만 조회하고 자신의 Actor는 제외합니다. 재질은 Bake의 기본 태그/PhysicalSurface 매핑을 재사용합니다.
  에디터의 Ground Actor 한정 필터·사용자 지정 재질 매핑·마찰 배율은 아직 이 경로로 옮기지 않았습니다.
- 식별: 연결 세대·Play·맵 체크섬·요청 번호·기준 시뮬레이션 시간·측정 개수를 확인합니다. epoch는 서버의 연결/맵 문맥 변경 번호입니다.
  Unreal 내부의 임의 지형 편집을 감지하는 월드 revision은 아직 아니므로, 이 결과를 동역학의 정답으로 재사용하지 않습니다.
- 결과: 정상 접촉·접촉 없음·측정 실패를 구분하고, 잘못된 수치·법선·레이 밖 접촉·부분 응답·중복/만료 응답은 거부합니다.
- 재생: 비교 결과는 물리에 쓰지 않으므로 기존 물리 기록 형식은 유지합니다. 결과를 물리에 연결하는 다음 단계에는 실제 적용 측정 기록이 필요합니다.

서버 로그의 `[GroundShadow]`에서 `accepted`, `timed_out`, `presence_mismatch`, `max_height_error_m`,
`max_normal_error_deg`, `material_mismatch`, `measurement_ms`, `round_trip_ms`를 확인합니다.
`source=solver`는 실제 계산 조회, `source=footprint`는 v1 진단입니다. `source_calls`는 선택된 원본 단계의 호출 수,
`source_unique`는 보관한 고유 요청 수, `source_discarded`는 상한/잘못된 수치로 수집에서 제외한 호출 수,
`selected`는 이번에 전송한 개수입니다. 제외해도 실제 물리 조회는 그대로 실행합니다.
초과 호출은 별도 집계하며 이를 전체 조회 검증 성공으로 취급하지 않습니다.
오차와 횟수는 프로세스 실행 이후 누적입니다. `round_trip_ms`는 서버 요청 준비부터 응답 수신까지의 로컬 경과 시간으로,
상태 전송·Unreal 프레임 대기·측정을 모두 포함하며 순수 네트워크 RTT나 키 입력 지연이 아닙니다.
`round_trip_p95_ms`와 `measurement_p95_ms`는 최근 최대 256개 정상 수신 묶음의 nearest-rank p95입니다.
표본이 256개보다 적으면 그만큼만 사용합니다. 만료/미수신은 백분위에 넣지 않으므로 해당 횟수도 함께 확인합니다.
로그는 1초 단위 요약이며 전 구간 원시 측정 저장, 전체 프레임/서버 step p95, 모드별 성능 인수는 아직 완료하지 않았습니다.

자동 검사는 `ground_query_shadow_tests`, Unreal의 `DriveIntegration.GroundShadow`입니다.
실제 주행에서는 동일 차종/입력으로 비교 모드 켜기·끄기를 비교하고, 평지 → 경사 → 한쪽 바퀴 연석 → 지면 없음 → 재접속을 확인합니다.
지형을 바꾼 뒤 차이 로그가 증가하는 것은 이 단계에서 정상이며, Bake 없이 새 지형으로 주행하는 기능이 완료된 것은 아닙니다.

## 실험용 플레이어 지면

기존 서버를 종료하고 아래 명령으로 실험 모드를 켭니다. 기본 실행 방식은 바뀌지 않습니다.

```powershell
.\scripts\run_signal_city_server.ps1 -Background -LiveGround
```

다시 빌드한 Unreal에서 Play합니다. 지면 비교 옵션과 동시에 켤 수 없고, 구형 live 클라이언트는 연결 협상에서 거부합니다.
서버 물리 기록/재계산은 측정 모드 v2를 사용합니다. 기본 파일 모드는 바뀌지 않습니다.

상자형 장애물과 NPC·보행자 지면을 함께 시험하려면 다음과 같이 실행합니다.

```powershell
.\scripts\run_signal_city_server.ps1 -Background -LiveObstacles -LiveNpcGround
```

두 옵션은 플레이어 지면도 켭니다. 첫 NPC 타일을 확보하는 동안 출발이 지연될 수 있습니다.
NPC 장애물 계획·구조물 피해·차로/신호·시작 시 검사에 쓰는 맵 패키지는 유지해야 합니다.

1. 평탄한 넓은 지면에서 `[LiveGround]`의 `patches`와 `steps`가 증가하는지 확인합니다.
2. 초기 측정 중의 `waiting=1`은 정상입니다. `waits`는 보류한 틱 수이며, 연결 실패 횟수가 아닙니다.
3. 단층 경사와 직선 연석에서 접지·차체 기울기를 확인합니다. 연석 양방향·비스듬한 진입·한쪽 바퀴만 걸친 상태를 각각 시험합니다. 측면 경계를 확인하지 못한 복합 형상은 보류될 수 있습니다.
4. 연결을 끊었다 복구하면 이전 연결 측정이 거부되고 새 자료를 기다립니다. 제어권 시간 제한은 그대로 유지합니다.
5. `rejected`는 잘못된 응답, `expired`는 응답 대기 중 요청 만료입니다. 이미 설치한 자료가 만료되어 멈추는 경우는 `waits`에도 나타납니다.
6. 충분히 넓은 평지에서 전진·후진·회전하며 속도를 올립니다. 측정 범위가 속도 방향으로 늘어나도 좁은 형상과 급격한 높이 변화까지 보장하지 않습니다. 실제 고속 주행 인수는 자동 검사와 별도로 기록합니다.
7. 실행 중 지원되는 바닥/상자를 이동하거나 충돌을 끕니다. `revision`이 바뀌고 새 결과가 도착하기 전에는 이전 형상으로 진행하지 않아야 합니다. 직접 형상을 수정하는 도구는 `NotifyLiveWorldChanged`를 호출해야 할 수 있습니다.
8. `-LiveObstacles`에서는 `obstacle_patches`, NPC 옵션에서는 `npc_patches`·`npc_tiles`를 확인합니다. 미지원 장애물·수집 한도 초과는 빈 공간으로 처리하지 않고 보류합니다. 보행자·NPC 지면 측정이 늦어져도 이미 생성한 객체가 없어져서는 안 됩니다.

`waiting=1`이 계속되면 범위 밖·단차·측정 실패 등을 먼저 확인합니다. 이는 파일 지면으로 자동 복귀하지 않는
제한된 실험 모드이며, 일반적인 연석/고속 주행·겹친 지면·정상 자유 낙하 인수는 남아 있습니다.
격자와 시간 제한은 [구현 계약](../architecture.md#실험용-플레이어-지면-연결)에 정리했습니다.
자동 검사는 `live_ground_patch_tests`, `live_obstacle_snapshot_tests`, `live_npc_ground_tests`,
`live_world_protocol_tests`와 Unreal의 `DriveIntegration.LiveGround`입니다.

## 입력 기반 물리 재생

![화면 상태를 다시 표시하는 재생과 입력을 다시 계산하는 물리 재생 비교](../images/replay-paths.svg)

기존 `R` snapshot CSV/`F6` visual ghost는 그대로 유지한다. 서버의 물리 재생은 별도
기능이며 화면 좌표를 복사하지 않고, 기록된 입력을 같은 `VehiclePhysics`에 다시 적용한다.

```powershell
# 기존 서버가 port 9000을 사용 중이면 먼저 종료하거나 --ws-port로 다른 port를 선택한다.
# 3,600 physics ticks(60Hz에서 약 60초)를 기록한다. 이 동안 Unreal에서 주행한다.
cpp/host/build/Release/simcore_publisher.exe `
  --runtime-config cpp/host/config/signal_city_server.cfg `
  --record-physics runtime_logs/my_drive.physics --record-ticks 3600

# 기록 때와 같은 실행 파일·차량·MapPackage를 사용한다.
cpp/host/build/Release/simcore_publisher.exe `
  --runtime-config cpp/host/config/signal_city_server.cfg `
  --verify-physics-replay runtime_logs/my_drive.physics

# Unreal 없이 실제 wire 입력 → 서버 기록 → offline 검증을 실행한다.
python scripts/smoke_physics_replay.py
```

실시간 측정을 기록할 때에는 기록과 검증 양쪽에 `--live-ground`를 추가합니다.
장애물/NPC 입력도 시험하려면 **기록 명령**에 `--live-obstacles --live-npc-ground`를 더합니다.
검증은 저장된 지면 조회·정적/동적 충돌 입력을 사용하므로 Unreal을 켜지 않아도 됩니다.
파일 모드 v1과 측정 모드 v2는 서로 다른 계약이며 잘못된 모드로 열면 거부합니다.
최대 1GiB이므로 큰 충돌 입력을 매 틱 기록할 때는 프레임 제한보다 먼저 중단될 수 있습니다.
강제 종료한 기록은 완성된 시험 결과로 사용하지 않습니다.

기록에는 fixed tick의 적용 입력, reset/안전 정지 등 lifecycle, 외부 동적 충돌 proxy와
결과 pose가 들어간다. 이 단계의 범위는 **Ego 차량 물리의 재현**이다. NPC/보행자 AI를
별도로 재실행하는 대신 해당 tick의 외부 충돌 입력을 기록하여 사용한다.
RESET 외 lifecycle marker는 원래 발생 사실을 보존하며, lease FSM 자체를 offline에서
재실행하지는 않는다. 안전 전이의 실제 물리 효과는 기록된 적용 입력으로 재현한다.

9월 5일부터 RESET에 플레이어 차종을 기록하고 해당 Ego profile로 재생한다. 차종이 없는
기존 RESET은 세단으로 읽으며 잘못된 차종값은 거부한다. 부품 조합을 선택했다면 RESET에
`LOADOUT <id>`를 추가해 재계산에도 같은 부품을 사용한다. Unreal의 별도 주행 CSV v3는
차종과 부품 조합 ID를 저장하며 v1·v2 읽기를 유지한다. 차종/조합 변경은 새 PlaySession에서
수행하며, 기존 주행 기록을 마무리하고 SensorRig sequence와 capture cadence도 초기화한다.
`parts_v1_` 개별 조합도 ID로 보존합니다. 카탈로그 내용에 따라 인덱스가 달라질 수 있으므로
기록 때와 같은 카탈로그 체크섬이 필요합니다. 예전 기록을 새 부품 파일로 임의 해석하지 않습니다.

동일 실행 파일·vehicle 설정·map checksum·origin·physics Hz를 확인하고 frame별 위치
1cm, yaw 0.1도 이내인지 검사한다. 잘못된 포맷·비유한 값·잘린 파일이나 환경 변경을
정상 재생으로 간주하지 않는다. 기록 중 map/traffic reload는 서로 다른 환경을 섞지 않도록
기록을 무효화한다. 자동 replay 통과가 사용자의 실제 녹화·재생 확인까지 대신하지 않는다.
기록은 기본 3,600tick, 최대 216,000tick·1GiB로 제한하며 기존 파일을 덮어쓰지 않는다.
기록 tick 수는 서버 시작부터 계산하므로 Unreal을 여는 준비 시간도 포함된다. 일반 종료나
Ctrl+C는 footer를 마무리하고, 강제 종료로 잘린 파일은 검증에서 거부한다.

물리 주기를 비교할 때는 같은 시간 기반 입력 시나리오를 각 주기로 따로 실행·기록합니다.
한 재생 파일의 Hz만 바꾸어 검증하면 같은 실험이 아닙니다. 분석용 5~10Hz 요약 로그도
tick별 입력·충돌 조건을 보존하는 이 기록의 대체물이 아닙니다.

## 성능 캡처

자동 검사 실행기의 UBT 로그는 해당 검사 폴더에, 에셋 생성 캐시는 `runtime_tmp/ue-ddc/`에
둡니다. 검사용 자식 프로세스만 `InstalledNoZenLocalFallback`을 사용하므로 일반 Editor의
공용 Zen·캐시 설정은 바꾸지 않습니다. 첫 실행에는 로컬 캐시 준비 시간이 추가될 수 있습니다.

측정은 기본 OFF다. Unreal 실행 인수에 다음을 넣으면 준비 시간 후 frame/state 간격을
기록하고, 측정 시간이 차면 파일을 저장한다. 렌더 설정이나 속도 제한을 자동 변경하지 않는다.

```text
-SimCorePerfCapture -SimCorePerfWarmup=10 -SimCorePerfDuration=60 -SimCorePerfLabel=smoke
```

파일은 해당 프로젝트/패키지의 `Saved/PerformanceCaptures/` 아래 CSV와 JSON 한 쌍이다.
아래 파일명은 캡처에서 출력된 실제 이름으로 바꾼다. JSON은 같은 폴더·같은 이름으로 둔다.

```powershell
python scripts/analyze_performance.py 'unreal/DriveIntegration/Saved/PerformanceCaptures/실제파일.csv'
# 유효한 패키지 측정 조건과 frame/state 수치까지 모두 충족해야 exit 0
python scripts/analyze_performance.py 'unreal/DriveIntegration/Saved/PerformanceCaptures/실제파일.csv' `
  --require-measurement-gates
```

Editor의 PIE나 NullRHI는 기록 경로 시험에는 쓸 수 있지만 패키지 성능 인수 자료가 아니다.
실제 패키지 인수에서는 1920×1080, uncapped, VSync/고정·스무딩 frame 제한 해제 여부를
확인하고 최소 1,800초를 측정한다. 목표 장비는 기존 계획의 i5-10400/RTX 2060이다.

지표의 의미:

- frame: 단조 시계로 측정한 엔진 frame-end 간격. GPU 단독 처리 시간은 아니다.
- state: 클라이언트가 승인한 WorldState의 game-thread 도착 간격.
- 입력→첫 상태 변화: 이 캡처에서는 측정하지 않는다. 상태 수신 간격을 입력 반응으로 대체하지 않는다.
  [별도 입력 반응 측정](./input_latency_measurement.md)은 물리 키 입력과 화면을 함께 촬영해
  화면 반응까지의 지연을 오차 포함으로 판정한다. 서버 적용 sequence 계측과는 다른 지표이며,
  원본 영상의 실제 동작 확인과 NFR-012 최종 인수 판단은 사람이 수행한다.

분석기는 평균 FPS, p95/max frame 및 state 간격을 계산하고, Editor/패키지·실제 해상도·
제한 설정·warmup/측정 시간·설정 변경 여부로 유효한 측정인지 구분한다. 30분 파일이
있다는 사실만으로 연속 수동 주행, crash/deadlock 부재와 조작감 인수를 자동 승인하지 않는다.

## 연구 실험 절차

다음은 **후속 연구의 비교 기준**입니다. 보간 버퍼·RTT 계측·비동기 exporter·DB 분석은
아직 구현하지 않았습니다. 기존 캡처와 복구·재생 시험을 먼저 사용하고, 필요한 계측을 추가한 뒤 비교합니다.
문서만 갱신하거나 시험 계획을 작성한 상태를 실험 완료로 기록하지 않습니다.

### 먼저 지표를 구분합니다

![수신 나이와 표시 예측, 후속 지연 보간이 가리키는 서로 다른 시간](../images/state-timing.svg)

| 지표 | 의미·현재 확보 방법 | 해석할 때 주의할 점 |
|---|---|---|
| frame/state 간격 | 기존 UE 성능 캡처·분석기의 평균 FPS, p95/max 간격 | 서버 로그만으로 UE FPS를 구할 수 없음. GPU 시간·1% low와도 다름 |
| `local_age` | 승인한 최신 상태를 로컬에서 받은 뒤 경과 시간 | 표시 버퍼의 의도적인 지연이나 RTT가 아님 |
| `server_age` | 서버 상태 UTC와 클라이언트 UTC의 차이 | 시계 오차·보정 영향을 받음. 단방향 순수 전송 지연으로 단정하지 않음 |
| `[Latency] applied_to_tick_ms` | 서버에서 입력 변경을 받은 뒤 적용 tick까지의 간격 | 입력 송신 이전과 화면 표시 시간은 포함하지 않음 |
| 키→화면 반응 | [키와 화면을 함께 촬영](input_latency_measurement.md) | 실제 화면 반응이며 서버 로그나 상태 간격으로 대체하지 않음 |
| RTT | 요청 송신부터 대응 응답 수신까지 같은 단조 시계로 측정할 예정 | 전용 식별자/응답 계측 미구현. 서로 다른 PC 시각을 단순 차감하지 않음 |
| 누락·거부·적체 | 기존 순번 공백·오래된 상태 거부 및 서버 overrun | 송신 대기 교체·분석 큐 폐기 계측은 추가 필요. overrun은 물리 주기 지각이지 패킷 손실이 아님 |
| 위치/방향 보정량 | 표시 방식 비교를 위해 정의·계측할 예정 | 동일한 표시 시각의 기준 상태와 비교. 지연 보간의 정상 지연을 오차로 섞지 않음 |

### 기준 실행을 남깁니다

1. revision과 미커밋 여부, 맵/카탈로그 체크섬, 차종·부품 조합, 서버 Hz·NPC 수를 기록합니다.
   입력의 시점·길이, 시작 위치, 카메라, UE 해상도·프레임 제한·VSync, 장비도 고정합니다.
2. 직선 가감속·곡선·차단/우회·충돌·연결 단절을 별도 시나리오로 나눕니다. 개선할 문제와 성공/실패 조건을
   먼저 적고, 같은 입력·초기 상태로 기준 실행과 변경 실행을 비교합니다.
3. 짧은 연구 비교는 준비 10초·측정 60초·조건별 3회 반복을 시작안으로 사용합니다. 시간이 부족하거나
   결과가 불안정하면 반복 수·측정 시간을 조정하고 이유를 남깁니다. 이 실행은 30분 패키지 인수가 아닙니다.
4. 각 실행에 고유 ID를 붙이고 Play 세션·메시지 순번·simulation tick으로 자료를 연결합니다.
   원시 로그·CSV/JSON·영상·오류를 보존합니다. 사용한 시계와 단위, 표본 수, 집계 범위를 함께 적습니다.
5. 한 번에 조건 하나만 바꿉니다. 평균·p95·최댓값과 실패 횟수를 함께 보고, 실행별 편차를 숨기지 않습니다.
   아직 측정하지 못한 값은 0이나 통과가 아니라 `미측정`으로 적습니다.

### 비교할 실험

| 실험 | 기준과 변경 조건 | 별도로 지킬 조건 |
|---|---|---|
| 표시 부드러움 | 현재 최신 상태+최대 50ms 예측 ↔ 50/100/150ms 보간 후보 | 최소 계측 구현 후 진행. 플레이어 반응·NPC 움직임을 따로 판정하고 Health/제어권 제한은 유지 |
| 로그 부하 | 로그 설정·저장 부하를 고정해 동기 경로와 유한 큐/별도 쓰기 작업 비교 | 큐 길이·폐기·쓰기 오류·종료 누락을 계측. 물리 재생 손실은 성공으로 처리하지 않음 |
| FPS와 물리 주기 | 먼저 서버 60Hz 고정 상태에서 UE FPS만 비교. 이후 필요할 때 서버 Hz만 변경 | 시간 기반 입력을 같은 길이로 적용하고 주기별 재생 기록을 따로 생성 |
| 단절·적체·규모 | 기존 복구 시험을 기준으로 단절·지연·메시지 처리 적체 및 NPC 수 비교 | 현재 NPC 설정 범위 1~16과 맵의 안전한 배치 범위 안에서 시작. 대규모화는 별도 구현 |

WebSocket은 TCP 위에서 동작합니다. 전송 계층의 패킷 손실/재전송과 응용 계층에서 상태 메시지를
생략·중복시키는 시험은 다릅니다. 장애 주입 위치·지연 분포·시드·강도·지속 시간을 명시하고,
송신 대기 교체·수신 거부·순번 공백·분석 큐 폐기를 하나의 ‘손실률’로 합치지 않습니다.

분석은 로컬 파일부터 시작할 수 있습니다. DB 설치를 기준 측정의 선행 조건으로 두지 않습니다.
요약은 [현재 상태](../status.md#연구개발-계획)에 근거 경로와 함께 남기고, 구조가 바뀌면
[설계 문서](../architecture.md#관측과-연구-확장)를 갱신합니다. 로그·영상·DB 덤프 자체는 Git에서 제외합니다.

## 여전히 사람이 확인할 항목

- 짧은 조향 tap, 중속 선회와 Space drift/해제 회복
- 경사·연석·벽·전복·Ego↔NPC/보행자 충돌의 실제 화면 및 dent 복원
- 신호·HUD·재연결, 고스트/센서 표시
- G/Tab 개별 부품 선택의 적용·취소·재열기, 차종/부품 변경 후 R/F6 기록·재생
- 목표 PC packaged 렌더·제어 지연과 30분 연속 수동 주행
- 최종 아트·에셋 출처와 촬영

실제 RGB image는 Should 확장, LiDAR/radar/segmentation은 Future다. SensorRig의
장착 위치·시간·프레임 metadata와 구분하며 실제 point-cloud를 Core 필수 인수로 잡지 않는다.

완료 기준과 검증 실적은 [현재 상태](../status.md), 이전 보고서 위치는
[보관 자료 안내](../README.md#이전-자료)를 확인한다.
