const P = {
  guide: 'docs/study/lessons/D01-one-day-project-guide.md',
  proto: 'protocol/vehicle.proto',
  main: 'cpp/host/src/main.cpp',
  host: 'cpp/host/src/simulation_host.hpp',
  session: 'cpp/host/src/simulation_host_session.cpp',
  physics: 'cpp/host/src/physics/vehicle_physics.cpp',
  ground: 'cpp/host/src/terrain/ground_query.hpp',
  wire: 'cpp/host/src/protocol/vehicle_messages.cpp',
  decoder: 'cpp/host/src/protocol/vehicle_message_decoder.cpp',
  lease: 'cpp/host/src/control/control_lease.hpp',
  socket: 'cpp/host/src/websocket/ws_server.cpp',
  client: 'unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.cpp',
  parser: 'unreal/DriveIntegration/Source/DriveIntegration/SimCoreProtocol.cpp',
};
const repo = (path, symbol, start, end) => ({ path, symbol, start, end });
const web = (url, label) => ({ url, label });
const official = {
  encoding: web('https://protobuf.dev/programming-guides/encoding/', 'Protocol Buffers Encoding'),
  proto3: web('https://protobuf.dev/programming-guides/proto3/', 'Language Guide: proto3'),
  presence: web('https://protobuf.dev/programming-guides/field_presence/', 'Application Note: Field Presence'),
  generated: web('https://protobuf.dev/reference/cpp/cpp-generated/', 'C++ Generated Code Guide'),
  tutorial: web('https://protobuf.dev/getting-started/cpptutorial/', 'Protocol Buffer Basics: C++'),
  threads: web('https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/core/threads.html', 'Threads and Boost.Asio'),
  websocket: web('https://www.boost.org/latest/libs/beast/doc/html/beast/using_websocket.html', 'Boost.Beast WebSocket'),
  cpp: web('https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines', 'C++ Core Guidelines'),
  process: web('https://learn.microsoft.com/en-us/windows/win32/procthread/about-processes-and-threads', 'Microsoft: About Processes and Threads'),
};
const chapters = [
  '01 프로젝트를 읽는 기준', '02 실제 코드로 배우는 C++',
  '03 통신 계층과 메시지 계약', '04 Protobuf 바이트 읽기',
  '05 필드의 의미와 호환성', '06 연결과 제어권의 생명주기',
  '07 왕복 추적과 종합 실습',
];
const slides = [];
const add = (chapter, slide) => slides.push({
  id: `V1-${String(slides.length + 1).padStart(2, '0')}`,
  chapter: chapters[chapter - 1], type: 'lesson', ...slide,
});

add(1, {
  title: '이 교재의 도착점', imageKey: 'architecture',
  body: ['입력 한 개가 서버 계산을 거쳐 화면에 돌아오는 과정을 설명합니다.',
    'C++ 문법은 실제 함수의 입력·결과·실패 조건과 함께 배웁니다.',
    '바이트 해석 성공과 운전 명령 수락의 차이를 구분합니다.'],
  notes: '학습을 마친 뒤에는 W를 누르면 어떤 함수가 호출되는지 외우는 데서 한 걸음 더 나아가야 합니다. 운전 의도와 계산된 차량 상태를 구분하고, 메시지가 올바른 형식이어도 서버가 왜 거부할 수 있는지 설명하는 것이 목표입니다. 첫 번째 읽기에서는 장 끝의 문제를 풀 수 있을 정도로 개념을 파악합니다. 두 번째 읽기에서는 연결된 파일에서 해당 심볼을 찾아 짧은 발췌 전후를 살펴봅니다. 세 번째 읽기에서는 정답을 가리고 자신의 설명을 적습니다. 현재 대상은 Windows의 Unreal 5.6 클라이언트와 별도 C++ 호스트입니다. 모든 C++ 문법이나 Protobuf 기능을 다루지는 않습니다. 실행 환경을 새로 설치하거나 생산 코드를 고치지 않아도 실습할 수 있습니다. 코드 링크의 줄 번호가 달라졌다면 함께 적은 함수나 타입 이름으로 검색하세요. 이해한 내용을 확인할 때는 직접 읽은 사실과 교육용 계산의 가정을 나눠 기록하시면 됩니다.',
  sources: [repo(P.guide, '1. 전체 구조', 112, 180)],
});
add(1, {
  title: '함수 하나를 읽는 순서',
  body: ['반환형에서 결과의 형태를 먼저 확인합니다.', '매개변수와 const를 보고 읽는 값·바꾸는 값을 구분합니다.',
    '실패 반환을 찾은 뒤 상태 저장과 다음 호출을 추적합니다.', '실습: GetLatestState가 false일 때 출력값을 써도 될까요?'],
  notes: '긴 함수를 위에서 아래까지 한 번에 이해하려 하면 이름과 세부 조건에 쉽게 묻힙니다. 먼저 함수가 어떤 약속을 제공하는지 살펴보세요. GetLatestState는 bool을 반환하므로 호출 성공 여부를 알려 줍니다. OutState와 OutStateAgeSeconds는 참조 매개변수이므로 함수가 호출자에게 결과를 써 줄 통로입니다. 함수 끝의 const는 이 호출이 컴포넌트의 일반 멤버 상태를 변경하지 않는다는 제한을 표현합니다. 본문 첫 조건에서 bHasState가 거짓이면 바로 false를 반환합니다. 이때 새 결과를 썼다는 보장이 없으므로 호출자는 이전 출력값을 최신 결과처럼 사용해서는 안 됩니다. 성공 경로에서는 저장한 상태를 복사하고 수신 후 경과 시간을 계산합니다. 여기서 확인할 것은 속도 계산식이 아니라 정보의 소유자와 유효성입니다. 연습 답은 성공 여부를 먼저 확인해야 한다는 것입니다. 다음 장의 참조·포인터·optional을 배울 때도 이 읽기 순서를 그대로 사용합니다.',
  sources: [repo(P.client, 'USimCoreClientComponent::GetLatestState', 139, 149)],
});
add(1, {
  title: '입력과 표시 사이의 왕복',
  body: ['Pawn이 운전 입력을 읽고 ClientComponent가 명령을 보냅니다.',
    'SimulationHost가 입력을 검증한 뒤 물리와 교통 상태를 진행합니다.',
    'WorldState를 받은 Unreal이 차량·신호·HUD를 표시합니다.',
    '질문: 화면용 위치 보간이 서버의 실제 위치를 바꿀까요?'],
  notes: '한 번의 왕복을 설명할 때 모든 통신 호출을 네트워크라고 뭉뚱그리지 마세요. Unreal은 스로틀과 조향 같은 운전 의도를 만들고, 서버는 그 의도를 검증하여 이번 계산에 사용할 입력을 정합니다. 차량이 실제로 얼마나 움직이는지는 서버 물리 상태와 시간 간격에 따라 달라집니다. 서버는 계산 결과를 WorldState에 담아 보내고, 클라이언트는 받은 상태가 현재 지도와 Play에 속하는지 확인합니다. 마지막 표시 단계는 화면을 매끄럽게 보이게 할 수 있지만 그 보간 결과를 곧바로 권한 있는 물리 상태로 취급하지 않습니다. 예를 들어 같은 스로틀을 보냈어도 경사와 접지 상태가 다르면 속도 결과가 달라집니다. 따라서 ControlCommand를 이미 계산된 이동량으로 읽으면 책임 분리가 무너집니다. 연습 답은 화면 보간만으로 서버 상태가 바뀌지 않는다는 것입니다. 실제 변경 통로가 필요하다면 별도의 명령 계약과 수락 조건을 검토해야 합니다.',
  sources: [repo(P.guide, '1-1. 먼저 기억할 왕복 흐름', 114, 141), repo(P.main, 'main', 347, 364)],
});
add(1, {
  type: 'table', title: '누가 무엇을 결정하나요?',
  headers: ['대상', '담당 책임', '대표 근거'],
  rows: [['Unreal', '입력·지면 측정·화면·소리', 'ClientComponent'],
    ['C++ 호스트', '입력 수락·물리·교통 상태', 'SimulationHost'],
    ['MapPackage', '측정한 지면과 충돌 자료', 'GroundQuery'],
    ['스키마', '값의 형태·단위·식별 번호', 'vehicle.proto']],
  body: ['자료를 만드는 책임과 그 자료로 힘을 계산하는 책임을 구분합니다.'],
  notes: '지면은 Unreal에서 측정하는데 접지는 서버가 계산한다는 설명은 서로 충돌하지 않습니다. 지면 측정은 어느 위치에 어떤 높이와 법선이 있는지 자료로 만드는 일입니다. 접지 계산은 그 자료와 바퀴 위치, 서스펜션 상태 등을 사용하여 접촉과 힘을 결정하는 일입니다. GroundHit에는 위치·법선·마찰 배율 같은 정보가 있고 GroundQuery는 아래 방향 질의의 결과를 제공합니다. 이 타입 자체가 타이어 힘을 계산하지는 않습니다. 현재 경로를 매 프레임 서버가 Unreal에 레이를 요청하고 답변을 기다리는 구조로 설명해서는 안 됩니다. MapPackage를 별도 판단 프로세스로 생각하는 것도 오해입니다. 실습으로 카메라 회전과 노면 마찰 변경을 각각 어느 책임에 적을지 생각해 보세요. 카메라 회전은 표시 계층에 속하며, 노면 마찰 자료는 측정 계약과 서버의 힘 계산 사이를 함께 확인해야 합니다. 자료의 의미와 그것을 소비하는 계산을 함께 추적하는 연습입니다.',
  sources: [repo(P.ground, 'GroundHit', 50, 70), repo(P.guide, '1-1. 먼저 기억할 왕복 흐름', 132, 141)],
});
add(1, {
  title: '프로세스와 스레드', imageKey: 'cpp',
  body: ['프로세스는 실행 중인 프로그램의 자원과 주소 공간을 가집니다.',
    '스레드는 프로세스 안에서 실행 순서를 진행하는 단위입니다.',
    '같은 PC의 Unreal과 서버도 메모리 객체를 그대로 공유하지 않습니다.',
    '연습: 서버 주소값을 Unreal에 보내면 그 객체를 읽을 수 있을까요?'],
  notes: '작업 관리자에서 UnrealEditor와 simcore_publisher를 각각 발견했다면 두 실행 주체를 구분할 수 있습니다. 같은 컴퓨터에 있어도 서버의 객체 주소가 곧 Unreal에서 유효한 주소를 뜻하지 않습니다. 그래서 차량 상태를 정해진 형식의 바이트로 바꾸어 통신합니다. 스레드는 한 프로세스 안의 자원을 공유할 수 있어서 통신 비용 없이 같은 객체를 접근할 수 있지만, 동시에 쓰면 별도의 동기화 문제가 생깁니다. 네트워크 클라이언트와 게임 스레드를 같은 말로 부르지 마세요. 클라이언트는 서비스 이용 역할이고 스레드는 실행 단위입니다. 연습 답은 주소값만 전송하여 다른 프로세스의 C++ 객체를 직접 사용할 수 없다는 것입니다. 특히 포인터 크기, 객체 배치, 수명까지 수신자가 같다고 가정하면 위험합니다. 이 프로젝트의 통신 계약은 객체 주소 대신 숫자와 문자열의 의미를 전달합니다. 나중에 디버거를 사용할 때도 어느 프로세스의 어느 스레드를 멈췄는지 먼저 기록하세요.',
  sources: [official.process, repo(P.main, 'main', 299, 311)],
});
add(1, {
  title: '현재 서버의 이벤트 루프',
  body: ['main의 ioc.run()이 비동기 완료 콜백을 실행합니다.',
    '현재 주 통신·시뮬레이션 처리는 하나의 이벤트 루프에서 진행합니다.',
    '지도 로더처럼 별도 작업 경로가 있어도 물리 교체는 틱 경계에서 합니다.',
    '비동기 함수를 썼다고 무거운 계산이 자동으로 병렬화되지는 않습니다.'],
  notes: '비동기는 기다리는 동안 현재 실행 흐름을 붙잡지 않는 호출 방식으로 이해하시면 됩니다. 그 자체가 새 작업 스레드를 만들거나 CPU 계산을 여러 코어로 나눠 준다는 뜻은 아닙니다. 현재 main은 하나의 io_context를 만들고 마지막에 ioc.run을 호출합니다. 통신 완료나 타이머에서 실행하는 사용자 코드가 오래 걸리면 같은 루프에서 대기한 다른 처리도 늦어질 수 있습니다. 그래서 렌더링 FPS는 높은데 서버 입력 반응은 늦는 상황이 가능합니다. 한편 지도 자료는 별도 로더가 준비할 수 있으며 SimulationHost의 선언은 후보를 큐에 넣고 run_tick에서 설치하는 경계를 설명합니다. 이 사실을 서버 전체에 스레드가 하나뿐이라고 일반화하면 안 됩니다. 연습으로 콜백 안에서 500ms 계산을 하면 무슨 일이 생길지 적어 보세요. 다음 완료 처리의 실행 기회가 늦어질 수 있다는 답이 핵심이며, 비동기 소켓이 계산 시간까지 없애 주지는 않습니다.',
  sources: [repo(P.main, 'main', 299, 311), repo(P.main, 'main', 409, 413), repo(P.host, 'queue_map_package_reload', 146, 155), official.threads],
});
add(2, {
  type: 'code', title: '나중에 호출할 동작 연결',
  code: { path: P.main, start: 307, end: 311 },
  body: ['대괄호는 람다가 주변 값을 사용하는 방법을 지정합니다.',
    '콜백 등록 시점과 메시지를 실제 전송하는 시점은 다릅니다.'],
  notes: '307행은 여러 통신 동작을 담을 callbacks 객체를 만듭니다. 308행은 broadcast_world_state 슬롯에 람다를 대입합니다. 캡처 목록의 &는 ws_server와 ioc, physics_replay를 참조로 사용하겠다는 뜻입니다. 매개변수 const std::string& message는 완성된 바이트 문자열을 복사 없이 읽게 합니다. 309행은 실제 호출 시 WebSocket 서버에 방송을 요청합니다. 310행은 기록 조건이 끝났다면 이벤트 루프를 멈춥니다. 마지막 중괄호와 세미콜론에서 람다 정의와 대입이 끝납니다. 이 부분을 읽는 순간 즉시 메시지가 전송되는 것은 아닙니다. 나중에 호스트가 해당 콜백을 호출해야 본문이 실행됩니다. 연습으로 콜백만 등록하고 아무도 호출하지 않는 경우를 생각해 보세요. 전송은 일어나지 않습니다. 또한 참조 캡처한 대상은 콜백 사용 기간 동안 살아 있어야 합니다. 같은 표기를 무심코 지역 변수에 적용하면 수명이 끝난 객체를 참조하는 문제가 생길 수 있습니다.',
  sources: [repo(P.main, 'callbacks.broadcast_world_state', 307, 311), repo(P.host, 'SimulationHostCallbacks', 69, 73)],
});
add(2, {
  type: 'code', title: '헤더 선언과 구현 파일',
  code: { path: P.host, start: 132, end: 137 },
  body: ['헤더는 호출자가 알아야 할 함수의 약속을 보여 줍니다.',
    '실제 검증 순서는 simulation_host_session.cpp의 같은 함수에서 읽습니다.'],
  notes: '132행과 133행은 시작과 정지 함수를 선언합니다. 다음 선언은 ClientMessageResult를 반환하는 handle_client_message입니다. 매개변수 message는 입력 바이트를 가리키는 string_view이며 연결 세대와 수신 시각도 함께 받습니다. 여기에는 검증 본문이 없으므로 호출자는 결과 타입과 매개변수 약속부터 알 수 있습니다. 구현 파일에서는 SimulationHost::handle_client_message 형태로 클래스 소속을 표시하고 실제 파싱과 분기를 정의합니다. 헤더에는 언제나 선언만 있다는 규칙은 아닙니다. 짧은 인라인 함수나 템플릿은 헤더 안에 정의하기도 합니다. 이 프로젝트의 state 함수도 헤더에서 바로 본문을 볼 수 있습니다. 연습으로 선언의 기본 인자를 보고 연결 세대 0이 생산용 표준값이라고 결론 내려도 될지 생각해 보세요. 기본 인자만으로 호출 맥락을 판단할 수 없으며 main이 실제로 넘기는 연결 세대를 함께 읽어야 합니다. 선언은 첫 지도이며 구현과 호출부가 설명을 완성합니다.',
  sources: [repo(P.host, 'SimulationHost::handle_client_message', 132, 142), repo(P.session, 'SimulationHost::handle_client_message', 128, 143)],
});
add(2, {
  type: 'code', title: 'const 참조와 입력 복사',
  code: { path: P.physics, start: 629, end: 637 },
  body: ['const VehicleInput&는 호출자의 입력을 읽는 참조입니다.',
    'input_ = input은 내부 보관용 값을 별도로 복사합니다.'],
  notes: '629행의 input은 함수에 전달한 원래 값에 대한 읽기 전용 접근입니다. 큰 구조체를 매개변수로 다시 복사하지 않으면서 이 경로로 원본을 수정하지 않겠다는 의도를 나타냅니다. 630행의 잠금은 다음 장에서 자세히 봅니다. 631행은 멤버 input_에 현재 입력값을 복사합니다. input과 input_는 이름이 비슷하지만 역할이 다릅니다. 앞은 이번 호출의 인자이고 뒤는 객체가 다음 계산에 사용할 보관 상태입니다. 이후 줄은 유한한 숫자인지 확인하고 스로틀과 브레이크는 0부터 1, 조향은 -1부터 1로 제한합니다. const라고 내부 상태를 전혀 바꾸지 못하는 함수는 아닙니다. 여기의 const는 인자로 받은 참조의 타입에 붙어 있기 때문입니다. 연습으로 입력 원본의 throttle이 2라면 원본과 내부 값이 각각 어떻게 될지 적어 보세요. 이 함수 경로에서 원본은 그대로이고 내부 값은 1로 제한됩니다. 복사 시점과 검증 대상을 구별해야 하는 이유입니다.',
  sources: [repo(P.physics, 'VehiclePhysics::set_input', 629, 637)],
});
add(2, {
  type: 'code', title: '포인터와 없는 출력 통로',
  code: { path: P.decoder, start: 21, end: 26 },
  body: ['error는 오류 문자열을 써 줄 객체를 가리키는 포인터입니다.',
    'if (error)가 nullptr 여부를 확인한 뒤 *error에 값을 씁니다.'],
  notes: '21행의 std::string* error는 문자열 자체가 아니라 문자열이 있는 위치를 받을 수 있는 변수입니다. 호출자가 오류 설명을 원하지 않는 경로에서는 빈 포인터를 전달할 수 있습니다. 23행은 포인터가 비어 있지 않은지 먼저 확인합니다. 24행의 *error는 해당 위치의 문자열 객체를 뜻하며, message를 문자열로 만들어 그 객체에 대입합니다. 포인터를 선언했다고 새 문자열이 자동으로 만들어지는 것은 아닙니다. 또한 포인터가 0이 아니라는 사실만으로 이미 사라진 객체까지 안전해지는 것도 아닙니다. 이 함수는 호출자가 제공한 문자열의 수명이 호출 동안 유효하다는 전제에서 동작합니다. 연습으로 error가 nullptr일 때 어떤 줄이 실행되는지 추적해 보세요. if 본문을 건너뛰고 종료하므로 오류 설명을 기록하지 않습니다. 반대로 *error를 조건 밖으로 옮기면 빈 포인터를 역참조할 수 있습니다. 주소와 값, 그리고 객체를 소유하는 책임은 서로 구분해서 읽으세요.',
  sources: [repo(P.decoder, 'set_error', 21, 26)],
});
add(2, {
  title: 'optional이 표현하는 부재',
  body: ['std::optional<T>는 T 값이 있거나 없는 두 상태를 표현합니다.',
    '지면 높이 0m와 지면을 찾지 못한 결과를 구분할 수 있습니다.',
    'nullopt를 만났을 때의 처리는 호출자가 결정합니다.',
    '질문: 지면이 없으면 무조건 높이 0으로 바꿔도 될까요?'],
  notes: '숫자 하나로 성공과 실패를 동시에 표현하면 의미가 겹치기 쉽습니다. 평지 높이 0m는 정상적인 값인데, 지면을 못 찾은 경우에도 0을 반환하면 호출자는 둘을 구분하지 못합니다. GroundQuery는 optional<GroundHit>를 반환해서 이 차이를 타입에 남깁니다. 값이 있을 때는 접촉 위치와 법선 등을 읽고, 값이 없을 때는 이동 보류나 실패 보고처럼 해당 호출부의 정책을 따릅니다. optional 자체가 안전 정지 동작을 실행하는 것은 아닙니다. 또 값이 존재한다는 사실과 그 값의 물리적 의미가 올바르다는 사실은 별도로 검증할 수 있습니다. 연습 답은 지면 부재를 임의의 평지로 바꾸면 안 된다는 것입니다. 그렇게 처리하면 실제 자료 밖에서도 허공을 지면처럼 사용하거나 차량을 갑자기 다른 높이에 놓을 수 있습니다. std::optional과 다음 장의 Protobuf optional은 모두 존재 여부를 생각하게 하지만 서로 다른 계층의 기능이라는 점도 미리 기억해 두세요.',
  sources: [repo(P.ground, 'GroundQuery::query_down', 65, 70), repo(P.ground, 'FlatGroundQuery::query_down', 83, 102)],
});
add(2, {
  type: 'code', title: '지면 질의의 실패 반환',
  code: { path: P.ground, start: 94, end: 102 },
  body: ['질의 시작 높이에서 평지 높이를 빼 아래 방향 거리를 구합니다.',
    '거리 범위를 벗어나면 nullopt, 범위 안이면 GroundHit를 돌려줍니다.'],
  notes: '94행의 distance는 질의 시작점과 평지 사이의 높이 차이입니다. 95행은 평지가 질의 시작점 위에 있거나 허용한 아래쪽 탐색 거리보다 멀면 실패하도록 검사합니다. 96행의 nullopt는 지면 높이를 0으로 정한다는 뜻이 아니라 결과가 없다는 표현입니다. 99행부터는 성공 결과를 구성합니다. 동쪽과 북쪽 좌표는 요청 위치를 쓰고 높이는 이 평지 객체의 고정 높이를 씁니다. 법선은 위쪽을 가리키는 (0,0,1)이며 마지막 distance는 접촉점까지의 거리입니다. 교육용 계산으로 시작 높이 2m, 평지 0m, 최대 거리 3m라면 거리는 2m여서 성공입니다. 최대 거리가 1m면 같은 평지여도 실패합니다. 이 코드가 모든 실제 도시 지면의 구현은 아닙니다. FlatGroundQuery는 같은 인터페이스를 구현한 평지 사례이며 실제 MapPackage의 높이 자료를 읽는 구현은 별도로 존재합니다. 인터페이스와 구현체를 혼동하지 않는 연습입니다.',
  sources: [repo(P.ground, 'FlatGroundQuery::query_down', 83, 102)],
});
add(2, {
  title: 'mutex와 짧은 임계 구역',
  body: ['mutex는 같은 공유 상태를 동시에 다루는 접근을 조정합니다.',
    '임계 구역은 잠금을 잡고 보호 대상에 접근하는 구간입니다.',
    '잠금을 오래 잡으면 기다리는 쪽의 실행도 늦어질 수 있습니다.',
    '실습: 이번 틱의 입력을 복사한 뒤에도 잠금을 유지해야 할까요?'],
  notes: '두 실행 흐름이 한 구조체를 동시에 읽고 쓰면 스로틀은 새 값인데 기어는 이전 값인 식의 일관성 문제가 생길 수 있습니다. 보호 규칙을 따르는 모든 접근이 같은 mutex를 사용하면 복사하는 동안 다른 쓰기를 기다리게 할 수 있습니다. VehiclePhysics의 입력 읽기는 잠금 안에서 이번 틱의 지역 변수로 짧게 복사하고, 물리 계산은 잠금 밖에서 진행합니다. 이렇게 읽어 온 값은 이후 입력이 바뀌어도 이번 계산에서는 일관된 스냅샷으로 사용합니다. mutex를 추가했다고 프로그램 전체가 자동으로 안전해지는 것은 아닙니다. 같은 멤버를 다른 경로에서 잠금 없이 수정한다면 보호 약속이 깨집니다. 현재 주 이벤트 루프가 단일 스레드라는 사실과 입력 클래스가 잠금 규칙을 제공한다는 사실도 함께 성립할 수 있습니다. 실습 답은 지역 복사를 마친 뒤의 긴 계산에 같은 입력 잠금을 계속 붙잡을 필요가 없다는 것입니다. 잠금 범위와 데이터 수명을 실제 중괄호에서 확인하세요.',
  sources: [repo(P.physics, 'VehiclePhysics::update', 680, 695), official.cpp],
});
add(2, {
  type: 'code', title: 'RAII로 잠금 수명 관리',
  code: { path: P.physics, start: 685, end: 692 },
  body: ['lock_guard 생성 시 잠금을 잡고 블록 종료 시 자동으로 풉니다.',
    'RAII는 자원의 획득과 해제를 객체 수명에 연결하는 방식입니다.'],
  notes: '685행은 이번 물리 계산에서 사용할 입력 변수 in을 만듭니다. 686행의 중괄호는 잠금 객체의 수명을 제한하는 작은 범위입니다. 687행에서 lock_guard가 mutex의 잠금을 획득하고, 688행이 공유 입력을 지역 변수로 복사합니다. 689행에서 범위를 벗어나면 lock 객체의 소멸자가 잠금을 해제합니다. 이후 691행과 692행은 잠금 밖에서 수행합니다. 이 예제에서는 unlock을 직접 적지 않아도 해제 지점이 코드 구조에 드러납니다. 정상 종료뿐 아니라 예외로 범위를 벗어날 때도 자동 객체 소멸 규칙이 중요합니다. 다만 강제 프로세스 종료 등 모든 상황의 복구까지 보장한다는 뜻은 아닙니다. 연습으로 닫는 중괄호를 함수 맨 아래로 옮겼다고 가정해 보세요. 결과 계산은 같아 보여도 잠금을 보유하는 시간이 크게 늘어납니다. 문법 한 줄보다 범위 전체를 읽어야 성능과 동시성 의도를 이해할 수 있습니다.',
  sources: [repo(P.physics, 'VehiclePhysics::update', 685, 692), official.cpp],
});
add(2, {
  title: '뷰와 소유 객체의 수명',
  body: ['string_view와 TArrayView는 기존 메모리를 바라보는 범위입니다.',
    '뷰를 복사해도 원본 바이트가 새로 복사되지는 않습니다.',
    'unique_ptr는 소유권, 참조 캡처는 접근 경로를 표현합니다.',
    '질문: 읽기 전용 뷰를 보관하면 원본 버퍼도 자동으로 살아 있을까요?'],
  notes: '읽기 전용이라는 말은 수정 권한에 관한 것이고, 언제까지 객체가 살아 있는지는 수명에 관한 문제입니다. FReader는 전달받은 TArrayView를 Data에 보관하고 Offset으로 읽은 위치를 추적합니다. 이 뷰는 바이트 소유 컨테이너가 아니므로 파싱하는 동안 원본 데이터가 유효해야 합니다. 서버의 string_view 입력도 같은 관점으로 읽으시면 됩니다. 반면 main의 unique_ptr<WsServer>는 서버 객체의 소유권을 표현합니다. shared_from_this를 캡처하는 비동기 세션 코드는 완료 콜백이 끝날 때까지 세션 객체를 유지하려는 별도의 수명 설계입니다. 모든 포인터가 소유 포인터라는 해석은 오해입니다. 연습 답은 뷰를 보관한다고 원본 수명이 자동 연장되지 않는다는 것입니다. 함수가 끝난 뒤에도 쓸 자료라면 누가 원본을 소유하며 어떤 종료 시점까지 보장하는지 찾아야 합니다. const, nullptr 검사, 잠금이 모두 있어도 수명 약속을 빠뜨리면 안전하지 않을 수 있습니다.',
  sources: [repo(P.parser, 'FReader', 50, 55), repo(P.parser, 'FReader::Data', 140, 142), repo(P.main, 'ws_server', 299, 302), repo(P.socket, 'WsSession::do_read', 133, 140)],
});
add(2, {
  type: 'quiz', title: 'C++ 읽기 연습',
  body: ['A. const VehicleInput&를 받으면 함수의 모든 멤버가 불변인가요?',
    'B. optional 지면 결과가 비었을 때 높이 0m로 처리해도 될까요?',
    'C. lock_guard가 있는 작은 블록은 어디서 잠금을 해제하나요?',
    'D. 람다에 참조로 잡은 지역 객체가 먼저 사라지면 안전한가요?'],
  notes: '정답을 바로 보지 말고 각 문항에 대해 코드의 어느 표기를 근거로 삼았는지 함께 적어 보세요. A에서는 const가 매개변수 타입에 붙었는지 멤버 함수 끝에 붙었는지를 구분합니다. B에서는 0이라는 값 자체가 정상 자료일 수 있다는 사실을 떠올립니다. C에서는 lock_guard를 선언한 중괄호 범위를 손가락으로 짚고, 그 안에서 실제로 어떤 공유 상태를 다루는지 표시합니다. D에서는 참조와 소유권이 같은 말인지 다시 생각해 봅니다. 답을 한 단어로만 쓰면 우연히 맞힐 수 있으므로 결과와 이유를 나눠 두 문장으로 설명하는 것이 좋습니다. 실행이나 코드 편집은 필요 없습니다. 모르는 용어가 나오면 지금은 해당 슬라이드의 실제 심볼만 다시 찾으세요. 이 연습의 목적은 문법 사전을 외우는 것이 아니라 반환 실패와 객체 수명 때문에 생길 수 있는 오류를 예측하는 것입니다. 네 답 모두 맞더라도 모든 C++ 동시성 문제를 이해했다고 확대 해석하지 않습니다.',
  sources: [repo(P.physics, 'VehiclePhysics::set_input', 629, 637), repo(P.ground, 'FlatGroundQuery::query_down', 94, 102), repo(P.main, 'callbacks.broadcast_world_state', 307, 311)],
});
add(2, {
  type: 'answer', title: 'C++ 읽기 연습 해설',
  body: ['A. 해당 참조를 통한 원본 수정만 제한합니다. 내부 input_는 바뀝니다.',
    'B. 부재와 0m 지면은 다릅니다. 호출자의 실패 정책을 따라야 합니다.',
    'C. lock 객체의 블록이 끝날 때 잠금을 풉니다.',
    'D. 참조는 수명을 연장하지 않습니다. 캡처 대상의 생존을 확인해야 합니다.'],
  notes: 'A의 근거는 input_ = input이라는 실제 대입입니다. const 매개변수를 받아도 함수는 자신이 소유한 다른 상태를 변경할 수 있습니다. B의 근거는 성공 경로에서 높이 0m를 가진 GroundHit도 반환할 수 있다는 점입니다. nullopt를 같은 높이로 대체하면 실패 정보가 사라집니다. C는 RAII 객체의 소멸 시점에 관한 질문입니다. 복사 직후 잠금 범위를 끝내므로 그 다음 계산에서 입력 잠금을 계속 차지하지 않습니다. D는 람다 문법보다 수명 설계에 관한 질문입니다. 콜백 등록이 성공해도 실제 호출 전에 캡처 대상이 사라지면 접근이 유효하지 않을 수 있습니다. 자신의 답에 함수 이름과 줄의 역할이 하나씩 들어 있으면 충분합니다. 답을 틀렸다면 전체 C++ 책으로 돌아갈 필요 없이 const가 붙은 위치, optional의 성공·실패 반환, 잠금 블록 경계 중 틀린 부분만 다시 읽으세요. 이후 Protobuf 파서에서 등장하는 참조 출력과 바이트 뷰를 같은 기준으로 해석해 보시면 이해가 이어집니다.',
  sources: [repo(P.physics, 'VehiclePhysics::update', 685, 692), repo(P.parser, 'FReader', 50, 55)],
});
add(3, {
  type: 'table', title: 'TCP·WebSocket·Protobuf',
  headers: ['계층', '담당 질문', '이 프로젝트의 예'],
  rows: [['TCP', '바이트 흐름을 어떻게 전달하나요?', '로컬 소켓 연결'],
    ['WebSocket', '어디까지 한 메시지인가요?', 'binary 메시지'],
    ['Protobuf', '바이트는 어떤 값인가요?', 'ControlCommand'],
    ['애플리케이션', '이 명령을 받아도 되나요?', '세션·지도·제어권 검사']],
  body: ['통로, 메시지 경계, 데이터 해석, 수락 정책을 각각 구분합니다.'],
  notes: '프로토콜이라는 단어가 여러 층에 사용되므로 어떤 층을 말하는지 먼저 정하면 설명이 쉬워집니다. TCP는 순서 있는 바이트 흐름을 제공하고 WebSocket은 그 위에서 메시지 단위의 양방향 통신을 제공합니다. Protobuf는 메시지 본문의 바이트가 스로틀인지 세션 문자열인지 해석할 형식입니다. 마지막으로 이 프로젝트는 형식이 맞는 입력에도 지도와 제어권 검사를 적용합니다. 예를 들어 valid한 ControlCommand라도 이전 Play의 연결에서 온 평상시 운전 입력이면 거부할 수 있습니다. Protobuf를 선택했다고 WebSocket 서버가 자동으로 생기거나 오래된 입력을 자동으로 거부하지는 않습니다. 연습으로 연결은 성공했는데 schema 오류가 난 경우를 어느 층부터 볼지 적어 보세요. 통로 개설 이후의 데이터 계약과 애플리케이션 협상을 먼저 확인해야 합니다. 반대로 포트에 아무 서버도 없다면 필드 번호부터 바꾸는 것은 맞는 진단 순서가 아닙니다. 같은 통신 오류처럼 보여도 원인의 층을 나눠 봅니다.',
  sources: [official.websocket, repo(P.socket, 'WsSession::do_read', 133, 172), repo(P.decoder, 'parse_client_message_envelope', 39, 52)],
});
add(3, {
  title: '메시지 경계와 분할 수신',
  body: ['한 번의 소켓 콜백이 언제나 전체 의미 메시지라고 가정하지 않습니다.',
    'UE는 들어온 조각을 모으고 마지막 조각에서 완성 메시지를 처리합니다.',
    '현재 수신 크기 한도는 1MiB이며 초과 시 메시지를 버립니다.',
    '분할 전송과 protobuf의 LEN 필드는 서로 다른 경계입니다.'],
  notes: '큰 상태 메시지를 받는 동안 라이브러리는 데이터를 조각으로 전달할 수 있습니다. ClientComponent의 ApplyBinaryMessage에는 Fragment와 bIsLastFragment가 있으므로 이 코드는 메시지 조립을 명시적으로 다룹니다. 현재 누적 크기와 새 조각 크기를 비교하고, 한도를 넘으면 수신 내용을 지우며 남은 조각도 폐기하는 경로가 있습니다. 한편 Protobuf의 길이 접두사는 이미 완성된 바이트 본문 안에서 문자열이나 중첩 메시지의 범위를 정합니다. 네트워크 조각의 마지막과 중첩 필드의 마지막을 섞어 읽으면 파서가 아직 오지 않은 바이트를 잘못된 데이터로 판단할 수 있습니다. 연습으로 300바이트짜리 의미 메시지가 100바이트와 200바이트로 전달될 때 첫 조각만 즉시 전체 Envelope로 해석하면 어떤 결과가 날지 생각해 보세요. 필드가 끝나지 않아 실패할 수 있습니다. 실제 분할 방식과 횟수는 전송 계층 상황에 따라 달라지므로 이 숫자는 계산용 예입니다. 크기 한도도 메시지 의미 검증을 대신하지 않습니다.',
  sources: [repo(P.client, 'USimCoreClientComponent::ApplyBinaryMessage', 906, 937), repo('unreal/DriveIntegration/Source/DriveIntegration/SimCoreClientComponent.h', 'MaxIncomingMessageBytes', 144, 144)],
});
add(3, {
  title: '직렬화와 역직렬화', imageKey: 'protobuf',
  body: ['직렬화는 의미 있는 값을 약속된 바이트 표현으로 바꿉니다.',
    '역직렬화는 그 바이트를 읽어 메시지 값으로 복원합니다.',
    '현재 서버는 생성 클래스로 쓰고 UE는 자체 파서로 읽습니다.',
    '실습: 메모리의 C++ 구조체를 그대로 복사해 보내면 왜 위험할까요?'],
  notes: '직렬화 전에는 VehicleState나 생성된 Envelope처럼 프로그램이 다룰 수 있는 객체가 있습니다. 전송 시에는 그 객체의 뜻을 상대 프로그램도 해석할 수 있는 바이트 규칙으로 바꿉니다. 수신자는 같은 계약에 따라 읽어 필드 값을 복원합니다. 이 과정에서 서버의 C++ 객체와 Unreal의 상태 구조체가 같은 메모리 배치를 가질 필요는 없습니다. 예를 들어 문자열 객체 안에는 포인터와 길이 같은 구현 정보가 있을 수 있으므로 구조체 메모리를 통째로 복사하면 글자 내용 대신 의미 없는 주소까지 전달할 수 있습니다. 정렬 여백이나 타입 크기 차이도 문제가 됩니다. 현재 serializer는 set_speed 같은 필드 접근으로 값을 채우고 SerializeAsString으로 바이트를 만듭니다. UE는 필드 번호와 wire type을 읽어 자신이 가진 구조체에 값을 넣습니다. 연습 답은 객체의 메모리 배치와 외부 데이터 계약을 구분해야 한다는 것입니다. 복원이 성공해도 값의 범위나 세션 유효성을 별도로 확인하는 이유는 뒤에서 이어집니다.',
  sources: [repo(P.wire, 'fill_entity_state', 66, 75), repo(P.wire, 'serialize_world_state_envelope', 594, 599), repo(P.parser, 'FReader::ReadTag', 73, 82), official.tutorial],
});

add(3, {
  type: 'code', title: 'IDL로 정의한 데이터 형태',
  code: { path: P.proto, start: 46, end: 50 },
  body: ['IDL은 서로 다른 프로그램이 공유할 데이터 형태를 적는 언어입니다.',
    'Vector3d의 x·y·z는 double이고 식별 번호는 각각 1·2·3입니다.'],
  notes: '이 프로젝트는 .proto 파일을 데이터 계약의 출발점으로 사용합니다. 46행은 Vector3d라는 메시지 타입을 선언합니다. 47행부터 49행은 세 좌표 성분의 타입과 필드 번호를 정합니다. x라는 이름은 사람이 의미를 읽고 생성된 API를 사용할 때 중요하고, 오른쪽 숫자는 바이너리에서 어느 필드인지 식별하는 데 중요합니다. 50행은 메시지 선언을 닫습니다. 같은 Vector3d라도 position_enu에서 사용할 때는 미터 단위 위치이고 angular_velocity_body에서 사용할 때는 rad/s 단위 각속도입니다. 타입 구조가 같다고 물리 의미까지 같지는 않습니다. 따라서 선언을 읽을 때 사용하는 필드의 주석도 함께 봐야 합니다. 연습으로 x가 double이라는 이유만으로 값의 단위를 추론할 수 있는지 답해 보세요. 정확한 단위는 해당 필드의 계약을 확인해야 합니다. IDL은 값의 모양을 공유하는 도구이며 차량의 다음 상태를 계산하는 물리 함수나 전송 주기를 정하는 타이머를 대신하지 않습니다.',
  sources: [repo(P.proto, 'Vector3d', 46, 50), repo(P.proto, 'EntityState', 120, 123), official.proto3],
});
add(3, {
  type: 'code', title: 'protoc와 생성 코드',
  code: { path: 'cpp/host/CMakeLists.txt', start: 39, end: 45 },
  body: ['protoc는 .proto 선언에서 언어별 메시지 코드를 생성합니다.',
    '현재 CMake는 C++ 결과를 빌드 폴더에 만들도록 설정합니다.'],
  notes: '39행의 protobuf_generate는 빌드 구성에 코드 생성 단계를 등록합니다. TARGET은 생성 결과를 사용할 simcore_protocol을 가리키고 PROTOS는 입력 스키마를 지정합니다. LANGUAGE cpp는 C++ 생성을 선택하며 IMPORT_DIRS는 다른 스키마를 찾는 기준 경로입니다. PROTOC_OUT_DIR는 결과를 현재 빌드 디렉터리에 둡니다. 생성된 파일에서 set_speed나 has_gear 같은 메시지 API를 얻습니다. 생성 코드를 직접 수정하면 다음 생성 때 편집 내용이 사라질 수 있으며, 원본 스키마와 구현의 관계도 흐려집니다. 그렇다고 이 페이지를 읽은 뒤 곧바로 스키마를 바꾸거나 전체 빌드를 실행할 필요는 없습니다. 연습으로 gear 필드가 바뀌었을 때 C++ 생성 파일만 고쳐도 충분한지 생각해 보세요. 원본 스키마와 송수신 소비자를 함께 검토해야 하며 UE의 자체 파서도 별도 확인 대상입니다. protoc 버전과 생성 코드에 맞는 런타임 사용 역시 빌드 환경의 책임입니다. 최신 문서의 기능이 현재 스키마에 모두 켜졌다는 뜻은 아닙니다.',
  sources: [repo('cpp/host/CMakeLists.txt', 'protobuf_generate', 19, 45), official.tutorial, official.generated],
});
add(3, {
  type: 'table', title: '생성 클래스와 UE 자체 파서',
  headers: ['구분', '현재 서버', '현재 Unreal'],
  rows: [['읽고 쓰는 방법', '생성된 C++ 클래스', 'SimCoreProtocol 수동 구현'],
    ['바이트 해석', 'ParseFromArray', 'FReader와 필드별 분기'],
    ['상태 저장', 'VehicleState 등', 'FVehicleState 등'],
    ['확장 시 확인', '생성·변환·검증 코드', '해석·검증·표시 코드']],
  body: ['스키마를 바꾼 뒤 양쪽이 자동으로 같은 기능을 갖추지는 않습니다.'],
  notes: '서버는 protobuf 라이브러리의 생성 클래스를 사용하므로 메시지 구조를 다루는 기본 API를 자동으로 얻습니다. Unreal 쪽은 현재 필요한 바이너리 범위를 SimCoreProtocol.cpp에서 직접 읽고 씁니다. 이 차이 때문에 서버에서 새 필드를 출력하는 것과 Unreal 화면에서 새 기능이 보이는 것은 별도의 작업입니다. 수동 파서가 알 수 없는 필드를 안전하게 건너뛸 수 있어도 그 값에 연결된 표시 기능을 자동 생성하지는 않습니다. 반대로 모든 새 필드를 필수로 취급하면 기존 송신자와의 호환성이 깨질 수 있습니다. 연습으로 NPC에 새로운 진단 불리언을 덧붙인 상황을 생각해 보세요. 기존 클라이언트가 모르는 필드를 건너뛰어 계속 운전하는 것과, 새 진단 값을 HUD에 표시하는 것은 서로 다른 완료 기준입니다. 현재 field 40인 npc_local_bypass_active가 그런 추가 메타데이터 사례입니다. 이 교재는 UE 파서가 Protobuf의 모든 언어 기능을 완전히 구현했다고 주장하지 않습니다. 실제 지원 범위는 분기와 검사를 읽어 확인합니다.',
  sources: [repo(P.decoder, 'parse_client_message_envelope', 30, 43), repo(P.parser, 'FReader', 50, 143), repo(P.proto, 'EntityState::npc_local_bypass_active', 146, 151)],
});
add(3, {
  type: 'code', title: 'Envelope의 공통 정보',
  code: { path: P.proto, start: 277, end: 280 },
  body: ['Envelope는 payload와 함께 순서·출처·세계 식별 정보를 보냅니다.',
    '여기에 적힌 2·3·4·5는 저장한 값이 아닌 필드 번호입니다.'],
  notes: '이 발췌는 Envelope 선언의 일부입니다. 277행은 sequence라는 uint64 필드를 2번으로 정의합니다. 실제 메시지의 순번은 2로 고정되지 않으며 100이나 101 같은 값을 담을 수 있습니다. 278행은 서버 시뮬레이션 시간, 279행은 송신 출처 문자열, 280행은 지도 패키지 식별 문자열입니다. 선언 앞에는 schema_version이 있고 뒤에는 연결 세션과 Play 식별자, payload 선택이 이어집니다. Envelope를 겉봉투로 비유할 수 있지만 우편 주소나 보안 인증 전체를 제공한다고 확대해서 이해하면 안 됩니다. source_id 같은 문자열은 프로젝트가 정한 식별 정보이며 암호학적으로 사용자를 인증하는 자격 증명이 아닙니다. 연습으로 sequence = 2가 메시지 두 개를 전송한다는 뜻인지 설명해 보세요. 답은 2번 필드에 순번 값을 담는 선언이라는 것입니다. 실제 순번이 어떻게 증가하고 수락되는지는 송신 함수와 세션 검증에서 읽습니다. 데이터 모양과 상태 변화 규칙을 분리하는 연습입니다.',
  sources: [repo(P.proto, 'Envelope', 275, 290)],
});
add(3, {
  type: 'table', title: '운전 요청과 계산 결과',
  headers: ['내용', 'ControlCommand', 'EntityState'],
  rows: [['스로틀', '0~1 운전 요청', '차량 상태를 직접 확정하지 않음'],
    ['조향', '-1~1 요청', 'steering_angle은 rad'],
    ['속도', '목표 속도 필드 없음', 'speed는 m/s'],
    ['위치', '위치 직접 지정 없음', 'position_enu는 m']],
  body: ['같은 이름처럼 보여도 입력 요청과 측정·계산 결과는 의미가 다릅니다.'],
  notes: 'ControlCommand의 steering과 EntityState의 steering_angle을 혼동하면 입력을 두 번 적용하거나 각도 단위를 잘못 읽을 수 있습니다. 전자는 -1부터 1 사이의 운전 요청이며 후자는 물리 계산에서 사용하는 실제 바퀴 조향각입니다. 현재 수동 운전 명령은 차량 위치나 속도를 직접 지정하지 않습니다. 스로틀을 크게 보내면 어느 속도로 반드시 이동한다는 약속도 없습니다. 서버가 질량과 기어, 마찰과 접지 조건을 사용해 다음 상태를 구합니다. 연습으로 steering이 0.5인 메시지를 받았다고 차체 heading에 0.5도를 더하면 되는지 판단해 보세요. 이 값은 차체 각도 변화량이 아니므로 그렇게 처리할 수 없습니다. 바퀴 조향과 차량 진행 방향 변화도 별도의 물리 관계를 가집니다. 이 표의 목적은 자동차 모델을 유도하는 것이 아니라 통신 필드의 역할을 정확히 읽는 것입니다. 물리 수식은 다른 권에서 자세히 다루며 여기서는 값의 출발점과 소비자를 연결합니다.',
  sources: [repo(P.proto, 'ControlCommand', 234, 243), repo(P.proto, 'EntityState', 106, 123)],
});
add(3, {
  type: 'table', title: '타입과 단위를 함께 읽기',
  headers: ['필드', '저장 타입', '의미·단위'],
  rows: [['speed', 'float', 'm/s'], ['heading', 'float', '북쪽 0°, 시계 방향 양수'],
    ['yaw_rate', 'float', 'rad/s, 왼쪽 양수'], ['position_enu', 'Vector3d', '동·북·위 위치, m'],
    ['simulation_time_ns', 'uint64', '서버 계산 시간, ns']],
  body: ['타입은 저장 형태를, 주석과 계약은 물리 의미를 설명합니다.'],
  notes: 'float라는 타입만 보고 어떤 값이 각도인지 속도인지 알 수 없습니다. 이 프로젝트는 heading을 도 단위 항법 방향각으로 보내지만 yaw_rate는 rad/s 단위의 차체 각속도로 보냅니다. 방향각의 시계 방향 양수와 차체 좌표의 왼쪽 양수도 같은 부호 규칙이 아닙니다. 연결 경계에서 단위를 한 번 더 바꾸면 수치가 57배 정도 달라지는 오류가 생길 수 있습니다. 길이 역시 서버의 미터와 Unreal의 센티미터를 구분해야 합니다. 연습으로 speed가 10일 때 HUD에 10km/h라고 쓰는 것이 맞는지 계산해 보세요. 10m/s는 36km/h이므로 표시용 변환이 필요합니다. 이 변환은 통신 필드의 의미를 바꾸지 않고 표현만 바꾸는 예입니다. 같은 메시지에 double과 float가 섞여 있는 이유를 모두 성능 최적화라고 단정하지 마세요. 현재 계약에 명시된 정확도와 의미를 확인하고 필요할 때 변경 영향을 검토하는 것이 학습의 출발점입니다.',
  sources: [repo(P.proto, 'EntityState', 101, 123), repo(P.proto, 'Envelope', 275, 282)],
});
add(4, {
  title: '필드 태그의 계산',
  body: ['태그 정수 = (필드 번호 × 8) + wire type입니다.',
    '스로틀은 2번 float이므로 태그는 2×8+5 = 21, 16진수 15입니다.',
    '21번 wheels는 LEN이므로 태그 정수는 170입니다.',
    '연습: Envelope의 sequence는 몇 번 필드이며 태그는 얼마인가요?'],
  notes: '바이트를 읽을 때 첫 값은 보통 그 필드의 종류를 설명하는 태그입니다. WriteTag의 실제 코드는 필드 번호를 왼쪽으로 세 비트 옮기고 아래 세 비트에 wire type을 넣습니다. 정수 계산으로는 필드 번호에 8을 곱한 뒤 타입 번호를 합친 것과 같습니다. ControlCommand의 throttle은 field 2이고 float의 wire type은 5이므로 태그 정수 21을 얻습니다. 이를 16진수로 적으면 15입니다. 여기서 decimal 21과 wheels의 field number 21을 섞지 마세요. 서로 다른 계산 맥락입니다. wheels는 메시지 목록이므로 wire type 2를 쓰고 태그 정수는 170입니다. 170은 한 바이트의 7비트로 담을 수 없어 뒤에서 배울 varint 두 바이트가 됩니다. 연습 답은 sequence가 field 2의 uint64이므로 wire type 0, 태그 정수 16, 16진수 10이라는 것입니다. 태그 자체도 varint라는 사실이 다음 계산과 연결됩니다.',
  sources: [repo(P.parser, 'WriteTag', 19, 22), repo(P.proto, 'ControlCommand', 234, 242), repo(P.proto, 'Envelope', 275, 282), official.encoding],
});
add(4, {
  type: 'code', title: '태그에서 필드와 타입 분리',
  code: { path: P.parser, start: 73, end: 82 },
  body: ['Tag >> 3은 필드 번호, Tag & 7은 아래 세 비트의 wire type입니다.',
    '0번 필드와 범위를 넘는 필드 번호는 수신기가 거부합니다.'],
  notes: '73행은 Field와 WireType에 결과를 써 주는 함수입니다. 75행에서 태그를 받을 64비트 정수를 준비하고, 다음 행에서 varint 읽기 실패나 0 태그를 거부합니다. 77행은 세 비트를 오른쪽으로 이동하여 필드 번호 부분을 얻습니다. 78행은 필드 번호가 유효한 범위를 벗어나지 않는지 확인합니다. 79행과 80행은 분리한 결과를 참조 매개변수에 저장하고, 마지막 true가 성공을 알립니다. 이 함수는 태그를 분해할 뿐 해당 필드가 현재 메시지에서 허용되는지까지 판단하지 않습니다. 그 검사는 ParseWheel 같은 상위 파서가 필드 번호별로 수행합니다. 손계산으로 Tag가 21이면 21을 8로 나눈 몫은 2이고 나머지는 5입니다. 따라서 field 2의 고정 32비트 값으로 읽을 준비를 합니다. 연습으로 Tag가 0이면 이 함수가 어떤 경로로 끝나는지 추적해 보세요. 유효하지 않은 태그를 정상 기본값으로 처리하지 않고 false를 반환하는 것이 핵심입니다.',
  sources: [repo(P.parser, 'FReader::ReadTag', 73, 82)],
});
add(4, {
  type: 'table', title: '현재 사용하는 wire type',
  headers: ['번호', '이름', '읽을 바이트', '프로젝트 사례'],
  rows: [['0', 'VARINT', '끝 표시까지 7비트씩', 'sequence·bool·enum'],
    ['1', 'I64', '정확히 8바이트', 'Vector3d의 double'],
    ['2', 'LEN', '길이 값 뒤 지정 길이', '문자열·중첩 메시지'],
    ['5', 'I32', '정확히 4바이트', 'throttle의 float']],
  body: ['wire type은 저장 구조를 알려 줍니다. 물리 단위까지 알려 주지는 않습니다.'],
  notes: '이 표는 현재 FReader의 Skip과 숫자 읽기 함수가 직접 처리하는 종류를 중심으로 정리했습니다. 같은 VARINT라도 sequence, bool, enum은 의미가 다르므로 상위 필드 계약을 확인해야 합니다. LEN도 문자열과 중첩 메시지를 모두 담을 수 있어서 길이만 읽고 내용의 의미를 알 수는 없습니다. I32라는 표기에서 32는 바이트 수가 아니라 비트 수이며 실제 값 영역은 4바이트입니다. double은 I64를 사용하여 값 영역이 8바이트입니다. Protobuf 전체 역사에는 group 타입도 있지만 현재 UE의 Skip은 0·1·2·5 외의 타입을 처리하지 않고 실패합니다. 따라서 이 표를 모든 규격 기능의 지원 선언으로 읽지 마세요. 연습으로 float 필드에서 wire type 0을 받았을 때 숫자 변환으로 대충 맞춰도 되는지 생각해 보세요. 현재 필드 파서는 기대한 wire type과 다르면 실패하도록 작성되어 있습니다. 값의 크기가 비슷해 보여도 해석 규칙을 바꾸면 뒤 필드 경계까지 틀어질 수 있습니다.',
  sources: [repo(P.parser, 'FReader::ReadFixed32', 84, 109), repo(P.parser, 'FReader::Skip', 128, 138), official.encoding],
});
add(4, {
  title: 'varint 150 손계산',
  body: ['150 = 22 + 1×128입니다. 낮은 7비트부터 나눕니다.',
    '첫 조각 22에 계속 표시 128을 더하면 150, 16진수 96입니다.',
    '마지막 조각 1은 계속 표시 없이 01입니다. 결과는 96 01입니다.',
    '복원: (96의 아래 7비트) + (01×128) = 22+128 = 150입니다.'],
  notes: 'varint는 값의 크기에 따라 사용하는 바이트 수가 달라지는 정수 표현입니다. 각 바이트의 아래 7비트에 숫자 조각을 넣고 가장 높은 비트로 뒤에 조각이 더 있는지 표시합니다. 교육용 값 150을 128로 나누면 나머지 22와 몫 1을 얻습니다. 낮은 조각 22에는 다음 조각이 있다는 표시를 붙여 0x96을 만들고, 마지막 조각은 0x01입니다. 이 계산은 현재 WriteVarint의 반복 조건과 오른쪽 7비트 이동을 손으로 실행한 결과입니다. 96을 십진수 96으로 읽지 말고 이 페이지의 두 자리 값은 16진수라는 점을 확인하세요. 0x96에서 높은 비트를 제거하면 0x16, 즉 십진수 22가 됩니다. 연습으로 127과 128을 비교해 보세요. 127은 7비트에 들어가므로 7F 한 바이트이고, 128은 80 01 두 바이트입니다. Protobuf 값 전체가 항상 이 방식은 아니며 float와 double은 앞의 표처럼 고정 길이를 사용합니다.',
  sources: [repo(P.parser, 'WriteVarint', 9, 17), repo(P.parser, 'FReader::ReadVarint', 57, 71), official.encoding],
});
add(4, {
  type: 'code', title: 'varint를 쓰는 반복문',
  code: { path: P.parser, start: 9, end: 17 },
  body: ['128 이상이면 아래 조각을 내보내고 값을 7비트 줄입니다.',
    '마지막 조각에는 다음 바이트가 있다는 표시를 붙이지 않습니다.'],
  notes: '9행은 출력 바이트 배열과 uint64 값을 받습니다. 출력은 참조이므로 함수가 배열 끝에 바이트를 추가할 수 있습니다. 11행은 값이 0x80 이상인지 확인합니다. 13행은 현재 낮은 부분을 한 바이트로 만들고 계속 비트 0x80을 설정합니다. 14행은 이미 쓴 아래 7비트를 제거합니다. 값이 128보다 작아지면 반복문을 끝내고 16행에서 마지막 바이트를 추가합니다. 입력 150이라면 반복문은 한 번 실행하며 첫 바이트 96을 씁니다. 이동한 값 1은 반복 조건을 만족하지 않으므로 마지막 01을 씁니다. 입력 0은 반복문을 건너뛰지만 마지막 00은 추가합니다. 다만 메시지 serializer가 기본값 필드 자체를 생략할 수 있는지는 이 함수 바깥의 결정입니다. 연습으로 Value가 300일 때 낮은 조각과 다음 값을 계산해 보세요. 44와 2이므로 바이트는 AC 02입니다. 함수가 숫자 필드의 의미까지 알지 못한다는 점도 함께 확인합니다.',
  sources: [repo(P.parser, 'WriteVarint', 9, 17)],
});
add(4, {
  type: 'code', title: 'varint의 범위 검사',
  code: { path: P.parser, start: 57, end: 68 },
  body: ['읽는 위치와 최대 64비트 범위를 함께 제한합니다.',
    '각 조각을 합친 뒤 계속 비트가 0이면 읽기를 완료합니다.'],
  notes: '57행은 결과를 참조 인자 Out에 저장합니다. 59행에서 이전 값을 지운 뒤 60행은 7비트씩 이동하면서 입력 배열 끝을 넘지 않게 제한합니다. 62행은 현재 바이트를 가져오고 Offset을 하나 증가시킵니다. 63행의 검사는 마지막 64비트 경계에서 표현할 수 없는 상위 비트가 남아 있는지 확인합니다. 허용되지 않으면 false를 반환합니다. 67행은 계속 비트를 제외한 숫자 조각을 해당 위치에 합칩니다. 68행은 가장 높은 비트가 0일 때 성공으로 끝납니다. 발췌 바로 뒤에는 정상 종료 표시 없이 반복이 끝난 경우 false를 반환하는 경로가 있습니다. 따라서 96 한 바이트만 있고 뒤의 01이 없는 데이터는 완성된 150으로 해석하지 못합니다. 연습으로 입력이 끝났는데 계속 비트가 1인 상황을 설명해 보세요. 단순 기본값 대입으로 복구하면 뒤 메시지 경계를 잘못 읽을 수 있으므로 실패를 상위 파서에 전달해야 합니다. 정상 계산과 악성·잘린 입력 검사는 같은 함수에서 함께 읽습니다.',
  sources: [repo(P.parser, 'FReader::ReadVarint', 57, 71)],
});
add(4, {
  title: 'float 0.5의 고정 32비트 표현',
  body: ['float 0.5의 IEEE 754 비트 패턴은 0x3F000000입니다.',
    '낮은 바이트부터 쓰면 00 00 00 3F입니다.',
    '스로틀 태그 15를 앞에 붙이면 15 00 00 00 3F입니다.',
    '교육용 단일 필드입니다. 완전한 운전 메시지나 수락 가능한 패킷은 아닙니다.'],
  notes: '고정 길이 값은 varint와 다르게 숫자를 7비트 조각으로 줄이지 않습니다. 현재 WriteFixed32는 float 값을 정수로 수치 변환하지 않고 메모리의 비트 패턴을 복사합니다. 0.5는 이진수 0.1로 정확히 표현할 수 있으며 float의 비트 패턴은 0x3F000000입니다. 이 정수 패턴에서 낮은 8비트씩 꺼내면 00, 00, 00, 3F가 됩니다. ControlCommand의 throttle은 field 2, wire type 5이므로 태그 0x15가 앞에 붙습니다. 전체 다섯 바이트는 스로틀 필드 하나의 표현일 뿐입니다. 실제 SerializeControlEnvelope는 다른 제어 필드와 출처·세션·지도를 포함한 Envelope까지 구성합니다. 연습으로 float를 int로 캐스팅해서 쓰면 어떤 값이 될지 생각해 보세요. 수치 변환은 0.5를 정수 0으로 만들 수 있으므로 원래 비트 패턴 전달과 다릅니다. 바이트 손계산을 실제 서버로 전송하는 실험으로 확대하지 않고 읽기 연습으로 사용합니다.',
  sources: [repo(P.parser, 'WriteFixed32', 24, 33), repo(P.parser, 'SerializeControlEnvelope', 934, 945)],
});
add(4, {
  type: 'code', title: '비트를 유지하는 float 쓰기',
  code: { path: P.parser, start: 24, end: 33 },
  body: ['Memcpy는 숫자를 반올림하지 않고 같은 비트 패턴을 복사합니다.',
    '반복문은 0·8·16·24비트 이동으로 네 바이트를 꺼냅니다.'],
  notes: '24행은 float 값을 받아 출력 배열에 씁니다. 26행의 Bits는 float와 같은 크기의 uint32이며, 27행의 static_assert는 이 크기 가정을 컴파일 때 검사합니다. 28행은 float 객체의 비트 표현을 Bits로 복사합니다. 29행부터 31행은 인덱스 0부터 3까지 반복하며 8비트씩 오른쪽으로 옮긴 부분을 한 바이트로 추가합니다. 이 순서가 낮은 바이트부터 쓰는 little-endian 표현을 만듭니다. 0.5 예제에서 마지막에 3F가 나오는 이유를 바로 이 인덱스 순서로 확인할 수 있습니다. 이 함수는 field 번호를 쓰지 않으므로 상위 호출자가 WriteTag를 먼저 호출해야 완전한 필드가 됩니다. 연습으로 태그를 빠뜨리고 네 바이트만 보냈다면 수신기는 첫 00을 무엇으로 해석하려 할지 생각해 보세요. 다음 필드의 태그로 읽기 시작하지만 0 태그는 유효하지 않아 실패합니다. 값 쓰기 함수와 필드 구성 함수를 구분하면 이런 경계 오류를 찾기 쉬워집니다.',
  sources: [repo(P.parser, 'WriteFixed32', 24, 33), repo(P.parser, 'FReader::ReadTag', 73, 82)],
});
add(4, {
  title: 'LEN과 UTF-8 길이',
  body: ['LEN은 태그 뒤에 길이 varint를 쓰고 그만큼의 바이트를 담습니다.',
    'Envelope source_id가 car라면 22 03 63 61 72입니다.',
    '길이는 글자 수가 아닌 UTF-8 바이트 수입니다.',
    '연습: 한글 글자 하나를 길이 1로 쓰면 왜 잘못될 수 있을까요?'],
  notes: 'source_id는 Envelope의 field 4 문자열이므로 태그 정수는 4×8+2=34이고 16진수로 22입니다. 교육용 문자열 car는 UTF-8에서도 세 바이트이므로 길이 03 뒤에 문자 코드 63, 61, 72가 이어집니다. 현재 WriteString은 FString을 FTCHARToUTF8로 변환하고 Utf8.Length를 씁니다. 이 길이는 화면에 보이는 글자 개수와 같은 개념이 아닙니다. 예를 들어 한글 가는 UTF-8에서 EA B0 80 세 바이트를 사용합니다. 길이를 1로 쓰면 수신자는 첫 바이트만 해당 문자열로 떼어 내고 남은 바이트를 다음 필드로 잘못 읽을 수 있습니다. 이 예는 문자열 인코딩 원리를 설명하며 실제 source_id는 프로젝트의 출력 가능한 ASCII 식별자 제한도 따릅니다. 따라서 한글 source_id를 보내는 동작이 허용된다는 뜻은 아닙니다. 길이 계산이 맞는 것과 애플리케이션에서 허용하는 문자열 조건은 다시 별도의 검사입니다.',
  sources: [repo(P.parser, 'WriteString', 42, 48), repo(P.proto, 'Envelope', 279, 280), repo(P.session, 'SimulationHost::handle_control_command', 321, 334)],
});
add(4, {
  type: 'code', title: '중첩 메시지를 담는 함수',
  code: { path: P.parser, start: 35, end: 40 },
  body: ['WriteBytes는 LEN 태그와 길이를 쓰고 원래 바이트를 붙입니다.',
    '완성한 Control 바이트를 Envelope의 11번 필드에 넣을 때 사용합니다.'],
  notes: '35행은 출력 배열, 필드 번호, 읽기 전용 바이트 뷰를 받습니다. 37행은 wire type 2로 태그를 씁니다. 38행은 Bytes.Num으로 payload의 바이트 길이를 varint로 기록합니다. 39행은 데이터 내용을 그 길이만큼 출력 배열 뒤에 복사합니다. 여기서 Bytes의 의미가 문자열인지 ControlCommand인지 이 함수가 판단하지는 않습니다. 상위 SerializeControlEnvelope가 Control 배열을 만든 뒤 field 11로 WriteBytes를 호출하므로 의미가 정해집니다. 예를 들어 앞에서 계산한 throttle 하나의 바이트가 5개라면 field 11 태그 5A와 길이 05를 앞에 붙인 교육용 중첩 조각을 만들 수 있습니다. 실제 송신 함수는 다른 필드도 함께 써서 길이가 달라집니다. 연습으로 길이를 실제보다 크게 적었을 때 수신기가 무엇을 확인해야 하는지 설명해 보세요. ReadMessage는 남은 바이트보다 길이가 크면 false를 반환합니다. 이 경계를 건너뛰면 다음 필드나 배열 밖 메모리를 잘못 읽을 수 있습니다.',
  sources: [repo(P.parser, 'WriteBytes', 35, 40), repo(P.parser, 'SerializeControlEnvelope', 947, 955), repo(P.parser, 'FReader::ReadMessage', 102, 109)],
});
add(4, {
  title: '중첩 바이트 손으로 풀기',
  body: ['교육용 조각: 5A 05 15 00 00 00 3F',
    '5A는 Envelope field 11, 05는 내부 길이 5바이트입니다.',
    '내부 15는 ControlCommand field 2의 float 태그입니다.',
    '필드 번호는 메시지마다 다시 해석합니다. 11번과 2번은 서로 다른 층입니다.'],
  notes: '이 페이지는 완전한 서버 요청이 아니라 중첩 구조를 읽는 계산 문제입니다. 첫 5A는 십진수 90이므로 8로 나누어 field 11과 wire type 2를 얻습니다. 다음 05는 내부 payload의 길이입니다. 다섯 바이트만 잘라 ControlCommand 파서에 전달한다고 생각하면 그 안의 첫 15는 다시 태그가 됩니다. 0x15는 field 2의 I32 값이며 남은 네 바이트를 float로 읽으면 0.5입니다. 따라서 바깥 Envelope에서 2번은 sequence라는 사실과 모순되지 않습니다. 각 메시지는 자체 필드 번호 공간을 가집니다. 전체 네트워크 데이터에서 2라는 숫자를 찾았다고 곧바로 sequence라고 부르면 안 되는 이유입니다. 연습으로 내부 길이를 04로 바꾸면 어떤 바이트가 밖에 남는지 적어 보세요. 마지막 3F가 중첩 메시지 밖에 남아 해석을 망가뜨립니다. 출처와 세션, schema_version, 제어 시간 등 필수 프로젝트 정보도 없는 이 조각을 실제 운전 가능 패킷으로 소개하지 않습니다.',
  sources: [repo(P.proto, 'Envelope::payload', 284, 290), repo(P.proto, 'ControlCommand', 234, 242), repo(P.parser, 'FReader::ReadMessage', 102, 109)],
});

add(5, {
  type: 'code', title: 'repeated와 21번 필드',
  code: { path: P.proto, start: 123, end: 126 },
  body: ['repeated는 WheelState를 여러 개 담을 수 있다는 뜻입니다.',
    '21은 목록의 필드 번호입니다. 바퀴 수나 바퀴 위치 번호가 아닙니다.'],
  notes: '123행의 선언을 왼쪽부터 읽으면 여러 원소를 담는 repeated, 원소 타입 WheelState, 필드 이름 wheels, 식별 번호 21로 나뉩니다. 뒤 주석은 기존 1번부터 21번 필드의 의미를 유지하면서 추가 정보를 붙이는 계약을 설명합니다. 목록에 몇 개가 들어 있는지는 메시지 내용과 애플리케이션 검증으로 결정합니다. Protobuf 선언만으로 바퀴가 항상 네 개라는 제약이 생기지는 않습니다. 현재 플레이어 차량 계약과 회귀검사는 네 WheelState의 상태를 확인하지만 여러 엔티티가 같은 구조를 쓸 때의 정책도 구분해야 합니다. 메시지형 repeated 원소는 각 중첩 메시지마다 태그와 길이를 가집니다. 숫자 목록에 사용하는 packed 표현을 모든 repeated에 똑같이 적용해서는 안 됩니다. 연습으로 wheels가 비어 있는 메시지에서 21이라는 숫자가 남아 바퀴 21개를 의미하는지 답해 보세요. 필드가 선언되어 있다는 사실과 실제 목록 원소 수는 별개입니다. 다음 페이지에서는 원소 안의 wheel_index를 별도로 읽습니다.',
  sources: [repo(P.proto, 'EntityState::wheels', 123, 126), repo(P.proto, 'WheelState', 52, 64), repo('cpp/host/tests/vehicle_protocol_test.cpp', 'test_world_state_envelope_roundtrip', 400, 407)],
});
add(5, {
  type: 'table', title: '바퀴 위치 번호의 계약',
  headers: ['wheel_index 값', '위치', '의미'],
  rows: [['0', '앞왼쪽', 'FL'], ['1', '앞오른쪽', 'FR'], ['2', '뒤왼쪽', 'RL'], ['3', '뒤오른쪽', 'RR']],
  body: ['목록 번호와 원소의 식별값을 혼동하지 않습니다.',
    '연습: wheels의 두 번째 원소에 wheel_index=3이면 어느 바퀴인가요?'],
  notes: 'WheelState 안의 wheel_index는 앞 페이지의 field number 21과 다른 숫자입니다. wheel_index 자체는 WheelState의 field 1이고 그 값으로 0부터 3까지 바퀴 위치를 표현합니다. 따라서 세 가지 숫자를 따로 적어 보면 혼동이 줄어듭니다. 바깥 목록을 식별하는 21, 내부 인덱스 필드를 식별하는 1, 실제 위치를 나타내는 값 3은 모두 다른 역할입니다. 연습 답은 뒤오른쪽 바퀴입니다. 단, 실제 소비자가 배열 순서를 가정하는지 명시적 인덱스로 찾는지까지 확인하면 더 정확하게 읽을 수 있습니다. 바이트 형식이 목록을 허용한다고 중복 인덱스나 잘못된 차종별 원소 수까지 모두 유효한 상태가 되는 것은 아닙니다. 이 표는 스키마 주석의 위치 계약이며 모든 엔티티가 반드시 같은 네 바퀴 시각 모델을 가진다는 선언은 아닙니다. 입력을 추적할 때는 차량과 바퀴의 ID를 먼저 고정한 뒤 하중이나 회전 속도를 비교하세요.',
  sources: [repo(P.proto, 'WheelState::wheel_index', 52, 64)],
});
add(5, {
  type: 'code', title: '바퀴 목록의 실제 수신',
  code: { path: P.parser, start: 542, end: 549 },
  body: ['21번 LEN 필드를 읽을 때마다 WheelState 한 개를 파싱해 추가합니다.',
    '32개 제한은 수신 방어 한도이며 정상 차량의 바퀴 수 선언이 아닙니다.'],
  notes: '542행은 EntityState의 21번 필드를 처리하는 분기입니다. 543행은 수신 상태에 너무 많은 원소가 쌓이지 않게 방어 한도를 검사합니다. 544행은 메시지형 값에 필요한 wire type 2를 요구하고, 545행은 정해진 길이의 내부 바이트 범위를 읽습니다. 546행은 바퀴 결과 구조체를 준비하고, 547행이 ParseWheel로 내부 필드를 해석합니다. 성공하면 548행에서 목록에 추가합니다. 어떤 단계라도 실패하면 현재 전체 해석을 성공으로 발표하지 않습니다. 이 코드만 보고 실제 자동차가 32륜이라고 해석하면 한도와 도메인 규칙을 혼동한 것입니다. 실습으로 21번 태그를 네 번 만났을 때 State.Wheels.Num이 어떻게 바뀌는지 적어 보세요. 각각 성공한다면 1, 2, 3, 4로 증가합니다. 같은 field number를 반복해서 만나는 것이 정상이라는 점 때문에 단일 필드의 중복 처리와 repeated 처리를 구분해야 합니다. 각 원소의 값 검증은 내부 파서와 후속 소비 조건까지 이어서 확인합니다.',
  sources: [repo(P.parser, 'ParseEntity', 542, 549), repo(P.proto, 'EntityState::wheels', 123, 123)],
});
add(5, {
  type: 'code', title: 'oneof의 payload 선택',
  code: { path: P.proto, start: 284, end: 290 },
  body: ['Envelope의 payload는 정의된 종류 중 하나를 활성 값으로 가집니다.',
    'WorldState 안의 여러 엔티티와 Envelope의 payload 선택은 다른 계층입니다.'],
  notes: '284행은 payload라는 oneof 선택 영역을 시작합니다. 285행부터 289행은 Hello, ControlCommand, WorldState, Health, SimulationReset 중 사용할 메시지 종류와 번호를 선언합니다. 한 Envelope에 여러 종류를 목록처럼 쌓으라는 선언이 아닙니다. 생성 API에서 다른 oneof 멤버를 설정하면 활성 멤버가 바뀝니다. 바이너리에 서로 다른 멤버가 여러 번 있으면 일반적인 oneof 해석은 마지막으로 선택된 멤버를 따르며, 현재 UE도 마지막 알려진 payload 종류를 추적해 WorldState인지 확인합니다. 그렇다고 모든 중복·모르는 미래 멤버 상황이 자동으로 안전하다는 뜻은 아닙니다. 수동 파서의 지원 범위와 애플리케이션 검증이 함께 필요합니다. 실습으로 WorldState에 NPC 열 대가 들어 있으면 payload가 열 개가 되는지 답해 보세요. 바깥 payload는 WorldState 하나이고 그 안의 entities 목록에 여러 상태가 있습니다. 바깥 선택과 내부 반복을 구분하는 것이 메시지 구조를 읽는 핵심입니다.',
  sources: [repo(P.proto, 'Envelope::payload', 284, 290), repo(P.parser, 'ParseWorldStateEnvelope', 1230, 1241), official.proto3],
});
add(5, {
  type: 'code', title: 'optional gear의 존재 여부',
  code: { path: P.proto, start: 237, end: 242 },
  body: ['optional gear는 필드가 없다는 상태와 값 0을 구분합니다.',
    '이 enum의 0은 Neutral이므로 부재와 구분하는 이유가 있습니다.'],
  notes: '237행과 238행은 브레이크와 조향 값을 정의하고, 239행은 핸드브레이크 불리언입니다. 240행에서 gear만 optional로 표시되어 명시적인 존재 여부를 추적할 수 있습니다. 다음 두 행은 비상 정지와 클라이언트 시각입니다. VehicleGear enum에서 0은 Neutral입니다. 따라서 필드가 없을 때 기본 getter 값만 읽으면, 송신자가 중립을 요청한 경우와 기어 정보를 보내지 않은 경우가 같은 숫자로 보일 수 있습니다. 현재 서버 decoder는 has_gear를 먼저 확인해서 이 차이를 처리합니다. 이 문법은 std::optional<T>라는 C++ 라이브러리 타입을 .proto에 그대로 넣은 것이 아닙니다. 생성된 API에 존재 여부를 확인할 기능을 제공하는 스키마 의미입니다. 연습으로 gear=0을 명시한 메시지와 gear를 생략한 메시지가 같은 운전 의도를 가진다고 가정해도 되는지 답해 보세요. 이 프로젝트에서는 둘을 구분하며, 실제 부재 시 동작은 다음 페이지의 VehicleInput 기본값과 분기를 함께 읽어야 합니다.',
  sources: [repo(P.proto, 'ControlCommand', 234, 243), repo(P.proto, 'VehicleGear', 5, 9), official.presence],
});
add(5, {
  type: 'code', title: 'gear 부재를 처리하는 코드',
  code: { path: P.decoder, start: 75, end: 83 },
  body: ['has_gear가 참일 때만 기어 값을 변환해 입력에 저장합니다.',
    '부재 시 현재 VehicleInput의 기본 Drive가 유지됩니다.'],
  notes: '75행부터 78행은 생성 클래스의 제어값을 내부 입력 구조체로 복사합니다. 79행의 has_gear는 값이 0인지 확인하는 조건이 아니라 실제 기어 필드가 존재하는지 확인하는 조건입니다. 참일 때 80행에서 프로토콜 enum을 내부 VehicleGear로 변환합니다. 거짓이면 대입을 건너뛰며 현재 VehicleInput의 기본 기어인 Drive가 남습니다. 83행은 준비한 parsed 객체를 결과 변형 타입에 담아 반환합니다. 여기의 기본 Drive는 프로젝트가 선택한 호환 동작입니다. Protobuf의 모든 enum 부재가 Drive가 된다는 일반 규칙은 아닙니다. 실제 test_missing_gear_keeps_default_drive 회귀는 바로 이 계약을 검사합니다. 연습으로 gear=Neutral을 명시한 경우를 추적해 보세요. has_gear가 참이므로 0 값을 변환하여 Neutral을 넣습니다. 또한 이 단계에서 파싱에 성공한 것과 실제 호스트가 운전을 수락한 것은 다릅니다. 테스트에 Hello가 없다고 해서 생산 통신도 Hello 없이 운전할 수 있다고 결론 내리지 마세요.',
  sources: [repo(P.decoder, 'parse_client_message_envelope', 75, 83), repo('cpp/host/src/physics/vehicle_physics.hpp', 'VehicleInput::gear', 37, 37), repo('cpp/host/tests/vehicle_protocol_test.cpp', 'test_missing_gear_keeps_default_drive', 952, 965)],
});
add(5, {
  title: '기본값 0과 정보 없음',
  body: ['proto3의 일반 숫자 필드가 없으면 getter는 기본값 0을 돌려줍니다.',
    'Health의 age=0만으로 방금 정상 입력을 받았다고 판단할 수 없습니다.',
    'has_control_command가 입력 수락 이력의 존재를 별도로 알려 줍니다.',
    '필드의 값과 그 값이 유효한 맥락을 함께 확인합니다.'],
  notes: '0은 많은 계약에서 유효한 숫자이므로 정보 없음의 표시로 무조건 쓰기 어렵습니다. Health의 last_command_age_ns가 0일 때는 실제 나이가 거의 0일 수도 있고 아직 정상 제어 입력을 한 번도 받지 않은 상태일 수도 있습니다. 현재 fill_health는 has_control_command가 거짓이면 age를 0으로 내보내고 별도 불리언에 존재 정보를 기록합니다. 이 때문에 UI나 검사 도구는 나이 숫자만 보고 정상 운전으로 판단하지 않아야 합니다. 스키마의 proto3 기본값과 애플리케이션이 추가한 유효성 플래그가 어떻게 만나는지 보여 주는 사례입니다. 연습으로 age=0, has_control_command=false, status=awaiting_control인 상태를 한 문장으로 설명해 보세요. 아직 정상 입력이 없고 제어를 기다리는 상태라는 답이 맞습니다. 반면 age=0과 플래그 참이라도 다른 상태 조건까지 확인해야 합니다. 기본값은 편리하지만 부재를 뜻하는지 실제 측정값인지 명확하게 정하지 않으면 화면에 잘못된 정상 표시를 만들 수 있습니다.',
  sources: [repo(P.proto, 'Health', 251, 262), repo(P.wire, 'fill_health', 160, 164), official.presence],
});
add(5, {
  type: 'table', title: '스키마 변경의 호환성',
  headers: ['변경', '위험', '검토할 점'],
  rows: [['새 번호에 필드 추가', '구 수신기가 의미를 모름', '기본 동작·unknown 처리'],
    ['기존 번호 재사용', '옛 기록을 다른 뜻으로 해석', '금지하고 reserved 검토'],
    ['기존 값의 단위 변경', '파싱은 되지만 의미가 달라짐', '버전·변환·검사'],
    ['새 필수 기능 요구', '구 클라이언트 협상 실패', 'capabilities 정책']],
  body: ['바이트 호환성과 기능·의미 호환성을 각각 확인합니다.'],
  notes: '새 필드를 추가하면 일반적인 Protobuf 수신기는 모르는 필드를 건너뛸 수 있으므로 바이너리 형식의 확장에 도움이 됩니다. 그러나 그 필드를 해석해야만 안전한 기능이라면 구 클라이언트를 그대로 허용할지 별도 정책이 필요합니다. 반대로 기존 field 번호를 다른 뜻에 재사용하면 과거 기록이나 다른 버전이 같은 바이트를 엉뚱하게 이해할 수 있습니다. 단위를 바꾸는 경우도 위험합니다. speed의 저장 타입을 float로 유지한 채 의미를 m/s에서 km/h로 바꾸면 파서는 성공하지만 주행과 HUD가 잘못될 수 있습니다. 연습으로 field 40 진단 값 추가와 field 9 speed 단위 변경을 비교해 보세요. 둘 다 파일 한 줄 수정처럼 보여도 영향은 다릅니다. 현재 스키마 주석은 1번부터 21번의 의미 유지와 추가 확장을 구분합니다. 삭제 필드의 번호와 이름을 reserved로 남기는 원칙은 미래 변경 시 적용할 일반 지침이며, 지금 교재가 실제 스키마를 수정한다는 뜻은 아닙니다.',
  sources: [repo(P.proto, 'EntityState', 123, 151), repo(P.session, 'SimulationHost::handle_hello', 248, 259), official.proto3],
});
add(5, {
  type: 'code', title: '알 수 없는 필드 건너뛰기',
  code: { path: P.parser, start: 130, end: 138 },
  body: ['값의 길이를 wire type으로 알아내어 다음 필드 위치로 이동합니다.',
    '현재 UE는 0·1·2·5를 지원하며 다른 wire type은 실패합니다.'],
  notes: '이 발췌는 FReader::Skip의 본문입니다. 132행은 varint 값을 끝까지 읽고 결과를 사용하지 않습니다. 133행은 고정 8바이트가 남아 있는지 확인한 뒤 읽기 위치를 옮깁니다. 134행은 LEN의 길이를 읽어 해당 범위를 건너뜁니다. 135행은 고정 4바이트를 처리하고, 136행은 지원하지 않는 종류를 실패로 돌려줍니다. 따라서 무시한다는 표현은 바이트를 무작정 버린다는 뜻이 아닙니다. 경계를 정확하게 계산해야 다음 필드부터 정상 해석을 계속할 수 있습니다. 현재 UE의 수동 파서는 건너뛴 값을 보관하여 다시 직렬화하는 기능을 제공하는 경로가 아닙니다. 생성된 Protobuf 메시지의 unknown-field 보존 동작과도 구분해야 합니다. 연습으로 알 수 없는 LEN 필드가 20바이트라고 적었는데 5바이트만 남아 있다면 성공해야 하는지 판단해 보세요. 길이가 유효하지 않아 실패해야 합니다. 호환을 위한 무시와 잘못된 입력까지 허용하는 동작은 서로 다른 것입니다.',
  sources: [repo(P.parser, 'FReader::Skip', 128, 138), repo(P.parser, 'FReader::ReadMessage', 102, 109), official.generated],
});
add(5, {
  type: 'code', title: '파싱 실패와 의미 검증',
  code: { path: P.decoder, start: 39, end: 43 },
  body: ['ParseFromArray 실패는 바이트에서 Envelope를 복원하지 못했다는 뜻입니다.',
    '복원이 성공해도 이어서 schema_version과 명령 조건을 검사합니다.'],
  notes: '39행은 생성된 Envelope 객체를 준비합니다. 40행의 ParseFromArray는 전달받은 바이트 범위에서 메시지를 읽습니다. 실패하면 41행에서 오류 설명을 기록하고 42행에서 nullopt를 반환합니다. 이 짧은 코드가 모든 입력 검증을 끝내는 것은 아닙니다. 바로 다음 부분은 schema_version이 현재 계약과 같은지 확인하고, 그 뒤 payload에 따라 내부 명령을 구성합니다. SimulationHost는 다시 지도 checksum과 수명·소유권을 검사합니다. 연습으로 바이트가 완전하지만 다른 지도의 정상 ControlCommand인 경우 이 다섯 줄에서 꼭 실패하는지 답해 보세요. 형식 복원은 성공할 수 있고 지도 검사 단계에서 거부될 수 있습니다. 테스트와 로그를 읽을 때 실패가 어느 층인지 구분해야 원인을 정확하게 좁힐 수 있습니다. 특히 parse_control_command_envelope 단위검사의 성공을 실제 네트워크 운전 수락으로 해석하지 마세요. 부분 함수 검사는 전체 생명주기 가운데 명시한 경계만 증명합니다.',
  sources: [repo(P.decoder, 'parse_client_message_envelope', 30, 52), repo(P.session, 'SimulationHost::handle_control_command', 321, 379)],
});
add(6, {
  type: 'code', title: 'Hello가 확인하는 기능',
  code: { path: P.session, start: 248, end: 257 },
  body: ['현재 생산 경로는 네 가지 필수 capability의 지원을 확인합니다.',
    'WebSocket 연결 성공 뒤에도 애플리케이션 Hello가 필요합니다.'],
  notes: '248행부터 253행은 호스트가 요구하는 기능 문자열 목록입니다. world-state.v2는 상태 규격, control.v2는 입력 규격, simulation-reset.v1은 Play 초기화, map-package-checksum.v1은 지도 식별 계약과 연결됩니다. 254행은 목록을 순회하고 255행에서 상대의 capability에 없는 항목을 찾습니다. 없으면 로그를 남기고 Rejected로 끝냅니다. 이 앞에는 source·session·build 문자열과 schema, 지도 checksum, 중복 capability 검증도 있습니다. WebSocket의 HTTP 업그레이드 handshake와 여기의 protobuf Hello는 서로 다른 층입니다. 첫 번째가 통로를 열어도 두 번째가 기능 계약을 확인하지 못하면 일반 제어를 허용하지 않습니다. 연습으로 socket connected 로그만 보고 차량이 움직여야 한다고 판단해도 되는지 답해 보세요. 후속 Hello와 reset, 정상 입력 수락까지 확인해야 합니다. 단위 통합에서 require_client_hello를 끌 수 있는 기본값이 있어도 main은 생산용으로 true를 설정하므로 실제 실행 경로를 기준으로 읽어야 합니다.',
  sources: [repo(P.session, 'SimulationHost::handle_hello', 224, 259), repo(P.main, 'host_config.require_client_hello', 343, 350)],
});
add(6, {
  title: '지도 이름과 checksum',
  body: ['지도 이름은 사람이 찾는 이름이고 checksum은 자료 일치 검사의 단서입니다.',
    '같은 레벨 이름이어도 bake 결과가 다르면 같은 세계라고 볼 수 없습니다.',
    '현재 호스트는 Envelope의 지도 checksum이 설정과 다르면 입력을 거부합니다.',
    'checksum 일치는 사용자 인증이나 충돌 없는 데이터의 증명이 아닙니다.'],
  notes: '클라이언트가 눈으로 보는 도로와 서버가 사용하는 지면·충돌 자료가 다르면 같은 좌표에서도 차량이 뜨거나 막힐 수 있습니다. 이 프로젝트는 관련 패키지 식별을 메시지에 실어 서로 같은 자료를 대상으로 동작하는지 확인합니다. handle_control_command는 받은 map_package_checksum과 현재 설정의 값을 비교하고 다르면 거부합니다. Hello와 WorldState 수신에도 지도 계약이 연결됩니다. 연습으로 레벨 파일 이름만 같게 바꾸면 mismatch가 해결되는지 답해 보세요. 이름 변경만으로 자료 내용과 검증된 패키지 정합이 맞춰지는 것은 아닙니다. 또한 이 식별값은 암호학적 인증이나 악의적 변조 방어 전체를 대신하지 않으며, 일치한다고 도로가 물리적으로 올바르게 설계되었다는 의미도 아닙니다. 지도 정합 오류가 발생하면 실제 실행한 패키지 경로와 내보낸 자료를 확인해야 합니다. 교재의 실습에서는 mismatch를 없애려고 검증 조건을 삭제하거나 checksum 문자열만 억지로 맞추지 않습니다.',
  sources: [repo(P.session, 'SimulationHost::handle_control_command', 330, 335), repo(P.session, 'SimulationHost::handle_hello', 230, 234), repo(P.proto, 'Envelope', 279, 282)],
});
add(6, {
  type: 'table', title: '출처·연결·Play 식별', imageKey: 'session',
  headers: ['이름', '구분하는 대상', '재연결 예'],
  rows: [['source_id', '누가 보낸 흐름인가', '동일 클라이언트 출처'],
    ['session_id', '어느 연결 수명인가', '새 연결이면 새 값'],
    ['play_session_id', '어느 Play 실행인가', '같은 Play면 유지'],
    ['connection_generation', '서버가 구분하는 소켓 세대', '이전 소켓 콜백 차단']],
  body: ['문자열 이름이 비슷해도 수명과 검증 목적은 서로 다릅니다.'],
  notes: '운전 중 연결이 잠깐 끊겼다가 복구되는 상황과 사용자가 End Play 후 다시 Play하는 상황을 나눠 생각해 보세요. 앞의 경우에는 같은 세계 상태를 유지하면서 새 연결에 정상 제어권을 넘기는 것이 필요합니다. 뒤의 경우에는 새 실행으로 상태를 초기화하는 요청이 필요합니다. 그래서 연결 식별자와 Play 식별자를 따로 둡니다. connection_generation은 Envelope 필드가 아니라 실제 WebSocket 연결에서 호스트에 전달하는 맥락 정보입니다. 숫자가 더 크다는 이유만으로 아무 요청이나 수락하지 않고 source와 session, Play 상태를 함께 검사합니다. 연습으로 같은 source_id를 쓰는 오래된 소켓이 최신 메시지를 늦게 보냈을 때 왜 추가 식별이 필요한지 설명해 보세요. 출처 이름만 같으면 이전 연결을 현재 운전자로 착각할 수 있습니다. 이 표를 보안 인증 체계 전체라고 소개해서는 안 됩니다. 현재 프로젝트의 수명 구분과 지연 메시지 차단 목적을 설명하는 표입니다.',
  sources: [repo(P.proto, 'Envelope', 275, 282), repo(P.session, 'SimulationHost::handle_control_command', 360, 368), repo(P.main, 'WsServer callback', 352, 364)],
});
add(6, {
  title: '같은 Play 재연결과 새 Play',
  body: ['같은 Play의 정상 재연결은 물리 상태를 보존하고 제어 연결을 교체합니다.',
    '새 Play의 정상 reset은 차량·NPC·구조물과 시뮬레이션 시간을 초기화합니다.',
    '이미 끝난 옛 Play의 지연 reset은 현재 상태를 덮어쓰면 안 됩니다.',
    '질문: 연결될 때마다 무조건 차량을 출발점으로 옮기면 어떤 문제가 생길까요?'],
  notes: 'handle_simulation_reset은 Play 식별자를 이미 본 적이 있는지 검사합니다. 같은 활성 Play이면 reset_for_reconnect로 새 연결 수명을 처리하고 차량 물리를 다시 만들지 않는 경로가 있습니다. 이때 차량 선택을 같은 Play 안에서 몰래 바꾸거나 이미 폐기한 연결을 다시 쓰는 요청도 거부합니다. 새로운 유효 Play라면 reset_player_vehicle와 런타임 엔티티 복구, 구조물 초기화, NPC와 보행자 재구성, 계산 시간 초기화를 진행합니다. 이미 보았지만 현재 활성 Play가 아닌 식별자라면 오래된 요청으로 거부하는 경계도 중요합니다. 연습 답은 운전 중의 짧은 통신 회복마다 차량과 사고 상태가 갑자기 초기화될 수 있다는 것입니다. 반대 오류도 있습니다. 새 Play인데 아무것도 초기화하지 않으면 이전 낙하나 사고 상태가 그대로 남습니다. 같은 함수 이름에 reset이 들어 있어도 어느 조건에서 물리 상태를 보존하거나 재설정하는지 분기별로 읽어야 합니다. 이 동작은 정상 협상과 지도 정합을 통과한다는 전제입니다.',
  sources: [repo(P.session, 'SimulationHost::handle_simulation_reset', 496, 535), repo(P.session, 'SimulationHost::handle_simulation_reset', 544, 569)],
});
add(6, {
  title: 'sequence와 거부 후 상태 보존',
  body: ['같은 유효 제어 흐름에서 sequence는 처리 순서를 나타냅니다.',
    '101을 수락한 뒤 100이 와도 더 최신 입력으로 취급하지 않습니다.',
    '거부한 높은 순번이 마지막 정상 순번을 갱신하면 안 됩니다.',
    'TCP 순서 보장만으로 이전 세션·중복·재전송 문제까지 해결되지 않습니다.'],
  notes: 'ControlLease::accept는 같은 세션의 이전 수락 순번과 새 순번을 비교합니다. 새 값이 이전 값 이하이면 StaleSequence로 거부합니다. 이 비교는 스로틀 크기나 수신한 패킷 길이와 관계없습니다. 특히 검증 도중 상태를 먼저 갱신하지 않는다는 점을 읽어야 합니다. 예를 들어 잘못된 미래 순번 10000을 거부하면서 highest_sequence를 바꾸어 버리면 그 뒤 정상 102도 오래된 입력으로 취급하는 오류가 생깁니다. 현재 코드는 검사 통과 후 수락 지표를 갱신합니다. 세션 계층에는 같은 Envelope 순번을 다른 payload로 재사용하는 경우 등 추가 규칙도 있으므로 단순 숫자 비교가 전체 규칙은 아닙니다. 연습으로 last accepted=101, invalid incoming=10000, valid incoming=102를 순서대로 적고 마지막 명령이 왜 수락 후보가 되어야 하는지 설명하세요. 다른 지도나 폐기 세션이라면 순번이 커도 거부된다는 전제까지 함께 적으면 정확한 답입니다.',
  sources: [repo(P.lease, 'ControlLease::accept', 106, 149), repo(P.session, 'SimulationHost::handle_client_message', 191, 214)],
});
add(6, {
  type: 'table', title: '서로 다른 시간값',
  headers: ['시간', '기준', '주요 용도'],
  rows: [['simulation_time_ns', '서버 고정 틱 누적', '상태의 계산 시점'],
    ['client_time_ns', '클라이언트 단조 시간', '입력 시간 진행 비교'],
    ['received_at', '서버 steady_clock', '제어권과 수신 간격'],
    ['EntityState.timestamp', 'Unix 초', '벽시계 진단']],
  body: ['다른 컴퓨터의 단조 시각을 직접 빼서 정확한 지연으로 단정하지 않습니다.'],
  notes: '시간값 이름에 time이 들어 있다고 모두 같은 원점을 가지지는 않습니다. simulation_time_ns는 시뮬레이션이 얼마나 진행했는지 나타내며 새 Play에서 초기화할 수 있습니다. client_time_ns는 UE가 FPlatformTime::Seconds를 나노초 단위로 바꾸어 보내는 값입니다. 서버의 received_at은 자기 steady_clock 기준 수신 시각입니다. 이 두 단조 시계는 서로 다른 컴퓨터에서 같은 원점이라고 가정할 수 없습니다. 그래서 현재 queue-age 추정은 직전 수신과 이번 수신 사이의 간격, 직전 클라이언트 시각과 이번 시각의 간격을 비교합니다. EntityState.timestamp의 Unix 초는 벽시계 진단에 사용하며 시스템 시계 정합의 영향도 받을 수 있습니다. 연습으로 60Hz의 한 틱을 밀리초로 계산해 보세요. 약 16.67ms입니다. 이 목표 간격이 실제 모든 계산이 그 안에 끝난다는 보장은 아닙니다. 시뮬레이션 시간과 실제 대기시간을 분리해야 높은 FPS에서도 늦은 반응이 생기는 이유를 설명할 수 있습니다.',
  sources: [repo(P.proto, 'Envelope', 275, 282), repo(P.proto, 'EntityState::timestamp', 92, 94), repo(P.client, 'USimCoreClientComponent::SendControl', 749, 759), repo(P.lease, 'ControlLease::accept', 118, 129)],
});
add(6, {
  type: 'table', title: 'ControlLease의 세 시간 기준',
  headers: ['현재 설정', '경계', '결과'],
  rows: [['입력 대기 추정 100ms', '초과한 입력', '오래된 명령 거부'],
    ['정상 입력 공백 250ms', 'soft timeout 초과', 'SafeStop, 소유권 유지'],
    ['정상 입력 공백 1000ms', 'hard timeout 초과', '세션 폐기, 재연결 요구']],
  body: ['정확한 경계 판정은 검사 함수가 실행될 때 이루어집니다.'],
  notes: 'lease는 제어권을 일정 조건에서만 유지하도록 하는 약속입니다. 현재 Signal City 설정에서 100ms는 오래 밀린 입력을 구별하는 추정 한도이고, 250ms와 1000ms는 마지막 정상 입력 이후의 공백에 대한 서로 다른 기준입니다. soft timeout은 안전 정지 상태로 바꾸되 기존 소유권을 유지하여 유효한 새 입력으로 회복할 수 있게 합니다. hard timeout은 기존 세션을 폐기하므로 정상 재연결 절차가 필요합니다. 코드 비교는 설정값을 초과하는지 사용하므로 정확히 250ms인 시점과 250ms를 넘은 시점도 구분할 수 있습니다. 서버 루프가 오래 막히면 정확한 250ms 순간에 검사를 실행할 수 없고, 다시 실행될 때 두 조건을 함께 발견할 수 있습니다. 연습으로 마지막 정상 입력 후 300ms에 검사가 실행됐고 다른 오류가 없다면 상태를 적어 보세요. SafeStop이며 hard timeout 전의 같은 정상 세션 입력으로 복구할 수 있습니다. 인터넷 연결 여부 하나만으로 운전 권한을 설명할 수 없는 이유입니다.',
  sources: [repo('cpp/host/config/signal_city_server.cfg', 'command_timeout_ms', 22, 24), repo(P.lease, 'ControlLease::update_timeout', 152, 180)],
});
add(6, {
  type: 'code', title: '입력 대기시간의 추정',
  code: { path: P.lease, start: 123, end: 132 },
  body: ['서버 수신 간격에서 클라이언트 생성 간격을 뺀 양수 부분을 씁니다.',
    '예: 서버 180ms, 클라이언트 20ms이면 추정 대기는 160ms입니다.'],
  notes: '123행은 서버가 직전에 정상 수락한 시각부터 이번 수신까지의 간격을 구합니다. 124행과 125행은 클라이언트가 보낸 두 시각의 차이를 나노초 간격으로 만듭니다. 126행부터 129행은 서버 간격이 더 크면 차이를 추정 대기로 쓰고, 그렇지 않으면 0으로 둡니다. 130행에서 설정 한도를 초과하면 ExcessiveQueueAge를 반환합니다. 앞부분에서는 클라이언트 시각이 거꾸로 갔는지도 확인하므로 이 계산만 떼어 쓰면 안 됩니다. 교육용 예에서 180-20=160ms는 현재 100ms 한도를 넘으므로 입력을 거부합니다. 이 값은 두 컴퓨터 시계를 직접 동기화하여 측정한 정확한 편도 네트워크 지연이 아닙니다. 수신과 생성 간격 변화로 밀림을 추정하는 프로젝트 정책입니다. 연습으로 서버와 클라이언트 간격이 모두 20ms라면 결과를 구해 보세요. 추정 대기는 0ms이지만 그것만으로 실제 네트워크 지연이 전혀 없다고 결론 내릴 수는 없습니다.',
  sources: [repo(P.lease, 'ControlLease::accept', 118, 132)],
});

add(7, {
  type: 'code', title: '현재 송신 경로 추적',
  code: { path: P.client, start: 754, end: 763 },
  body: ['PendingControl과 순번·출처·세션·지도를 Envelope로 묶습니다.',
    'Socket->Send의 마지막 true는 바이너리 전송을 선택합니다.'],
  notes: '이 발췌의 입력은 SetControl이 준비한 PendingControl입니다. 발췌 직전에는 연결과 지도 협상이 끝났는지 확인하고 client_time_ns를 채웁니다. 754행부터 759행은 SerializeControlEnvelope에 입력과 메시지 식별 정보를 넘깁니다. OutgoingSequence++는 이번 값으로 메시지를 만든 뒤 다음 순번을 준비합니다. 760행은 완성된 바이트 배열을 바이너리로 전송합니다. 761행부터 763행은 마지막으로 전송한 입력을 기록하고 변경 표시를 내립니다. 이 시점에 서버가 입력을 수락했다고 확인된 것은 아닙니다. 서버에서는 WsSession::do_read가 메시지를 전달하고 handle_client_message, decoder, handle_control_command, ControlLease의 검사를 거칩니다. 연습으로 W를 계속 누르면서 조향만 바뀌면 무엇이 새 메시지의 핵심 내용인지 적어 보세요. 이번 운전 요청과 새 순번·시각이 함께 중요합니다. Send 호출 성공만으로 해당 입력이 이미 물리 틱에 반영됐다고 말하지 않는 것이 정확한 왕복 설명입니다.',
  sources: [repo(P.client, 'USimCoreClientComponent::SetControl', 124, 137), repo(P.client, 'USimCoreClientComponent::SendControl', 749, 765), repo(P.socket, 'WsSession::do_read', 155, 172), repo(P.session, 'SimulationHost::handle_control_command', 375, 392)],
});
add(7, {
  type: 'code', title: '상태와 Health를 한 번에 담기',
  code: { path: P.wire, start: 443, end: 449 },
  body: ['서버는 같은 Envelope에 차량 상태와 Health를 함께 넣습니다.',
    '이후 NPC·신호·구조물 등을 채운 뒤 SerializeAsString으로 바이트를 만듭니다.'],
  notes: '443행은 이번 방송용 Envelope를 만듭니다. 444행은 순번과 지도 등 공통 metadata를 채우고, 445행은 payload를 WorldState로 선택합니다. 446행은 entities 목록에 상태 하나를 만들고 플레이어 차량의 값을 기록합니다. 447행부터 449행은 Health가 있을 때 같은 WorldState 안에 상태 진단을 넣습니다. 함수 뒤쪽은 런타임 엔티티와 신호·구조물 등을 검사하고 채운 뒤 바이트 문자열을 반환합니다. 같은 봉투에 넣는 이유는 화면 위치와 그 위치를 설명하는 건강 상태가 같은 순번을 공유하게 하기 위해서입니다. 현재 WebSocket 방송 큐는 전송 중인 상태와 최신 대기 상태를 중심으로 유지하므로 Health를 별도 상태 슬롯처럼 보내면 설명 대상 위치와 엇갈릴 위험이 있습니다. 실습으로 별도 Health가 WorldState를 대체해 버린 경우 화면에 무엇이 남을지 상상해 보세요. 정상 진단 메시지는 왔는데 새 위치가 빠질 수 있습니다. 원자적 스냅샷 계약은 데이터 형식과 큐 정책이 만나는 실제 설계 사례입니다.',
  sources: [repo(P.wire, 'serialize_world_state_envelope', 433, 449), repo(P.wire, 'serialize_world_state_envelope', 594, 599), repo(P.proto, 'WorldState', 222, 231), repo(P.socket, 'WsSession::send_initial_binary', 70, 80)],
});
add(7, {
  type: 'code', title: '검증한 상태만 화면에 반영',
  code: { path: P.client, start: 1099, end: 1107 },
  body: ['LatestState 저장은 파싱·지도·Play·순서 검사를 통과한 뒤에 수행합니다.',
    '상태 수신 시각과 엔티티 표시도 같은 수락 경계에 연결됩니다.'],
  notes: '이 코드를 읽을 때 1102행의 대입만 보면 언제든 새 바이트가 화면을 바꾸는 것처럼 오해할 수 있습니다. 앞부분에서 소켓 세대와 연결 상태, 조각 크기, protobuf 구조, 지도와 현재 Play, 순번을 확인합니다. 1099행은 정상 수신 간격 진단에 사용할 시각을 기록하고 1100행은 성능 기록기에 수락된 상태를 알립니다. 1101행은 수락 수를 증가시키며, 1102행은 Parsed를 LatestState로 이동합니다. 1103행과 1104행은 상태 나이와 존재 여부의 기준을 갱신합니다. 마지막 두 행은 런타임 엔티티와 신호 표시를 진행합니다. MoveTemp는 객체 내용을 넘기는 표현이며 네트워크 재전송이나 서버 물리 수정 명령이 아닙니다. 연습으로 현재 Play와 다른 WorldState가 도착했을 때 이 대입까지 와야 하는지 답해 보세요. 일반 위치 상태는 앞의 Play 경계에서 거부해야 합니다. 실제 코드는 별도의 전역 E-stop 상태 관찰 예외도 두므로 모든 payload를 무조건 같은 처리로 요약하지 않습니다.',
  sources: [repo(P.client, 'USimCoreClientComponent::ApplyBinaryMessage', 1048, 1108)],
});
add(7, {
  type: 'quiz', title: '바이트와 필드 종합 문제',
  body: ['1. uint64 sequence=150을 2번 필드에 넣으면 태그와 값 바이트는?',
    '2. 5A 05 15 00 00 00 3F에서 안쪽 float 값은 무엇인가요?',
    '3. repeated WheelState wheels=21의 21과 wheel_index=3의 3은?',
    '4. gear 필드 부재와 gear=Neutral 명시는 현재 서버에서 같은가요?'],
  notes: '이 문제는 메시지 구조와 필드 의미를 한 번에 연결하는 연습입니다. 첫 문제는 태그를 먼저 계산한 뒤 값 150을 varint로 나누세요. 두 번째는 바깥 LEN 길이를 확인하고 내부 다섯 바이트만 별도 메시지로 읽습니다. 세 번째는 바깥 필드 번호, 내부 인덱스 필드 번호, 실제 인덱스 값을 서로 다른 칸에 적으면 좋습니다. 네 번째는 schema의 enum 기본값만 보고 답하지 말고 has_gear 분기와 VehicleInput의 초기값을 근거로 판단하세요. 이 바이트 예시는 Envelope 전체의 정상 협상과 수락 조건을 생략한 교육용 조각입니다. 정확히 계산했다고 실행 중인 서버로 직접 전송하지 않습니다. 답안에는 각 숫자가 10진수인지 16진수인지 반드시 표시하세요. 0x10이라는 태그를 십진수 10으로 잘못 읽는 실수가 흔합니다. 문제를 풀다가 막히면 코드 전체 대신 WriteTag, WriteVarint, WriteBytes와 decoder의 has_gear만 다시 찾아보시면 됩니다.',
  sources: [repo(P.parser, 'WriteTag', 19, 22), repo(P.parser, 'WriteVarint', 9, 17), repo(P.proto, 'WheelState', 52, 64), repo(P.decoder, 'parse_client_message_envelope', 75, 83)],
});
add(7, {
  type: 'answer', title: '바이트 문제의 계산 과정',
  body: ['1. 태그 2×8+0=16=0x10, 값 150=96 01. 결과는 10 96 01입니다.',
    '2. 내부 field 2의 float 0.5입니다. 길이 05가 내부 범위를 정합니다.',
    '3. 21은 목록 식별 번호이고 3은 뒤오른쪽 바퀴의 위치값입니다.',
    '4. 부재는 현재 기본 Drive 유지, 명시한 Neutral은 Neutral로 변환합니다.'],
  notes: '첫 답에서 0x10은 sequence 값이 아니라 field 2의 VARINT 태그입니다. 뒤의 0x96은 아래 7비트 값 22와 계속 비트를 합친 것이고 0x01은 128 자리 값 1입니다. 따라서 값은 22+128=150입니다. 두 번째는 5A를 field 11의 LEN으로 해석한 뒤 다섯 바이트를 떼어 냅니다. 내부 태그 15는 field 2의 I32이고 00 00 00 3F는 float 0.5의 비트 표현입니다. 세 번째는 메시지마다 번호 공간이 다르다는 점까지 설명하면 좋습니다. wheel_index라는 필드 자체는 WheelState의 1번입니다. 네 번째는 Protobuf 규칙과 프로젝트 정책을 나눈 답입니다. presence가 두 경우를 구분할 수 있게 하고 현재 decoder가 부재 시 내부 기본값을 유지합니다. 답안에 이 구분이 없다면 단순 암기에 가까울 수 있습니다. 틀린 문제는 값만 고치지 말고 자신이 어떤 층의 번호나 기본값을 섞었는지 한 줄로 기록하세요. 그 기록이 다음 복습 범위를 정해 줍니다.',
  sources: [repo(P.parser, 'WriteVarint', 9, 17), repo(P.parser, 'WriteFixed32', 24, 33), repo(P.decoder, 'parse_client_message_envelope', 75, 83), repo('cpp/host/src/physics/vehicle_physics.hpp', 'VehicleInput::gear', 37, 37)],
});
add(7, {
  type: 'quiz', title: '연결과 제어권 판단 문제',
  body: ['1. 같은 Play의 정상 새 연결은 차량 물리를 초기화하나요?',
    '2. 정상 입력 후 300ms, 1100ms에 검사가 실행되면 각각 어떤 상태인가요?',
    '3. 수신 간격 180ms, 생성 간격 20ms인 입력은 현재 한도를 통과하나요?',
    '4. 올바른 바이트·높은 순번이지만 지도 checksum이 다르면 수락하나요?'],
  notes: '각 문제는 다른 전제 오류가 없다는 조건에서 푸세요. Hello와 지도 계약이 정상이라는 말은 E-stop이 걸려 있거나 이전 세션이 폐기된 경우까지 생략해도 된다는 뜻은 아닙니다. 첫 문제는 연결과 Play를 구분하는 연습이며 새 연결이라는 이유만으로 초기화를 선택하지 않아야 합니다. 두 번째는 설정값을 초과한 시점에서 timeout 검사가 실제로 실행됐다는 조건을 넣었습니다. 세 번째는 한 번의 수신 시각과 생성 시각을 직접 뺀 값이 아니라 연속된 간격의 차이를 계산하는 문제입니다. 네 번째는 데이터 해석과 수락 정책을 구분하는 문제입니다. 답을 적을 때 단순히 연결됨이나 끊김 대신 유지하는 상태와 폐기하는 상태를 써 보세요. 예를 들어 물리 상태, 제어 소유권, 마지막 정상 입력 시각은 같은 상태가 아닙니다. 실제 테스트의 결과 enum을 찾아 같은 의미의 단어와 연결하면 설명이 더 정확해집니다. 생산 서버를 멈추거나 임의 패킷을 보내지 않아도 풀 수 있는 문제입니다.',
  sources: [repo(P.session, 'SimulationHost::handle_simulation_reset', 496, 535), repo(P.lease, 'ControlLease::accept', 118, 132), repo(P.lease, 'ControlLease::update_timeout', 152, 180), repo(P.session, 'SimulationHost::handle_control_command', 330, 335)],
});
add(7, {
  type: 'answer', title: '연결 문제의 판단 근거',
  body: ['1. 물리를 보존하고 새 연결을 묶습니다. 정상 입력은 다시 필요합니다.',
    '2. 300ms는 SafeStop, 1100ms는 세션 폐기와 재연결 요구입니다.',
    '3. 추정 160ms가 100ms 한도를 초과하므로 거부합니다.',
    '4. 지도 정합 검사에서 거부합니다. 높은 순번이 이를 우회하지 못합니다.'],
  notes: '첫 답의 보존은 차량이 즉시 정상 조종을 계속한다는 뜻과 다릅니다. 연결을 넘긴 뒤 새 정상 입력을 기다리는 단계가 있으며, 기존의 지연 메시지를 현재 입력으로 쓰지 않아야 합니다. 두 번째에서 SafeStop은 소유권 유지와 함께 안전 입력을 적용하는 구간입니다. hard timeout이 지나면 기존 세션을 폐기하므로 같은 방식의 즉시 회복을 기대할 수 없습니다. 세 번째의 160ms는 queue-age 정책이 계산한 추정치라는 표현까지 포함해야 정확합니다. 네 번째에서는 파싱이 성공했더라도 코드가 현재 지도 문자열과 비교하여 거부한다는 근거를 적으면 됩니다. 이 네 답을 바탕으로 사용자가 인터넷은 정상인데 조작이 안 된다고 말할 때 무엇부터 볼지 설명해 보세요. 전송 통로, Hello·지도·Play, 상태 신선도와 입력 수락을 구분하여 확인해야 합니다. 안전 한도를 무작정 크게 늘리는 것은 이 교재의 해결책이 아닙니다. 실제 로그와 느린 코드 경로를 찾아 정상 한도 안에서 처리하도록 수정하는 문제는 후속 권의 사례 학습에서 이어집니다.',
  sources: [repo(P.lease, 'ControlLease::update_timeout', 152, 180), repo(P.session, 'SimulationHost::handle_simulation_reset', 509, 535), repo(P.guide, '5-3. 안전 정지와 강제 재접속은 다릅니다', 673, 702)],
});
add(7, {
  type: 'code', title: '회귀검사를 설명하는 실습',
  code: { path: 'cpp/host/tests/vehicle_protocol_test.cpp', start: 954, end: 965 },
  body: ['준비·실행·검사·막으려는 회귀를 각각 한 문장으로 적습니다.',
    '이 검사는 gear 부재의 디코딩 정책을 확인하며 전체 연결 수락 검사가 아닙니다.'],
  notes: '954행과 955행은 Envelope와 현재 schema_version을 준비합니다. 956행은 throttle만 쓰므로 gear는 명시하지 않습니다. 958행부터 960행은 오류 문자열을 준비하고 직렬화한 바이트를 decoder에 다시 전달합니다. 962행은 파싱 결과가 존재하는지 확인하고 963행과 964행은 내부 기어가 Drive인지 검사합니다. 준비는 기어 없는 제어 메시지, 실행은 직렬화와 디코딩, 검사는 결과 존재와 기본 Drive, 막으려는 회귀는 부재를 Neutral 명시로 오인하는 변경이라고 설명할 수 있습니다. 이 코드에는 실제 WebSocket 연결과 Hello, 지도 검증을 전부 진행하는 과정이 없습니다. 따라서 통과해도 생산 서버가 어떤 출처의 명령이든 수락한다고 결론 내릴 수 없습니다. 실습 답안을 쓸 때 무엇을 검증하지 않는지도 한 문장 추가해 보세요. 그 문장이 있으면 테스트의 범위를 스스로 이해하고 있다는 근거가 됩니다. 여기서는 기존 테스트를 읽으며 기대값을 낮추거나 테스트 소스를 수정하지 않습니다.',
  sources: [repo('cpp/host/tests/vehicle_protocol_test.cpp', 'test_missing_gear_keeps_default_drive', 952, 965)],
});
add(7, {
  type: 'table', title: '다음 복습에서 찾을 함수',
  headers: ['복습 질문', '먼저 찾을 심볼', '완료 기준'],
  rows: [['입력은 어디서 바이트가 되나요?', 'SerializeControlEnvelope', '태그·값·겉봉투 설명'],
    ['값 부재는 어떻게 처리하나요?', 'parse_client_message_envelope', 'has_gear 분기 설명'],
    ['오래된 입력은 왜 거부하나요?', 'ControlLease::accept', '순서·간격 검사 설명'],
    ['새 연결과 새 Play는?', 'handle_simulation_reset', '보존·초기화 분기 설명'],
    ['화면 상태는 언제 바뀌나요?', 'ApplyBinaryMessage', '검증 후 저장 위치 설명']],
  body: ['책을 덮고 왕복을 설명한 뒤 막힌 심볼만 다시 찾아봅니다.'],
  notes: '복습은 처음부터 모든 슬라이드를 다시 읽는 방식보다 자신이 설명하지 못한 연결을 찾는 방식으로 진행합니다. 먼저 빈 종이에 입력, 제어 메시지, 서버 검증, 계산 결과, 화면 반영을 적고 각 단계에 실제 함수 이름을 하나씩 연결하세요. 그다음 필드 번호와 값, 존재 여부와 기본값, 연결과 Play의 차이를 예시 하나로 설명합니다. 바이트 계산은 150의 varint와 스로틀 0.5의 고정값을 다시 손으로 풀면 충분합니다. 마지막에는 단위검사와 실제 Play 관찰의 범위를 나눠 적습니다. 현재 교재는 인증·암호화 설계 전체나 모든 Protobuf 언어 기능, Unreal 수동 파서의 범용 완전성을 다루지 않습니다. 또한 통신 계약을 이해했다고 자동차 물리와 NPC 판단 전체를 익힌 것은 아닙니다. 다음 권에서는 이 기초를 사용해 Unreal의 입력·표시 수명과 서버 계산 사례를 더 깊게 읽을 수 있습니다. 자신의 설명에서 확신이 없는 부분은 지우지 말고 확인할 함수 이름으로 바꾸어 남겨 두세요.',
  sources: [repo(P.parser, 'SerializeControlEnvelope', 934, 955), repo(P.decoder, 'parse_client_message_envelope', 30, 83), repo(P.lease, 'ControlLease::accept', 75, 149), repo(P.session, 'SimulationHost::handle_simulation_reset', 496, 569), repo(P.client, 'USimCoreClientComponent::ApplyBinaryMessage', 1048, 1108)],
});

export default { id: '01', title: '공통 기초와 Protobuf',
  subtitle: 'C++ 코드를 읽고 입력·바이트·세션의 왕복을 설명하는 독학 교재', chapters, slides };
