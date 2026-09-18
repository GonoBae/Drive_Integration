# 부록 A. Windows에서 실행하는 방법

이 부록은 실행 방법을 설명합니다. 교재를 만들면서 아래 게임 빌드나 검사를 새로 실행하지 않았습니다. 경로는 현재 PC를 기준으로 적었습니다.

## 실행 전 준비

PowerShell에서 저장소 루트로 이동합니다.

```powershell
Set-Location 'B:\Portfolio\Drive_Integration'
```

전체 검사는 C++ 실행 파일과 Unreal 모듈을 다시 빌드합니다. 레벨을 저장하고 Unreal Editor를 종료하세요. 표준 Release 실행 파일로 켜 둔 서버도 먼저 종료해야 파일 잠금과 빌드 충돌을 피할 수 있습니다. 검사기는 사용자의 기존 에디터나 서버를 이름으로 찾아 강제 종료하지 않습니다.

개발 PC에는 CMake/CTest, Visual Studio C++ 도구, 준비된 vcpkg, Unreal 5.6과 Python 검증 의존성(`grpcio-tools`, `protobuf`, `websockets`)이 필요합니다. Python은 개발용 검사 도구에 사용합니다. 배포 폴더를 실행하는 사용자에게 Python 설치가 필요하다는 뜻은 아닙니다. 이 문서는 기존 개발 환경을 사용하는 방법을 다루며 설치 환경을 자동 변경하지 않습니다.

## A-1. 검사하지 않고 준비물과 실행 계획 확인

```powershell
python scripts/check_core.py --engine-root 'B:\Epic Games\UE_5.6' --preflight-only
```

확인할 결과는 `PREFLIGHT ONLY (no tests executed)`와 실행할 단계 목록입니다. 도구나 의존성이 없으면 준비 단계에서 실패합니다. 이 명령이 성공한 것은 검사 준비 확인이지 게임 기능의 통과가 아닙니다.

## A-2. 전체 자동 검사

```powershell
python scripts/check_core.py --engine-root 'B:\Epic Games\UE_5.6'
```

기본 계획은 16개 단계입니다. 단계별 제한 시간이 있으므로 실제 소요 시간은 빌드 상태와 PC 부하에 따라 달라집니다. 처음 빌드와 후속 빌드의 시간을 같다고 가정하지 마세요.

결과는 실행마다 새로운 `B:\Portfolio\Drive_Integration\runtime_logs\core-check-날짜-시간-ID\` 폴더에 남습니다. 마지막 줄의 보고서 경로를 확인합니다. `automatic_pass`도 실제 화면·청감·목표 PC 성능과 수동 운전 인수를 자동 승인하지 않습니다.

## A-3. 특정 C++ 검사부터 실행

현재 구성된 검사 이름을 보기만 할 때에는 다음 명령을 사용합니다. 등록 목록은 빌드 폴더의 구성 상태를 반영하므로 소스를 바꾼 뒤에는 configure가 필요할 수 있습니다.

```powershell
ctest --test-dir cpp/host/build -C Release -N
```

테스트 바이너리가 현재 소스로 빌드되어 있다면 이름을 정확히 선택할 수 있습니다.

```powershell
ctest --test-dir cpp/host/build -C Release -R '^control_lease_tests$' --output-on-failure --no-tests=error
```

관련 검사 그룹을 고르는 예시는 다음과 같습니다. `-R`은 정규식 필터이며 선택된 이름은 실행 출력에서 확인합니다.

```powershell
ctest --test-dir cpp/host/build -C Release -R 'vehicle_physics|collision_world|chassis_ground_contact' --output-on-failure --no-tests=error
```

현재 소스로 빌드해야 한다면 먼저 아래 순서로 진행합니다. 빌드 중인 같은 대상에 여러 작업을 동시에 실행하지 않습니다.

```powershell
Set-Location 'B:\Portfolio\Drive_Integration\cpp\host'
cmake --preset release
cmake --build --preset release
ctest --preset release --no-tests=error
```

`--output-on-failure`는 실패한 검사의 출력을 보여 줍니다. `--no-tests=error`는 필터에 아무 검사도 걸리지 않는 상황을 오류로 처리합니다. 이것만으로 개별 검사의 skip을 모두 실패로 바꾸지는 않습니다. 전체 Core 실행기는 XML 보고서에서 skipped 항목도 별도로 거부합니다. CTest는 등록된 실행 파일을 호출하는 실행기이며 내부 assertion의 개수까지 같은 숫자로 세는 것이 아닙니다. [CTest 공식 옵션 문서](https://cmake.org/cmake/help/latest/manual/ctest.1.html)

## A-4. Unreal의 특정 Automation 검사

현재 Editor 대상이 빌드되어 있어야 합니다. 다음 예시는 좌표 계약 검사 하나만 선택합니다. 엔진 설치 경로가 다르면 바꿔 주세요.

```powershell
Set-Location 'B:\Portfolio\Drive_Integration'
$testingStamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$testingOutput = Join-Path (Get-Location).Path "runtime_logs\study-automation-$testingStamp"
New-Item -ItemType Directory -Path $testingOutput | Out-Null

& 'B:\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'B:\Portfolio\Drive_Integration\unreal\DriveIntegration\DriveIntegration.uproject' `
  -unattended -nop4 -NullRHI -NoSound -NoSplash -stdout -FullStdOutLogOutput `
  '-ExecCmds=Automation RunTests DriveIntegration.Coordinates.GeoTransformContract' `
  '-TestExit=Automation Test Queue Empty' `
  "-ReportExportPath=$testingOutput" `
  "-abslog=$testingOutput\engine.log"
```

PowerShell의 줄 끝 백틱 뒤에는 공백을 넣지 마세요. 코드 전체를 함께 복사하면 줄 연결을 유지하기 쉽습니다. 전체 `DriveIntegration` 필터로 바꾸면 해당 빌드에서 발견하는 모든 프로젝트 검사를 선택하지만, C++ 서버 검사나 실제 통신 검사는 포함하지 않습니다.

찾아야 할 것은 검사 발견, 해당 경로의 `Test Completed` 결과와 완료 개수입니다. `RunTest`가 `true`를 반환해도 assertion 오류가 누적되면 실패할 수 있습니다. 위 단독 명령은 Core 보고서 검증기를 함께 호출하지 않으므로 엔진 로그와 내보낸 결과를 직접 확인해야 합니다. 단독 프로세스가 예상과 달리 끝나지 않으면 해당 실행을 조사하고, 전체 Core 실행기의 제한 시간 관리와 혼동하지 않습니다.

`NullRHI`는 실제 GPU 렌더링을 생략하는 실행 경로이고 `NoSound`는 장치 재생을 생략합니다. 따라서 이 결과로 화면의 자연스러움과 소리 품질을 판단하지 않습니다. 세부 등록 방식은 [Unreal 5.6 Automation 공식 개요](https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine?application_version=5.6)를 참고합니다.

## A-5. Python 검사 도구만 확인

```powershell
Set-Location 'B:\Portfolio\Drive_Integration'
python -m unittest discover -s scripts -p 'test_*.py' -v
python -m unittest -v python.relay_server.tests.test_generated_proto
```

첫 명령은 검증용 스크립트의 unit test를 찾습니다. 두 번째는 저장소의 Python Proto 생성 파일 정합성을 검사합니다. 서버 실행을 대신하는 명령이 아닙니다. 일부 도구 unit test는 임시 파일이나 자신이 만든 작은 자식 프로세스를 사용하지만 실제 게임의 전체 실행과 다릅니다.

`python/relay_server/tests/test_relay.py` 파일이 존재한다는 이유로 전체 Core가 그 테스트까지 실행한다고 설명하지 않습니다. 현재 Core의 별도 Python 모듈 호출 대상은 `test_generated_proto`입니다. 실행 계획과 실제 discovery 범위를 기준으로 판단해야 합니다.

## A-6. 실제 서버 통신과 입력 기반 물리 재생

현재 Release 서버가 빌드되어 있어야 합니다.

```powershell
python scripts/smoke_signal_city.py
python scripts/smoke_physics_replay.py
```

각 검사는 임시 loopback 포트에 직접 만든 서버를 실행합니다. 기본 9000 서버에 입력을 보내지 않으며, 식별자가 일치한 자식 서버인지 확인합니다. 만들어진 로그와 summary를 확인하고 종료 여부를 봅니다.

Signal City 검사는 실제 상태와 신호·경로 계약을 확인합니다. 같은 연결에서 입력 중단 후 복구하는 과정과 새 연결에서 새 Play가 초기화되는 과정을 구분합니다. 같은 Play의 연결 자체를 다시 만드는 시나리오는 물리 replay 스모크 등 별도 경로에서 확인합니다.

물리 replay 스모크는 실제 wire 입력으로 제한된 기록을 만들고, 기록 파일을 오프라인 검증기로 다시 계산합니다. 이 경로의 480프레임은 C++ 내부 host 테스트의 80틱 fixture와 다른 검사입니다. 저장 좌표를 그대로 다시 표시하는 CSV Ghost 재생과도 다릅니다.

## A-7. 부분 실행 옵션의 의미

```powershell
python scripts/check_core.py --skip-unreal
python scripts/check_core.py --engine-root 'B:\Epic Games\UE_5.6' --skip-build
```

첫 명령은 Unreal 빌드·Automation·맵 commandlet을 제외하지만 C++ 빌드는 기본적으로 수행합니다. 두 번째는 이미 빌드한 바이너리로 검사합니다. `--skip-build`만으로 Unreal 검사를 생략하지 않습니다.

두 경우 모두 성공한 보고서는 `partial_pass`입니다. 검사하지 않은 범위와 바이너리의 최신성을 기록하세요. 여러 부분 성공을 단순히 합쳐 한 시점의 전체 통과라고 부르지 않습니다.

## A-8. 패키지 검사

가벼운 계획·실행기 검사입니다. 게임 빌드나 주행을 하지 않습니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test_package_windows.ps1
```

이미 만든 패키지의 부팅·서버 연결을 확인하는 명령입니다. 경로는 실제 생성 결과로 바꾸어야 합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_windows_package.ps1 `
  -PackageDirectory 'B:\Portfolio\Drive_Integration\runtime_tmp\packages\실제_생성된_폴더'
```

위의 `실제_생성된_폴더`는 바꾸어 넣는 자리이며 존재하는 패키지 경로가 아닙니다. 검사기는 테스트용 서버와 Cook된 게임을 실행해 설정·Hello·지도·상태 수신을 확인합니다. 실행기는 직접 만든 자식만 정리합니다. 상세한 실제 패키지 생성 절차는 [Windows 패키지 문서](B:/Portfolio/Drive_Integration/docs/windows_package.md)에서 확인합니다.

## A-9. 성능 캡처 분석

Unreal 실행 인수로 측정을 켤 수 있습니다. 아래 60초 예시는 짧은 캡처 경로 확인용이며 30분 인수 조건을 만족하지 않습니다.

```text
-SimCorePerfCapture -SimCorePerfWarmup=10 -SimCorePerfDuration=60 -SimCorePerfLabel=study
```

생성된 CSV와 같은 이름의 JSON을 함께 둡니다. 아래 `실제파일.csv`는 캡처가 출력한 실제 이름으로 바꿉니다.

```powershell
python scripts/analyze_performance.py 'unreal/DriveIntegration/Saved/PerformanceCaptures/실제파일.csv'
python scripts/analyze_performance.py 'unreal/DriveIntegration/Saved/PerformanceCaptures/실제파일.csv' --require-measurement-gates
```

첫 명령의 정상 종료는 자료를 분석했다는 의미입니다. 결과의 측정 조건이나 목표값이 미충족일 수 있습니다. 두 번째는 유효한 측정 조건과 frame/state 기준까지 충족해야 종료 코드 0을 반환합니다. 잘린 파일이나 NaN은 입력 오류로 거부하지만, 상태 표본이 없는 유효한 자료는 분석 결과에 기준 미충족으로 나타날 수 있습니다.

측정 중 실제 렌더링과 설정이 유지되어야 합니다. 1920×1080 패키지의 1800초 측정, 제한 해제 등은 프로젝트에 정한 인수 조건이며 일반적인 모든 게임의 표준값이 아닙니다. 현재 `input_first_change`는 `not_measured`, 수동 주행 인수는 별도입니다.

# 부록 B. 보고서 읽는 법

## 가장 먼저 볼 항목

| 항목 | 의미 | 주의할 점 |
| --- | --- | --- |
| `started_utc`, `finished_utc` | 실제 실행 시각 | 소스 조사 기준일과 구분합니다. |
| `git_commit`, `worktree_dirty` | 실행 당시 코드 배경 | dirty는 미커밋 변경이 있었다는 정보입니다. 정확한 변경 전체를 재구성하는 파일은 아닙니다. |
| `skip_build`, `skip_unreal` | 생략한 범위 | 부분 성공을 전체 성공으로 읽지 않습니다. |
| `steps[].command`, `log` | 실제 명령과 원시 로그 | 마지막 성공 줄만 보지 말고 첫 실패를 찾습니다. |
| `steps[].tests.passed` | 해당 실행에서 확인한 개수 | assertion 수나 품질 백분율과 다릅니다. |
| `manual_gates_not_run` | 자동 실행으로 완료하지 않은 인수 | `automatic_pass`여도 남습니다. |

## 결과 상태

| 상태 | 해석 |
| --- | --- |
| `automatic_pass` | 생략 없는 자동 계획을 마쳤습니다. 수동 인수 완료가 아닙니다. |
| `partial_pass` | 선택한 부분은 통과했습니다. 생략한 부분은 확인하지 않았습니다. |
| `failed` | 실패한 단계가 있습니다. 이후 단계는 실행하지 않았을 수 있습니다. |
| `interrupted` | 사용자가 중단하는 등 전체 계획을 마치지 못했습니다. |
| `running` | 아직 최종 상태가 기록되지 않았습니다. 성공으로 읽지 않습니다. |

UE 보고서에서는 경고 없는 성공과 경고 포함 성공을 구분합니다. 예상 오류를 의도적으로 넣는 부정 검사는 `AddExpectedError`처럼 허용 범위를 명시할 수 있습니다. 이것을 모든 Warning/Error를 무시한다는 규칙으로 확대하지 않습니다. 새롭거나 예상하지 않은 경고는 원인을 확인해야 합니다.

## 실패를 조사하는 짧은 예

“WorldState가 도착하지 않았다”는 문장을 보면 먼저 접속 대상 식별과 Hello를 확인합니다. 서버가 죽었는지, 올바른 child인지, 같은 지도인지, 입력 lease가 유지되는지를 나눕니다. 반대로 “truck extent mismatch”라면 실제 자산과 프로필 값, 단위를 먼저 확인합니다. 모든 실패를 네트워크 문제나 성능 부족 하나로 묶지 않습니다.

# 부록 C. 어떤 변경에 어떤 검사를 연결할까?

| 변경 | 먼저 볼 검사 | 이어서 볼 검사 | 사람이 확인할 부분 |
| --- | --- | --- | --- |
| 조향·구동·브레이크 | vehicle physics | 접지·충돌·실제 코스·wire | 운전 감각, 고속·연석 상황 |
| 차체 원점·트럭 모델 | 좌표·프로필·Ghost Automation | 서버 충돌 범위, 패키지 자산 | 실제 충돌 표시·차체 외형 |
| 세션·timeout | ControlLease·clock | health·Signal City·replay wire | 재연결 시 화면과 조작 복구 |
| 깜빡이·오디오 | 시간·상태·표본 Automation | 입력 연결과 재접속 | 밝기, 클릭 길이, 실제 청감 |
| NPC 우회 | 기하·후진·search budget | host·저장 맵·wire | 복잡한 막힘과 자연스러운 주행 |
| 지도·교통 데이터 | 저장 자료·ValidateOnly | 실제 코스와 신호 주기 | 차선 가독성, 신호와 실제 장면 |
| 프로토콜 | 생성 바인딩·직렬화·거부 | Unreal 해석과 실제 wire | 실제 게임의 연결·표시 |
| 패키지 실행기 | 계획·경로·파일 검사 | 새 Cook된 패키지 smoke | 다른 경로·다른 PC·실제 렌더 |

# 부록 D. 직접 해 볼 연습과 답안 방향

## 연습 1. 아무 테스트도 실행하지 않은 성공

조건: 프로그램은 종료 코드 0을 반환했지만 UE 완료 로그가 없습니다.

기대: Core 단계는 `failed`여야 합니다. 이유는 명령 종료 성공과 검사 실행 성공이 다르기 때문입니다. [해당 검사](B:/Portfolio/Drive_Integration/scripts/test_check_core.py:62)를 읽고 `verify="automation"`을 생략했을 때 어떤 구분이 사라지는지 설명해 보세요. 실제 파일을 수정하지 않고 코드만 읽어도 됩니다.

## 연습 2. 첫 깜빡이가 짧아지는 문제

준비: 전체 월드 시각이 점멸 주기 중간인 순간에 좌측 깜빡이를 선택합니다. 실행: 선택 시각부터 경계 직전과 직후로 시간을 전진시킵니다. 판정: 첫 점등도 설정한 길이를 유지하고, 같은 프레임을 두 번 처리해 클릭을 중복 생성하지 않아야 합니다.

효과: 드문 시작 시각과 프레임 순서를 실제로 오래 기다리지 않고 재현할 수 있습니다. 한계: 밝기와 클릭 음색은 직접 봐야 하고 들어야 합니다. 그림의 같은 점등 길이는 원리이며 실제 경계 포함 여부는 코드에서 확인합니다.

## 연습 3. 연석 진입은 되는데 재출발은 안 되는 문제

진입 시험과 정지 후 재출발 시험의 준비 조건은 다릅니다. 차종·정확한 자세·접지 하중·기어·스로틀을 기록하고, 같은 자세에서 전진과 후진을 각각 확인합니다. 공중 바퀴에 근거 없는 접지력을 더해 통과시키는 방식은 피해야 합니다. 정상 연석·더 높은 벽·지지면 없는 경우도 같이 검사합니다.

답안의 핵심은 “이동했다” 하나 외에 어느 바퀴가 지지하는지, 힘이 어느 경로로 전달되는지, 막아야 할 장애물은 계속 막는지를 확인하는 것입니다.

## 연습 4. 잘못된 replay 파일의 checksum을 다시 계산하는 이유

손상 파일의 checksum만 틀리면 reader는 가장 앞의 무결성 검사에서 거부합니다. 그러면 틱 순서나 이벤트 종류 검사까지 실제로 도달했는지 알기 어렵습니다. 잘못된 구조를 남기고 checksum만 맞춰 주는 부정 사례는 더 깊은 의미 검사가 작동하는지 확인합니다. 정상 사용자 기록을 바꾸라는 뜻이 아니라 임시 테스트 fixture의 구성 방법입니다.

## 연습 5. 자동 테스트를 면접에서 설명하기

“C++ 검사는 고정 초기 상태와 입력으로 물리 조건을 비교했고, Unreal에서는 실제 Pawn과 메시를 만들어 좌표·표시 계약을 확인했습니다. 실제 서버 연결과 기록 재검산은 별도 통합 검사로 확인했습니다. 자동 통과는 작성한 조건의 근거이므로 운전 감각과 소리, 목표 PC의 패키지 주행은 직접 확인할 범위로 남겼습니다.”

이 문장을 그대로 개인 실적으로 암기하지는 마세요. AI가 작성하거나 실행한 부분과 본인이 요구사항을 정하고 제보·재현·검증한 부분을 실제 수행 범위에 맞춰 바꿔 설명해야 합니다.

# 부록 E. 용어를 짧게 정리하면

| 용어 | 의미 | 프로젝트 예 |
| --- | --- | --- |
| Assertion | 기대 조건의 참·거짓을 판정하는 문장 | `require`, `TestTrue`, `assertEqual` |
| Fixture | 반복 가능한 검사를 위한 준비 데이터·환경 | 평탄 지면, 임시 UWorld, 합성 로그 |
| Oracle | 정답이나 허용 결과를 정하는 기준 | 수동 계산 좌표, 명세 범위, 독립 기준 구현 |
| Negative test | 거부되어야 할 입력을 넣는 검사 | 잘못된 schema, NaN, 빈 보고서 |
| Regression | 변경 후 이전 동작이 깨지는 현상과 이를 막는 검사 용도 | 연석 재출발, Ghost 원점 |
| Mock / Fake | 외부 동작을 통제하거나 간단히 대체하는 검사 대역 | 실패하는 subprocess 호출, FakeConnection |
| Integration test | 실제 경계 사이의 연결을 확인하는 검사 | Python 클라이언트와 C++ 서버 wire |
| Smoke | 대표 기능 경로의 연결 상태를 확인하는 용도 | Signal City 통신, 패키지 부팅 |
| Timeout | 기다림의 최대 시간 | 멈춘 자식 검사 중단, 서버 lease 한도와는 별개 |
| Flaky | 같은 조건으로 보이는데 성공·실패가 불규칙한 검사 | 원인 미확인 시간·환경 의존 실패 |
| Headless | 사람이 조작하는 창 없이 실행하는 방식 | 명령행 자동 검사. 렌더링 여부는 별도 옵션 확인 |
| NullRHI | 실제 그래픽 렌더링을 생략하는 Unreal 실행 경로 | 기능 Automation과 패키지 접속 검사 |
| CI | 코드 변경에 맞춰 서버 등에서 자동 계획을 실행하는 운영 방식 | 현재 로컬 검사기가 있다는 것만으로 CI 구축을 뜻하지 않음 |

[그림별 원본과 생성 프롬프트](B:/Portfolio/Drive_Integration/docs/study/automated_testing/ILLUSTRATIONS.md)

개념 설명의 주요 근거는 프로젝트의 실제 검사 코드입니다. 공식 문서에서는 CTest의 실행 옵션, unittest의 assertion·discovery, Unreal Automation의 등록·실행 역할을 확인했습니다. 구체적인 시나리오와 판정 값은 해당 프로젝트 소스와 날짜가 있는 실행 기록을 기준으로 합니다.
