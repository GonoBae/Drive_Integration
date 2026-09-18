const U = 'unreal/DriveIntegration/Source/DriveIntegration/';
const E = 'unreal/DriveIntegration/Source/DriveIntegrationEditor/';
const src = (file, symbol, start, end) => ({path: U + file, symbol, start, end});
const epic = (page, label) => ({url: `https://dev.epicgames.com/documentation/en-us/unreal-engine/${page}?application_version=5.6`, label});
const objectRef = epic('unreal-object-handling-in-unreal-engine', 'Epic UE 5.6: Unreal Object Handling');
const lifeRef = epic('unreal-engine-actor-lifecycle', 'Epic UE 5.6: Actor Lifecycle');
const inputRef = epic('input-overview-in-unreal-engine', 'Epic UE 5.6: Input Overview');
const coordRef = epic('coordinate-system-and-spaces-in-unreal-engine', 'Epic UE 5.6: Coordinate System and Spaces');
const apiRef = epic('API/QuickStart', 'Epic UE 5.6: API Quick Start');
const testRef = epic('automation-test-framework-in-unreal-engine', 'Epic UE 5.6: Automation Test Framework');
const chapters = ['게임 프레임워크와 수명', '입력과 서버 연결', '좌표와 상태 표시', '차량과 도로의 표현', '운전 화면과 소리', '기록·디버깅·검증', '독립 실습과 해설'];
const slides = [];
function add(chapter, type, title, body, notes, sources, extra = {}) {
  const refs = sources.map(s => s.url ? `${s.label}: ${s.url}` : `읽을 코드: ${s.path}:${s.start} — ${s.symbol}`).join('\n');
  slides.push({id: `V3-${String(slides.length + 1).padStart(2, '0')}`, chapter: chapters[chapter], type, title, body, notes: `${notes}\n\n${refs}`, sources, ...extra});
}
function code(chapter, title, body, notes, file, symbol, start, end) {
  add(chapter, 'code', title, body, notes, [src(file, symbol, start, end)], {code: {path: U + file, start, end}});
}

add(0, 'lesson', '이 클라이언트가 결정하는 것과 보여 주는 것',
 ['Unreal은 입력을 읽고, 서버가 계산한 차량 상태를 화면과 소리로 표현합니다.', '도로 충돌 정보를 측정하는 일과 차량 운동을 계산하는 일은 서로 다릅니다.', '목표: W 입력 한 번의 왕복을 실제 함수 이름으로 설명할 수 있습니다.'],
 '자동차가 화면에서 움직인다고 해서 Unreal의 차량 물리 기능이 그 움직임을 계산한다고 단정하면 안 됩니다. 이 프로젝트의 플레이어 Pawn은 운전 의도를 명령으로 정리하고, 외부 서버가 보낸 위치·회전·바퀴 상태를 표시합니다. 따라서 화면의 바퀴가 이상하면 표시 계산을, 권위 상태의 속도가 이상하면 서버 계산을 각각 조사해야 합니다. 이 권에서는 서버 수식을 새로 배우기보다 경계를 구분하는 데 집중합니다. 이후 슬라이드의 일반 개념은 Epic의 Unreal 설명이며, 구체적인 키·단위·시간 제한은 이 저장소가 선택한 계약입니다. Unreal의 기본 네트워크 복제와 현재 WebSocket 통신도 같은 기능으로 취급하지 마세요.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::Tick',176,225), src('SimCoreClientComponent.cpp','USimCoreClientComponent::SetControl',124,137)], {imageKey:'unreal_scene'});

add(0, 'table', '처음에는 다섯 개의 파일만 연결합니다',
 ['파일 전체를 읽기 전에, 함수의 입력·출력·호출자를 한 줄씩 적으세요.'],
 '처음부터 Pawn의 모든 카메라·손상·운전자 코드를 읽으면 주행의 중심 경로를 놓치기 쉽습니다. 표의 순서대로 기본 흐름을 좁게 확인한 다음 필요한 표현 기능으로 가지를 넓히세요. 설정에서는 이름이 무엇인지, Pawn에서는 어떤 함수가 호출되는지, 클라이언트에서는 어디에 값이 보관되는지, 프로토콜에서는 어떤 형태의 바이트가 되는지, 표현 함수에서는 위치가 어떤 단위로 바뀌는지 읽습니다. 중간에 모르는 보조 함수를 만났다면 우선 반환값의 의미만 기록하고 계속 진행해도 됩니다. 다섯 파일을 다 읽었다는 표시보다, 같은 Throttle 값이 저장과 송신을 거쳐 어떻게 전달되는지 손으로 추적한 기록이 훨씬 유용합니다.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::PushControl',850,881),src('SimCorePresentation.cpp','BuildVehicleSample',35,96)],
 {headers:['순서','파일','찾을 지점'],rows:[['1','DefaultInput.ini','Throttle / DriveRecord'],['2','ExternalVehiclePawn.cpp','SetupPlayerInputComponent / PushControl'],['3','SimCoreClientComponent.cpp','SetControl / SendControl'],['4','SimCoreProtocol.cpp','SerializeControlEnvelope'],['5','SimCorePresentation.cpp','BuildVehicleSample']]});

add(0, 'table', 'Actor·Pawn·Controller·Component를 구분합니다',
 ['상속은 “무엇의 한 종류인가”, 컴포넌트는 “어떤 기능을 붙이는가”를 설명합니다.'],
 'AExternalVehiclePawn은 APawn을 상속하며, APawn은 Actor 계열입니다. 그래서 레벨 안의 대상이면서 플레이어가 빙의하여 입력을 받을 수 있습니다. 반면 SimCoreClientComponent는 통신이라는 기능을 Pawn에 붙인 객체입니다. 카메라나 조명처럼 변환이 필요한 기능은 SceneComponent 계열을 사용합니다. Controller의 존재만으로 서버 권한을 얻는 것은 아닙니다. 이 저장소에서 외부 서버의 운전 권한은 별도의 연결·지도·세션 계약을 통과해야 합니다. 또한 NPC가 화면에 나타난다는 이유로 Unreal AIController가 그 경로를 계획한다고 추측하지 마세요. 현재 NPC Actor는 서버 스냅샷의 시각적 표현입니다.',
 [src('ExternalVehiclePawn.h','AExternalVehiclePawn',25,42),src('SimCoreClientComponent.h','USimCoreClientComponent',29,40),apiRef],
 {headers:['종류','핵심 역할','현재 연결'],rows:[['Actor','월드에 존재하는 대상','NPC 표시 Actor'],['Pawn','빙의 가능한 Actor','AExternalVehiclePawn'],['Controller','Pawn의 제어 주체','Player0가 플레이어 Pawn 빙의'],['ActorComponent','소유 Actor에 기능 제공','SimCoreClientComponent'],['SceneComponent','부착 관계와 Transform','PresentationRoot / CameraBoom']]});

add(0, 'lesson', '컴포넌트 트리는 차량의 부품 관계입니다',
 ['차량 전체의 기준은 PresentationRoot입니다.', '차체·바퀴 피벗·카메라는 각자의 로컬 변환을 가집니다.', 'ActorComponent에 모두 위치가 있는 것은 아닙니다.'],
 '차체가 회전할 때 바퀴와 운전석도 함께 이동해야 합니다. 이를 매번 별도 월드 좌표로 계산하는 대신 부착 관계로 표현하면 부모의 움직임을 자식이 따라갑니다. 다만 자식마다 추가 회전의 목적은 다릅니다. 바퀴 피벗은 조향과 지면 방향을, 바퀴 메시 자체는 굴러가는 회전을 담당합니다. 통신 컴포넌트는 공간상의 부품이 아니므로 그 자체를 앞바퀴 옆에 배치한다는 발상이 필요하지 않습니다. 컴포넌트 목록을 볼 때 클래스 이름의 접미사보다 USceneComponent를 상속했는지와 SetupAttachment의 부모가 누구인지 확인하세요. 이 관계를 그려 보면 차체 기울기와 카메라 기울기를 분리한 이유도 이해하기 쉽습니다.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::AExternalVehiclePawn',31,150)],{imageKey:'actor_components'});

code(0, '기본 부품은 생성자에서 구성합니다',
 ['CreateDefaultSubobject는 이 Pawn이 기본적으로 갖는 부품을 만듭니다.', 'NoCollision은 현재 차체 메시의 물리 권한을 읽는 중요한 단서입니다.'],
 '첫 줄은 이 Actor가 프레임 갱신을 받을 수 있도록 허용합니다. 다음 줄의 Player0 자동 빙의는 로컬 플레이어가 이 Pawn을 제어할 수 있도록 하는 설정입니다. 그다음 SceneComponent를 만들어 루트로 지정하고, StaticMeshComponent를 만들어 루트에 붙입니다. 마지막의 NoCollision은 이 메시를 Unreal 충돌 해결의 주체로 쓰지 않는다는 뜻입니다. 이것이 서버 충돌 검사까지 없앤다는 뜻은 아닙니다. 이 코드 조각에서 연결·파일 기록·현재 플레이어 입력 같은 실행 중 작업을 찾지 못하는 것도 자연스럽습니다. 생성자는 기본 구조와 기본값을 정하는 자리이며, 실제 게임 실행 시점의 초기화는 뒤에서 BeginPlay로 구분합니다.',
 'ExternalVehiclePawn.cpp','AExternalVehiclePawn::AExternalVehiclePawn',34,40);

add(0, 'lesson', 'UObject와 GC는 일반 포인터 관리와 다릅니다',
 ['UObject는 Unreal이 클래스 정보와 객체 수명을 관리하는 기반입니다.', 'UPROPERTY로 추적되는 참조와 추적되지 않는 원시 포인터를 구분하세요.', 'TWeakObjectPtr는 객체를 붙잡아 두지 않고 유효성을 확인합니다.'],
 '일반 C++ 객체에서 배운 new와 delete만으로 Unreal 객체 수명을 설명하면 부족합니다. Unreal은 반사 정보와 참조 관계를 사용해 UObject를 관리합니다. 현재 코드의 컴포넌트 멤버는 UPROPERTY와 TObjectPtr 조합으로 선언되어 엔진이 관계를 알 수 있도록 합니다. 반면 비동기 콜백이 Pawn의 컴포넌트를 오래 붙잡으면 Play가 끝난 뒤에도 잘못 접근할 수 있으므로 약한 참조의 유효성 검사가 중요합니다. 약한 참조를 썼다고 콜백의 논리적 유효성까지 보장되는 것은 아닙니다. 같은 객체가 새 연결을 시작한 경우에는 옛 소켓의 콜백인지 추가로 확인해야 합니다. 그래서 뒤에서 SocketGeneration을 별도의 문제로 다룹니다.',
 [src('ExternalVehiclePawn.h','PresentationRoot',45,62),src('SimCoreClientComponent.cpp','USimCoreClientComponent::BindSocketDelegates',588,619),objectRef]);

add(0, 'lesson', 'UCLASS·UPROPERTY는 어떤 정보를 공개하나요?',
 ['UCLASS와 GENERATED_BODY는 엔진이 C++ 클래스를 이해하도록 연결합니다.', 'VisibleAnywhere는 보이게, EditAnywhere는 수정 가능하게 하는 의도입니다.', 'BlueprintReadOnly는 C++ const와 같은 의미가 아닙니다.'],
 '헤더에서 매크로를 지우고 순수 C++만 남겨 읽는 습관은 처음에는 도움이 되지만, 에디터에 값이 보이는 이유와 객체 참조 관리까지 이해하려면 다시 매크로를 읽어야 합니다. 예를 들어 ServerUrl은 편집 가능한 설정이고, 계산되어 표시되는 지도 체크섬은 관찰용 값입니다. BlueprintReadOnly는 Blueprint 쪽에서의 접근 제약을 나타내며 해당 멤버가 C++ 코드에서도 절대 변경되지 않는다는 뜻이 아닙니다. 또한 UPROPERTY라는 말 자체가 자동 네트워크 동기화를 의미하지는 않습니다. 이 프로젝트의 PendingControl과 수신 상태는 자체 WebSocket 경로를 사용합니다. 반사 노출, 저장, 네트워크 송신을 서로 다른 축으로 구분해서 노트에 적어 보세요.',
 [src('SimCoreClientComponent.h','USimCoreClientComponent',29,94),objectRef]);

add(0, 'table', '생성부터 Play 종료까지 역할이 나뉩니다',
 ['생성자가 호출됐다는 사실과 게임이 시작됐다는 사실은 같지 않습니다.'],
 '에디터의 객체와 PIE 월드의 객체를 같은 생명주기로 생각하면 Play를 반복할 때의 버그를 설명하기 어렵습니다. 이 프로젝트에서 소켓 연결은 클라이언트 컴포넌트의 BeginPlay에 있고, 매 프레임 처리할 일은 TickComponent에 있습니다. Play가 끝나면 EndPlay가 연결과 표시 자원을 정리합니다. 정리가 필요한 이유는 메모리만이 아닙니다. 사운드가 계속 울리거나 이전 소켓의 이벤트가 새 플레이에 들어오는 것도 수명 관리 실패입니다. 표는 학습용 책임 구분이며 모든 엔진 내부 초기화 단계를 생략 없이 나열한 것은 아닙니다. PostLoad와 OnConstruction 등은 필요해질 때 Epic의 전체 생명주기 설명으로 확장하세요.',
 [src('SimCoreClientComponent.cpp','USimCoreClientComponent::BeginPlay',50,84),lifeRef],
 {headers:['단계','현재 프로젝트에서 하는 일','피할 혼동'],rows:[['생성자','컴포넌트·기본값 구성','현재 플레이가 이미 존재한다고 가정'],['BeginPlay','새 Play ID·연결·표현 초기화','이전 Play 상태 재사용'],['Tick / TickComponent','입력·통신·표현 갱신','서버 고정 스텝과 동일시'],['EndPlay','소켓·사운드·Ghost 정리','GC 때까지 통신을 방치']]});

code(0, '수신 상태는 값과 도착 나이를 함께 꺼냅니다',
 ['OutState는 저장된 상태의 복사본입니다.', 'StateAge는 “마지막 상태가 이 클라이언트에 도착한 뒤 지난 시간”입니다.'],
 '143행은 아직 수신 상태가 없다면 실패를 반환합니다. 144행은 내부 LatestState를 호출자의 OutState에 복사합니다. 이어지는 계산은 현재 로컬 단조 시계와 마지막 수신 시각의 차이를 구하고 음수가 되지 않도록 제한합니다. 마지막의 true는 값을 꺼냈다는 뜻이지, 이 값이 충분히 신선하다는 뜻은 아닙니다. 신선도 판정은 호출자가 자신의 제한과 비교해야 합니다. 이 나이는 서버의 시뮬레이션 시간과 직접 뺀 값도, 왕복 지연 측정값도 아닙니다. 60fps로 같은 상태를 여러 번 표시할 수 있으며, 그동안 OutState의 Sequence는 같아도 StateAge는 증가합니다. 이 차이가 끊긴 연결을 화면에서 안전하게 다루는 출발점입니다.',
 'SimCoreClientComponent.cpp','USimCoreClientComponent::GetLatestState',143,148);

add(0, 'lesson', 'Tick의 시간은 서버 물리 스텝이 아닙니다',
 ['DeltaSeconds는 해당 프레임 사이의 경과 시간입니다.', '입력 완만화·휠 회전·카메라·표시 효과가 프레임 갱신을 이용합니다.', '서버 상태가 새로 왔는지는 Sequence와 수신 시각으로 판단합니다.'],
 'Unreal이 초당 60번 그린다고 서버가 60개의 새로운 상태를 보냈다는 뜻은 아닙니다. 반대로 한 렌더 프레임 사이에 여러 네트워크 상태가 도착할 수도 있습니다. 현재 Pawn은 Tick에서 최신 상태를 가져와 표시 샘플을 만들고, 클라이언트 컴포넌트는 별도 TickComponent에서 송신 주기와 프록시 표시를 갱신합니다. 프레임 독립적인 입력 변화율에는 DeltaSeconds가 필요하지만, 이미 마우스에서 누적 이동량으로 전달된 값에 시간을 또 곱하면 감도가 프레임률에 따라 달라질 수 있습니다. 뒤의 카메라 예제에서 두 종류 입력을 비교합니다. 지금은 프레임 시계·도착 시계·시뮬레이션 시계 세 칸을 만들고 각각 사용하는 함수를 분류하세요.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::Tick',176,245),src('SimCoreClientComponent.cpp','USimCoreClientComponent::TickComponent',70,84)]);

add(0, 'lesson', '콜백 수명은 객체와 연결 세대를 모두 확인합니다',
 ['약한 참조는 객체가 사라졌는지 확인합니다.', 'SocketGeneration은 이전 소켓의 늦은 콜백을 구분합니다.', 'EndPlay와 Disconnect는 델리게이트와 연결을 함께 정리합니다.'],
 '예를 들어 연결 A가 끊기고 연결 B를 시작한 뒤 A의 종료 이벤트가 늦게 도착할 수 있습니다. 컴포넌트 자체는 여전히 살아 있으므로 WeakThis만 검사하면 B의 상태를 잘못 끊을 수 있습니다. 이때 연결을 시작할 때 캡처한 Generation과 현재 세대를 비교하는 검사가 필요합니다. 반대로 세대 값이 맞더라도 객체가 이미 끝났다면 접근하면 안 됩니다. 두 검사는 서로 대체되지 않습니다. 이 프로젝트는 ReleaseSocket에서 등록한 델리게이트 핸들을 제거하고, 게임 스레드에 전달된 작업에서도 안전성을 확인합니다. 생명주기 문제를 볼 때 “포인터가 null인가”만 묻지 말고 “이 이벤트가 지금 플레이와 지금 연결에 속하는가”까지 질문하세요.',
 [src('SimCoreClientComponent.cpp','USimCoreClientComponent::Disconnect',104,122),src('SimCoreClientComponent.cpp','USimCoreClientComponent::ReleaseSocket',697,733)]);

code(0, 'Build.cs는 사용 가능한 엔진 기능을 선언합니다',
 ['C++의 include와 모듈 의존성은 다른 층의 설정입니다.', '이 프로젝트는 WebSockets·AudioMixer·ProceduralMeshComponent를 사용합니다.'],
 '표시된 문자열은 모듈 이름입니다. Core와 CoreUObject는 기반 타입과 UObject 체계를, Engine은 Actor와 월드 같은 게임 기능을 제공합니다. InputCore는 키 입력 관련 타입, AudioMixer는 합성 오디오, ProceduralMeshComponent는 실행 중 메시 처리, WebSockets는 외부 서버 연결에 연결됩니다. 헤더를 include했더라도 필요한 모듈 의존성이 없다면 빌드나 링크에서 문제가 날 수 있습니다. 반대로 목록에 모듈을 넣었다고 해당 기능이 자동으로 작동하지는 않습니다. 실제 컴포넌트 생성과 함수 호출이 필요합니다. 이 코드 아래의 sensors.json RuntimeDependencies도 확인하세요. 설정을 소스로 읽는 것과 패키징 결과에 파일을 포함시키는 것은 별도로 챙길 일입니다.',
 'DriveIntegration.Build.cs','DriveIntegration',13,20);

add(1, 'table', 'Axis와 Action은 입력을 전달하는 방식이 다릅니다',
 ['현재 프로젝트는 DefaultInput.ini와 BindAxis / BindAction을 사용합니다.', 'UE 5.6에서 가능한 다른 입력 체계와 현재 구현을 섞지 마세요.'],
 'Axis는 값이 계속 전달되는 입력에 적합하고 Action은 눌림·해제 같은 사건을 구분하는 데 적합합니다. 하지만 키보드는 반드시 Action만 써야 하는 것은 아닙니다. 현재 W와 S는 Axis로 등록되어 페달 값을 전달합니다. A와 D는 눌림 상태를 Action으로 저장하고 Tick에서 완만한 조향 명령으로 바꿉니다. H는 눌림과 해제가 모두 중요합니다. 눌림만 처리하면 지속 경적을 끝낼 근거가 사라집니다. 표를 본 뒤 DefaultInput.ini에서 이름을 찾고 SetupPlayerInputComponent에서 같은 문자열을 찾으세요. 설정의 이름과 바인딩 문자열이 다르면 구현 함수가 올바르더라도 입력이 연결되지 않습니다.',
 [{path:'unreal/DriveIntegration/Config/DefaultInput.ini',symbol:'AxisMappings',start:87,end:114},inputRef],
 {headers:['키','입력 이름','방식','용도'],rows:[['W / S','Throttle / Brake','Axis','전진·후진 페달 의도'],['A / D','SteerLeft / SteerRight','Action','눌림 상태로 조향 완만화'],['Space','Handbrake','Action','사이드 브레이크'],['H','Horn','눌림 + 해제','누르는 동안 경적'],['R / F6','DriveRecord / DriveReplay','Action','기록 / Ghost 재생']]});

add(1, 'lesson', '키가 안 먹으면 입력 초점을 먼저 확인합니다',
 ['Play한 뷰포트가 키 입력을 받고 있는지 확인합니다.', 'Pawn 빙의와 FInputModeGameOnly 설정은 별개의 확인 지점입니다.', '입력 문제와 서버 운전 권한 문제를 로그로 구분합니다.'],
 '에디터 단축키가 실행되거나 카메라만 움직인다면 물리 수식을 바로 의심하기보다 입력이 어디로 전달되는지 확인해야 합니다. SetupPlayerInputComponent 끝부분은 마우스 커서를 숨기고 게임 전용 입력 모드를 설정합니다. 생성자의 자동 빙의 설정도 함께 확인할 수 있습니다. 그럼에도 서버가 지도 불일치나 연결 문제로 명령을 받지 못하면 차는 움직이지 않습니다. 따라서 SetThrottle에 값이 들어오는지, PendingControl이 변하는지, 연결 상태가 Connected인지 순서대로 구분하세요. 디버거로 장시간 멈추면 서버 제어 임대가 만료될 수 있으므로 첫 진단에서는 짧은 로그나 조건을 좁힌 관찰을 우선하는 편이 현상을 덜 바꿉니다.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::SetupPlayerInputComponent',313,321),src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::AExternalVehiclePawn',34,40)]);

code(1, 'W를 누르면 위치가 아니라 페달 값이 바뀝니다',
 ['Throttle 바인딩은 SetThrottle을 호출합니다.', '이 함수에는 SetActorLocation이나 서버 Send가 없습니다.'],
 '770행의 Value는 Throttle 축에서 전달된 값입니다. 772행은 0과 1 사이로 제한한 뒤 ForwardPedalInput에 보관합니다. 773행의 PushControl은 현재 다른 입력과 차량 상태를 함께 사용해 최종 운전 명령을 정리합니다. 함수가 짧아 보이지만 책임 분리가 잘 드러나는 지점입니다. W를 눌렀을 때 차가 정확히 몇 미터 이동할지는 여기서 결정하지 않습니다. 또한 이 호출 한 번이 네트워크 패킷 한 개와 같지도 않습니다. SetControl에서 최신 명령을 저장한 뒤 클라이언트의 송신 주기가 실제 전송을 결정합니다. 노트에는 “입력 값 변경”, “명령 해석”, “명령 보관”, “송신”을 서로 다른 동사로 써 보세요.',
 'ExternalVehiclePawn.cpp','AExternalVehiclePawn::SetThrottle',770,774);

add(1, 'lesson', 'S는 즉시 후진 기어를 강제로 넣지 않습니다',
 ['SetBrake라는 이름의 함수는 ReversePedalInput을 저장합니다.', 'ControlResolver는 진행 방향·기어·신선한 속도를 함께 판단합니다.', '앞으로 달릴 때 S는 먼저 제동하고, 방향 전환 조건이 되면 후진합니다.'],
 '함수 이름 하나만 읽으면 S가 언제나 브레이크라고 생각하기 쉽습니다. 실제로는 전진과 후진 페달 의도를 별도로 보관한 뒤 Resolver가 Throttle·Brake·Gear로 변환합니다. 예를 들어 앞으로 충분히 움직이는 상태에서 후진 페달을 누르면 기어를 즉시 뒤집지 않고 제동을 요구합니다. 속도가 방향 전환 기준에 가까워지고 상태가 신선해야 후진 선택을 허용할 수 있습니다. W와 S를 동시에 누른 경우에도 두 추진 명령을 더하는 대신 제동 우선 규칙이 있습니다. 정지한 차량에서의 동작만 테스트하면 이 정책을 놓칩니다. 전진 중 S, 후진 중 W, 상태가 오래된 경우를 각각 따로 적고 Resolve의 분기를 찾아 보세요.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::PushControl',850,881),src('SimCoreVehicleControlResolver.cpp','Resolve',5,71)]);

code(1, '짧은 A 입력을 작은 조향 명령으로 바꿉니다',
 ['같이 누르거나 모두 놓으면 목표값은 0입니다.', '누르는 속도와 복귀 속도를 나누고 DeltaSeconds를 사용합니다.'],
 '18~20행은 좌우 키 상태로 목표값을 고릅니다. 둘의 상태가 같으면 중립이며, 왼쪽만 누르면 +1, 오른쪽만 누르면 −1입니다. 21~23행은 목표가 중립일 때 복귀 속도를 선택하고 그 외에는 조향 상승 속도를 선택합니다. 이어지는 세 줄은 변화율이 유한한 양수인지 확인합니다. 마지막의 FInterpConstantTo는 프레임당 고정량이 아니라 초당 변화율을 이용하여 현재값을 목표값 쪽으로 이동시킵니다. 이 함수에는 차량 속도가 입력되지 않습니다. 그다음 ResolveCommand에서는 키보드 값의 부호를 보존한 제곱으로 초반 감도를 더 작게 만듭니다. 명령의 부드러움과 실제 타이어 힘 때문에 달라지는 회전 반응을 분리해서 이해하세요.',
 'SimCoreSteeringInput.cpp','AdvanceKeyboardCommand',18,27);

add(1, 'lesson', '조향의 부호는 경계에서 한 번 바꿉니다',
 ['장치 X축은 오른쪽 +, 현재 명령 계약은 왼쪽 +입니다.', '게임패드 축은 InputAxisToCanonicalSteering에서 변환합니다.', 'A / D는 처음부터 계약의 부호로 목표값을 만듭니다.'],
 '오른쪽 키를 눌렀는데 차가 왼쪽으로 가면 여러 곳에 마이너스를 붙여 우연히 맞추기 쉽습니다. 그렇게 하면 바퀴 표시나 카메라 회전에서 다시 부호가 틀어질 수 있습니다. 현재 코드는 장치 입력의 오른쪽 양수를 외부 명령의 왼쪽 양수로 바꾸는 경계를 명명했습니다. 키보드 경로는 왼쪽 키의 목표를 처음부터 양수로 만들기 때문에 같은 변환을 다시 적용하지 않습니다. 실제 바퀴를 Unreal Yaw로 표시할 때에는 별도의 좌표 변환 함수를 사용합니다. 노트에 장치 값, ControlCommand.Steering, 서버 SteeringAngleRad, Unreal 바퀴 Yaw 네 값을 나란히 놓고 오른쪽 조향 예를 적으면 중복 변환을 찾기 쉽습니다.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::SetSteering',782,787),src('SimCoreCoordinateFrames.cpp','InputAxisToCanonicalSteering',245,253)]);

code(1, 'SetControl은 최신 명령 보관함을 갱신합니다',
 ['입력 범위와 데드존을 정리한 값이 PendingControl에 들어갑니다.', 'bControlDirty는 의미 있는 변경이 있는지를 나타냅니다.'],
 '131~133행은 가속·제동을 0~1, 조향을 −1~1로 제한하고 작은 입력을 데드존으로 처리합니다. 이어서 사이드 브레이크와 기어를 저장합니다. 마지막 줄은 이전 전송값과 비교했을 때 다시 보낼 가치가 있는 변화인지 계산합니다. 이 코드에는 Socket->Send가 없습니다. 매 프레임 입력을 읽는 빈도와 서버에 명령을 전송하는 빈도를 분리하기 위한 설계입니다. 같은 프레임 안에서 페달과 조향 함수가 여러 번 호출되어도 보관함은 최신 조합을 유지합니다. 여기서 명령마다 무조건 큐를 추가하면 오래된 입력이 뒤늦게 소비되는 지연 문제가 생길 수 있으므로, 현재 값 보관이라는 성질을 주의해서 읽으세요.',
 'SimCoreClientComponent.cpp','USimCoreClientComponent::SetControl',131,136);

add(1, 'lesson', '입력 변화 송신과 생존 확인 송신을 구분합니다',
 ['기본 CommandRateHz는 20Hz, 변경 입력 상한은 MaxChangedCommandRateHz 30Hz입니다.', '바뀐 값은 더 빠르게 보내되, 같은 값도 정해진 간격으로 갱신합니다.', '프레임률을 높이는 것만으로 명령 송신률이 무한히 높아지지는 않습니다.'],
 '값이 바뀐 경우만 보내면 키를 계속 누르고 있는 동안 서버가 클라이언트의 제어가 살아 있는지 알기 어려워집니다. 반대로 모든 Tick마다 보내면 불필요한 전송과 처리 부하가 생깁니다. 현재 코드는 누적 시간과 Dirty 여부에 따라 간격을 고릅니다. 20Hz는 약 50ms, 30Hz는 약 33.3ms의 간격이지만 실제 프레임 스케줄에 따라 호출 시점은 양자화됩니다. 그래서 숫자를 하드 실시간 보장으로 설명하면 안 됩니다. ShouldSendControl에는 연결과 지도 검증 조건도 있습니다. 송신 주기를 볼 때 단순히 Hz만 찾지 말고 “전송해도 되는가”와 “언제 다시 보내는가” 두 질문을 나눠 읽으세요.',
 [src('SimCoreClientComponent.h','CommandRateHz',58,76),src('SimCoreClientComponent.cpp','USimCoreClientComponent::TickControlTransmission',297,315),src('SimCoreClientComponent.cpp','USimCoreClientComponent::ShouldSendControl',740,747)]);

add(1, 'table', '소켓 연결 성공은 운전 준비 완료가 아닙니다',
 ['Hello·지도·Play 세션을 통과해야 현재 플레이의 상태를 사용할 수 있습니다.'],
 'WebSocket이 열렸다는 것은 바이트를 주고받을 통로가 생겼다는 뜻입니다. 그러나 서로 다른 스키마나 지도를 사용하는 프로그램끼리 차를 움직여서는 안 됩니다. 클라이언트는 Hello로 호환성을 확인하고, 받은 상태의 지도 정보도 검사합니다. 새로운 PIE는 PlaySessionId로 구분되며 서버에 초기화 요청을 보낸 뒤 해당 세션의 상태를 받아야 합니다. 연결을 재시도하는 경우와 새 Play를 시작하는 경우를 같은 사건으로 단순화하지 마세요. 전자는 같은 플레이의 통신 복구일 수 있고 후자는 새로운 시뮬레이션 실행을 요청하는 상황입니다. 표의 검사들을 통과하지 못했다면 움직임보다 실패 이유를 먼저 보여 주는 것이 안전한 동작입니다.',
 [src('SimCoreClientComponent.cpp','USimCoreClientComponent::ApplyConnected',861,877),src('SimCoreClientComponent.cpp','USimCoreClientComponent::ApplyBinaryMessage',903,1108)],
 {headers:['검사','확인하는 것','실패 시 의미'],rows:[['WebSocket','통신 통로가 열렸는가','연결 / 재연결 문제'],['Hello','스키마·기능 계약이 맞는가','호환 불가'],['Map identity','같은 지도 데이터인가','다른 지면으로 계산할 위험'],['PlaySessionId','이번 Play의 상태인가','이전 실행 상태'],['Sequence','이전보다 새 상태인가','중복 또는 순서 역전']]});

add(1, 'lesson', '자체 프로토콜로 송신한다는 의미',
 ['ControlCommand는 Envelope 안에 직렬화되어 바이너리 메시지로 전송됩니다.', 'Unreal의 Replicated 속성이나 RPC가 이 왕복을 대신하지 않습니다.', '프로토콜 코드는 바이트 형식과 값의 범위를 함께 검증합니다.'],
 'SetControl에 보관된 C++ 구조체를 그대로 메모리 복사해서 서버에 보내는 것은 아닙니다. 메모리 배치와 패딩은 플랫폼이나 컴파일 조건의 영향을 받을 수 있기 때문입니다. 이 저장소는 명시한 필드 번호와 wire 형식으로 값을 기록하고 Envelope에 넣습니다. C++ 서버는 생성된 Protobuf 코드를 쓰지만 Unreal 쪽은 SimCoreProtocol.cpp의 전용 작성기와 파서를 사용합니다. 따라서 스키마가 바뀌면 파일 하나만 바꾸고 끝났다고 생각할 수 없습니다. 송신·수신·검증·테스트가 같은 계약을 이해해야 합니다. 여기서는 모든 varint 구현을 암기하기보다 SerializeControlEnvelope가 어떤 입력 구조체를 받고 어떤 바이트 배열을 반환하는지부터 확인하세요.',
 [src('SimCoreProtocol.cpp','SerializeControlEnvelope',934,968),src('SimCoreClientComponent.cpp','USimCoreClientComponent::SendControl',749,766)]);

code(1, '소켓 버퍼는 콜백 안에서 복사합니다',
 ['콜백이 받은 Data 포인터를 나중에 그대로 사용하면 수명 문제가 생깁니다.', '크기를 검사한 뒤 소유한 TArray에 복사합니다.'],
 '640~642행은 int32로 표현할 수 있는 크기인지, 애플리케이션의 최대 메시지 크기를 넘지 않는지, 데이터가 필요한데 포인터가 비어 있지는 않은지 검사합니다. 643행의 Fragment는 이 코드가 소유하는 배열입니다. 조건을 통과하고 실제 데이터가 있을 때만 Append로 바이트를 복사합니다. 뒤쪽에서는 이 배열을 MoveTemp로 게임 스레드 작업에 넘깁니다. 외부 라이브러리에서 잠깐 빌려준 버퍼와 작업이 끝날 때까지 살아 있어야 하는 배열은 다른 수명입니다. 비동기 코드에서 가장 먼저 물어야 할 질문은 “이 포인터는 누가 소유하고 언제까지 유효한가”입니다. 복사 비용이 있다고 검사를 지우거나 원시 포인터만 캡처하면 안전성을 잃습니다.',
 'SimCoreClientComponent.cpp','USimCoreClientComponent::BindSocketDelegates',640,647);

code(1, '엔진 객체에 반영할 작업을 게임 스레드로 보냅니다',
 ['이미 게임 스레드라면 바로 호출하고, 그렇지 않으면 해당 스레드에 예약합니다.', '스레드를 바꾸는 일은 이전 연결의 이벤트를 허용한다는 뜻이 아닙니다.'],
 '20행은 현재 실행 위치가 게임 스레드인지 확인합니다. 그렇다면 22행에서 작업을 바로 수행하고 반환합니다. 그렇지 않은 경우 26행은 AsyncTask를 통해 게임 스레드에서 실행되도록 전달합니다. 네트워크 콜백이 언제나 게임 스레드에서 호출된다고 가정하지 않기 위한 작은 경계 함수입니다. 배열을 안전하게 복사하는 것만으로 Actor와 컴포넌트 접근의 실행 위치 문제가 해결되지는 않습니다. 반대로 게임 스레드에서 실행한다고 이미 끝난 객체나 옛 소켓의 이벤트가 유효해지는 것도 아닙니다. 그러므로 전달된 작업 안에서 약한 참조와 연결 세대도 확인합니다. 이 코드가 모든 무거운 작업을 게임 스레드에 몰아넣으라는 일반 규칙은 아니며, 엔진 상태 반영의 경계를 명시한 현재 구현입니다.',
 'SimCoreClientComponent.cpp','ApplyOnGameThread',20,26);

add(1, 'lesson', '메시지 조각과 완성 메시지는 다릅니다',
 ['OnBinaryMessage의 마지막 조각 표시를 확인해 메시지를 조립합니다.', '프레임의 남은 바이트 수를 전체 메시지 완료와 혼동하지 않습니다.', '누적 크기 제한은 조각마다 적용해야 합니다.'],
 '큰 상태 메시지는 여러 조각으로 들어올 수 있습니다. 각 조각을 완성된 Protobuf Envelope라고 가정하면 일부 메시지만 파싱하거나 정상 연결을 오류로 처리할 수 있습니다. 반대로 계속 누적하기만 하면 손상된 입력으로 메모리가 증가할 수 있습니다. 현재 ApplyBinaryMessage는 연결 세대를 검사하고 수신 버퍼에 조각을 모은 뒤 마지막 조각이 되었을 때 파싱합니다. 이전에 길이가 컸던 메시지의 남은 데이터가 새 연결로 넘어가지 않도록 세션 초기화에서도 버퍼를 정리합니다. 스레드 전환과 조각 조립은 목적이 다릅니다. 전자는 엔진 객체 접근의 실행 위치를, 후자는 네트워크 메시지의 완전성을 해결합니다.',
 [src('SimCoreClientComponent.cpp','USimCoreClientComponent::ApplyBinaryMessage',903,949),src('SimCoreClientComponent.cpp','USimCoreClientComponent::ResetConnectionSession',510,544)]);

add(1, 'lesson', '파싱 성공 뒤에도 상태를 바로 표시하지 않습니다',
 ['바이트를 읽을 수 있다는 것과 현재 상태로 받아도 된다는 것은 다릅니다.', 'Hello 이후의 지도·세션·순서 검사가 LatestState 교체를 보호합니다.', '알 수 없는 선택 필드는 건너뛸 수 있어도 손상된 데이터는 거절합니다.'],
 '파서는 필드 형식과 메시지의 구조를 읽습니다. 하지만 형식이 맞는 과거 Play 상태도 현재 플레이에 적용하면 차량이 순간이동할 수 있습니다. 이 때문에 클라이언트 컴포넌트의 ApplyBinaryMessage는 파싱 후 추가 계약을 검사합니다. 같은 맵인지, 현재 Play ID인지, Sequence가 최신보다 큰지 확인한 다음 LatestState를 교체합니다. 앞으로 새로운 선택 필드가 생겼을 때 기존 파서가 모르는 필드를 건너뛰는 호환성과, 필수 의미가 맞지 않는 상태를 받아들이는 것은 다른 정책입니다. 소스 읽기에서는 return 지점 옆에 “구조 오류”, “다른 연결”, “다른 맵”, “과거 상태”처럼 거절 이유를 짧게 적어 보세요.',
 [src('SimCoreProtocol.cpp','ParseWorldStateEnvelope',1167,1225),src('SimCoreClientComponent.cpp','USimCoreClientComponent::ApplyBinaryMessage',995,1108)]);

add(1, 'lesson', '새 Play와 재접속은 상태를 섞지 않아야 합니다',
 ['BeginPlay는 새 PlaySessionId를 만듭니다.', 'Reset 요청과 수신 Play ID 검사는 이전 차량 위치가 넘어오는 것을 막습니다.', 'SocketGeneration·PlaySessionId·Sequence는 서로 다른 범위를 구분합니다.'],
 'SocketGeneration은 클라이언트 내부의 연결 세대이며, PlaySessionId는 플레이 실행의 정체성, Sequence는 수신 상태의 순서입니다. 셋 중 하나만으로 나머지를 대신할 수 없습니다. 예를 들어 같은 Play에서 재접속하면 소켓 세대는 달라질 수 있지만 Play의 의미는 유지됩니다. 반대로 Stop 후 Play를 다시 누르면 새 실행을 구분할 식별자가 필요합니다. Sequence가 크다고 다른 Play의 상태를 허용해서도 안 됩니다. 이 구분은 자동 깜빡이 취소나 센서 기록에도 이어집니다. 같은 상태를 여러 렌더 프레임에서 보았다고 새로운 회전 증거나 새 센서 프레임으로 세지 않는 이유를 이 세 범위와 연결해 보세요.',
 [src('SimCoreClientComponent.cpp','USimCoreClientComponent::BeginPlay',50,56),src('SimCoreClientComponent.cpp','USimCoreClientComponent::SendSimulationReset',767,789),src('SimCoreClientComponent.cpp','USimCoreClientComponent::ApplyBinaryMessage',1064,1108)]);

add(2, 'lesson', '신선도·예측 한도·서버 타임아웃은 다릅니다',
 ['클라이언트의 상태 나이는 마지막 도착 이후의 로컬 시간입니다.', '표시 예측은 제한된 시간까지만 허용합니다.', '서버 제어 임대 만료는 클라이언트 표시 신선도와 별개입니다.'],
 '상태가 120ms 동안 오지 않았다는 말과 입력이 서버에 120ms 동안 도착하지 않았다는 말은 서로 다른 방향의 문제입니다. 현재 클라이언트에는 기본 100ms 수준의 상태 신선도 조건이 있으며 표현 함수에는 별도의 최대 외삽 시간이 있습니다. 이 수치를 늘려 화면이 계속 움직이게 한다고 통신이 회복되는 것은 아닙니다. 마지막 속도로 오래 예측하면 실제 벽이나 사고 상태를 무시한 화면이 됩니다. 서버의 soft·hard 제어 임대 시간은 입력 수신 안전 정책이므로 또 따로 읽어야 합니다. “60fps인데 늦다”는 관찰을 기록할 때 FPS, 상태 Sequence 변화, 도착 나이, 연결 상태를 나눠 적으면 원인을 크게 좁힐 수 있습니다.',
 [src('ExternalVehiclePawn.h','StateStaleTimeoutSeconds',105,124),src('SimCorePresentation.cpp','BuildVehicleSample',43,51),src('SimCoreClientComponent.cpp','USimCoreClientComponent::GetLatestState',139,148)]);

code(2, '표시 예측은 최신 상태의 짧은 외삽입니다',
 ['PredictionSeconds는 상태 나이와 최대 외삽 시간 중 작은 값입니다.', '이 계산은 새로운 서버 권위 상태를 만드는 물리 시뮬레이션이 아닙니다.'],
 '44행은 반환할 표시 샘플을 만듭니다. 45행은 상태 나이가 음수가 되지 않도록 정리합니다. 46~48행은 그 값이 최대 외삽 시간을 넘지 않도록 제한합니다. 49~51행에서는 차량 로컬 속도를 지도 ENU 속도로 변환합니다. 뒤의 위치 계산은 이 속도와 짧은 예측 시간을 이용해 화면 위치를 조금 전진시킵니다. 가속·브레이크·충돌을 새로 계산하는 서버 물리와는 다릅니다. 예를 들어 최신 상태가 20ms 전이고 속도가 10m/s라면 단순 수평 외삽 거리는 0.2m입니다. 이 값을 PendingControl에 되돌려 넣거나 서버의 실제 위치로 기록하면 표시 보정을 권위 계산으로 오해하는 설계가 됩니다.',
 'SimCorePresentation.cpp','BuildVehicleSample',44,51);

add(2, 'lesson', '보간·외삽·완만화의 차이를 숫자로 봅니다',
 ['보간: 이미 알고 있는 두 상태 사이의 값을 구합니다.', '외삽: 최신 상태 이후를 짧게 추정합니다.', '완만화: 현재 표시값이 목표값에 급격히 붙지 않도록 변화시킵니다.'],
 '시각적으로 부드럽다는 결과가 같아 보여도 사용하는 정보와 지연 특성은 다릅니다. 위치 A와 B 사이의 중간값을 구하는 보간은 두 상태가 필요합니다. 최신 위치에 속도×20ms를 더하는 외삽은 미래를 짧게 추정합니다. 운전자의 항의 동작 Alpha를 초당 일정량 바꾸는 완만화는 상태를 바꾸는 속도를 제한합니다. 현재 플레이어의 핵심 BuildVehicleSample은 제한 외삽을 사용하므로 “항상 두 네트워크 프레임을 보간한다”고 설명하면 틀립니다. Ghost 재생은 기록된 프레임에서 샘플을 구하며 다른 경로입니다. 부드럽게 만들겠다고 모든 곳에 FInterpTo를 넣기 전에 추가 지연이 허용되는 대상인지부터 판단하세요.',
 [src('SimCorePresentation.cpp','BuildVehicleSample',43,96),src('SimCoreDriverPresentation.cpp','USimCoreDriverPresentation::AdvancePresentation',588,610),src('SimCoreDriveReplayFormat.cpp','SimCoreDriveReplay::Sample',93,135)]);

add(2, 'table', '월드 ENU와 차량 FLU를 구분합니다',
 ['ENU는 지도 기준, FLU는 차량 기준입니다.', 'Unreal 월드의 북쪽 배치는 이 프로젝트의 약속이며 엔진의 지리적 규칙이 아닙니다.'],
 '같은 숫자 벡터라도 어떤 좌표계에 속하느냐에 따라 의미가 달라집니다. ENU의 첫 성분은 동쪽이고, FLU의 첫 성분은 차량 앞쪽입니다. 북쪽을 바라보는 차량과 동쪽을 바라보는 차량은 같은 전진 속도를 가질 수 있지만 지도상의 이동 방향은 다릅니다. 현재 Unreal 월드는 X를 북쪽, Y를 동쪽으로 대응시킵니다. Actor 로컬에서는 X가 앞, Y가 오른쪽이므로 FLU의 왼쪽 성분은 부호를 바꿉니다. 화면 HUD의 픽셀 좌표는 이 두 3차원 좌표계와 또 다릅니다. 좌표 이름을 변수명에서 지우지 말고 PositionEnu, RelativeLocation, ScreenPosition처럼 기준을 보존하는 습관을 들이세요.',
 [src('SimCoreCoordinateFrames.cpp','MapEnuPolarVectorToUnrealWorld',123,147),src('SimCoreCoordinateFrames.cpp','BodyFluPolarVectorToUnrealActor',205,214),coordRef],
 {imageKey:'coordinates',headers:['공간','X','Y','Z'],rows:[['지도 ENU','East','North','Up'],['차량 FLU','Forward','Left','Up'],['현재 UE 월드','North','East','Up'],['UE 차량 로컬','Forward','Right','Up']]});

code(2, '미터를 센티미터로 바꾸고 표시 원점을 더합니다',
 ['ENU (2, 5, 1)m는 오프셋 0일 때 UE (500, 200, 100)cm입니다.', '축 교환과 단위 변환은 서로 다른 연산입니다.'],
 '133행의 함수 이름은 입력 공간과 단위를 함께 알려 줍니다. 134행은 지도 ENU 미터 위치, 135행은 Unreal 센티미터 단위의 표시 오프셋입니다. 137행은 먼저 동·북 축을 Unreal 월드에 대응시킨 다음 100을 곱합니다. 138행의 오프셋은 이미 센티미터이므로 다시 100을 곱하지 않습니다. 예를 들어 표시 오프셋이 (100, −50, 0)cm이면 앞의 예제 위치는 (600, 150, 100)cm가 됩니다. 위치에는 원점 이동을 더하지만 속도 벡터에는 원점 이동을 더하지 않습니다. 이 차이를 놓치면 맵 원점을 바꾸는 순간 속도가 변하는 것처럼 잘못 계산하게 됩니다. 역변환 함수도 바로 아래에서 확인할 수 있습니다.',
 'SimCoreCoordinateFrames.cpp','MapEnuPositionMetersToUnrealCentimeters',133,139);

add(2, 'lesson', 'Heading은 지도 방위각입니다',
 ['현재 Heading은 북쪽 0°, 동쪽 90°인 나침반 방위각입니다.', '왼쪽 조향 양수와 Heading 증가 방향은 반대입니다.', '359°와 1°의 차이는 단순 뺄셈 대신 각도 래핑으로 구합니다.'],
 '차가 북쪽에서 조금 오른쪽으로 돌면 Heading은 증가합니다. 반면 현재 FLU 계약에서 왼쪽 조향과 왼쪽 yaw 회전은 양수이므로 Heading과 부호를 그대로 공유하지 않습니다. 이 때문에 자동 깜빡이 취소 코드는 선택 방향을 곱하고 Heading 변화의 부호를 조정합니다. 359도에서 1도로 바뀐 회전은 작은 오른쪽 회전인데 단순히 1−359를 계산하면 −358도가 됩니다. FMath::FindDeltaAngleDegrees 같은 최단 각도 차이 계산이 필요한 이유입니다. 테스트가 북쪽 경계 양쪽을 지나도록 만든 것은 우연한 숫자 선택이 아닙니다. 방위각, 일반 수학의 ENU yaw, Unreal 표시 회전을 구분해야 합니다.',
 [src('SimCoreCoordinateFrames.cpp','NavigationHeadingDegreesToEnuYawRadians',255,265),src('SimCoreTurnSignals.cpp','SimCoreTurnSignals::FAutoCancel::Update',63,99)]);

add(2, 'lesson', '경사와 전복은 단일 Yaw 부호로 해결되지 않습니다',
 ['서버 Pitch·Roll·각속도와 좌표계의 의미를 함께 변환합니다.', '부모 차체 회전과 자식 바퀴의 지면 정렬을 구분합니다.', 'Euler 각도에 임의 마이너스를 붙이는 수정은 복합 회전에서 깨질 수 있습니다.'],
 '평지에서 좌우 회전만 시험하면 잘못된 변환도 맞아 보일 수 있습니다. 경사면에서 앞을 들거나 옆으로 기울어진 상태에서는 회전축의 순서와 좌표계의 손잡이 방향이 중요해집니다. 현재 CoordinateFrames는 회전의 기준 변환을 모아 두고, BuildUnrealActorRotation이 Heading·Pitch·Roll과 각속도 예측을 함께 처리합니다. 물리 상태에서 앞이 들렸는데 화면에서 아래로 숙여진다면 이 경계를 먼저 확인합니다. 바퀴를 따로 올려서 차체가 반대로 기울어진 문제를 가리면 전체 자세는 여전히 잘못됩니다. 평지 좌우, 앞 오르막, 좌측 높은 경사, 전복 자세 네 사례를 따로 손으로 그려 예상 방향을 적어 보세요.',
 [src('SimCoreCoordinateFrames.cpp','CanonicalAttitudeToUnrealActorQuaternion',319,325),src('SimCoreCoordinateFrames.cpp','BuildUnrealActorRotation',326,363)],{imageKey:'coordinates'});

add(2, 'lesson', '표시 샘플을 Actor에 적용하는 마지막 경계',
 ['BuildVehicleSample의 결과를 SetActorLocationAndRotation에 전달합니다.', '차체 위치 적용과 바퀴·운전자·손상 표현 갱신은 다른 단계입니다.', '표시 오차를 고치기 위해 서버 상태 자체를 덮어쓰지 않습니다.'],
 'Pawn Tick에서 BuildVehicleSample 호출 뒤를 따라가면 계산한 ActorLocation과 ActorRotation을 실제 Actor에 적용하는 부분을 찾을 수 있습니다. 이곳은 명령 생성 경로가 아니라 상태 표시 경로입니다. 이후 바퀴 피벗과 회전, 손상, 운전자 등은 같은 상태를 각자의 방식으로 해석합니다. 이 경계를 알면 “전체 차가 순간이동한다”와 “차체는 맞는데 바퀴만 뜬다”를 다른 문제로 분류할 수 있습니다. 전자는 수신 상태·좌표·외삽·Actor 적용을, 후자는 접촉점에서 허브 위치를 만드는 계산과 부모 기준을 확인합니다. 메시에 임시 오프셋을 넣어 증상만 덮기 전에 어느 단계의 숫자가 처음 틀리는지 노트로 추적하세요.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::Tick',210,277),src('SimCorePresentation.cpp','BuildVehicleSample',98,145)]);

add(3, 'lesson', '보이는 메시와 충돌 권위는 일치하지 않을 수 있습니다',
 ['플레이어의 표시 차체는 NoCollision입니다.', '차량 운동·충돌 결과는 외부 서버 상태가 결정합니다.', 'Unreal 충돌 쿼리는 도로 측정이나 표현 보조에 사용될 수 있습니다.'],
 'NoCollision을 발견했다고 프로젝트에 충돌이 없다고 결론내리면 안 됩니다. 서버는 내보낸 지도와 동적 상태를 사용해 충돌 결과를 계산하고 Unreal은 그 결과를 표시합니다. 여기서 차체 메시의 Simulate Physics를 켜면 외부 서버와 Unreal이 같은 차량의 위치를 각각 결정하는 두 권위 문제가 생길 수 있습니다. 한편 Unreal의 모든 충돌 기능을 쓰지 않는 것도 아닙니다. 지면 측정은 월드의 충돌 표면을 조회하고, 카메라나 운전자 표현도 목적에 따라 쿼리를 사용할 수 있습니다. “충돌을 쓴다”는 표현 대신 누가 무엇을 감지하고, 누가 힘과 최종 위치를 결정하는지까지 말해야 정확합니다.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::AExternalVehiclePawn',34,48),src('GroundCollisionExporter.cpp','AGroundCollisionExporter::ExportGroundSurface',600,661)],{imageKey:'curb'});

add(3, 'lesson', '지면을 측정하는 Unreal과 계산하는 서버',
 ['GroundCollisionExporter는 저작된 충돌 표면을 샘플링합니다.', '높이·법선·재질과 정적 충돌 정보를 지도 패키지로 전달합니다.', 'Bake 성공과 차량 물리의 실제 주행 성공은 별도로 확인합니다.'],
 '에디터에서 눈에 보이는 Landscape나 도로 메시가 서버 메모리에 자동으로 존재하지는 않습니다. Unreal 쪽 Exporter가 충돌 표면을 읽어 외부 계산이 이해할 수 있는 데이터로 만들어야 합니다. 샘플 간격이 너무 크면 좁은 연석이나 급한 높이 변화가 사라질 수 있고, 범위를 줄이면 운전 가능한 영역이 잘립니다. 따라서 내보내기 성공 문구만 보지 말고 원점 포함 여부, 범위, 빠진 셀, 경계 조건을 확인해야 합니다. 서버는 이 데이터를 지면 질의와 힘 계산에 이용하며, 매 서버 틱마다 Unreal 화면을 직접 읽는 구조가 아닙니다. 지면 저작·측정·패키지 검증·주행 검증 네 단계를 나눠 설명하세요.',
 [src('GroundCollisionExporter.cpp','AGroundCollisionExporter::ExportGroundSurface',600,705),src('SimCoreGroundSnapshot.cpp','Build',210,301)],{imageKey:'curb'});

add(3, 'lesson', '레벨 이름보다 지도 데이터의 정체성이 중요합니다',
 ['런타임 설정과 지도 manifest를 연결 시점에 다시 확인합니다.', '같은 화면의 도로라도 서버가 다른 패키지를 읽으면 계산이 어긋납니다.', '체크섬 불일치를 무시하면 지면 관통 원인을 숨길 수 있습니다.'],
 'L_SignalCity라는 레벨 이름 자체가 외부 서버에 지면을 전달하는 것은 아닙니다. 실제 연결에는 클라이언트가 선택한 지도 패키지와 manifest의 식별 정보가 관여합니다. 시작 또는 재접속 때 설정을 새로 읽는 이유는 이전에 캐시한 지도 정보로 잘못 운전하지 않기 위해서입니다. Bake 후 화면은 새 도로인데 서버가 이전 높이 데이터를 사용하면 차가 뜨거나 바닥으로 들어가는 것처럼 보일 수 있습니다. 이때 체크섬 검사를 지우는 것은 해결이 아닙니다. 현재 설정이 어떤 디렉터리를 가리키는지, manifest의 정체성이 서버 상태와 같은지, 이번 연결에서 갱신됐는지를 확인하세요. 파일명과 데이터 정체성의 차이를 이해하는 연습입니다.',
 [src('SimCoreClientComponent.cpp','USimCoreClientComponent::PrepareRuntimeSettings',266,294),src('SimCoreClientComponent.cpp','USimCoreClientComponent::StartConnectionAttempt',151,191)]);

add(3, 'lesson', 'NPC의 화면 Actor는 운전 판단의 주체가 아닙니다',
 ['서버가 NPC 경로·속도·사고 상태를 계산합니다.', '클라이언트는 Entity 스냅샷을 Actor의 위치·바퀴·운전자 표시로 바꿉니다.', '일반 도로 커브와 차선 변경 의도를 같은 깜빡이 조건으로 취급하지 않습니다.'],
 'NPC가 막힌 차를 피하지 못할 때 화면 Actor의 회전을 더 부드럽게 만든다고 우회 판단이 생기지는 않습니다. RuntimeEntities 경로는 수신한 엔티티를 찾아 필요한 표시 Actor를 만들고 ApplySnapshot으로 상태를 전달합니다. 경로 선택과 국소 우회는 외부 서버 쪽 문제입니다. 반대로 서버 경로가 자연스러운데 바퀴가 정지해 있다면 클라이언트의 바퀴 표현을 봐야 합니다. 현재 NPC 방향지시등도 서버가 보낸 의도를 표현하는 책임에 가깝습니다. 실제 차선 변경과 단순한 커브 주행을 분리하는 규칙은 주행 판단 쪽에서 먼저 의미를 정해야 합니다. 이 구분을 익히면 어느 파일부터 읽을지 선택하는 시간이 크게 줄어듭니다.',
 [src('SimCoreClientRuntimeEntities.cpp','USimCoreClientComponent::TickRuntimeProxyActors',58,160),src('SimCoreNpcPresentationActor.cpp','ASimCoreNpcPresentationActor::ApplySnapshot',223,326)]);

add(3, 'lesson', '차량 모델은 메시·재질·클래스별 배치의 조합입니다',
 ['세단·경차·트럭·오토바이는 같은 장식만 축소한 대상으로 설명하면 부족합니다.', '차체 경로, 바퀴 위치, 실내와 카메라 배치가 클래스별로 달라집니다.', '현재 모델은 저작 코드 중심의 프로토타입이며 상용 차량 아트와 구분합니다.'],
 '현재 차량 외형은 저장소의 시각 계약과 생성 코드, 메시 에셋, 실행 중 컴포넌트 배치가 결합되어 있습니다. 클래스가 바뀌면 단순히 VehicleMesh만 교체하는 것이 아니라 바퀴 수와 위치, 운전자 자세, 램프와 카메라 위치도 맞아야 합니다. 오토바이에 문이 나타났던 종류의 문제는 클래스별 표시 계약을 일부만 바꿨을 때 생길 수 있습니다. 코드로 형상을 만들었다는 사실이 잘못은 아니지만, 실제 제작 차량 수준의 토폴로지·UV·정교한 실내·스캔 재질을 완성했다는 의미도 아닙니다. 포트폴리오에서는 현재 구현한 조합과 이후 아티스트 에셋으로 교체할 수 있는 경계를 솔직하게 설명하세요.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::ConfigureVehicleClass',394,453),src('SimCoreNpcPresentationActor.cpp','ASimCoreNpcPresentationActor::ConfigureVehicleClass',163,221),{path:E+'BuildSedanVisualCommandlet.cpp',symbol:'UBuildSedanVisualCommandlet::Main',start:1770,end:1815}]);

add(3, 'lesson', '투명 유리와 램프는 다른 재질 문제입니다',
 ['유리는 내부가 보이는 표면, 램프는 발광 표면과 실제 조명의 조합입니다.', '불투명 차체 재질 하나로 모든 부품을 표현하지 않습니다.', '렌즈 위치·재질 슬롯·빛의 세기를 함께 확인해야 합니다.'],
 '유리가 투명해졌다고 실내가 자동으로 생기지는 않습니다. 뒤에 실제 운전자와 실내 메시가 배치되어 있어야 합니다. 반대로 깜빡이 표면이 밝은 색이라고 주변을 비추는 광원이 존재하는 것도 아닙니다. 현재 TurnSignals는 차체의 재질 값과 램프 위치에 배치한 조명 컴포넌트를 함께 다룹니다. 손상이 생겨 렌즈 위치가 변하면 광원 위치도 시각적으로 따라야 합니다. 이 내용을 공부할 때 머티리얼 노드를 전부 외우기보다, 차체 메시의 어떤 슬롯이 유리인지, 램프 점등 함수가 어떤 컴포넌트를 켜는지 찾으세요. 현재 절차적 표현이 반사·굴절·광학을 정밀 시뮬레이션한다는 뜻은 아니며 시각적 전달을 위한 구성입니다.',
 [src('SimCoreTurnSignals.cpp','USimCoreTurnSignals::OnRegister',155,193),src('SimCoreTurnSignals.cpp','USimCoreTurnSignals::UpdateSignal',194,227),src('SimCoreSedanVisualContract.h','SimCoreSedanVisualContract',1,27)]);

add(3, 'lesson', '운전자와 문은 사건의 순서를 보여 줍니다',
 ['사고 상태를 운전자 목표 자세로 해석한 뒤 표현 시간이 진행됩니다.', '문 열기·하차·문 닫기·항의는 서로 다른 단계입니다.', '현재 절차적 포즈는 완성된 모션 캡처 애니메이션과 다릅니다.'],
 '운전자가 다쳤다는 서버 상태와 캐릭터의 팔·다리 위치는 같은 데이터가 아닙니다. BuildTarget은 서버 상태를 표현 목표로 바꾸고, AdvancePresentation과 ApplyPose는 시간에 따라 실제 부품을 배치합니다. 하차 과정에서 문이 열리기 전에 몸이 통과하거나 발이 지면에서 뜨면 이벤트의 조건뿐 아니라 단계별 Alpha와 기준 좌표를 봐야 합니다. 클래스에 따라 오토바이 승차 자세는 일반 승용차의 문 동작을 사용해서는 안 됩니다. 현재 구현은 프로토타입 수준의 절차적 포즈와 보조 지면 정렬을 포함하며, 모든 관절에 물리 기반 래그돌과 자연스러운 모션 연결이 완성되었다고 설명하지 않습니다. 관찰한 한계를 기록하는 것도 중요한 공부입니다.',
 [src('SimCoreDriverPresentation.cpp','BuildTarget',340,419),src('SimCoreDriverPresentation.cpp','USimCoreDriverPresentation::ApplyPose',808,879),src('SimCoreDriverPresentation.cpp','USimCoreDriverPresentation::ConfigureVehicleClass',511,534)]);

add(3, 'lesson', '찌그러짐은 메시 변형과 충돌 계산을 분리합니다',
 ['서버의 손상 정보를 이용해 차체의 표시 정점을 변형합니다.', '변형된 메시가 곧 서버의 새 충돌 형상을 뜻하지는 않습니다.', '손상 위치·방향·영향 범위를 나눠 읽습니다.'],
 '차가 부딪힌 뒤 외형이 찌그러지는 효과는 눈에 보이는 정점의 위치를 바꾸는 작업입니다. 현재 DeformableBody는 원래 메시의 데이터를 기준으로 손상 위치와 영역에 따라 정점을 조정하고 ProceduralMeshComponent에 표시합니다. 이 방식은 한쪽 면 전체의 재질만 바꾸는 것보다 국소 손상을 전달하기 좋지만 실제 금속의 소성 변형을 푸는 해석과는 다릅니다. 또한 표시 메시가 변했다고 외부 서버의 충돌 상자가 자동으로 같은 형상으로 바뀌지는 않습니다. 공부할 때 충격 검출, 손상 상태, 정점 변형, 재질 표시 네 단계를 따로 적으세요. “차체가 찌그러졌는데 충돌 범위는 다르다”는 관찰은 이 경계를 점검할 근거가 됩니다.',
 [src('SimCoreDeformableBody.cpp','USimCoreDeformableBody::DeformVertex',29,72),src('SimCoreDeformableBody.cpp','USimCoreDeformableBody::ApplyDamage',73,136)]);

add(3, 'lesson', '바퀴의 조향과 굴림을 별도 축으로 다룹니다',
 ['허브 위치는 접촉점과 타이어 반지름에 맞춰 계산합니다.', '피벗은 조향과 지면 법선을, 자식 메시 회전은 굴림을 나타냅니다.', '서스펜션 링크는 차체와 허브의 상대 위치를 연결합니다.'],
 '바퀴를 하나의 Rotator로 매번 덮어쓰면 조향을 적용한 뒤 굴림을 적용할 때 앞선 회전이 사라질 수 있습니다. 현재 표현은 허브와 피벗의 역할을 분리합니다. 서버 접촉점은 바퀴 중심이 아니므로 지면 법선 방향으로 반지름을 더하는 과정이 필요합니다. 그 위치를 권위 차체의 로컬 기준으로 바꾼 뒤 표시 차체와 함께 이동시켜야 경사에서 바퀴만 미끄러져 보이는 문제를 줄일 수 있습니다. SuspensionPresentation은 허브와 차체 사이의 링크를 배치해 구조를 보여 줍니다. 링크를 보이게 그렸다고 그 링크가 실제 스프링 힘을 계산하는 것은 아닙니다. 힘 계산과 구조 표시를 계속 구분하세요.',
 [src('SimCorePresentation.cpp','BuildVehicleSample',98,145),src('SimCorePresentation.cpp','BuildWheelPivotRelativeRotation',306,327),src('SimCoreSuspensionPresentation.cpp','USimCoreSuspensionPresentation::UpdateLinks',78,130)],{imageKey:'wheel_rig'});

code(3, '속도와 반지름으로 표시 회전 속도를 제한합니다',
 ['굴림의 기준은 ω = v / r이며, 단위는 rad/s입니다.', '현재 표현은 작은 슬립은 허용하되 과도한 헛도는 외관을 제한합니다.'],
 '208~210행은 반지름이 0이 되어 나눗셈이 깨지지 않도록 최소값을 둡니다. 211~212행의 RoadAngularSpeed는 부호 있는 차량 속도를 반지름으로 나눈 값입니다. 뒤의 세 줄은 허용할 시각적 슬립 폭을 절대 최소값과 기준 각속도의 3% 중 큰 값으로 정합니다. 이 폭으로 앞뒤 축의 표시 회전 속도를 제한합니다. 10m/s, 반지름 0.32m라면 약 31.25rad/s이고 초당 약 4.97회전입니다. 이것은 타이어 힘을 결정하는 서버 슬립 모델을 삭제한 것이 아니라 표시 정책입니다. 정지 또는 오래된 상태일 때 표시 회전을 0으로 만드는 앞부분도 함께 확인하면 “정지 중 휠만 돈다”는 문제와 연결할 수 있습니다.',
 'SimCorePresentation.cpp','BuildVehicleSample',208,215);

code(3, '회전 속도를 각도로 누적합니다',
 ['rad/s를 deg/s로 바꾼 뒤 프레임 시간을 곱합니다.', '큰 속도에서 안 도는 것처럼 보이면 표시 샘플링도 함께 확인합니다.'],
 '296~299행은 현재 각도, 각속도, 프레임 시간을 받습니다. 302행에서 라디안을 도로 바꾸고 DeltaSeconds를 곱해 이번 프레임의 각도 증가량을 구합니다. 303행의 360도 나머지는 각도가 끝없이 커지지 않게 합니다. 후진에서는 각속도의 부호가 바뀌므로 굴림 방향도 반대가 됩니다. 무늬가 반복되는 바퀴는 빠르게 회전할 때 화면의 프레임 샘플링 때문에 멈추거나 거꾸로 도는 듯 보일 수 있습니다. 그래서 눈으로만 판단하지 말고 수신 속도, 계산 각속도, 누적 각도도 같이 비교해야 합니다. 반대로 값 자체가 0이면 실제 표시 조건의 문제입니다. 시각적 착시와 계산 오류를 구분하는 작은 실습이 됩니다.',
 'SimCorePresentation.cpp','AdvanceWheelSpinDegrees',296,304);

code(4, 'HUD는 화면 크기로 배치합니다',
 ['계기판은 350×136 기준 크기를 화면 비율에 맞춰 줄입니다.', '오른쪽 아래 여백을 두고 배치해 중앙 운전 시야를 비웁니다.'],
 '65행은 배치 결과를 담는 구조체를 만듭니다. 66~67행은 화면 크기가 비정상 값이거나 0이 되는 경우를 정리합니다. 68행은 가로·세로 비율과 확대 상한 중 가장 작은 값을 사용합니다. 69행은 기준 크기에 이 배율을 곱하고 70행은 같은 기준으로 여백을 계산합니다. 71행은 화면의 오른쪽 아래에서 크기와 여백을 빼 왼쪽 위 위치를 구합니다. 1280×720에서는 약 5.2% 면적을 차지하는 계산입니다. 이 방식은 공간을 줄이는 정책이지 모든 해상도에서 글자 가독성이 자동 보장된다는 뜻은 아닙니다. 작은 화면 검사에서는 경계와 중앙 비움뿐 아니라 실제 글자의 읽기 쉬움도 확인해야 합니다.',
 'SimCoreInstrumentCluster.cpp','SimCoreInstrumentCluster::BuildLayout',65,72);

add(4, 'lesson', '계기판과 디버그 정보는 다른 표시 채널입니다',
 ['계기판은 속도·기어·회전수·연결 상태를 운전 중 보여 줍니다.', 'F3 디버그는 충돌·접촉·진단 정보를 필요할 때 표시합니다.', '정상 숫자처럼 보이는 오래된 값보다 STALE 표시가 안전합니다.'],
 'HUD를 작게 만드는 문제는 단순히 글자를 축소하는 문제가 아닙니다. 평소 운전에 필요한 정보와 문제 분석용 정보를 다른 채널에 배치해야 합니다. BuildDisplayState는 상태가 없거나 오래되었거나 숫자가 비정상인 경우를 구분해 표시를 만듭니다. 속도계에서 m/s를 km/h로 바꾸는 것과 서버 값의 유효성을 확인하는 것도 각각 필요합니다. F3로 추가 정보가 켜져도 필수 연결 상태를 잃어서는 안 됩니다. 화면이 복잡할 때는 어떤 값이 항상 필요한지, 어떤 값은 문제가 있을 때만 필요한지 먼저 분류하세요. 이 프로젝트의 Canvas 기반 HUD는 UMG 위젯을 사용한 구현과 다르므로 면접에서 사용하지 않은 UI 기술을 섞어 설명하지 마세요.',
 [src('SimCoreInstrumentCluster.cpp','SimCoreInstrumentCluster::BuildDisplayState',75,130),src('SimCoreInstrumentCluster.cpp','ASimCoreInstrumentClusterHud::DrawHUD',133,186),src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::ToggleVehicleDebug',522,534)]);

add(4, 'lesson', 'SpringArm과 카메라의 회전을 분리합니다',
 ['SpringArm은 카메라 거리와 장애물 회피를 위한 부품입니다.', '현재 Follow 카메라는 차체 Pitch·Roll을 그대로 상속하지 않습니다.', 'V는 시점을 바꾸고 C는 시점을 초기화합니다.'],
 '경사에서 차체가 흔들리는 운동과 플레이어의 시선을 완전히 동일하게 만들면 운전 화면이 불편할 수 있습니다. 현재 생성자는 CameraBoom에 절대 회전을 사용하고 부모의 Pitch·Yaw·Roll 상속을 끕니다. 이어지는 카메라 갱신 함수가 선택한 모드에 맞는 회전을 따로 구성합니다. 운전석 시점에서는 차체 기준의 시선이 필요하므로 다른 변환을 사용합니다. SpringArm의 충돌 검사와 카메라 지연은 서로 다른 기능이며, 이 프로젝트는 모든 기본 지연 옵션을 무조건 켜는 구성이 아닙니다. 카메라가 늦게 따라온다는 현상과 차량 자체의 입력 지연을 구분하기 위해 고정 시점과 추적 시점에서 같은 조작을 비교해 보세요.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::AExternalVehiclePawn',118,139),src('SimCoreOrbitCamera.cpp','NextMode',23,31),src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::UpdateOrbitCamera',708,747)]);

add(4, 'lesson', '마우스 이동량과 게임패드 회전율은 다릅니다',
 ['마우스 값은 이번 프레임에 누적된 이동량입니다.', '게임패드 축은 유지되는 방향·세기이며 회전율에 시간을 곱합니다.', '두 경로에 DeltaSeconds를 똑같이 붙이면 감도가 달라질 수 있습니다.'],
 '마우스를 같은 실제 거리만큼 움직였는데 프레임률에 따라 카메라 각도가 달라지면 입력의 단위를 의심해야 합니다. AddMouseDelta는 누적 이동량에 감도만 곱합니다. AddGamepadRate는 축 값에 초당 회전 속도와 DeltaSeconds를 곱합니다. 게임패드를 0.5만큼 1초 유지하는 경우에는 그 시간 동안 계속 회전해야 하기 때문입니다. Driver 모드에서는 시야가 몸 뒤를 통과하지 않도록 좌우와 상하 각도 범위도 제한합니다. 연습으로 초당 90도 설정에서 축 0.5를 0.2초 유지하면 9도라는 계산을 해 보세요. 같은 식을 마우스 델타에 적용하지 않는 이유를 단위와 함께 말할 수 있으면 이해한 것입니다.',
 [src('SimCoreOrbitCamera.cpp','AddMouseDelta',89,94),src('SimCoreOrbitCamera.cpp','AddGamepadRate',96,104),src('SimCoreOrbitCamera.cpp','AddDriverMouseDelta',61,69)]);

add(4, 'lesson', '절차적 오디오는 상태를 소리의 매개변수로 바꿉니다',
 ['BuildParameters는 RPM·속도·접촉·슬립을 소리의 목표값으로 바꿉니다.', 'Renderer는 오디오 샘플 속도로 파형과 노이즈를 생성합니다.', '게임 스레드의 상태 갱신과 오디오 샘플 생성은 빈도가 다릅니다.'],
 '엔진음은 RPM 숫자를 화면에 쓰는 것과 달리 매초 수만 개의 샘플을 만들어야 합니다. 현재 구현은 게임 상태에서 주파수와 진폭 같은 목표값을 계산한 뒤 오디오 렌더러가 그 목표에 따라 파형을 생성합니다. 진폭을 갑자기 0에서 크게 바꾸면 클릭이나 거친 전이가 생길 수 있어 변화 속도와 엔벨로프를 사용합니다. 엔진에는 배음 조합이, 타이어에는 불규칙한 노이즈와 공명 성분이 필요합니다. 실제 차량의 녹음과 엔진 부하별 샘플을 사용한 완성형 사운드 제작과는 범위가 다릅니다. 현재 코드는 동작 조건을 전달하는 절차적 사운드이며, 실제 차량 청감 재현은 별도의 아트·오디오 작업이 필요하다는 한계를 함께 설명하세요.',
 [src('SimCoreVehicleAudio.cpp','SimCoreVehicleAudio::BuildParameters',15,69),src('SimCoreVehicleAudio.cpp','SimCoreVehicleAudio::FRenderer::Render',129,191)],{imageKey:'audio'});

code(4, '엔진 회전수에서 기본 주파수를 만듭니다',
 ['현재 소리는 4기통의 단순화된 발화 빈도 모델을 사용합니다.', '차종별 실제 엔진 녹음이나 정밀 연소 모델을 의미하지 않습니다.'],
 '31행은 RPM 입력을 제한하여 음원 매개변수가 과도해지지 않도록 합니다. 32행은 RPM을 지정한 구간의 0~1 값으로 바꿉니다. 33~34행은 크랭크 한 바퀴당 두 번의 연소 사건이라는 4기통 단순화를 사용하므로 rpm÷60×2, 즉 rpm÷30의 주파수를 얻습니다. 35행은 회전수가 높아질수록 진폭을 키우되 제곱근 형태로 증가시킵니다. 예를 들어 3000rpm이면 기본 주파수는 100Hz입니다. 뒤의 Renderer는 기본파에 배음을 더합니다. RPM이 같아도 실제 자동차는 실린더 수·배기계·부하에 따라 다른 소리가 나므로, 이 작은 코드가 실제 모든 차종의 음색을 구현했다고 설명해서는 안 됩니다.',
 'SimCoreVehicleAudio.cpp','SimCoreVehicleAudio::BuildParameters',31,35);

add(4, 'lesson', '타이어음은 속도 하나만으로 켜지지 않습니다',
 ['접촉과 하중이 있고, 속도·슬립각·횡미끄럼 속도가 함께 커야 합니다.', '현재 시작 구간은 8m/s, 0.17rad, 2.2m/s입니다.', '일상적인 조향이나 정지 중 핸들 조작을 스키드음으로 만들지 않습니다.'],
 '회전할 때마다 타이어 소리를 켜면 낮은 속도의 평범한 코너에서도 미끄러지는 차처럼 느껴집니다. 현재 BuildParameters는 바퀴가 지면에 닿고 의미 있는 하중을 받는지 먼저 검사합니다. 이후 차량 속도와 슬립각, 속도×sin(슬립각)으로 구한 횡미끄럼 속도의 게이트를 곱합니다. 세 조건 중 하나가 0이면 결과도 0입니다. 각 시작값에서 갑자기 최대 소리가 나는 것이 아니라 구간 안에서 점진적으로 증가합니다. 노이즈와 공명을 사용하는 렌더 방식은 경보음 같은 고정 순음을 줄이기 위한 표현 선택입니다. 이 조건들이 타이어 마찰력을 계산하는 수식이 아니라 이미 계산된 상태로부터 소리를 선택하는 정책임을 기억하세요.',
 [src('SimCoreVehicleAudio.cpp','SimCoreVehicleAudio::BuildParameters',37,68),src('SimCoreVehicleAudio.cpp','SimCoreVehicleAudio::FRenderer::Render',164,180)]);

add(4, 'lesson', '플레이어 경적은 누르는 동안 유지합니다',
 ['H 눌림은 Held를 켜고, 해제는 Held를 끕니다.', '짧은 Attack·Release 엔벨로프가 시작과 끝의 튀는 소리를 줄입니다.', 'NPC의 이벤트 기반 짧은 경적과 플레이어의 지속 입력은 별도입니다.'],
 'H를 누를 때마다 짧은 소리 하나를 재생하는 방식은 계속 누르는 경적과 다릅니다. 현재 TriggerManualHorn은 Held 상태를 켜고 오디오 스레드에도 전달합니다. HandleHornRelease는 반대로 끕니다. FHeldEnvelope는 눌렀을 때 올라가고 놓았을 때 내려가는 레벨을 부드러운 곡선으로 바꿉니다. 사용자가 다른 창으로 이동해 KeyUp 이벤트를 놓치거나 Play가 일시정지되더라도 소리가 계속 남지 않도록 추가 확인도 있습니다. 이 플레이어 경적은 현재 로컬 오디오 입력이며, 서버에 지속 경적 필드를 새로 보내는 경로라고 설명하면 틀립니다. NPC는 서버의 경적 이벤트 순서를 관찰해 짧은 펄스를 내는 별도 경로를 사용합니다.',
 [src('SimCoreVehicleHorn.cpp','SimCoreVehicleHorn::FHeldEnvelope::Advance',73,83),src('SimCoreVehicleHorn.cpp','USimCoreVehicleHornComponent::TriggerManualHorn',110,143)]);

code(4, '깜빡이 시계는 선택한 순간부터 시작합니다',
 ['방향 선택이 바뀌면 SelectionTimeSeconds를 다시 잡습니다.', '램프와 클릭음은 같은 경과 시간을 사용합니다.'],
 '24~25행은 처음 선택했는지, 방향이나 비상등 상태가 바뀌었는지, 시계가 크게 되돌아갔는지 확인합니다. 해당되면 27행에서 선택 시작 시간을 현재 시간으로 설정합니다. 이어서 선택 상태와 마지막 시각을 저장하고 32행에서 경과 시간을 계산합니다. 33행은 이 값을 반환합니다. 전역 월드 시간을 바로 주기에 나누면 이미 주기의 중간에서 시작해 첫 점등만 짧아질 수 있습니다. 선택 상대 시간을 사용하면 첫 점등이 정상 길이를 갖습니다. 현재 한 주기는 0.72초이고 켜짐은 0.44초입니다. 소리 쪽도 같은 상대 시계의 점등 경계를 관찰해야 램프와 릴레이 클릭이 서로 어긋나지 않습니다.',
 'SimCoreTurnSignals.cpp','SimCoreTurnSignals::FPhaseClock::Update',24,33);

add(4, 'lesson', '자동 취소는 실제 회전 후 중앙 복귀를 기다립니다',
 ['선택 방향으로 조향 0.12rad 이상과 회전 8° 이상이 먼저 필요합니다.', '그 뒤 조향 절댓값이 0.035rad 이하로 돌아오면 취소합니다.', '전진 속도 0.5m/s 이상인 신선한 상태를 근거로 판단합니다.'],
 '깜빡이를 켠 직후 핸들이 중앙에 있다는 이유만으로 바로 끄면 기능을 사용할 수 없습니다. 현재 FAutoCancel은 WaitingForTurn과 WaitingForCentre 두 단계를 둡니다. 선택 방향의 의미 있는 조향과 누적 방위각 변화가 확인되어야 다음 단계로 넘어갑니다. 그 뒤 핸들이 중앙으로 돌아온 것을 보고 선택을 취소합니다. 운전자가 키를 얼마나 눌렀는지가 아니라 서버의 실제 조향각과 이동 상태를 사용하는 것이 중요합니다. 잠깐 조향했지만 차량이 거의 움직이지 않았다면 완료된 회전 증거가 아닙니다. 이 숫자들은 프로젝트의 사용성 정책이며 실제 모든 차량의 기계식 방향지시등 구조를 그대로 모델링한 값은 아닙니다.',
 [src('SimCoreTurnSignals.cpp','SimCoreTurnSignals::FAutoCancel::Update',79,104),src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::Tick',191,214)]);

add(4, 'lesson', '취소하면 안 되는 경우도 상태기계의 일부입니다',
 ['정지·후진·반대 방향 조향만으로는 완료 회전으로 세지 않습니다.', '비상등·선택 변경·새 Play·오래된 상태는 이전 증거를 정리합니다.', '같은 패킷을 여러 프레임 표시해도 회전량을 중복 누적하지 않습니다.'],
 '자동 취소를 테스트할 때 좌회전 한 번 성공한 장면만 확인하면 부족합니다. 비상등은 양쪽에 경고를 주는 기능이므로 일반 방향 선택의 자동 취소 규칙에 넣지 않습니다. 새 Play에서 예전 Heading을 이어받으면 첫 상태만으로 큰 회전이 생긴 것처럼 보일 수 있어 Play ID를 검사합니다. 동일한 SimulationTimeNs를 여러 Tick에서 보았을 때에는 한 번의 증거로만 다룹니다. 긴 시간 점프나 큰 자세 점프도 실제 연속 회전으로 인정하지 않습니다. 이 방어 조건들은 기능을 괜히 복잡하게 만드는 장식이 아니라, 입력·네트워크·표시의 서로 다른 시계가 만나는 지점에서 필요한 의미 검증입니다.',
 [src('SimCoreTurnSignals.cpp','SimCoreTurnSignals::FAutoCancel::Update',44,81),src('SimCoreTurnSignalsTests.cpp','FSimCorePlayerIndicatorAutoCancelGuardTest::RunTest',265,287),src('SimCoreTurnSignalsTests.cpp','FSimCorePlayerIndicatorAutoCancelResetTest::RunTest',292,340)]);

add(4, 'lesson', '배기가스는 상태를 전달하는 시각 효과입니다',
 ['배기구의 로컬 위치와 차량 클래스에 맞춰 효과를 배치합니다.', '상태의 유효성과 RPM 등에 따라 생성량을 조절합니다.', '현재 효과를 실제 배출가스 유동·성분 시뮬레이션으로 설명하지 않습니다.'],
 '배기가스 표현은 엔진이 동작한다는 인상을 주지만 차량 추진력을 계산하지는 않습니다. ExhaustComponent는 권위 상태에서 효과 생성률을 정하고 목표값에 완만하게 접근시킵니다. 연결이 끊겨 유효한 상태를 잃으면 오래된 RPM을 무한히 재사용하지 않고 사용할 수 없는 상태로 처리합니다. 위치도 차량 전체 월드 좌표에 고정하는 대신 배기구의 로컬 배치를 이용해야 차체 회전과 함께 움직입니다. 연기가 차보다 뒤처지거나 차체 내부에서 나오면 생성률보다 부착 관계와 오프셋을 먼저 확인하세요. 실제 배기가스 색·입자 운동·라이팅의 품질은 별도 시각 효과 작업이며 현재 절차적 템플릿의 한계를 구분하는 태도가 중요합니다.',
 [src('SimCoreExhaustComponent.cpp','USimCoreExhaustComponent::ApplyAuthoritativeState',77,109),src('SimCoreExhaustComponent.cpp','USimCoreExhaustComponent::EnsureRuntimeTemplate',111,180)]);

add(5, 'lesson', 'R 기록은 주행 모습을 다시 보는 기능입니다',
 ['R로 권위 상태 기록을 시작·종료하고 F6로 Ghost를 재생합니다.', '기록 파일은 Saved/DriveReplays/last_drive.csv입니다.', '입력을 다시 넣어 물리 결과를 재계산하는 서버 재현과 다릅니다.'],
 '현재 Unreal DriveReplay는 수신한 차량 상태를 기록하고 별도의 Ghost Actor를 움직여 재생합니다. 따라서 같은 입력이 다시 같은 충돌 결과를 만드는지 검증하는 결정론적 서버 물리 재현과 목적이 다릅니다. 화면의 카메라 연출이나 표시 결과를 비교하기에는 유용하지만 이 기록만으로 물리 수식의 재현성을 증명할 수는 없습니다. F5가 에디터 동작과 충돌했던 맥락 때문에 현재 기록 키는 R입니다. 예전 문서나 기억보다 DefaultInput.ini와 ToggleRecording의 현재 안내를 기준으로 읽으세요. CSV에 무엇을 저장하는지, 중복 Sequence가 왜 거절되는지, 맵이나 Play의 정체성이 왜 필요한지까지 확인하면 단순 저장 기능 이상의 계약을 배울 수 있습니다.',
 [src('SimCoreDriveReplay.cpp','USimCoreDriveReplayComponent::ToggleRecording',55,69),src('SimCoreDriveReplay.cpp','USimCoreDriveReplayComponent::ToggleReplay',71,93),src('SimCoreDriveReplayFormat.cpp','FTrack::Capture',48,80)]);

code(5, 'Ghost 재생은 기록에서 샘플을 꺼내 표시합니다',
 ['재생 중에만 기록 시간을 증가시킵니다.', 'ApplySnapshot은 표시 Actor에 결과를 적용하며 운전 명령을 보내지 않습니다.'],
 '109행은 재생할 상태를 담는 임시 구조체입니다. 110행의 Sample은 기록과 재생 경과 시간으로 상태를 구하며 실패하면 재생을 끝냅니다. 111행은 Ghost Actor에 이 상태를 표시하도록 전달합니다. 112행은 이번 프레임 시간을 누적하고, 113행은 기록 길이를 넘었을 때 종료합니다. 여기에는 SetControl이나 서버 물리 Step이 없습니다. 재생 시간을 빨리 돌린다고 서버의 실제 차량이 빨리 움직이는 기능이 아닌 이유가 코드에 드러납니다. 재생 Ghost와 현재 플레이어 차량이 동시에 보일 수 있으므로 어느 Actor를 관찰하는지도 구분하세요. 화면 재생은 표시 경로를 복습하는 좋은 예이며, 서버 입력 로그 재생과 비교할 때 차이를 분명하게 말할 수 있어야 합니다.',
 'SimCoreDriveReplay.cpp','USimCoreDriveReplayComponent::TickComponent',109,113);

add(5, 'lesson', 'SensorRig는 현재 메타데이터 골격입니다',
 ['설정된 센서 종류·장착 변환·주기에 따라 프레임 메타데이터를 만듭니다.', '같은 상태 Sequence를 중복 센서 프레임으로 세지 않습니다.', '이 코드만으로 카메라 영상과 LiDAR 점군 취득이 완성된 것은 아닙니다.'],
 'SensorRig라는 이름만 보고 실제 이미지와 점군을 네트워크로 전송한다고 생각하면 현재 구현을 과장하게 됩니다. ObserveAuthoritativeState는 설정한 센서 주기와 SimulationTimeNs를 비교하고 센서 ID, 기준 프레임, 소스 Sequence, 맵 체크섬, Play ID, 장착 변환 같은 메타데이터를 보관합니다. 맵이나 Play가 바뀌면 마지막 관측 순서와 센서별 시각을 정리합니다. 이것은 추후 센서 데이터 취득의 기준을 일관되게 만드는 골격입니다. 테스트의 20Hz·10Hz 예를 보면 동일한 권위 상태 흐름에서 센서별로 다른 주기를 적용하는 방법을 배울 수 있습니다. 영상 렌더 타깃이나 실제 레이 기반 점군 배열이 어디에 있는지도 찾아 보고, 없다면 미구현 범위라고 명시하세요.',
 [src('SimCoreSensorRig.cpp','USimCoreSensorRigComponent::ObserveAuthoritativeState',143,179),src('SimCoreSensorRigTests.cpp','FSimCoreSensorRigConfigTest::RunTest',11,60)]);

add(5, 'lesson', 'F3와 로그는 서로 다른 질문에 답합니다',
 ['F3는 접촉 위치·차체 범위 등 공간 관계를 보는 데 사용합니다.', '로그는 연결·지도·세션·상태의 시간 순서를 추적합니다.', '디버그 레이는 렌더링의 Ray Tracing 기능과 다릅니다.'],
 '바퀴가 연석에 닿기 전에 밀리는지 확인하려면 화면 위에 그린 접촉점과 충돌 범위가 도움이 됩니다. 반대로 키가 10초 늦게 반영되는 문제는 한 장의 충돌 그림만으로 설명하기 어렵습니다. 수신 상태의 순서와 나이, 연결 경고, 지도 불일치를 시간 순으로 읽어야 합니다. “레이 트레이싱 디버그”라고 부르기 전에 충돌 질의의 선을 그리는 것인지 빛의 경로를 추적하는 렌더링 기술인지 구분하세요. 현재 포트폴리오에서는 실제 구현한 충돌·접촉 시각화와 통신 로그를 정확히 명명하는 편이 좋습니다. 관찰 기록은 기대 동작, 실제 동작, 상태 숫자, 화면 증거 순으로 짧게 남기면 다시 비교하기 쉽습니다.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::DrawVehicleDebug',536,630),src('SimCoreClientComponent.cpp','USimCoreClientComponent::ApplyBinaryMessage',995,1108),{path:'docs/study/lessons/D01-one-day-project-guide.md',symbol:'8-5. 로그를 읽는 순서',start:978,end:1000}]);

code(5, '실제 회귀 테스트에서 성공 조건을 읽습니다',
 ['9° 회전 뒤 0.05rad는 아직 중앙이 아니며, 0.02rad에서 취소됩니다.', '정답 숫자보다 경계와 순서를 검증한다는 점을 보세요.'],
 '244행은 다음 권위 상태의 시뮬레이션 시각을 설정합니다. 245행은 조향이 중앙 근처로 돌아왔지만 아직 기준을 넘는 0.05rad라는 상황입니다. 246행의 TestFalse는 이때 취소하면 안 된다는 명세입니다. 다음 상태는 500ms이고 조향은 0.02rad입니다. 249~250행은 이제 취소해야 한다고 검사합니다. 이 조각 앞에서 실제 선택 방향 회전이 이미 9도 발생하도록 준비했다는 점도 읽어야 합니다. 준비를 생략하고 이 몇 줄만 복사하면 같은 테스트가 아닙니다. 테스트를 공부할 때는 준비 상태, 수행한 행동, 기대 결과의 세 문장으로 번역하세요. 실패 메시지는 요구사항을 짧게 보존한 문장으로 볼 수 있습니다.',
 'SimCoreTurnSignalsTests.cpp','FSimCorePlayerIndicatorAutoCancelTest::RunTest',244,250);

add(5, 'lesson', '자동 검사와 실제 플레이 검사는 서로 보완합니다',
 ['순수 함수 검사는 경계값·부호·프레임 독립성을 빠르게 확인합니다.', 'UE 자동 검사는 컴포넌트와 에셋 연결 조건도 확인할 수 있습니다.', '실제 플레이는 청감·시야·조작감을 관찰하는 데 필요합니다.'],
 '수학적으로 HUD가 화면 안에 들어간다는 검사와 작은 화면에서 글자가 편하게 읽힌다는 관찰은 서로 다릅니다. 오디오 샘플이 유한한 범위라는 검사도 타이어음이 자연스럽다는 보장을 대신하지 않습니다. 반대로 체감이 좋아졌다는 이유로 좌우 부호나 세션 경계 검사를 생략해서는 안 됩니다. 현재 Unreal 테스트는 IMPLEMENT_SIMPLE_AUTOMATION_TEST와 RunTest를 사용하며 EditorContext 등 실행 조건을 선언합니다. 프로젝트 C++이나 UCLASS 구조를 바꿨다면 현재 에디터가 어떤 바이너리를 쓰는지도 확인해야 합니다. 이 학습에서는 임의로 전체 빌드나 서버 재시작을 하지 않고, 테스트의 준비·검증·실행 맥락을 먼저 읽는 것만으로도 충분한 연습이 됩니다.',
 [src('SimCoreTurnSignalsTests.cpp','FSimCorePlayerIndicatorAutoCancelTest::RunTest',213,250),src('SimCoreInstrumentClusterTests.cpp','BuildLayout',82,119),testRef]);

add(6, 'quiz', '실습 1 — W 입력의 왕복을 빈 종이에 씁니다',
 ['정지·Drive·연결 정상인 상황에서 W를 누른 뒤 여덟 지점을 순서대로 적으세요.', '키 매핑, Pawn 함수, Resolver, 보관, 송신, 수신, 표시 계산, Actor 적용을 포함하세요.', '각 지점에 “의도”인지 “권위 상태”인지 표시하세요.'],
 '먼저 책과 코드를 닫고 자신이 기억하는 흐름을 적습니다. 파일명 전체를 정확히 외우지 못했다면 함수명이나 역할부터 써도 괜찮습니다. 그런 다음 DefaultInput.ini의 Throttle을 시작으로 SetupPlayerInputComponent와 SetThrottle을 찾아 자신의 순서를 교정합니다. 중요한 것은 W가 곧 위치 이동 함수로 이어진다고 쓰지 않는 것입니다. PendingControl은 입력 명령이고 LatestState는 서버에서 검증을 거쳐 받은 결과라는 선을 그으세요. 마지막에는 “이 과정에서 같은 렌더 프레임에 여러 번 일어날 수 있는 일”과 “주기 조건에 따라 일어나는 일”을 하나씩 표시합니다. 완성된 노트는 다음 정답을 보기 전에 사진이나 별도 텍스트로 보관하세요.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::SetThrottle',770,774),src('SimCoreClientComponent.cpp','USimCoreClientComponent::SetControl',124,137)]);

add(6, 'quiz', '실습 2 — 좌표와 바퀴를 손으로 계산합니다',
 ['ENU (3, 7, 0.4)m, 표시 오프셋 (100, −50, 0)cm의 UE 위치는?', '전진 12m/s, 반지름 0.30m일 때 기준 휠 각속도와 초당 회전수는?', '마지막 상태 나이 20ms일 때 단순 수평 외삽 거리는 얼마인가요?'],
 '종이에 각 숫자의 단위를 반드시 붙여서 풉니다. 첫 문제는 축 교환과 미터·센티미터 변환, 마지막 오프셋 더하기 세 단계를 나눠 계산하세요. 두 번째는 v/r 결과가 rad/s라는 사실을 이용하고 초당 회전수는 2π로 나눠 구합니다. 세 번째는 평지에서 속도가 일정하다는 교육용 가정이며, 실제 BuildVehicleSample은 접촉 법선을 이용한 높이 방향 보정과 최대 예측 한도를 포함합니다. 결과 숫자만 맞히기보다 왜 위치에는 오프셋을 더하고 각속도에는 더하지 않는지 말해 보세요. 시간을 ms 그대로 곱하거나 반지름을 cm로 넣었을 때 오차가 몇 배가 되는지도 옆에 적으면 단위 검사의 가치가 분명해집니다.',
 [src('SimCoreCoordinateFrames.cpp','MapEnuPositionMetersToUnrealCentimeters',133,139),src('SimCorePresentation.cpp','BuildVehicleSample',208,215)]);

add(6, 'quiz', '실습 3 — 자동 취소의 반례를 설계합니다',
 ['정지 상태에서 Q를 켜고 A를 길게 눌렀다 놓으면 취소되어야 할까요?', '좌회전 증거가 생긴 뒤 X로 비상등을 켜면 어떤 상태를 정리해야 할까요?', '같은 SimulationTimeNs를 10번 표시하면 회전 증거도 10번 늘어날까요?'],
 '세 질문에 예 또는 아니오만 쓰지 말고 근거가 되는 상태 값을 적습니다. 정지 사례에서는 LinearVelocityBody.X를, 비상등 사례에서는 선택의 의미와 Reset 경로를, 중복 표시 사례에서는 SimulationTimeNs 비교를 찾아야 합니다. 추가 문제로 왼쪽 깜빡이를 켠 뒤 오른쪽으로만 돌고 핸들을 중앙에 놓는 상황을 만들어 보세요. 사용자가 깜빡이를 잘못 켰다고 프로그램이 모든 움직임을 완료 회전으로 세면 안 됩니다. 마지막에는 기존 테스트에 더하고 싶은 반례 하나를 준비 상태·행동·기대 결과 세 문장으로 적습니다. 실제 코드를 수정하거나 테스트를 실행하지 않아도 이 명세 작성만으로 상태기계 학습을 할 수 있습니다.',
 [src('SimCoreTurnSignals.cpp','SimCoreTurnSignals::FAutoCancel::Update',44,104)]);

add(6, 'quiz', '실습 4 — 화면 문제를 담당 경계로 분류합니다',
 ['차체 위치는 맞는데 바퀴만 뜨는 문제는 어디부터 읽을까요?', 'NPC가 우회하지 않는 문제와 NPC 휠이 안 도는 문제를 구분하세요.', 'R 기록, SensorRig, 절차적 오디오의 현재 한계를 한 문장씩 적으세요.'],
 '각 현상에 대해 먼저 볼 파일 하나와 그 파일에서 확인할 숫자 하나를 적습니다. 예를 들어 바퀴가 뜬다면 접촉점, 반지름, 허브의 로컬 변환 중 무엇을 관찰할지 정해야 합니다. NPC 문제에서는 서버 경로 판단과 클라이언트 표시를 나눠야 하며 두 현상을 같은 원인으로 묶지 않습니다. 마지막 세 기능의 한계를 적는 이유는 구현을 낮춰 평가하기 위해서가 아닙니다. 이미 있는 기반과 아직 없는 기능을 정확히 알아야 다음 작업의 입력과 검증 기준을 정할 수 있기 때문입니다. “미완성”이라고만 적지 말고 현재 제공하는 출력과 제공하지 않는 출력을 구체적으로 대비하세요. 다음 해설을 보기 전 자신의 문장을 소리 내어 읽어 보세요.',
 [src('SimCorePresentation.cpp','BuildVehicleSample',98,145),src('SimCoreClientRuntimeEntities.cpp','USimCoreClientComponent::TickRuntimeProxyActors',58,160),src('SimCoreSensorRig.cpp','USimCoreSensorRigComponent::ObserveAuthoritativeState',143,179)]);

add(6, 'answer', '해설 1 — 명령 경로와 상태 경로는 만나는 지점이 다릅니다',
 ['W → Throttle → SetThrottle → PushControl / Resolve → SetControl로 의도를 정리합니다.', 'TickControlTransmission / SendControl 이후 서버가 계산한 WorldState를 받습니다.', 'ApplyBinaryMessage → LatestState → BuildVehicleSample → Actor 적용으로 표시합니다.'],
 '앞부분은 사용자가 원하는 운전 명령을 만드는 경로입니다. DefaultInput의 이름을 SetupPlayerInputComponent가 함수에 연결하고, Resolver는 기어와 실제 진행 방향을 고려해 가속·제동 명령으로 정리합니다. SetControl에서 최신 값을 보관한 뒤 송신 조건이 맞을 때 SendControl이 전송합니다. 뒤의 수신 경로는 결과를 다룹니다. 바이트 파싱과 지도·Play·순서 검사를 통과한 상태가 LatestState가 되고 Pawn Tick에서 표시용 샘플로 바뀝니다. W 입력과 SetActorLocationAndRotation 사이에 서버 권위 계산이 있다는 점이 핵심입니다. 키를 누른 횟수, 렌더 프레임 수, 보낸 명령 수, 받은 상태 수가 항상 같다고 쓰지 않았다면 흐름을 올바르게 구분한 것입니다.',
 [src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::PushControl',850,881),src('SimCoreClientComponent.cpp','USimCoreClientComponent::SendControl',749,766),src('ExternalVehiclePawn.cpp','AExternalVehiclePawn::Tick',210,245)]);

add(6, 'answer', '해설 2 — 단위와 기준을 붙이면 계산이 단순해집니다',
 ['위치: (700, 300, 40)cm + (100, −50, 0)cm = (800, 250, 40)cm입니다.', '각속도: 12 / 0.30 = 40rad/s, 초당 회전수는 약 6.37회입니다.', '단순 외삽 거리: 12 × 0.020 = 0.24m입니다.'],
 '첫 계산은 ENU의 북쪽 값 7이 Unreal X로, 동쪽 값 3이 Y로 이동한 뒤 100배가 됩니다. 표시 오프셋은 이미 센티미터이므로 마지막에 한 번 더하기만 합니다. 두 번째 계산의 반지름은 반드시 미터여야 속도의 미터 단위가 소거됩니다. 40rad/s를 회전수로 바꾸려면 한 바퀴가 2π라디안이라는 관계를 사용합니다. 세 번째의 0.24m는 화면 외삽의 기초 거리이며 서버가 새로 이동했다고 확정한 거리가 아닙니다. 실제 코드에는 예측 시간의 상한과 접촉 평면 보정이 있습니다. 답을 비교할 때 숫자만 보지 말고 자신의 식에 m, cm, s, rad가 어디에서 사라지고 남는지 다시 확인하세요.',
 [src('SimCoreCoordinateFrames.cpp','MapEnuPositionMetersToUnrealCentimeters',133,139),src('SimCorePresentation.cpp','BuildVehicleSample',43,82)]);

add(6, 'answer', '해설 3 — 회전 증거가 없으면 취소하지 않습니다',
 ['정지 조향은 전진 회전의 증거가 아니므로 자동 취소하지 않습니다.', '비상등 선택은 일반 방향 자동 취소의 누적 증거를 정리합니다.', '동일 시뮬레이션 시각의 반복 표시는 새 증거로 세지 않습니다.'],
 '첫 사례는 전진 속도 0.5m/s 조건을 통과하지 못합니다. 핸들이 움직였다는 사실과 차량이 선택 방향으로 회전을 완료했다는 사실은 다릅니다. 두 번째 사례는 비상등을 켠 시점에 일반 좌우 방향 선택과 다른 의미로 바뀌므로 Reset 경로를 탑니다. 세 번째는 같은 패킷을 60fps 화면에서 반복 표시하는 정상 상황이며, SimulationTimeNs가 같으면 추가 누적을 하지 않습니다. 추가 반례인 반대 방향 회전에서는 선택 방향의 조향 증거를 정리하여 잘못된 완료 판단을 막습니다. 좋은 답안은 단순히 “안 꺼집니다”가 아니라 어떤 조건이 충족되지 않았는지 설명합니다. 이런 반례 목록은 구현 전 명세로도, 구현 후 회귀 테스트 목록으로도 사용할 수 있습니다.',
 [src('SimCoreTurnSignals.cpp','SimCoreTurnSignals::FAutoCancel::Update',44,104),src('SimCoreTurnSignalsTests.cpp','FSimCorePlayerIndicatorAutoCancelGuardTest::RunTest',265,287)]);

add(6, 'answer', '해설 4 — 표시 문제와 계산 문제를 분리합니다',
 ['바퀴 위치는 BuildVehicleSample의 접촉점·반지름·로컬 변환을 확인합니다.', 'NPC 우회는 서버 판단, NPC 휠 표시는 ApplySnapshot과 표시 샘플을 확인합니다.', 'Ghost는 상태 재생, SensorRig는 메타데이터, 오디오는 절차적 합성입니다.'],
 '차체의 권위 위치가 정상이라면 바퀴 허브 계산과 피벗 부모를 먼저 보는 것이 합리적입니다. 단, 처음부터 차체 위치가 잘못된 경우에는 지도·좌표·수신 상태를 먼저 확인해야 합니다. NPC 우회는 행동 선택의 문제이므로 클라이언트의 보간만 바꿔 해결할 수 없습니다. 휠만 멈춘 경우에는 반대로 서버 계획기를 처음부터 읽을 필요가 없습니다. 한계 설명의 예는 “R 기록은 받은 차량 상태를 Ghost로 재생하며 서버 물리를 재계산하지 않습니다”, “SensorRig는 장착·시각·ID 메타데이터를 만들며 영상과 점군 취득을 완성하지 않았습니다”, “현재 소리는 상태 기반 합성이며 차종별 실제 녹음을 재현한 완성형 사운드는 아닙니다”입니다.',
 [src('SimCoreNpcPresentationActor.cpp','ASimCoreNpcPresentationActor::ApplySnapshot',223,326),src('SimCoreDriveReplay.cpp','USimCoreDriveReplayComponent::TickComponent',104,113),src('SimCoreSensorRig.cpp','USimCoreSensorRigComponent::ObserveAuthoritativeState',143,179)]);

add(6, 'lesson', '포트폴리오에서는 구현과 근거를 함께 설명합니다',
 ['“외부 권위 상태를 Unreal의 차량·UI·소리로 일관되게 표현했습니다.”', '입력 보관, 연결 세대, 좌표 변환, 표시 시계 중 한 문제를 깊게 설명하세요.', '현재 제공하는 기능과 아직 제공하지 않는 기능을 구분합니다.'],
 '면접에서 파일 개수나 기능 이름을 많이 나열하는 것보다 한 문제를 재현 조건부터 설명하는 편이 이해를 전달하기 좋습니다. 예를 들어 첫 깜빡이 간격이 짧았던 문제는 전역 시간의 위상에서 시작해 선택 상대 시계와 램프·소리 동기화로 설명할 수 있습니다. 경사에서 차체와 바퀴가 달라 보였던 문제는 ENU·FLU·로컬 허브 계산으로 이어집니다. 자신이 이해하고 확인한 부분을 말하고 도구가 작성한 코드를 무조건 스스로 처음부터 설계했다고 과장하지 마세요. 실제 소스를 읽고 실패 조건을 재현하며 수정의 효과와 한계를 설명할 수 있다는 것은 별도의 역량입니다. 마지막으로 지금 설명을 뒷받침하는 함수 하나와 회귀 테스트 하나를 직접 찾아 보여 주세요.',
 [src('SimCoreTurnSignals.cpp','SimCoreTurnSignals::FPhaseClock::Update',10,33),src('SimCoreTurnSignalsTests.cpp','FSimCoreSelectedTurnSignalPhaseTest::RunTest',178,211),{path:'docs/study/lessons/D01-one-day-project-guide.md',symbol:'9-1. 책을 덮고 3분 설명합니다',start:1021,end:1040}]);

add(6, 'table', '복습용 함수 읽기 순서',
 ['한 줄 요약을 먼저 쓰고, 코드에서 근거를 찾아 틀린 부분만 고칩니다.', '전체 파일 완독보다 입력·출력·실패 조건을 정확히 연결하는 것이 우선입니다.'],
 '복습 때마다 처음부터 모든 파일을 읽으면 같은 부분에 시간을 쓰기 쉽습니다. 표의 다섯 묶음 중 자신이 설명하지 못하는 묶음부터 고르세요. 입력 묶음에서는 같은 S 키가 왜 제동과 후진 두 의도로 해석되는지, 통신 묶음에서는 소켓 연결 성공 뒤에도 상태를 거절하는 이유를 답합니다. 좌표와 표현에서는 단위를 손으로 계산하고, 감각 표현에서는 무엇이 실제 권위 상태이며 무엇이 표현 정책인지 나눕니다. 마지막에는 테스트의 한 경계값을 바꾸면 어떤 결과가 나와야 하는지 실행 전에 예측합니다. 이 자료와 로컬 소스만으로 노트를 수정할 수 있다면 AI가 없는 날에도 공부를 계속할 수 있는 기반을 갖춘 것입니다.',
 [src('SimCoreVehicleControlResolver.cpp','Resolve',5,71),src('SimCoreClientComponent.cpp','USimCoreClientComponent::ApplyBinaryMessage',903,1108),src('SimCoreTurnSignalsTests.cpp','FSimCorePlayerIndicatorAutoCancelTest::RunTest',213,250)],
 {columnWidths:[150,320,682],headers:['묶음','첫 함수','이어 읽을 함수'],rows:[['입력','SetThrottle','PushControl / Resolve / SetControl'],['통신','SendControl','ApplyBinaryMessage / GetLatestState'],['좌표·표시','BuildVehicleSample','MapEnuPositionMetersToUnrealCentimeters'],['감각 표현','BuildParameters','FPhaseClock::Update / FAutoCancel::Update'],['검증','RunTest','준비 상태 / TestFalse / TestTrue']]});

export default {id:'03', title:'Unreal 5.6 클라이언트', subtitle:'입력과 화면, 소리를 연결하는 실제 코드 수업', chapters, slides};
