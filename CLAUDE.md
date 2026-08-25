# 프로젝트 목적/용도
- host(cpp): Unreal 또는 향후 Python 자율주행 입력을 받아 자체 차량 물리를 계산하는 권한 서버
- relay_server(python): R1에서 동결한 선택형 메시지 관찰 도구. 기본 수동운전 빌드·실행·시험에서는 제외
- Unreal: 운전자 입력, IG(영상 생성), UI, 센서, 도로·차량·보행자·신호 표현
- protocol: C++·Python·향후 Unreal이 함께 사용하는 공통 protobuf 계약

# 빌드/실행 명령어

## C++ host

- 의존성 준비(macOS/Linux): `bash cpp/host/scripts/setup.sh`
- 빌드(macOS/Linux): `bash cpp/host/scripts/build.sh`
- 테스트: `cd cpp/host && ctest --preset release`
- Windows: `cpp/host/scripts/setup.ps1`, `cpp/host/scripts/build.ps1`

## Python observer(동결·선택)

- C++ 선택 빌드: `cmake --preset release-zmq-observer -S cpp/host`
- 명시적 실행: `make run-python`
- Proto 재생성: `make generate-proto`

# 개발 컨벤션이나 주의사항

- 공통 Protobuf 원본은 `protocol/vehicle.proto` 하나만 유지한다.
- C++ Protobuf 생성물은 `cpp/host/build/`에만 생성한다.
- Python Protobuf 생성물은 `python/relay_server/generated/`에 둔다.
- `cpp/host/build/`, `cpp/host/vcpkg/`, Python `.venv`, Unreal 생성물은 커밋하지 않는다.
- 수동운전 runtime과 기본 `make test`는 Unreal/C++만 대상으로 하며 Python/ZMQ는 기본 OFF다.
- 동결된 Python relay는 opt-in legacy observer 확인에만 사용하고 R1 완료 근거로 삼지 않는다.
- 향후 자율주행 코드는 `python/autonomy_server/`로 분리하고 relay에 혼합하지 않는다.

# Team B
