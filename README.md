# Drive Integration

Unreal Engine을 영상 생성·입력·센서 환경으로 사용하고, 외부 C++ SimCore가 차량 물리를 계산하는 운전 시뮬레이션 프로젝트다. 1차 목표는 Wall Street·Broad Street 주변 수동운전이며, 이후 동일한 제어 프로토콜 위에 Python 자율주행 서버를 추가한다.

## 현재 상태

- C++ 자체 차량 물리 기초 모델: `GroundQuery`·평지 구현과 바퀴별 1D 서스펜션 기반까지 구현
- WebSocket binary + Protobuf 제어·상태 통신: C++ host와 Unreal client 구현
- Python relay: R1에서 동결한 선택 기능; ZMQ 기본 OFF, 구 `EntityStatePacket` observer만 보존
- Unreal 외부 차량 Pawn·수동 입력·상태 표시: 최소 통합 구현
- Wall/Broad MapPackage: 구현 전
- 자율주행 학습·추론: 8월 범위 제외

상세 범위와 일정은 [프로젝트 문서](./docs/README.md)를 기준으로 한다.

## 디렉터리

```text
Drive_Integration/
├── protocol/                 # 언어 간 공통 Protobuf 원본
├── cpp/host/                 # 권한 물리 서버와 통신
├── python/relay_server/      # observer/debug relay
├── python/autonomy_server/   # 향후 자율주행 서버 위치
├── unreal/DriveIntegration/  # Unreal Engine 5.6 C++ 프로젝트
├── map_packages/             # 공통 지도·차선·충돌 패키지
└── docs/                     # 일정, 기능표, 아키텍처, ADR, 작업일지
```

`protocol/vehicle.proto`가 유일한 Proto 원본이다. C++ 생성물은 빌드 디렉터리에 만들고 Python 생성물은 `python/relay_server/generated/`에 둔다.

## C++ host

필수 도구는 Git, CMake 3.21 이상, C++20 컴파일러다. 의존성은 프로젝트 로컬 vcpkg로 준비한다.

macOS/Linux:

```bash
bash cpp/host/scripts/setup.sh
bash cpp/host/scripts/build.sh
cd cpp/host && ctest --preset release
```

Windows PowerShell:

```powershell
cpp\host\scripts\setup.ps1
cpp\host\scripts\build.ps1
cd cpp\host
ctest --preset release
```

실행 파일은 macOS/Linux에서 `cpp/host/build/simcore_publisher`, Visual Studio 기반 Windows 빌드에서 `cpp/host/build/Release/simcore_publisher.exe`에 생성된다.

## 선택적 Python observer(동결)

Python relay는 R1 수동운전의 개발·실행·시험 대상이 아니다. 기본 C++ 빌드는 ZeroMQ를
컴파일하거나 5555 포트를 열지 않는다. 과거 디버그 observer를 명시적으로 확인할 때만
`release-zmq-observer` preset으로 C++ host를 별도 빌드한 뒤 아래 명령을 사용한다.

```bash
cmake --preset release-zmq-observer -S cpp/host
cmake --build cpp/host/build-zmq-observer --config Release --parallel
```

macOS/Linux:

```bash
python3 -m venv python/relay_server/.venv
python/relay_server/.venv/bin/python -m pip install -r python/relay_server/requirements.txt
python/relay_server/.venv/bin/python -m python.relay_server
```

Windows PowerShell:

```powershell
py -3.10 -m venv python\relay_server\.venv
python\relay_server\.venv\Scripts\python.exe -m pip install -r python\relay_server\requirements.txt
python\relay_server\.venv\Scripts\python.exe -m python.relay_server
```

Proto를 변경한 경우에만 macOS/Linux에서
`bash python/relay_server/scripts/generate_proto.sh`, Windows에서
`python\relay_server\scripts\generate_proto.ps1`을 실행한다.
`make test-python`은 relay 회귀 시험과 생성물 동기화를 함께 검증한다.

Unreal은 C++ host의 `ws://<host>:9000`에 직접 연결한다. 선택적 relay는
`tcp://127.0.0.1:5555`의 버전 없는 구 `EntityStatePacket`을 읽는 보존 코드이며,
schema-v2 `Envelope{WorldState}` 계약이나 R1 완료 게이트로 간주하지 않는다. Python
자율주행은 R2에서 별도 통신 계약을 결정한 뒤 다시 시작한다.

## 개발 원칙

- C++ SimCore만 최종 차량 물리 상태를 결정한다.
- 수동 입력과 향후 자율주행 명령은 같은 `ControlCommand`를 사용한다.
- 지도별 데이터는 코드에 넣지 않고 버전이 있는 MapPackage로 분리한다.
- 빌드 결과, vcpkg checkout, Python 가상환경, Unreal 생성물은 커밋하지 않는다.
- 기능 완료와 기술 결정은 `docs/`의 기능표·아키텍처·ADR·작업일지에 반영한다.
