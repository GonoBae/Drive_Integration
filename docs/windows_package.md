# Windows 배포 패키지

UE 5.6 클라이언트와 C++ 서버를 같은 폴더에 묶는 실행 절차입니다. 받는 PC에는 Unreal Editor나 Python을 설치하지 않아도 됩니다. Windows x64, DirectX 12를 지원하는 그래픽 장치와 Microsoft 실행 런타임은 필요합니다.

## 만들기

먼저 에디터에서 변경한 레벨을 저장하고 Unreal Editor를 종료합니다. 빌드 PC에는 기존 개발 환경인 UE 5.6, Visual Studio C++ 도구, CMake, 준비된 vcpkg 의존성이 있어야 합니다.

저장소 루트의 PowerShell에서 실행합니다. 엔진 설치 위치가 다르면 `-EngineRoot`만 바꿉니다.

```powershell
# 파일·도구·엔진 버전만 확인합니다. 빌드와 테스트는 실행하지 않습니다.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1 -EngineRoot "B:\Epic Games\UE_5.6" -PrepareOnly

# C++ 서버 Release 빌드 → UE Windows Development 빌드/Cook → 배포 폴더 구성
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1 -EngineRoot "B:\Epic Games\UE_5.6"
```

결과는 `runtime_tmp/packages/DriveIntegration-날짜-시간-고유값/`에 생성됩니다. 매번 새 폴더를 사용하며 기존 결과를 덮어쓰거나 삭제하지 않습니다. 실패한 결과도 조사할 수 있도록 남깁니다. 이 경로는 Git에서 제외됩니다.

9/17부터 manifest v2에는 Git revision·미커밋 여부, 실제 빌드 입력 목록과 SHA-256,
배포 파일별 크기·SHA-256을 기록합니다. 신규 미추적 소스도 빌드 입력에 포함하며,
빌드 도중 입력 파일이 바뀌면 일관된 후보로 인정하지 않습니다. `source_snapshot.json`은
파일 식별 기록이지 소스 백업이 아닙니다. 정식 공유 전에는 필요한 신규 파일까지 커밋하고
그 revision을 다시 빌드해야 합니다. 이 스크립트는 Git 커밋·푸시를 하지 않습니다.

`-ValidationReports`에 기존 검사 보고서 경로 배열을 전달하면 `evidence/`로 복사합니다.
이 자료는 **패키지 생성 전 참고 검사**로 표시하며, 해당 패키지의 주행 인수로 간주하지 않습니다.

패키지 명령은 로컬 빌드·셰이더 컴파일을 사용합니다. PC에 설치된 IncrediBuild/XGE의
라이선스 상태에 의존하지 않도록 이 명령에서만 비활성화하며, 엔진의 전역 설정은 바꾸지 않습니다.
UAT 로그는 새 배포 폴더의 `build_logs/`, 생성 캐시는 저장소의 `runtime_tmp/ue-ddc/`를
사용합니다. 이 실행에서만 파일 기반 캐시를 선택하여 공용 Zen 서비스·사용자 로그 폴더의
쓰기 권한에 의존하지 않으며, 종료 후 프로세스 환경값을 복원합니다.
셰이더 임시 작업 폴더도 이 Cook 명령에서만 프로젝트의
`Intermediate/Shaders/PackageWorkingDirectory/`로 지정합니다.
다만 UE의 Staging 단계 자체는 엔진의 `Engine/Intermediate/Build/BuildCookRun/`에도
임시 파일을 작성하므로 이 경로의 쓰기 권한은 필요합니다.

`L_SignalCity`만 배포 맵으로 지정합니다. 실행 중 차종 선택과 보행자 생성에 필요한 차량·캐릭터 에셋은 Cook 대상에 명시해, 에디터에서는 보이지만 패키지에서는 사라지는 문제를 예방합니다. Development 패키지는 진단용이며 Shipping 배포를 검증했다는 의미는 아닙니다.

SensorRig가 직접 읽는 `Config/sensors.json`은 일반 INI와 별도로 `Windows/DriveIntegration/Config/sensors.json`에 원본 그대로 포함합니다. 빌드 규칙에서 loose runtime dependency로 지정하며, 패키징 스크립트가 누락 여부와 원본 SHA-256 일치를 확인합니다. 센서 메타데이터 설정이 패키지에서 빠진 상태로 성공 처리하지 않습니다.

## 실행하기

생성된 **폴더 전체**를 옮깁니다. `Windows/DriveIntegration.exe`만 복사하면 서버와 MapPackage가 빠집니다.

```text
DriveIntegration-.../
├─ StartServer.ps1
├─ StartClient.ps1
├─ SimCoreClient.ini
├─ package_manifest.json
├─ source_snapshot.json         # 미추적 파일을 포함한 빌드 입력 식별
├─ evidence/                    # 선택적으로 첨부한 생성 전 검사 보고서
├─ Windows/                     # Cook된 Unreal 게임과 실행 전제 프로그램
├─ cpp/host/build/Release/       # 서버 EXE와 app-local DLL
├─ cpp/host/config/              # 서버·기본 차량 설정
├─ map_packages/signal_city_v2/  # 지면·충돌·교통 데이터
└─ scripts/                     # 로컬 서버 실행 도우미
```

Microsoft 실행 런타임이 없는 PC에서는 먼저 `Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe`를 설치합니다. UE 5.6에 포함된 설치 프로그램을 함께 배포합니다. 그런 다음 배포 폴더에서 PowerShell 창 두 개를 열어 순서대로 실행합니다.

```powershell
# 첫 번째 창: 종료할 때 Ctrl+C
powershell -NoProfile -ExecutionPolicy Bypass -File .\StartServer.ps1

# 두 번째 창: 게임 실행
powershell -NoProfile -ExecutionPolicy Bypass -File .\StartClient.ps1
```

서버가 이미 같은 포트를 사용하고 있으면 실행기가 알리고 중단합니다. 기존 서버를 임의로 종료하거나 교체하지 않습니다. 서버를 연 창은 주행 중 열어 둡니다.

## 접속 설정

배포 루트의 `SimCoreClient.ini`는 다음 형식입니다.

```ini
[SimCoreClient]
ServerUrl=ws://127.0.0.1:9000/
MapPackageDirectory=map_packages/signal_city_v2
```

실행기는 이 INI의 절대 경로를 `-SimCoreClientConfig="경로"`로 게임에 전달합니다. MapPackage의 상대 경로는 **INI가 있는 폴더**를 기준으로 해석하므로, 저장소와 다른 위치나 공백이 있는 폴더로 옮겨도 같은 파일 구성이 유지됩니다.

현재 배포 범위는 같은 PC의 loopback WebSocket 연결입니다. 포트를 바꾸려면 INI의 `ServerUrl`과 `cpp/host/config/signal_city_server.cfg`의 `ws_port`를 함께 수정합니다. 클라이언트 설정은 서버의 포트를 바꾸지 않습니다. 서버 설정 파일의 기존 `../../../map_packages` 상대 경로를 보존하기 위해 배포 폴더도 해당 디렉터리 구조를 유지합니다.

## 완료 판정

패키지 생성 성공은 **빌드와 Cook, 파일 구성 성공**입니다. 자동으로 게임이나 서버를 켜거나 인수 테스트를 실행하지 않습니다. `package_manifest.json`에도 `assembled_unverified`, `manual_acceptance=not_run`으로 기록합니다.

작업이 끝난 뒤 다음을 별도로 확인해야 합니다.

- 저장소 밖의 공백 포함 경로에서 서버·클라이언트 실행 및 연결
- 네 차종 선택, 카메라 전환, 차량·캐릭터 에셋 누락 여부
- 지면·충돌·NPC·HUD·재연결·기록 재생
- 실제 렌더링 1080p·60fps와 입력 지연, 30분 연속 주행

배포 폴더에서 `StartClient.ps1 -PerformanceCapture`를 실행하면 10초 준비 후 1,800초의
frame/state CSV·JSON을 저장합니다. `-CaptureSeconds 60`은 짧은 경로 확인용이며 30분
인수가 아닙니다. 이 옵션은 조작을 대신하거나 VSync·프레임 제한을 변경하지 않습니다.
실행 전 측정 조건을 확인하고 [성능 캡처 분석](./core_validation.md#성능-캡처)과
[별도 입력 반응 실측](./input_latency_measurement.md)을 각각 수행합니다.

실행 계획과 실행기 템플릿만 검사하는 가벼운 검사는 아래와 같습니다. 게임을 실행하거나 빌드하지 않습니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test_package_windows.ps1
```

2026-09-17에 최신 Windows 패키지를 생성하고, 저장소 밖 공백 포함 경로에서 파일95개의
무결성과 NullRHI 서버 연결·지도 일치·상태 수신을 확인했습니다.
실제 화면·주행·다른 PC 배포는 아직 인수하지 않았습니다.
최신 결과와 생성 폴더는 [9/17 작업일지](./worklogs/2026-09-17.md)에,
이전 검사 이력은 [9/7 작업일지](./worklogs/2026-09-07.md)에 기록했습니다.

패키지 생성 후 화면을 띄우지 않고 접속·설정·지도 로딩만 확인하려면 다음을 실행합니다.
기존 9000 서버는 사용하지 않으며 임시 포트에 띄운 테스트용 서버와 게임만 종료합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_windows_package.ps1 -PackageDirectory '생성된 배포 폴더 경로'
```

이 검사는 실제 Cook된 게임을 사용하지만 NullRHI로 실행하므로 그래픽 품질·프레임 성능이나
주행 인수 결과가 아닙니다. 부팅·Hello·지도 일치와 상태 수신만 확인합니다.
실행 전에 manifest v2의 파일 해시도 확인합니다. 이전 v1 패키지는 최신 스크립트로 다시
생성해야 하며 파일 누락·변경이 있으면 게임을 실행하기 전에 중단합니다.
