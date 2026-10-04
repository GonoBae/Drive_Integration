# Drive Integration

[English](README.md) | [한국어](README.ko.md)

**Unreal Engine 5.6**과 별도 **C++ 시뮬레이션 서버**로 만든 Windows용 운전 시뮬레이션입니다.

Unreal은 입력과 화면 표시를 담당합니다. SimCore는 기본 초당 60회 차량 움직임과 교통을 계산합니다. 외부 차량 물리 SDK 없이 자체 계산식을 사용합니다.

## 주요 기능

| 구분 | 구현 내용 |
|---|---|
| 주행 | 가속·후진·조향·제동, 네 바퀴 접지와 서스펜션 |
| 차량 | 세단·경차·트럭·오토바이, 공용 JSON 설정 |
| 부품 | 세단 프리셋 2종, 타이어·서스펜션·동력계 개별 선택 |
| 교통 | NPC 차량 10대·보행자 8명, 신호·경로 선택·장애물 우회 |
| 충돌 | 차량 충돌, 국부 차체 변형, 보행자 낙상, 구조물 손상 |
| 표현 | 계기판, 카메라 3종, 운전자, 등화·경적·차량음·배기 |
| 진단 | 접지·충돌 디버그, 시각 기록·재생, 서버 물리 재생 |

맵은 국내 도로 규칙 일부를 반영한 가상 시험 코스이며, 실제 도로 체계를 모두 재현하지는 않습니다.

## 동작 구조

![현재 클라이언트와 서버 구조](docs/images/system-overview.svg)

차량의 최종 상태는 서버가 결정합니다. 현재 주행 계산은 Unreal에서 내보낸 지면·장애물·교통 데이터를 사용합니다. 코드에서는 이 파일 묶음을 **MapPackage**라고 부릅니다.

Unreal이 월드를 실시간 측정하는 구조로 전환 중입니다. 플레이어 지면·상자형 장애물·NPC와 보행자 지면을 각각 켤 수 있는 실험 모드를 추가했습니다. 기본 실행은 파일 기반이며, 전체 전환이 끝난 것은 아닙니다. [범위와 제한](docs/architecture.md#실시간-월드-측정-전환-계획)

## 실행 환경

- Windows 64-bit
- Unreal Engine 5.6
- Visual Studio 2022의 **Desktop development with C++**
- CMake 3.21 이상, Git, PowerShell

게임·서버 실행에는 Python이 필요하지 않습니다. 일부 개발 검사는 Python을 사용합니다.

## 빠른 시작

저장소 루트에서 PowerShell을 열고 실행합니다.

```powershell
.\cpp\host\scripts\setup.ps1
.\cpp\host\scripts\build.ps1
.\scripts\run_signal_city_server.ps1 -Background
```

설치 스크립트는 프로젝트 안에 vcpkg 의존성을 준비합니다. 빌드하면 `cpp\host\build\Release\simcore_publisher.exe`가 생성됩니다.

1. `unreal\DriveIntegration\DriveIntegration.uproject`를 Unreal Engine 5.6으로 엽니다.
2. C++ 모듈 빌드 안내가 나오면 진행합니다.
3. `/Game/SignalCity/Maps/L_SignalCity`를 열고 **Play**를 누릅니다.
4. HUD의 연결 상태가 **Active**인지 확인합니다.

저장된 맵은 바로 사용할 수 있으므로 일반 Play 전에 Bake를 다시 할 필요는 없습니다.

실행기는 서버 PID와 로그 경로를 표시합니다. 로그는 `runtime_logs/`에 저장됩니다. 해당 서버를 종료하려면 다음 명령을 사용합니다.

```powershell
Stop-Process -Id <PID>
```

포트 9000에는 서버 하나만 실행할 수 있습니다. 현재 터미널에서 로그를 보려면 `-Background`를 빼고 실행합니다.

### 실시간 지면 비교 모드

기존 서버를 종료한 뒤 진단 모드로 실행합니다.

```powershell
.\scripts\run_signal_city_server.ps1 -Background -GroundQueryShadow
```

다시 빌드한 Unreal 클라이언트에서 Play합니다. 서버의 `[GroundShadow]` 로그로 응답 시간과 지면 차이를 확인합니다. 개발용 비교 기능이며 실시간 물리 전환이 완료된 상태는 아닙니다. [측정 범위와 검사](docs/guides/core_validation.md#실시간-지면-측정-비교)

### 실험용 플레이어 지면

기존 서버를 종료하고 `scripts/run_signal_city_server.ps1 -Background -LiveGround`로 실행한 뒤, 새로 빌드한 Unreal에서 Play합니다. 차량 주변 지면을 측정해 서버의 기존 물리 계산에 공급합니다. 해당 지면을 다시 Bake할 필요는 없습니다.

직선 연석의 경계를 측정하고 속도에 따라 측정 범위를 조정합니다. 누락·만료·판단 불가능한 구간은 보류하며, 월드가 바뀌면 이전 측정을 무효화합니다. 서버 물리 기록에는 실제 사용한 측정 결과도 저장합니다.

상자형 장애물은 `-LiveObstacles`, NPC·보행자 지면은 `-LiveNpcGround`로 추가합니다. 두 옵션은 `-LiveGround`도 함께 켭니다. 지원하지 않는 장애물 형상은 무시하지 않고 실험 계산을 보류합니다. 시작 시 맵 검사·교통 규칙·구조물 피해에는 기존 데이터가 필요합니다. 지면 비교 옵션과는 함께 실행할 수 없으며, 일반적인 연석/고속 주행·겹친 지면·최신 패키지 인수는 남아 있습니다. [검사 방법](docs/guides/core_validation.md#실험용-플레이어-지면)

## 조작법

| 입력 | 동작 |
|---|---|
| `W` / `S` | 가속 / 제동, 정지 후 S를 계속 누르면 후진 |
| `A` / `D` | 좌우 조향 |
| `Space` | 후륜 사이드 브레이크 |
| `Q` / `E` / `X` | 좌·우 방향지시등 / 비상등 |
| `H` | 누르는 동안 경적 |
| `1` / `2` / `3` / `4` | 세단 / 경차 / 트럭 / 오토바이 선택 후 새 주행 |
| `G` | 차량 부품 선택 화면 열기·닫기 |
| `↑` / `↓` / `Enter` | 프리셋 또는 항목 선택 / 정차 상태에서 적용 |
| 화면 내 `Tab` / `←` / `→` | 개별 부품 편집 전환 / 선택 부품 변경 |
| 마우스 / 마우스 휠 | 카메라 회전 / 거리 변경 |
| `V` / `C` | 카메라 3종 전환 / 기본 추적 카메라 복귀 |
| `F3` | 접지·충돌 디버그 |
| `R` / `F6` | 기록 시작·종료 / 마지막 주행 시각 재생 |

부품을 적용하면 출발점에서 새 주행을 시작합니다. 같은 주행 중 재연결하면 부품 선택과 시뮬레이션 상태를 유지합니다.

## 테스트

서버 빌드 후 실행합니다.

```powershell
Set-Location .\cpp\host
ctest --preset release
Set-Location ..\..
```

통신 규격·Unreal·통합 검사는 [개발 검증 도구](scripts/README.md)를 참고하세요. 자동 검사가 실제 운전 감각·화면·소리 확인이나 30분 주행을 대신하지는 않습니다.

## 현재 한계와 진행 중인 작업

- 차량 물리는 간소화 모델이며 특정 실차의 시험 데이터에 맞춘 모델은 아닙니다. 오토바이 균형도 간소화되어 있습니다.
- NPC는 경로와 장애물 규칙으로 판단합니다. 카메라 영상을 인식하거나 플레이어와 같은 전체 서스펜션 모델을 계산하지 않습니다.
- 카탈로그에 등록한 부품을 선택합니다. 비교 프리셋은 현재 세단용이며 부품 질량을 차량 총질량·관성에 재합산하지 않습니다.
- 화면의 찌그러짐이 서버 충돌체를 변형하지는 않습니다.
- 같은 PC의 서버만 지원합니다. 기본 주소는 `ws://127.0.0.1:9000/`입니다.
- 실시간 측정은 아직 지면·충돌 Bake 데이터를 대체하지 않습니다. 실제 주행과 최신 배포본의 최종 확인은 별도 작업입니다.

로컬 서버 주소를 바꾸려면 `unreal\DriveIntegration\SimCoreClient.example.ini`를 같은 폴더의 `SimCoreClient.ini`로 복사하고 `ServerUrl`을 수정한 뒤 Play를 다시 시작합니다.

## 폴더와 상세 문서

| 폴더 | 역할 |
|---|---|
| `cpp/host/` | C++ 서버, 물리·교통 계산, 테스트 |
| `unreal/DriveIntegration/` | Unreal 프로젝트·맵·에셋·공유 차량 카탈로그 |
| `protocol/` | Protobuf 메시지 정의 |
| `map_packages/` | 현재 서버가 사용하는 지면·충돌·교통 데이터 |
| `scripts/` | Windows 실행기와 개발 검사 |
| `docs/` | 구조·현재 상태·사용법·에셋 출처 |

상세 기술 문서는 현재 한국어로 관리합니다.

- [현재 상태와 남은 작업](docs/status.md)
- [시스템 구조와 차량 설정](docs/architecture.md)
- [Windows 배포 안내](docs/guides/windows_package.md)
- [에셋 출처](docs/assets/README.md)
- [전체 문서 목록](docs/README.md)

교재와 이전 제작 산출물은 프로젝트 밖에 보관합니다. 빌드 결과·캐시·로그·개인 설정은 Git에서 제외합니다.
