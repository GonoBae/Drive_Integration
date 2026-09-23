# Core 자동 통합·재생·성능 검증

현재 코드의 자동 검증 절차입니다. **자동 통과와 실제 주행 인수 완료는 다릅니다.**
운전 감각·화면/충돌 일치·패키지 성능과 30분 주행은 별도로 확인합니다.
이미 수행한 결과와 남은 항목은 [현재 상태](../status.md)에 모았습니다.

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

부품 조합 선택·같은 Play 재연결·다른 조합 적용·기본값 복구는 아래의 별도 실제 통신 검사로
확인한다. 임시 포트의 자체 서버만 사용하므로 실행 중인 플레이 서버를 초기화하지 않는다.

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

## 입력 기반 물리 재생

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

동일 실행 파일·vehicle 설정·map checksum·origin·physics Hz를 확인하고 frame별 위치
1cm, yaw 0.1도 이내인지 검사한다. 잘못된 포맷·비유한 값·잘린 파일이나 환경 변경을
정상 재생으로 간주하지 않는다. 기록 중 map/traffic reload는 서로 다른 환경을 섞지 않도록
기록을 무효화한다. 자동 replay 통과가 사용자의 실제 녹화·재생 확인까지 대신하지 않는다.
기록은 기본 3,600tick, 최대 216,000tick·1GiB로 제한하며 기존 파일을 덮어쓰지 않는다.
기록 tick 수는 서버 시작부터 계산하므로 Unreal을 여는 준비 시간도 포함된다. 일반 종료나
Ctrl+C는 footer를 마무리하고, 강제 종료로 잘린 파일은 검증에서 거부한다.

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

## 여전히 사람이 확인할 항목

- 짧은 조향 tap, 중속 선회와 Space drift/해제 회복
- 경사·연석·벽·전복·Ego↔NPC/보행자 충돌의 실제 화면 및 dent 복원
- 신호·HUD·재연결, 고스트/센서 표시
- 목표 PC packaged 렌더·제어 지연과 30분 연속 수동 주행
- 최종 아트·에셋 출처와 촬영

실제 RGB image는 Should 확장, LiDAR/radar/segmentation은 Future다. SensorRig의
장착 위치·시간·프레임 metadata와 구분하며 실제 point-cloud를 Core 필수 인수로 잡지 않는다.

완료 기준과 검증 실적은 [현재 상태](../status.md), 이전 보고서 위치는
[보관 자료 안내](../README.md#이전-자료)를 확인한다.
