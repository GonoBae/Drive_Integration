# Drive Integration — AI 없이 공부하는 하루

> Unreal 클라이언트 지원을 준비하며, 내가 보여 줄 프로젝트를 내 말로 설명하기 위한 교재입니다.
> 기준: 2026-09-08 현재 작업 폴더 · Windows · Unreal Engine 5.6 · Signal City

이 교재는 처음부터 끝까지 코드를 외우는 책이 아닙니다. **입력이 어디서 만들어지고, 누가 상태를 계산하며, 어떤 근거로 문제를 고쳤는지**를 찾아 설명하는 연습입니다. AI에게 다시 묻지 않아도 진행할 수 있도록 개념 설명, 읽을 함수, 실습, 예상 결과와 정답을 함께 넣었습니다.

오늘의 도착점은 다음 다섯 가지입니다.

- `W` 입력이 화면의 차량 이동으로 돌아오는 경로를 그림 없이 설명합니다.
- Unreal의 입력·표시와 서버의 물리·교통 계산을 구분합니다.
- 높은 FPS와 늦은 입력 반응이 동시에 생길 수 있는 이유를 설명합니다.
- 연석 구동력, NPC 탐색 지연, 제자리 회전 중 두 사례를 코드와 연결합니다.
- 직접 관찰한 결과와 자동검사 결과, 아직 확인하지 못한 부분을 구분해 기록합니다.

**하루에 C++·Unreal·자동차 물리를 모두 익히는 목표는 아닙니다.** 처음 보는 수학이나 문법은 표시만 하고 지나가도 됩니다. 각 시간대 끝의 산출물을 완성하는 것이 더 중요합니다.

## 하루 시간표

시작 시각은 옮겨도 됩니다. 아래는 **09:00~18:00, 학습 7시간 30분 + 식사·휴식 1시간 30분**입니다. 한 시간을 놓쳤다고 점심과 휴식을 없애지 마세요.

| 시간 | 순서 | 어디를 볼까요? | 끝나면 남길 것 |
|---|---|---|---|
| 09:00~09:20 | [0장: 준비](#chapter-0) | 이 교재, 프로젝트 README, 로컬 파일 | 학습 노트와 환경 확인 |
| 09:20~10:00 | [1장: 전체 구조](#chapter-1) | `main.cpp`, `simulation_host.hpp` | 책임 분담표·왕복 흐름 |
| 10:00~10:10 | 휴식 | 화면에서 눈을 떼세요 | — |
| 10:10~11:20 | [2장: Unreal 클라이언트](#chapter-2) | Pawn·ClientComponent·표시 함수 | W 입력 추적표 |
| 11:20~12:10 | [3장: 통신 계약](#chapter-3) | `vehicle.proto`, 세션 처리 | 정상·중복·잘못된 입력 판정표 |
| 12:10~13:10 | 점심 | — | — |
| 13:10~14:00 | [4장: 차량 물리](#chapter-4) | `VehiclePhysics`, 접지·구동력 | 단위 계산·연석 원인 설명 |
| 14:00~14:10 | 휴식 | — | — |
| 14:10~15:00 | [5장: 지연·재연결](#chapter-5) | `ControlLease`, `run_tick`, 작업일지 | 장애 시간선·원인/증상 구분 |
| 15:00~15:50 | [6장: NPC](#chapter-6) | 경로 선택·추종·우회·표시 | 결정 단계와 회전 오류 설명 |
| 15:50~16:00 | 휴식 | — | — |
| 16:00~16:45 | [7장: 관찰 실습](#chapter-7) | `L_SignalCity`, 로그 | 조건·관찰·판정 표 3개 |
| 16:45~17:25 | [8장: 테스트 읽기](#chapter-8) | 회귀검사 소스, 선택한 CTest | 테스트 한 개를 말로 번역 |
| 17:25~18:00 | [9장: 복습·설명](#chapter-9) | 노트, 정답과 자기평가 | 3분 설명·다음 복습 목록 |

**시간이 부족하면:** 0·1·2·3·5·9장을 우선합니다. 물리와 NPC는 각 장의 개념·사례·정답만 읽고 긴 함수 추적은 다음 날로 미룹니다. 하루를 완주하지 못해도 실제 학습하지 않은 장을 완료 표시하지 마세요.

<a id="chapter-0"></a>

## 0. 시작하기 — 토큰 없이 공부할 환경 만들기

### 0-1. 오늘 사용할 파일

- [이 교재의 브라우저용 보기](B:/Portfolio/Drive_Integration/docs/study/lessons/D01-one-day-project-guide.html): 로컬 HTML입니다. 더블클릭해서 브라우저로 열면 인터넷·AI 연결 없이 읽을 수 있습니다. 정답을 펼치고 인쇄할 수도 있습니다.
- [프로젝트 README](B:/Portfolio/Drive_Integration/README.md): 현재 실행 방법과 조작법을 먼저 봅니다.
- [최근 작업 기록](B:/Portfolio/Drive_Integration/docs/worklogs/2026-09-08.md): 무엇이 왜 바뀌었는지 확인할 때만 찾아봅니다.
- 이 교재에 연결한 코드: 링크가 에디터에서 열리지 않으면 파일 경로를 복사해서 직접 엽니다. 브라우저는 보안 설정에 따라 코드 파일을 다운로드하거나 열기를 막을 수 있습니다.

HTML은 외부 글꼴·인터넷 스크립트·로그인 없이 읽는 자료입니다. 아래 명령도 **이미 이 PC에 설치된 도구와 빌드 결과**를 쓰는 경로입니다. 처음 설치하거나 엔진 전체를 다시 받는 작업은 오늘 범위가 아닙니다.

### 0-2. 학습 노트 한 개를 만드세요

메모장이나 익숙한 편집기로 `내_프로젝트_학습노트.md`를 원하는 위치에 만듭니다. 다음 양식을 복사합니다.

```text
학습일 / 시작·종료 시각:
오늘 확인한 프로젝트 경로:

[파일을 읽을 때]
파일 / 함수:
이 함수가 받는 값:
읽거나 바꾸는 상태:
다음에 넘기는 값:
잘못된 입력일 때:
내 말로 한 문장:

[실습할 때]
조건 / 내 예상 / 실제 관찰 / 근거 / 아직 모르는 것:

[복습할 때]
책 없이 설명한 것:
다시 찾아본 것:
다음에 확인할 질문 3개:
```

읽은 줄 수 대신 이 노트를 채웁니다. 코드를 복사해 붙이는 것만으로는 이해했는지 판단하기 어렵습니다.

### 0-3. 오늘의 안전 규칙

현재 작업 폴더에는 아직 커밋하지 않은 개발 내용이 있습니다. **오늘의 기본 과정은 읽기·관찰·검사입니다.** 전체 초기화, 파일 삭제, 무작정 Pull, 설정 일괄 변경을 하지 않습니다.

- 코드·설정·맵을 수정하지 않아도 모든 필수 과제를 할 수 있습니다.
- Ground Bake, 맵 생성, 전체 빌드, 패키징은 오늘 필수가 아닙니다.
- 서버가 이미 실행 중인지 확인하기 전 두 번째 서버를 띄우지 않습니다.
- 디버거로 서버를 오래 멈추면 제어권이 만료될 수 있습니다. 이를 제품의 새로운 버그로 단정하지 마세요.
- 로그에 표시된 PID는 실행 때마다 달라집니다. 교재의 숫자를 외워 종료하지 않습니다.

### 0-4. 파일과 함수를 찾는 법

프로젝트 폴더를 편집기에서 열고 **전체 검색 `Ctrl+Shift+F`**에 교재의 함수 이름을 넣습니다. 찾은 함수에서 먼저 매개변수·반환형·실패 시 `return`을 보고, 그다음 본문을 읽습니다. 하나의 파일을 처음부터 끝까지 읽지 않습니다.

PowerShell에서도 찾을 수 있습니다. 오늘 명령은 어느 폴더에서 시작했는지 혼동하지 않도록 먼저 절대 경로로 이동합니다.

```powershell
Set-Location 'B:\Portfolio\Drive_Integration'
Select-String -Path '.\cpp\host\src\simulation_host.cpp' -Pattern 'void SimulationHost::run_tick'
```

`rg`를 사용할 수 있다면 아래처럼 더 빠르게 검색할 수 있습니다. 없으면 설치하지 말고 편집기나 위 명령을 쓰세요.

```powershell
rg -n 'SendControl|ApplyBinaryMessage|GetLatestState' unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp
```

**함수를 찾았는데 너무 길다면:** ① 입구의 검증, ② 핵심 다른 함수 호출, ③ 결과를 저장하거나 반환하는 부분만 찾으세요. 템플릿 구현·바이너리 파서 내부·메시 정점 생성 전체는 오늘 건너뛰어도 됩니다.

<a id="chapter-1"></a>

## 1. 전체 구조 — 누가 무엇을 결정하나요?

### 1-1. 먼저 기억할 왕복 흐름

```text
키보드 / 마우스
    ↓
Unreal Pawn: 입력과 운전 의도
    ↓
ClientComponent: 전송할 명령을 모으고 직렬화
    ↓  ControlCommand / WebSocket / 로컬 9000번 포트
C++ Host: 신원·순서·제어권 검사
    ↓
차량 물리 + 교통 + 충돌 계산
    ↓  WorldState: 차량·NPC·보행자·신호·상태 정보
Unreal ClientComponent: 받은 결과 검증
    ↓
Pawn / 표시 Actor / HUD / 소리
```

이 흐름을 **입력→계산→상태→표시의 왕복**이라고 부르겠습니다. Unreal 화면이 자동차처럼 보이게 하는 일과 자동차의 다음 물리 상태를 확정하는 일은 다른 책임입니다.

| 구분 | 이 프로젝트에서 하는 일 | 맡기지 않는 일 |
|---|---|---|
| Unreal 클라이언트 | 입력, 카메라, 차량·인물·신호 표시, HUD, 소리, 지면 측정·내보내기 | 플레이어의 최종 차량 물리를 독립적으로 다시 결정하기 |
| C++ 서버 | 입력 검증, 고정 시간 간격 계산, 차량 물리, 충돌, NPC·보행자·신호 상태 | 플레이어 화면의 픽셀·재질·카메라 직접 렌더링 |
| MapPackage | 측정된 지면·충돌 데이터와 일치 여부를 식별할 정보 제공 | 실행 중 의사결정을 수행하는 별도 프로그램 |
| Protobuf 스키마 | 주고받는 값의 형태·식별 번호·단위 계약 | 전송 빈도·운전 판단을 스스로 결정 |

Python 파일이 저장소에 있다고 해서 현재 수동운전에 Python 서버가 필수인 것은 아닙니다. 기본 경로는 Unreal과 C++ 서버의 직접 통신입니다. Python은 현재 검사 도구·생성 코드 등에도 쓰이며, 보존된 relay/observer 경로와 기본 실행 경로는 구분해야 합니다.

### 1-2. 읽을 순서: 15분만 사용하세요

1. [README](B:/Portfolio/Drive_Integration/README.md): `실행 방법`, `폴더 구성`만 읽습니다.
2. [main.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/main.cpp): `SimulationHost`, `WsServer`, `run`을 검색해 객체를 연결하는 위치를 찾습니다. 명령행 해석 전체는 건너뜁니다.
3. [simulation_host.hpp](B:/Portfolio/Drive_Integration/cpp/host/src/simulation_host.hpp): `SimulationHostConfig`, `SimulationHostCallbacks`, `class SimulationHost`의 공개 함수를 읽습니다. 큰 private 데이터 목록은 지금 외우지 않습니다.
4. [signal_city_server.cfg](B:/Portfolio/Drive_Integration/cpp/host/config/signal_city_server.cfg): `physics_hz=60`, `ws_port=9000`, `npc_count=10`과 타임아웃 3개를 찾습니다. 수정하지 않습니다.

`main.cpp`는 식당에 비유하면 주방·주문창구·계산대를 연결하는 개점 준비입니다. `SimulationHost`는 계산 순서를 조정합니다. `VehiclePhysics`는 실제 차량 상태를 계산합니다. `main.cpp`에서 모든 물리식을 찾으려 하면 길을 잃기 쉽습니다.

### 1-3. 처음 보는 C++는 이 정도만 해석하세요

| 표기 | 오늘 필요한 해석 |
|---|---|
| `class`, `struct` | 관련 상태와 함수를 묶은 타입입니다. 기본 접근 권한은 다르지만 오늘은 역할과 데이터부터 봅니다. |
| `const T&` | 큰 값을 복사하지 않고 참조하며, 그 경로로 값을 바꾸지 않겠다는 뜻입니다. |
| `T*`, `nullptr` | 객체 위치를 가리킬 수 있고, 없을 수도 있습니다. 사용 전 검사가 왜 있는지 봅니다. |
| `std::optional<T>` | 결과가 있을 수도 없을 수도 있습니다. 실패를 숫자 0과 혼동하지 않도록 합니다. |
| `std::vector<T>` / `TArray<T>` | 여러 값을 담는 컨테이너입니다. 바퀴 목록·엔티티 목록에서 찾아봅니다. |
| `return false`, `std::nullopt` | 실패나 사용 불가를 알리는 경로일 수 있습니다. 호출자가 이를 어떻게 처리하는지 같이 봅니다. |
| `[this](...) { ... }` | 나중에 호출할 동작을 연결하는 람다일 수 있습니다. 등록 시점과 실행 시점이 다릅니다. 수명 관리까지 오늘 모두 외우지는 않습니다. |

### 1-4. 직접 해 보세요

노트에 위 왕복을 보지 않고 다시 적고, 화살표마다 **명령인지 상태인지**를 표시합니다. 그다음 다음 질문에 답합니다.

1. 카메라를 돌렸을 때 서버 차량의 물리 위치가 바뀌어야 하나요?
2. 서버를 종료하면 Unreal이 같은 물리를 대신 계산하며 계속 주행해야 하나요?
3. 지면을 Unreal에서 측정한다는 말과 서버가 접지력을 계산한다는 말은 모순인가요?

<details>
<summary>1장 정답과 해설</summary>

1. 아닙니다. 카메라 조작은 표시 책임입니다. 운전 입력과 섞지 않아야 화면을 돌려도 차량이 움직이지 않습니다.
2. 아닙니다. 서버의 권한 있는 상태를 받을 수 없게 되므로 정상 운전 상태가 아니어야 합니다. 오래된 명령과 상태를 계속 믿고 움직이는 것은 별도 문제입니다.
3. 모순이 아닙니다. Unreal이 지형에서 높이·법선·재질 등을 측정해 데이터를 내보내고, 서버는 그 지면 자료와 차량 상태를 사용해 접촉·하중·힘을 계산합니다. 이 프로젝트를 ‘매 프레임 지면 레이를 네트워크로 묻는 구조’라고 설명해서는 안 됩니다.

</details>

<a id="chapter-2"></a>

## 2. W 입력이 화면 속 자동차가 되기까지 — Unreal 70분

이 장의 목표는 Unreal 기능을 많이 외우는 것이 아닙니다.
**“제가 W를 누르면 어떤 데이터가 어디로 가고, 누가 자동차의 위치를 결정하나요?”**를 소스에 근거해 설명하시면 됩니다.
오늘은 코드를 수정하거나 서버를 실행하지 않습니다. 편집기의 파일 검색과 함수 검색, 개인 노트만 사용하세요.
함수 안에서 모르는 이름이 나와도 곧바로 정의를 전부 따라가지 마세요. 아래에서 지정한 범위까지만 읽습니다.

### 2.1 읽는 순서와 시간 배분

| 시간 | 할 일 | 끝났다는 기준 |
|---|---|---|
| 0~8분 | Pawn과 Component의 역할 구분 | 객체 세 종류의 차이를 한 문장씩 적습니다. |
| 8~18분 | 생성자와 플레이 생명주기 읽기 | 시작·반복·종료 시 작업을 구분합니다. |
| 18~36분 | W 입력과 송신 추적 | `SetControl`과 `SendControl`의 차이를 설명합니다. |
| 36~48분 | 수신과 화면 표시 추적 | `LatestState`가 갱신되는 지점을 찾습니다. |
| 48~58분 | 좌표와 회전 부호 계산 | ENU 위치와 좌회전 부호를 손으로 변환합니다. |
| 58~70분 | 실습 노트 정리와 자기 확인 | 노트 3개와 질문 4개를 완성합니다. |

링크의 줄 번호는 작성 당시 소스를 확인한 위치입니다. 이후 코드가 바뀌면 함께 적힌 **찾을 심볼**을 검색하세요.
파일 전체를 위에서 아래로 읽는 순서가 아닙니다. 지정한 함수가 끝나면 다음 항목으로 넘어가셔도 됩니다.

### 2.2 차 한 대 안에서 Actor, Pawn, Component 구분하기 — 0~8분

먼저 [ExternalVehiclePawn.h](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/ExternalVehiclePawn.h:25)를 여세요.
찾을 심볼은 `AExternalVehiclePawn : public APawn`, `PresentationRoot`, `VehicleMesh`, `SimCoreClient`입니다.

- `Actor`는 레벨 안에 배치하거나 생성하는 객체의 기본 단위입니다. 자동차뿐 아니라 신호등도 Actor로 표현할 수 있습니다.
- `Pawn`은 Controller가 소유하여 조종할 수 있는 Actor입니다. 현재 플레이어 자동차는 `APawn`을 상속합니다.
- `Component`는 Actor에 붙여 기능을 구성하는 단위입니다. 통신, 화면에 보이는 메시, 카메라 등을 나누어 둡니다.

“자동차가 Pawn이다”와 “Pawn 클래스 하나가 자동차의 모든 일을 직접 한다”는 다른 이야기입니다.
현재 Pawn은 여러 Component를 만들고 연결합니다. 네트워크 처리는 `USimCoreClientComponent`에 맡깁니다.
메시와 카메라는 위치 관계가 필요하므로 `USceneComponent` 계열을 사용합니다.
반면 통신용 `USimCoreClientComponent`는 `UActorComponent`를 상속합니다. 통신 기능 자체에 별도의 공간 좌표가 필요한 것은 아닙니다.

이어서 [SimCoreClientComponent.h](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.h:29)를 여세요.
찾을 심볼은 `UCLASS`, `GENERATED_BODY`, `UPROPERTY`, `ServerUrl`, `MapPackageChecksum`입니다.

`UCLASS`와 `GENERATED_BODY`는 일반 C++ 클래스를 Unreal의 객체·리플렉션 시스템에 연결하는 선언입니다.
`UPROPERTY`는 멤버에 대한 정보를 Unreal에 알려 줍니다. 에디터 노출, 직렬화, UObject 참조 추적 등과 관련됩니다.
여기에 어떤 지정자를 붙였는지에 따라 실제 사용 방식이 달라집니다.

현재 `ServerUrl`의 `EditAnywhere, BlueprintReadWrite`와 `MapPackageChecksum`의 `VisibleAnywhere, BlueprintReadOnly`를 비교해 보세요.
앞쪽은 편집 가능한 설정이고, 뒤쪽은 실행 중 확인한 값을 보여 주는 용도라는 차이가 드러납니다.
**`UPROPERTY`를 붙였다고 이 서버로 값이 자동 전송되지는 않습니다.** 이 프로젝트의 송신 코드는 뒤에서 따로 찾습니다.
또한 헤더에 적힌 초기값만 보고 현재 접속 설정을 단정하지 마세요. `PrepareRuntimeSettings`가 실행 시 설정을 반영합니다.

### 2.3 생성자, BeginPlay, Tick, EndPlay — 8~18분

[ExternalVehiclePawn.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/ExternalVehiclePawn.cpp:31)에서 생성자의 앞부분만 읽으세요.
찾을 심볼은 `AExternalVehiclePawn::AExternalVehiclePawn`, `CreateDefaultSubobject`, `SetupAttachment`, `AutoPossessPlayer`입니다.

생성자에서는 기본 구성과 기본값을 준비합니다. `PresentationRoot`를 만들고, 메시와 여러 표시용 Component를 연결합니다.
`SetupAttachment`는 부모·자식의 공간적 관계를 정합니다. “통신 순서”나 “함수 호출 순서”를 정하는 함수가 아닙니다.
`AutoPossessPlayer = Player0`은 첫 번째 로컬 플레이어가 이 Pawn을 소유하도록 설정한 부분입니다.
`VehicleMesh->SetCollisionEnabled(NoCollision)`도 찾으세요. 현재 플레이어 메시가 Unreal의 독립적인 차량 물리 시뮬레이션을 주도하는 구조는 아닙니다.
그렇다고 “이 프로젝트에서 Unreal의 충돌 기능을 전혀 사용하지 않는다”는 뜻은 아닙니다. 지금 보고 있는 메시와 책임 범위를 한정해서 읽어야 합니다.

다음 두 파일에서 같은 생명주기 이름이 어떻게 다르게 쓰이는지 비교하세요.

| 읽을 곳과 찾을 심볼 | 이 코드에서 하는 일 |
|---|---|
| [Pawn](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/ExternalVehiclePawn.cpp:154)의 `BeginPlay` | 깜빡이 판정을 초기화하고 표시·오디오를 준비합니다. |
| [Client Component](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:50)의 `BeginPlay` | 새 `PlaySessionId`를 만들고 `Connect`를 호출합니다. |
| [Pawn](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/ExternalVehiclePawn.cpp:176)의 `Tick` | 조향 입력을 갱신하고 최신 서버 상태로 화면을 갱신합니다. |
| [Client Component](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:70)의 `TickComponent` | 연결·송신·NPC·신호·진단 갱신을 호출합니다. |
| [Client Component](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:58)의 `EndPlay` | 성능 기록을 끝내고 연결을 정리합니다. |

`BeginPlay`는 플레이가 시작될 때의 준비, `Tick`은 활성화된 객체의 프레임별 작업, `EndPlay`는 플레이 종료 등에 따른 정리라고 구분하세요.
생성자와 `BeginPlay`를 같은 것으로 생각하면 “왜 객체를 구성할 때 서버에도 접속하지 않나요?”라는 혼동이 생깁니다.
현재 코드는 구성은 생성자에서, 실행 중 연결은 Component의 `BeginPlay`에서 시작합니다.

`DeltaSeconds` 또는 `DeltaTime`은 해당 프레임에 전달된 시간 간격입니다.
“Tick이 60번 호출된다”와 “매번 1만큼 이동한다”를 결합하면 프레임 수에 따라 움직임이 달라집니다.
그러나 이 Pawn의 이동은 그런 방식이 아닙니다. 위치의 출발점은 서버에서 받은 상태라는 점을 기억하세요.

### 2.4 W를 한 번 누르는 과정 추적하기 — 18~36분

이번에는 “차량이 거의 정지했고, 정상 연결 상태에서 W만 누른다”는 상황으로 범위를 제한합니다.
후진 중의 W는 제동으로 해석될 수 있으므로 처음부터 모든 분기를 동시에 읽지 않으셔도 됩니다.

#### 첫 번째 연결: 물리 키에서 입력 이름으로

[DefaultInput.ini](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Config/DefaultInput.ini:87)에서 `AxisName="Throttle"`, `Key=W`를 찾으세요.
여기서는 실제 키와 `Throttle`이라는 입력 이름을 연결합니다.
그다음 [SetupPlayerInputComponent](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/ExternalVehiclePawn.cpp:278)에서 `BindAxis(TEXT("Throttle"), ..., SetThrottle)`를 찾으세요.
이 두 곳이 이어져야 W의 축 값이 `SetThrottle`에 도달합니다.
현재 W는 눌림 이벤트 하나를 보내는 `BindAction`이 아니라 축 값을 읽는 `BindAxis`입니다. “W 한 번 = 네트워크 패킷 한 개”가 아닙니다.

#### 두 번째 연결: 입력 값에서 운전 명령으로

[SetThrottle](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/ExternalVehiclePawn.cpp:770)은 값을 0~1로 제한하고 `ForwardPedalInput`에 저장한 뒤 `PushControl`을 부릅니다.
그 함수 안에는 자동차의 위치를 직접 바꾸는 코드가 없습니다.
이어서 [PushControl](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/ExternalVehiclePawn.cpp:850)을 읽으세요.
찾을 심볼은 `ForwardPedalInput`, `AuthoritativeLongitudinalSpeedMps`, `Resolve`, `SimCoreClient->SetControl`입니다.

입력 해석은 [SimCoreVehicleControlResolver.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreVehicleControlResolver.cpp:5)의 `Resolve`에 분리되어 있습니다.
오늘은 `bForwardPressed` 분기만 따라가세요. 현재 기어와 최근 서버 속도를 보고 가속할지, 먼저 제동할지 판단합니다.
따라서 `ForwardPedalInput`은 사용자의 전진 의도이고, 최종 `Output.Throttle`은 상황을 반영한 가속 명령입니다. 둘은 항상 같지는 않습니다.
참고로 `SetBrake`라는 이름은 현재 `ReversePedalInput`을 갱신합니다. 이름만 보고 S가 언제나 제동만 한다고 단정하지 마세요.

#### 세 번째 연결: 명령 저장에서 실제 송신으로

[SimCoreClientComponent.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:124)의 `SetControl`을 읽으세요.
찾을 심볼은 `PendingControl`, `bControlDirty`입니다. 이 함수는 보낼 값을 저장하고 변경 여부를 표시합니다.
**이 함수에는 `Socket->Send`가 없습니다.** 입력을 처리하는 일과 실제 송신 시점을 정하는 일이 나뉘어 있습니다.

이어서 같은 파일의 [TickControlTransmission](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:297), [ShouldSendControl](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:740), `SendControl` 순서로 읽으세요.
`ShouldSendControl`은 연결·지도 확인 여부와 송신 간격을 확인합니다. 변경된 입력뿐 아니라 주기적인 명령 갱신도 송신 조건입니다.
설정 기본값은 [CommandRateHz와 MaxChangedCommandRateHz](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.h:60)에서 확인할 수 있습니다.
20Hz와 30Hz는 해당 경로의 기본 주기·제한값입니다. 에디터 프레임률이나 실제 송신 횟수가 무조건 그 숫자로 고정된다는 뜻은 아닙니다.

[SendControl](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:749)은 현재 명령에 시각을 넣고 직렬화한 뒤 이진 메시지를 보냅니다.
직렬화 함수는 [SimCoreProtocol.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreProtocol.cpp:934)의 `SerializeControlEnvelope`입니다.
바이트를 만드는 세부 함수는 오늘 외우지 마세요. 입력 구조체가 메시지로 바뀌는 경계라는 점까지만 확인합니다.

다음은 실제 코드의 호출 관계를 줄인 **개념 의사코드**입니다. 그대로 복사해 실행하는 코드는 아닙니다.

```text
W 축 값 → SetThrottle → PushControl → Resolve
       → SetControl: PendingControl 갱신
Component Tick → ShouldSendControl → SendControl → 서버
서버 계산 결과 → ApplyBinaryMessage → LatestState
Pawn Tick → BuildVehicleSample → SetActorLocationAndRotation
```

여기서 중요한 분리는 `PendingControl`과 `LatestState`입니다.
전자는 “이렇게 운전하고 싶습니다”라는 명령, 후자는 “계산 결과 현재 이렇게 되었습니다”라는 상태입니다.
전진 입력이 1이라고 속도가 즉시 최대가 되는 것이 아닙니다. 서버 쪽 계산은 3장에서 이어서 살펴봅니다.

### 2.5 서버에서 온 값이 화면에 나타나는 과정 — 36~48분

[SimCoreClientComponent.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:637)에서 `OnBinaryMessage`를 찾으세요.
이벤트가 받은 조각 데이터를 복사하고, `ApplyOnGameThread`를 통해 `ApplyBinaryMessage`로 전달하는 부분만 읽습니다.
수신 이벤트 안에서 아무 Unreal 객체나 즉시 변경하는 것이 아니라 게임 스레드에서 적용하도록 넘기는 경계가 있습니다.

이어서 [ApplyBinaryMessage](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:903)를 읽되 아래 네 가지 지점만 찾으세요.

1. `IncomingMessage.Append`와 마지막 조각 확인: 완성된 메시지를 만든 뒤 해석합니다.
2. `ParseWorldStateEnvelope`: 바이트를 현재 차량과 주변 엔티티 상태로 해석합니다.
3. `Parsed.MapPackageChecksum`, `Parsed.PlaySessionId`, `Parsed.Sequence`: 다른 지도·다른 Play·오래된 순서의 상태를 걸러냅니다.
4. `LatestState = MoveTemp(Parsed)`: 통과한 최신 상태와 도착 시각을 저장합니다.

따라서 화면에서 보이는 값이 “네트워크에서 방금 받은 아무 값”은 아닙니다.
현재 지도와 플레이에 맞는지 확인한 상태입니다. `MoveTemp`의 상세 문법은 오늘의 핵심이 아니므로 “수신 결과를 최신 상태로 넘기는 지점”으로 표시해 두세요.
`LatestState`는 끝없이 쌓는 재생 목록이 아니라 Pawn이 조회할 최신 상태입니다.

다시 [GetLatestState](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp:139)와 [Pawn의 Tick](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/ExternalVehiclePawn.cpp:176)을 여세요.
`GetLatestState`는 상태 사본과 로컬 수신 후 경과시간을 반환합니다. 이 경과시간이 `StateAgeSeconds`입니다.
이는 입력을 누른 시점부터 결과를 받기까지의 왕복시간과 같은 측정값이 아닙니다.
Pawn은 상태가 없을 때 오디오 등을 정리하고 빠져나가지만, 카메라 갱신은 계속 호출합니다. 연결 문제와 카메라 조작을 분리한 예입니다.

상태가 있으면 [SimCorePresentation.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCorePresentation.cpp:35)의 `BuildVehicleSample`이 표시할 위치·회전을 만듭니다.
이 함수는 최근 상태의 속도 등을 이용해 제한된 시간만큼 표시 위치를 예측하고, 좌표계를 변환합니다.
새 가속력이나 타이어 힘을 계산해서 서버의 차량 상태를 대신 만드는 함수는 아닙니다.
마지막으로 Pawn의 `SetActorLocationAndRotation`이 그 표시 결과를 적용합니다.
이 호출에서 `Sweep`은 `false`입니다. 이 줄을 “Unreal 충돌 검사로 차량의 최종 이동을 결정한다”고 읽으면 책임을 반대로 이해하게 됩니다.

연결 절차는 오늘 다음 정도만 기억하셔도 충분합니다.
소켓 연결 뒤 Hello로 호환성을 확인하고, 지도 일치 확인 뒤 해당 Play의 Reset과 Control을 보냅니다.
관련 심볼은 `ApplyConnected`, `ValidateServerHello`, `bMapHandshakeComplete`, `SendSimulationReset`입니다.
“소켓이 연결됐다”와 “서버가 이 입력으로 운전하도록 허용한다”는 같은 뜻이 아닙니다. 권한·안전 제어의 자세한 내용은 3장에서 확인합니다.

### 2.6 좌표와 회전 부호를 손으로 계산하기 — 48~58분

[SimCoreCoordinateFrames.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreCoordinateFrames.cpp:133)의 `MapEnuPositionMetersToUnrealCentimeters`부터 읽으세요.
한 단계 위로 올라가 `MapEnuPolarVectorToUnrealWorld`가 축을 어떻게 바꾸는지도 확인합니다.

| 데이터 | 프로젝트에서 사용하는 뜻 | 예시 |
|---|---|---|
| 서버의 `PositionEnu` | X=동, Y=북, Z=위, 단위 m | `(2, 5, 1)` |
| Unreal 표시 위치 | X=북, Y=동, Z=위, 단위 cm | 오프셋이 0이면 `(500, 200, 100)` |
| 차량 로컬 FLU | X=앞, Y=왼쪽, Z=위 | 좌측 방향 성분은 양수입니다. |
| Unreal Actor 로컬 축 | X=앞, Y=오른쪽, Z=위 | FLU의 Y 성분은 부호를 바꿉니다. |

“m에 100을 곱한다”만 기억하면 축 교환을 놓칩니다. 위치 변환에는 축 순서와 단위가 모두 들어갑니다.
`PresentationOffsetCentimeters`는 변환한 위치에 더하는 표시용 오프셋입니다. 이 값까지 m로 착각해 100배 하지 마세요.

다음은 같은 파일의 [InputAxisToCanonicalSteering](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreCoordinateFrames.cpp:245)과 `CanonicalSteeringToUnrealYawDegrees`입니다.
오른쪽이 양수인 입력 축은 서버 명령의 “왼쪽 양수” 규칙으로 바뀝니다.
실제 바퀴의 조향각도 왼쪽이 양수인 rad 값이며, Unreal 표시용 yaw로 바꿀 때는 도 단위로 바꾸고 부호를 뒤집습니다.
예를 들어 조향각 `+0.1rad`는 약 `-5.73°`의 Unreal 바퀴 yaw에 해당합니다. 이 음수가 “차가 반대로 조향한다”는 뜻은 아닙니다.

마지막으로 [BuildUnrealActorRotation](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreCoordinateFrames.cpp:326)을 찾아 주석과 `PredictedAttitude`만 읽으세요.
차량 전체의 heading은 북쪽 0°, 동쪽 90°인 시계방향 방위각입니다. 양수인 좌회전 yaw rate를 적용하면 heading은 감소합니다.
하지만 차체 회전은 pitch·roll도 함께 변환하므로, 모든 회전에 무조건 마이너스 하나를 붙이는 규칙으로 일반화하면 안 됩니다.
오늘은 행렬·쿼터니언 유도를 하지 않습니다. 입력 부호, 바퀴 조향각, 차량 방위각이 서로 다른 종류의 값이라는 점까지 정리하세요.

### 2.7 실습 노트 3개 — 58~64분

노트는 정답을 보기 전에 작성하세요. 코드 변경 없이 각 2분씩 사용합니다.

1. **입력 추적 노트:** `키/입력 이름/입력 저장 멤버/최종 명령 멤버/실제 송신 함수` 다섯 칸을 만들고 W의 경로를 채우세요. “직접 위치를 바꾸는가?”도 각 칸 옆에 적으세요.
2. **상태 추적 노트:** `상태 수신/검증/저장/조회/표시 적용` 다섯 칸에 해당 심볼을 적으세요. 상태가 없을 때 계속 가능한 기능 하나를 덧붙이세요.
3. **좌표 노트:** 오프셋 0일 때 ENU `(3, 8, 0.5)`의 Unreal 위치, 오른쪽 입력 `+0.4`의 서버 조향 명령, 바퀴 조향 `+0.2rad`의 Unreal yaw를 계산하세요.

<details>
<summary>실습 노트의 대조용 답과 해설</summary>

1. `W / Throttle / ForwardPedalInput / PendingControl.Throttle / SendControl`입니다. 중간에 `PushControl → Resolve → SetControl`이 있습니다. 나열한 함수는 차량 위치를 직접 바꾸지 않습니다. 상황에 따라 `Resolve`의 결과가 제동일 수도 있습니다.
2. `OnBinaryMessage·ApplyBinaryMessage / map·play·sequence 확인 / LatestState / GetLatestState / BuildVehicleSample·SetActorLocationAndRotation`입니다. 상태가 없어도 카메라 갱신은 호출됩니다.
3. 위치는 `(800, 300, 50)cm`, 서버 조향 명령은 `-0.4`, 바퀴 yaw는 약 `-11.46°`입니다. 조향 명령 `-0.4`는 정규화된 명령이지 `-0.4rad`가 아닙니다.

</details>

### 2.8 자기 확인 질문 4개 — 64~70분

1. `SetThrottle`에서 `PendingControl`까지 값이 바뀌었는데 아직 `Socket->Send`가 호출되지 않을 수 있는 이유는 무엇인가요?
2. `UPROPERTY`를 붙인 `ServerUrl`은 서버로 자동 복제되나요? 아니면 실제 메시지를 만드는 코드가 따로 있나요?
3. 서버가 보내는 속도가 계속 0인데 W 입력만으로 Pawn을 이동시키면, 지금 구조에서 어떤 두 결과가 충돌하나요?
4. “좌회전인데 Unreal의 바퀴 yaw가 음수이고 heading도 줄어드니 버그다”라는 설명은 왜 틀렸나요?

<details>
<summary>자기 확인 답과 해설</summary>

1. `SetControl`은 명령을 저장하고 변경 여부만 표시합니다. Component Tick에서 연결·지도 확인·송신 간격 조건을 통과해야 `SendControl`이 호출됩니다. 같은 값도 주기적인 명령 갱신으로 다시 보낼 수 있습니다.
2. 자동 복제되지 않습니다. 해당 지정자는 Unreal의 편집·접근 등에 관한 정보입니다. 현재 프로젝트의 통신 데이터는 `SerializeControlEnvelope` 같은 명시적인 직렬화 함수와 `Socket->Send`를 거칩니다.
3. 서버의 권위 있는 차량 상태와 클라이언트가 임의로 만든 위치가 충돌합니다. 다음 서버 상태에서 위치가 되돌아오거나, 보이는 차량과 서버 충돌 판정이 달라질 수 있습니다. 현재 표시는 서버 상태와 제한된 예측을 기준으로 합니다.
4. 서버의 조향각은 왼쪽 양수, Unreal 바퀴 yaw 변환은 그 반대 부호입니다. heading은 시계방향 방위각이므로 좌회전하면 감소합니다. 값의 기준축과 단위를 먼저 확인해야 합니다.

</details>

마지막으로 노트를 덮고 “입력은 명령이고, 수신 값은 결과이며, Pawn은 그 결과를 변환해 표시한다”를 자신의 말로 설명해 보세요.
`SetControl`과 `SendControl`, `PendingControl`과 `LatestState`를 구분하고 실제 파일에서 찾을 수 있으면 이 장의 목표를 달성한 것입니다.
막힌 부분은 모르는 함수 이름을 전부 모으지 말고, “어느 값이 어디서 결정되는지 모르겠다”라는 질문 한 문장으로 적은 뒤 3장으로 넘어가세요.

<a id="chapter-3"></a>

## 3. 통신 계약 — 연결됐다는 것과 운전 가능하다는 것은 다릅니다

### 3-1. 오늘 읽을 정확한 범위

1. [vehicle.proto](B:/Portfolio/Drive_Integration/protocol/vehicle.proto): `ControlCommand`, `EntityState`, `WorldState`, `Hello`, `Health`, `SimulationReset`, `Envelope` 선언.
2. [simulation_host_session.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/simulation_host_session.cpp): `handle_client_message`, `handle_control_command`. 처음부터 파싱 내부를 외우지 말고 거부되는 조건을 세 개만 찾습니다.
3. [vehicle_messages.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/protocol/vehicle_messages.cpp): `SerializeAsString`, `set_`을 검색해 C++ 상태가 메시지로 바뀌는 위치를 찾습니다.
4. [SimCoreClientComponent.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp): `SendHello`, `SendSimulationReset`, `ApplyBinaryMessage`에서 수신 검증과 상태 저장을 확인합니다.

### 3-2. 메시지를 택배로 생각해 보세요

`ControlCommand`는 ‘이렇게 운전하고 싶다’는 요청입니다. `EntityState`는 ‘계산 결과 지금 이 상태다’라는 결과입니다. `Envelope`는 누가·어느 세션에서·어떤 순서로 보냈는지 적힌 겉봉투입니다.

| 메시지 | 핵심 의미 | 대표 방향 |
|---|---|---|
| `Hello` | 서로 지원하는 규격과 기능을 확인 | 양방향 |
| `SimulationReset` | 새 Play·선택 차종에 맞는 시작 요청 | Unreal → 서버 |
| `ControlCommand` | 스로틀·브레이크·조향·기어 등 요청 | Unreal → 서버 |
| `WorldState` | 같은 스냅샷의 엔티티·신호·상태 정보 | 서버 → Unreal |
| `Health` | 제어권·안전 정지·상태 진단 | 서버 → Unreal; 현재는 `WorldState.health`에 포함 |

WebSocket 연결 성공은 통신 통로가 생겼다는 뜻입니다. **서로 맞는 프로토콜·지도·Play 상태인지 확인하고 유효한 입력을 받아야** 운전할 수 있습니다. 지도 이름이 같다는 것만으로 같은 지면 데이터라는 뜻도 아닙니다. checksum은 서로 같은 데이터를 사용하고 있는지 검증하는 근거입니다.

### 3-3. 숫자 세 가지를 구분하세요

실제 스키마에는 다음 선언이 있습니다.

```proto
repeated WheelState wheels = 21;
```

- `repeated`: `WheelState`가 여러 개 담길 수 있습니다.
- `21`: 이 필드를 식별하는 **필드 번호**입니다. 바퀴 21개라는 뜻이 아닙니다.
- 각 WheelState의 `wheel_index` 값: 0~3 바퀴 위치 계약입니다.

코드에서 변수 이름을 예쁘게 바꾸는 일과 공개된 필드 번호를 바꾸는 일은 위험이 다릅니다. 이미 저장한 기록과 다른 버전의 프로그램이 같은 숫자를 다른 뜻으로 읽을 수 있기 때문입니다. 오늘은 스키마를 수정하거나 생성 파일을 직접 고치지 않습니다.

`oneof`는 한 Envelope가 여러 종류의 payload를 동시에 싣는 목록이 아니라, 정의된 선택지 중 한 종류를 담게 하는 구조입니다. WorldState 안에 여러 엔티티가 있는 것과 Envelope의 payload 선택은 다른 수준입니다.

### 3-4. 세션과 순서를 손으로 추적하세요

다음 값은 역할이 다릅니다.

| 값 | 막으려는 혼동 |
|---|---|
| `source_id` | 누가 보내는 요청인지 |
| `session_id` | 같은 프로그램 이름의 이전 연결인지 새 연결인지 |
| `play_session_id` | 같은 Play 재연결인지 새 Play 시작인지 |
| `sequence` | 같은 흐름에서 이미 처리한 메시지인지 |
| `simulation_time_ns` | 서버 계산 시간상 언제의 상태인지 |
| `client_time_ns` | 입력 생성 시간의 진행과 밀림을 판단할 단서 |
| 지도 checksum | 서로 다른 지면·충돌 패키지를 같은 세계로 착각하는지 |

예를 들어 같은 Play에서 연결만 회복했는데 차량을 항상 출발점으로 초기화하면 운전 중 위치가 튑니다. 반대로 End Play 후 새 Play인데 이전 사고 상태를 그대로 남겨도 이상합니다. 그래서 연결 세션과 Play 세션을 구분합니다.

**숫자 예제:** 같은 활성 연결에서 명령 101을 받은 다음 100이 도착했습니다. 100의 스로틀 값이 더 크다고 해서 최신 명령은 아닙니다. 이미 처리한 순서보다 오래된 값이라면 거부해야 합니다. 오래된 입력을 다시 적용하면 사용자가 키를 놓은 뒤 가속이 재개될 수도 있습니다.

### 3-5. 연습: 처리 결과를 먼저 적으세요

아래는 계약 이해를 위한 단순화 사례입니다. 실제 코드는 Hello, 세션, 제어권 등의 전제 조건을 함께 검사합니다.

| 사례 | 내 예상: 수락 / 거부 / 초기화 / 보존 | 이유 |
|---|---|---|
| 같은 연결에서 200 처리 후 199 입력 | | |
| 새 연결인데 이전 Play ID로 정상 재연결 | | |
| 새 Play ID의 정상 초기화 요청 | | |
| 클라이언트와 서버 지도 checksum이 다름 | | |
| 알 수 없는 메시지 필드를 받음 | | |

<details>
<summary>3장 정답과 해설</summary>

1. 199 입력은 오래된 순서이므로 거부해야 합니다. 거부한 패킷이 ‘마지막 정상 입력’ 기록까지 갱신해서는 안 됩니다.
2. 정상적인 재연결 협상과 제어권 획득 절차를 거친다면 같은 Play의 물리 상태를 보존합니다. 새 소켓이면 어떤 입력이나 즉시 허용한다는 뜻은 아닙니다.
3. 유효한 새 Play 초기화 요청은 출발 상태로 재설정하는 대상입니다. 이전 세션의 지연 패킷이 나중에 이를 덮어쓰지 못해야 합니다.
4. 같은 세계라고 믿고 계속 운전시키지 않아야 합니다. 파일 이름을 맞추는 것만으로 해결됐다고 판단하지 않습니다.
5. Protobuf에는 알 수 없는 필드를 무시해 확장과 호환하는 방식이 있습니다. 다만 우리 수신기의 필수 기능·버전·내용 검증까지 생략해도 된다는 뜻은 아닙니다. ‘모르는 필드는 무조건 연결 오류’와 ‘어떤 확장도 무조건 안전’은 모두 과도한 설명입니다.

추가 질문: `EntityState`와 `ControlCommand`를 구분하지 않으면 어떤 버그가 생길까요? 표시용으로 예측한 위치를 권한 있는 물리 결과로 착각하거나, 조향 요청을 이미 계산된 회전량으로 적용할 수 있습니다.

</details>

<a id="chapter-4"></a>

## 4. 자동차 물리: 입력이 힘과 움직임으로 바뀌는 과정 — 50분

이 장에서는 현재 코드의 입력, 접지, 힘, 회전, 위치 갱신을 구분하고 연석 정지 문제를 설명하는 연습을 합니다.
에디터나 서버를 실행하지 않아도 됩니다. 소스 편집 없이 계산기와 메모장으로 진행하세요.

### 4.1. 0~6분: 단위와 방향부터 고정하세요

[vehicle_physics.hpp](B:/Portfolio/Drive_Integration/cpp/host/src/physics/vehicle_physics.hpp)에서 `VehicleInput`, `WheelState`, `VehicleState`를 찾으세요.
오늘은 필드 옆의 단위·방향 주석만 읽고, 클래스의 private 멤버 목록은 건너뛰세요.

| 항목 | 이 프로젝트에서 읽는 방법 |
| --- | --- |
| `VehicleInput::steering` | 단위 없는 입력입니다. +1은 왼쪽 최대 조향, -1은 오른쪽 최대 조향입니다. |
| `steering_angle` | 바퀴 조향각이며 단위는 rad입니다. 차체 방향각이 아닙니다. |
| `heading` | 차체가 향하는 방향이며 단위는 degree입니다. 북쪽 0°, 동쪽 90°입니다. |
| `yaw_rate` | 차체의 회전 속도이며 단위는 rad/s입니다. 공개 상태에서는 왼쪽 회전이 양수입니다. |
| `speed`, `east`, `north` | 각각 m/s, m, m입니다. Unreal 화면의 cm와 바로 섞으면 안 됩니다. |
| `normal_load`, `longitudinal_force` | 각각 타이어 수직 하중과 진행 방향 힘이며 단위는 N입니다. |

공개 차체 좌표는 X=전방, Y=왼쪽, Z=위쪽입니다. 내부 계산에는 오른쪽이 양수인 값도 있습니다.
[body_frame_adapter.hpp](B:/Portfolio/Drive_Integration/cpp/host/src/coordinates/body_frame_adapter.hpp)의 `BodyFrameAdapter`가 이 경계를 담당합니다.
`canonical_steering_to_solver`와 `solver_heading_rate_to_canonical_yaw_rate` 두 함수만 확인하세요.
음수가 보인다고 바로 오류라고 판단하지 말고, “어느 좌표계의 값인가?”를 먼저 적으세요.
휠 인덱스는 0=앞왼쪽, 1=앞오른쪽, 2=뒤왼쪽, 3=뒤오른쪽입니다.

### 4.2. 6~14분: 전체 함수를 읽지 말고 계산의 연결만 찾으세요

[vehicle_physics.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/physics/vehicle_physics.cpp)에서 `VehiclePhysics::update`를 검색하세요.
오버로드 중 `double dt`와 `dynamic_proxies`를 함께 받는 긴 함수를 읽습니다.
다음 네 묶음을 순서대로 찾고, 각 묶음 옆에 한 문장으로 역할을 적으세요.

1. `VehicleInput in`과 `input_mutex_`: 이번 계산에서 사용할 입력을 복사합니다.
2. `steering_target_rad`: 입력을 조향 목표로 바꾸고 조향 속도 한계에 맞춰 바퀴를 움직입니다.
3. `friction_limit`, `longitudinal_force_reserve`: 각 바퀴가 낼 수 있는 접지력의 한계를 구합니다.
4. `total_yaw_moment`, `solver_yaw_rate_rad_s_`: 바퀴 힘이 만든 회전 모멘트를 차체 회전 속도에 반영합니다.

`update_wheel_contacts`는 지면 질의와 서스펜션 상태를 바탕으로 접촉을 계산하는 별도 함수입니다.
오늘은 함수 이름과 `WheelState`에 연결되는 역할까지만 확인하세요.
충돌 반복 해결, 지면 비관통 보정, 프로토콜 직렬화, 모든 상수의 유도는 오늘 읽지 않습니다.
`steering=0.5`를 차체 `heading`에 그대로 더하는 구조가 아니라는 점을 확인하시면 됩니다.
앞바퀴의 좌우 각도도 Ackermann 기하 때문에 서로 다를 수 있습니다. 차체 회전과는 또 다른 값입니다.

### 4.3. 14~24분: 숫자로 확인하는 세 가지 계산

첫째, 50 km/h를 서버 단위로 바꿉니다.

```text
50 × 1000 / 3600 = 13.8889 m/s
한 틱 = 1/60 s = 약 0.016667 s
속도가 일정한 한 틱의 이동 = 13.8889 × 1/60 = 0.23148 m
Unreal 길이로 표현하면 약 23.15 cm입니다.
```

위 계산은 직선·등속인 한 틱의 근사입니다. 실제 틱에서는 힘으로 속도와 방향도 변합니다.
13.89 m/s를 13.89 km/h로 읽거나, 0.231 m를 0.231 cm로 읽지 마세요.
둘째, 질량 1,500 kg 자동차의 정지 하중을 계산합니다. `g=9.80665 m/s²`를 사용합니다.

```text
전체 무게 mg = 1500 × 9.80665 = 14709.98 N
앞뒤·좌우가 완전히 균등하다고 가정한 mg/4 = 3677.49 N
마찰계수 μ=1이라고 가정하면 타이어 하나의 평면 힘 크기는 약 3677.49 N 이하입니다.
```

`mg/4`는 교육용 가정이지 현재 세단의 정확한 배분식이 아닙니다.
[vehicle_sedan.cfg](B:/Portfolio/Drive_Integration/cpp/host/config/vehicle_sedan.cfg)의 `front_static_load_fraction=0.55`를 확인하세요.
평평한 정지 상태를 단순화하면 앞바퀴당 약 4,045.24 N, 뒷바퀴당 약 3,309.74 N입니다.
경사, 가속, 서스펜션 압축, 충돌이 있으면 각 바퀴 하중은 다시 달라집니다.
`F ≤ μFz`에서 F를 바퀴가 내는 평면 합력의 크기로 읽으면, 코너링과 가속이 같은 여력을 쓴다는 뜻입니다.
예를 들어 μ=1, Fz=3,677.5 N에서 Fx=2,000 N을 쓰면 남는 Fy는 약 `sqrt(3677.5²-2000²)=3086.1 N`입니다.
서버 일반 주행에서는 반대로 횡력을 먼저 배분한 뒤 종력 여력을 구합니다. 사이드 브레이크에는 별도 처리가 있습니다.
따라서 엔진 힘을 크게 설정해도 지면과 접촉한 바퀴가 그 힘을 모두 전달한다는 보장은 없습니다.

셋째, 조향각과 주행 반경의 관계를 교육용 자전거 모델로 계산합니다.

```text
휠베이스 L=2.70 m, 대표 앞바퀴 조향각 δ=10°라고 가정합니다.
미끄럼 없는 저속 기하식: R ≈ L / tan(δ) = 15.31 m
이 반경을 50 km/h로 유지한다고 가정한 yaw rate: v/R ≈ 0.907 rad/s
필요한 횡가속도: v²/R ≈ 12.60 m/s²
```

계산기의 degree/radian 모드를 확인하세요. `tan(10 rad)`로 계산하면 다른 답이 나옵니다.
마찰계수 1.05인 평지의 단순 한계 `μg≈10.30 m/s²`보다 요구 횡가속도가 큽니다.
따라서 위 “미끄럼 없는 원”을 그 속도에서도 그대로 유지한다는 가정은 성립하기 어렵습니다.
이 식은 원리를 설명하는 도구이며 서버의 네 바퀴 힘·서스펜션·충돌 전체 계산식이 아닙니다.
조향각을 속도에 따라 강제로 줄이는 것과, 같은 조향각에서도 힘의 한계 때문에 궤적이 달라지는 것을 구분하세요.

### 4.4. 24~34분: 연석 위에서 조작이 안 되던 사례를 읽으세요

[signal_city_course_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/signal_city_course_test.cpp)의 `test_reported_diagonal_curb_restart`를 찾으세요.
이 함수만 읽고, 다른 코스 검사는 건너뛰세요. 실제 문제가 난 위치와 방향을 시험 안에 옮긴 사례입니다.
정착 후 하중은 약 `[7285.3, 790.4, 0.001, 4552.1] N`으로 재현됩니다.
뒷왼쪽은 거의 하중이 없지만 뒷오른쪽에는 충분한 하중이 있습니다. “지면 감지=true”만으로는 부족합니다.
당시 두 후륜 구동 토크를 작은 쪽 마찰 여력으로 함께 제한해서, 양쪽 구동력이 거의 사라졌습니다.
개방형 차동장치의 약한 쪽 제한이라는 성질 자체와, 게임에서 어떤 구동 보조를 제공할지는 별개입니다.
지면을 통과시키거나 차체를 순간이동시켜 해결하지 않고, 저속 제동식 구동 보조를 추가했습니다.
[vehicle_physics.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/physics/vehicle_physics.cpp)의 `axle_drive_torque_per_wheel`부터 `traction_control_brake_torque` 계산을 읽으세요.
하중이 없는 바퀴 쪽의 제동 반력이 차동장치의 반대쪽 구동을 허용하되, 그 바퀴에 없는 접지력을 만들어 주지는 않습니다.

정지·전진·구동 요구가 충분히 올라온 순간을 단순화해서 손으로 계산해 보겠습니다.
여기서는 종력 외의 마찰 사용과 휠 관성의 과도응답을 생략합니다. 실제 첫 틱 출력과 같다는 뜻은 아닙니다.

```text
세단 요구 힘 6000 N, 타이어 반지름 0.32 m → 반축 요구 토크 6000×0.32÷2 = 960 N·m
후륜 한쪽 서비스 제동 용량 → 18000×(1-0.65)÷2×0.32 = 1008 N·m
하중 없는 쪽 접지 토크≈0, 다른 쪽 접지 토크≈4552.1×1.05×0.32 = 1529.5 N·m
공통 토크 상한 = min(1529.5, 0+1008) = 1008 N·m
요구 960 N·m은 상한 안입니다. 약한 쪽은 이를 제동으로 상쇄하고 강한 쪽은 접지로 전달합니다.
```

보조는 1.5 m/s까지 충분히 허용되고, 3 m/s까지 서서히 꺼집니다. 모든 속도에서 작동하는 차동 잠금이 아닙니다.
서비스 브레이크 입력은 `drive_force_suppressed`로 구동·보조 계산을 건너뜁니다.
사이드 브레이크나 요청 방향 반대로 도는 반축도 보조를 제한합니다. “브레이크 용량 내”라는 조건을 유지하기 위해서입니다.

### 4.5. 34~46분: 혼자 하는 실습 두 가지

실습 A — 6분: 종이에 다음 다섯 칸을 만들고 직접 채우세요.
“72 km/h의 m/s 값 / 60 Hz 한 틱 이동 / 30 Hz 한 틱 이동 / `steering=+0.25`의 방향 / 공개 `yaw_rate`의 단위”.
마지막 두 칸은 감으로 답하지 말고 `VehicleInput`과 `VehicleState` 주석을 다시 확인하세요.
완료 기준은 모든 숫자에 단위를 붙이고, 조향 입력과 차체 회전을 서로 다른 칸에 쓰는 것입니다.

실습 B — 6분: 위 연석 사례가 2 m/s로 움직인다고 가정하고 보조 비율과 제동 용량을 계산하세요.
같은 하중 조건에서 양쪽 반축의 공통 토크 상한을 계산하고, 하중 없는 바퀴의 접지력은 어떻게 되는지 적으세요.
그다음 회귀 함수의 `normal_load`, `longitudinal_force`, `distance` 관련 `require`를 찾아 각각 무엇을 방지하는지 적으세요.

<details>
<summary>실습 A·B 확인값 — 직접 계산한 뒤 여세요</summary>

A: 20 m/s, 약 0.3333 m, 약 0.6667 m, 왼쪽, rad/s입니다. 입력 +0.25 자체는 각도 단위가 없습니다.
B: 보조 비율 `(3-2)/(3-1.5)=2/3`, 제동 용량 `1008×2/3=672 N·m`입니다.
약한 쪽 여력을 0으로 근사하면 공통 토크 상한도 672 N·m입니다. 약한 바퀴의 접지력은 여전히 거의 0입니다.
시험은 문제 하중의 재현, 강한 바퀴의 구동력, 약한 바퀴에 허위 힘을 만들지 않는 것, 재출발·수직 순간이동 방지를 함께 확인합니다.

</details>

### 4.6. 46~50분: 자기 확인 네 문항

1. 50 km/h를 서버 `speed=50`으로 해석하면 왜 틀리나요?
2. 조향 입력이 같으면 차체가 매 초 같은 각도만큼 돌아야 하나요?
3. `in_contact=true`인 후륜에서도 구동력이 거의 없을 수 있는 이유는 무엇인가요?
4. 차종을 바꾸면 무엇이 달라지고, 무엇까지 실제 차량과 같다고 주장하면 안 되나요?

<details>
<summary>자기 확인 정답과 설명</summary>

1. 서버 `speed`는 m/s입니다. 50 km/h는 약 13.89 m/s이며, 50 m/s는 180 km/h입니다.
2. 아닙니다. 조향각은 입력 목표이고 yaw rate는 타이어 힘, 속도, 하중과 관성의 영향을 받은 결과입니다.
3. 접촉 플래그와 하중은 다릅니다. 수직 하중이 거의 없으면 `μFz`와 전달 가능한 힘도 거의 없습니다.
4. [player_vehicle_profile.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/player_vehicle_profile.cpp)의 `make_player_vehicle_parameters`를 보세요.
세단·경차·트럭·오토바이는 질량, 치수, 출력, 관성 등을 달리하며 오토바이는 `single_track` 처리도 사용합니다.
그러나 차종별 실차 측정값으로 검증한 전체 동역학은 아닙니다. 지면 기반 축약 모델이며 일반적인 점프·낭떠러지 자유낙하까지 지원한다고 말하면 안 됩니다.

</details>

<a id="chapter-5"></a>

## 5. 지연과 재연결 — FPS가 높은데 왜 반응이 늦을까요?

### 5-1. 서로 다른 시계를 구분하세요

| 지표 | 뜻 | 뜻하지 않는 것 |
|---|---|---|
| Unreal FPS | 화면을 그리는 빈도 | 입력이 서버에서 즉시 처리됐다는 보장 |
| 서버 목표 60Hz | 물리 계산의 설정된 간격 | 실제 모든 틱이 16.67ms 안에 끝난다는 보장 |
| 상태 나이 | 마지막 받은 상태가 얼마나 오래됐는지 | 무조건 인터넷 회선 문제라는 결론 |
| 입력 대기시간 | 요청이 처리되기까지 밀린 시간 | 화면 FPS와 같은 값 |
| overrun | 계산/일정이 목표 시각을 넘긴 기록 | 그 자체만으로 확정되는 원인 |

60Hz의 한 간격은 `1000 / 60 ≈ 16.67ms`입니다. 32개의 후보 탐색이 각각 30ms 걸린다고 가정하면 `32 × 30 = 960ms`입니다. 이 30ms는 설명용 가정이지 매 후보의 실측 고정값이 아닙니다. 핵심은 **작은 작업도 한 번에 몰아서 하면 입력을 처리할 기회를 오래 빼앗을 수 있다**는 것입니다.

### 5-2. 지금 코드에서 확인할 곳

1. [control_lease.hpp](B:/Portfolio/Drive_Integration/cpp/host/src/control/control_lease.hpp): `accept`, `update_timeout`, `update_hard_timeout`.
2. [simulation_host.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/simulation_host.cpp): `run_tick`. timeout 확인 → NPC·보행자 준비 → 물리 계산 → 상태 발행의 순서를 화살표로 적습니다.
3. [simulation_host_npc.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/simulation_host_npc.cpp): `begin_npc_escape_reverse`, `update_npc_navigation`, `prepare_lane_npc`, `next_reverse_probe_m`.
4. [9/8 작업일지](B:/Portfolio/Drive_Integration/docs/worklogs/2026-09-08.md): `세 번째 주행 피드백`만 읽습니다.
5. [SimCoreClientRuntimeEntities.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientRuntimeEntities.cpp): `TickRuntimeProxyActors`. 표시할 신뢰 조건이 사라지면 어떤 경로로 가는지 확인합니다.

### 5-3. 안전 정지와 강제 재접속은 다릅니다

현재 Signal City 설정은 `command_timeout_ms=250`, `hard_command_timeout_ms=1000`, `max_command_queue_age_ms=100`입니다.

```text
마지막 정상 명령
  ├─ 입력이 계속 정상 도착: Active
  ├─ 250ms 초과: SafeStop, 기존 소유권은 유지
  │      └─ hard timeout 전 새 정상 입력: 같은 Play에서 복구 가능
  └─ 1,000ms 초과: 기존 제어 세션 폐기, 재연결 필요
```

위 시간은 검사가 실행될 때 판정됩니다. 서버가 1초 넘게 막혀 있으면 정확히 250ms 시점에 코드를 실행할 수도 없습니다. 다시 실행됐을 때 soft와 hard 조건이 한 번에 참이 될 수 있습니다.

`max_command_queue_age_ms=100`은 또 다른 기준입니다. 서버에서 오래 밀린 입력을 최신 조작처럼 적용하지 않기 위한 제한입니다. 이 코드의 대기시간 추정은 연속된 서버 수신 시간 차와 클라이언트 시간 차를 사용합니다. 서로 다른 컴퓨터 시계를 무조건 직접 빼서 정확한 네트워크 지연을 구한다고 설명하지 마세요.

### 5-4. 이번 장애를 증상→기전→수정으로 분리하세요

| 단계 | 이번 사례 |
|---|---|
| 사용자 관찰 | `stale → reconnecting`, NPC가 사라졌다 나타남 |
| 로그 근거 | 한 번에 약 61~63개의 틱 초과 증가, 대기시간 초과 입력 거부, 약 1.04~1.09초의 제어권 만료 반복 |
| 코드 경로 | 막힌 도로에서 후진 후보 최대 32개를 한 호출에서 검사 |
| 영향 | 같은 실행 흐름의 입력 처리·상태 전달이 지연되고 세션 만료 |
| 수정 | 전체 NPC에 틱당 탐색 차례 하나, 0.5m 후보를 여러 틱에 이어서 검사, 순환 배분 |
| 확인 | 3대 동시 장애물·32후보 소진·재탐색·입력 Active 유지·새 Play 초기화 검사 |

화면의 NPC를 무조건 계속 표시해 놓는 것만으로 서버가 정상인 것은 아닙니다. 반대로 매번 인터넷 연결을 의심하는 것도 근거가 부족합니다. 화면 표시의 신뢰 조건과 서버가 상태를 만들 수 있는지를 따로 봐야 합니다.

이전에 있었던 **처리 대기열이 점점 쌓이는 지연**과 이번 **한 번의 무거운 탐색이 실행 흐름을 점유하는 지연**도 구분합니다. 증상이 비슷해도 원인과 검증 조건은 다릅니다. [이전 입력 지연 사례](B:/Portfolio/Drive_Integration/docs/troubleshooting/ue56-websocket-growing-input-delay.md)는 시간이 남으면 읽습니다.

### 5-5. 실행하지 않고 손으로 풀어 보세요

1. 마지막 정상 입력 후 300ms가 흘렀고 hard timeout 전입니다. 예상 상태와 복구 방법은?
2. 서버가 1.2초 막힌 뒤 오래된 입력 여러 개를 처리하려 합니다. 입력을 전부 순서대로 적용하면 왜 위험한가요?
3. timeout을 10초로 늘리면 원인을 해결한 것인가요?
4. 3대에게 ID 1001→1002→1003 순서로 매 틱 탐색 차례를 준다고 할 때, 각 차량이 ‘첫 요청 1회 + 후진 후보 32회’를 모두 처리하는 데 필요한 차례는 몇 개인가요?

<details>
<summary>5장 정답과 해설</summary>

1. timeout 검사가 실행됐다면 SafeStop입니다. 같은 세션이 유지되는 구간이므로 유효한 새 입력으로 복구할 수 있습니다. E-stop 등 다른 거부 조건이 없다는 전제입니다.
2. 사용자가 이미 놓은 키나 예전 조향을 지금 적용할 수 있습니다. 최신 상태를 중심으로 처리하고 오래된 입력은 검증해서 거부해야 합니다.
3. 아닙니다. 입력 처리 지연은 남아 있고, 끊어진 제어를 더 오래 정상으로 착각할 수 있습니다. 측정된 원인 경로를 줄이고 기존 안전 기준에서 시험해야 합니다.
4. 설명용으로 각 차례가 정확히 한 요청을 처리한다면 `(1 + 32) × 3 = 99`차례입니다. 60Hz라면 약 1.65초에 분산되지만, 실제 경로 성공·제동 대기·재탐색 cooldown·실제 틱 시간이 달라 전체 완료 시간을 보장하는 식은 아닙니다. 1.65초 동안 입력을 못 받는다는 뜻도 아닙니다. 각 차례 사이에 입력을 처리할 기회가 생깁니다.

</details>

<a id="chapter-6"></a>

## 6. NPC: 경로 선택, 근거리 판단, 화면 표현을 나누어 읽기 — 50분

이 장의 목표는 “정해진 경로만 따라간다”와 “완전히 자유롭게 판단한다” 사이에서 현재 구현을 정확히 설명하는 것입니다.
물리 장을 먼저 풀지 않았어도 진행할 수 있습니다. 거리=m, 시간=s, 각도=rad 또는 degree 구분만 유지하세요.
실습은 소스 읽기와 손으로 추적하기만 합니다. 서버 실행이나 코드 수정은 필요하지 않습니다.

### 6.1. 0~7분: 세 가지 책임을 먼저 나누세요

| 책임 | 지금 열 파일과 찾을 심볼 | 오늘 읽을 범위 |
| --- | --- | --- |
| 큰 경로 선택 | [npc_route_planner.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/traffic/npc_route_planner.cpp)의 `shortest_route`, `choose_destination` | 후보 비용·연결·차단 차선 처리만 읽습니다. |
| 실제 진행 판단 | [npc_lane_follower.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/traffic/npc_lane_follower.cpp)의 `step` | 속도 목표·정지 이유·경로 진행의 연결을 찾습니다. 모든 분기는 읽지 않습니다. |
| 근거리 우회 결정 | [simulation_host_npc.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/simulation_host_npc.cpp)의 `update_npc_navigation` | 장애물, 검색 요청, 후진 후보 호출을 읽습니다. 보행자 코드는 건너뜁니다. |
| 화면 표현 | [SimCoreNpcPresentationActor.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreNpcPresentationActor.cpp)의 `ASimCoreNpcPresentationActor::ApplySnapshot` | 유효성 검사·좌표 적용·바퀴와 신호등 갱신만 읽습니다. 메시 생성은 건너뜁니다. |

여기서 “NPC 제어”는 플레이어처럼 입력 키를 발생시켜 동일한 네 바퀴 물리 계산을 돌린다는 뜻이 아닙니다.
NPC는 경로 추종과 속도·회전 제한으로 기준 움직임을 만들고, 충돌 반응을 별도로 결합합니다.
Unreal은 서버가 보낸 NPC 상태를 받아 보간·제한된 예측과 시각 표현을 적용합니다. 목적지를 다시 결정하지 않습니다.

### 6.2. 7~16분: 최단 경로는 도로 연결을 고르는 계산입니다

`shortest_route`에서 `cost`, `visited`, `predecessor` 세 변수를 찾으세요.
이름을 한국어로 각각 “현재까지 비용 / 확정한 차선 / 바로 이전 차선”이라고 적으시면 됩니다.
아직 확정하지 않은 가장 싼 후보를 고르고, 연결된 다음 차선 비용을 갱신하는 방식입니다.
비용은 차선 길이이며, 현재 코드는 시작 차선의 전체 길이도 초기 비용에 넣습니다.
실시간 정체까지 예측하는 최단 시간 계산은 아닙니다.

다음 연결을 종이에 그린 뒤, 코드의 `candidate = best + ...length_m`를 따라 계산하세요.

```text
차선 길이: A=20 m, B=35 m, C=50 m, D=25 m
연결: A→B→D, A→C→D
A→B→D 비용: 20+35+25 = 80 m
A→C→D 비용: 20+50+25 = 95 m
B가 차단되면 A→C→D가 남습니다. 두 가지가 모두 막히면 경로가 없을 수 있습니다.
```

`choose_destination`도 읽어 보세요. 후보는 도로 그래프에서 도달할 수 있고 다시 돌아올 연결이 있는 차선이어야 합니다.
엔티티 ID와 결정 횟수로 후보를 선택하므로, 같은 시작 조건이면 선택을 재현할 수 있습니다.
“스스로 목적지를 고른다”는 기능은 있지만 도로가 없는 공간에 임의의 목적지를 만들지는 않습니다.

### 6.3. 16~25분: 눈앞의 사고와 32개 후진 후보를 이해하세요

최단 경로가 있어도 바로 앞을 사고 차량이 막으면 가까운 공간을 다시 살펴야 합니다.
`try_npc_lane_change`는 작성된 차선 변경 연결을, `try_npc_local_bypass`는 가까운 우회 곡선을 검사합니다.
[npc_local_bypass.cpp](B:/Portfolio/Drive_Integration/cpp/host/src/traffic/npc_local_bypass.cpp)의 `plan_npc_local_bypass`가 곡선 후보를 만드는 지점입니다.
지면과 차량 크기, 장애물에 대한 통과 가능성을 확인합니다. “화면상 빈 것 같다”만으로 통과를 허용하지 않습니다.
도로 전체를 바꾸는 최단 경로와 사고 옆을 통과하는 근거리 곡선은 서로 다른 판단입니다.
그 자리에서 회전 공간이 부족하면 `begin_npc_escape_reverse`가 조금 뒤에서 시작하는 후보를 확인합니다.
실제 뒤로 이동하기 전에 후방 경로와 우회 가능성을 검사하며, 앞차를 통과시키거나 좌표를 건너뛰지 않습니다.

```text
사고 식별자가 있는 장애물의 최대 후진 탐색 거리: 16 m
후보 간격: 0.5 m
최대 후보 수: 16 / 0.5 = 32개
순서: 0.5 → 1.0 → 1.5 → … → 16.0 m
```

일반 정적 장애물은 현재 6 m 한도를 사용합니다. 모든 장애물이 32개 후보를 쓰는 것은 아닙니다.
현재 위치·차선·후방 장애물에 따라 16 m보다 먼저 중단될 수도 있습니다.
이전에는 여러 후진 후보의 무거운 검사를 한 틱에서 몰아 수행하여 새 입력 처리와 상태 전송을 오래 막을 수 있었습니다.
현재 `prepare_lane_npc`는 전체 차량 중 한 대를 순환 선택하고, `next_reverse_probe_m`에 다음 후보를 남깁니다.
`search_steps`는 검색 차례의 상한을 확인하는 진단 값입니다. 검사 내부의 모든 연산 개수를 세는 값은 아닙니다.
후진 대기·검사 실패도 있으므로 “카운터 1회=항상 똑같은 실행 시간”이라고 해석하면 안 됩니다.

### 6.4. 25~33분: 일반 커브의 깜빡이와 정지 회전 문제

`NpcLaneFollower::turn_signal_intent`를 찾고 `signal_group_id`, `minimum_turn`을 확인하세요.
같은 도로가 부드럽게 휘었다는 이유만으로 깜빡이를 켜지는 않습니다.
신호 교차로 접근 구간과 다음 연결 구간의 진행 방향 차이를 보고 실제 좌·우회전 의도를 구분합니다.
현재 교차로 판단 기준은 방향 변화가 30°를 넘는지이며, 차선 변경·근거리 우회는 별도 의도를 사용합니다.
“모든 도로 굴곡=깜빡이”와 “교차로·차선 변경 의도=깜빡이”를 비교해서 설명해 보세요.
[npc_lane_follower_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_lane_follower_test.cpp)의 `test_turn_signals_describe_junction_intention_not_road_curvature`가 근거입니다.

다음으로 `prepare_lane_npc`에서 `nominal_movement`, `distance_yaw_budget`를 찾으세요.
이동 거리가 거의 0이면 경로를 맞추기 위한 회전 허용량도 0으로 제한합니다.
조금 움직일 때는 `이동 거리 / 최소 회전 반경`으로 허용할 방향 변화량을 제한합니다.
회전 속도·회전 가속도 제한만으로는 차가 선 자리에서 방향을 끝까지 맞추는 현상을 막지 못했기 때문입니다.
이 제한은 경로 추종용 회전입니다. 사고 충격 때문에 회전하는 물리 반응까지 없애는 조건은 아닙니다.
[npc_host_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_host_test.cpp)의 `test_stationary_npc_does_not_finish_a_route_yaw_correction`을 확인하세요.

### 6.5. 33~45분: 혼자 하는 실습 두 가지

실습 A — 6분: 6.2의 그래프에서 B를 차단하고 `cost`와 `predecessor`를 손으로 갱신하세요.
D까지 선택한 경로와 비용을 적은 뒤, “이 결과만으로 눈앞 사고차를 피할 수 있는가?”에 두 문장으로 답하세요.
이어 실제 함수에서 `blocked.contains(successor)`를 찾아 자신이 B를 제외한 단계와 연결하세요.
완료 기준은 경로 선택과 충돌 없는 곡선 선택을 서로 다른 작업으로 설명하는 것입니다.

실습 B — 6분: A·B·C 세 NPC가 모두 검색을 계속 요청한다고 가정하고 여섯 검색 차례를 적으세요.
전체 틱에서 1대만 선택한다면 순서는 A→B→C→A→B→C입니다. 두 번째 차례에 A의 후보가 어디로 가는지 추적하세요.
[npc_search_budget_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_search_budget_test.cpp)의 `exercise_searches`에서 다음 검사를 찾으세요.
`fleet_steps <= 1`, `next_reverse_probe_m`, `order[index]`, `exhausted`, `retried`가 각각 막는 재발 문제를 한 줄씩 적으세요.
마지막으로 새 Play에서 초기화해야 할 값 두 가지와 그 이유를 적으세요.
주의: 첫 검색 차례는 전진 우회 검사를 하면서 후진 후보 0.5 m를 예약할 수 있습니다. 예약과 후보 검사를 구분하세요.

<details>
<summary>실습 A·B 확인값 — 먼저 자신의 설명을 적으세요</summary>

A: B를 제외하면 C의 비용은 70 m, D의 비용은 95 m이고, 이전 차선은 각각 A와 C입니다.
도로 연결은 선택했지만 차량 크기와 실제 장애물을 피해 갈 곡선은 아직 확인하지 않았습니다.
B: 전진 검사가 실패해 0.5 m를 예약했다면 A의 다음 차례에 0.5 m 후보를 검사합니다. 이것도 실패하면 다음은 1.0 m입니다.
검사 항목은 각각 틱당 작업 상한, 후보의 작은 거리 우선 진행, 순환 공정성, 전체 후보 소진, 이후 재시도를 확인합니다.
새 Play에서는 후진 후보 위치와 순환 선택 기준을 초기화해야 이전 사고의 계획이나 특정 차량 우선권이 남지 않습니다.

</details>

### 6.6. 45~50분: 자기 확인 네 문항

1. NPC가 목적지를 선택한다는 사실만으로 완전한 자유 공간 자율주행이라고 설명해도 되나요?
2. 최단 경로가 발견됐는데도 NPC가 멈춰 있을 수 있는 이유 두 가지는 무엇인가요?
3. 32개 후보를 한 번에 검사하는 것보다 나누어 검사하면 무엇이 좋아지고, 무엇은 여전히 측정해야 하나요?
4. 일반 커브에서 깜빡이를 끄고 정지 중 경로 회전을 막은 수정이 충돌 회전까지 금지하나요?

<details>
<summary>자기 확인 정답과 설명</summary>

1. 아닙니다. 작성된 차선 그래프, 제한된 근거리 우회, 지면·충돌 검사에 기반한 자율 선택입니다.
2. 예를 들어 신호가 진입을 막거나, 실제 차량 크기가 통과할 충돌 없는 곡선이 없을 수 있습니다. 후방도 막혔다면 후진하지 않습니다.
3. 통신·입력이 실행될 기회를 확보하고 특정 NPC가 검색을 독점하는 일을 줄입니다. 후보 한 번의 실제 실행 시간과 상태 지연도 따로 확인해야 합니다.
4. 아닙니다. 교차로·차선 변경 의도와 단순 도로 굴곡을 구분하고, 경로 회전과 사고 충격 회전을 별도로 다룹니다.
현재 NPC의 네 차종은 크기·질량·가감속·최소 회전 반경 등이 다른 축약 추종·충돌 모델입니다.
플레이어의 네 바퀴 힘 계산을 NPC 모두에게 동일하게 실행하거나, 사람 운전자의 시야·의도를 완전히 재현한 구현은 아닙니다.

</details>

<a id="chapter-7"></a>

## 7. 45분 관찰 실습 — 코드를 바꾸지 않고 가설을 확인하기

### 7-1. 시작 전 5분

서버 상태를 교재 작성 시점의 PID로 판단하지 않습니다. PowerShell에서 현재 상태를 확인합니다.

```powershell
Set-Location 'B:\Portfolio\Drive_Integration'
Get-Process -Name simcore_publisher -ErrorAction SilentlyContinue | Select-Object Id,Path
netstat -ano -p tcp | Select-String ':9000\s'
```

`127.0.0.1:9000`의 `LISTENING` PID와 프로젝트 서버 실행 파일이 일치하는지 봅니다. 서버가 이미 있다면 추가로 실행하지 않습니다. 서버가 없고 포트도 비어 있을 때만 아래를 실행합니다.

```powershell
.\scripts\run_signal_city_server.ps1 -Background
```

실행 정책 때문에 이 스크립트만 거부되면 시스템 정책을 영구 변경하지 말고 별도 PowerShell 프로세스에서 실행할 수 있습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_signal_city_server.ps1 -Background
```

이후 [DriveIntegration.uproject](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/DriveIntegration.uproject)를 열고 `/Game/SignalCity/Maps/L_SignalCity`에서 Play합니다. viewport를 클릭해 입력을 받게 하고 HUD의 정상 연결 상태를 확인합니다. 이번 교재의 실습에는 새 Bake가 필요하지 않습니다.

**실행이 막히면 10분까지만 확인하세요.** 빌드 결과나 엔진이 없다면 재설치에 하루를 쓰지 않습니다. 아래의 ‘실행 없이 하는 대체 실습’을 하고 8장으로 넘어갑니다.

### 7-2. 실습 A — 입력과 카메라를 분리해 보기, 10분

1. 정지 상태에서 카메라만 돌립니다. 차량과 바퀴가 움직여야 하는지 먼저 적습니다.
2. 천천히 전진하며 `A/D`를 아주 짧게 눌렀을 때와 길게 눌렀을 때를 비교합니다.
3. 조향 요청, 앞바퀴 조향각, 차량의 진행 방향 변화가 각각 같은 개념인지 적습니다.
4. 빠른 주행이나 충돌은 필요 없습니다. 같은 장소·같은 차종으로 비교합니다.

예상: 카메라 회전은 주행 상태를 바꾸지 않습니다. 키보드 입력은 순간적으로 최대 조향에 도달하도록 단순히 붙어 있는 것이 아니므로 짧게 누른 입력과 길게 누른 입력의 효과가 다를 수 있습니다. 바퀴가 꺾이는 각도와 차량의 순간 회전 속도는 같은 값이 아닙니다.

### 7-3. 실습 B — 깜빡이의 상태를 관찰하기, 10분

1. `Q` 또는 `E`를 켠 후 정지 상태에서 핸들만 움직입니다.
2. 선택 방향으로 충분히 회전하고 핸들을 중앙으로 되돌립니다.
3. `X` 비상등 상태에서 같은 과정을 반복합니다.
4. 결과를 다음 표에 적습니다. 일부 조건에서는 자동취소가 일어나지 않는 것이 정상입니다.

| 조건 | 켜짐 유지 / 자동 해제 | 그 이유를 코드로 설명 |
|---|---|---|
| 정지 상태 조향 | | |
| 선택 방향으로 전진 회전 후 중앙 복귀 | | |
| 비상등 | | |
| 아주 작은 조향만 한 경우 | | |

<details>
<summary>실습 B 예상 결과</summary>

정지 상태·비상등·작은 조향은 유지하는 것이 기대 결과입니다. 유효한 최신 상태에서 전진하며 선택 방향의 충분한 조향과 방향 변화가 확인된 뒤 중앙으로 돌아오면 해제됩니다. ‘몇 초 지났으니 해제’하는 단순 타이머가 아닙니다. 최신 기준은 [SimCoreTurnSignals.cpp의 FAutoCancel::Update](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreTurnSignals.cpp)에서 확인합니다.

</details>

### 7-4. 실습 C — NPC와 소리를 조건별로 나눠 보기, 15분

- NPC가 같은 차로의 커브를 도는 경우와 실제로 옆으로 우회하는 경우를 나눠 관찰합니다. 일반 커브만 따라가는데 계속 깜빡이가 켜진다면 발생 위치를 기록합니다.
- NPC 앞을 막을 때 옆에 실제 차체가 지나갈 공간이 있는 경우와 없는 경우를 나눕니다. 단순히 ‘안 피함’이라고 적지 말고 공간·접근 차량·신호·사고 상태를 함께 적습니다.
- 정상 선회와 강한 횡미끄러짐에서 타이어 소리를 비교합니다. ‘커브’라는 단어 하나로 모든 선회를 같은 조건으로 취급하지 않습니다.
- 제자리 회전 여부는 단순 눈대중으로 결론 내리지 말고 `F3` 충돌 표시와 주변 기준물을 함께 봅니다. 충돌 직후의 회전과 정차한 정상 차량의 경로 보정 회전을 구분합니다.

연석 문제는 억지로 재현하려고 오래 쓰지 않습니다. 우연히 조작 불능이 나타나면 시간·차종·위치 화면을 남기고 `DriveBlock` 로그와 연결합니다.

### 7-5. 실행 없이 하는 대체 실습

실행이 안 되더라도 아래 파일을 읽고 똑같은 형식으로 ‘조건→예상→검사 근거’를 작성할 수 있습니다.

- [깜빡이 시험](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreTurnSignalsTests.cpp): `AutoCancel`을 검색합니다.
- [타이어음 시험](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreVehicleAudioTests.cpp): `SharpCorner`, `Tire`, `Correlation` 등을 검색해 실제 존재하는 시험 구간을 찾습니다.
- [검색 분할 시험](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_search_budget_test.cpp): `exercise_searches`, `fleet_steps`, `full_search`를 읽습니다.
- [작업일지](B:/Portfolio/Drive_Integration/docs/worklogs/2026-09-08.md): 관찰 수치와 ‘실제 청감은 별도’라는 제한을 함께 기록합니다.

교재의 예상과 다른 결과가 나오면 정답에 억지로 맞추지 않습니다. **관찰한 사실을 남긴 것이 실습의 성과**입니다.

<a id="chapter-8"></a>

## 8. 테스트와 디버깅 — 통과 숫자보다 실패 조건을 읽으세요

### 8-1. 테스트를 문장으로 번역하는 법

테스트를 아래 네 칸으로 읽습니다.

1. **준비:** 어떤 지도·차량·입력·시간 상태를 만들었나요?
2. **실행:** 어떤 함수를 호출하거나 몇 틱 진행했나요?
3. **검사:** 결과가 무엇이어야 한다고 요구하나요?
4. **회귀 방지:** 이 검사가 없으면 어떤 사용자 문제가 다시 생길 수 있나요?

예를 들어 `fleet_steps <= 1`을 검사한다면 ‘계산 결과가 참이다’로 읽지 않습니다. **여러 NPC의 탐색이 같은 틱에 몰리지 않는다는 계약**을 검사한다고 설명합니다. 단, 이것만으로 모든 지도에서 항상 60FPS가 나온다고 결론 내릴 수는 없습니다.

### 8-2. 오늘은 세 파일 중 두 개만 고릅니다

| 파일 | 찾을 내용 | 막으려는 문제 |
|---|---|---|
| [npc_search_budget_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/npc_search_budget_test.cpp) | `exercise_searches`, `fleet_steps`, `next_reverse_probe_m` | 탐색 독점·기아·새 Play에 남은 후보 |
| [control_lease_test.cpp](B:/Portfolio/Drive_Integration/cpp/host/tests/control_lease_test.cpp) | `ExcessiveQueueAge`, `hard`, `timeout` | 밀린 입력 수락·소유권·재접속 판정 |
| [SimCoreTurnSignalsTests.cpp](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Source/DriveIntegration/SimCoreTurnSignalsTests.cpp) | `AutoCancel` | 정지·비상등 오취소·회전 후 취소 누락 |

`require(...)`, `TestTrue(...)`, `TestEqual(...)`를 찾아 **왜 그 값이어야 하는지** 한 줄씩 적으세요. 테스트 코드를 모두 타이핑할 필요는 없습니다.

### 8-3. 이미 빌드된 C++ 검사만 실행해 보기

이 명령은 소스를 수정하거나 전체 프로젝트를 빌드하지 않습니다. 먼저 등록 이름을 확인합니다.

```powershell
Set-Location 'B:\Portfolio\Drive_Integration'
ctest --test-dir '.\cpp\host\build' -C Release -N
```

목록에서 아래 이름이 있으면 선택해 실행합니다. 둘을 실행하는 데 GUI Unreal이 필수는 아닙니다.

```powershell
ctest --test-dir '.\cpp\host\build' -C Release -R '^(control_lease_tests|npc_search_budget_tests)$' --output-on-failure
```

이 테스트는 프로젝트 테스트 프로그램의 격리된 상태를 사용합니다. 서버 실행기의 9000 포트로 직접 운전 명령을 보내는 실습과 구분합니다. `No tests were found`는 통과가 아닙니다. 경로와 기존 빌드 유무를 확인하고, 없는 환경에서는 소스 읽기로 대체합니다.

실패하면 마지막의 `Failed`만 복사하지 말고 **처음 실패한 조건 문장**을 읽습니다. 기대한 결과와 다른 실제 값, 재현 조건을 노트에 적습니다. AI 없이 실패 원인을 모르면 그 자리에서 안전 기준을 완화하지 말고 보류합니다.

### 8-4. 디버거를 쓸 때의 함정

서버 `run_tick`에 중단점을 걸고 10초 동안 멈추면 통신과 시간 판정에 영향을 줍니다. Unreal에서 연결 오류가 보였다고 해서 평상시에도 10초 멈추는 코드라는 뜻은 아닙니다.

오늘 권장하는 방법은 우선 **시험 함수 안의 입력과 기대값을 읽고 손으로 추적**하는 것입니다. 실제 서버를 멈춰 확인했다면 ‘디버거로 의도적으로 정지시킨 시간’을 실습 조건에 반드시 기록합니다. 그 결과를 근거로 timeout을 늘리지 않습니다.

### 8-5. 로그를 읽는 순서

1. 언제·무엇을 했는지 메모합니다.
2. 같은 세션의 서버 stdout/stderr와 Unreal 로그를 찾습니다.
3. 맨 마지막 증상보다 **처음 달라진 시점**을 봅니다.
4. `Timing` → 입력 거부 → `Safety` → 연결 종료 같은 순서를 연결합니다.
5. 가설과 사실을 구분하고, 재현 조건 하나를 정합니다.

아래는 최근 로그 파일을 찾는 읽기 전용 명령입니다.

```powershell
Get-ChildItem '.\runtime_logs' -Filter 'simcore-signal-city-*.stderr.log' |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 5 Name,LastWriteTime,Length
```

파일 이름을 확인한 뒤 읽습니다. `<실제 파일명>`은 그대로 입력하는 문자열이 아니라 방금 찾은 파일 이름으로 바꿉니다.

```powershell
Get-Content -LiteralPath '.\runtime_logs\<실제 파일명>' -Tail 80
```

Unreal 로그는 [Saved/Logs](B:/Portfolio/Drive_Integration/unreal/DriveIntegration/Saved/Logs)에서 찾습니다. 이름이 같은 로그라도 실행 날짜가 다를 수 있습니다.

### 8-6. 자기확인

1. 테스트가 44개 통과하면 실제 주행감도 좋다고 증명되나요?
2. 새 분할 처리로 첫 틱이 아니라 다음 검색 차례에 계획이 생깁니다. 검사 시점 변경과 안전 조건 완화는 어떻게 다른가요?
3. 빌드 성공, 자동검사 통과, Play 관찰은 각각 무엇을 확인하나요?

<details>
<summary>8장 정답과 해설</summary>

1. 아닙니다. 작성된 조건을 만족했다는 근거입니다. 작성하지 않은 재현 조건·렌더링·청감·장시간 성능은 별도로 확인해야 합니다.
2. 새로 정의한 실행 순서 안에서 검사하되 무접촉·공간 유지·실제 완주 같은 본래 조건을 유지하면 계약에 맞춘 검사입니다. 반대로 실패를 없애려고 충돌 허용 폭을 키우거나 오래된 명령을 허용하는 것은 다른 변경입니다. 검사가 무엇을 증명하는지 먼저 설명해야 합니다.
3. 빌드는 소스가 해당 도구와 설정으로 컴파일·링크되는지, 자동검사는 작성한 동작 계약을 만족하는지, Play는 실제 입력·화면·소리가 의도한 경험을 주는지 확인합니다. 셋 중 하나를 나머지의 대체물로 말하지 않습니다.

</details>

<a id="chapter-9"></a>

## 9. 마지막 35분 — 내 프로젝트로 설명하기

### 9-1. 책을 덮고 3분 설명합니다

다음 순서대로 말하거나 휴대전화에 녹음합니다. 문장을 외우는 것이 아니라 중간 연결을 이해했는지 확인하는 과제입니다.

1. 무엇을 만드는 프로젝트인지 20초.
2. `W` 입력부터 화면 갱신까지 60초.
3. Unreal과 서버의 책임이 다른 이유 30초.
4. 최근 문제 하나의 증상·근거·원인·수정·검증 60초.
5. 아직 확인하지 못한 한계 10초.

<details>
<summary>설명 예시 — 그대로 외우지 말고 내 표현과 실제 관찰로 바꾸세요</summary>

이 프로젝트는 Unreal 5.6 화면과 별도 C++ 서버를 연결한 운전 시뮬레이션입니다. Unreal Pawn이 입력을 받고 ClientComponent가 명령을 모아 전송합니다. 서버는 지도·세션·순서·제어권을 확인하고, 고정 시간 간격으로 차량과 교통 상태를 계산합니다. 계산 결과를 WorldState로 보내면 Unreal이 검증한 뒤 차량·신호·HUD를 표시합니다. 카메라와 표시 예측은 서버의 물리 권한과 구분합니다.

최근에는 NPC의 후진 후보 탐색이 한 번에 몰려 약 1초의 지연과 재접속 반복을 만들었습니다. 로그의 틱 초과·입력 거부·세션 만료 순서를 확인하고, 탐색 차례를 NPC 사이에 나누고 후보를 여러 틱에 이어 검사하도록 바꿨습니다. 동시 장애물과 입력 유지, 재탐색·새 Play 초기화를 시험했습니다. 자동검사 통과와 실제 주행감·소리 인수는 구분해서 보고 있습니다.

구현 과정에서 AI의 도움을 받았다면 그렇게 말하면 됩니다. 대신 오늘 직접 읽은 함수, 직접 관찰한 조건, 직접 확인한 검사와 아직 설명하지 못하는 부분을 정확히 구분하세요. 직접 작성하지 않은 부분을 혼자 설계·구현했다고 바꾸어 말할 필요는 없습니다.

</details>

### 9-2. 종합 확인 문제 10개

정답을 보지 않고 한 문제당 한두 문장으로 답하세요. 앞선 단원 문제와 겹치는 것은 의도적인 복습입니다.

1. W 입력과 차량 속도 상태는 왜 같은 데이터가 아닌가요?
2. `repeated WheelState wheels = 21`의 세 요소를 설명하세요.
3. 같은 Play 재연결과 새 Play 초기화의 차이는 무엇인가요?
4. 60Hz 한 틱은 약 몇 ms이고, 높은 Unreal FPS만으로 무엇을 보장할 수 없나요?
5. 50km/h는 몇 m/s인가요? 그 속도를 일정하게 유지하면 60Hz 한 틱 동안 약 몇 m 이동하나요?
6. 한쪽 구동륜 하중이 거의 0일 때 왜 다른 구동륜에도 힘이 전달되지 않는 문제가 생겼나요?
7. 일반 커브와 차선 변경을 같은 깜빡이 조건으로 보면 어떤 오류가 생기나요?
8. 정차 중의 경로 자세 보정과 충돌로 생긴 회전은 왜 다르게 취급하나요?
9. 250ms SafeStop과 1,000ms hard timeout은 무엇이 다른가요?
10. 오늘 직접 관찰한 사실 하나와 아직 판단하지 못한 사실 하나를 쓰세요.

<details>
<summary>종합 정답·채점 기준</summary>

1. 전자는 운전 의도, 후자는 물리 계산 결과입니다. 같은 스로틀이어도 질량·기어·저항·접지에 따라 결과가 달라집니다.
2. 여러 WheelState를 담는 목록이며 21은 필드 번호입니다. 각 원소 안의 wheel_index 값 0~3은 바퀴 위치입니다.
3. 정상적인 같은 Play 재연결은 물리 상태를 보존하는 경로이고, 유효한 새 Play는 새 초기 상태를 요청합니다. 연결 ID와 Play ID를 구분합니다.
4. 약 16.67ms입니다. FPS만으로 서버의 처리 시간·입력 나이·왕복 지연을 보장하지 못합니다.
5. 약 13.89m/s, 약 0.2315m입니다. 가속이 없다는 단순화 조건입니다.
6. 기존 축 배분이 양쪽 중 작은 구동 여유에 함께 제한되는 경로였기 때문입니다. 접지 하중이 있는 쪽도 같이 제한됐습니다. 수정은 저속 제동 반력을 이용하는 차동 보조이며 지면을 무시하거나 무제한 힘을 만드는 것은 아닙니다.
7. 같은 차로를 따라 굽거나 폭이 달라지는 곳에서도 불필요한 깜빡이를 켭니다. 이동 의도를 구분해야 합니다.
8. 일반 주행 중 멈춘 차가 남은 경로 방향 차이를 제자리에서 해소하면 자동차답지 않습니다. 반면 충돌 각운동은 실제 충격 반응으로 별도 계산해야 하므로 무조건 모두 0으로 지우지 않습니다.
9. soft는 안전 정지하되 기존 제어권을 유지하는 구간이고, hard는 기존 세션을 폐기해 재연결을 요구하는 경계입니다.
10. 정해진 정답은 없습니다. ‘코드에서 봤다’, ‘자동검사로 확인했다’, ‘Play에서 직접 봤다’를 구분하고 조건과 근거를 적으면 됩니다.

각 문항 2점: 핵심과 이유를 설명하면 2점, 용어만 맞으면 1점, 연결을 못 하면 0점입니다. 16점 이상이면 오늘의 기초 목표를 통과한 것으로 자기평가할 수 있습니다. 점수가 낮으면 전체를 처음부터 다시 읽지 말고 틀린 문항의 장으로 돌아갑니다. 이 자기평가는 채용 합격이나 개발 역량 전체의 인증이 아닙니다.

</details>

### 9-3. 오늘의 제출물 체크리스트

- [ ] 명령과 상태를 구분한 전체 왕복 흐름 한 장
- [ ] 실제 함수 이름이 들어간 W 입력 추적표
- [ ] 물리 단위 손계산 2개 이상
- [ ] 최근 장애 한 건의 증상·근거·원인·수정·검증 표
- [ ] 관찰 실습 또는 대체 실습 기록 3개
- [ ] 테스트 한 개의 준비·실행·검사·방지할 버그 설명
- [ ] 노트 없이 해 본 3분 설명과 종합 문제 답
- [ ] 다음에 확인할 질문 최대 3개

### 9-4. 다음날 토큰이 없어도 할 수 있는 20분 복습

5분 동안 빈 종이에 왕복 흐름을 그리고, 5분 동안 틀렸던 함수 위치를 찾아봅니다. 5분 동안 장애 한 건을 말로 설명하고, 마지막 5분에 아직 모르는 질문 3개를 더 작게 바꿉니다.

‘NPC 전체를 이해하고 싶다’보다 ‘정차 상태에서 yaw 보정이 실행되는 조건은 어디인가?’가 다음 학습에 더 도움이 됩니다. 오늘 확인하지 못한 부분은 솔직하게 남겨 두세요. **어디를 찾아 확인할 수 있는지 아는 것도 프로젝트를 이해하는 중요한 능력입니다.**

## 부록. 막혔을 때 빠른 찾기

| 막힌 지점 | 먼저 볼 곳 | 오늘 하지 않을 일 |
|---|---|---|
| 실행 경로를 모르겠음 | 0장·7장의 절대 경로 | 폴더마다 무작정 스크립트 실행 |
| 파일 링크가 안 열림 | 표시된 경로를 복사해 편집기에서 열기 | 인터넷 링크로 대체했다고 가정 |
| 서버 주소·포트 충돌 | 실행기 출력, 현재 PID·9000 listener | 이름이 같은 프로세스 전부 종료 |
| stale·reconnecting | 5장, 같은 시점의 서버/UE 로그 | 근거 없이 timeout 증가 |
| 이동하지 않음 | 연결 상태→입력→기어→접지/DriveBlock 순서 | 곧바로 지형 Bake 재실행 |
| 함수가 너무 김 | 매개변수·실패 반환·다음 호출·결과 저장 | 수백 줄을 전부 암기 |
| 수학이 어려움 | 단위와 힘/각도의 의미, 손계산 | 전체 차량 모델을 하루에 증명 |
| 테스트가 없다고 나옴 | `ctest -N`, 기존 build 폴더 | ‘실패 0개’니까 통과로 기록 |
| 오래된 문서와 다름 | 현재 코드·설정·9/8 작업일지 | 옛 일정표만 보고 미구현으로 단정 |

교재 작성 시점 이후 코드가 바뀌면 줄 번호보다 **파일 이름과 함수 이름**을 먼저 검색하세요. 기존 S00/S01의 학습 이력은 별도로 보존되어 있으며, 이 하루 과정을 읽었다는 이유로 정규 과정 전체를 완료 처리하지 않습니다.
