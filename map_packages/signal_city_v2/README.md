# signal_city_v2 — 다중 교차로 신호 도심 blockout

Unreal `/Game/SignalCity/Maps/L_SignalCity`에서 측정한 로컬 ENU MapPackage다. 기존
`L_VirtualCity`/`virtual_city_v1`을 교체하지 않으며, 두 교차로의 독립 signal controller와
비정형 외곽 loop·중앙 avenue를 검증하기 위한 별도 코스다. 최종 도시 아트나 실지리
데이터로 간주하지 않는다.

실행·생성·수동 인수 절차는
[signal city 빠른 시작](../../docs/signal_city_quickstart.md), traffic schema와 fail-closed
규칙은 [차선·신호 계약](../../docs/traffic_network_signals.md)을 따른다.

## 검증된 생성물

2026-09-02 재생성 후 두 commandlet의 `-ValidateOnly` exact 검증을 통과한 값이다.

| 항목 | 값 |
|---|---:|
| map identity | `signal_city_v2` |
| collision checksum | `fnv1a64:7446108adad3e25b` |
| traffic network checksum | `fnv1a64:1fb2c67092bb4aff` |
| 저장 scene box | 227 |
| Ground component | 124 |
| static OBB collider | 49 |
| QA route checkpoint | 394 |
| heightfield | `401 × 481`, 50cm spacing |
| traffic lane | 42 |
| runtime signal head | 8 |
| signal controller | 2 |

Collision checksum은 아래 collision payload의 현재 raw bytes identity다. 이후 정상 Bake로
payload가 바뀌면 값이 바뀌어야 하며 이 표나 manifest 문자열만 손으로 고쳐 맞추지 않는다.
traffic JSON의 `source_map_checksum`은 이 값과 일치한다. Traffic network checksum은 JSON
전체 바이트의 별도 identity이므로 공백·키 순서까지 달라지면 함께 바뀐다.

## 파일 책임

| 파일 | 책임 |
|---|---|
| `manifest.cfg` | map identity, 좌표계, collision 파일 순서와 FNV-1a64 checksum |
| `ground_heightfield.bin` | Unreal collision에서 측정한 `SIMGHF2` 지면 높이·normal·재질·마찰 snapshot |
| `ground_surface.csv` | heightfield package임을 표시하는 legacy sentinel |
| `static_colliders.csv` | 건물·연석 등 명시적 수평 OBB 정적 충돌체 |
| `drive_route.csv` | 생성 geometry의 한 바퀴 정합을 확인하는 QA checkpoint; 물리 경로나 traffic graph가 아님 |
| `traffic_network.json` | collision checksum에 정합된 format version 2 lane·signal plan sidecar |

MapPackage manifest의 `format_version=1`과 traffic JSON의 `format_version=2`는 서로 다른
파일 계약의 version이다. 전자를 traffic v1로 해석하거나, traffic sidecar를 collision
payload 목록에 임의로 추가하지 않는다.

## 저작·runtime 경계

- geometry 원본은 `SimCoreSignalCityLayout`, traffic 원본은
  `SimCoreSignalCityTrafficLayout`이다. 저장 map과 JSON을 서로 독립적으로 손 편집해 원본과
  다른 상태로 만들지 않는다.
- Unreal exporter는 저장 asphalt의 중심·좌우 폭, signal pole base와 authored geometry를
  검증한 뒤 collision snapshot과 traffic JSON을 생성한다.
- C++ server만 phase plan과 simulation clock으로 authoritative aspect/countdown을 계산한다.
  Unreal의 runtime signal head는 `NoCollision` 표시 actor이며 로컬 주기를 만들지 않는다.
- controller 1과 2는 각각 36초 cycle과 0초·9초 offset을 사용한다. 서로 다른 controller는
  동시에 녹색일 수 있고, 같은 controller의 상충 group은 동시에 permissive일 수 없다.
- `cpp/host/config/signal_city_server.cfg`의 NPC는 두 교차로를 지나는 고정 loop 한 대다.
  다중 NPC traffic director나 일반 경로 계획을 뜻하지 않는다.

## 재생성 규칙

정상 변경 순서는 **map 저장 → Ground Bake → traffic export/update → map·traffic
ValidateOnly**다.

- 기존 map 검증·Bake에는 `BuildVirtualCity -SignalCity -ValidateOnly` 또는
  `-BakeOnly`를 사용한다. v1 전용 `-Sync*`와 일반 `-Replace`는 지원하지 않는다.
- `traffic_network.json` 최초 생성은 파일이 없을 때만 허용한다. 기존 파일은
  `ExportVirtualCityTraffic -SignalCity -ValidateOnly -nowrite`로 검증하거나, Ground Bake
  뒤 `-UpdateExisting -nowrite`로 명시적으로 갱신한다.
- traffic exporter는 map·Ground package·static collision을 변경하지 않는다.
- launcher는 필수 payload와 traffic JSON을 preflight하고 port를 점유한 다른 서버를
  종료하지 않는다.

전체 복사 가능한 명령은
[빠른 시작의 빌드와 생성 순서](../../docs/signal_city_quickstart.md#빌드와-생성-순서)에만
유지한다.

## 검증 경계와 한계

재생성 결과는 다음 로그에서 확인했다.

- `runtime_logs/ue-signal-city-create-20260902-r4.log`
- `runtime_logs/ue-signal-city-traffic-export-20260902-r4.log`
- `runtime_logs/ue-signal-city-map-validate-20260902.log`
- `runtime_logs/ue-signal-city-traffic-validate-20260902.log`
- `runtime_logs/health-smoke-20260902-105402-759a8176.*.log`
- `runtime_logs/traffic-smoke-20260902-110742-ed34c190.*.log`
- `runtime_logs/traffic-smoke-20260902-110839-be182f55.*.log` (v1 회귀)

별도 임시 `cpp/host/build_signal_city/Release/simcore_publisher.exe` link와 랜덤 localhost
port의 Health smoke는 새 package checksum·Hello/reset·lease/reconnect 경로를 확인했다.
약 975MB의 임시 build는 검증 뒤 정리했고 결과 로그는 보존했다. 이어진
38초 traffic smoke는 2,281 atomic WorldStates, 8 heads/4 groups, controller 내부 충돌 없음,
controller 간 simultaneous permission과 reset/soft·hard timeout/reconnect를 확인했다. 시작
진단도 controller 1·2의 36초 cycle과 0초·9초 offset을 출력했고, 같은 최신 publisher의 v1
회귀는 기존 30초 주기를 확인했다.
이후 기존 v1 PID 9604를 종료하고 표준 Release publisher를 최신 소스로 재빌드했다.
PowerShell launcher의 multiline `if` parse 오류도 중첩 `if`로 수정했으며, 수정된 launcher는
2026-09-02 11:25:06에 PID 23880을 `127.0.0.1:9000`에 시작했다. 실제 시작 로그
`runtime_logs/simcore-signal-city-20260902-112506-5abc9230.stdout.log`는 map checksum
`7446108adad3e25b`, traffic checksum `1fb2c67092bb4aff`, lanes42/heads8,
controller cycle36·offset0/9, lane NPC route 8 lanes/578.211m를 확인한다. 실제 PIE를 통과했다는
뜻은 아니다.

이 자동 검증은 실제 화면과 운전 감각의 합격 증거가 아니다. 두 교차로 head의 위치·가독성,
NPC 적색 정지/녹색 재출발, Ego 전체 loop, reconnect/all-red, 1920×1080 60fps와 30분
안정성은 PIE에서 별도로 인수한다.

현재 heightfield는 단일 상단 표면이므로 overpass·터널 같은 다층 도로를 표현하지 않는다.
저작 도로 밖을 무한 주행 영역으로 보장하지 않으며, 지면 support가 없는 곳은 물리가
fail-closed할 수 있다. 보행자 신호·횡단 AI, Ego 자동 신호 준수, 다중 NPC 교통량과 최종
환경 아트도 이 package의 완료 범위가 아니다.
